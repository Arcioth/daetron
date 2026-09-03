#define _GNU_SOURCE
#include "dump.h"
#include "nl.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Hardcoded tags for the debug printer only. Live dispatch uses rules.d. */
static const char *classify(const char *sub, const char *devtype,
			    const char *devpath, const char *product)
{
	if (sub && !strcmp(sub, "usb") &&
	    devtype && !strcmp(devtype, "usb_device") && product) {
		if (!strncmp(product, "2ca3/4011/", 10))
			return "dji";
		if (!strncmp(product, "46d/", 4))
			return "logi";
	}
	if (sub && !strcmp(sub, "typec") && devpath &&
	    strstr(devpath, "-partner"))
		return "imex";
	return NULL;
}

int dump_main(int applet)
{
	int fd;
	char buf[UEVENT_BUF];

	fd = nl_open();
	if (fd < 0)
		return 1;

	for (;;) {
		const char *action, *sub, *devtype, *product, *devpath, *tag;
		ssize_t n = nl_recv(fd, buf, sizeof(buf));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("recv");
			return 1;
		}
		if (n == 0)
			continue;

		action = nl_kv(buf, n, "ACTION");
		sub = nl_kv(buf, n, "SUBSYSTEM");
		devtype = nl_kv(buf, n, "DEVTYPE");
		product = nl_kv(buf, n, "PRODUCT");
		devpath = nl_kv(buf, n, "DEVPATH");
		tag = classify(sub, devtype, devpath, product);

		if (applet) {
			if (!tag)
				continue;
			if (!action || (strcmp(action, "add") && strcmp(action, "remove")))
				continue;
			printf("%s %s %s %s %s\n",
			       tag,
			       action,
			       sub ? sub : "-",
			       product ? product : "-",
			       devpath ? devpath : "-");
		} else {
			printf("%s %s %s %s %s%s%s\n",
			       action ? action : "-",
			       sub ? sub : "-",
			       devtype ? devtype : "-",
			       product ? product : "-",
			       devpath ? devpath : "-",
			       tag ? " " : "",
			       tag ? tag : "");
		}
		fflush(stdout);
	}
}
