CC=clang
OFLAGS=-O2 -march=native
WFLAGS=-Wall -Wpedantic -Wextra -Wconversion -Wstrict-prototypes -Werror=implicit-function-declaration -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion
CFLAGS=`pkg-config --cflags libcurl yajl mpv` -pthread
LFLAGS=`pkg-config --libs libcurl yajl mpv`
DFLAGS=-g -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address -fsanitize=undefined

SOURCES=src/linenoise.c src/shared.c src/config.c src/disk_io.c src/json_parser.c src/menu.c src/network.c src/main.c

OBJECTS=build/linenoise.o build/menu.o build/shared.o build/config.o build/disk_io.o build/json_parser.o build/network.o build/main.o



jftui: build src/command_parser.c $(SOURCES)
	$(CC) $(CFLAGS) $(LFLAGS) $(OFLAGS) $(SOURCES) -o build/$@

debug: build $(OBJECTS) $(SOURCES)
	$(CC) $(WFLAGS) $(LFLAGS) $(DFLAGS) $(OBJECTS) -o build/jftui_debug



build:
	mkdir -p build

src/command_parser.c: src/command_grammar.leg
	leg -o $@ $^

build/linenoise.o: src/linenoise.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

build/menu.o: src/menu.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

build/shared.o: src/shared.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

build/config.o: src/config.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

build/disk_io.o: src/disk_io.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

build/json_parser.o: src/json_parser.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

build/network.o: src/network.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

build/main.o: src/main.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^



.PHONY: install uninstall clean

install: jftui
	install -Dm555 build/$^ $(DESTDIR)/usr/bin/$^

uninstall:
	rm $(DESTDIR)/usr/bin/jftui

clean:
	rm -f build/*
