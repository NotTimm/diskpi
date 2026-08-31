CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
LDLIBS  := -lm
PREFIX  ?= /usr/local

OBJS := main.o scan.o term.o render.o

diskpi: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJS): diskpi.h

install: diskpi
	install -Dm755 diskpi $(DESTDIR)$(PREFIX)/bin/diskpi

clean:
	rm -f diskpi $(OBJS)

.PHONY: install clean
