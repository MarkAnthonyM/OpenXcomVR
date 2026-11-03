#!/usr/bin/env python3
"""Hand-built models of the farm trees (CULTIVAT 9 and 12, fruit trees; 13, dead tree): 0.21.0-terrain.7.

Run after the preview's `dumpart CULTIVAT` (see voxlib.py); writes bin/common/Resources/VR/models/
CULTIVAT_<id>.vox (shapes and kinds only). Owner's choices: 32 model voxels across a tile, the ground
patch painted flat on the floor.

Design (per tree, from its sprite):
- The trunk stands where the art puts its foot: the roots are widest on sprite row 35, three table voxels
  in front of the tile's centre, at (11, 11) (the game's collision is a 3x3 column at the centre).
- Fruit trees: a crown of leaf clumps round an ellipsoid sized from the art's outline, then fitted to the
  art seen from the game's angle: voxels landing on empty pixels go (gaps you can see through), crown
  pixels nothing covers get a clump near the ellipsoid. Branch-coloured pixels inside the crown get wood
  near the trunk with the leaves in front of it taken away, and a branch back to the trunk. Apples where
  the art has them, and as many again on the far side, mirrored through the trunk.
- Dead tree: trunk and branches traced from the sprite by hand (screen polylines), each branch given a
  direction round the trunk so the tree spreads in 3D; twigs one model voxel thick (one sprite pixel).
"""
import math, os, random, sys
import numpy as np
from voxlib import *

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(REPO, 'bin', 'common', 'Resources', 'VR', 'models')
SX = SY = 72   # 36 table voxels: a tile and 10 on each side for crowns
SZ = 76        # up to table height 39.5 (shapes may rise 16 voxels into the level above)
AX = AZ = 11.0 # the trunk's axis (table units)
VIEW = np.array([1.0, 1.0, 1.0]) / math.sqrt(3) # towards the flat game's eye (X, Y, Z)


def disc_line(g, a, b, r, kind, overwrite=True):
    """Solid capsule from table point a to b (X, Y, Z) with radius r (table units)."""
    a = np.array(a, float); b = np.array(b, float)
    P = np.stack([g.X, g.Y, g.Z], -1)
    ab = b - a
    t = np.clip(((P - a) @ ab) / max(1e-9, ab @ ab), 0, 1)
    d = np.linalg.norm(P - (a + t[..., None] * ab), axis=-1)
    m = (d <= r) & (g.Y >= 2.0)
    # and every voxel the centre line passes through, so a thin branch never breaks up on the grid
    n = int(np.ceil(np.linalg.norm(ab) * 2 * 4)) + 1
    for t in np.linspace(0, 1, n):
        q = g.fine(*(a + t * ab)[[0, 2, 1]])
        if all(0 <= q[j] < (g.sx, g.sy, g.sz)[j] for j in range(3)) and q[2] >= 1: m[q] = True
    if overwrite: g.k[m] = kind
    else: g.k[m & (g.k == 0)] = kind


def trunk(g, top, r, flare):
    """A vertical trunk at the axis from the floor to height `top`, roots flaring over the bottom voxels."""
    rr = np.hypot(g.X - AX, g.Z - AZ)
    h = g.Y - 2.0
    rad = r + flare * np.clip(1 - h / 3.0, 0, 1) ** 2
    g.k[(rr <= rad) & (g.Y >= 2) & (g.Y <= top)] = WOOD


def paint_patch(g):
    """The ground patch: paint over the whole tile (the art covers its diamond)."""
    x = (np.arange(SX) + 0.5 - SX / 2) * 0.5 + 8
    y = (np.arange(SY) + 0.5 - SY / 2) * 0.5 + 8
    X, Z = np.meshgrid(x, y, indexing='ij')
    g.k[:, :, 0] = np.where((X >= 0) & (X < 16) & (Z >= 0) & (Z < 16), PATCH, 0)


def pixel_of(g, yoff):
    px, py = g.project(yoff)
    return np.floor(px).astype(int), np.floor(py).astype(int)


def fruit_tree(i, seed, crown_rows):
    pal = palette()
    idx, yoff = sprite('CULTIVAT', i)
    cl = classes(idx, pal)
    H, W = cl.shape
    # apples: red pixels and the dark shading pixel drawn beside each
    fruit = cl == FRUIT
    near_fruit = np.zeros_like(fruit)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            near_fruit |= np.roll(np.roll(fruit, dy, 0), dx, 1)
    rows = np.arange(H)[:, None] * np.ones((1, W), int)
    crown = (cl > 0) & (rows < crown_rows)
    wood_in = crown & (cl == WOOD) & ~near_fruit   # trunk and branches seen through the leaves
    ys, xs = np.nonzero(crown)
    xl, xr, yt, yb = xs.min(), xs.max(), ys.min(), ys.max()
    R = (xr - xl + 1) / 2 / math.sqrt(2)
    Hh = (yb - yt + 1) / 2
    Rv = math.sqrt(max(Hh * Hh - R * R / 2, (0.7 * R) ** 2))
    yc = (yt + yb + 1) / 2
    Yc = 24 + (AX + AZ) / 2 + yoff + 2 - yc
    print(f'CULTIVAT {i}: crown R {R:.1f} Rv {Rv:.1f} centre height {Yc:.1f}, bottom {Yc - Rv:.1f}')

    g = Grid(SX, SY, SZ)
    rnd = random.Random(seed)
    # crown: ellipsoid (a little smaller) with leaf clumps over its surface
    f = ((g.X - AX) / R) ** 2 + ((g.Y - Yc) / Rv) ** 2 + ((g.Z - AZ) / R) ** 2
    body = f <= 0.80
    for _ in range(140):
        u, v = rnd.uniform(-1, 1), rnd.uniform(0, 2 * math.pi)
        s = math.sqrt(1 - u * u)
        c = (AX + R * 0.86 * s * math.cos(v), Yc + Rv * 0.86 * u, AZ + R * 0.86 * s * math.sin(v))
        rc = rnd.uniform(1.6, 3.0)
        body |= (g.X - c[0]) ** 2 + (g.Y - c[1]) ** 2 + (g.Z - c[2]) ** 2 <= rc * rc
    body &= g.Y >= 2.5
    g.k[body] = LEAF
    # trunk up into the crown, roots flaring
    trunk(g, Yc, 1.15, 1.6)
    # fit to the art from the game's angle
    pix, piy = pixel_of(g, yoff)
    ok = (pix >= 0) & (pix < W) & (piy >= 0) & (piy < H)
    art = np.zeros(g.k.shape, int)
    art[ok] = cl[piy[ok], pix[ok]]
    in_crown = np.zeros(g.k.shape, bool)
    in_crown[ok] = crown[piy[ok], pix[ok]]
    leafy = (g.k == LEAF)
    g.k[leafy & ~in_crown] = 0                       # outline and gaps
    depth = g.X + g.Y + g.Z                          # larger = nearer the flat game's eye
    # crown pixels nothing covers: a clump where the ray passes nearest the ellipsoid's surface
    solid = g.k > 0
    covered = np.zeros((H, W), bool)
    covered[piy[solid & ok], pix[solid & ok]] = True
    for y, x in zip(*np.nonzero(crown & ~covered)):
        ray = ok & (pix == x) & (piy == y)
        if not ray.any(): continue
        cand = np.argwhere(ray)
        fv = f[ray]
        j = int(np.argmin(np.abs(fv - 0.85)))
        a = cand[j]
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1):
                    p = (min(a[0] + dx, SX - 1), min(a[1] + dy, SY - 1), min(a[2] + dz, SZ - 1))
                    if in_crown[p]: g.k[p] = LEAF
    # branches seen through the leaves: wood where the ray passes nearest the trunk, leaves before it go
    for y, x in zip(*np.nonzero(wood_in)):
        ray = ok & (pix == x) & (piy == y)
        if not ray.any(): continue
        cand = np.argwhere(ray)
        dist = np.hypot(g.X[ray] - AX, g.Z[ray] - AZ)
        j = int(np.argmin(dist))
        a = cand[j]
        X, Y, Z = g.X[tuple(a)], g.Y[tuple(a)], g.Z[tuple(a)]
        g.k[ray & (depth > depth[tuple(a)]) & (g.k == LEAF)] = 0
        g.k[tuple(a)] = WOOD
        # a branch back to the trunk, a little lower
        disc_line(g, (AX, max(2.5, Y - 3.0), AZ), (X, Y, Z), 0.45, WOOD, overwrite=False)
    # apples on the near side where the art has them, and as many again mirrored round the trunk
    solid = g.k > 0
    for y, x in zip(*np.nonzero(fruit & crown)):
        ray = ok & (pix == x) & (piy == y) & solid
        if not ray.any(): continue
        cand = np.argwhere(ray)
        a = cand[int(np.argmax(depth[ray]))]
        g.k[tuple(a)] = FRUIT
        X, Y, Z = g.X[tuple(a)], g.Y[tuple(a)], g.Z[tuple(a)]
        # mirrored: the outermost solid voxel along the line from the axis through the mirrored point
        mx, mz = 2 * AX - X, 2 * AZ - Z
        best = None
        for s in np.linspace(1.3, 0.5, 33):
            q = g.fine(AX + (mx - AX) * s, AZ + (mz - AZ) * s, Y)
            if all(0 <= q[n] < (SX, SY, SZ)[n] for n in range(3)) and g.k[q] == LEAF:
                best = q
                break
        if best: g.k[best] = FRUIT
    paint_patch(g)
    return g


# the dead tree: screen polylines (sprite pixels, base to tip) and a direction round the trunk (degrees:
# 0 east, 90 south, 180 west, 270 north; the flat game looks from the south-east)
DEAD_BRANCHES = [
    # left limb: up-left out of the fork towards the front left, then a twig straight up
    ([(15, 22), (13, 20), (11, 18), (10, 17), (9, 15), (9, 12)], 150, 0.9),
    # middle twig: up the trunk's left side, leaning back
    ([(16, 22), (15, 19), (15, 17), (16, 15), (16, 12)], 240, 0.6),
    # right limb: out to the right towards the back right, forking into two twigs
    ([(17, 22), (19, 21), (21, 20), (23, 19), (24, 18), (24, 15)], 330, 0.8),
    ([(24, 18), (25, 17), (26, 15)], 10, 0.45),
]


def dead_tree():
    pal = palette()
    idx, yoff = sprite('CULTIVAT', 13)
    g = Grid(SX, SY, SZ)
    fork = 37 - 22 + yoff   # table height of sprite row 22 at the axis: Y = 24 + X0 + 2 - py
    trunk(g, fork + 0.5, 1.05, 1.9)
    placed = {}   # sprite point -> table point, so a twig grows from where its limb passes
    for pts, az, r0 in DEAD_BRANCHES:
        th = math.radians(az)
        cx, sz_ = math.cos(th), math.sin(th)
        k = cx - sz_                 # px change per unit out along the branch
        bx, bz, bpx = AX, AZ, 16.0   # where it grows from: the trunk, or its parent limb
        if pts[0] in placed:
            bx, _, bz = placed[pts[0]]
            bpx = 16 + bx - bz
        prev = None
        d = 0.0
        n = len(pts)
        for j, (px, py) in enumerate(pts):
            if abs(k) > 0.2: d = min(6.0, max(d, (px + 0.5 - bpx) / k))  # limbs reach at most 6 out
            X, Z = bx + d * cx, bz + d * sz_
            Y = 24 + (X + Z) / 2 + yoff + 2 - (py + 0.5)
            cur = placed[(px, py)] if j == 0 and (px, py) in placed else (X, Y, Z)
            placed.setdefault((px, py), cur)
            if prev is not None:
                r = r0 + (0.3 - r0) * (j / (n - 1))
                disc_line(g, prev, cur, max(0.3, r), WOOD, overwrite=False)
            prev = cur
    paint_patch(g)
    return g


def main():
    os.makedirs(OUT, exist_ok=True)
    trees = {9: lambda: fruit_tree(9, 9, 28), 12: lambda: fruit_tree(12, 12, 28), 13: dead_tree}
    which = [int(a) for a in sys.argv[1:]] or list(trees)
    for i in which:
        g = trees[i]()
        path = os.path.join(OUT, f'CULTIVAT_{i}.vox')
        g.write_vox(path)
        print(f'wrote {path}: {int((g.k > 0).sum())} voxels')


if __name__ == '__main__':
    main()
