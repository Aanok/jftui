CC=clang
OFLAGS=-O2 -march=native
WFLAGS=-Wall -Wpedantic -Wextra -Wconversion -Wstrict-prototypes -Werror=implicit-function-declaration -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion
CFLAGS=`pkg-config --cflags --libs libcurl yajl mpv` -pthread
DFLAGS=-g -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address -fsanitize=undefined

all: src/*.c
	$(CC) $(WFLAGS) $(CFLAGS) $(OFLAGS) $^ -o jftui

debug: src/*.c
	$(CC) $(WFLAGS) $(CFLAGS) $(DFLAGS) $^ -o jftui_debug

clean:
	rm -f jftui jftui_debug
