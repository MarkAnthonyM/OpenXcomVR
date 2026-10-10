Play OpenXcom Extended in VR as a tabletop wargame. You sit at a holographic war table in an X-COM command centre: the battlescape is a voxel diorama on the table, you select soldiers and tiles with your fingers, press the game's buttons on a control console, and pinch items in an inventory tray. The flat game runs on a screen on the wall behind the table.

> **About this prototype:** it was built with Claude, Anthropic's AI model, to find out whether playing X-COM this way is fun. It is, so I'm now rebuilding the VR mode myself on the [`vr`](https://github.com/MarkAnthonyM/OpenXcomVR/tree/vr) branch. This build is a playable preview of the idea; expect rough edges.

## What you need

- Windows 10 or 11 (64-bit)
- SteamVR and a PC VR headset
- Your own copy of **X-COM: UFO Defense** (Steam or GOG). No game data is included.

## Headsets

| Headset | Status |
|---|---|
| Valve Index | Supported and play-tested |
| Meta Quest 3 (Steam Link, Air Link or Virtual Desktop) | Supported, **not tested yet** |
| Steam Frame | Supported, **not tested yet** |

Quest 3 and Steam Frame have their own control layouts, written against their OpenXR controller profiles. If you try one, please open an issue and tell me how it went.

## Install

1. Download `OXCE-VR-Prototype-0.9.1-Windows.zip` and unzip it anywhere.
2. Copy the original game's data folders (`GEODATA`, `GEOGRAPH`, `MAPS`, `ROUTES`, `SOUND`, `TERRAIN`, `UFOGRAPH`, `UFOINTRO`, `UNITS`) into the `UFO` folder.
3. Start SteamVR, then run **Play VR.bat**. `Play flat.bat` starts the normal game, and `Desktop preview.bat` shows the VR room in a window without a headset.

Sit (or stand) facing the table. Tap Left B (Index) to recentre; hold it for a second with your hands resting where the tabletop should be to set the table height. `README-VR.md` in the zip has the full controls for each headset.

## Highlights

- The battlescape as a voxel diorama with soldier and alien figures, the game's own cursor and path preview, and voxel fog over unexplored ground.
- Tap a soldier's head to select it, tap a tile to move, drag away from a soldier to turn it. The game's buttons rise out of the table as physical keys; attack options and spotted aliens show up as keys too.
- An inventory tray: pinch items to move them, drop clips on weapons to reload, use the unload pad, and page through what's on the floor.
- Voxel effects for each weapon: tracers, red laser beams, green plasma, rockets with smoke, explosions, smoke and fire.
- Night missions are as dark as on the flat map, and lasers, plasma, rockets and explosions light up the ground around them.
- A wrist minimap, live wall screens (stats, squad, minimap), shadows, and lighting that reacts to the battle.

## New in 0.9.1

- Lasers, plasma, rocket exhaust, incendiary rounds and blaster bombs light the ground around them as they fly; impacts and explosions flare.
- The hidden-movement fog is gone. During hidden alien movement the table keeps its view and shows nothing new until the aliens come into view.
- Unload pad and floor pages on the inventory tray (0.9.0), tray reloading that follows the game's rules, and night as dark as the flat map (0.8.1).

## Known limitations

- Windows only.
- The inventory tray's floor area shows items up to three cells tall; bigger items are only on the screen's inventory.
- Items lying on the floor aren't shown on the table map yet.
- Typing (save names, base names) needs a keyboard.
- Changing video options in-game stops VR; restart the game.

## Licences and source

OpenXcom Extended is GPL-3.0; the zip includes `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt` for the libraries built in, and `BUILD-INFO.txt` with the exact commit. The source for this release is the `proto-0.9.1` tag on the `vr-prototype` branch.

Not affiliated with or endorsed by the owners of X-COM, the OpenXcom team, Valve, Meta or Khronos.
