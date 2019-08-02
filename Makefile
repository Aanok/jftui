CC=clang
OFLAGS=-O2 -march=native
WFLAGS=-Wall -Wpedantic -Wextra -Wconversion -Wstrict-prototypes -Werror=implicit-function-declaration -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion
CFLAGS=`pkg-config --cflags --libs libcurl yajl mpv` -pthread
DFLAGS=-g -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address -fsanitize=undefined

SOURCES=src/shared.c src/config.c src/json_parser.c src/menu.c src/network.c src/main.c


jftui: $(SOURCES) src/command_parser.c
	$(CC) $(CFLAGS) $(OFLAGS) $(SOURCES) -o $@

debug: $(SOURCES) src/command_parser.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) $(SOURCES) -o jftui_debug

src/command_parser.c: src/command_grammar.leg
	leg -o $@ $^


.PHONY: install uninstall clean

install: jftui
	install -Dm555 $^ $(DESTDIR)/usr/bin/$^

uninstall:
	rm $(DESTDIR)/usr/bin/jftui

clean:
	rm -f jftui jftui_debug
