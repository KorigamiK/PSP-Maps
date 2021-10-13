CC ?= gcc
CFLAGS += -O2 -g -Wall `sdl-config --cflags` `curl-config --cflags`
LIBS += -lSDL_image -lSDL_gfx -lSDL_ttf `sdl-config --libs` `curl-config --libs` $(LDFLAGS) -lm
PREFIX ?= /usr/local
DESTDIR ?= 

.PHONY: all install uninstall clean

all: pspmaps

pspmaps: pspmaps.c $(ICON) global.o kml.o tile.c io.c cJSON.o
	$(CC) $(CFLAGS) -o pspmaps$(EXEEXT) pspmaps.c $(ICON) global.o kml.o cJSON.o $(LIBS)

global.o: global.c global.h
	$(CC) $(CFLAGS) -c global.c

kml.o: kml.c kml.h
	$(CC) $(CFLAGS) -c kml.c

cJSON.o: cJSON.c cJSON.h
	$(CC) $(CFLAGS) -c cJSON.c

icon.o: icon.rc
	$(WINDRES) -i icon.rc -o icon.o

install: pspmaps
	install -v -m 0755 -d $(DESTDIR)$(PREFIX)/bin
	install -v -m 0755 ./pspmaps$(EXEEXT) $(DESTDIR)$(PREFIX)/bin

uninstall: pspmaps
	rm -rfv $(DESTDIR)$(PREFIX)/bin/pspmaps$(EXEEXT)

clean:
	rm -rfv pspmaps pspmaps.exe *.o PSP-Maps.prx PSP-Maps.elf PARAM.SFO EBOOT.PBP pspmaps.gpu cache/ data/*.dat kml/

