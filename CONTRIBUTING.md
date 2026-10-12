# Contributing

Thanks for your interest. A few rules keep this repository publishable.

## Never commit

- ROMs, saves, `emerald3ds.pak`, or any file extracted or generated from a ROM
  (graphics, maps, audio, text, tables, voxel data);
- logs, screenshots from test runs, editor or AI-agent state, chat
  transcripts;
- build outputs (`*.3dsx`, `*.elf`, `3ds_port/romfs/` contents).

`python tools/release_audit.py --repo . --strict` must pass; CI runs it on
every pull request. New images or audio need a provenance entry in
`tools/release_audit_allow.toml` and must be your own work.

## Changes to the decompilation

The upstream tree is not part of this repository: its changes live in
`patches/pokeemerald/`. Prefer a hook in `3ds_port/` over editing a game
file, and keep game-side edits minimal and behind `PORT_BRIDGE` or
`PLATFORM_3DS`. To change a patch, edit the bootstrapped tree in
`build/upstream`, then regenerate the patches from it (`git diff` against the
pinned commit, one file per topic; see the headers of the existing patches).

## Code

- C99/GNU99. Native units build with `-Werror`.
- Match the surrounding code; comments explain *why*.
- Open pull requests against `dev`; `main` holds the latest release.
- Run `make verify` and the builder's tests before opening a pull request.
- Test on hardware when you touch rendering, memory, timing or file I/O, and
  say what you tested in the pull request.

## AI-assisted contributions

Welcome, under the same standard as any other: you are responsible for what
you submit, it must be reviewed and tested, and prompts or transcripts do not
belong in the repository. See `AI_DISCLOSURE.md`.
