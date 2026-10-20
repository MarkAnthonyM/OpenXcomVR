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
- Branch `vr-prototype-weather` (from vr-prototype 0.13.0): the weather and
  time of day, builds `0.13.0-weather.1` ... `.13`, merged into vr-prototype as
  0.14.0. It brought the sky debug panel on the left wrist (`vrDebugPanel`,
  `VrDebug.cpp`), the moon, the sky visuals and vanilla mode (`vrVanillaSky`).
- Branch `vr-prototype-scanner` (from vr-prototype 0.13.0, 0.14.0 merged
  in): the motion scanner in the hand (`VrScanner.cpp`), builds
  `0.13.0-scanner.1`, then `0.14.0-scanner.<n>`.
- Branch `vr-prototype-thunder` (from vr-prototype 0.14.0): better thunder,
  builds `0.14.0-thunder.<n>`. The owner asked for the three improvements
  (variety, direction, distance) on their own branch.
  - `VrAudio.*`: the table's own mixer channels. The game allocates 16 and
    reserves 0-4; VrAudio adds AUDIO_SLOTS more and reserves 0..4+slots, so
    ours are 5.. (the game's free pool keeps its size) and a position set
    with Mix_SetPosition never sticks to a channel the game uses next.
    `playAt` (volume, angle clockwise from ahead; only with
    Options::StereoSound) and `moveSound` (each frame while playing; clears
    the position when done). Slots: two for thunder (a new clap doesn't cut
    the last roll), one spare (meant for the scanner's sounds).
  - `synthThunder(near, variant, freq)`: 4 near + 4 far variants, each its own
    noise, scaled to the same peak (the volume says the distance). Near: the
    old recipe (crack + rumble in swells, 4 s), first swell right after the
    crack. Far: no crack, a third low-pass, soft start, longer swells and
    slower decay (6 s). Made on first use per variant.
  - Each strike gets a bearing (its own Rng, so the weather's dice are
    unchanged) and a variant; the thunder plays from that bearing relative to
    the head (Board::setListener -> WeatherView::viewerFwd, map space) and
    is re-aimed every frame as the head turns. The flash itself is still
    undirected. Test op `thunderwav <path> <near 1/0> <variant>`.
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
- 0.13.0-weather.2: the moon is computed (low-precision orbit from the game
  date/time and the site; phase from the sun-moon elongation), starlit when
  below the horizon; darkness 10-11 is a twilight blend. Under the sun/moon a
  map tile's brightness is split in the shader (mode 3): min(tile, map shade)
  is lit by sun/moon + sky, the rest is the game's own light, lit by uLampCol
  (scaled like the old night key so brightness matches); point lights other
  than the key stay off map surfaces (gMap). The owner turned down a visible
  sun or moon (a voxel disc with puffy clouds, stars, light shafts looked
  cartoony and took the menace away; weather.3): they show only through
  light. A coloured room light low on the light's
  side, cloud shadows at night too (moonlit nights roll clouds 60%), and on a
  moonlit night the unlit ground's brightness is at least uMoonFloor (up to
  0.05 for a full, high moon) times the cloud shadow. Weather and the (now
  empty) sky are drawn after the room's solid parts (Board::drawWeather): the
  back-wall screens drawn later would cover them. `vrVanillaSky` = the
  pre-0.13 table (Weather treats it like indoors).
- 0.13.0-weather.4: the rim light on backlit standees was removed (the owner
  found it didn't help), with the dead sky-object code (Weather::drawSky,
  shader mode 7). The moon is worked out for every outdoor mission (not only
  at night) so the panel and marker can show it. With `vrDebugPanel` on,
  Board::Impl::drawSkyMarker draws over the map (no depth test, after the
  weather): a compass ring, the real sun and moon (SkyInfo realAz/realElev,
  moonAz/moonAlt, phase disc) with labels, the table's key light (sunAz/
  sunElev, colour, darkness) and the sky clock; the status line gets
  `mark=sun+90,moon-69` (test `mark`, script skymark). Board labels can be
  256 px wide now. The `skyclock` script op re-runs the sky at once.
- 0.13.0-weather.5: a moon under the horizon is mirrored above it (same
  azimuth, |altitude|; SkyInfo moonMirrored/tableMoonAlt), its light the phase
  but at least 0.4 (moonLight); no starlit nights any more. Warmer SUN_COL
  ending red-orange at darkness 11, a pinker DAWN_COL for mornings, and the
  sky fill going from DAY_FILL to a blue-violet DUSK_FILL / lavender
  DAWN_FILL. Soldiers' light pools take the sky light's hue (uPoolSkyCol:
  sky.col scaled to the lamp's luminance, no channel above 1.4); fires and
  lit items keep uLampCol. Per tile, Board::Impl::poolWarm (LL_FIRE/LL_ITEMS
  vs LL_UNITS) goes in the chunk vertex alpha (and the chunk hash), units get
  it as uPoolWarm; mode 3 mixes the two colours by vColor.a * uPoolWarm.
  Cloud shadows (shader cloudCover): 3-octave value noise at 1/70 per map
  voxel, a narrow threshold (0.545-0.585) for crisp edges and ~35% cover;
  under a cloud the sun is cut by uCloud.x (day 0.60-0.75, night 0.65-0.85,
  forced 0.68/0.75), the sky fill by 30% of that and mode 3's base term dims
  toward a cool blue; drift 16 map voxels/s (22 in rain).
  test/run.sh copies the test options.cfg before every run (the game saved
  -vrDebugPanel true into it, so later tests ran with the panel on).
- 0.13.0-weather.6: SUN_ELEV lowered (50..40 day, 35..20 golden hour,
  16/12/8 at 9-11) so shadows show from any seat (the owner saw them only
  with the sun across the table: with the light behind you, short shadows
  hide behind what casts them). The evening's golden hour (darkness 4-8, not
  mornings) is a deeper gold and 25% stronger. In mode 3 the base term is
  dimmed to 60% where the sun is blocked (gSunVis: shadow x cloud). Terrain
  (uTerrain, set around the chunk draws) has no sun/moon highlight unless
  raining (uWet: a small, sharper sheen); figures keep theirs. Clouds:
  cloudCover() projects the fragment along the sun to a layer 200 map voxels
  up, snaps to a 4-voxel grid moving with the drift (uCloud2.z), turns into
  the wind's frame stretched 1.5 : 1 (uCloud2.xy, .w), domain-warped 4-octave
  noise with a clumping threshold (0.50 +- 0.07): about a fifth of the sky.
- 0.13.0-weather.7: one shade model (shader sunShadow/sunOpen): sunShadow
  is the shadow map (0 blocked .. 1 lit), sunOpen adds clouds but only where
  the sun reaches (no cloud pattern indoors); in any shade the sky fill drops
  to 70% and mode 3's base term to a cool ~45%; the moonlit floor is shaded
  the same way. uSunBoost (SkyLight.boost, status glow=): the direct sun on
  the battle map is multiplied by shadeLight(shade - lift) / shadeLight(shade),
  lift 3 at darkness 4-8, 2 at 9-10, 1 at 11; the shade keeps the game's
  darkness. The shadow pass draws every level (and units above the view
  level), so roofs the view cuts away still shade the rooms below. (Standees
  already face the light in the shadow pass: setShadowPass swaps the viewer.)
- 0.13.0-weather.8: soldiers' light (LL_UNITS share of a tile's own light,
  poolWarm) on tiles open to the sky is drawn as the sun/moon itself (mode 3
  skyLit: uPoolSkyCol x direction factor x shadow x cloud x 0.85, plus
  uPoolFillCol x shade); terrain gets ndl / max(sun.y, 0.2) (flat ground full,
  capped 1.6), figures a wrapped 0.55-1.0. Under a roof (Board::Impl::roofed:
  a floor on any level above; chunk vertex alpha + 2, uPoolRoofed for figures)
  it stays the flat uPoolSkyCol light. Fires and lit items keep uLampCol.
  Cloud drift per mission from the wind roll (Weather cloudSpeed): normal
  16 x 0.4-1.0, storms (rain, dust, sand) 22 x 0.7-1.0 map voxels/s, gusting
  +-15%; status cloudspeed=.
- 0.13.0-weather.9: roofed() counts only a floor on the level right above
  (a Skyranger's wings two levels up had made the ground under them
  "indoors", so the soft light filled the soldiers' shadows there). SUN_ELEV
  sunset end raised (... 26, 23, 20, 17, 15 at darkness 7-11; was 23, 20,
  16, 12, 8); the moon's table height is at least 20 degrees (was 12).
- 0.13.0-weather.10: standee self-shadow fix (the owner saw half a figure
  shaded when the sun came from the side: the visible card, turned to the
  viewer, was tested against its own light-facing copy in the shadow map).
  In the main pass each standee passes uFigShadow/uFigAxis/RV/RL/NL/Up and
  sunShadow() samples shadowAt() at the matching point of the light-facing
  copy (same height, same offset from the middle; Board::Impl::shadowLight
  is where setShadowPass turned them), so a card never shades itself while
  walls, roofs and other figures still shade the parts they cover. Mist 30%.
  Rain/snow strength (Impl::strength, rolled r5: 40/40/20; forced "rain" is
  steady, forcing it again steps 0-1-2, "rain:2" picks): sun dimming, cloud
  floor, wind (drizzle x0.5, storm x1.6; flurries x0.6, blizzard x2.4),
  particle count (x0.35 / 1 / 1.8) and drop speed; blizzard haze uses the
  mist sheets (Impl::haze, whitish, alpha 0.09). Gusts: Impl::gust() 1 +-0.4
  over ~55 s on steady/heavy, scaling count and wind. Lightning by strength
  (none / far only every 15-35 s / every 4-10 s, half near): near = full
  flash, thunder after 0.15-0.7 s at full volume; far = 0.28-0.45 flash,
  2-5 s, 30-45% volume. Status strength=, panel "Strength:" line, test storms.
- 0.13.0-weather.11: a roofed tile only counts as indoors (soft soldier
  light) when the sun can't reach its floor: Board::Impl::sunReachesFloor
  traces four floor points toward the sun through TileEngine::voxelCheck
  (terrain only); at least two must get out. Cached per tile (reachCache),
  cleared when the sun moves or a chunk is rebuilt; chunks under a roof hash
  the sun direction. Under the Skyranger's cabin the low sun got in from the
  side, but the soft light filled the soldiers' shadows there.
- 0.13.0-weather.12: the sun-reach cache is only cleared when the terrain
  changes (Chunk::terrain: a hash of the parts and UFO doors, worked out with
  the chunk hash) or the sun moves; before, every chunk rebuild cleared it
  (shade changes as soldiers move at night). Measured on the preview machine:
  474 roofed tiles traced in 2.2 ms (Skyranger save), 202 in 0.4 ms (farm).
- 0.13.0-weather.13: the sun-reach trace skips the tile's own object
  (V_OBJECT on the starting tile). The trace starts just above the floor,
  which is inside a craft's fittings on some cabin-edge tiles, so those tiles
  were called indoors and filled in. On the owner's Shadow_blocked_2 save,
  24,35 is now sunlit; 25,35 stays indoors because the object on 24,35 really
  blocks its sun (neighbour shading). Still left: a tile half in the sun (e.g.
  24,29) is all or nothing; the proper fix is a terrain-only shadow map.
- The wrist debug panel's darkness buttons are a table-only offset
  (`Board::setShadeOffset`: every tile's shade + n for the table's colours,
  and the sky worked out again); the game's global shade is never touched.
  The weather roll keeps using the game's darkness for its seed.

## Known gaps

- Quest 3 and Steam Frame layouts have never been run on those headsets.
- Items lying on the floor aren't shown on the table map; ground items taller
  than three cells are only in the screen's inventory.
- Weather and the sun have only been judged in the desktop preview (no
  headset here); thunder has never been heard (the preview has no audio).
- Changing video options in-game stops VR (restart).

## Where things are

`README-VR.md` has the file-by-file overview ("How it works") and the engine
hooks. In short: `src/VR/VrSystem.cpp` (frame loop, input mapping, lights,
test script runner), `VrBoard.cpp` (everything on the table: diorama, view
rules, HUD, fog, effects, minimap), `VrTable.cpp` (console buttons, action
keys, alert buttons, inventory tray, wrist pad, wall screens), `VrHands.cpp`
(hands and pinch), `VrXr.cpp` (OpenXR, controller profiles), `VrRoom.cpp`
(room layout), `VrWeather.cpp` (sun, weather, lightning over outdoor
battles), `VrShaders.h`, `VrVersion.h`.
