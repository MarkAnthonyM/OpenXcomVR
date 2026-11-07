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
  tiles, animations, items and bodies, tree crowns). Builds `0.21.0-terrain.<n>` (now terrain.19).
  Owner's choices: one build (the lab is in openxcom.exe, command line only, view-only); relief from a
  hand-checked list of parts; holes a quarter level deep to start; both voxel steps and smooth relief to
  compare; hidden sides mirror the visible side, nearest colour where that fails; the small scout gets a
  hand-built model first (the other ships decided after it); bodies as short voxel lumps, small items as
  flat pictures with a faint outline.
  Findings: floors are all the same 2-voxel slab (drawn as a surface at height 0 since terrain.9), rocks/craters only painted inside the diamond; most
  objects and walls repeat the floor's bottom slab (the flicker: two coplanar faces); POLAR 60-62 are tiny
  ice bits on a 4x4-voxel block (the grey "missing" squares); the small scout's collision is near a cube.
  - terrain.1 to terrain.12 (the terrain lab, relief, the comparison and sheets, the farm trees, thin floors,
    model resolution): moved to `vr-dev/HISTORY.md` ("Terrain branch: early builds").
  - terrain.13 (owner: start the wheat and the small scout; apples as 4 voxels). Owner's choices: wheat dense
    like the art, 3 versions mixed by tile position; scout a thin shell like the art; an apple a 2 x 2 square
    facing out. farm_trees.py apple() (tree 9: 22 -> 88 fruit voxels; 12: 2 -> 8, its mirrored apples don't
    find leaves). VrModels: kind 5 PLAIN (any pixel; the wheat's mid browns class as FRUIT, so WOOD voxels
    skipped them and striped the field dark below / pale gold on top), versions `<key>-2.vox`.. (modelVariants;
    TileKey::variant from a position hash in tileKey), `<key>.ref` ("VRF1", float baseY, uint32 count, then
    part/column/row bytes per solid voxel in z, y, x order; 255 = own sprite) -> Model::ref / baseY; addModel
    takes the part's MapData for the set; floors can be modelled (buildTile modelledFloor; bottom faces at
    z 1 drawn when baseY != 0). Atlas: shelves by height class (4/8/12/16/then by 8), usedPercent() in
    boardinfo; first wheat (9000 quads a tile, 1 model voxel stalks) filled the 2048 atlas in the farm save
    and looped "atlas full; rebuilding". wheat.py: upright 2 x 2 stalks on 58% of table voxels, tops 8-12,
    ears 2-3.5 nudged a voxel, thin tips: ~1700 quads a tile; CULTIVAT 4 shape 92% colour 100%; farm save
    atlas 41% rows / 29% texels. small_scout.py: profile fitted to the composite (R 23, rim 4, top 23, rb 18,
    e 1.4; IoU 0.984; `--fit`), shell 2 model voxels, underside layer z 1 + inside floor z 2, slices per
    tile (UFO1 0-7 level 1), cap = UFO1 10 (floor on level 2) from level-1 height 16 with baseY -8, legs
    16-19 from the sprites (top 14.1 out / 25 up, foot pad 20.3 out). Refs: outer voxels mirrored to the near
    half (dx + dz -> -(dx + dz), same screen column) into the composite of hull + cap; inner/floor voxels to
    the far half into the far pieces (W, N, NW + centre floor). UFO1A map bytes - 2 = UFO1 ids (BLANKS
    first). dumpart now reaches any set the lab knows (findSet). Sheets vs terrain.9: only UFO1 and wheat
    moved (UFO1 hull 45-72% -> 71-92% shape; legs 38-68 -> 95-100; cap 96 -> 52: the model holds the dome over
    the centre tile, the cap's sprite only the light; isolated far/side slices show their inside, so colour
    63-76% for 7 and 1). Not yet seen in the headset.
    Cost: boardinfo now reports "terrain triangles" (all built chunks): `outside` 172k without the wheat
    models, 305k with them (llvmpipe: ~0.9 -> ~0.8 s a frame). That pushed v3 over its timing (the table
    advances at most 0.1 s a frame: VrSystem dt cap), so v3 and v2 wait 40 more frames before their last
    readout; both pass now (v2's old 0.062 m failure was the same timing). Full run: h2 and bh failed once,
    h2 passed alone (flaky, like bh).
  - Owner on terrain.13: the wheat dropped the frame rate in the headset, steadily (not stutters): drawing
    cost. Measured per wheat tile (temporary OXCE_VR_TRI_LOG in tileProto, not kept): hand-built 3120-3360
    triangles, rule-built 946-954 (a block's flat sides merge into a few rectangles; each of ~140 stalks has
    its own sides and top, the nudged ears and thin tips more); `outside` has ~58 wheat tiles. Cheaper
    generator variants counted (faces a tile): now ~1800, feet on the grid ~1600, + straight ears without
    tips ~590, + 40% fill ~450. Owner chose the rule-built wheat for now. He asked why UFO1 19 wasn't 100%:
    the legs were simple shapes from a few measurements (piston too thin, a knee ring the art lacks, top and
    pad a pixel high).
  - terrain.14: CULTIVAT_4*.vox removed (wheat.py and the versions code stay); triangles in `outside` back
    to 171,744. small_scout.py fit_leg(): rays() = every 4 x 4 sample's ray through the voxel grid (the
    comparison's view: covered when 8 of 16 samples meet the shape); a voxel for each art pixel whose
    footprint falls mostly in it (nearest the strut), then the strut's voxels that change no pixel's status,
    then single switches. Carving the strut first could not get there (a voxel straddles half a pixel row:
    no voxel's footprint lands exactly on one pixel, (i + j) - 2k is odd). Legs 16-19: 0 pixels differ in
    the game's comparison (no red or blue at all), 100% / 100%. Sheets vs terrain.13: only those and wheat
    (92 -> 90, rule-built).
  - Owner on terrain.14: the exact-fit legs looked worse in the headset than terrain.13's. Seen from four
    sides (offline render): a lumpy strut, the piston no longer round, stray voxels round the pad. The fit
    only sees the game's angle; a voxel straddles half a pixel, so each one-pixel detail becomes a bump or
    notch; the art's dark edge pixels and shading become shape. Lesson (owner agreed): don't chase hand-built
    hard-edged pieces to 100%; use the score to catch real mistakes (proportions, placement, a part the art
    lacks), fix those in the shape's parameters, stop when what's left is pixel noise, judge in the headset.
    Organic pieces (crowns) take more fitting. terrain.15: small_scout.py leg(shape) with LEG = top 23,
    piston_r 1.2 (the art's 4-pixel piston), piston 0.45, strut_r 0.55, no knee ring, pad_r 3.4, pad_h 0.9,
    cone_r 1.4; `--legs` searches those one at a time against the four sprites (wrong_pixels(): the
    comparison's 4 x 4 sampling via rays()). Pixels wrong (four legs): terrain.13 129, untuned 116, searched
    66 with pad_h 0.7; 86 with 0.9 kept (0.7 leaves the pad one model voxel thin). Game comparison: 16/17/18
    100%, 19 98% shape, colour 99-100%. fit_leg() removed.
  - Explorer for the owner (not in the repo; built from his data in the session, sent as a file, never
    published: it holds the game's art): the scout's 15 sprites assembled in the game's draw order (Map.cpp
    drawTerrain: z, then x, then y), spread apart, build steps, the table's model rendered from the game's
    angle beside it, piece colours, both cuts mapped onto the saucer from above. Finding while making it: I
    had told the owner the artist cut the saucer "along lines on the screen" (strips above each diamond);
    wrong. Mapped back onto the saucer, the art's pieces roughly follow the 3 x 3 tiles with hand-drawn,
    wandering edges (south and east reach diagonally in toward the middle); each sprite is bounded by its
    32 x 40 frame and nearer tiles draw over farther ones, nothing more.
  - Owner on terrain.15 (headset): close, but (1) grey voxels spill into the blue rim, (2) the pink top comes
    to a point (the art's is flat) with grey crossing its edge, (3) the "!!" marks should be even, aligned and
    on all four sides, (4) drop the art's light-left/dark-right shading? (agreed: the table lights it; painted
    shading doubles up and stays on the art's left whichever way the table turns; the mirrored far side
    carried it too). Causes: per-voxel colours from the pixel each voxel lands on, half a pixel off the art's
    grid, on 1-2 pixel features; the profile's superellipse top. The art's side marks (slanted, higher, at the
    left and right) are the same "!!" seen from the side: one ring of marks, four diagonals (the art shows 3).
    He chose: an even grey with a light speckle; rim lights like the flat game; cap pulse and lift flip like
    the game; glow at night on their own surfaces only (he asked whether glow gives the UFO away in the fog:
    no, unexplored tiles aren't drawn at all; only light thrown on explored ground nearby could).
    Animation (decoded UFO1.PCK/TAB/MCD; the game steps terrain frames every 100 ms, BattlescapeState
    DEFAULT_ANIM_SPEED, Tile::animate): near hull pieces 3-7 swap two sprites each frame (the rim's specks
    in two interleaved sets: lights running round); cap 10: frames 9, 10, 11, 10 (purple x 0.88/0.76/0.89,
    0.83/0.67/0.84 of the first); lift 9: 8, 8, 17, 17 (dark frames x 0.74/0.46/0.71). The far pieces 0-2 and
    the legs don't animate.
  - terrain.16: profile with a flat top (radius() rc: R 23, rim 3.5, top 21, rb 18, e 1.4, rc 4.5; IoU 0.987).
    colours(): areas by place on the shape, each from palette entries by reference (pixel_with(): a pixel of
    that colour in a UFO1 sprite): hull 244/243/245 speckle (hash), rim band 115/114 (1-5 up, radius 20.8+),
    20 rim lights (224) at 45 + 18 k degrees, 2.5-4.1 up, half-width 0.55, sets A (even k) / B (odd), collar
    241/240 (top - 3.5 up, radius <= 7), light 194 (centre 193, radius <= 2) pulsing (radius <= 5), marks 5
    (light grey) at 45/135/225/315 degrees, bars +-0.75..2.15 along the rim at radius 10.6-12.6, dots
    13.3-14.0 (measured: assembled picture rows 81-85 mapped onto the shape), inside 227/228, inside floor
    198/197, underside the hull's (the table darkens it). Lift: UFO1_9 model (floor part, used at level 0
    and level 1's middle): a plate one model voxel thick, baseY -0.5 (flush), its own sprite's pixels,
    flipping. `.ref` "VRF2": header + the lights' colour (part, column, row, 0), 4 bytes a voxel (part,
    column, row, light). Board: mask values per face kind (greedy merges like faces only), MAT_GLOW_PULSE /
    FLIP for lit voxels, a layer 0.04 out for rim lights A/B (MAT_GLOW_A/B) over the band's own colour;
    shader: materials 10-13 output the texture x uPulse/uFlip x uTint, unlit, never darkened; A shows on even
    frames, B on odd (discarded otherwise); Board::draw sets uAnimFrame from SavedBattleGame::getAnimFrame()
    (lab: SDL_GetTicks()/100), uPulse/uFlip constants measured above (UFO1's; another animated model with
    other patterns would need them per pattern). VrCompare skips material 11 (frame 0 = the art). Sheets:
    shapes about as before (hull 72-93%), colour 5-89%: expected, the art's painted shading is gone. Preview:
    in the farm save the rim's specks change between frames; at shade 15 the hull is dark, the lights lit.
    Not yet seen in the headset.
  - terrain.17 (owner: look at the legs again with the hand-built notes in mind): the legs were the scout's
    last part still coloured pixel by pixel from their own sprites (the art's shading: a lit stripe and a
    dark side on the piston, a dark cap, a lit pad edge, each leg different). leg_colours(): areas by height,
    piston 227/228 above the knee, rod 231/230, pad (y <= pad_h) the hull's 244/243/245; written as `.ref`
    (VRF2, no lights). Read from the sprites: piston ramp 226 lit .. 231 dark, rod 229 .. 234, pad 240-249
    (the hull's greys); the piston's dark top sits under the hull, left out. LEG piston_r 1.2 -> 1.3, strut_r
    0.55 -> 0.5: voxels a layer piston 21-24 (was 16-21: ridges), rod 3-4 (was 3-5: kinks); pixels wrong over
    the four legs 86 -> 84. Tried and dropped: a fixed footprint stamped per layer (perfectly even, but the
    slanted leg became a stack of offset blocks). Sheets: legs shape 98-100% as before, colour 55-74%
    (expected: no painted shading). Hull files unchanged. Not yet seen in the headset.
  - terrain.18 (owner: the Skyranger next; chose the outside first and the inside's ribs (real, standing
    out a voxel) in the following build, markings on both sides, nozzles plain blue, not glowing).
    `vr-dev/models/skyranger.py` (after `dumpart PLANE` in the lab): PLANE.MAP 10 x 20 x 3, nose south
    (+Z); map bytes - 2 = PLANE ids; nothing in PLANE.MCD animates. The craft is built whole on one grid
    (map X 0-160, Z 96-304, Y 0-80, 2 cells a table voxel; Craft.comp = component, .area = colour area),
    then cut: holds() says which components a part may hold (level 0 gear/ramp; level 1 pods, else the
    fuselage/cone/canopy, floors none: the game's floors stay; level 2 wings, fins, else the roof), bands()
    its heights (a level-1 part holds up to the top where level 2 has no fuselage piece: the arch's
    shoulders, the nose). A part used in several places gets one model: its most common shape (ties: its
    first place); differences are printed (only the rear roof row: the end's top, and the front wings
    poking 1 table voxel into the fuselage at roof height, hidden). Fitted (common silhouettes, shared /
    union against the art of those parts): fuselage section w 28, floor 24, top 52, e 2.5, bottom rounding
    24 (85% of the middle); constant Z 112-256 (forced: the game repeats roof pieces 60/61 rows 7-15),
    nose zt 298, p 1.5, q 2, tip height 28, tip half-width 4 (90%); wing plate Y 48-49, trailing edge Z 112,
    leading edge Z = 126.5 + 0.754 X, tip at X 0 (the art mapped onto its plane); fins plates at X 64-65 and
    95-96, Z 112-142 at the roof, top Y 72 back to Z 131 (mapped onto their planes); pods centred in their
    tiles (forced: both pods use parts 41/42) X 40/120, Y 32-48, chamfer 2, Z 116-170, intake leaning 4
    forward at the top (89%; off-centre fits were worse); main gear X 56/104, Z 152, wheels r 4.5 w 2 at X
    +-3, Z +-5 (the art's overlap, spaced to touch), nose gear X 72/88, Z 250, a brace back 9; canopy
    ellipsoid Z 274, Y 41, radii 11/8/17 (64% of the art's glass pixels, the rest its frame bars); yellow
    from Z 294 plus a cone to Z 303 (60% of the art's yellow); ramp from (Z 144, Y 24) to (Z 96, Y 0).
    Colours by area from the palette (AREAS), shading left out: hull 246/245/247 (the middle of the art's
    240-252 ramp), inside 230/231, pods 243/242/244, red 34/35, stripe 149/150, nozzle 132/131/133, etc.
    The wings' panel lines from the art mapped onto the wing plane (one along the span at Z 123.5, two
    across); the darkest pixels near the root there are the fuselage's painted shadow, left out. Markings
    mirrored to the other side (owner): roundel (Z 246, Y 37, r 4.5; a red cross), the fins' "V" and
    stripe on their outer faces. Whole craft against the assembled art: 96% shape. Sheets: PLANE mean
    shape 82.8 -> 84.9%; the cockpit pieces 48-53 score 14-33% in isolation (their sprites draw consoles
    and glass inside, the model's cut gives them the shell), the composite is what counts there.
    VrBoard buildTile: any part slot can have a model now (modelled[O_MAX]; the left fin and a nose piece
    are walls); an object's model still paints the ground from the floor's art. Cost: `outside` with
    `reveal` 809k -> 857k terrain triangles (+48k, one craft). Not yet seen in the headset.
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
  the Skyranger's outside (terrain.18; faceted and textured from the art, terrain.19; its inside, ribs and
  cockpit, still to do).
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

`vr-dev/HISTORY.md` keeps the per-build notes of merged branches and the terrain branch's early builds. `README-VR.md` has the file-by-file overview ("How it works") and the engine
hooks. In short: `src/VR/VrSystem.cpp` (frame loop, input mapping, lights,
test script runner), `VrBoard.cpp` (everything on the table: diorama, view
rules, HUD, fog, effects, minimap), `VrTable.cpp` (console buttons, action
keys, alert buttons, inventory tray, wrist pad, wall screens), `VrHands.cpp`
(hands and pinch), `VrXr.cpp` (OpenXR, controller profiles), `VrRoom.cpp`
(room layout), `VrWeather.cpp` (sun, weather, lightning over outdoor
battles), `VrLab.cpp` (the terrain lab), `VrRelief.cpp` (relief on floors), `VrShaders.h`,
`VrVersion.h`.
