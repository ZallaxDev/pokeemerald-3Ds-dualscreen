# Pokémon Emerald 3Ds Dual Screen Builder

Turns the player's own Pokémon Emerald ROM matching a release payload into
`emerald3ds.pak` and installs the game on an SD card.

Release users get a standalone Windows executable (no Python needed). From
source (any OS, Python 3.11+ and Pillow):

```
python -m emerald3ds_builder                          # window
python -m emerald3ds_builder --payload DIR build --rom ROM --output OUT
python -m emerald3ds_builder --payload DIR install --rom ROM --sd /media/me/3DS
python -m emerald3ds_builder --payload DIR verify --pak FILE
python -m emerald3ds_builder detect
```

`DIR` is a release's `payload/` folder (executable, `emerald3ds.recipe`,
`voxelgen/`); by default the builder looks next to itself.

| Module | Role |
|---|---|
| `rom.py` | recognises the supported ROM |
| `recipe.py` | recipe format; rebuilds one file from the ROM and checks it |
| `vtree.py` | rebuilds the voxel generators' inputs from the ROM |
| `voxel.py` | runs the bundled generators |
| `pak.py` | writes and reads the data pack |
| `build.py` | ROM → data pack |
| `install.py` | SD card detection and installation |
| `cli.py`, `gui.py` | the two front ends |

Privacy: the ROM is read into memory and never copied, uploaded or modified;
temporary files are removed even when a step fails.

Tests: `python -m unittest discover -s builder/tests` (synthetic data only).

## Spanish ROM status

Clean BPES dumps are recognised by SHA-1, including trimmed files and ZIPs.
Building requires Spanish data, a Spanish recipe and the corresponding 3DS
executable. This branch does **not** include that payload; the original v0.1.2
payload remains English-only. See [macOS and Spanish work](../docs/MACOS.md).
