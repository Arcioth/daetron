#define _GNU_SOURCE
#include "run.h"
#include "nl.h"
#include "rules.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define MAX_PENDING 16
#define MAX_USB 128

static int scan_usb_products(char prod[][64], int max);
static int usb_suppressed(const struct rule *r, char prod[][64], int nprod);

static volatile sig_atomic_t got_hup;
static volatile sig_atomic_t got_term;
static int dry, verbose;
static int wake_rd = -1, wake_wr = -1;
static struct ruleset rs;
static int lockfd = -1;

struct pending {
	int used;
	int rule_i;
	char devpath[256];
	unsigned long fire_at;
};

static struct pending pend[MAX_PENDING];
static char armed_name[MAX_RULES][64];
static int narmed;

static int is_armed(const char *name)
{
	int i;

	for (i = 0; i < narmed; i++) {
		if (!strcmp(armed_name[i], name))
			return 1;
	}
	return 0;
}

static void set_armed(const char *name)
{
	if (is_armed(name) || narmed >= MAX_RULES)
		return;
	snprintf(armed_name[narmed++], sizeof(armed_name[0]), "%s", name);
}

static void clear_armed(const char *name)
{
	int i, j;

	for (i = 0; i < narmed; i++) {
		if (strcmp(armed_name[i], name))
			continue;
		for (j = i + 1; j < narmed; j++)
			memcpy(armed_name[j - 1], armed_name[j], sizeof(armed_name[0]));
		narmed--;
		return;
	}
}

static void on_hup(int sig)
{
	char c = 'h';

	(void)sig;
	got_hup = 1;
	if (wake_wr >= 0)
		(void)!write(wake_wr, &c, 1);
}

static void on_term(int sig)
{
	char c = 't';

	(void)sig;
	got_term = 1;
	if (wake_wr >= 0)
		(void)!write(wake_wr, &c, 1);
}

static unsigned long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000UL +
	       (unsigned long)ts.tv_nsec / 1000000UL;
}

static const char *runtime_dir(void)
{
	const char *d = getenv("XDG_RUNTIME_DIR");

	return (d && d[0]) ? d : "/tmp";
}

static int pidfile_path(char *buf, size_t n)
{
	return snprintf(buf, n, "%s/daetron.pid", runtime_dir());
}

static int lock_path(char *buf, size_t n)
{
	return snprintf(buf, n, "%s/daetron.lock", runtime_dir());
}

static void write_pid(void)
{
	char path[256];
	FILE *f;

	pidfile_path(path, sizeof(path));
	f = fopen(path, "w");
	if (!f)
		return;
	fprintf(f, "%d\n", (int)getpid());
	fclose(f);
}

static void expand_cmd(char *dst, size_t n, const char *src)
{
	const char *home = getenv("HOME");

	if (!home)
		home = "";
	if (!strncmp(src, "$HOME/", 6) || !strncmp(src, "${HOME}/", 8)) {
		const char *rest = src[1] == '{' ? src + 8 : src + 6;

		snprintf(dst, n, "%s/%s", home, rest);
	} else if (!strncmp(src, "~/", 2)) {
		snprintf(dst, n, "%s/%s", home, src + 2);
	} else {
		snprintf(dst, n, "%s", src);
	}
}

static int comm_eq(const char *path, const char *want)
{
	char c[64];
	FILE *f = fopen(path, "r");

	if (!f)
		return 0;
	if (!fgets(c, sizeof(c), f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	c[strcspn(c, "\n")] = '\0';
	return !strcmp(c, want);
}

static int kill_comm(const char *name, int sig)
{
	DIR *d;
	struct dirent *ent;
	int n = 0;
	char path[320];

	d = opendir("/proc");
	if (!d)
		return 0;
	while ((ent = readdir(d))) {
		int pid;

		if (!isdigit((unsigned char)ent->d_name[0]))
			continue;
		snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);
		if (!comm_eq(path, name))
			continue;
		pid = atoi(ent->d_name);
		if (pid > 1 && !kill(pid, sig))
			n++;
	}
	closedir(d);
	return n;
}

static int comm_running(const char *name)
{
	DIR *d;
	struct dirent *ent;
	char path[320];
	int found = 0;

	d = opendir("/proc");
	if (!d)
		return 0;
	while ((ent = readdir(d))) {
		if (!isdigit((unsigned char)ent->d_name[0]))
			continue;
		snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);
		if (comm_eq(path, name)) {
			found = 1;
			break;
		}
	}
	closedir(d);
	return found;
}

static void poke_waybar(int n)
{
	if (n <= 0)
		return;
	kill_comm("waybar", SIGRTMIN + n);
}

static void spawn_cmd(const char *cmd)
{
	char expanded[512];
	pid_t pid;

	expand_cmd(expanded, sizeof(expanded), cmd);
	if (dry) {
		printf("exec %s\n", expanded);
		fflush(stdout);
		return;
	}
	pid = fork();
	if (pid < 0) {
		perror("fork");
		return;
	}
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", expanded, (char *)NULL);
		_exit(127);
	}
}

static const char *kill_name(const struct rule *r)
{
	if (!strncmp(r->on_remove, "kill:", 5) && r->on_remove[5])
		return r->on_remove + 5;
	return NULL;
}

static void do_add(int i)
{
	const struct rule *r = &rs.r[i];
	const char *kn = kill_name(r);

	set_armed(r->name);
	if (kn && comm_running(kn)) {
		if (verbose)
			fprintf(stderr, "skip add %s: %s already running\n", r->name, kn);
		poke_waybar(r->waybar_signal);
		return;
	}
	if (verbose || dry)
		fprintf(stderr, "add %s\n", r->name);
	if (r->on_add[0])
		spawn_cmd(r->on_add);
	if (!dry)
		poke_waybar(r->waybar_signal);
	else if (r->waybar_signal)
		printf("waybar SIGRTMIN+%d\n", r->waybar_signal);
}

static void do_remove(int i)
{
	const struct rule *r = &rs.r[i];
	const char *kn = kill_name(r);

	if (!is_armed(r->name)) {
		if (verbose)
			fprintf(stderr, "skip remove %s: never started\n", r->name);
		return;
	}
	clear_armed(r->name);
	if (verbose || dry)
		fprintf(stderr, "remove %s\n", r->name);
	if (kn) {
		if (dry)
			printf("kill %s\n", kn);
		else {
			kill_comm(kn, SIGTERM);
			kill_comm(kn, SIGKILL);
		}
	} else if (r->on_remove[0]) {
		spawn_cmd(r->on_remove);
	}
	if (!dry)
		poke_waybar(r->waybar_signal);
	else if (r->waybar_signal)
		printf("waybar SIGRTMIN+%d\n", r->waybar_signal);
}

static void pend_clear_rule(int rule_i)
{
	int i;

	for (i = 0; i < MAX_PENDING; i++) {
		if (pend[i].used && pend[i].rule_i == rule_i)
			pend[i].used = 0;
	}
}

static void pend_add(int rule_i, const char *devpath, unsigned long fire_at)
{
	int i;

	pend_clear_rule(rule_i);
	for (i = 0; i < MAX_PENDING; i++) {
		if (!pend[i].used) {
			pend[i].used = 1;
			pend[i].rule_i = rule_i;
			snprintf(pend[i].devpath, sizeof(pend[i].devpath), "%s",
				 devpath ? devpath : "");
			pend[i].fire_at = fire_at;
			return;
		}
	}
	fprintf(stderr, "pending overflow, firing %s now\n", rs.r[rule_i].name);
	do_add(rule_i);
}

static int pend_timeout_ms(void)
{
	unsigned long t = now_ms(), soon = 0;
	int i, any = 0;

	for (i = 0; i < MAX_PENDING; i++) {
		if (!pend[i].used)
			continue;
		if (!any || pend[i].fire_at < soon) {
			soon = pend[i].fire_at;
			any = 1;
		}
	}
	if (!any)
		return -1;
	if (soon <= t)
		return 0;
	{
		int ms = (int)(soon - t);
		int i, need_tick = 0;

		for (i = 0; i < MAX_PENDING; i++) {
			if (pend[i].used && rs.r[pend[i].rule_i].nsuppress) {
				need_tick = 1;
				break;
			}
		}
		if (need_tick && ms > 100)
			ms = 100;
		if (ms > 60000)
			ms = 60000;
		return ms;
	}
}

static void pend_check_sysfs(void)
{
	char usbprod[MAX_USB][64];
	int nusb = -1;
	int i, any = 0;

	for (i = 0; i < MAX_PENDING; i++) {
		if (pend[i].used && rs.r[pend[i].rule_i].nsuppress) {
			any = 1;
			break;
		}
	}
	if (!any)
		return;
	nusb = scan_usb_products(usbprod, MAX_USB);
	for (i = 0; i < MAX_PENDING; i++) {
		if (!pend[i].used)
			continue;
		if (usb_suppressed(&rs.r[pend[i].rule_i], usbprod, nusb)) {
			if (verbose)
				fprintf(stderr, "debounce cancel %s (sysfs)\n",
					rs.r[pend[i].rule_i].name);
			pend[i].used = 0;
		}
	}
}

static void pend_fire_due(void)
{
	unsigned long t = now_ms();
	char usbprod[MAX_USB][64];
	int nusb = -1;
	int i;

	for (i = 0; i < MAX_PENDING; i++) {
		if (!pend[i].used || pend[i].fire_at > t)
			continue;
		if (rs.r[pend[i].rule_i].nsuppress) {
			if (nusb < 0)
				nusb = scan_usb_products(usbprod, MAX_USB);
			if (usb_suppressed(&rs.r[pend[i].rule_i], usbprod, nusb)) {
				if (verbose)
					fprintf(stderr, "debounce cancel %s (sysfs)\n",
						rs.r[pend[i].rule_i].name);
				pend[i].used = 0;
				continue;
			}
		}
		do_add(pend[i].rule_i);
		pend[i].used = 0;
	}
}

static void pend_suppress_product(const char *product)
{
	int i;

	for (i = 0; i < MAX_PENDING; i++) {
		if (!pend[i].used)
			continue;
		if (rule_suppress_product(&rs.r[pend[i].rule_i], product)) {
			if (verbose)
				fprintf(stderr, "debounce cancel %s (saw %s)\n",
					rs.r[pend[i].rule_i].name, product);
			pend[i].used = 0;
		}
	}
}

static int is_add_remove(const char *action)
{
	return action && (!strcmp(action, "add") || !strcmp(action, "remove"));
}

static void handle_event(const char *action, const char *sub, const char *devtype,
			 const char *product, const char *devpath)
{
	int i, isadd;

	if (!is_add_remove(action))
		return;
	isadd = !strcmp(action, "add");

	if (isadd && product) {
		pend_suppress_product(product);
		for (i = 0; i < rs.n; i++) {
			if (!is_armed(rs.r[i].name))
				continue;
			if (!rule_suppress_product(&rs.r[i], product))
				continue;
			if (verbose)
				fprintf(stderr, "retract %s (saw %s)\n",
					rs.r[i].name, product);
			do_remove(i);
		}
	}

	for (i = 0; i < rs.n; i++) {
		if (!rule_match(&rs.r[i], sub, devtype, product, devpath))
			continue;
		if (isadd) {
			if (rs.r[i].debounce_ms)
				pend_add(i, devpath, now_ms() + rs.r[i].debounce_ms);
			else
				do_add(i);
		} else {
			pend_clear_rule(i);
			do_remove(i);
		}
	}
}

static int read_sysfs(const char *path, char *out, size_t n)
{
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	if (!fgets(out, (int)n, f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	out[strcspn(out, "\r\n")] = '\0';
	return 0;
}

static int scan_usb_products(char prod[][64], int max)
{
	DIR *d;
	struct dirent *ent;
	int n = 0;
	char vp[320], pp[320], vs[16], ps[16];

	d = opendir("/sys/bus/usb/devices");
	if (!d)
		return 0;
	while ((ent = readdir(d))) {
		unsigned vid, pid;

		if (ent->d_name[0] == '.')
			continue;
		snprintf(vp, sizeof(vp), "/sys/bus/usb/devices/%s/idVendor", ent->d_name);
		snprintf(pp, sizeof(pp), "/sys/bus/usb/devices/%s/idProduct", ent->d_name);
		if (read_sysfs(vp, vs, sizeof(vs)) < 0)
			continue;
		if (read_sysfs(pp, ps, sizeof(ps)) < 0)
			continue;
		vid = (unsigned)strtoul(vs, NULL, 16);
		pid = (unsigned)strtoul(ps, NULL, 16);
		if (n < max)
			snprintf(prod[n++], 64, "%x/%x/", vid, pid);
	}
	closedir(d);
	return n;
}

static int usb_suppressed(const struct rule *r, char prod[][64], int nprod)
{
	int i;

	for (i = 0; i < nprod; i++) {
		if (rule_suppress_product(r, prod[i]))
			return 1;
	}
	return 0;
}

static void boot_scan(void)
{
	char usbprod[MAX_USB][64];
	int nusb, i;
	DIR *d;
	struct dirent *ent;

	nusb = scan_usb_products(usbprod, MAX_USB);

	for (i = 0; i < rs.n; i++) {
		int u;

		if (strcmp(rs.r[i].match_subsystem, "usb"))
			continue;
		for (u = 0; u < nusb; u++) {
			if (rule_match(&rs.r[i], "usb",
				      rs.r[i].match_devtype[0] ? rs.r[i].match_devtype : "usb_device",
				      usbprod[u], NULL))
				do_add(i);
		}
	}

	d = opendir("/sys/class/typec");
	if (!d)
		return;
	while ((ent = readdir(d))) {
		char link[320], real[512];
		ssize_t L;

		if (!strstr(ent->d_name, "-partner"))
			continue;
		snprintf(link, sizeof(link), "/sys/class/typec/%s", ent->d_name);
		L = readlink(link, real, sizeof(real) - 1);
		if (L < 0)
			snprintf(real, sizeof(real), "/class/typec/%s", ent->d_name);
		else {
			real[L] = '\0';
			/* sysfs class link is relative; suffix match only needs the name */
		}
		for (i = 0; i < rs.n; i++) {
			if (strcmp(rs.r[i].match_subsystem, "typec"))
				continue;
			if (!rule_match(&rs.r[i], "typec", NULL, NULL, ent->d_name) &&
			    !rule_match(&rs.r[i], "typec", NULL, NULL, real))
				continue;
			if (usb_suppressed(&rs.r[i], usbprod, nusb)) {
				if (verbose)
					fprintf(stderr, "boot skip %s: usb suppress\n",
						rs.r[i].name);
				continue;
			}
			do_add(i);
		}
	}
	closedir(d);
}

static int take_lock(void)
{
	char path[256];

	lock_path(path, sizeof(path));
	lockfd = open(path, O_CREAT | O_RDWR, 0644);
	if (lockfd < 0) {
		perror(path);
		return -1;
	}
	if (flock(lockfd, LOCK_EX | LOCK_NB) < 0) {
		fprintf(stderr, "already running (lock %s)\n", path);
		return -1;
	}
	return 0;
}

static void load_or_die(const char *dir)
{
	int n = rules_load(&rs, dir);

	if (n < 0)
		exit(1);
	if (n == 0) {
		fprintf(stderr, "no .rule files in %s\n", dir);
		exit(1);
	}
	fprintf(stderr, "loaded %d rule%s from %s\n", n, n == 1 ? "" : "s", dir);
}

int run_reload(void)
{
	char path[256];
	FILE *f;
	int pid = 0;

	pidfile_path(path, sizeof(path));
	f = fopen(path, "r");
	if (!f) {
		perror(path);
		return 1;
	}
	if (fscanf(f, "%d", &pid) != 1 || pid <= 1) {
		fclose(f);
		fprintf(stderr, "bad pid file\n");
		return 1;
	}
	fclose(f);
	if (kill(pid, SIGHUP) < 0) {
		perror("SIGHUP");
		return 1;
	}
	fprintf(stderr, "reload sent to pid %d\n", pid);
	return 0;
}

int run_main(const char *dir, int dry_run, int verb)
{
	char defdir[512];
	int fd, pfdn;
	struct pollfd pfd[2];
	char buf[UEVENT_BUF];
	int pipefd[2];
	struct sigaction sa;

	dry = dry_run;
	verbose = verb;
	setvbuf(stderr, NULL, _IOLBF, 0);
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (!dir || !dir[0]) {
		rules_default_dir(defdir, sizeof(defdir));
		dir = defdir;
	}

	if (pipe(pipefd) < 0) {
		perror("pipe");
		return 1;
	}
	wake_rd = pipefd[0];
	wake_wr = pipefd[1];
	fcntl(wake_rd, F_SETFD, FD_CLOEXEC);
	fcntl(wake_wr, F_SETFD, FD_CLOEXEC);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_hup;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_NOCLDWAIT;
	sigaction(SIGCHLD, &sa, NULL);

	if (!dry && take_lock() < 0)
		return 1;
	if (!dry)
		write_pid();

	load_or_die(dir);
	boot_scan();

	fd = nl_open();
	if (fd < 0)
		return 1;

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	pfd[1].fd = wake_rd;
	pfd[1].events = POLLIN;
	pfdn = 2;

	if (verbose || dry)
		fprintf(stderr, "dispatch ready (dry=%d)\n", dry);

	while (!got_term) {
		int n, i;
		char dummy[8];

		n = poll(pfd, pfdn, pend_timeout_ms());
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			return 1;
		}
		if (got_hup) {
			got_hup = 0;
			for (i = 0; i < MAX_PENDING; i++)
				pend[i].used = 0;
			load_or_die(dir);
			if (verbose)
				fprintf(stderr, "reloaded rules (no rescan; unplug/replug or restart)\n");
		}
		if (pfd[1].revents & POLLIN)
			(void)!read(wake_rd, dummy, sizeof(dummy));
		/* Drain netlink before firing timers so a USB add in the
		 * same burst can cancel a typec-partner debounce. */
		for (;;) {
			ssize_t r = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);

			if (r < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK ||
				    errno == EINTR)
					break;
				perror("recv");
				return 1;
			}
			if (r == 0)
				break;
			buf[r] = '\0';
			handle_event(nl_kv(buf, r, "ACTION"),
				     nl_kv(buf, r, "SUBSYSTEM"),
				     nl_kv(buf, r, "DEVTYPE"),
				     nl_kv(buf, r, "PRODUCT"),
				     nl_kv(buf, r, "DEVPATH"));
		}
		pend_check_sysfs();
		pend_fire_due();
	}
	return 0;
}
