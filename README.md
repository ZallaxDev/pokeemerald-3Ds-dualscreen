<p align="center">
  <img src="https://i.imgur.com/Its9ceu.png" alt="Pokémon Emerald 3Ds Dual Screen" width="480">
</p>

<h1 align="center">Pokémon Emerald 3Ds Dual Screen</h1>

<p align="center">
  <strong>Rediscover Hoenn. Two screens. A new perspective.</strong><br>
  A native Nintendo 3DS port with a touch interface and an optional voxel overworld.
</p>

<p align="center">
  <a href="https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen/releases/latest"><img src="https://img.shields.io/badge/Download-Latest_release-168B67?style=for-the-badge&amp;logo=github&amp;logoColor=white" alt="Download the latest release"></a>
  <a href="https://x.com/DustZallax"><img src="https://img.shields.io/badge/Follow-%40DustZallax-18181B?style=for-the-badge&amp;logo=x&amp;logoColor=white" alt="Follow @DustZallax on X"></a>
  <a href="https://discord.com/invite/tfqHF8496P"><img src="https://img.shields.io/badge/Discord-Join_the_community-5865F2?style=for-the-badge&amp;logo=discord&amp;logoColor=white" alt="Join the community on Discord"></a>
  <a href="https://ko-fi.com/zallax"><img src="https://img.shields.io/badge/Ko--fi-Buy_me_a_coffee-FF5E5B?style=for-the-badge&amp;logo=kofi&amp;logoColor=white" alt="Buy Zallax a coffee on Ko-fi"></a>
</p>

<p align="center">
  <a href="#screenshots">Screenshots</a> &nbsp; · &nbsp;
  <a href="#features">Features</a> &nbsp; · &nbsp;
  <a href="#getting-started">Getting started</a> &nbsp; · &nbsp;
  <a href="#documentation">Documentation</a> &nbsp; · &nbsp;
  <a href="#community">Community</a> &nbsp; · &nbsp;
  <a href="#support-the-project">Support the project</a>
</p>

---

Pokémon Emerald 3Ds Dual Screen brings the adventure to Nintendo 3DS as
**native homebrew**. The game runs on the top screen, while a dedicated touch
interface on the bottom screen replaces the START menu. Enable the optional
**voxel overworld** to explore supported areas from a new angle.

## Screenshots

<p align="center"><em>A look at the adventure across both screens.</em></p>

<table>
  <tr>
    <td><img src="https://i.imgur.com/iIWrkvW.png" alt="Screenshot 1" width="400"></td>
    <td><img src="https://i.imgur.com/XyYVdLh.png" alt="Screenshot 2" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/Ge6YSZ2.png" alt="Screenshot 3" width="400"></td>
    <td><img src="https://i.imgur.com/tYlqIdF.png" alt="Screenshot 4" width="400"></td>
  </tr>
</table>

<details>
<summary><strong>View more screenshots · 9 more images</strong></summary>

<br>

<table>
  <tr>
    <td><img src="https://i.imgur.com/W2iUYUy.png" alt="Screenshot 5" width="400"></td>
    <td><img src="https://i.imgur.com/dkz6pIr.png" alt="Screenshot 6" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/l2D6kr5.png" alt="Screenshot 7" width="400"></td>
    <td><img src="https://i.imgur.com/hdqRrCC.png" alt="Screenshot 8" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/I5p7CB0.png" alt="Screenshot 9" width="400"></td>
    <td><img src="https://i.imgur.com/oIOiK1B.png" alt="Screenshot 10" width="400"></td>
  </tr>
  <tr>
    <td><img src="https://i.imgur.com/aYtWyER.png" alt="Screenshot 11" width="400"></td>
    <td><img src="https://i.imgur.com/cf0bucr.png" alt="Screenshot 12" width="400"></td>
  </tr>
  <tr>
    <td colspan="2" align="center"><img src="https://i.imgur.com/CYa2gEP.png" alt="Screenshot 13" width="400"></td>
  </tr>
</table>

</details>

## Features

| | What to expect |
| :--- | :--- |
| **Native 3DS homebrew** | Runs directly on the console's ARM11 using libctru, Citro2D and Citro3D, without an emulator. |
| **Two screens, one adventure** | The game on top, with a dedicated touch interface below in place of the START menu. |
| **Optional voxel overworld** | Modelled buildings, trees, signposts and terrain relief, with adjustable camera angle and zoom. |
| **Built from your own cartridge** | The builder generates the game data locally from your own dump. No ROM content is distributed. |

### A new perspective on Hoenn

The voxel overworld is **off by default**. Open **OPTION** on the bottom
screen and enable **VOXEL 3D**, then adjust **3D ANGLE** and **3D ZOOM** to
set your camera.

> **3D mode is a work in progress.** Only some buildings, trees, signposts
> and terrain relief are modelled today. Most of the map and nearly all
> interiors are still shown flat. More areas will be modelled as the
> project moves forward.

## Getting started

### What you need

- A **Nintendo 3DS / 2DS family console** with custom firmware (Luma3DS)
  and the Homebrew Launcher.
- A **clean dump of your own Pokémon Emerald (USA, Europe) cartridge**.
- Your console's **SD card**, connected to your computer.

Supported ROM SHA-1:

```text
f3ae088181bf583e55daf962a92bb46f4f1d07b7
```

### Install on Windows

1. Download `Emerald3DS-vX.Y.Z-Windows.zip` from the
   [latest release](https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen/releases/latest)
   and extract the whole ZIP.
2. Run `Emerald3DS-Builder.exe`, choose your ROM and your SD card, press
   **Install**.
3. Make sure `SD:/3ds/dspfirm.cdc` is present for sound, put the SD card back
   in your console and launch **Pokémon Emerald 3Ds Dual Screen** from the
   Homebrew Launcher.

For the complete walkthrough, including sound setup and updates, see the
**[installation guide](docs/INSTALLATION.md)**.

The builder writes `/3ds/emerald3ds/Emerald3DS.3dsx`, `Emerald3DS.smdh` and
`emerald3ds.pak`. The ROM is only read: it is not copied, uploaded or modified,
and the builder needs no Internet connection.

**Updating?** Run the new release's builder again to generate a matching
data pack. The game tells you if the pack does not match the release.
Installing or updating does not overwrite your save.

<details>
<summary><strong>Linux and macOS installation</strong></summary>

Run the builder from source with **Python 3.11+ and Pillow**, using the
`payload/` folder from the matching release ZIP:

```sh
python -m emerald3ds_builder --payload /path/to/payload install --rom /path/to/rom.gba --sd /path/to/card
```

See the [builder documentation](builder/) for setup and additional commands.

</details>

## About this repository

**This repository contains no game content.** It holds the port's own code,
the tools that build it and the builder that turns *your own* cartridge dump
into the game's data pack. Nothing derived from the ROM is distributed here or
in the releases.

| Component | Location | Role |
| :--- | :--- | :--- |
| Engine | [pret/pokeemerald](https://github.com/pret/pokeemerald) · [`upstream.lock`](upstream.lock) | The Pokémon Emerald decompilation, pinned to a specific revision. |
| Port patches | [`patches/pokeemerald/`](patches/pokeemerald) | The port's changes to the upstream engine. |
| 3DS backend | [`3ds_port/`](3ds_port) | ARM11 backend, GPU compositor, NDSP audio, touch UI and voxel overworld. |
| Data builder | [`builder/`](builder/) | Creates the data pack locally from the player's own supported ROM. |

## Spanish ROMs, macOS and CIA export

This branch adds Spanish game builds from a clean BPES ROM, a native macOS
Apple Silicon builder and **Generate CIA** for a complete installable game.
The original English payload still requires BPEE. ROMs and generated game
data are processed locally and excluded from the repository and builder ZIP.
See [docs/MACOS.md](docs/MACOS.md) for installation and build instructions.

The user has confirmed the Spanish ROM acceptance and bottom-screen resource
corrections on console using the v0.1.2-based build. Integration with the latest
upstream features requires separate console testing.

## Building from source

```sh
python tools/bootstrap.py        # pinned upstream + patches + port -> build/upstream
python tools/bootstrap.py --make # also builds the 3DSX there
```

Start with the [development guide](docs/DEVELOPMENT.md) for requirements,
the development loop, loose data, data packs and host tests.

## Documentation

| Guide | What's inside |
| :--- | :--- |
| [Install and update](docs/INSTALLATION.md) | Installation, sound setup and updating an existing installation. |
| [Development](docs/DEVELOPMENT.md) | Build requirements, workflow and tests. |
| [Architecture](docs/ARCHITECTURE.md) | How the engine and the 3DS backend fit together. |
| [Asset pipeline](docs/ASSET_PIPELINE.md) | How game data is prepared for the port. |
| [Releasing](docs/RELEASING.md) | Building and packaging a release. |
| [Contributing](CONTRIBUTING.md) | Guidelines for contributing to the project. |
| [Changelog](CHANGELOG.md) | Changes across releases. |

## Community

Join the **[Discord community](https://discord.com/invite/tfqHF8496P)** to
talk about the project and share your adventures in Hoenn. Follow
**[@DustZallax on X](https://x.com/DustZallax)** for project updates and
to stay in touch.

<p align="center">
  <a href="https://discord.com/invite/tfqHF8496P"><img src="https://img.shields.io/badge/Join_us_on-Discord-5865F2?style=for-the-badge&amp;logo=discord&amp;logoColor=white" alt="Join us on Discord"></a>
</p>

## Support the project

If you're enjoying this new way to explore Hoenn, you can **buy me a coffee**
on Ko-fi. It's an optional way to support my work on the project, and every
coffee is appreciated. Thank you for being part of the adventure!

<p align="center">
  <a href="https://ko-fi.com/zallax"><img src="https://img.shields.io/badge/Buy_me_a_coffee-Support_on_Ko--fi-FF5E5B?style=for-the-badge&amp;logo=kofi&amp;logoColor=white" alt="Buy Zallax a coffee — support on Ko-fi"></a>
</p>

You can also support the project by starring the repository, sharing it
or [contributing](CONTRIBUTING.md).

---

## Provenance and licences

Only original Pokémon Emerald 3Ds Dual Screen code, tools and documentation are licensed by this
project ([LICENSE-PORT.md](LICENSE-PORT.md)). The decompilation, the game and
third-party components keep their own terms; see [NOTICE.md](NOTICE.md) and
[docs/PROVENANCE.md](docs/PROVENANCE.md). AI-assisted tooling was used during
development: [AI_DISCLOSURE.md](AI_DISCLOSURE.md).

Pokémon Emerald 3Ds Dual Screen is an unofficial fan project, not affiliated with or endorsed by
Nintendo, Game Freak, Creatures or The Pokémon Company. Pokémon and Pokémon
Emerald are trademarks of their respective owners.
