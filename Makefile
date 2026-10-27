CFLAGS ?= -O2 -Wall -Wextra -std=c99
PREFIX ?= $(HOME)/.local

batstat: batstat.c

install: batstat
	install -Dm755 batstat $(DESTDIR)$(PREFIX)/bin/batstat

clean:
	rm -f batstat

.PHONY: install test clean
