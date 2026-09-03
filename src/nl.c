#define _GNU_SOURCE
#include "nl.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#define RCVBUF (2 * 1024 * 1024)

const char *nl_kv(const char *buf, ssize_t n, const char *key)
{
	size_t klen = strlen(key);
	const char *p = buf;
	const char *end = buf + n;

	while (p < end && *p) {
		size_t flen = strlen(p);
		if (flen > klen && !memcmp(p, key, klen) && p[klen] == '=')
			return p + klen + 1;
		p += flen + 1;
	}
	return NULL;
}

int nl_open(void)
{
	int fd, rcv = RCVBUF;
	struct sockaddr_nl sa = {
		.nl_family = AF_NETLINK,
		.nl_groups = 1,
	};

	fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if (fd < 0) {
		perror("socket");
		return -1;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv)) < 0)
		perror("SO_RCVBUF");
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("bind");
		close(fd);
		return -1;
	}
	return fd;
}

ssize_t nl_recv(int fd, char *buf, size_t n)
{
	ssize_t r = recv(fd, buf, n - 1, 0);

	if (r < 0)
		return r;
	if (r == 0)
		return 0;
	buf[r] = '\0';
	return r;
}
