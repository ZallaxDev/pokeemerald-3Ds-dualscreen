# Development

## Requirements

- devkitPro with devkitARM, libctru, citro2d, citro3d and 3dstools
  (`pacman -S --needed libctru citro2d citro3d 3dstools`), including its
  `picasso` shader assembler.
- A native host C/C++ compiler for the decomp tools and the host tests
  (on Windows, MSYS2 MinGW64's gcc/g++ on `PATH`).
- Python 3.11+ with Pillow.
- git.

## Getting a tree

The repository holds the port and the changes it makes to the decompilation,
not the decompilation itself:

```
python tools/bootstrap.py
```

clones pret/pokeemerald at the commit pinned in `upstream.lock` into
`build/upstream`, places the files of aarant/pokeemerald `icons-followers`
pinned there (`[followers]`: the files it adds and the graphics it changes,
fetched into `build/upstream-followers`), applies
`patches/pokeemerald/*.patch` in order, and places
`3ds_port/`, `tools/` and `builder/` inside it. `--make` also builds the
decomp tools and the 3DSX there (`--jobs N`). Re-running it refreshes the
port files without touching the upstream checkout unless `--clean` is given.

## Building

From `build/upstream/3ds_port` (devkitPro's shell on Windows, or `build.ps1`):

```
make -j8 PYTHON=python3          # emerald3ds.3dsx, data embedded
make verify                      # host tests + link audit
make release                     # dist/Emerald3DS.3dsx, engine files only
make pak                         # build/emerald3ds.pak from this build's data
make devdata                     # build/devdata/ for the loose backend
```

Options: `VOXEL=0`, `VOXEL_LIGHTING=0`, `SHOW_FPS=0`, `EMBED_DATA=0`,
`DATA_BACKEND=1|2|3` (force romfs, loose or pak), `SAMPLE_OVERLAY_DIR=...`
(replacement sample .bin files, for private experiments). Per-machine
settings can go in `3ds_port/local.mk`, which is never committed.

Game translation units have no header dependency tracking: after editing a
shared header, delete the affected `build/root/src/*.o`.

## Testing on the console

- **Development build**: copy `emerald3ds.3dsx` to `/3ds/emerald3ds/` and
  start it from the Homebrew Launcher. It reads its data from its own RomFS.
- **Loose data**: `make devdata`, then
  `python tools/dev_assets.py --dest <SD root>` (or `--ftp HOST:PORT` for an FTP
  server on the console) copies only the files that changed into
  `/3ds/emerald3ds/devdata/` and writes the marker that selects the loose
  backend. Delete the marker to go back.
- **Exactly the release**: `make release pak`, copy `dist/Emerald3DS.3dsx`
  and `build/emerald3ds.pak`.

The log is `/3ds/emerald3ds/port.log`, written only if an empty file named
`/3ds/emerald3ds/debug.txt` exists on the SD card (create it for development
builds); the previous session is kept as `port-prev.log`. Emulator runs (Azahar) are useful for
diagnosis; acceptance is on hardware — the SD card's latency, the linear heap
and VRAM behave differently there.

## Host tests

`make verify` builds and runs the C host tests (input decoding, video decode,
pack parser, voxel world hashing, arena, atlas, trees, lighting, ground mesh,
signposts, relief) and audits the link. The builder's tests:

```
python -m unittest discover -s builder/tests
```

## Keeping the upstream patches small

Game-side changes belong in `3ds_port/` whenever a hook can carry them; a
change inside the decompilation should be the smallest hook that works,
behind `PORT_BRIDGE` or `PLATFORM_3DS`, and must not alter data the ROM also
holds unless the port needs it (the recipe report shows every byte of the
original game's objects that stopped matching).

## Before publishing

```
python tools/release_audit.py --repo . --strict
```

must pass; CI runs it on every push.
