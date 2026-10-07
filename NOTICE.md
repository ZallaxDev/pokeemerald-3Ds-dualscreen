# Notices

Pokémon Emerald 3Ds Dual Screen combines original port code with the pret decompilation and a few
third-party components. Each keeps its own terms; `LICENSE-PORT.md` covers
only the original Pokémon Emerald 3Ds Dual Screen work.

## Upstream decompilation

- **pret/pokeemerald** — https://github.com/pret/pokeemerald, pinned in
  `upstream.lock`. Fetched by `tools/bootstrap.py`, never vendored here. The
  repository does not state a licence; Pokémon Emerald 3Ds Dual Screen does not relicense it. The
  files in `patches/pokeemerald/` carry the port's changes and, as patch
  context, lines of the upstream files they modify.
- **aarant/pokeemerald `icons-followers`** — https://github.com/aarant/pokeemerald,
  follower Pokémon and Gen 6 Pokémon icons on top of pret/pokeemerald, pinned in `upstream.lock`
  (`[followers]`). Fetched by `tools/bootstrap.py`, never vendored here: it
  places the files the branch adds and the graphics it changes; its changes to
  existing files are in `patches/pokeemerald/0038-followers.patch`. Its
  sprites, icons and palettes are not in releases either: the builder
  downloads them from GitHub at the pinned commit on the player's computer
  (or in the browser), checks their SHA-256 and converts them into the
  locally built `emerald3ds.pak` (`builder/emerald3ds_builder/externals.py`).
  The repository does not state a licence; Pokémon Emerald 3Ds Dual Screen
  does not relicense it.

## Code derived from third parties

- **pokeemerald-multiplatform** (gradenGnostic/pokeemerald-multiplatform,
  commit db1cab3d2dc9f0e0a9f4a3d67983e5acf30a9a46), MIT. The voxel overworld
  in `3ds_port/src/voxel/` reuses the logic of its SDL2/OpenGL voxel renderer
  (map instances, metatile classification, atlas composition, follow camera).
  Full text and scope: `3ds_port/src/voxel/NOTICE.md`.
- **devkitARM `3dsx.ld`**, Mozilla Public License 2.0.
  `3ds_port/emerald3ds.ld.in` is a modified copy and stays under the MPL 2.0
  (https://mozilla.org/MPL/2.0/).

## Libraries used by the build

- **libctru, citro2d, citro3d** (devkitPro), zlib licence — linked into the 3DSX.
- **devkitARM / GCC / newlib** — toolchain and C library.

## Bundled with the Windows builder

- **Python** (PSF License Agreement) and its standard library, **Tcl/Tk**
  (BSD-style) and **Pillow** (MIT-CMU / HPND), packaged by **PyInstaller**
  (GPL 2.0 with the bootloader exception).

## Game content

Pokémon Emerald's code, data, graphics, audio and text are © Nintendo, Game
Freak and Creatures. None of it is included in this repository or in the
releases; the builder produces the data pack on the player's computer from
their own ROM.
