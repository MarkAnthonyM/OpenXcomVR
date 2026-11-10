# Texturing hand-built models from the art: what made the Skyranger's fins and wings work

Written after terrain.24, at the owner's request, before the canopy study. The fins and wings are the best-looking
parts of any hand-built model so far. This file explains why and turns it into a recipe for the next parts and
models. `HANDOFF.md`'s Decisions 10-12 are the short form; the history of each choice is in its build notes there.
The code is in `vr-dev/models/straighten.py` and `skyranger.py` (`tex_wings`, `tex_fins`, `tex_panels`, `tex_fuselage`).

## Why the fins and wings look good

1. **Flat faces lying square to the voxel grid.** A fin is a plate across X and the wing's top is a flat plate at
   one height. Every voxel of a face sits in one plane, so there are no staircase steps. Steps are what make the
   "cloth" or knitting rows: each step has a lit tread and a darker riser. Faces sloped at a clean angle (1:1, 1:2,
   2:1) step evenly; other angles step irregularly. Curved shapes always step.
2. **The art's own texture, straightened onto the face.** Each face is divided into cells of one table voxel
   (2 x 2 model voxels). One table voxel is the size of one art pixel, so the art keeps its own pixel size (never
   Scale2x). Each cell takes the one art pixel that shows its middle from the game's angle. The cell grid is
   shifted by 0 or half a voxel along each axis, and the shift with the straightest rows wins (the most equal
   neighbours). The result is clean pixel art lying flat on the face, not a sheared copy of the sprite.
3. **The dither redrawn, not copied.** Where the art checkers two neighbouring shades (a 2 x 2 "a b / b a" of one
   ramp, at most 3 entries apart), the cell gets a checkerboard of the same two shades on the face's own grid:
   the lighter one on cells with an even (u + v). Copied pixel by pixel, the art's checkerboard lies diagonally on
   a top face and turns into streaks; straightened as drawn, it turns into uneven blotches.
4. **Seams cleaned up; markings left alone.** Panel lines (seams) are made solid, straight and defined, even
   where the art draws them broken. Painted markings (the fin's stripe, the "V") stay exactly as drawn.
5. **The craft's symmetry used as a filter.** The art is lit from the left, so its two wings differ. A feature
   that both mirrored wings show is real (a seam). One that only one wing shows comes from the light (a
   shadow's edge, a stray mark), and it goes.
6. **The paint's own shading kept, cast shadows taken out.** The art's light-to-dark gradient across the wing
   (tip light, root dark) is the wing's tone: kept. The fuselage's shadow painted onto the wing's root belongs to
   another part: replaced by the wing's gradient, carried on to the root. The table's sun and sky add the real
   light and shadow.
7. **Each flat panel lit evenly** by its own direction (`.ref` VRF3, terrain.22). A plate reads as a plate with
   crisp edges, not the smooth light the owner rejected.

## The recipe for a new part

1. **Shape first.** Build the part from clean flat panels: square to the grid where possible, otherwise at 1:1,
   1:2 or 2:1. Fit the shape and its neighbours against the art's outline from the game's angle before any
   texture (Decision 11). A texture can't fix steps.
2. **Collect the art.** Use a `Canvas` with only the parts that draw this surface, put together in the game's
   draw order. Then a point on the model maps to its pixel through `proj()` (px = 16 + X - Z,
   py = 24 + (X + Z)/2 - Y).
3. **Pick the face's grid.** Use two axes in the face's plane: (X, Z) for a top, (Z, Y) for a fin, (u across,
   v up) for a sloped panel (`tex_panels`). Use cells of one table voxel, and try the half-voxel shifts
   (`plane_cells`, `straightness`).
4. **Sample only what's really this surface.** Take the pixel at each cell's middle only where the game's view
   shows that very point (`seen_at`: the nearest cell drawn there must be the point's own). Then:
   - Accept only this surface's colour family: the hull ramp 240-255 for hull (`hull_grey`), the blues for
     glass, the yellows for the beak.
   - Anything else is something seen through or drawn over it (glass, consoles, outlines, highlights). Those
     cells take their nearest accepted neighbour.
   - Faces the art never shows take the mirrored face's cells (the fins: the art shows the left fin's inner
     face and the right fin's outer face).
5. **Resolve the dither.** Each checkerboard pair becomes the two-shade checker by cell parity (`checkers`,
   `resolve`).
6. **Clean up, by rule, not by hand:**
   - Seams: find the broken dark lines (`find_lines`: greys >= 248 running 6+ cells, dark on 45%+ of them).
     Join only those both mirrored halves have, over the stretch both reach, in the subtler (lighter) of their
     two greys.
   - Dark marks only one side has: back to the local gradient (the column's mean).
   - Cast shadows painted on (12 voxels from the hull on the wings): back to the gradient, extended with a line
     fitted to the ten columns next to the shadow.
   - Lone cells far off their neighbours: a cell 4+ entries lighter than all of them (the hull), or 2+ off its
     3 x 3 middle on a noisy panel (`even`, the nose), takes their middle.
   - Outlines the art draws round a feature (the pale ring round the roundel) aren't the surface: they take
     neighbours' cells.
7. **Clean areas get flat colour.** A marking with a clean edge on the model (the red wing tip, the roundel)
   takes area colour, so its edge follows the shape exactly.
8. **Panels.** Give every flat panel one lighting direction (`panels()`), so each plate is shaded evenly.

## What was tried and dropped

- Copying the art voxel by voxel: its pixel rows lie sheared across side faces and diagonal on top faces, giving
  sawtooth lines and streaks.
- The dither at half the art's pixel size; no dither (about four flat bands of grey instead of a gradient).
- A redrawn fin stripe: its red pixels are the art's anti-aliasing of a 2:1 line; kept as drawn.
- Lighting by the shape's smooth direction (removes the cloth look, but the owner rejected the soft look).
- Area colours with a speckle (Decision 3) on the scout's and the Skyranger's hulls: the owner found them
  cloth-like. That was mostly the steps' lighting, but he liked the art's own texture better, and it replaced
  the speckle on the Skyranger. Decision 3 still holds for small, clean features and markings.
- A 16-sided small scout: still stepped irregularly; an octagon was crisp. Use fewer, cleaner panels.

## Where it gets harder (why the hull, nose and canopy lag behind the fins and wings)

- **Curved or tapering shapes.** One constant section can be unrolled round the craft (`tex_fuselage`).
  A shape that changes along its length has to be textured panel by panel (`tex_panels`, terrain.23).
  Each panel is small, so it gets few art pixels.
- **Thin strips between panels.** A sliver of panel facing a different way from its neighbours reads as a line
  under even panel lighting. Seen in terrain.24: the vertical strip at the wings' height along the hull (fixed
  in terrain.25 by lighting it as the slope below). A strip that exists only to meet another part should share a
  neighbour's lighting direction.
- **Small parts the art draws in a few pixels.** The art there is mostly outline, highlight and anti-aliasing,
  not texture. The fewer pixels a surface gets, the more the rules for accepting colours decide what it looks
  like.
- **Painted light that isn't the surface's own.** Edge highlights (the cowl's white edge became a white
  patch) and reflections. Decide case by case with the owner: the canopy's glass highlight was kept at his
  choice.

## What the canopy and nose study added (terrain.25)

- **Faces that step one way vs two ways.** A face parallel to one voxel axis (a top sloping only along Z, an
  upright side, a flat front) steps in one direction: its texture pixels stay square and crisp from any angle. A
  facet that tapers (narrows along the length while sloping across) steps in two directions, and from the game's
  diagonal view each texture pixel reads as a hexagon (a honeycomb), however the texture is drawn. When the art's
  shape is rounder than one-way faces allow, the owner prefers crisp pixels over the closer outline (the
  Skyranger's nose: outline 96.2% -> 95.3%, accepted).
- **Building such a shape:** take the body's section as a prism and cut it by half-space planes, each parallel
  to one axis (`nose_planes`). The profiles along the length must be concave and start at the body's own values;
  otherwise the extended cut planes slice into the body behind (a fit that scored better by doing that was
  invalid). Check the result for voxels drawn by two pieces where it meets other parts (the front wings).
- **Texture by rule where the art is thin or ambiguous.** On the nose and the glass the per-panel copy left
  blotches: cell grids at odd angles, 25-50% of cells with no art seen (filled by nearest), and the art's
  scattered dither. Instead (`tex_rule`): cells on the voxel grid by the face's facing, each tone the Gaussian
  mean (sigma 1.5 cells) of the art samples around it, and a checker of the two neighbouring entries between
  steps. The glass the same way per table-voxel block (2 x 2 x 2): steps by distance from the frame and from the
  hull's edge, half steps a checker; the art's highlight spots kept.
- **Frames by distance along the surface.** A uniform frame (2 cells, one table voxel) is a band found by a
  surface walk from the edges, not a shape of its own; bars between panes split evenly over the seam.
- **The flush trap.** Fitting a raised part (the canopy) freely to the art's outline pulls it down flat onto the
  hull: the score improves while the part disappears. Fix its shape by what the art plainly shows (its height at
  the back, the windscreen's rake) and fit only the rest.
- **One view can't fix depth.** The game's projection is blind along (1, 1, 1), so a shape that matches the
  outline can still be wrong in 3D. Show the owner other angles before settling a shape.

## Before sending a textured part

- Render it from several sides, panel-lit and unlit, and compare it with the art from the game's angle
  (Decision 7).
- Look along seams and at the part's edges, where it meets its neighbours: lines and bars show up there.
- Check that no voxel is drawn by two placed pieces (Decision 12). Offline renders can't show that, so count it.
- Say plainly that it hasn't been seen in the headset.
