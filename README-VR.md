# OpenXcom Extended — VR Tabletop (prototype)

Play OpenXcom Extended in VR as if it were a tabletop wargame. You stand in an
X-COM command center at a holographic war table:

- **Battlescape** – the mission map is rebuilt in 3D on the table as a
  diorama. Soldiers and aliens are cardboard stand-up figures on colored bases
  (blue = X-COM, red = aliens, green = civilians). Upper floors lift off like a
  dollhouse. Point at a tile and pull the trigger to move or shoot.
- **Geoscape** – a physical globe hovers over the table, with bases, craft,
  UFOs and mission sites as markers and the real day/night terminator.
- **Everything else** (menus, inventory, research, base management) is on the
  big game screen behind the table. Point at it and use the trigger as a mouse.

This is an engine fork (a ruleset mod can't add a VR renderer), but it reads the
game state generically, so content mods such as X-Com Files should work.

## Setting it up

1. Copy the original **X-COM: UFO Defense** data into the `UFO` folder next to
   the executable (the folders `GEODATA`, `GEOGRAPH`, `MAPS`, `ROUTES`,
   `SOUND`, `TERRAIN`, `UFOGRAPH`, `UFOINTRO`, `UNITS`).
2. Start **SteamVR** and make sure it is your OpenXR runtime
   (SteamVR → Settings → OpenXR → "Set SteamVR as OpenXR runtime").
3. Launch:
   - Windows: `Play VR.bat` (or `Desktop preview.bat`, `Play flat.bat`)
   - Linux: `./play-vr.sh` (or `./desktop-preview.sh`, `./play-flat.sh`)

If no headset or OpenXR runtime is found, the game starts in normal flat mode
and writes the reason into `openxcom.log`. Look for lines starting with `[VR]`.

The mode is the `vrMode` option: `0` = flat, `1` = headset, `2` = desktop preview.
It can be passed on the command line (`-vrMode 2`) or set in `options.cfg`.

## Controls (Valve Index / Touch / Vive wands)

| Input | What it does |
|---|---|
| Trigger | Click on the screen, pick a tile on the table, click the globe |
| A (right) | Right-click (turn a soldier, cancel) |
| B (right) | Back / Escape |
| Stick up/down | Scroll lists; over the table: map level up/down; over the globe: zoom |
| Grip over the table | Slide the map (one hand), zoom + turn it (both hands) |
| Grip on the globe | Spin the globe |
| Grip on the screen | Pick the screen up and put it somewhere else |
| Grip elsewhere | Move yourself (one hand), turn the room (both hands) |
| Left A | Put the screen on your left hand as a tablet (again to put it back) |
| Left B | Recenter |
| Other stick left/right | Snap turn |

Whichever hand pulled its trigger last is the pointing hand.

**Desktop preview** (`vrMode 2`) shows the same room on your monitor: the mouse
aims from your eyes, clicks work on the screen and the table, and holding the
middle mouse button lets you look around. Useful for trying things without the
headset on.

## Known limitations

- The headset path (OpenXR) was built against the spec and SteamVR's documented
  behaviour but **has not yet been run on a real headset**. Everything else was
  tested in the desktop preview with the original game data.
- Changing video options in-game recreates the OpenGL context, which stops VR
  (restart the game).
- Typing (base names, save names) needs the real keyboard.
- Projectiles and explosions on the table are simple glowing markers; smoke,
  fire and floor items are not shown on the table yet (they are on the screen).
- Linux build needs glibc 2.38+ (SteamOS 3.6+, Arch, Ubuntu 24.04+, Fedora 39+)
  and the SDL 1.2 family: `sdl12-compat` (or `libsdl1.2`), `SDL_image 1.2`,
  `SDL_mixer 1.2`. `SDL_gfx` and the OpenXR loader are bundled.

## How it works (for the rebuild)

All VR code lives in `src/VR/`; the engine only gets small hooks.

| File | Role |
|---|---|
| `VrApi.h` | The only header the engine includes. No-ops when built without `OXCE_VR`. |
| `VrXr.*` | OpenXR: instance, session on the engine's GL context (Xlib or Win32 binding), stereo swapchains, action bindings for Index/Touch/Vive/simple controllers, haptics. |
| `VrSystem.cpp` | Frame loop, tracking-space → world "rig", the controller → mouse bridge (synthetic SDL events), grabbing, desktop preview camera, test automation (`OXCE_VR_SCRIPT`). |
| `VrRoom.*` | Procedural command-center room, war table, controller models. |
| `VrBoard.*` | Everything on the table: battlescape diorama, unit standees, tile picking, globe. |
| `VrGL.*`, `VrShaders.h` | Tiny GL helper layer and the single scene shader (linear lighting into an sRGB target). |

Key ideas:

- **Terrain from line-of-fire voxels.** Every terrain part already has a
  16×16×24 voxel shape (`LOFTEMPS`, via `MapData::getLoftID`). Exposed voxel
  faces are colored by projecting them back into the part's isometric sprite
  (`px = 16 + x − y`, `py = 24 + (x + y)/2 − z + yOffset`), merged with greedy
  meshing, and textured from an atlas. Parts with no voxels (grass tufts) become
  crossed cards. Meshes are built per unique `MapData` and stitched into 8×8
  tile chunks, which are rebuilt only when their tiles change (doors,
  destruction, fog of war, lighting).
- **Standees** are drawn with the game's own `UnitSprite`, after temporarily
  rotating the unit's facing by the angle you view it from, so a soldier seen
  from behind shows its back.
- **Input** reuses the game's own code paths: screen clicks become SDL mouse
  events; table clicks call `BattlescapeState::vrTileClick`, which mirrors
  `mapClick` (primary/secondary action on a tile); globe clicks are converted
  back into a click on the flat globe.
- **The flat camera and the table stay in sync.** The table follows the game's
  camera center and view level, and sliding the map on the table moves the
  game's camera.

Engine hooks: `Game.cpp` (startup, event filter, per-loop `VR::frame`),
`Screen.cpp` (hand the composed frame to VR), `Options` (`vrMode`),
`BattlescapeState::vrTileClick`, `Map::setSelectorTile`,
`BattleUnit::get/setFacingSnapshot`, `Globe::getCenter`, `Game::getStates`, and
`SDL_GetMouseState`/`SDL_WarpMouse` calls routed through `VR::getMouseState` /
`VR::warpMouse` so the laser pointer is the mouse.

## Building

Linux: SDL 1.2, SDL_image/mixer/gfx 1.2, OpenGL, the OpenXR SDK (loader +
headers) and glm, then the usual `cmake -B build && cmake --build build`.
`-DOXCE_VR=OFF` builds the normal game.

Windows: builds with MinGW (llvm-mingw was used) against static SDL 1.2
libraries and the static OpenXR loader; pass `-DDEPS_DIR=<nonexistent>` so
CMake uses pkg-config.
