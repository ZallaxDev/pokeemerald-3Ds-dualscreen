<p align="center">
  <img src="https://i.imgur.com/Its9ceu.png" alt="Pokémon Emerald 3Ds Dual Screen" width="480">
</p>

# Pokémon Emerald 3Ds Dual Screen

A native Nintendo 3DS port of Pokémon Emerald that uses both screens: the
game on the top screen with an optional 3D (voxel) overworld, and a touch
interface on the bottom screen that replaces the START menu.

## Screenshots

<table>
  <tr>
    <td><img src="https://i.imgur.com/L2mHoFq.png" alt="Screenshot 1" width="400"></td>
    <td><img src="https://i.imgur.com/VrdaKAo.png" alt="Screenshot 2" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/Qxrp6VI.png" alt="Screenshot 3" width="400"></td>
    <td><img src="https://i.imgur.com/tsAvr5X.png" alt="Screenshot 4" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/lvkS1Fh.png" alt="Screenshot 5" width="400"></td>
    <td><img src="https://i.imgur.com/0mf8Pb1.png" alt="Screenshot 6" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/ixEKk8C.png" alt="Screenshot 7" width="400"></td>
    <td><img src="https://i.imgur.com/bMsccID.png" alt="Screenshot 8" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/oMdW10P.png" alt="Screenshot 9" width="400"></td>
    <td><img src="https://i.imgur.com/SsMYGSq.png" alt="Screenshot 10" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/7ALyzMX.png" alt="Screenshot 11" width="400"></td>
    <td><img src="https://i.imgur.com/O0W9Rs3.png" alt="Screenshot 12" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/MvZ5Iw4.png" alt="Screenshot 13" width="400"></td>
  </tr>
</table>

## Features

- **Native 3DS homebrew**: runs on the console's ARM11 (libctru, Citro2D and
  Citro3D), not through an emulator.
- **Two screens**: the game keeps the top screen; the bottom screen is a touch
  interface that takes the place of the START menu.
- **Optional voxel overworld**: the top screen can show the overworld in 3D,
  with modelled buildings, trees, signposts and terrain relief. It is off by
  default: turn on **VOXEL 3D** in the bottom screen's OPTION screen, where
  **3D ANGLE** and **3D ZOOM** then adjust its camera.
- **Your own data**: the game data is built on your computer from your own
  cartridge dump; nothing from the ROM is distributed.

## Current scope of the 3D mode

The modelled part of the voxel overworld is still limited. Only some
buildings, trees, signposts and relief are modelled today; **most of the map
and nearly all interiors are still shown flat**. More areas will be modelled
as the project moves forward.

## About this repository

**This repository contains no game content.** It holds the port's own code,
the tools that build it and the builder that turns *your own* cartridge dump
into the game's data pack. Nothing derived from the ROM is distributed here or
in the releases.

- Engine: [pret/pokeemerald](https://github.com/pret/pokeemerald) decompilation,
  pinned in [`upstream.lock`](upstream.lock), plus the port's changes in
  [`patches/pokeemerald/`](patches/pokeemerald).
- Port: [`3ds_port/`](3ds_port) — ARM11 backend (libctru, Citro2D/Citro3D),
  GPU compositor, NDSP audio, bottom-screen UI, voxel overworld.
- Data: generated on the player's computer by the
  [Pokémon Emerald 3Ds Dual Screen Builder](builder/) from a Pokémon Emerald (USA, Europe) ROM.

## macOS / Spanish adaptation branch

This branch adds a macOS launcher, compiler preflight and native release packaging,
and recognises the clean Spanish BPES ROM. **Spanish gameplay is not ready:** the
pinned decompilation and original v0.1.2 payload are English. A Spanish data build
and matching recipe/executable are still required. Instructions and the tested
scope are in [docs/MACOS.md](docs/MACOS.md).

## Playing

For full installation, sound and update instructions, see
[Install and update](docs/INSTALLATION.md).

You need a 3DS/2DS family console with custom firmware (Luma3DS) and the
Homebrew Launcher, and a clean dump of your own Pokémon Emerald (USA, Europe)
cartridge (SHA-1 `f3ae088181bf583e55daf962a92bb46f4f1d07b7`).

1. Download `Emerald3DS-vX.Y.Z-Windows.zip` from the releases and extract it.
2. Run `Emerald3DS-Builder.exe`, choose your ROM and your SD card, press
   **Install**.
3. Start **Pokémon Emerald 3Ds Dual Screen** from the Homebrew Launcher.

The builder writes `/3ds/emerald3ds/Emerald3DS.3dsx`, `Emerald3DS.smdh` and
`emerald3ds.pak`. The ROM is only read: it is not copied, uploaded or modified,
and no Internet connection is involved. Each release comes with its own
builder; after updating, run it again (the game tells you when the data pack
does not match the release).

On Linux and macOS the builder runs from source:
`python -m emerald3ds_builder install --rom <ROM> --sd <card>` (see
[builder/](builder/)).

## Building from source

```
python tools/bootstrap.py        # pinned upstream + patches + port -> build/upstream
python tools/bootstrap.py --make # also builds the 3DSX there
```

Requirements, the development loop (loose data, data packs, host tests) and
the release process are in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) and
[docs/RELEASING.md](docs/RELEASING.md). How the pieces fit together is in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/ASSET_PIPELINE.md](docs/ASSET_PIPELINE.md).

## Provenance and licences

Only original Pokémon Emerald 3Ds Dual Screen code, tools and documentation are licensed by this
project ([LICENSE-PORT.md](LICENSE-PORT.md)). The decompilation, the game and
third-party components keep their own terms; see [NOTICE.md](NOTICE.md) and
[docs/PROVENANCE.md](docs/PROVENANCE.md). AI-assisted tooling was used during
development: [AI_DISCLOSURE.md](AI_DISCLOSURE.md).

Pokémon Emerald 3Ds Dual Screen is an unofficial fan project, not affiliated with or endorsed by
Nintendo, Game Freak, Creatures or The Pokémon Company. Pokémon and Pokémon
Emerald are trademarks of their respective owners.
