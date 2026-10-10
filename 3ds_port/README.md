# 3ds_port

The Nintendo 3DS port. It is built inside a bootstrapped upstream tree
(`python tools/bootstrap.py`), where it lives at `3ds_port/` next to the
decompilation it compiles.

| Path | What |
|---|---|
| `src/main_3ds.c`, `3ds_platform.c` | process, heaps, frame pacing, lifecycle |
| `src/3ds_video.c`, `3ds_video_decode.c` | GPU compositor: GBA registers/VRAM/OAM to Citro3D |
| `src/3ds_audio.c` | mixer frames to NDSP |
| `src/3ds_input*.c` | buttons, Circle Pad, touch |
| `src/3ds_data.c`, `3ds_pak.c` | game data backends (romfs, loose, pack) |
| `src/3ds_assets.c`, `3ds_map_loader.c`, `3ds_script_loader.c` | INCBIN stubs, map payloads, bundled regions |
| `src/3ds_compat.c`, `3ds_game_full.c`, `3ds_game_bridge.c` | the bridge: saves, fonts, entry into `AgbMain` |
| `src/3ds_bottom_ui.c`, `3ds_bottom_screen.c` | bottom-screen interface |
| `src/voxel/` | voxel overworld |
| `compat/` | headers game translation units see (`port_platform.h`, staging modes) |
| `scripts/` | build-time generators and checks (bundles, voxel data) |
| `tests/` | host tests (`make verify`) |
| `emerald3ds.ld.in` | linker script with the `.gamedata` section |

See `docs/ARCHITECTURE.md` and `docs/DEVELOPMENT.md` at the repository root.


## Console-clock lighting

OPTIONS → ENHANCEMENTS → DAY/NIGHT enables outdoor lighting based on the
console's local wall clock. It is off by default and saved as `day_night=1`
in the port settings. The bedroom clock and fast-forward do not change it.

The 2D field and voxel world transition through night, dawn, daylight and
sunset. Interiors, caves and menus keep their normal lighting. With
`VOXEL_LIGHTING=1` (the default), cast shadows follow an approximate sun/moon
arc; direction changes every ten console-clock minutes through the existing
chunk rebuild budget.

Annotated glass panes and lanterns glow at night. They fade in from 18:00 to
20:00 and out from 05:00 to 07:00. Their cached overlay respects window
frames, includes the lower door gradients, and uses a temporary DEPTH16 bias
without changing its screen position or writing scene depth. This adds
emissive surfaces, rather than a dynamic point-light pass.

The building generator writes optional glass bounds after the VXB7 vertices;
files with a zero rectangle count remain readable. The retained light mesh
is bounded and compacted after extraction, and reused until the camera origin,
world epoch or required texture pages change. Updated game data must match
the executable's ABI.

Run `make verify-daynight verify-night-lights` from `3ds_port` for the portable
clock and clipping/depth checks. In a bootstrapped tree, `make verify-night-pack`
also checks the generated window and lantern data. For a local software preview:

```sh
cd romfs
../build/night_buildings_test.exe ../build/night-mesh.bin
cd ..
python scripts/preview_night_windows.py --pack romfs/voxel/buildings.bin \
    --lights build/night-mesh.bin --out build/night-preview.png
```

The preview uses the C-emitted light mesh and rejects a dump whose fingerprint
does not match the building pack. Its output is ROM-derived and must stay
local; it is not a hardware screenshot.
