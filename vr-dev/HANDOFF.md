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
| `dbgpin on [dist]`, `dbgpage n`, `dbgpress ID` | the wrist debug panel (needs `-vrDebugPanel true`): hold it in front of the eye (the preview has no left hand to turn), show page 0 info / 1 controls, press a control (`RAIN`, `DARKER`, `TURNR`, `REAL`, `STRIKE`, `SUNMARK`, `SHOT`...) |
| `weather kind`, `strike`, `shade n`, `sunaz deg` | the battle's weather (`rain`, `snow`, `dust`, `sand`, `mist`, `clouds`, `clear`, `auto`), a lightning strike now, the mission's darkness (global shade, relit and rebuilt), the sun's direction for the following re-rolls (`-1` normal); `weather`/`shade` re-roll the sky, so set `sunaz` first |
| `boardinfo`, `status` | log the table view (focus, where the soldier sits, level) / the table state |
| `reveal`, `findset NAME`, `centertile x y` | the whole map explored (looking at terrain without fog); log where a terrain set's tiles are and centre the table on them (`findset UFO1`, `findset PLANE`); centre the table on a tile |
| `nofloor`, `floorids [part]` | log ground tiles without a floor part; which parts (0 floor .. 3 object) the ground level uses, how often, one place each |
| `closeup x y z name` | the terrain lab's two clean shots of a tile (`shots/<name>_view.png`, `_close.png`) |
| `dumpart SET` | any terrain set's sprites (the lab's map or not: `UFO1`) as palette indices (`shots/art/<SET>_<id>.png`), the parts list and the palette: input for `vr-dev/models/` |
| `comparepart SET id` | the lab's comparison picture of one part of any terrain set on its own (`shots/part_<SET>_<id>.png`) |
| `compare x y z name`, `inspect x y z name`, `sheets [all\|TERRAIN]` | the lab's comparison picture of a tile (`shots/<name>_compare.png`); its inspect panel (`_art.png`); contact sheets (current terrain without an argument) |
| `log text`, `quit` | |

Terrain lab: `run.sh <script> -vrTerrainLab POLAR` (switch last). Its keys by `click` in game-frame
pixels, columns x 526/602/678/754: row y 358 terrain-, terrain+, page-, page+; 378 relief, light, sun,
sky; 398 strength, lamps, pictures, clear marks; 486 (x 678) sheets. `pointer x y` + `boardclick [right]` = trigger / A on
the table; `move` before `click` in the mark window. Marks go to `$VR_RUN/user/terrain-lab/`.

Practical notes: the first 9 s of every script are loading (`wait 9000`).
The New Battle flow is driven by blind clicks on the flat screen (see
`scripts/nb3.txt`). The preview has no audio device; that error is harmless
(to exercise sound, run with `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=<file>`:
raw s16 stereo at 22050 Hz, not in real time).

On this slow machine (2 CPUs): run.sh stops a run after `VR_RUN_TIMEOUT`
seconds (default 400; the tap test needs ~250). Known flaky: `bh` (even
with its longer wait), `h2` once in terrain.13, and the first test after the machine or Xvfb
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
  -> 0.21.0. Test `v2` failed on 0.16.1 (0.062 m off centre, check allows 0.06); passed on terrain.1; fails again
  the same way on terrain.5 and .6 (checked: identical readout on both); timing, fixed in terrain.13 (40 more frames).
- Branch `vr-prototype-terrain` (from vr-prototype 0.21.0): terrain looks (owner's list: flat rocks and
  holes, rough hidden sides, flicker, the Skyranger's white lines and nose, the small scout, missing polar
  tiles, animations, items and bodies, tree crowns). Builds `0.21.0-terrain.<n>` (now terrain.26).
  Owner's choices: one build (the lab is in openxcom.exe, command line only, view-only); relief from a
  hand-checked list of parts; holes a quarter level deep to start; both voxel steps and smooth relief to
  compare; hidden sides mirror the visible side, nearest colour where that fails; the small scout gets a
  hand-built model first (the other ships decided after it); bodies as short voxel lumps, small items as
  flat pictures with a faint outline.
  Findings: floors are all the same 2-voxel slab (drawn as a surface at height 0 since terrain.9), rocks/craters only painted inside the diamond; most
  objects and walls repeat the floor's bottom slab (the flicker: two coplanar faces); POLAR 60-62 are tiny
  ice bits on a 4x4-voxel block (the grey "missing" squares); the small scout's collision is near a cube.
  - terrain.1 to terrain.18 (the terrain lab, relief, the comparison and sheets, the farm trees, thin floors,
    model resolution; the wheat, the small scout's hand-built model and its legs, the Skyranger's first hand-built
    outside): moved to `vr-dev/HISTORY.md` ("Terrain branch: early builds"). The lessons from them are the
    Decisions below.
  - Texture study after terrain.18 (owner, headset: the scout's and Skyranger's hulls look "cloth-like";
    the old rule-built Skyranger, broken as it was, looked interesting because it kept the pixel-art texture).
    Findings, from offline renders (study scripts were session scratch, not in the repo):
    the cloth look is mostly the voxel staircase of curved surfaces lit face by face (each half-voxel step a lit
    tread and a darker riser: rows like knitting), not the speckle; even one flat colour shows it. Lighting by
    the shape's smooth direction removes it: owner rejected that look. Faceted shapes step evenly only at clean
    angles (panels square to the grid or at 45 degrees, slopes 1:1, 1:2, 2:1); a 16-sided scout still stepped
    irregularly, an octagon was crisp (but eight-sided). Owner: the full art shading looks best so far
    (faceted maybe better: he will judge in the headset). He first thought the art's checkerboard dithering
    looked too big (an art pixel = one table voxel = 2 x 2 model voxels); he later corrected that: what looked bad
    in the whole-craft renders was the exact copy's streaks (below), not the dither's size. The art is
    drawn for one view: copied exactly, its pixel rows lie sheared across a flat face, so lines drawn straight
    on screen become sawtooth staircases seen from the side. Straightening (each face cell 1 table voxel, the
    art pixel at the cell's middle, the cell grid's phase chosen for the straightest rows) gives clean pixel art
    on the face; from the game's angle it is off by up to half a pixel. Owner's choices so far, on the fins:
    texture only (no shape details yet), straightened, at the art's pixel size (not Scale2x), the stripe kept as
    drawn (its red pixels are the art's anti-aliasing of a 2:1 line; redrawn versions were tried and dropped).
    The wing (a flat top face): there the art's pixel grid lies diagonally (each art pixel lands as a slanted
    diamond of one table voxel's area), so its checkerboard can't survive a copy: exact, it becomes diagonal
    streaks; straightened as drawn, uneven blotches (the noise seen on the flatter hull parts). The wing's
    dither is the art's light-to-dark shading (tip light, root dark; some of the root's dark is the fuselage's
    painted shadow), checkering two neighbouring greys. Tried: the checker redrawn on the face's own grid at the
    art's pixel size, at half size, and no dither (the shade between: about four flat bands). Owner chose the
    checker redrawn at the art's pixel size: where the art dithers between two greys, a table-voxel checkerboard
    of the same two greys, the lighter on cells with even (x + z). The panel line along the span came out dotted:
    the art itself draws it broken (its darkest grey every other pixel, a slightly lighter grey between), and
    each cell's middle lands alternately on the line and beside it, so the gaps came out lighter still. Owner
    chose it joined (one solid line of the darkest grey, X 16-47 at Z 124), and as a rule for the model: panel
    lines (seams) clean, solid and defined. Painted markings (the fin stripe) stay as drawn. Next: the curved hull
    (straightened panel by panel; fits a faceted shape).
    The hull (a strip of the fuselage's middle, round and faceted): straightening turns the art's shading into
    clean pixel art there too, but thin lines along the hull stay: each voxel step on a curve or slope has a lit
    top and a darker side, lit separately by the table (the cloth look's cause); faceting only makes them
    regular. Lighting each faceted panel evenly (every voxel of a panel by the panel's direction, not its
    face's) removes them: flat plates with crisp edges, unlike the smooth light he rejected. That needs the
    table's renderer (VrModels .ref panel data, addModel merging per panel with the panel's direction, the
    shader lighting by it); models without panel data unchanged. Owner: first a build faceted and straightened
    without that change (terrain.19), then one with it to compare.
  - terrain.19: `skyranger.py` FACETED (fuselage and nose as flat panels: CROSS (Y, half-width) 24/24, 28/28,
    36/28, 44/24, 50/18, 52/14; nose NOSE_W/T/B; the canopy a faceted octagonal hood 2.5 above the nose's top,
    sloping to it: a fixed-height hood sank into the faceted nose) and ART_TEXTURE (texture(): the new
    `straighten.py`: Canvas = the art of chosen parts put together in the game's draw order, checkers(), cell
    phase by straightness). Per piece: fuselage unrolled (per table-voxel slab along Z the skin's cells round
    the section in angle order s, each the art at its middle if the game's view shows that cell there, from a
    canvas of the fuselage's own parts; the hidden side mirrored; only the hull's ramp 240-255 counts, other
    colours (glass seen through, markings, consoles) take neighbours'; checker by slab + s); wings: cells over
    X, Z on the top's plane from the main wings' parts, join_lines() makes the broken dark lines (>= 248) solid;
    fins: per face as drawn (the approved study); pods: per table voxel, the art where seen (mirrored);
    anything left (undersides, ends) the nearest textured cell. Markings, glass, cone, gear, ramp by area. The
    wing tiles now also hold the hull (the faceted shoulders are wider at Y 48-50 and reach X 60-64: they were
    left out, and the wings stopped short of them). Sheets vs terrain.18: only PLANE; mean shape 84.9 -> 82.0
    (the faceted sides are boxier than the art's round ones: level-1 sides 99 -> 85, roof 85 -> 73; the right
    wing roots 100 -> 91, their tiles now holding the shoulders the art draws elsewhere), colour 61.9 -> 83.5.
    `outside` with `reveal`: 857k -> 852k terrain triangles. No engine code changed. Not yet seen in the
    headset.
  - Owner on terrain.19 (headset): the wings look really good (slight adjustments to come); the hull's
    texture cut the cloth look on the sides a lot, but looks off (to show with screenshots); a gap on the
    sides let him see into the craft. He asked for a screenshot on the debug panel, the panel only for a
    launch with his debug .bat (it persisted in options.cfg), and the sun marker switchable, off by default.
  - terrain.20: the gap was terrain.19's own: letting the wing tiles hold the hull put them in bands()'s
    `roof` set, so the level-1 side pieces under them (part 4 / 10, also used in rows 11-13 with no wing
    above) got two bands; the most common (to Y 48) won, and rows 11-16 lost the shoulders above Y 48 (a slit
    at X 60-64 / 96-100). Reverted (wing tiles hold the wing only; level 1 holds the shoulders); a check that
    assembles every part's model at every place against the whole craft finds only the open doorway at the
    rear (as terrain.18). Options: vrDebugPanel is set only from the command line (init ignores options.cfg)
    and save() always writes it off. Debug panel (H 420): SCREENSHOT beside the tabs (3 s countdown, a tick a
    second on the left controller; WristDebug::takeShot() polled by VrSystem doScreenshot(): captureTo() at
    2560 x 1600, 80 degrees from the head, S->capturing (no hands, lasers) and the panel hidden; saved as
    user/vr-screenshots/vr_<date>_<time>.png, the panel shows the file); SUN MARK on the controls page
    (Board::setSkyMarker, off at the start; test `mark` presses it first). Test `dbg` takes a screenshot.
  - Owner on terrain.20 (headset, with screenshots): the hull's texture cleaned right up with the gap closed.
    Wings: the dark gradient at the roots had dark blocks, not running smoothly into the lighter part; the
    long seam (along the span, "outlining the rudder") too bold against the other seams and too long (the art's
    doesn't reach the hull); the left wing had two bold seams the right doesn't; the red tips missed a bit at
    the ends; the roundel had stray whitish blocks round its edge.
  - terrain.21 (`skyranger.py` tex_wings, rewritten): both wings on one cell grid; find_lines() finds each
    wing's broken dark lines outside ROOT_SHADE (12 voxels from the hull: the art's painted fuselage shadow,
    250-252, made extra lines and ran the span line to the hull); a line is joined only if the other wing has
    one there too (mirrored, each pairs with at most one), over the stretch both have, in the lighter of their
    two greys (248 here); dark cells of lines only one wing has, and shadow cells near the root, take the
    wing's gradient (per column the mean of its other cells; at the root a line fitted to the ten columns
    next to it), the checker where it falls between two entries. Red tip: X < 1 or (X < 2.5 and Z >= the
    leading edge's start), filling the leading corner's steps. Fuselage: cells within 2 voxels of the roundel
    take their neighbours' texture (near_roundel(): the art's pale outline round the disc became whitish
    cells), and a cell 4+ entries lighter than every neighbour takes their middle (2 cells). The roundel's
    light stripes on its upper half are the sloped panel's steps lit (the panel-lighting build's job).
  - terrain.22 (owner: the build with each panel lit evenly, to compare with terrain.21 in the headset):
    `.ref` "VRF3" (VrModels): as VRF2, then uint32 P and P panel normals (3 floats, model axes x east, y south,
    z up), and a fifth byte per voxel (0 or 1 + panel); Model::panel / panelNormal. addModel: the mask is
    16-bit, kind | panel << 3, so the greedy mesher merges faces only within a panel; a face whose panel
    normal points along it (> 0.05) gets the panel's direction and MAT_PANEL (14), else its own (cut ends).
    Shader: the two-sided flip (normal turned to the eye) skips material 14 (a step's side seen from below
    could face away from its panel's direction). `skyranger.py` panels() / PANEL_LIGHT: per Z stretch (112-256
    and the nose's 256-272-284-292-298), per side, CROSS's five segments plus the flat bottom and top; each
    panel's direction the gradient of fus_faceted's boundary at its middle; every fuselage / cone voxel
    facing out takes the nearest panel plane of its stretch and side (within 2.5 of the segment's height);
    the canopy hood: its top the nose top's panel of that stretch, its eight sides one panel each (68
    normals in all, each piece's .ref holds a table of the ones it uses). write_piece writes VRF3 when a
    piece has panels. boardinfo now also logs "mesh hash <all> without PLANE <rest>" (positions, normals,
    materials, indices summed over tile shapes; deterministic): the new build with terrain.21's model files
    gave exactly terrain.21's hashes, and with the new files the hash without PLANE is unchanged
    (8692edd3816e1a6e in `outside` with `reveal`, top level). Triangles 851,580 -> 852,868.
  - Owner on terrain.22 (headset): the panel lighting gives the best result so far (kept). To fix on the Skyranger:
    (1) the wings too low (they should sit by the fins like the art, joining the wings' and fins' dark gradients;
    the front wings too), (2) a texture pass on the front, (3) the canopy's shape and texture like the art, (4) the
    nose cone's shape, (5) a feature under the cockpit with a red triangle (missing). Asked what (5) is: read from
    the art as a small device on the nose's right side (two small dark ports in a light frame, dark slats behind,
    a red triangle with a yellow middle pointing down ahead of it, an amber dash above): a gun or sensor fairing
    with a warning marker, or a hatch with steps; the Skyranger has no weapons in the game. Owner: keep the
    canopy's highlight; one build; for (5) my recommendation, modelled simply (a pad one table voxel out, the ports
    as recesses, the slats as ribs, the triangle and light painted, both sides).
  - terrain.23 (`skyranger.py`; no engine code changed, the mesh hash without PLANE unchanged, 8692edd3816e1a6e).
    Wings: a fit of their height and planform (both mirrored) against the art's wing pixels from the game's angle,
    the rest of the craft in front: terrain.22 89.6%; the best flat wing one higher 93.7%; a slope or a root fillet
    no better. What still missed was the hull covering the left wing's root: measured along the stretch between
    the wings, the art's far shoulder from the game's angle (min X - Y of the outline) is 15 against the faceted
    hull's 12 (the near side's lower edge within 1). CROSS's upper facets 2 lower and in: (34,28), (42,24), (50,16),
    (52,12) (keeps 16 at the wing's height: the wing tiles end at X 64). With that the wings fit best where they
    were (Y 48-49, 96.3%): it was the hull that made them look low; WING te 113, le0 127.5, tip at X 1 (red tip
    from there). Where the nose narrows (Z 256+) that left a one-model-voxel slit at the front wings' leading root
    corner: filled as hull (held by roof piece 52/53). Canopy: the art's glass and frame mapped onto the nose
    (centred Z 281, X 71-91, Z 271-291; the terrain.19 hood sat 8 too far back); HOOD (the octagon hood, refitted:
    centre Z 282, rz 10, rx 11, lift 5, glass/frame score 0.31 -> 0.60); labelled glass ahead of Z 279 (the
    windscreen = front faces, the side windows; a frame bar between the front diagonals and the sides: the art's one
    bar), frame at the foot and the glass's rear edge, the cowl behind grey. A solid roof amid glass (tried, roof
    L < 0.55 or 0.3) looked like an eyeball from above. Beak: BEAK, a regular octagon tapering elliptically, Y 27,
    r 6, Z 293-302 (yellow IoU 0.61 -> 0.75), bottom at Y 24 (nothing of the game's below level 1 there; the
    level-0 band would drop it). Panels: the hood's eight sides, its roof the nose's top panel; the beak's eight
    faces per Z stretch (293-296-299-302) and its bottom (93 normals). Texture: main computes panels first;
    tex_panels() straightens the art per panel (u across, v up the panel; cells of one table voxel; the art where the
    game's view shows the point, the hidden side mirrored; accept() colour classes; phase by straightness; gaps the
    nearest given cell): the nose's hull from Z 256 (the unrolled body texture assumes the constant section), with
    `even` (a cell 2+ entries off its 3 x 3 neighbours' middle takes it: the art's irregular dark noise made
    blotches), the glass (128-143 and the highlight 224-228), the cowl (hull greys; the art's white edge highlight
    made a white patch), the beak (16-31). FEATURE / side_feature(): a pad one table voxel out (Z 279-287, Y 31-35.5)
    with two ports (outer layer cut, inner dark 13/12) and slats (alternate rows cut, grooves 11/12), the triangle
    (37/36, middle 20) at Z 289.5 and the amber light (101/100) at Y 39.5-41: both sides. Sheets vs terrain.22: only
    PLANE, mean shape 80.5 -> 85.1% (wing parts 71-86 -> 90-100, level-1 sides 84 -> 97), colour 85.2 -> 85.1.
    Triangles 852,868 -> 858,940. Not yet seen in the headset.
  - Owner on terrain.23 (headset, two screenshots): the wings still don't sit flush against the fins like the art; a
    shadow that seems to belong to the wings shows on the hull above both wings (front and rear). Asked whether the
    faceting caused it: no (the round hull rose above the wing roots too). Found: (a) rear: the hull reached past the
    fin line at the wing's height, so a narrow 1:1 strip (49-50) and a 1:2 bevel rose between the wing root and the
    fin's foot, lit as plates (the strip dark); the hull's texture there was the same mid-grey as between the wings
    (measured per height), not a painted shadow. (b) front: the dark bars were two pieces drawing the same voxels:
    the front wings' parts are the main wings' (one model each, the main wings' shape), and with terrain.23's slimmer
    hull their inner edge sat in the hull's skin (558 voxels claimed by two placed pieces, all at Y 48-49, Z 225-261).
  - terrain.24 (`skyranger.py`; no engine code changed; mesh hash without PLANE unchanged). Tried and dropped: the wings
    one higher with a flat roof at 50 level with them (one plane; the wing 94.1% vs 96.3% against the art, and the roof
    pieces' sprites 85.6%: isolated, a roof piece's sprite does show the roof's height, a craft's outline doesn't).
    Kept: CROSS (24,24), (28,28), (33,28), (41,24), (47.9,17.1), (48,16), (49,16), (54,6), FUS yt 54, NOSE_T from
    (256,54): the hull stands at the fin line (16 out) through the wing's thickness (48-49), so wing, hull edge and
    the fins' feet meet in one line and no voxel is shared; below the wing a step of one (the shell's inner layer must
    stay outside X 64, where the cabin's floor tiles hold nothing; its innermost cells there left out, `wall`, the
    next ones the inside layer); the far shoulder on the art's line (X - Y = 15); the roof a 1:2 cap to a flat top 12
    wide at 54 (roof pieces 60/61 at rows 11-13, sprites alone: 92.2% vs terrain.23's 90.5%). Wings back to terrain.23's
    (Y 48-49, te 113, le0 127.5, tip X 1). The front wings' root-corner fill (the nose narrows from Z 256) now walks in
    until it meets the hull (up to 3 there). Checks: no voxel drawn by two pieces (was 558), no wing cell facing a gap
    of up to 4 before the hull, nothing missing but the rear roof row's end. Sheets vs terrain.23: only PLANE, mean
    shape 85.1 -> 86.8% (wing roots 93-97 -> 100, cockpit pieces 49/51/52 up), colour 85.1 -> 85.7. Triangles
    858,940 -> 868,212. Offline renders can't show two pieces drawing the same voxel (they draw the assembled grid):
    count it (every part's one model at every place) instead. Not yet seen in the headset.
  - Owner on terrain.24's renders: a dark edge along the hull's sides at the wings' height. It's the hull's vertical
    strip at the fin line (Y 48-49, panels 6 / 13) and the ledge under it (5 / 12): a sliver facing sideways,
    evenly lit, between panels tilted up, the whole length (the roof pieces repeat, so it can't exist only under the
    wings). Rendered with the strip and ledge lit as the 1:1 slope below (panels 4 / 11), the line is gone and the
    shape is unchanged. Owner: bundle that fix with the canopy changes (next: a canopy study). Before it, at his
    request, the fins' and wings' texturing findings written up as a recipe for future models: `vr-dev/TEXTURING.md`.
  - Canopy study (owner, after terrain.24; scratch log kept in the session only). Owner's choices: the battlescape
    art is the only reference (not the Ufopaedia picture); the canopy longer (glass from Z 270, was 278) as a low
    glass cabin (variant 3: sides rise from the hull, roof sloping down to a raked windscreen); a uniform frame 2
    cells (one table voxel) wide all round, bars split 1 + 1 over the windscreen's seams; the windscreen's glass
    carried down the nose to Y 35.5 (a lower window); the windscreen's top corners about 2 cells apart (the front
    frame looked triangular); the angled front corner panes are part of one long side window. Texture splotches
    (owner asked why): per-panel cell grids at odd angles, 25-50% of cells with no art (filled by nearest), the
    art's scattered dither, and faces that step two ways (a texture pixel seen as a hexagon from the diagonal).
    Owner chose: the nose's texture drawn by rule (TEX_AXIS 'rule', sigma 1.5) and the nose reshaped so every face
    steps one way only (crisp pixels over roundness). Options shown and not taken: a hybrid (one-way top and sides,
    tapering shoulders; the shoulders stayed big beside the canopy), the body's facets tapering into the nose in
    steps (jagged edges, not crisper), a flat deck on today's facets (sides still two-way), the deck as one triangle
    from the wing roots (chines; best outline 95.9% but the chines step two ways: owner, "crisp pixels are more
    important"), flat panel planes drawn by the renderer (engine change; parked). A first refit of option 1 that
    scored 96.2% was invalid (non-concave profiles: the extended cut planes sliced the body); profiles must be
    concave and start at the body's values.
  - terrain.25 (`skyranger.py`; no engine code changed; mesh hash without PLANE unchanged). The nose (NOSE_MODE 'cut',
    nose_planes): the body's section (CROSS) as a prism cut by planes each parallel to one voxel axis: top sloping
    along Z (NOSE_T 54, 46, 39.5, 34.5, 30 at Z 256/272/284/292/298), upright sides (NOSE_W 28, 20.5, 14.5, 9, 4),
    bottom (NOSE_B), front at Z 293.5, so only the beak is yellow; the body (Z <= 256) stays `_scaled` (terrain.24's
    wing fit), and a notch at the fin line (Y 47.9-49, x > 16) keeps the nose out of the front wings' voxels. The
    beak (BEAK deck=True): based on the nose's front face, its flat top meeting the deck's front edge, tapering
    (p 1.5) to a tip at Y 26, Z 302. The canopy (CANOPY_MODE 'cabin', CABIN axis=True): roof flush with the deck
    at the back (47 at Z 270) sloping to 46, windscreen foot Z 291.5 raked 4, sides one flat face each (parallel
    to Z, leaning 0.436 above Y 34, so the canopy narrows toward the back on the sloping deck), upright corner cuts
    3 / 2 cells at the top, kept inside the nose (in_nose) and above Y 33; frame and chin by cabin_post. Glass by
    rule (glass_rule2, per table-voxel block of 2x2x2): lighter in the middle, darker toward the frame (ring 0, 2,
    1, .5) and the hull's edge (foot 0, 5, 4.5 ... .5), half steps a checker, the art's highlight spots kept.
    Panels: each nose skin cell takes the cut plane it lies on. The dark edge fixed: the strip and ledge at the
    wings' height (panels 5/6, 12/13) lit as the slope below (4/11). Checks: no voxel drawn by two pieces (78 at
    the front wings before the notch), holes in the outer surface as terrain.24. Sheets vs terrain.24: only PLANE,
    mean shape 86.7 -> 86.4%, colour 85.9 -> 85.5 (the art's nose is rounder than any one-way shape: outline
    96.2 -> 95.3%). Triangles 868,212 -> 828,452. Not yet seen in the headset.
  - Owner on terrain.25 (renders): looks good; the canopy's front corners seem too close to the edges of the nose's flat
    top (art: a bigger gap); the hood by the canopy not noticeable, with a black rectangle across it. Measured from the
    game's angle: the hull's outline there matches the art within a pixel, the canopy's far front corner is 2-3 px
    nearer it than the art's (the cabin is limited only by the nose's width, CABIN in_nose, so at the front it fills the
    deck). Shown: corners kept 3 or 5 voxels in from the nose's sides (CABIN margin), a narrower canopy; owner: leave it
    for now, revisit later (margin stays 0). The black rectangle: the cockpit's level-1 tiles have the roof pieces'
    floors above them, and the table lights such a tile as indoors (no sun, `indoors()`); in terrain.25 the deck dipped
    below level 2's floor (Y 48) in front of the glass, so that strip belonged to the level-1 pieces 49/51 and was lit
    as indoors. Offline renders don't apply that rule.
  - Hood study: in the art, behind the glass a raised hump to about Z 263, darker than the deck, its far top edge lit
    (traced by putting the art's pixels on the model from the game's angle; the dark band along the canopy's near side
    is the nose's own shaded shoulder, not part of the hood: a hood wrapping the canopy's sides matched worse). Shown:
    the roof carried back as a grey hood (flush with the deck at its back, or standing 1.5-3 above it; back corners
    cut), which blended into the deck; the hood 2-3 steps darker; a light line on its top edges (looked painted on).
    Owner chose E2b (2 steps darker), then asked for the art's gradient: G2 chosen.
  - terrain.26 (`skyranger.py`; no engine code changed; mesh hash without PLANE unchanged). The hood (CABIN zr 262,
    zg 273, yr0 53, rch 6): the cabin's part behind the glass's start (zg) is hull grey (cabin_post), its roof the
    cabin's roof carried back (53 at Z 262, 3 above the deck there), its back corners cut upright at 45 degrees
    (rcham, its own panels); the glass now from Z 273 (as the art; was 270) and higher at its back. Its tone by rule
    (hood_grad 241-248, 0.8 across): per table-voxel block, light along the middle by the glass to dark at the sides
    and back end, half steps a checker; symmetric (the art's lighter far side is its light from the left). The
    rectangle: bands(placed, c) lets the roof pieces over a column whose outer surface dips below Y 48 reach down to
    hold it (the cockpit's 52/53 now from Y 45, `.ref` base -3, as UFO1_10's -8); level-1 pieces left with nothing
    there get an empty model (49/51), since a part without a model is built from its collision voxels. Checks: no
    voxel drawn by two pieces, holes as terrain.25; in the preview the rectangle is gone and the hood stands out.
    Sheets vs terrain.25: only PLANE, mean shape 86.4 -> 85%, colour 85.5 -> 86% (49/51 now empty: their sprites
    show the cockpit, drawn by 52/53; 52/53 lower too, as the roof pieces' sprites end at their tile). Triangles
    828,452 -> 828,796. Not yet seen in the headset.
  - Owner after terrain.26: the outside looks good enough for now; next, the interior (in a new session). Where it
    stands: the hull is a shell one table voxel thick (build(): `cav = erode(outer, SHELL)`), its inner layer the
    `inside` area (palette 230/231, flat colour, not textured); no bottom under the cabin's floor tiles (X 64-96,
    Z 112-288: the game's own floors, rule-built, are the floor) or over the ramp's hatch; level-1 pieces hold the
    walls up to Y 48 where roof pieces (level 2) are above, else the whole height (bands()); the cockpit's level-1
    pieces 49/51 are empty and its roof is 52/53 from Y 45. Cutting the view to level 1 hides the roof pieces, which
    is how the inside is seen. The game's own idea of the interior is its collision voxels: PLANE.MCD (62-byte
    records, LOFT ids at offset 8, 12 entries of 2 voxel layers) and GEODATA/LOFTEMPS.DAT (16 rows of uint16 per
    template, bit 15 - x of row y); and the sprites (`dumpart PLANE`). Start with a study of what the art and the
    game's data show inside (seats, consoles, ribs, the cockpit) before building, as the owner prefers.
  Setup fixes from this session: setup.sh creates the Linux prefix's include/lib first and stops if the
  Linux OpenXR loader doesn't build (it needs `-I$VR_LP/include` for EGL in the cloud workspace; before,
  it was marked done anyway and CMake quietly built the preview without VR); build-preview.sh refuses a
  build without VR.
- Released so far: 0.12.2 (the first published release; 0.9.1 was prepared but never published). The
  tags proto-0.1.0 ... proto-0.8.0 on the fork are from before the tag-only-releases rule.

## Decisions (keep these unless the owner changes them)

Hand-built models (what made the small scout come out right, terrain.13-16; owner: so the next ones come
out better the first time)
1. Start from what the thing is, not from its pixels: list its features (hull, rim band, light, marks,
   legs), how they repeat and how they're symmetric. A feature the art shows once, or squashed at its
   edge, is usually repeated: the scout's "!!" faces all four diagonals (the art shows three, two of them
   slanted). Make repeats identical and evenly placed.
2. Shape: clean parametric shapes (a surface of revolution, struts, discs) whose few sizes are searched
   against the art's outline from the game's angle. Among sizes that match about as well, pick the one that
   keeps a part equally thick on every voxel layer (count voxels per layer): a slanted cylinder's radius
   can make it swing (ridges, kinks). Stamping a fixed footprint per layer is worse (offset blocks). Never fit voxel by voxel for hard-edged pieces: the
   exact-fit legs (terrain.14) matched every pixel and looked lumpy from every other side. Give designed
   features their real form (a flat top under the light, a rim at a fixed height), and stop when what's
   left is pixel noise. Organic pieces (tree crowns) can take more fitting.
3. Colour by area, not by pixel: mark the areas on the model and give each even colours from the art's
   palette (by reference to a pixel of that colour), a light speckle of 2-3 neighbouring entries on big
   areas. Area edges then follow the shape exactly. Taking the pixel each voxel lands on smears features
   one or two pixels wide (a voxel sits half a pixel off the art's grid): grey spilled into the rim.
4. Measure in 3D: map the art's pixels onto the fitted shape (rays from the game's angle) to read where
   features sit (height, radius, angle), and build them there.
5. No painted light: the art's light-left/dark-right shading, highlights and drawn shadows stay out of the
   colours; the table's sun and sky shade the model. Copied in, they double up and stay on the art's left
   whichever way the table turns (and mirroring the near side onto the far side copies them there too).
6. Look at all the art's frames, not just the first: pixels that change between frames are lights (the
   scout's rim, cap and lift). Model them as lights: glowing, stepping with the game's terrain frames,
   lighting nothing round them.
7. Look before sending: render the model files from several sides, not only the game's angle, and say
   plainly that it hasn't been seen in the headset. The owner judges in the headset.
8. Cost: count triangles per tile for anything repeated across many tiles (the hand-built wheat, ~3,200 a
   tile against ~950 rule-built, dropped his frame rate); a one-off piece like a UFO can afford more.
9. Scores: the shape score catches wrong proportions and placement; the colour score compares against the
   painted shading, so it drops for a model coloured by area (the scout's hull 5-89%) and is only a check
   that the areas sit in the right places, not a target.
10. Texture (the study after terrain.18; on trial on the Skyranger since terrain.19, where it replaces 3 and 5
   for the hull, wings, fins and pods): the art's own texture and shading, straightened onto each face (one
   colour per table voxel, the art pixel at the cell's middle, the grid's phase for the straightest rows;
   never copied voxel by voxel: the art's grid lies slanted on the faces and turns into streaks); the art's
   dithering as a checkerboard of the same two shades on the face's own grid, at the art's pixel size; panel
   lines (seams) joined into clean solid lines; painted markings as drawn. `vr-dev/models/straighten.py`.
   With it, faceted hulls with each panel lit evenly by its own direction (`.ref` VRF3; owner on terrain.22: the
   best result so far, kept). Where a shape isn't one constant section (a tapering nose, a hood, a beak) the
   texture goes panel by panel (`tex_panels()`, terrain.23). The full recipe, from the fins and wings (the best
   parts so far), what was dropped and where it gets harder: `vr-dev/TEXTURING.md`. Read it before texturing a
   new part.
11. When a part looks misplaced, check its neighbours against the art first: terrain.23's "too low" wings were
   at the art's height; the hull's top was too wide and covered their roots. Fit the outline of what's around a
   part (from the game's angle) before moving the part. The craft's outline along a constant section can't show
   heights (a roof's, a shoulder's); a piece's own sprite alone can (the roof pieces, terrain.24).
12. A part used in several places has one model: wherever it lands next to other pieces, check that no voxel is
   claimed by two placed pieces (faces drawn twice flicker or show as dark bars; the front wings, terrain.23), and
   that pieces meet without gaps. Shape the neighbours so the shared model fits every place.

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
- Craft and UFO hulls are their collision voxels, except the small scout (hand-built since terrain.13) and
  the Skyranger's outside (terrain.18; faceted and textured from the art, terrain.19; panels lit evenly,
  terrain.22; the front and the hull's top refitted, terrain.23; wings flush with the fins, terrain.24; the nose cut to one-way faces and the canopy rebuilt as a glass cabin,
  terrain.25; the hood behind the glass, terrain.26; its inside, ribs and cockpit, still to do).
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

- Skyranger nose as one triangle (owner, terrain.25 study): the flat deck from the hull's top at the wing roots
  (Y 49 at Z 256, x 16) to the front edge (33.4 at Z 293.5), chines x + Y + 0.636 (Z - 256) <= 65 down to the
  sides. Outline 95.9% (best of the study) but the chines step two ways (honeycomb pixels); owner preferred crisp
  pixels. Worth another look if faces that step two ways can be made crisp (next idea).
- Skyranger canopy front corners (owner, after terrain.25: revisit later): the art leaves more of the deck beside the
  windscreen's front corners. CABIN margin (keep the canopy that many voxels inside the nose's sides; 3 or 5 shown,
  glass match 0.753 / 0.750 vs 0.728) is in the generator, set to 0.
- Flat panel planes drawn by the renderer (terrain.25 study, offline preview only): each panel a true plane with
  its texture laid on it, so any shape gets crisp, regular pixels; an engine change, and the style moves toward
  low-poly.

## Where things are

`vr-dev/HISTORY.md` keeps the per-build notes of merged branches and the terrain branch's early builds.
`vr-dev/TEXTURING.md` is the recipe for texturing hand-built models from the art. `README-VR.md` has the file-by-file overview ("How it works") and the engine
hooks. In short: `src/VR/VrSystem.cpp` (frame loop, input mapping, lights,
test script runner), `VrBoard.cpp` (everything on the table: diorama, view
rules, HUD, fog, effects, minimap), `VrTable.cpp` (console buttons, action
keys, alert buttons, inventory tray, wrist pad, wall screens), `VrHands.cpp`
(hands and pinch), `VrXr.cpp` (OpenXR, controller profiles), `VrRoom.cpp`
(room layout), `VrWeather.cpp` (sun, weather, lightning over outdoor
battles), `VrLab.cpp` (the terrain lab), `VrRelief.cpp` (relief on floors), `VrShaders.h`,
`VrVersion.h`.
