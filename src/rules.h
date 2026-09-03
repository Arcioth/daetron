#ifndef NIXAF_RULES_H
#define NIXAF_RULES_H

#include <stddef.h>

#define MAX_RULES 64
#define MAX_SUPPRESS 8

struct rule {
	char name[64];
	char match_subsystem[32];
	char match_devtype[32];
	char match_product_prefix[64];
	char match_devpath_suffix[64];
	unsigned debounce_ms;
	char suppress[MAX_SUPPRESS][32];
	int nsuppress;
	char on_add[512];
	char on_remove[256];
	int waybar_signal; /* 0 = none; else SIGRTMIN + N */
};

struct ruleset {
	struct rule r[MAX_RULES];
	int n;
	char dir[512];
};

int rules_load(struct ruleset *rs, const char *dir);
int rule_match(const struct rule *r, const char *sub, const char *devtype,
	       const char *product, const char *devpath);
int rule_suppress_product(const struct rule *r, const char *product);
void rules_default_dir(char *buf, size_t n);

#endif
