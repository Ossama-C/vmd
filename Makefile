CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
PREFIX ?= $(HOME)/.local

SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)

vmd: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o vmd

%.o: %.c vmd.h
	$(CC) $(CFLAGS) -c $< -o $@

install: vmd
	mkdir -p "$(PREFIX)/bin"
	cp vmd "$(PREFIX)/bin/vmd"
	chmod 755 "$(PREFIX)/bin/vmd"

clean:
	rm -f vmd $(OBJECTS)

.PHONY: install clean
