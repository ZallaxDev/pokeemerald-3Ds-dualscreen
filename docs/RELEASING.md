# Releasing

A release is a set of assets attached to a GitHub release. None of them ever
contains a ROM, a data pack or anything extracted from the game.

| Asset | For |
|---|---|
| `Emerald3DS-vX.Y.Z-Windows.zip` | the Windows builder: builder, engine-only 3DSX, recipe, `README.txt`, `LICENSES/` |
| `Emerald3DS.3dsx`, `Emerald3DS.smdh` | quick update of the executable when the data ABI did not change |
| `Emerald3DS-WebPayload.zip` | the web builder (website): payload, builder package, licences, `web-manifest.json` |
| `web-manifest.json` | the same manifest on its own, read by the website without downloading the payload |
| `SHA256SUMS.txt` | SHA-256 of every asset above |

The website (separate repository) discovers each new release through the
GitHub API and uses these assets as they are: publishing the release is all it
takes for the site and its web builder to offer the new version. The contract
between the two is `builder/emerald3ds_builder/webmanifest.py`; keep asset
names as they are, or bump the manifest's `schemaVersion` together with the
website.

## Prerequisites (maintainer machine)

- A tree from `tools/bootstrap.py --make` (or the maintainer's workspace).
- The supported ROM, and the ELF of the original game built from the same
  pinned upstream (`make` in the upstream tree's root produces
  `pokeemerald.elf`): the recipe generator needs its symbol table to know
  where each table lives in the ROM.
- PyInstaller (`pip install pyinstaller`).

## Steps

```
python tools/build_release.py --version 0.1.0 --rom baserom.gba \
    --gba-elf build/upstream/pokeemerald.elf \
    --make "make -j8 PYTHON=python"
```

This runs `make release`, writes the recipe, assembles
`dist/Emerald3DS-v0.1.0-Windows/` (builder, `payload/Emerald3DS.3dsx`,
`payload/Emerald3DS.smdh`, `payload/emerald3ds.recipe`, `payload/voxelgen/`,
`README.txt`, `LICENSES/`) and zips it, copies the standalone `.3dsx` and
`.smdh` to `dist/`, writes `dist/Emerald3DS-WebPayload.zip` and
`dist/web-manifest.json` (`tools/build_web_payload.py`), audits both ZIPs
(including the ROM scan and, for the web payload, the manifest and its hashes)
and writes `dist/SHA256SUMS.txt`.

The web payload can also be made on its own from any release's `payload/`
folder, for example the one inside an already published Windows ZIP:

```
python tools/build_web_payload.py --payload path/to/payload --version 0.1.2 --out dist
python tools/release_audit.py --zip dist/Emerald3DS-WebPayload.zip --strict --web-payload
```

## Before tagging

1. On a clean Windows machine without Python or devkitPro: extract the ZIP,
   run the builder with the ROM, install to an SD card, boot the game.
2. `emerald3ds-builder-cli verify --pak <SD>/3ds/emerald3ds/emerald3ds.pak`.
3. Open the website's build page with `?payload=local`, choose
   `dist/Emerald3DS-WebPayload.zip` and the ROM, build, and check that the
   ZIP it gives boots the game too (the web builder runs the same code with
   in-process generators).
4. Update `CHANGELOG.md` (keep the `## X.Y.Z — YYYY-MM-DD` headings: the
   website links them to the releases), tag `vX.Y.Z`, and attach every file
   listed in `SHA256SUMS.txt` plus `SHA256SUMS.txt` itself.

Do not mark a release as a draft once it should be offered: the website only
reads published releases. A release marked *pre-release* is shown in the
release history but is not offered as the latest version while a normal
release exists.

## Future: HOME Menu forwarder

When a forwarder CIA exists, attach it as `Emerald3DS-Forwarder.cia` and pass
`--cia-forwarder Emerald3DS-Forwarder.cia` to `tools/build_web_payload.py`
(manifest `assets.ciaForwarder`). The website shows its download only when the
asset is really attached. The forwarder launches
`sdmc:/3ds/emerald3ds/Emerald3DS.3dsx`, so it is installed once and later
updates only replace the `.3dsx`.

The same pack must never be attached to a release or an issue: it is
generated from the player's ROM.
