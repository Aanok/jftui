OFLAGS=-O2 -march=native
WFLAGS=-Wall -Wpedantic -Wextra -Wconversion -Wstrict-prototypes -Werror=implicit-function-declaration -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion
CFLAGS=`pkg-config --cflags libcurl yajl mpv`
LFLAGS=`pkg-config --libs libcurl yajl mpv` -pthread

ifeq ($(shell uname -s),Linux)
	CFLAGS += `pkg-config --cflags libbsd-overlay`
	LFLAGS += `pkg-config --libs libbsd-overlay`
endif

DFLAGS=-g -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address -fsanitize=undefined -DJF_DEBUG

SOURCES=src/linenoise.c src/shared.c src/config.c src/disk.c src/json.c src/menu.c src/playback.c src/net.c src/mpv.c src/main.c

OBJECTS=build/linenoise.o build/menu.o build/shared.o build/config.o build/disk.o build/json.o build/net.o build/playback.o build/mpv.o build/main.o

DOCS=docs/command_state_machine.png

BUILD_DIR := build

.PHONY: all debug install uninstall clean docs




all: ${BUILD_DIR}/jftui

debug: ${BUILD_DIR}/jftui_debug

install: all
	install -Dm555 ${BUILD_DIR}/jftui $(DESTDIR)/usr/bin/jftui

uninstall:
	rm $(DESTDIR)/usr/bin/jftui

clean:
	rm -rf ${BUILD_DIR} runtime

docs: $(DOCS)
	


${BUILD_DIR}:
	mkdir -p ${BUILD_DIR}

${BUILD_DIR}/jftui: ${BUILD_DIR} $(SOURCES)
	$(CC) $(CFLAGS) $(OFLAGS) $(SOURCES) $(LFLAGS) -g -o $@

${BUILD_DIR}/jftui_debug: ${BUILD_DIR} $(OBJECTS) $(SOURCES)
	$(CC) $(WFLAGS) $(DFLAGS) $(OBJECTS) $(LFLAGS) -o $@

src/cmd.c: src/cmd.leg
	leg -o $@ $^

${BUILD_DIR}/linenoise.o: src/linenoise.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

${BUILD_DIR}/menu.o: src/menu.c src/cmd.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ src/menu.c

${BUILD_DIR}/shared.o: src/shared.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

${BUILD_DIR}/config.o: src/config.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

${BUILD_DIR}/disk.o: src/disk.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

${BUILD_DIR}/json.o: src/json.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

${BUILD_DIR}/net.o: src/net.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

${BUILD_DIR}/playback.o: src/playback.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

${BUILD_DIR}/mpv.o: src/mpv.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^

${BUILD_DIR}/main.o: src/main.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) -c -o $@ $^


docs/command_state_machine.png: docs/command_state_machine.gv
	neato -T png $^ > $@
