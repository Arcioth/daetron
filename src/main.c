#define _GNU_SOURCE
#include "dump.h"
#include "run.h"

#include <libgen.h>
#include <stdio.h>
#include <string.h>

static void usage(const char *argv0)
{
	fprintf(stderr,
		"nixafnetlink — kernel uevents to applet commands, no libudev\n"
		"\n"
		"usage: %s [command] [options]\n"
		"\n"
		"  dump              print every kernel uevent (default)\n"
		"  applet            print tagged add|remove (debug)\n"
		"  run [-r DIR]      load rules.d and exec on match\n"
		"      --dry-run     print actions, do not exec/kill\n"
		"      -v            log matches\n"
		"  reload            SIGHUP the running dispatcher\n"
		"\n"
		"rules dir: -r DIR, $NIXAFNETLINK_RULES, ./rules.d,\n"
		"           else ~/.config/nixafnetlink/rules.d\n"
		"reload reads rules again; does not rescan already-plugged devices.\n",
		argv0);
}

int main(int argc, char **argv)
{
	const char *cmd = "dump";
	const char *dir = NULL;
	int dry = 0, verbose = 0, i, argi = 1;
	char *base, buf[256];

	snprintf(buf, sizeof(buf), "%s", argv[0]);
	base = basename(buf);

	if (argi < argc && argv[argi][0] != '-') {
		cmd = argv[argi++];
	} else if (argi < argc && (!strcmp(argv[argi], "--applet") || !strcmp(argv[argi], "-a"))) {
		cmd = "applet";
		argi++;
	} else if (argi < argc && !strcmp(argv[argi], "--dispatch")) {
		cmd = "run";
		argi++;
	} else if (!strcmp(base, "uevent") && argi >= argc) {
		cmd = "dump";
	}

	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
		usage(argv[0]);
		return 0;
	}

	for (i = argi; i < argc; i++) {
		if (!strcmp(argv[i], "-r") && i + 1 < argc)
			dir = argv[++i];
		else if (!strncmp(argv[i], "--rules=", 8))
			dir = argv[i] + 8;
		else if (!strcmp(argv[i], "--dry-run") || !strcmp(argv[i], "-n"))
			dry = 1;
		else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
			verbose = 1;
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		} else if (!strcmp(argv[i], "--applet") || !strcmp(argv[i], "-a"))
			cmd = "applet";
		else {
			fprintf(stderr, "unknown option %s\n", argv[i]);
			usage(argv[0]);
			return 2;
		}
	}

	if (!strcmp(cmd, "dump"))
		return dump_main(0);
	if (!strcmp(cmd, "applet"))
		return dump_main(1);
	if (!strcmp(cmd, "run") || !strcmp(cmd, "dispatch"))
		return run_main(dir, dry, verbose);
	if (!strcmp(cmd, "reload"))
		return run_reload();

	fprintf(stderr, "unknown command %s\n", cmd);
	usage(argv[0]);
	return 2;
}
