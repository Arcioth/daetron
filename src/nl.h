#ifndef NIXAF_NL_H
#define NIXAF_NL_H

#include <sys/types.h>

#define UEVENT_BUF 8192

int nl_open(void);
ssize_t nl_recv(int fd, char *buf, size_t n);
const char *nl_kv(const char *buf, ssize_t n, const char *key);

#endif
