# Linux release workflow

The workflow at `.github/workflows/linux-release.yml` builds a portable Linux x86_64 builder and attaches `Emerald3DS-vX.Y.Z-Linux-x86_64.tar.gz` to a **draft GitHub Release**. It also updates `SHA256SUMS.txt`.

## Release procedure

1. Update the project version in `builder/pyproject.toml` and `builder/emerald3ds_builder/__init__.py` as part of the usual release process.
2. Create a **draft** GitHub Release for tag `vX.Y.Z`.
3. Upload the matching `Emerald3DS-vX.Y.Z-Windows.zip` from the normal release build. It contains the exact `payload/` (recipe, 3DSX/SMDH, and voxel generators) required by the Linux builder.
4. Open **Actions → Linux Release → Run workflow**, enter the draft release tag, and start the workflow.
5. Confirm that the Linux `.tar.gz` and updated `SHA256SUMS.txt` are attached, then publish the release manually.

The workflow uses a draft release because GitHub repositories may enable immutable releases, where assets cannot be changed once the release is published. The action intentionally does not publish it automatically.

This does not create a Linux-native game executable: the game is Nintendo 3DS homebrew. The Linux deliverable is the Builder, which processes a player's own supported ROM locally and generates the data pack. Neither a ROM nor a generated game data pack is added to the release.
