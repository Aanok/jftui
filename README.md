jftui is a minimalistic, lightweight command line client for the open source [Jellyfin](http://jellyfin.org/) media server. It is developed for the GNU/Linux OS only, although it may be possible to make it run on BSD's.

# Installation
The program must be built from source.

For Arch Linux users, there is an AUR [package](https://aur.archlinux.org/packages/jftui/).

## Dependencies
- [libcurl](https://curl.haxx.se/libcurl/) (runtime)
- [libmpv](https://mpv.io) >= 1.24 (runtime)
- [YAJL](https://lloyd.github.io/yajl/) >= 2.0 (runtime)
- [PEG](http://piumarta.com/software/peg/) (development only)


## Building
Make sure to checkout a release as the master branch is not guaranteed to compile or indeed work at all times.

Then have a look at the Makefile: you may want to swap the default `clang` compiler for `gcc`.

Finally, run
```
make && sudo make install
```

# Usage
Run `jftui`. You will be prompted for a minimal interactive configuration on first run.

**BEWARE**: jftui fetches `https://github.com/Aanok/jftui/releases/latest` on startup to check for newer versions. You can avoid this by passing the `--no-check-updates` argument. There is also a [settings file](https://github.com/Aanok/jftui/wiki/Settings) entry.

jftui will use `mpv.conf` and `input.conf` files in `$XDG_CONFIG_HOME/jftui` (this location can be overridden with the `--config-dir` argument).

It is recommended to consult the [wiki page](https://github.com/Aanok/jftui/wiki/mpv-commands) on configuring mpv commands to use jftui: a few special ones are required in particular to manipulate the playback playlist.

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

There is one further command that will be parsed, but it is left undocumented because its implementation is barely more than a stub. Caveat.

To safely run multiple instances of jftui concurrently, make sure to specify distinct `--runtime-dir` arguments to at least each one after the first.

# Plans and TODO
- Search;
- Explicit command to recursively navigate folders to send items to playback;
- Filters: played, unplayed etc, to be applied before requesting a directory or to the currently open menu;
- Explicitly marking items played and unplayed;
- Transcoding.
