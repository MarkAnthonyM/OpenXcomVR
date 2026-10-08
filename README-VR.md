# OpenXcom Extended — VR Tabletop (prototype)

Play OpenXcom Extended in VR as if it were a tabletop wargame. You sit (or
stand) in an X-COM command center at a holographic war table:

- **Battlescape** – the mission map is rebuilt in 3D on the table as a
  diorama. Soldiers and aliens are cardboard stand-up figures on colored bases
  (blue = X-COM, red = aliens, green = civilians). Upper floors lift off like a
  dollhouse. Point at a tile and pull the trigger, or tap it with your finger,
  to move or shoot. The game's 3D cursor is a wireframe box on the table (red
  over an empty tile, flashing yellow over a unit, blue below the current
  level), the selected soldier has a yellow box and the bobbing arrow over its
  head, and the path preview arrows and TU / energy numbers appear on the tiles
  when the game's path preview option is on. The hit chance the flat screen
  shows next to the aiming cursor (UFO Extender accuracy option) floats next
  to the box. Unexplored ground is buried under a bank of voxel fog (you can
  still tap into it to target a wall); projectiles, explosions, smoke and fire
  are voxel effects styled after the weapon: tracers, red laser beams that
  leave crackling static hanging in the air, green plasma bolts, rockets with
  smoke trails. Within each weapon class the effect grows bolder with the
  weapon's damage (a heavy laser is much more intense than a laser pistol).
  During your turn the table keeps its own view and smoothly keeps a walking
  soldier in view. During the alien turn it follows the game, and while the
  flat game shows **Hidden Movement**, a bank of fog rises out of the table
  and covers the whole map until an alien comes into view.
- **Geoscape** – a physical globe hovers over the table, with bases, craft,
  UFOs and mission sites as markers and the real day/night terminator.
- **The table is a control panel.** Right in front of you along the near edge
  is a console: when a battle starts, hatches open and physical buttons rise for
  the battlescape controls (kneel, end turn, next soldier, level up/down, ...).
  Press them with your finger. Both weapon buttons sit at the left end of the
  bay, closest to you; End Turn and Abort are at the far right end. Left of the
  buttons is an inventory tray for the selected soldier, about as deep as the
  buttons: the game's sections (hands, shoulders, legs, backpack, belt, ground)
  repacked into one row, with small floating voxel models of the items; pinch
  one to move it. When the game asks how to fire (aimed / snap / auto, throw),
  a board of keys unfolds over the console, and spotted aliens show up as red
  buttons right of the bay (press one to centre the view on that alien), so
  you never need to look up at the screen for those.
- **Your hands** are tracked finger by finger (Valve Index knuckles) and collide
  with the table and the buttons: a finger pressed against the table edge stops
  there and bends. Tap a soldier's head to select it, tap a tile to walk there.
- **Your right wrist** carries a minimap of the whole battlefield: turn the back
  of your wrist toward you like checking a watch, and tap the map with your left
  index finger to move the table there.
- **The back wall** holds the game screen (menus, research, base management:
  point at it and use the trigger as a mouse) with the selected soldier's stats,
  the squad roster and a minimap beside it.
- **The room reacts**: the light over the table casts shadows of the figures and
  your hands onto the map, the table glows up onto everything above it, the
  screens tint the room, muzzle flashes and explosions light it up, night
  missions turn the room dark with dim blue strips so the table is the main
  light, and red alert lights sweep round while aliens are in sight.

This is an engine fork (a ruleset mod can't add a VR renderer), but it reads the
game state generically, so content mods such as X-Com Files should work.

## Setting it up

1. Copy the original **X-COM: UFO Defense** data into the `UFO` folder next to
   the executable (the folders `GEODATA`, `GEOGRAPH`, `MAPS`, `ROUTES`,
   `SOUND`, `TERRAIN`, `UFOGRAPH`, `UFOINTRO`, `UNITS`).
2. Start **SteamVR** and make sure it is your OpenXR runtime
   (SteamVR → Settings → OpenXR → "Set SteamVR as OpenXR runtime").
3. Launch `Play VR.bat` (or `Desktop preview.bat`, `Play flat.bat`).

If no headset or OpenXR runtime is found, the game starts in normal flat mode
and writes the reason into `openxcom.log`. Look for lines starting with `[VR]`.

The mode is the `vrMode` option: `0` = flat, `1` = headset, `2` = desktop preview.
It can be passed on the command line (`-vrMode 2`) or set in `options.cfg`.
The war table is 1.9 × 1.2 m with a 3.5 cm rim; `vrTableScale` (percent,
60–150, default 100) makes it bigger or smaller. Its near edge and the control
console stay where they are, sized for your hands.

**Seat height:** rest both hands where you want the tabletop to be (as if they
lay flat on it) and hold **Left B** for a second. The room moves so the table
meets your palms; the result is saved as `vrSeatOffset` (millimetres), so a tall
chair is a one-time setup. `vrShadows` (default on) turns the table shadows off
if your PC needs the frame time.

## Controls (Valve Index / Touch / Vive wands)

| Input | What it does |
|---|---|
| Left stick | Slide the view across the map (push forward = travel forward from where you look); spins the globe on the geoscape |
| Right stick left/right | Turn the map |
| Trackpad swipe up/down (either hand) | Zoom the map / globe |
| Right stick up/down at the screen | Scroll lists |
| Trigger | Laser click on the screen, a tile, the globe, a table button or key |
| A (right) | Right-click with the laser (turn a soldier, cancel) |
| B (right) | Back / Escape |
| Left B | Recenter; hold for a second to set the seat height |
| Squeeze grip over the table | Slide the map by hand (one hand), zoom + turn (both hands) |
| Squeeze grip elsewhere | Move yourself (one hand), turn the room (both hands) |

Whichever hand pulled its trigger last is the pointing hand. The controls are
the same whether you point at the game screen or not: only the trigger clicks
the screen, and right stick up/down scrolls lists there. Pointing at the screen
never moves the map (edge and drag scrolling are off in VR).

The table follows the game's camera when it moves on purpose (next soldier, an
alien button, the alien turn) and ignores its automatic "centre on soldier"
after every move. During the alien turn you can't move the map; you can turn
and zoom it while the fog is lifted.

Grabbing is deliberate: on the Index the grip has to be **squeezed** (force
sensor, not just touched) for about a tenth of a second; on other controllers
it has to be pulled most of the way.

**With your hands** (anywhere the laser is off, i.e. over the table):

| Gesture | What it does |
|---|---|
| Press a table button | Same as clicking that battlescape button. Buttons travel about 5 mm, click near the bottom of the stroke (with a haptic tick) and spring back. A laser click on a button works too. |
| Point your index finger just above the map | The game's cursor follows your fingertip (with its hit chance when aiming) |
| Tap a soldier's head with your index finger | Select that soldier |
| Tap a tile on the map with your index finger | Move the selected soldier there (with path preview on, tap again to confirm) |
| Put your finger on the selected soldier's tile and drag it across the map | Turn the soldier to face that way (the game's right-click turn); yellow arrows show the direction while you drag |
| Pinch an item in the inventory tray: rest your index finger on the trigger and your thumb on A | The fingers close on the nearest item (it lights up as your hand approaches) and pick it up; lift either finger over a slot or hand to drop it there (costs TU like in the game). Dropping onto an occupied hand swaps the items. |
| Press a key on the action board | Choose that firing mode / action, or Cancel |
| Press a red alien button | Centre the view on that spotted alien |
| Turn your right wrist toward you, tap the minimap with your left index | Move the table to that part of the map |

If SteamVR offers skeletal finger data (`XR_EXT_hand_tracking`), the hands use
it; otherwise each hand is posed from the controller (trigger = index finger,
grip = other fingers, thumb on a button/stick = thumb down). If your real hand
goes far into the table, the virtual one turns see-through and stops colliding
until you pull back.

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
- Valve Index finger tracking, the grip force thresholds and haptics have been
  written against the spec but **not yet felt on a real headset**; the hand
  physics, buttons, inventory pinching and taps were tested with a simulated
  hand in the desktop preview.
- The inventory tray moves items between slots and hands; loading ammo by
  dragging a clip onto a weapon is not supported yet (use the screen). The
  ground area on the tray is five columns wide and shows the floor items that
  fit; the rest are on the big screen's inventory.
- Typing (base names, save names) needs the real keyboard.
- Items lying on the floor are not shown on the table map yet (they are in the
  tray's ground area and on the screen).
- The fog hides unexplored ground up to the level you are viewing; walls seen
  from outside stay visible, as on the flat map.
- Windows only for now. The code still compiles on Linux (that build drives
  the automated desktop-preview tests), but it is not packaged or supported.

## How it works (for the rebuild)

All VR code lives in `src/VR/`; the engine only gets small hooks.

| File | Role |
|---|---|
| `VrApi.h` | The only header the engine includes. No-ops when built without `OXCE_VR`. |
| `VrXr.*` | OpenXR: instance, session on the engine's GL context (Xlib or Win32 binding), stereo swapchains, action bindings for Index/Touch/Vive/simple controllers, haptics. |
| `VrSystem.cpp` | Frame loop, tracking-space → world "rig", the controller → mouse bridge (synthetic SDL events), grabbing, desktop preview camera, test automation (`OXCE_VR_SCRIPT`). |
| `VrRoom.*` | Procedural command-center room, war table, controller models. |
| `VrBoard.*` | Everything on the table: battlescape diorama, unit standees (a critically damped follower turns the game's ~33 steps a second into smooth motion at the headset's frame rate), tile picking, the HUD (wireframe cursor, selected-unit marker, path preview, readouts in the game's font), voxel fog of war, voxel effects (projectiles, explosions, smoke, fire), the whole-map minimap, globe. |
| `VrHands.*` | Hand skeletons (from XR hand tracking or posed from controller inputs), collision against box colliders with per-finger joint solving, pinch / point gestures. |
| `VrTable.*` | The table's control panel: hatches and spring buttons, spotted-alien buttons, the pop-up action board, the inventory tray with voxel item models, head/tile taps, the wrist minimap and the live wall screens. |
| `VrGL.*`, `VrShaders.h` | Tiny GL helper layer (incl. a shadow map target) and the single scene shader: linear lighting into an sRGB target, up to 12 point lights per frame, shadows from the key light, soft room-corner shading, the table's up-glow, the fog material. |

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
- **Physical buttons mirror the game's own buttons.** `BattlescapeState::vrButtons`
  reports each icon-panel button's rectangle; a fully pressed cap clicks that
  rectangle through the same synthetic mouse path, so the game decides what the
  button does. Button caps are colliders for the fingers, so pressing one is
  just the finger collision pushing it down against a spring.
- **Wall screens reuse game states off-screen.** `UnitInfoState` is rendered into
  a private surface with `State::vrBlitTo` (palette saved and restored, sounds
  muted), and `MiniMapView` is drawn directly.
- **The HUD reads the game's own state.** Path preview comes straight from the
  tiles (`Tile::getPreview`, `getTUMarker`, `getEnergyMarker`,
  `getMarkerColor`) and the `battleNewPreviewPath` option; the readout next to
  the cursor comes from `Map::getCursorInfo`, which is the flat map's own
  accuracy / damage code moved into a function both use. Readouts are drawn
  with the game's small font and palette and turned to face you.
- **Pop-up controls mirror the game's own widgets.** `ActionMenuState::vrItems`
  lists the visible action menu entries and `BattlescapeState::vrVisibleUnitButton`
  the red spotted-alien indicators. The table copies their pictures from the game
  frame and clicks their screen rectangles, so mods' custom actions just appear.
- **The pinch is a controller gesture, the animation follows it.** Finger on the
  trigger + thumb on A is the pinch (instant and reliable); the virtual index and
  thumb are bent toward each other with a small IK solver (FABRIK) and eased over
  a few frames, and the pinch point is where their pads meet.
- **The flat camera and the table stay in sync.** The table follows the game's
  camera center and view level, and sliding the map on the table moves the
  game's camera.

Engine hooks: `Game.cpp` (startup, event filter, per-loop `VR::frame`),
`Screen.cpp` (hand the composed frame to VR), `Options` (`vrMode`, `vrTableScale`,
`vrSeatOffset`, `vrShadows`),
`UnitWalkBState` (in VR every visible unit counts as on screen: the engine
otherwise skips most of the walk animation, and runs at timer interval 0, for
units outside the flat camera's view, which made them sprint across the table),
`BattlescapeState::vrTileClick` / `vrSelectUnit` / `vrButtons` / `vrIcons`,
`BattlescapeState::vrVisibleUnitButton`, `ActionMenuState::vrItems`,
`Map::vrHiddenMovement` (the hidden movement screen is up), edge and drag
scrolling switched off in VR (`Camera::mouseOver`, `BattlescapeState::mapPress`),
`Projectile::vrAction` / `vrAmmo` / `vrBulletSprite` (style the projectile),
`State::vrBlitTo` / `vrMute`, `FlcPlayer` and `VideoState` (keep VR frames
going during the intro and cutscenes), `Map::setSelectorTile` /
`getCursorSize` / `getCursorInfo` (the cursor readout code, moved out of
`drawTerrain` unchanged so the table can show it too),
`BattleUnit::get/setFacingSnapshot`, `Globe::getCenter`, `Game::getStates`, and
`SDL_GetMouseState`/`SDL_WarpMouse` calls routed through `VR::getMouseState` /
`VR::warpMouse` so the laser pointer is the mouse.

## Building

Windows: builds with MinGW (llvm-mingw was used) against static SDL 1.2
libraries (SDL, SDL_image, SDL_mixer, SDL_gfx), the static OpenXR loader and
glm; pass `-DDEPS_DIR=<nonexistent>` so CMake uses pkg-config.
`-DOXCE_VR=OFF` builds the normal game.
