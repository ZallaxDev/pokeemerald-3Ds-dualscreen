# Changelog

## Unreleased

- Releases also carry `Emerald3DS.3dsx` and `Emerald3DS.smdh` on their own
  (quick update) and `Emerald3DS-WebPayload.zip` with `web-manifest.json` for
  the web builder.
- The builder can run the voxel generators in-process (used by the web
  builder); the Windows builder and the command line are unchanged.
- The Windows ZIP's `LICENSES/` folder again includes `LICENSE-PORT.md`,
  `NOTICE.md` and `AI_DISCLOSURE.md`.

## 0.1.2 — 2026-09-30

New and improved:

- PokéNav on the bottom screen, driven by touch: laid out as header, body
  and help bar, a tap moves the cursor straight to the option or entry, and
  the Hoenn map scrolls smoothly.
- 3D slider: the intro and the title screen now have stereo depth, and 2D
  screens keep blended sprites and text windows at their own depth.
- Battle transitions now play in both the classic 2D field and the voxel
  overworld, at full speed on Old 3DS.
- Voxel: the world fades with the screen on warps and battle starts.
- Voxel: characters are drawn at their proper proportions, stand on their
  feet and meet their shadow; the walking bob is kept.
- Voxel: walls and mountain sides facing away from the sun are in shade,
  lit by the same sun as the cast shadows.
- Voxel: newly modelled Rustboro's gym (its statues stand in all eight gyms),
  Oldale's two houses, Mr Briney's cottage and the Pretty Petal flower shop.
- Voxel: every mountain of the general tileset is drawn; Route 116's
  mountain is built from straight faces and one-level steps; stairs, lamps
  and Rustboro's railings stand again.
- Voxel: Route 104's beach and sea are a level below the route, and Routes
  105 and 106 follow them down.
- Intro, title and menus: the margins around the 240x160 picture are made
  from each screen's own art (Rayquaza stands on the bottom edge under a lit
  sky, the Professor's sky reaches the top), and menu screens are centred.
- Title screen and palette fades much smoother on Old 3DS: layer textures
  are redrawn only where they change, and fades are drawn as a tint.
- Installation guide in `docs/INSTALLATION.md`, and bug report and feature
  request forms on GitHub.

Fixes:

- PokéNav tiles and palettes were wrong.
- With the 3D slider up, black screens came out blue and the save selection
  lost its backdrop; the intro's leaves scene slowed down.
- Voxel: sprites went black on their own during warps and battle starts.
- Voxel: cast shadows striped and came apart from the feet.
- Voxel: black gaps along the shores of recessed water (now a rim).
- Voxel: ledges were sunk into a toothed trench; they now sit on the ground
  as on Route 101, and side ledges are no longer striped by the lighting.
- Voxel: Rustboro's lamps against walls did not stand.

## 0.1.1 — 2026-09-27

- The voxel overworld is now off by default. It is switched on from the new
  **VOXEL 3D** row of the bottom-screen OPTION screen.
- While it is on, **3D ANGLE** (34-46 degrees, default 40) and **3D ZOOM**
  (90-120%, default 100%) adjust its camera.
- These settings are kept in `/3ds/emerald3ds/settings.txt` and apply at once,
  without saving the game.
- Classic 2D field: text boxes, prompts and the map name are centred as in
  the voxel view.
- Classic 2D field: much faster on Old 3DS; the backgrounds are kept in
  textures and only the cells that change are redrawn.
- Classic 2D field: sprites just below the view are no longer drawn at the
  top of the screen.

## 0.1 — 2026-09-26 — first public version

- Native ARM11 port of pokeemerald for Nintendo 3DS: GPU compositor at
  native 400x240, NDSP audio, touch and Circle Pad input, SD saves.
- Bottom-screen interface replacing the START menu (map, party, bag, trainer
  card, Pokédex, PokéNav, save, options) and touch battle menus.
- Optional voxel overworld with buildings, trees, signposts and terrain relief
  modelled from each map's own art, fixed-sun lighting and cast shadows.
  The modelled part is still limited: most of the map and nearly all
  interiors are shown flat for now.
- Game data outside the executable: embedded (development), loose files or a
  single `emerald3ds.pak` with ABI and integrity checks.
- Pokémon Emerald 3Ds Dual Screen Builder: generates the data pack from the player's own ROM and
  installs the game on an SD card, with no toolchain or Python required on
  Windows.
