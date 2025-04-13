jftui is a minimalistic, lightweight C99 command line client for the open source [Jellyfin](http://jellyfin.org/) media server.

# Installation
The program must be built from source.

For Arch Linux users, there is an AUR [package](https://aur.archlinux.org/packages/jftui/).
For (Open)BSD-folk, you need to use `gmake` (GNU Make) instead of `make`.

## Dependencies
- [libcurl](https://curl.haxx.se/libcurl/) (runtime)
- [libmpv](https://mpv.io) >= 1.24 (runtime)
- [libbsd](https://libbsd.freedesktop.org/wiki/) (linux only)
- [YAJL](https://lloyd.github.io/yajl/) >= 2.0 (runtime)
- [PEG](http://piumarta.com/software/peg/) (development only)


## Building
Make sure to checkout a release as the master branch is not guaranteed to work correctly or indeed compile at any time.

Then, simply run
```
make && sudo make install
```

# Usage
Run `jftui`. You will be prompted for a minimal interactive configuration on first run.

**BEWARE**: jftui fetches `https://github.com/Aanok/jftui/releases/latest` on startup to check for newer versions. You can avoid this by passing the `--no-check-updates` argument. There is also a [settings file](https://github.com/Aanok/jftui/wiki/Settings) entry.

The interface should be intuitive enough: select one or more entries by entering the corresponding index number. See below for a full description of the command syntax.

jftui will drop into a command line instance of mpv when starting playback. It will use `mpv.conf` and `input.conf` files found in `$XDG_CONFIG_HOME/jftui` (this location can be overridden with the `--config-dir` argument). It will also try and load scripts found in the same folder, but no guarantees are made about them actually working correctly.

It is recommended to consult the [wiki page](https://github.com/Aanok/jftui/wiki/mpv-commands) on configuring mpv commands to use jftui: a few special ones are required in particular to manipulate the playback playlist.

## Jftui commands
The grammar defining jftui commands is as follows:
```
S ::= "q" (quits)
  | ( "help" | "?" )          (print a help menu)
  | "h"                       (go to "home" root menu)
  | ".."                      (go to previous menu)
  | "f" ( "c" | [pufrld]+ )   (filters: clear or played, unplayed, favorite, resumable, liked, disliked)
  | "m" ("p" | "u") Selector  (marks items played or unplayed)
  | "m" ("f" | "uf") Selector (marks items favorite or unfavorite)
  | Selector                  (opens a single directory entry or sends a sequence of items to playback)
Selector :: = '*'             (everything in the current menu)
  | Items
Items ::= Atom "," Items      (list)
  | Atom
Atom ::= n1 "-" n2            (range)
  | n                         (single item)
```

Whitespace may be scattered between tokens at will. Inexisting items are silently ignored. Both `quit` and `stop` mpv commands will drop you back to menu navigation.

There is one further command that will be parsed, but it is left undocumented because its implementation is barely more than a stub. Caveat.


# Plans and TODO
- Search;
- Explicit command to recursively navigate folders to send items to playback;
- Transcoding.
