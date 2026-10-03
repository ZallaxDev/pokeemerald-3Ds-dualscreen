Pokémon Emerald 3Ds Dual Screen
===============================

A native Nintendo 3DS port of Pokémon Emerald that uses both screens.

This download does NOT contain the game. You need your own dump of your own
Pokémon Emerald cartridge matching this release’s recipe. The builder reads that ROM on this
computer and generates the game's data pack from it. The ROM is not uploaded,
copied or modified, and no Internet connection is needed.

What you need
-------------
- A Nintendo 3DS / 2DS family console with custom firmware (Luma3DS) and the
  Homebrew Launcher.
- A clean English (BPEE) or Spanish (BPES) Emerald ROM and a matching payload.
  English SHA-1: f3ae088181bf583e55daf962a92bb46f4f1d07b7.
  Spanish SHA-1: fe1558a3dcb0360ab558969e09b690888b846dd9.
  Recognition of BPES does not supply Spanish game data. The original v0.1.2
  English payload cannot build from BPES.
- The console's SD card in this computer (or any folder, to copy by hand).

Install
-------
1. Extract this whole ZIP to a folder.
2. Windows: run Emerald3DS-Builder.exe.
   macOS: open Emerald3DS-Builder.command.
   Linux: run ./Emerald3DS-Builder.
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

Generate a standalone CIA
-------------------------
Choose your ROM and press Generar CIA. Choose where to save Esmeralda3DS.cia.
The CIA includes the engine and all game data. Install it with FBI on a console
with Luma3DS and launch it from the HOME Menu. No separate data pack is needed.
Saves remain in /3ds/emerald3ds/emerald3ds.sav, shared with the 3DSX version.
The packaged CIA tools run locally and offline. Console play testing is pending.

Command line
------------
    emerald3ds-builder-cli.exe build   --rom "Pokemon Emerald.gba" --output out
    emerald3ds-builder-cli.exe install --rom "Pokemon Emerald.gba" --sd E:\
    emerald3ds-builder-cli.exe verify  --pak E:\3ds\emerald3ds\emerald3ds.pak
    emerald3ds-builder-cli.exe cia --rom "Pokemon Emerald.gba" --output Esmeralda3DS.cia
    emerald3ds-builder-cli.exe verify-cia --cia Esmeralda3DS.cia

On macOS/Linux omit .exe in the commands above. macOS releases are built
for the architecture in the ZIP name (arm64 or x86_64).

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
