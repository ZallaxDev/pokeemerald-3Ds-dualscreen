Pokémon Emerald 3Ds Dual Screen
===============================

A native Nintendo 3DS port of Pokémon Emerald that uses both screens.

This download does NOT contain the game. You need your own dump of your own
Pokémon Emerald (USA, Europe) or Pokémon Esmeralda (Spain) cartridge; the game
is built in that ROM's language. The builder reads that ROM on this
computer and generates the game's data pack from it. The ROM is not uploaded,
copied or modified.

The follower Pokémon sprites and Gen 6 icons are not in the ROM: the
builder downloads them from GitHub (aarant/pokeemerald, at the version this
release was made with, each file checked against its SHA-256) and keeps them
in %LOCALAPPDATA%\emerald3ds-builder for later builds. The first build needs
an Internet connection for that; nothing else is sent or received.

What you need
-------------
- A Nintendo 3DS / 2DS family console with custom firmware (Luma3DS) and the
  Homebrew Launcher.
- A clean ROM of Pokémon Emerald (USA, Europe),
  SHA-1 f3ae088181bf583e55daf962a92bb46f4f1d07b7, or of Pokémon Esmeralda
  (Spain), SHA-1 fe1558a3dcb0360ab558969e09b690888b846dd9.
- The console's SD card in this computer (or any folder, to copy by hand).

Install
-------
1. Extract this whole ZIP to a folder.
2. Run Emerald3DS-Builder.exe.
3. Choose your ROM. The builder checks that it is the supported one.
4. Choose your SD card (it is detected when it has a "Nintendo 3DS" folder)
   or any folder.
5. Press Install. It writes:
       /3ds/emerald3ds/Emerald3DS.3dsx
       /3ds/emerald3ds/Emerald3DS.smdh
       /3ds/emerald3ds/emerald3ds.pak
6. Put the card back in the console and start Pokémon Emerald 3Ds Dual Screen from the
   Homebrew Launcher.

Saves are kept in /3ds/emerald3ds/emerald3ds.sav. Reinstalling or updating
never touches it.

Command line
------------
    emerald3ds-builder-cli.exe build   --rom "Pokemon Emerald.gba" --output out
    emerald3ds-builder-cli.exe install --rom "Pokemon Emerald.gba" --sd E:\
    emerald3ds-builder-cli.exe verify  --pak E:\3ds\emerald3ds\emerald3ds.pak

Updating
--------
Each release comes with its own builder. Run the new builder again with the
same ROM: a data pack only works with the release that generated it, and the
game tells you when they do not match.

Legal
-----
Pokémon Emerald 3Ds Dual Screen is an unofficial fan project, not affiliated with or endorsed by
Nintendo, Game Freak, Creatures or The Pokémon Company. Pokémon and Pokémon
Emerald are trademarks of their respective owners. See LICENSES/.
