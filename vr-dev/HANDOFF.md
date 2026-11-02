# VR prototype: handoff for a new session

This folder lets a fresh session (or person) pick up work on the VR tabletop
prototype without the original machine: how to rebuild the toolchain and
libraries, build the Windows release and the desktop preview, run the tests,
package and publish a version, and what was decided along the way.

`README-VR.md` (repository root) is the player-facing description and the
"How it works" overview of the code. This file is for working on it.

## Who does what

- The prototype (branch **`vr-prototype`**) is built by Claude. The project
  owner play-tests it on a Valve Index and decides what changes.
- The owner is rebuilding the VR mode himself on branch **`vr`** (the default
  branch). On that branch Claude is a consultant: design discussion, code
  review, explanations, debugging help and small throwaway probes, but the
  owner writes the code. Never commit to `vr`, and never merge or cherry-pick
  prototype code into it.
- When a request leaves a real design choice open (controls, what the table
  shows, game rules), the owner prefers to discuss options before anything is
  built. Clear requests are just built.

## Quick start (new session)

1. Clone the fork and switch to the prototype branch:
   `git clone https://github.com/MarkAnthonyM/OpenXcomVR.git oxce-vr && cd oxce-vr && git switch vr-prototype`
   Pushing needs the Claude GitHub App installed on the repo (the owner did
   this once). Branch pushes work from a cloud session; **tag pushes and
   GitHub API writes (tags, releases) are refused by the session's proxy**:
   the owner creates tags and releases in GitHub's web UI.
2. `vr-dev/setup.sh` (installs Ubuntu packages if missing, downloads
   llvm-mingw, fetches every dependency at its pinned commit, builds the
   Linux and Windows libraries into `~/vrdev`). Takes a while; re-runs skip
   finished steps. `vr-dev/setup.sh linux` or `windows` does one half.
   In the cloud workspace Ubuntu's apt is unreachable: setup.sh puts the
   OpenGL headers in place before building SDL (SDL built without them
   fails every test with "X11 driver not configured with OpenGL"; if that
   happens, delete the linux-sdl stamp in `$VRDEV/stamps` and re-run) and
   writes a stand-in `GL/glu.h` when the system has none.
3. Ask the owner to attach his X-COM: UFO Defense folder (or a zip of it). It
   can't be in the repo. Then `vr-dev/test/setup-data.sh <folder-or-zip>`.
   If he sends a RAR (it has happened even when named .zip): with "package
   managers" allowed in his Capabilities settings, npm works through the
   session proxy once its no-proxy list is overridden, e.g.
   `npm install --noproxy="localhost,127.0.0.1" --proxy $HTTPS_PROXY node-unrar-js@2.0.2`
   in a scratch folder, then extract with a few lines of Node and point
   setup-data.sh at the extracted XCOM folder. Building third-party code
   from GitHub source (e.g. libarchive) was refused by the session's
   security check; use the npm package.
4. `vr-dev/build-preview.sh` and `python3 vr-dev/test/regress.py`: every line
   should say `ok`.
5. `vr-dev/build-windows.sh` builds `openxcom.exe`.

All paths come from `vr-dev/env.sh` (`VRDEV` defaults to `~/vrdev`).

## Building

| What | Command | Output |
|---|---|---|
| Desktop preview (Linux, the tests' program) | `vr-dev/build-preview.sh` | `$VRDEV/build/preview/bin/openxcom` |
| Windows release build (static, 64-bit) | `vr-dev/build-windows.sh` | `$VRDEV/build/windows/bin/openxcom.exe` |
| Release zip | `vr-dev/package.sh` | `$VRDEV/dist/OXCE-VR-Prototype-<version>-Windows.zip` |

- Windows only is shipped (the owner dropped Linux packages). The Linux build
  exists to run the desktop preview tests.
- The Windows build is cross-compiled with llvm-mingw 20250924 (UCRT) against
  static SDL 1.2 + SDL_image/SDL_mixer/SDL_gfx, libpng, zlib, Ogg/Vorbis and
  the static OpenXR loader 1.1.63. The exe depends only on Windows DLLs.
- Library versions and licences are in `vr-dist/THIRD-PARTY-NOTICES.txt`;
  update it (and `vr-dist/licenses/`) if a library changes.

## Testing

There is no headset here. The desktop preview (`-vrMode 2`) renders the VR
room in a window from a fixed or scripted eye, under Xvfb with software
OpenGL (llvmpipe, ~10 fps, so give scripts generous waits). Tests are scripts
of commands the preview reads from `OXCE_VR_SCRIPT`:

```
vr-dev/test/run.sh v3 -load outside.asav       # one script with a save
python3 vr-dev/test/regress.py                  # the regression set with checks
python3 vr-dev/test/regress.py v3 i3            # some of it
```

- The game's log (with all `[VR]` lines) is `$VRDEV/run/user/openxcom.log`;
  screenshots land in `$VRDEV/run/shots/`. Look at them: several things
  (night darkness, glow, layout) can only be judged by eye.
- Saves in `vr-dev/test/saves` (copied to `$VRDEV/run/user/xcom1`):
  `battle_test` (New Battle start, soldiers in the Skyranger, mixed weapons;
  unit 1 rifle), `outside` (six soldiers walked out of the Skyranger: unit 1
  rifle, 2 rocket launcher, 3 heavy cannon, 4 laser rifle, 5 heavy laser,
  7 heavy plasma), `night` (battle start at night), `outnight` (`outside`
  with global shade 15), `newbattle`, `geo_test` (geoscape), `tap_test`
  (the tap test's roofs and soldiers), `bigunit` (`outside` with its sectoid
  made a moving reaper next to unit 7: the scanner's big-unit test), `farm_wheat`
  (the owner's farm: unit 2 in a wheat field).
- The owner can send save files from his game for specific situations; they
  need the same mods (plain xcom1) and an OXCE 8.7.1-compatible version.

Script commands (one per line; numbers are optional with defaults):

| Command | Does |
|---|---|
| `wait ms`, `waitframes n`, `waitidle maxms` | wait (idle = nothing walking, flying or being fired) |
| `cam x y z yaw pitch` | place the eye (room metres; seated view ≈ `cam 0 1.30 0.10 0 -42`) |
| `camunit back up [side]` | look at the selected soldier from behind and above |
| `shot file`, `gameshot file` | screenshot of the VR view / of the flat game screen |
| `click x y`, `rclick x y`, `move x y`, `key sym` | mouse and keys on the flat game (320x200 coordinates) |
| `selectunit id` | select a unit as a head tap on the table would |
| `clicktile dx dy dz` | tap a tile relative to the selected unit (twice to walk: preview, confirm) |
| `clickabs x y z [right]` | tap an absolute tile |
| `fireat x y z [aimed\|snap]` | the selected unit fires its right-hand weapon at a tile |
| `dragturn dx dy` | finger down on the selected soldier, drag, lift (turns it) |
| `pokebutton name`, `pokemenu i`, `poketile x y z`, `pokeunit`, `pokenear` | a simulated fingertip presses a table button / action key / tile |
| `pinchitem i`, `pinchto SLOT cx cy` | pinch tray item `i` and drop it on a slot (`STR_RIGHT_HAND`, `STR_GROUND`, `UNLOAD`, ...) |
| `items` | log the tray's items (index, slot, ammo) and the soldier's TU |
| `unloadright`, `refilltu`, `spotaliens n` | test helpers: empty the right-hand weapon, refill TU, make aliens spotted |
| `groundkey dir`, `padswipe dy`, `wheel dir` | tray ground pages, left-trackpad level swipe, level change |
| `pokekey label`, `kbtype words...` | the fingertip presses a table keyboard key (`A`, `DEL`, `ENTER`, `SHIFT`...); type text through the keys (`\n` = Enter) |
| `pokefloat gx gy`, `waitfloat maxms [open]` | the fingertip touches the floating pop-up over game pixel gx, gy; wait until a pop-up starts to float (or is fully open) |
| `pokebase cx cy`, `holdbase cx cy` | a quick fingertip tap / a 2 s press on grid cell cx, cy of the base on the table |
| `dragfloat gx gy dy` | a fingertip presses the floating window at game pixel gx, gy, slides dy pixels and lifts |
| `pokedome dx dz` | the fingertip presses the globe dome where it faces (dx, up, dz) from its centre |
| `testufo n`, `selectdest`, `infobox text`, `testwork` | test helpers: n UFOs next to the first base with its armed craft sent after them (dogfights); the destination picker for the first craft; a battle message box; research and production in the first base |
| `boardpan dx dz`, `boardrotate r`, `boardzoom f`, `minimaptap u v`, `gamecam x y z` | move the table view / the game camera |
| `hand ...`, `handmove ...`, `handcurl ...`, `handoff` | a scripted right hand (grip pose and finger curls) |
| `testburst dx dy big` | an explosion effect next to the selected soldier |
| `skyshift days` | move the sky clock like the panel's SKY CLOCK keys (no argument: back to the game's clock); re-rolls the sky at once |
| `skyclock day hour minute` | the date and time the sun and moon are worked out for (`skyclock -1` = the game's clock); takes effect at the next re-roll (`weather`, `shade`) |
| `dbgpin on [dist]`, `dbgpage n`, `dbgpress ID` | the wrist debug panel (needs `-vrDebugPanel true`): hold it in front of the eye (the preview has no left hand to turn), show page 0 info / 1 controls, press a control (`RAIN`, `DARKER`, `TURNR`, `REAL`, `STRIKE`...) |
| `weather kind`, `strike`, `shade n`, `sunaz deg` | the battle's weather (`rain`, `snow`, `dust`, `sand`, `mist`, `clouds`, `clear`, `auto`), a lightning strike now, the mission's darkness (global shade, relit and rebuilt), the sun's direction for the following re-rolls (`-1` normal); `weather`/`shade` re-roll the sky, so set `sunaz` first |
| `boardinfo`, `status` | log the table view (focus, where the soldier sits, level) / the table state |
| `reveal`, `findset NAME`, `centertile x y` | the whole map explored (looking at terrain without fog); log where a terrain set's tiles are and centre the table on them (`findset UFO1`, `findset PLANE`); centre the table on a tile |
| `nofloor`, `floorids [part]` | log ground tiles without a floor part; which parts (0 floor .. 3 object) the ground level uses, how often, one place each |
| `log text`, `quit` | |

Terrain lab: `run.sh <script> -vrTerrainLab POLAR` (switch last). Its keys by `click` in game-frame
pixels, columns x 526/602/678/754: row y 358 terrain-, terrain+, page-, page+; 378 relief, light, sun,
sky; 398 strength, lamps, pictures, clear marks. `pointer x y` + `boardclick [right]` = trigger / A on
the table; `move` before `click` in the mark window. Marks go to `$VR_RUN/user/terrain-lab/`.

Practical notes: the first 9 s of every script are loading (`wait 9000`).
The New Battle flow is driven by blind clicks on the flat screen (see
`scripts/nb3.txt`). The preview has no audio device; that error is harmless
(to exercise sound, run with `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=<file>`:
raw s16 stereo at 22050 Hz, not in real time).

On this slow machine (2 CPUs): run.sh stops a run after `VR_RUN_TIMEOUT`
seconds (default 400; the tap test needs ~250). Known flaky: `bh` (even
with its longer wait), and the first test after the machine or Xvfb
restarts (it can segfault; a rerun passes). If every test suddenly fails
with things missing, Xvfb has died: run.sh restarts it on the next run.

## Versions and releases

- Version: `0.<round>.<fix>` in `src/VR/VrVersion.h`; a round of requested
  changes bumps the middle number, a small follow-up fix the last one. Also
  put it in `README-VR.md`'s title. It shows on the main menu and on the
  controls plate at the table's edge.
- Every build delivered to the owner: bump the version, update
  `README-VR.md`, commit (message starts `VR prototype <version>:`), push
  `vr-prototype`, build Windows, `vr-dev/package.sh`, send the zip.
- Only versions the owner **publishes** get a git tag (`proto-<version>`). He
  creates it in GitHub's release form ("Choose a tag", target
  `vr-prototype`), marked as a pre-release. Release text: copy
  `vr-dist/RELEASE-NOTES-<version>.md` from the last one and update it. It
  must say the prototype is AI-built (with Claude) and that the owner's own
  rebuild is under way on `vr`, and list Quest 3 / Steam Frame as supported
  but untested until someone has tried them. The zip includes `LICENSE.txt`,
  `THIRD-PARTY-NOTICES.txt`, `licenses/` and `BUILD-INFO.txt` (package.sh
  does this).
- Merged branches so far (details per build in `vr-dev/HISTORY.md`): weather 0.13.0-weather.1-13 ->
  0.14.0 (sky debug panel `vrDebugPanel`, moon, vanilla mode `vrVanillaSky`); thunder -> 0.15.0;
  scanner -> 0.16.0 (0.20.1 fix: 2x2 units count once, test `bigscan`); select (taps) -> 0.17.0;
  shade (indoors rule) -> 0.18.0; fog -> 0.19.0; hidden (alien turn) -> 0.20.0; rain (weather sound)
  -> 0.21.0. Test `v2` failed on 0.16.1 (0.062 m off centre, check allows 0.06); passes on terrain.1.
- Branch `vr-prototype-terrain` (from vr-prototype 0.21.0): terrain looks (owner's list: flat rocks and
  holes, rough hidden sides, flicker, the Skyranger's white lines and nose, the small scout, missing polar
  tiles, animations, items and bodies, tree crowns). Builds `0.21.0-terrain.<n>`.
  Owner's choices: one build (the lab is in openxcom.exe, command line only, view-only); relief from a
  hand-checked list of parts; holes a quarter level deep to start; both voxel steps and smooth relief to
  compare; hidden sides mirror the visible side, nearest colour where that fails; the small scout gets a
  hand-built model first (the other ships decided after it); bodies as short voxel lumps, small items as
  flat pictures with a faint outline.
  Findings: floors are all the same 2-voxel slab, rocks/craters only painted inside the diamond; most
  objects and walls repeat the floor's bottom slab (the flicker: two coplanar faces); POLAR 60-62 are tiny
  ice bits on a 4x4-voxel block (the grey "missing" squares); the small scout's collision is near a cube.
  - terrain.1: the terrain lab (VrLab, `-vrTerrainLab [TERRAIN]`: the table binds the lab's own
    SavedBattleGame, no BattlescapeState; revalidate and raycast know about it). Board::buildTile: one
    shape per combination of a tile's parts (TileKey); the floor owns z 0-1; shown faces from the owner's
    sprite, hidden ones from the solid's far side; `stray` parts (under 40% of shown faces on their art)
    lose the faces that miss. growToArt: objects covering under 70% of their art grow crowns round about
    their axis. VrRelief: 64 parts in `bin/common/Resources/VR/terrain-relief.txt`, steps (default) or
    smooth, `-vrRelief`. VrRoom: a 10 cm basin under the glass; the shader leaves the glass out under
    the map (uMapCut / uMapCutInv). Dropped: a "boxed" rule rebuilding objects whose box misses their
    art (sand-hill ramps became bowls). See README-VR "How it works".
  - Owner after terrain.1: steps (missions use them; smooth/flat only to compare). He asked for a lab
    to check against the flat art and to report problems: terrain.2 adds the inspect panel, marks with
    A (window: reasons + note on the table keyboard; non-modal TextEdit focus, a modal one swallows the
    buttons), per-run notes + screenshots (doCaptures: the scene again from the head, 1600x1000, 0.6 s
    after the window closes), marks.txt, LAMPS (TerrainLabState::updateLamps: LL_UNITS power 15),
    STRENGTH (Weather::force "kind:N" for dust/sand/mist/clouds too: density), ASSEMBLED (mutual 3-in-4
    neighbour rule over the map blocks), PICTURES (cards; Board::flatPicture). Owner's choices: all
    three picture kinds, A to mark, screenshots, a folder per run, marks persist until removed.
  - Owner's first lab notes (CULTA: wheat too tall and cone-topped, two fruit trees "crown wrong") and
    his save `vr-dev/test/saves/farm_wheat.asav` (unit 2 standing in wheat at 31,28; wheat should come
    to a soldier's waist). terrain.3: growToArt fits slices (a disc clipped to the tile, radius per
    height, largest that stays inside the art), drops top slices that add nothing (flat tops, no
    cones), ignores a ground patch's colour inside the diamond (trunks), replaces the collision when
    the overlap is 60%+ (natural sets only; U_/UFO/PLANE/XBASE/UBASE/BRAIN and craft sets keep the old
    additive rule). "tall" entries in terrain-relief.txt set an object's height (CULTIVAT 4 tall 11).
    `OXCE_VR_SHAPE_LOG=1` logs each fit. Lab shots are clean (Board::setCapturePass: no HUD, lab
    overlays, hands or lasers; corner ticks round the marked tile) plus `NNN-close.png` (40 degrees,
    three tiles high, from the head's direction, at least 20 degrees down). PICTURES starts off (owner).
  Setup fixes from this session: setup.sh creates the Linux prefix's include/lib first and stops if the
  Linux OpenXR loader doesn't build (it needs `-I$VR_LP/include` for EGL in the cloud workspace; before,
  it was marked done anyway and CMake quietly built the preview without VR); build-preview.sh refuses a
  build without VR.
- Released so far: 0.12.2 (the first published release; 0.9.1 was prepared but never published). The
  tags proto-0.1.0 ... proto-0.8.0 on the fork are from before the tag-only-releases rule.

## Decisions (keep these unless the owner changes them)

The table and its view
- The table keeps its own view; the flat screen is just a screen. Pointing
  the laser at it never changes the controls; edge and drag scrolling are off
  in VR.
- Tapping a soldier on the table never moves the view (it's in reach). The
  next-soldier button, the first soldier of a mission or turn, and the end of
  every walk bring the soldier to `NEAR_SPOT` (0.45 of half the map depth
  toward the player, `VrBoard.cpp`).
- A walking soldier is kept inside a band around that spot. The game's
  automatic re-centre after a move is ignored.
- Shots: the table follows the game camera chasing the projectile, looks at
  the impact for 0.8 s, then goes back to where it was (unless the player
  moved the map).
- Alien turn (0.19.0-hidden, owner): the table is dark except a window on
  what the flat screen shows; it takes the flat view's turn and zoom and
  jumps with the flat camera (no glide); Hidden Movement or a full-screen
  state over the map (the next turn's screen) = all dark. Nothing is frozen:
  the table updates underneath. Sliding and turning are locked (zooming
  works, hidden.2); the
  player's view is restored for their turn. Battle lights still light the
  room (owner: an unseen war around you). Battle monitors and the wrist
  minimap are jammed for the whole alien turn.
- Night: tiles and figures are darkened by the game's own shade with factors
  measured from the battlescape palette, in linear light
  (`shadeFactor()` in `VrBoard.cpp`); the fog darkens too.
- Glowing shots (lasers, plasma, rocket exhaust, incendiary, blaster bombs)
  and explosions light the terrain and figures on the table only (board
  lights in the shader), not the game's lighting or vision. Bullets don't.
- Projectile styles: tracer, red laser beam with fading static, green plasma,
  rockets with smoke, thrown items; intensity scales with the weapon's power
  within its damage class.

Controls (by controller, `HandState::kind`)
- Index: left stick slides the map relative to where you look, right stick
  X turns it, right trackpad swipe zooms, left trackpad swipe steps the level
  (a third of the pad per level, haptic tick), A right click with the laser
  out, B back, left B tap recenter / hold seat height.
- Quest 3 (Touch): B/A hold zoom in/out; tap Y/X level up/down; hold Y seat
  height; hold X recenter; right stick click right click; left Menu back.
- Steam Frame (own profile when available): Y/A zoom; D-pad up/down level;
  D-pad left tap recenter, hold seat height; X right click; B back.
- Right stick up/down scrolls lists at the screen on every controller, but
  never over the battlescape map (that would change the level).
- Pinch = finger on the trigger + thumb on A (X on the left Touch, D-pad on
  the left Frame); drag away from a soldier to turn it.

Table console and tray
- Table buttons and action keys are silent (the game's battle buttons are);
  only haptics.
- The tray follows the inventory screen's rules: TU costs, a clip dropped on
  an empty weapon loads it, a weapon that still holds ammo can't be loaded,
  the unload pad works like the Unload button (and unprimes grenades), the
  ground area shows five columns per page with page keys.
- The tray's controls strip is on the map side; nearer the player the raised
  rim would hide them from a seated view.

Geoscape, pop-ups, keyboard (0.10.0)
- The globe is a dome sunk into the table (`DOME_TOP` 22 cm above the glass,
  tilted 35° toward the player); zooming sinks it, so it never hides the wall
  screen. Geoscape side buttons are console keys in the bay: time speeds on
  the far row (the raised rim hides part of the near row from a seated eye),
  the six menu buttons on the near row.
- Pop-ups (any non-screen state except the action menu and the Loading /
  Saving bars, plus Briefing, Debriefing, NextTurn, Promotions,
  Commendation(Late), MonthlyReport and MiniMap screens) float over the near
  half of the map, turned to face the eyes when they open (5–50° lean), up
  to 66 × 42 cm (2.6 mm per game pixel at most), the game's pixels as they
  are (with the screen's scanlines). Dogfights
  float side by side. BuildNewBase / SelectDestination float only their bar,
  far beyond the globe; the dome takes fingertip taps (always on the
  geoscape). Info boxes stay at least 2.5 s. A hand whose fingertip is over
  the table or near a window has no laser on that window (0.10.1). Effect "D": a line grows, unfolds with scanlines and
  flicker; it folds back on close (no sparks, 0.12.1). It waits
  for the game's Window grow animation. The backing is drawn over whatever
  stands on the table (depth test always) and before the hands.
- The keyboard is on the table (not floating), at the near edge above the
  rim, tilted 14°: digits + DEL, QWERTY, ASDF + ENTER, SHIFT ZXCVBNM - , .,
  SPACE; 3.6 cm keys. Shift is one-shot and starts on for an empty field. A
  pop-up lifts a little higher while the keyboard is up.

Pop-up rules and craft models (0.12.2)
- Every full screen opened over the geoscape floats, except the base screen
  (the base on the table), anything while a battle exists (the base defense
  preview included) and cutscenes (Video, Slideshow, Cutscene states).
- Pop-up pictures: game pixels repeated 4 x 4, mipmapped, anisotropic, mode 1
  samples with textureGrad; `uScan` is the scanline period in texels.
- Craft models leave out the shadow drawn into the base sprites (dark greys
  flood-filled from the sprite's lower/right edges); the model casts its own.
- Base labels face the head (Board::viewer), never each eye: per-eye
  billboards don't fuse.
- Known preview flake: once in a while the preview crashes at startup in
  `CrossPlatform::setWindowIcon` (SDL/X11 under Xvfb), before any VR code;
  rerun the test.

Wall dashboards (0.11.0)
- The voxel-assembly pop-up effect ("A") was tried in 0.11 and removed in
  0.12.1 at the owner's choice; so were the Scale2x upscale of pop-ups and
  the voxel sparks. A full screen whose Window is growing is briefly marked
  a pop-up by the game (`Window::vrScreenState` tells): it doesn't float.
- Back wall: two monitors per side. Geoscape: research (left top),
  production (left bottom), craft status (right top), UFO activity (right
  bottom), refreshed every second (`VrDash.cpp`). Battle: stats and roster on
  the left, the minimap as a square in the right top monitor, right bottom
  dark.

Base on the table (0.12.0)
- Shown while a BasescapeState or PlaceFacilityState is on the stack (the
  globe hides). 0.62 m square, near edge 3 cm beyond the console; slides with
  the left stick / grip. Rebuilt only when the facilities, construction or
  parked craft change (a signature string).
- Tap = the game's right click on that cell (through the base view, so the
  game picks the screen); hold 1 s = its left click (dismantle; not on the
  lift, where the game's left click starts a base defense preview); tap on an
  empty cell = BasescapeState::vrBuild (the Build Facilities list). While
  placing, the finger moves the game's mouse over the placement view (its
  selector also says whether the spot fits) and a touch clicks there. Only
  while the base screen itself is on top.
- Every screen opened from the base screen floats (POPUP); PlaceFacility
  floats only its window, beyond the base. Floating windows: touch = mouse
  down, lift = mouse up (click), drag > 14 px = cancel the press off the
  window and send wheel steps; the right stick scrolls at the last touched
  point.
- Base screen keys use the screen-key mechanism of the geoscape keys
  (`Btn::geo` slot, `group` 1 = BasescapeState::vrButtons, 2 = base selector).

Room and rendering
- Walls close; live screens (flat game, stats, roster, minimap) on the back
  wall; wrist minimap shown when the right wrist turns toward the face.
- Up to 12 room lights (key light with shadows, fills, screen spill, red
  alert beacon, battle flashes), voxel fog of war, voxel effects.
- Outdoor battles (0.13.0): the sun's direction is the real sun at the site
  (rolled when it is overhead, on Mars or with no site); its height and colour
  come only from the game's global shade (0-3 high white, 4-8 gold, 9-11
  orange, 12-15 blue moonlight from the other side) so it never contradicts
  the darkness. The map's tiles stay exactly as dark as the game's shade. The
  sun lights only the map area (not the holo glass) and replaces the key
  light there; the shadow map follows it.
- Weather is cosmetic and rolled per mission from a hash of the mission
  (type, site, time, shade, map size): snow polar/mountain 50%, rain
  jungle/forest/farm/city 35% (always with lightning), dust desert 40%, red
  sand Mars 60%, clouds on 40% (70% before weather.8) of missions with shade <= 7, mist on 30% (was 50%) of
  dawn missions (shade 3-10, sun in the east). Indoors (base defence, UBASE /
  XBASE datasets): no sun, no weather. Rain and snow stop at the highest
  floor or object in a column (the fog top over unexplored ground), never
  fall under a roof that the view level hides, and thin out near hands and
  the cursor. Lightning lights only what is drawn on the table.
  `vrWeather` off removes the weather, not the sun.
- Sky and weather details (moon, sunset ramp, cloud shadows, soldiers' light pools as sun/moon, rain
  and snow strength, gusts, lightning, the indoors / sun-reach rule): `vr-dev/HISTORY.md`.
- The wrist debug panel's darkness buttons are a table-only offset
  (`Board::setShadeOffset`: every tile's shade + n for the table's colours,
  and the sky worked out again); the game's global shade is never touched.
  The weather roll keeps using the game's darkness for its seed.
- Scanner (`VrScanner.cpp`): while ScannerState is on top, a handheld scanner (DETBORD art made solid)
  in the right hand, tipped 35 degrees; a green camera feed with a ping wave and contact rings, compass
  bar, side radar; the map is locked while it is out; only its own button closes it (sends a right
  click). Sounds made in code. Details, timings and test ops (`usescanner`, `scannerpress`,
  `scannerdump`, test `scan`): `vr-dev/HISTORY.md`.

## Known gaps

- Quest 3 and Steam Frame layouts have never been run on those headsets.
- Items lying on the floor aren't shown on the table map; ground items taller
  than three cells are only in the screen's inventory. (Planned on the terrain branch.)
- Terrain parts' animations (8 frames) aren't shown: the table builds frame 0. (Planned on the terrain branch.)
- Craft and UFO hulls are their collision voxels; the small scout is close to a box. A hand-built scout
  model is planned on the terrain branch.
- Weather and the sun have only been judged in the desktop preview (no
  headset here); thunder has never been heard (the preview has no audio).
- Changing video options in-game stops VR (restart).

## Parked ideas (owner: maybe later)

- Sky: make the table's night match the game's night rules. The game counts
  a tile as dark above darkness 9 (maxDarknessToSeeUnits): X-COM sees 9 tiles
  there instead of 20. The table's sunset now runs to darkness 11 (red-orange,
  sun 15 degrees up at 11), so darkness 10-11 still look like late dusk while
  sight is already at night range. Idea: squeeze the sunset ramp (SUN_COL /
  DAWN_COL / SUN_ELEV) into 0-9, blending between the steps, so the red-orange
  low sun lands on 9; at 10-11 the sun is down (as on the globe there) and an
  afterglow fades into night. Tried once in the preview (not kept, 0.16.1):
  0-9 looked right, a little brighter ground at 9 but the colour held. The
  afterglow failed: a red-orange tint on the sky fill barely showed, and with
  the moon as key light (here a full moon mirrored high) 10-11 read as a cool
  moonlit night, brighter than now. To make it work: the afterglow's light
  should come low from the sunset side (a direction, not the fill), with the
  moon kept weak at 10 and taking over at 11. The owner likes the current
  dusk, the red-orange sunset most; keep that look whatever changes.

## Where things are

`vr-dev/HISTORY.md` keeps the per-build notes of merged branches. `README-VR.md` has the file-by-file overview ("How it works") and the engine
hooks. In short: `src/VR/VrSystem.cpp` (frame loop, input mapping, lights,
test script runner), `VrBoard.cpp` (everything on the table: diorama, view
rules, HUD, fog, effects, minimap), `VrTable.cpp` (console buttons, action
keys, alert buttons, inventory tray, wrist pad, wall screens), `VrHands.cpp`
(hands and pinch), `VrXr.cpp` (OpenXR, controller profiles), `VrRoom.cpp`
(room layout), `VrWeather.cpp` (sun, weather, lightning over outdoor
battles), `VrLab.cpp` (the terrain lab), `VrRelief.cpp` (relief on floors), `VrShaders.h`,
`VrVersion.h`.
