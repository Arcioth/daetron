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

all: daetron uevent

daetron: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

uevent: daetron
	ln -sf daetron uevent

clean:
	rm -f daetron uevent

install: daetron
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m755 daetron $(DESTDIR)$(PREFIX)/bin/daetron
	ln -sf daetron $(DESTDIR)$(PREFIX)/bin/uevent
