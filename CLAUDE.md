# kew (custom fork)

## Build and install: keep one running copy

There must be exactly one kew on this machine, and it is
`~/.local/bin/kew`. Everything points at it:

- `kew` on PATH resolves to `~/.local/bin/kew`.
- Super+F5 runs `~/.config/hypr/hyprland/scripts/music.sh`, which launches
  `~/.local/bin/kew` explicitly inside kitty on the `special:music` workspace.
- The Arch package (`/usr/bin/kew`) was removed. Do not reinstall it.

`./kew` in this repo is only the compiler output. It is not a second
install and nobody runs it directly.

After every code change, always do all three steps:

```sh
make -j$(nproc) PREFIX=$HOME/.local
make install PREFIX=$HOME/.local
```

`PREFIX=$HOME/.local` is required on the build step too, not only on
install. It bakes the data dir `~/.local/share/kew` into the binary. A
build without it looks for layouts in `/usr/local/share/kew`, finds
nothing, and exits at startup with "Couldn't copy layouts". The Super+F5
script then shows an empty dimmed special workspace. Make does not notice
a PREFIX change on its own, so run `make clean` first when switching.

then quit the running kew with Shift+Q and press Super+F5 to relaunch.
A kew that is already open keeps the old code in memory until it is
restarted, and it usually is open, hidden on the special workspace.

Check with `cmp ~/.local/bin/kew ./kew` when in doubt. They must be
identical.

## Testing in a terminal

Run `./kew` inside tmux with a fixed size to capture the UI without
touching the user's session. Back up `~/.config/kew/kewstaterc` first and
restore it afterwards, since kew saves the queue on exit.

## Library database

`~/.config/kew/library.dat` is versioned by `DB_VERSION` in
`src/data/directorytree.c`. Bump it when `FileSystemEntryDisk` changes.
A bump forces a one-time rescan on next launch.
