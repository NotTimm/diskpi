CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -pthread
LDLIBS  := -lm -pthread
PREFIX  ?= /usr/local

OBJS := main.o scan.o term.o render.o chart.o \
        chart_pie.o chart_bars.o chart_curve.o chart_points.o \
        chart_radar.o chart_table.o

diskpi: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJS): diskpi.h chart.h

install: diskpi
	install -Dm755 diskpi $(DESTDIR)$(PREFIX)/bin/diskpi

clean:
	rm -f diskpi $(OBJS)

.PHONY: install clean
