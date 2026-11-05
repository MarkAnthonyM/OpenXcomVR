"""Helpers for the hand-built terrain models (see src/VR/VrModels.h).

Models are shapes only: each voxel is a kind (1 wood, 2 leaves, 3 fruit, 4 ground patch), never a colour
from the game. The table colours them from the player's own sprites. The sprites used to design a model
come from the preview's `dumpart SET` test op ($VR_RUN/shots/art/), which needs the player's X-COM data:
they are read here, never written into the repo.

Frames: model voxel (x, y, z), x east, y south, z up, two per table voxel, the model's x/y centre on the
tile's centre; z = 0 is paint on the floor, z >= 1 solid from the floor's top (table height 0 since
0.21.0-terrain.9, where the art's height 0 is too).
Sprite pixel of a point (table units X east, Y up, Z south): px = 16 + X - Z, py = 24 + (X + Z) / 2 - Y + yoff.
"""
import os, struct
import numpy as np
from PIL import Image

WOOD, LEAF, FRUIT, PATCH = 1, 2, 3, 4
# preview colours for MagicaVoxel (the table never uses them)
PREVIEW = {WOOD: (110, 74, 40), LEAF: (60, 140, 50), FRUIT: (200, 40, 30), PATCH: (120, 170, 70)}

ART = os.path.join(os.environ.get('VRDEV', os.path.expanduser('~/vrdev')), 'run', 'shots', 'art')


def palette():
    return [tuple(map(int, l.split())) for l in open(os.path.join(ART, 'palette.txt'))]


def sprite(set_, i):
    """Palette indices (rows, cols) and the part's y offset."""
    idx = np.array(Image.open(os.path.join(ART, f'{set_}_{i}.png')).convert('L')).astype(np.int32)
    yoff = 0
    for l in open(os.path.join(ART, f'{set_}.txt')):
        f = l.split()
        if int(f[0]) == i: yoff = int(f[3])
    return idx, yoff


def classes(idx, pal):
    """Per pixel: 0 empty, LEAF (green), FRUIT (red), WOOD (anything else); same rule as the table."""
    h, w = idx.shape
    out = np.zeros((h, w), np.int32)
    for y in range(h):
        for x in range(w):
            c = idx[y, x]
            if not c: continue
            r, g, b = pal[c]
            if g > r * 1.15 and g >= b: out[y, x] = LEAF
            elif r > g * 1.6 and r > 90: out[y, x] = FRUIT
            else: out[y, x] = WOOD
    return out


class Grid:
    def __init__(self, sx, sy, sz):
        self.sx, self.sy, self.sz = sx, sy, sz
        self.k = np.zeros((sx, sy, sz), np.uint8)
        # centres of every voxel in table units
        x = (np.arange(sx) + 0.5 - sx / 2) * 0.5 + 8
        y = (np.arange(sy) + 0.5 - sy / 2) * 0.5 + 8
        z = (np.arange(sz) - 0.5) * 0.5       # z = 1 -> 0.25
        self.X, self.Z, self.Y = np.meshgrid(x, y, z, indexing='ij')

    def project(self, yoff=0):
        px = 16 + self.X - self.Z
        py = 24 + (self.X + self.Z) / 2 - self.Y + yoff
        return px, py

    def fine(self, X, Z, Y):
        """Model voxel of a table point."""
        return (int(np.floor((X - 8) * 2 + self.sx / 2)), int(np.floor((Z - 8) * 2 + self.sy / 2)), int(np.floor(Y * 2 + 1)))

    def write_vox(self, path):
        pts = np.argwhere(self.k > 0)
        body = b''
        size = struct.pack('<4sii3i', b'SIZE', 12, 0, self.sx, self.sy, self.sz)
        xyzi = struct.pack('<4siii', b'XYZI', 4 + 4 * len(pts), 0, len(pts))
        xyzi += b''.join(struct.pack('<4B', int(x), int(y), int(z), int(self.k[x, y, z])) for x, y, z in pts)
        rgba = bytearray()
        for i in range(1, 257):
            c = PREVIEW.get(i, (128, 128, 128))
            rgba += bytes((c[0], c[1], c[2], 255))
        rgba = struct.pack('<4sii', b'RGBA', 1024, 0) + bytes(rgba)
        body = size + xyzi + rgba
        main = struct.pack('<4sii', b'MAIN', 0, len(body))
        with open(path, 'wb') as f:
            f.write(b'VOX ' + struct.pack('<i', 150) + main + body)
