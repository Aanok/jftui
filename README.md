jftui is a minimalistic, lightweight command line client for the open source [Jellyfin](http://jellyfin.org/) media server. It is developed for the GNU/Linux OS only, although it may be possible to make it run on BSD's.

***DISCLAIMER***: I work on this program in my spare time and mostly to my own personal preference. Absolutely no guarantees are provided on its safety or behavior.

# Installation
## Dependencies
- [libcurl](https://curl.haxx.se/libcurl/) (runtime)
- [libmpv](https://mpv.io) (runtime)
- [YAJL](https://lloyd.github.io/yajl/) (runtime)
- [PEG](http://piumarta.com/software/peg/) (development only)

## Building
Have a look at the Makefile: you may want to swap the default `clang` compiler for `gcc`.

Then, run
```
make && sudo make install
```

# Usage
Run `jftui`. You will be prompted for a minimal interactive configuration on first run.

jftui will use `mpv.conf` and `input.conf` files in `$XDG_CONFIG_HOME/jftui` (this location can be overridden with the `--config-dir` argument). It is recommended to at least add binds for the mpv commands `script-message jftui-playlist-next` and `script-message jftui-playlist-next` to allow playlist navigation.

The grammar defining jftui commands is as follows:
```
S ::= "q" (quits)
  | "h" (go to "home" root menu)
  | ".." (go to previous menu)
  | Selector (opens a single directory entry or sends a sequence of items to playback)
Selector :: = '*' (everything in the current menu)
  | Items
Items ::= Atom "," Items (list)
  | Atom
Atom ::= n1 "-" n2 (range)
  | n (single item)
```

Whitespace may be scattered between tokens at will. Inexisting items are silently ignored. Both `quit` and `stop` mpv commands will drop you back to menu navigation.

To safely run multiple instances of jftui concurrently, make sure to specify distinct `--runtime-dir` arguments to at least each one after the first.

# Plans and TODO
- Video item support, including subtitles, and multipart items;
- Support for different versions of media items;
- Search;
- Explicit command to recursively navigate folders to send items to playback;
- Filters: played, unplayed etc, to be applied before requesting a directory or to the currently open menu;
- Explicitly marking items played and unplayed.
