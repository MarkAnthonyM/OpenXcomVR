# VR prototype: design history of merged branches

Moved out of `HANDOFF.md` (which must stay small enough to read in one go). These are the
per-build notes of branches already merged into `vr-prototype`: what each build changed and
why, with the owner's choices. The decisions they produced are summed up in HANDOFF's
Decisions; look here for the details (shader uniforms, timings, levels).

## Merged branches

- Branch `vr-prototype-weather` (from vr-prototype 0.13.0): the weather and
  time of day, builds `0.13.0-weather.1` ... `.13`, merged into vr-prototype as
  0.14.0. It brought the sky debug panel on the left wrist (`vrDebugPanel`,
  `VrDebug.cpp`), the moon, the sky visuals and vanilla mode (`vrVanillaSky`).
- Branch `vr-prototype-scanner` (from vr-prototype 0.13.0, 0.14.0 merged
  in): the motion scanner in the hand (`VrScanner.cpp`), builds
  `0.13.0-scanner.1`, `0.14.0-scanner.2-4`, then `0.15.0-scanner.<n>`
  (0.15.0 merged in); merged into vr-prototype as 0.16.0 (scanner.12, owner
  approved). The button sounds were set aside (the buttons are silent).
  0.20.1 (fix on vr-prototype, owner report): a 2x2 unit (reaper, cyberdisc)
  counted as four contacts, with four beeps. scan() now keeps two lists:
  `contacts` per tile (the rings and arrows, as the game draws them) and
  `units` per unit at its nearest tile (the count, nearest, beeps, haptic
  ticks, status). Status adds `tiles=<n>` when they differ. Test `bigscan`
  (save `bigunit.asav`: outside.asav with the sectoid made a moving reaper at
  10,24,0) expects contacts=6 tiles=9.
- Branch `vr-prototype-rain` (from vr-prototype 0.20.1): the weather's
  sound (owner: rain had none). Owner's choices: rain both all around the
  room and on the table; snow silent except a blizzard's wind. Builds
  `0.20.1-rain.<n>`.
  - rain.1: three loops made in code (VrWeather.cpp, like the thunder),
    their ends crossfaded into their starts (loopOf, equal power): the room
    (synthRainRoom: muffled noise wash, rumble and low-passed drops; a
    different noise per ear, a stereo chunk, so it surrounds), the table
    (synthRainTable: crisp drop ticks, some with a short falling ring, over
    a faint hiss; mono, placed with Mix_SetPosition at the table's middle,
    distance from WeatherView::metersPerVoxel), the wind (synthWind: noise
    through two moving resonances over a low rush, gusting; leaned towards
    the upwind ear with Mix_SetPanning, at most ~2/3 in the far ear). Made
    per strength on first use and kept. Channels: AudioSlot AUDIO_RAIN_ROOM,
    AUDIO_RAIN_TABLE, AUDIO_WIND (VrAudio: playLoop, setLevel, placeLoop,
    makeStereoChunk). Impl::updateSound sets each loop's level every frame:
    room 0.3/0.5/0.75, table 0.45/0.6/0.8 by strength, x(0.8+0.2 gust) when
    steady or heavy; wind 0.7 scaled by the gust; fades 2 s in, 1 s out.
    A re-roll (debug panel, test `weather` op) keeps the loops (begin() uses
    Impl::reset(), only end() stops the sound), so a change crossfades;
    leaving the battle or going indoors fades them out (0.8 s).
    Status: `sound=room:<%>,table:<%>` / `wind:<%>` / `-`. Test op
    `weatherwav <path> room|table|wind <strength>`; test `rainwav` writes
    them all to shots/. Playback can be checked without a sound card:
    `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=<file>` on run.sh (raw s16
    stereo at 22050 Hz; it doesn't keep real time on this machine).
  - rain.2 (owner: the table's patter sounded cartoony, like birds
    chirping; should be rain on earth or grass; all too loud, should sit
    under the music): Drops has no ringing tones any more (they chirped),
    each tick swells in over 0.3 ms; the table is soft dull taps (low-pass
    ~3.4 kHz, 1.5-5 ms, even sizes, 90/350/1000 a second) over a fluttering
    rustle (noise 1.2-4.5 kHz trembling at ~35 Hz), high-passed at 250 Hz:
    centroid ~3.5 kHz (was ~6 kHz). Levels about a third of rain.1: room
    0.1/0.16/0.24, table 0.12/0.17/0.24, wind 0.22.
    Also the alien turn's fade (owner: it went washed-out grey instead of
    to black): main() mixed the map toward (0.035, 0.040, 0.057), which on
    screen is ~52-67/255, while the darkness under it shows at ~4-10/255
    (the glass darkens the cover). It now mixes toward (0.0012, 0.0016,
    0.0030), the darkness as it shows.
  Merged into vr-prototype as 0.21.0 (rain.2, owner: good for a first
  pass).
- Branch `vr-prototype-hidden` (from vr-prototype 0.19.0): the alien turn /
  hidden movement (owner: levels could be seen moving as hidden aliens took
  stairs, and the monitors showed the acting alien's stats). Builds
  `0.19.0-hidden.<n>`.
  - hidden.1: Board::Impl::alienView (side not the player's, no debug).
    followFlatScreen(): yaw FLAT_YAW (-45 degrees: the flat screen's top, the
    map's north-west, away from the player), focus snapped every frame to the
    flat screen's middle worked out from Camera::getMapOffset and the new
    Camera::vrScreenWidth / vrVisibleMapHeight (tile floor middle at sprite
    16, 32; x - y = (W/2 - off.x - 16) / 16, x + y = (H/2 - off.y - 32 +
    level * 24) / 8 + 1), win = that area as half sizes W/32, H/16 along the
    diagonals, tileSize fitting it to the map area (x0.98). reveal: rises in
    REVEAL_IN 0.06 s while the flat game shows its map (not Hidden Movement,
    and the top full-screen State is the BattlescapeState: mapOnScreen),
    falls over FALL_T 0.35 s otherwise (a flicker until hidden.3). The shader's main()
    discards map fragments outside the window or not yet revealed (uWinOn,
    uWin, uWinReveal, uWinInv; quarter-tile cells dissolve, the edge dithered
    over 0.7 tile) for everything Board draws (terrain, units, effects, HUD,
    haze, weather, sky marker); drawCover() lays an opaque quad over the
    whole map area 0.6 mm above the table glass, under the map's ground (MAT_
    FOG, uv.y 3: the void colour with drifting haze, glowing from battle
    lights). The view (yaw, zoom, focus) is saved when the alien turn starts
    and restored at the player's (no bringing the first soldier close then);
    the whole map dissolves back in once the flat game shows it. Removed: the
    freezes (chunk and fog rebuilds, smoke/fire snapshot, shots and blasts
    hidden while the movement was hidden). Board::rotate / zoom respect
    viewLocked. VrTable: jam / jamPicture: the stats, minimap and roster
    monitors tear up (rows thrown, colours split, flashes) over what they
    showed, fade into dark rolling static by 0.9 s, flicker every few
    seconds, and come back at the player's turn through 0.6 s of
    interference (Wall::shown / useShown hold the jammed picture; no
    UnitInfoState is made for aliens). renderMinimap shows static during
    the alien turn. boardinfo shows alienTurn / hidden / reveal / win.
  - hidden.2: zooming works in the alien turn (owner: too close to check
    things): Board::Impl::alienZoom multiplies the flat view's fitting zoom
    (followFlatScreen), clamped to the usual 0.02-0.14 m tiles; set by
    Board::zoom (trackpad) and a two-hand pinch (beginGrab lets the hands
    in, updateGrab only zooms then); kept for the battle's later alien
    turns, reset in bindBattle. Turning and sliding stay locked.
  - hidden.3: the darkness falls without the flicker (owner), a plain
    dissolve over FALL_T; the monitors keep their glitch.
  - hidden.4: no dissolving specks either (owner: distracting): the window
    fades evenly (main(): fragments mixed toward the darkness's colour by
    reveal, see-through ones fading out, discarded at 0). Its edge stays
    dithered and fixed (winInside); a smooth edge (mixing toward that colour
    by distance) showed light outlines around the window's unexplored
    ground, which is darker than the darkness. Same timings (REVEAL_IN
    0.06 s in, FALL_T 0.35 s out).
  Merged into vr-prototype as 0.20.0 (hidden.4, owner approved).
- Branch `vr-prototype-fog` (from vr-prototype 0.18.0): the fog of war
  (owner: the table should feel as suspenseful as the flat map's darkness; on
  the flat map a shot comes out of the black, and a building's dark outline
  sometimes hints where from). Builds `0.18.0-fog.<n>`.
  - fog.1: the voxel cloud bank is gone. Unexplored ground (fogTop) is a
    near-black slab on the ground (buildChunk, z 0) under a haze
    (Board::Impl::buildFog): two quads per quarter tile, 3 and 9 voxels over
    the highest fogged level, vertex alpha 1 over fogged tiles fading to 0
    0.6 tile out onto explored ground (smoothstep on the distance to the
    nearest fogged tile), uv.y = 2 marks them. Drawn blended without depth
    writes, so shots inside show. Shader (vMat 9, uv.y > 1.5): colour
    (0.050, 0.058, 0.082) x drifting two-octave noise, alpha 0.75 / 0.45 x
    density; it glows from the battle's own lights only (uFxFirst: the index
    where Board::lights start in the light list) with exp(-d / 0.07) / d^2,
    so a blast or shot shows roughly where, never what is there. Taps
    (tileUnder) and the laser (raycast) reach the void on the ground and
    still pick the highest fogged level; rain over the void stops at the
    highest explored floor above the fog, else the ground (updateStops), never
    at anything unseen. Test bursts (testburst) now give off an explosion's
    light (testFlashT) for the previews. Owner saw stills (tap_test.asav):
    day, dusk, night, a blast in the dark. Merged into vr-prototype as 0.19.0
    (fog.1, owner approved after the headset).
- Branch `vr-prototype-shade` (from vr-prototype 0.17.0): which roofed
  tiles keep the soft indoor light (Board::Impl::indoors). The owner found no
  figure shadows under the Skyranger (the low sun can't get under the deck, so
  those tiles were indoors: a soft light in the sun's colour, which read as
  sunlight without shadows) and liked the ground there lit as outdoors (the
  craft's own shadow; figures' shadows where the sun gets in). Two test builds
  to compare, chosen by ROOF_RULE at the top of VrBoard.cpp:
  - shade.1, ROOF_UNDER_CRAFT_OUTDOORS: the ground under a landed craft is
    outdoors: the floor right above belongs to the craft's map data sets
    (Board::Impl::craftSets, from the craft in the battle's
    RuleCraft::getBattlescapeTerrainData, BLANKS left out; SavedBattleGame
    doesn't keep the craft's position) and nothing on the tile does. The cabin
    and buildings stay indoors.
  - shade.2, ROOF_ALL_OUTDOORS: every roofed tile is lit as outdoors (inside
    buildings and the cabin too, only the sky fill where the roof shades).
  - shade.3: the owner chose shade.1; ROOF_RULE removed, indoors() skips
    underCraft() tiles. Merged into vr-prototype as 0.18.0 (owner approved).
- Branch `vr-prototype-select` (from vr-prototype 0.16.1): fixes to tapping
  soldiers and tiles on the table (owner's reports, with his save, now
  vr-dev/test/saves/tap_test.asav, test `tap`); builds `0.16.1-select.<n>`;
  merged into vr-prototype as 0.17.0 (select.1, owner approved).
  - select.1 (VrTable::updateTaps, Board::tileUnder / unitMarkers):
    1. A tap on the Skyranger's deck in front of a soldier inside selected
       the soldier standing under that tile on the ground: heads were found
       by distance only (a ball of max(1.6 cm, 6 voxels) + the fingertip),
       before the tile. Now a head is skipped when an explored floor is shown
       on the level right above it (UnitMarker::covered: show its level to
       tap it), when the finger is over a floor higher than the unit
       (tileUnder), and while the finger is pressing a tile (it went on down
       through the deck onto that head).
    2. Taps on enemies did nothing: any head the finger was in swallowed the
       tap (touching), and only our own could be selected. Now someone
       else's head lets the tap go on to its tile. While aiming (the current
       action's targeting: shots, throws, psi), coming down onto any head
       (own soldiers too, owner's choice, as the flat map's click; mind
       control counts by the unit's current side) clicks its tile, once
       (aimedAt: no tile tap until the finger is back above that height).
    3. Roofs over buildings not yet entered didn't take taps (no buzz): the
       fog check counted unexplored levels up from the ground and took the
       fog bank's top (13 voxels into the level below the roof) before any
       explored floor above it. Now explored floors from the top down come
       first, as the laser's raycast already did; a tile with a unit on it
       counts as a floor too (standing on something on the level below).
    Test ops `aim [snap]` (start aiming the right-hand weapon) and
    `pokehead x y z` (poke the head of whoever stands there); boardinfo
    shows the selected unit and its position.
    Test `v2` failed on vr-prototype 0.16.1 (the walked soldier ended
    0.062 m off centre, the check allows 0.06); it passes on 0.21.0-terrain.1.
- Branch `vr-prototype-thunder` (from vr-prototype 0.14.0): better thunder,
  builds `0.14.0-thunder.<n>`; merged into vr-prototype as 0.15.0 (thunder.1, owner
  approved). The owner asked for the three improvements
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

## Sky and weather builds (0.13.0-weather.2 to .13)

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
  side (removed in 0.16.1: the owner read its glow on the wall as a picture
  of the sun or moon), cloud shadows at night too (moonlit nights roll clouds 60%), and on a
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

## The motion scanner (vr-prototype-scanner)

- Scanner branch (`vr-prototype-scanner`): when the game's ScannerState is the
  top state, VrScanner shows a handheld scanner in the right hand (left if
  only it is tracked); closing only with a button (letting go was rejected:
  accidental drops), which sends the game a right-click.
  - scanner.1 (0.13.0): a voxel model of the item's BIGOBS art with the radar
    picture turning on its screen. The owner then asked for the scanner of the
    game's scanner screen instead, with a camera.
  - scanner.2 (0.14.0, after merging the weather round): the device is
    DETBORD.PCK made solid (`build`: one alpha-cut textured front quad, walls
    where the surface steps, a flat back; 0.8 mm per art pixel; the screens
    set in, the four buttons standing out). Rects in the art: camera 56,24
    152x152 (the game's ScannerView spot), compass bar 217,29 47x11, side
    display 216,48 49x81, buttons blue 216,136 / yellow 240,136 / green
    216,160 / red 240,160 (17x17). Held with the fist just off the right edge
    (art 298,150; left hand 22,150) so the fingers keep off the side panel.
  - The camera (owner's choices): low resolution in the radar's green, 152 x
    152, slightly zoomed (45 degrees; blue/yellow step 25-75). VrSystem's
    renderScannerFeed draws the room and the board from the back of the
    device into its own target every other frame; the scene shader's `uFeed`
    pass (feedColor) turns it into 8 steps of green and adds the ping and the
    contact rings from world position -> tiles from the soldier (uFeedO/E/S).
    Rings, not markers at the unit: they light whatever is at the tile, any
    floor (the owner wanted it faithful: the game doesn't tell the floor).
    Contacts are exactly ScannerView::draw's (units with motion points within
    9 tiles, size motionPoints / 5 up to 5), minus the soldier himself.
  - The ping (owner's idea): from the soldier to the corners of the range in
    1.2 s, drawn only inside the 19 x 19 square, so it fizzles at the edges;
    a contact lights when the wave reaches it, the picture fades from 1.5 s to
    2.4 s (to 12%), a new ping every 2.5 s; a haptic tick per contact.
  - Compass bar: the map's 8 directions sliding past a middle mark (45 degrees
    = 18 art pixels), game small font. Side display: the cropped game radar
    (north up, or turning with the device inside a circle, the whole square
    kept) with the camera's view as a wedge; readout "CONTACTS n" (or "MOTION
    n" if too wide), "NEAREST", "<tiles> <dir>".
  - scanner.3, after the owner's first headset try: the device is tipped 35
    degrees in the hand (TILT: the camera looks below the hand's forward, the
    screen tips up; his wrist had to bend down too far). The camera skips the
    table's markers (Board::setFeedPass: no drawHud). While the scanner is out
    the map can't be moved and no input reaches the game except its own close
    button (scannerLocked in VrSystem: board.clickScreen, floating-window
    mouse, table keys, trigger, right-click, Back, the wheel; sticks, pads,
    level and zoom buttons, grabbing the map): any click on ScannerState
    closes it. The whole picture follows the ping (owner: fade to black, but
    no long black stretch): each pixel shows from when the wave passes it
    (plus a random 0-0.3 s, so it fills in as points) for 1.9-2.35 s (random,
    so it dissolves), so every spot is black for well under a second before
    the next wave refills it; outside the 19 x 19 range it is always black.
  - scanner.4: the dissolve sweeps outward too: each pixel shows for 1.8 s
    (+ up to 0.4 s, so it dissolves) from when the wave passes it, and the
    next ping starts as the edge of the range begins to go out (cycle = wave
    crossing 1.2 s + 1.8 s = 3 s; every spot dark about a second; owner's
    rule). When the scanner opens, Board::showRange centres the table on the
    soldier and zooms out only if the 19 x 19 square (turned with the table)
    doesn't fit the map area; the view stays like that afterwards (owner).
    The controls stay locked: with the range on the table there's no need to
    turn or slide (the owner first asked for turning, for soldiers at the
    table's edge). The green button (radar north up / turning) stays for now;
    ideas discussed for it: mark the contacts on the table until the end of the
    turn (favoured), ping now, show the range, freeze the picture.
  - scanner.5 (0.15.0 merged in, with VrAudio): sounds and the animation.
    Sounds (synthScan, made in code, three more VrAudio slots: ping, blip,
    UI, later two wave slots and UI, see scanner.12; from the device's place
    around the head): a sonar ping per wave, a
    blip per frame the wave reaches contacts (8 pitches, high = near the
    soldier, pitched by the nearest; same moment as the haptic tick), a
    power-on chirp when the screens light, power-off at closing, button
    clicks (a heavier one for red). No hum (owner). Test op
    `scannerwav <prefix>` writes them as WAVs.
    Animation (owner chose the build-in + tube screens, beam from the table):
    OPENING lasts 1 s: a beam from the nearest point of the table's rim to
    the fist (0-0.15 s), the device forms behind a plane rising along its
    up axis (uBuild/uBuildPlane: discard beyond it, glowing band at it;
    0.15-0.65 s), the beam fades, the screens turn on (uCrt: a line across,
    then it opens; staggered for the bar and side display; 0.65-1 s) with
    the chirp; the ping clock only runs once OPEN. CLOSING 0.8 s: screens
    fold to a line and a dot (0-0.25), dissolve downward (to 0.65), the beam
    retracts. Screens: uScanK 0.3 (scanlines; 0.12 elsewhere), uVignette 0.4.
    Buttons work 0.3 s after it is open. Test op `scannerfreeze <t>` holds
    the animation at t (-1 runs it again).
  - scanner.6 (owner's test of .5): the beam is gone (build-in 0-0.5 s, tubes
    on 0.5-0.85 s; closing: tubes off 0-0.25, dissolve to 0.65 s). The side
    radar is built from DETBORD2 + the ScannerView surface (cropRadar), not
    cut from the frame, so the game's cursor never shows. Sounds redone dark
    (owner: "cartoony"; wanted dread): synthScan's ping is a ~165 Hz boom
    with a detuned partner (beats) and a sub octave, blips are heartbeat
    double thumps 58-116 Hz (near: louder, a tritone-over-octave overtone,
    tanh grit), power on a low saw swell opening up with crackle, off a sub
    drop with static, buttons relay clunks; all through darkSpace (highs off,
    two filtered feedback echoes). Volumes ping 0.55, blips 0.5, UI 0.45.
    Green button = soldier view (owner's idea): camera at
    TileEngine::getSightOriginVoxel + 24 voxels per step (0-2; yellow up,
    blue down) via Board::voxelToWorld, heading from the commander camera,
    pitch raised by TILT (so a level hand looks level), level horizon, near
    plane 0.8 mm; Board::setHiddenUnit hides the soldier's own figure and
    renderScannerFeed turns the standees to the camera (setViewer). Side
    radar (owner: the wedge was ambiguous and didn't match the screen):
    always turned inside a circle; commander view: world -z (the table's far
    side) up, the camera's footprint on the soldier's floor outlined (corner
    rays, 30 tiles max) and an orange ring where the scanner is; soldier view:
    its heading up and the cone from the middle. It always opens in the
    commander view.
  - scanner.7: the soldier view's pitch is the scanner's own (held upright in
    front of you it looks level; owner), and it starts looking where the
    soldier faces: on switching, yawOffset = the soldier's facing (8
    directions x 45) - where the scanner points, added to the heading from
    then on (owner chose "start there, then follow your turns"); the compass
    and the side radar use that heading.
  - scanner.8: the sounds the owner picked over several rounds of previews
    (prototyped in Python, then ported to synthScan; the in-game WAVs match
    the previews to 0.1 dB): ping = a reversed low boom (60-100 Hz) then a
    muffled thump (it starts PING_LEAD = 0.25 s before each wave, so the
    thump leaves with the wave; pingK counts the waves, the first leaves as
    the tube is on); blips = a double beep at 1440 Hz +-2 semitones by
    distance (near up), gap 10 ms (near) .. 60 ms (far), 130 ms beeps that
    swell in over 60 ms, a three-tap echo (0.235/0.19/0.29 s), louder near
    (modelled on a reference clip the owner sent; only measured, nothing of it
    used); on = the low 50 Hz saw swell timed to the tube opening; off = the
    same hum closing (filter shutting, 62->42 Hz) from the fold's start. No
    button sounds for now (owner). Not run through an 8-bit/11 kHz "game
    palette" (tried; the owner prefers clean unless they clash). The zoom
    resets to 45 degrees each time it opens, and the soldier view always uses
    45 (Impl::fov()).
  - scanner.9: the ping was hard to hear on a headset (nearly all 60-100 Hz):
    tanh grit (x2.2) for overtones and a short noise knock on the thump,
    peak 0.85 (owner chose this over just louder). The power-on is audible
    from the instant the tube lights (owner: it seemed late; it started from
    silence): its envelope starts at 45% and a soft 70 Hz thump + crackle
    marks the catch.
  - scanner.10: the ping without the knock (owner preferred it; the grit
    stays). Soldier view leaning (owner: to peek past a corner): the head's
    position when the view starts is the reference (re-set by switching the
    view off and on); the head's horizontal offset, split across / along the
    view's heading, moves the eye by LEAN_MAX (0.33 tile) per LEAN_FULL (15 cm),
    capped at 0.33 tile from the soldier's centre (inside its own tile, short
    of the walls at its edges), smoothed; a haptic knock when it hits the cap.
    Fair: the commander view already shows the area from above.
  - scanner.11: the lean is measured along the player's own right / forward
    (headFwd, flattened), then applied along the view's: before, it was read
    along the view's axes, so with the soldier facing another way a lean to
    the right came out as a lean back (owner). Blips play on four channels in
    turn (AUDIO_SCANNER_BLIP .. +3): a further contact's blip no longer cuts
    off a nearer one's echo (owner).
  - scanner.12: the spacing between blips drifted (owner: two contacts' blips
    steady for a while, then closer). Each was started from the frame that
    saw the wave reach its contact, and SDL_mixer starts a sound only at its
    next buffer (1024 samples at 22050 Hz, about 46 ms), so the gap came out
    one buffer short or long depending on where each wave fell against the
    buffers, plus frame hitches. Now each wave is one sound (playWave): the
    ping with every contact's blip mixed in at PING_LEAD + dist / PING_SPEED
    (contacts reached within 15 ms share a blip, pitched by the nearest),
    started PING_LEAD before the wave and trimmed by how late its frame was,
    so it stays with the picture. Waves alternate between two channels
    (AUDIO_SCANNER_WAVE_A/B, replacing PING and BLIP..+3); a wave's chunk is
    freed when its channel comes round again (freeChunk); the playing wave
    follows the device round the head (moveSound) and fades out (80 ms) when
    the scanner closes. The haptic ticks stay per frame. `scannerwav` also
    writes the last wave as mixed (`wave.wav`). Checked with the owner's save
    (two contacts, 5.8 and 10.8 tiles) and SDL's disk audio driver
    (SDL_AUDIODRIVER=disk): the same gap in every wave of the mixer's output.
  - Test ops `usescanner` (ScannerState for the selected soldier, no time
    units), `scannerpress [red|blue|yellow|green]`, `scannerdump <png>` (the
    three screens); test `scan`.

