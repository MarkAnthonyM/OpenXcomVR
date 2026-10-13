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
3. Ask the owner to attach his X-COM: UFO Defense folder (or a zip of it). It
   can't be in the repo. Then `vr-dev/test/setup-data.sh <folder-or-zip>`.
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
  with global shade 15), `newbattle`, `geo_test` (geoscape).
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
| `boardpan dx dz`, `boardrotate r`, `boardzoom f`, `minimaptap u v`, `gamecam x y z` | move the table view / the game camera |
| `hand ...`, `handmove ...`, `handcurl ...`, `handoff` | a scripted right hand (grip pose and finger curls) |
| `testburst dx dy big` | an explosion effect next to the selected soldier |
| `boardinfo`, `status` | log the table view (focus, where the soldier sits, level) / the table state |
| `log text`, `quit` | |

Practical notes: the first 9 s of every script are loading (`wait 9000`).
The New Battle flow is driven by blind clicks on the flat screen (see
`scripts/nb3.txt`). The preview has no audio device; that error is harmless.

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
- Released so far: 0.9.1.

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
- Alien turn: the table follows the game while aliens are visible; sliding is
  locked (turning and zooming allowed). Hidden movement: no curtain (removed
  in 0.9.1); the table keeps its view and freezes what it shows (terrain,
  fog of war, tile smoke/fire, no shots or blasts) until the map is shown
  again, so nothing gives an alien away.
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

Room and rendering
- Walls close; live screens (flat game, stats, roster, minimap) on the back
  wall; wrist minimap shown when the right wrist turns toward the face.
- Up to 12 room lights (key light with shadows, fills, screen spill, red
  alert beacon, battle flashes), voxel fog of war, voxel effects.

## Known gaps

- Quest 3 and Steam Frame layouts have never been run on those headsets.
- Items lying on the floor aren't shown on the table map; ground items taller
  than three cells are only in the screen's inventory.
- Typing (save and base names) needs the keyboard.
- Changing video options in-game stops VR (restart).

## Where things are

`README-VR.md` has the file-by-file overview ("How it works") and the engine
hooks. In short: `src/VR/VrSystem.cpp` (frame loop, input mapping, lights,
test script runner), `VrBoard.cpp` (everything on the table: diorama, view
rules, HUD, fog, effects, minimap), `VrTable.cpp` (console buttons, action
keys, alert buttons, inventory tray, wrist pad, wall screens), `VrHands.cpp`
(hands and pinch), `VrXr.cpp` (OpenXR, controller profiles), `VrRoom.cpp`
(room layout), `VrShaders.h`, `VrVersion.h`.
