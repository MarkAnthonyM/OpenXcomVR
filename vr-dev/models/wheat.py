#!/usr/bin/env python3
"""Hand-built models of the wheat (CULTIVAT 4): 0.21.0-terrain.13.

Writes bin/common/Resources/VR/models/CULTIVAT_4.vox, CULTIVAT_4-2.vox and CULTIVAT_4-3.vox (three versions;
the table picks one per tile by its position, so a field doesn't repeat one tile). Shapes only: every
voxel is "plain" (kind 5: whatever the sprite draws where it lands; the wheat's darker browns would count
as fruit, red), so the table colours it from the wheat's own sprite, dark at the foot and golden at the
ears, as the art draws it.

Owner's choices: dense like the art (from the game's angle the field reads as solid wheat; its sides and
top are stalks and ears, not walls), up to a soldier's waist with the ears sticking out above
(terrain.3/4 notes), three versions.

Design: an upright stalk on more than half of the tile's table voxels, one table voxel thick (a sprite
pixel: the art's stripes), its foot shifted by up to half a voxel; an ear on top, nudged half a voxel to
one side, often with a thin tip. Everything stays inside the tile. (A first try with thin leaning stalks,
bent ears and blades made 9000 faces a tile, and every wheat tile of the map carries its model's faces.)
"""
import math, os, random, sys
import numpy as np
from voxlib import *

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PER = 2
OUT = os.path.join(REPO, 'bin', 'common', 'Resources', 'VR', 'models')
SX = SY = 20 * PER     # the tile and 2 table voxels round it
SZ = 18 * PER + 1      # up to table height 18

TOP = (8.0, 12.0)     # stalk tops (table units): the body of the field, at a soldier's waist (tall 11)
EAR = (2.0, 3.5)       # ear length above the stalk
FILL = 0.58            # share of the tile's table voxels with a stalk


def column(g, x, y, z0, z1, w=2):
    """A w x w model-voxel column from model layer z0 up to z1 (inclusive), feet at model (x, y)."""
    for zz in range(max(1, z0), min(g.sz - 1, z1) + 1):
        g.k[max(0, x):min(g.sx, x + w), max(0, y):min(g.sy, y + w), zz] = PLAIN


def wheat(seed):
    """Stalks one table voxel (one sprite pixel, the art's stripes) thick and upright: a model's every face
    is copied onto each wheat tile of the map, so the field is made of few, long faces (thin leaning stalks
    break into many: 9000 faces a tile at first)."""
    rnd = random.Random(seed)
    g = Grid(SX, SY, SZ, PER)
    o = (SX - 16 * PER) // 2   # the tile's first model voxel
    stalks = 0
    for ty in range(16):
        for tx in range(16):
            if rnd.random() > FILL: continue
            # foot: this table voxel, shifted up to half a voxel (so the stripes don't sit on a grid)
            x = o + tx * PER + rnd.randint(0, 1) - (1 if tx == 15 else 0)
            y = o + ty * PER + rnd.randint(0, 1) - (1 if ty == 15 else 0)
            top = int(rnd.uniform(*TOP) * PER)
            column(g, x, y, 1, top)
            # the ear: on up, nudged a model voxel to one side (a slight bend), often with a thin tip
            el = int(rnd.uniform(*EAR) * PER)
            dx, dy = rnd.choice(((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)))
            column(g, x + dx, y + dy, top + 1, top + el)
            if rnd.random() < 0.6:
                column(g, x + dx + rnd.randint(0, 1), y + dy + rnd.randint(0, 1), top + el + 1, top + el + rnd.randint(1, 2), 1)
            stalks += 1
    print(f'wheat seed {seed}: {stalks} stalks, {int((g.k > 0).sum())} voxels')
    return g


def main():
    os.makedirs(OUT, exist_ok=True)
    for v, seed in enumerate((41, 42, 43)):
        g = wheat(seed)
        path = os.path.join(OUT, 'CULTIVAT_4.vox' if v == 0 else f'CULTIVAT_4-{v + 1}.vox')
        g.write_vox(path)
        print(f'wrote {path}')


if __name__ == '__main__':
    main()
