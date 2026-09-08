#define _GNU_SOURCE
#include "rules.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t')
		memmove(s, s + 1, strlen(s));
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
		*--e = '\0';
}

static void setstr(char *dst, size_t n, const char *src)
{
	snprintf(dst, n, "%s", src);
}

static int parse_file(struct rule *r, const char *path)
{
	FILE *f;
	char line[1024];
	int lineno = 0;

	memset(r, 0, sizeof(*r));
	f = fopen(path, "r");
	if (!f) {
		perror(path);
		return -1;
	}
	while (fgets(line, sizeof(line), f)) {
		char *eq;

		lineno++;
		line[strcspn(line, "\n")] = '\0';
		trim(line);
		if (!line[0] || line[0] == '#')
			continue;
		eq = strchr(line, '=');
		if (!eq) {
			fprintf(stderr, "%s:%d: missing '='\n", path, lineno);
			continue;
		}
		*eq++ = '\0';
		trim(line);
		trim(eq);
		if (!strcmp(line, "name"))
			setstr(r->name, sizeof(r->name), eq);
		else if (!strcmp(line, "match_subsystem"))
			setstr(r->match_subsystem, sizeof(r->match_subsystem), eq);
		else if (!strcmp(line, "match_devtype"))
			setstr(r->match_devtype, sizeof(r->match_devtype), eq);
		else if (!strcmp(line, "match_product_prefix"))
			setstr(r->match_product_prefix, sizeof(r->match_product_prefix), eq);
		else if (!strcmp(line, "match_devpath_suffix"))
			setstr(r->match_devpath_suffix, sizeof(r->match_devpath_suffix), eq);
		else if (!strcmp(line, "debounce_ms"))
			r->debounce_ms = (unsigned)strtoul(eq, NULL, 10);
		else if (!strcmp(line, "suppress_if_product_prefix")) {
			char *p, *save = NULL, buf[256];

			setstr(buf, sizeof(buf), eq);
			for (p = strtok_r(buf, ",", &save); p && r->nsuppress < MAX_SUPPRESS;
			     p = strtok_r(NULL, ",", &save)) {
				trim(p);
				if (*p)
					setstr(r->suppress[r->nsuppress++],
					       sizeof(r->suppress[0]), p);
			}
		} else if (!strcmp(line, "on_add"))
			setstr(r->on_add, sizeof(r->on_add), eq);
		else if (!strcmp(line, "on_remove"))
			setstr(r->on_remove, sizeof(r->on_remove), eq);
		else if (!strcmp(line, "waybar_signal"))
			r->waybar_signal = atoi(eq);
		else if (!strcmp(line, "dismiss"))
			; /* poll-loop leftover; event-driven does not respawn */
		else
			fprintf(stderr, "%s:%d: unknown key '%s'\n", path, lineno, line);
	}
	fclose(f);
	if (!r->name[0]) {
		fprintf(stderr, "%s: missing name=\n", path);
		return -1;
	}
	if (!r->match_subsystem[0] && !r->match_devtype[0] &&
	    !r->match_product_prefix[0] && !r->match_devpath_suffix[0]) {
		fprintf(stderr, "%s: no match_* keys\n", path);
		return -1;
	}
	return 0;
}

static int endswith(const char *s, const char *suf)
{
	size_t n = strlen(s), m = strlen(suf);

	return n >= m && !strcmp(s + n - m, suf);
}

int rules_load(struct ruleset *rs, const char *dir)
{
	DIR *d;
	struct dirent *ent;
	char path[1024];
	int i, j;

	rs->n = 0;
	snprintf(rs->dir, sizeof(rs->dir), "%s", dir ? dir : ".");
	d = opendir(rs->dir);
	if (!d) {
		perror(rs->dir);
		return -1;
	}
	while ((ent = readdir(d))) {
		struct rule tmp;

		if (ent->d_name[0] == '.')
			continue;
		if (!endswith(ent->d_name, ".rule"))
			continue;
		if (rs->n >= MAX_RULES) {
			fprintf(stderr, "too many rules (max %d)\n", MAX_RULES);
			break;
		}
		snprintf(path, sizeof(path), "%s/%s", rs->dir, ent->d_name);
		if (parse_file(&tmp, path) == 0)
			rs->r[rs->n++] = tmp;
	}
	closedir(d);

	/* stable order: name */
	for (i = 0; i < rs->n; i++) {
		for (j = i + 1; j < rs->n; j++) {
			if (strcmp(rs->r[j].name, rs->r[i].name) < 0) {
				struct rule t = rs->r[i];
				rs->r[i] = rs->r[j];
				rs->r[j] = t;
			}
		}
	}
	return rs->n;
}

int rule_match(const struct rule *r, const char *sub, const char *devtype,
	       const char *product, const char *devpath)
{
	if (r->match_subsystem[0]) {
		if (!sub || strcmp(sub, r->match_subsystem))
			return 0;
	}
	if (r->match_devtype[0]) {
		if (!devtype || strcmp(devtype, r->match_devtype))
			return 0;
	}
	if (r->match_product_prefix[0]) {
		if (!product ||
		    strncmp(product, r->match_product_prefix,
			    strlen(r->match_product_prefix)))
			return 0;
	}
	if (r->match_devpath_suffix[0]) {
		if (!devpath || !endswith(devpath, r->match_devpath_suffix))
			return 0;
	}
	return 1;
}

int rule_suppress_product(const struct rule *r, const char *product)
{
	int i;

	if (!product)
		return 0;
	for (i = 0; i < r->nsuppress; i++) {
		if (!strncmp(product, r->suppress[i], strlen(r->suppress[i])))
			return 1;
	}
	return 0;
}

void rules_default_dir(char *buf, size_t n)
{
	const char *env = getenv("DAETRON_RULES");
	const char *home = getenv("HOME");
	struct stat st;

	if (env && env[0]) {
		snprintf(buf, n, "%s", env);
		return;
	}
	if (stat("rules.d", &st) == 0 && S_ISDIR(st.st_mode)) {
		snprintf(buf, n, "rules.d");
		return;
	}
	if (home && home[0]) {
		snprintf(buf, n, "%s/.config/daetron/rules.d", home);
		if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode))
			return;
	}
	snprintf(buf, n, "rules.d");
}
