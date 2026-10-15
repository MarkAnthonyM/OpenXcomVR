Play OpenXcom Extended in VR as a tabletop wargame. You sit at a holographic war table in an X-COM command centre: the battlescape is a voxel diorama on the table, the geoscape a globe rising out of it, and your bases a voxel model you build with your fingers. The game's buttons rise out of the table as physical keys, its messages float over the table, and the flat game runs on a screen on the wall behind it.

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

1. Download `OXCE-VR-Prototype-0.12.2-Windows.zip` and unzip it anywhere.
2. Copy the original game's data folders (`GEODATA`, `GEOGRAPH`, `MAPS`, `ROUTES`, `SOUND`, `TERRAIN`, `UFOGRAPH`, `UFOINTRO`, `UNITS`) into the `UFO` folder.
3. Start SteamVR, then run **Play VR.bat**. `Play flat.bat` starts the normal game, and `Desktop preview.bat` shows the VR room in a window without a headset.

Sit (or stand) facing the table. Tap Left B (Index) to recentre; hold it for a second with your hands resting where the tabletop should be to set the table height. `README-VR.md` in the zip has the full controls for each headset.

## Highlights

**Battlescape**
- The mission as a voxel diorama with soldier and alien figures, the game's own cursor and path preview, and voxel fog over unexplored ground.
- Tap a soldier's head to select it, tap a tile to move, drag away from a soldier to turn it. The game's buttons rise out of the table as physical keys; attack options and spotted aliens show up as keys too.
- An inventory tray: pinch items to move them, drop clips on weapons to reload, use the unload pad, and page through what's on the floor.
- Voxel effects for each weapon (tracers, laser beams, plasma, rockets with smoke, explosions, smoke and fire); lasers, plasma and explosions light up the ground at night, and night is as dark as on the flat map.

**Geoscape**
- The globe is a dome rising out of the table, tilted toward you, so the wall screen stays in view. Spin it with your hand; touch a spot with your finger to send a craft there or pick a base site.
- The geoscape's buttons (time speeds, Intercept, Bases, ...) rise out of the console as keys.
- Always-on dashboards on the back-wall monitors: research and production progress, craft status, and UFO activity.

**Bases**
- Your base on the table as a voxel model of the game's own facility pictures, with craft parked in the hangars and new facilities standing in scaffolding with the days left.
- Tap a facility to open its screen (lab → research, workshop → manufacture, hangar → its craft, ...), hold a finger on it to dismantle it, tap an empty spot to build: pick the facility, then its footprint follows your finger and a tap places it.
- The base screen's buttons and a key per base rise from the console.

**Everywhere**
- Messages and screens float over the table as windows that unfold out of a bright projection line. Touch their buttons, drag a finger to scroll lists (or use the right stick). Dogfights float side by side.
- A keyboard appears on the table whenever the game wants text (base names, save names, renames).
- A wrist minimap in battle, live wall screens, shadows, and lighting that reacts to the game.

## Known limitations

- Windows only.
- Floating windows show the game's 320 × 200 pictures; small text can look soft in the headset.
- The inventory tray's floor area shows items up to three cells tall; bigger items are only on the screen's inventory.
- Items lying on the floor aren't shown on the battle map yet.
- Changing video options in-game stops VR; restart the game.

## Licences and source

OpenXcom Extended is GPL-3.0; the zip includes `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt` for the libraries built in, and `BUILD-INFO.txt` with the exact commit. The source for this release is the `proto-0.12.2` tag on the `vr-prototype` branch.

Not affiliated with or endorsed by the owners of X-COM, the OpenXcom team, Valve, Meta or Khronos.
