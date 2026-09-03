# Default: musl -static (RSS tens of KB). glibc: make STATIC=0
MUSL_GCC := $(shell command -v musl-gcc 2>/dev/null)
CFLAGS ?= -O2 -pipe -Wall -Wextra -std=c11 -s
PREFIX ?= /usr/local
STATIC ?= 1

ifeq ($(STATIC),1)
  ifneq ($(MUSL_GCC),)
    CC := $(MUSL_GCC)
    LDFLAGS += -static
  else
    CC ?= gcc
  endif
else
  CC ?= gcc
endif

SRC = src/main.c src/nl.c src/rules.c src/dump.c src/run.c

.PHONY: all clean install

all: nixafnetlink uevent

nixafnetlink: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

uevent: nixafnetlink
	ln -sf nixafnetlink uevent

clean:
	rm -f nixafnetlink uevent

install: nixafnetlink
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m755 nixafnetlink $(DESTDIR)$(PREFIX)/bin/nixafnetlink
	ln -sf nixafnetlink $(DESTDIR)$(PREFIX)/bin/uevent
