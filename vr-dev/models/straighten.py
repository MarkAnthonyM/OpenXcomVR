"""The art's texture laid onto a hand-built model, straightened (the texture study after terrain.18; owner's
choices in vr-dev/HANDOFF.md).

The art is drawn for one view: copied voxel by voxel, its pixel rows lie sheared (on side faces) or diagonal
(on top faces) across the model's faces, and the dithering turns into streaks and blotches. Straightened, a
face gets one colour per table voxel (2 x 2 model voxels), the art pixel at the cell's middle, on a cell grid
whose phase is chosen for the straightest rows. Where the art dithers two shades in a checkerboard, the cells
get a checkerboard of the same two shades on the face's own grid (the art's pixel size).

Only palette indices come out of here; the generators write them as references to pixels of that colour
(never the game's pictures)."""
import numpy as np
from voxlib import sprite

SX, SY, W, H = 330, 60, 560, 380          # the canvas: big enough for a 10 x 20 tile map block


def proj(X, Y, Z):
    """Map voxels -> canvas pixels, the game's view (px = 16 + X - Z, py = 24 + (X + Z) / 2 - Y)."""
    return 16 + X - Z + SX, 24 + (X + Z) / 2 - Y + SY


class Canvas:
    """The art of the parts keep() selects, put together as the map places them (the game's draw order)."""

    def __init__(self, set_, placed, keep):
        self.part = np.full((H, W), -1, int)
        self.col = np.zeros((H, W), int)
        for tx, ty, tz, slot, p in sorted(placed, key=lambda t: (t[2], t[0], t[1], t[3])):
            if not keep(tx, ty, tz, slot, p): continue
            g, yoff = sprite(set_, p)
            ox = (tx - ty) * 16 + SX
            oy = (tx + ty) * 8 - tz * 24 - yoff + SY
            ys, xs = np.nonzero(g)
            self.part[ys + oy, xs + ox] = p
            self.col[ys + oy, xs + ox] = g[ys, xs]
        self.ok = self.part >= 0
        self.mark, self.other = checkers(self.ok, self.col)

    def pixel(self, x, y, z, reach=3):
        """The drawn pixel at a point (the nearest drawn one within reach at the art's edges), or None."""
        px, py = proj(x, y, z)
        ix, iy = int(np.floor(px)), int(np.floor(py))
        for r in range(reach + 1):
            for dy in range(-r, r + 1):
                for dx in range(-r, r + 1):
                    if 0 <= iy + dy < H and 0 <= ix + dx < W and self.ok[iy + dy, ix + dx]: return iy + dy, ix + dx
        return None

    def value(self, q):
        """A pixel's texture value: its colour, or (lo, hi) for a pixel of a two-shade checkerboard."""
        c = int(self.col[q])
        if self.mark[q]:
            o = int(self.other[q])
            return (min(c, o), max(c, o))
        return c


def checkers(ok, col):
    """Pixels inside a 2 x 2 checkerboard of two shades of one ramp (a b / b a, at most 3 entries apart), and
    each one's other shade."""
    ramp = col // 16
    a, b = col[:-1, :-1], col[:-1, 1:]
    c, d = col[1:, :-1], col[1:, 1:]
    same = (ramp[:-1, :-1] == ramp[1:, 1:]) & (ramp[:-1, 1:] == ramp[:-1, :-1])
    allok = ok[:-1, :-1] & ok[1:, 1:] & ok[:-1, 1:] & ok[1:, :-1]
    chk = (a == d) & (b == c) & (a != b) & (np.abs(a - b) <= 3) & same & allok
    mark = np.zeros(col.shape, bool)
    other = col.copy()
    for dy in (0, 1):
        for dx in (0, 1):
            sl = (slice(dy, col.shape[0] - 1 + dy), slice(dx, col.shape[1] - 1 + dx))
            mark[sl] |= chk
            # the other shade: the horizontal neighbour inside the same block
            nb = col[dy:col.shape[0] - 1 + dy, (1 - dx):col.shape[1] - dx]
            other[sl] = np.where(chk, nb, other[sl])
    return mark, other


def resolve(v, parity):
    """A cell's palette index: a checkerboard pair resolves by the cell's parity on its face (even: lighter,
    the lower entry)."""
    if isinstance(v, tuple): return v[0] if parity % 2 == 0 else v[1]
    return v


def straightness(T):
    """How well cells line up: equal neighbours along both axes of a face grid (None = no cell)."""
    s = 0
    for ax in range(T.ndim):
        a = np.take(T, range(T.shape[ax] - 1), ax); b = np.take(T, range(1, T.shape[ax]), ax)
        s += sum(1 for u, v in zip(a.ravel(), b.ravel()) if u is not None and u == v)
    return s
