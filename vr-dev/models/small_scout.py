#!/usr/bin/env python3
"""Hand-built model of the small scout (UFO1, map block UFO1A): 0.21.0-terrain.13.

Run after the preview's `dumpart UFO1` (see voxlib.py). Writes bin/common/Resources/VR/models/UFO1_<id>.vox
for the hull pieces (0-7, level 1), the cap (10, level 2) and the legs (16-19, level 0), and UFO1_<id>.ref
beside the hull pieces and the cap: where each voxel takes its colour (see src/VR/VrModels.h). Shapes and
pixel references only, never the game's pictures.

Owner's choices: a thin shell like the art (the inside as roomy as the art draws it; units still stand
only on the centre tile), 32 model voxels across a tile.

Design:
- The whole saucer is one solid of revolution round the centre tile's middle, fitted to the art put
  together as the map places the pieces (`--fit` redoes it: 98% of the outline shared). Profile, in table
  units above level 1's floor: the rim (radius 23) 4 up, the dome to 23 (the cap, a floor on level 2,
  sits on it), the underside curving in to radius 18 at the bottom (cut flat at the floor, 0).
- A shell one table voxel thick; inside it a floor (two half-voxel layers: the hull's underside, then the
  inside floor). It is cut along the tiles into the eight ring pieces; the dome over the centre tile goes
  to the cap (level 2), lowered to stand 8 table voxels below that level's floor (Model::baseY). The centre
  tile itself keeps the game's floor (part 9, the lift). When the game destroys a piece, its slice goes.
- Colours (terrain.16, owner): even colours per area from the art's palette (see colours()), no painted
  shading: the table's sun and sky shade it. Lights (".ref" version 2): the rim's 20 lights in two sets
  that take turns with the game's terrain frames, the cap's light pulsing, the lift (part 9) flipping;
  they glow on their own surface only.
- Legs: a strut from under the hull (14 out from the axis, 25 up) to a foot pad on the ground (20 out),
  as the leg sprites put them; thicker above (the piston), coloured from their own sprites as usual.
"""
import math, os, struct, sys
import numpy as np
from voxlib import *

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(REPO, 'bin', 'common', 'Resources', 'VR', 'models')
MAP = os.path.join(os.environ.get('VRDEV', os.path.expanduser('~/vrdev')), 'run', 'data', 'UFO', 'MAPS', 'UFO1A.MAP')
PER = 2
SET = 'UFO1'
OFFSET = 2          # map bytes -> UFO1 part ids: the terrain's first set (BLANKS) comes first
AX = AZ = 72.0      # the saucer's axis (map voxels): the middle of tile (4, 4)
PROFILE = dict(R=23.0, rim=3.5, top=21.0, bottom=-1.0, rb=18.0, e=1.4, rc=4.5)   # from --fit (terrain.16: flat top)
CAP_BASE = -8.0     # the cap model stands this far below level 2's floor (level 1 height 16)
SHELL = 2           # model voxels


def radius(h, R, rim, top, bottom, rb, e, rc=0.0):
    """The saucer's radius at height h above level 1's floor (0 outside it). The dome rises from the rim to
    a flat top of radius rc (terrain.16; the cap's light sits on it: the owner saw a cone)."""
    h = np.asarray(h, float)
    r = np.zeros_like(h)
    lo = (h >= bottom) & (h < rim)
    t = (h[lo] - bottom) / (rim - bottom)
    r[lo] = rb + (R - rb) * np.sqrt(t)
    up = (h >= rim) & (h <= top)
    t = (h[up] - rim) / (top - rim)
    r[up] = rc + (R - rc) * np.clip(1 - t ** e, 0, 1) ** (1 / e)
    return r


# ---------------------------------------------------------------- the art, put together as the map does

def map_parts():
    b = open(MAP, 'rb').read()
    ry, cx, hz = b[0], b[1], b[2]
    t = b[3:]
    out = []   # (tx, ty, tz, slot, part) in the order the game draws them
    for tz in range(hz):
        fz = hz - 1 - tz
        for ty in range(ry):
            for tx in range(cx):
                o = ((fz * ry + ty) * cx + tx) * 4
                for slot in range(4):
                    if t[o + slot] >= OFFSET: out.append((tx, ty, tz, slot, t[o + slot] - OFFSET))
    return out


CW, CH = 200, 200   # canvas: pixel (16 + X - Z + SHIFT_X, 24 + (X + Z) / 2 - Y + SHIFT_Y), map voxels
SHIFT_X, SHIFT_Y = 40, 20          # puts tiles 3..5 into the canvas


def project(X, Y, Z):
    return 16 + X - Z + SHIFT_X, 24 + (X + Z) / 2 - Y + SHIFT_Y


def composite(placed, keep):
    """Per canvas pixel: the part drawn last there and its sprite pixel (-1 where nothing)."""
    part = np.full((CH, CW), -1, int)
    loc = np.zeros((CH, CW, 2), int)
    for tx, ty, tz, slot, p in placed:
        if not keep(tx, ty, tz, p): continue
        g, yoff = sprite(SET, p)
        ox = (tx - ty) * 16 + SHIFT_X
        oy = (tx + ty) * 8 - tz * 24 - yoff + SHIFT_Y
        ys, xs = np.nonzero(g)
        gy, gx = ys + oy, xs + ox
        ok = (gx >= 0) & (gx < CW) & (gy >= 0) & (gy < CH)
        part[gy[ok], gx[ok]] = p
        loc[gy[ok], gx[ok], 0] = xs[ok]
        loc[gy[ok], gx[ok], 1] = ys[ok]
    return part, loc


def lookup(comp, px, py, reach=6):
    """(part, column, row) of the sprite pixel at canvas (px, py), else the nearest within reach."""
    part, loc = comp
    x0, y0 = int(math.floor(px)), int(math.floor(py))
    for r in range(reach + 1):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if max(abs(dx), abs(dy)) != r: continue
                x, y = x0 + dx, y0 + dy
                if 0 <= x < CW and 0 <= y < CH and part[y, x] >= 0:
                    return int(part[y, x]), int(loc[y, x, 0]), int(loc[y, x, 1])
    return None


# ---------------------------------------------------------------- the fit

def fit(placed):
    hull, _ = composite(placed, lambda tx, ty, tz, p: tz == 1 and p != 9 or tz == 2)
    art = hull >= 0
    st = 0.5
    g = np.arange(-26, 26, st) + st / 2
    gh = np.arange(-4, 30, st) + st / 2
    X, Z, H = np.meshgrid(g, g, gh, indexing='ij')
    R2 = np.hypot(X, Z)
    px, py = project(X + AX, 24 + H, Z + AZ)
    px, py = np.floor(px).astype(int), np.floor(py).astype(int)
    ok0 = (px >= 0) & (px < CW) & (py >= 0) & (py < CH)
    best = (0, None)
    for R in (22.5, 23, 23.5):
        for rim in (2.5, 3, 3.5, 4):
            for top in (19, 20, 20.5, 21, 21.5, 22):
                for bottom in (-1,):
                    for rb in (17, 18):
                        for e in (1.0, 1.2, 1.4, 1.7, 2.0):
                            for rc in (4, 4.5, 5, 5.5, 6, 7):
                                p = dict(R=R, rim=rim, top=top, bottom=bottom, rb=rb, e=e, rc=rc)
                                m = np.zeros_like(art)
                                sol = (R2 <= radius(H, **p)) & ok0
                                m[py[sol], px[sol]] = True
                                sc = (m & art).sum() / max(1, (m | art).sum())
                                if sc > best[0]: best = (sc, p)
    print('fit:', best)


# ---------------------------------------------------------------- the hull

def hull():
    """The whole saucer on one model grid over tiles 3..5 (x, y) and level 1 (z 1.. = 0.. table units up):
    kind grid and per-voxel role (1 outer, 2 inner, 3 underside, 4 inside floor)."""
    n = 3 * 16 * PER
    sz = 24 * PER + 1
    i = np.arange(n)
    X = 48 + (i + 0.5) / PER
    k = np.arange(sz)
    Hh = (k - 0.5) / PER
    XX, ZZ, HH = np.meshgrid(X, X, Hh, indexing='ij')
    rr = np.hypot(XX - AX, ZZ - AZ)
    outer = (rr <= radius(HH, **PROFILE)) & (HH >= 0)
    outer[:, :, 0] = False
    # the cavity: the solid eroded by the shell's thickness, from the inside floor's top up
    cav = outer.copy()
    for _ in range(SHELL):
        c = cav.copy()
        for ax in range(3):
            for s in (1, -1):
                sh = np.roll(cav, s, axis=ax)
                idx = [slice(None)] * 3
                idx[ax] = 0 if s == 1 else -1
                sh[tuple(idx)] = False
                c &= sh
        cav = c
    cav[:, :, :3] = False
    solid = outer & ~cav
    # roles
    def touches(mask):
        t = np.zeros_like(mask)
        for ax in range(3):
            for s in (1, -1):
                sh = np.roll(mask, s, axis=ax)
                idx = [slice(None)] * 3
                idx[ax] = 0 if s == 1 else -1
                sh[tuple(idx)] = False
                t |= sh
        return t
    outside = ~outer
    outside[:, :, 0] = True
    role = np.zeros(solid.shape, np.uint8)
    role[solid & touches(cav)] = 2
    role[solid & touches(outside)] = 1                 # outer wins where both
    role[solid & (role == 0)] = 1
    # the bottom two layers inside the rim: underside (z 1) and the inside floor (z 2)
    under = solid & (np.arange(sz)[None, None, :] == 1)
    floor = solid & (np.arange(sz)[None, None, :] == 2) & ~touches(outside)
    role[under & (rr < radius(np.full_like(HH, 0.25), **PROFILE) - 1)] = 3
    role[floor] = 4
    print(f'hull: {int(solid.sum())} voxels (outer {int((role == 1).sum())}, inner {int((role == 2).sum())}, '
          f'underside {int((role == 3).sum())}, floor {int((role == 4).sum())})')
    return solid, role, XX, ZZ, HH


# ---------------------------------------------------------------- colours (terrain.16)
# Each area of the saucer takes even colours from the art's own palette (by reference to a pixel of that
# colour), not the pixel it lands on: the owner wanted clean edges on the rim and the cap, and no painted
# shading (the table's sun and sky shade it). Measured from the art mapped onto the shape: the blue rim
# band 1.5-4.5 up (radius 21.7+), white rim lights about 3.3 up, the purple light on the flat top (radius 5),
# a lighter grey collar round it, the "!!" marks on the upper slope (rows 81-85 of the assembled picture:
# two bars 2 pixels wide and 3 tall, a gap, a dot), four of them, one each way (owner).
AREAS = {
    'hull': [(244, 50), (243, 25), (245, 25)],        # palette index, share: an even, fine speckle
    'rim': [(115, 70), (114, 30)],
    'collar': [(241, 70), (240, 30)],
    'light_mid': [(193, 100)], 'light': [(194, 100)],
    'mark': [(5, 100)],                               # light grey: the art's bars are its lightest greys there
    'inside': [(227, 70), (228, 30)],
    'floor': [(198, 70), (197, 30)],
}
RIM_LIGHT = 224          # the rim lights' colour
RIM_BAND = (1.0, 5.0, 20.8)       # height from, to; from radius
RIM_LIGHTS = 20          # evenly spaced, alternate ones in the two sets (the art: two interleaved sets)
RIM_LIGHT_H = (2.5, 4.1) # height of a light
RIM_LIGHT_W = 0.55       # half its width along the rim (table units)
LIGHT_R, COLLAR_R = 5.0, 7.0
MARK_AZ = (45, 135, 225, 315)   # degrees from east toward south: the four diagonals (the art shows three)
MARK_BARS = ((-2.15, -0.75), (0.75, 2.15))   # along the rim's direction, from the mark's middle
MARK_R = ((10.6, 12.6), (13.3, 14.0))        # bar, dot: how far out from the axis (down the slope)


def pixel_with(index, cache={}):
    """(part, column, row) of a pixel of this palette colour in the set's sprites: colours by reference."""
    if index not in cache:
        for p in range(32):
            try: g, _ = sprite(SET, p)
            except Exception: continue
            ys, xs = np.nonzero(g == index)
            if len(xs):
                cache[index] = (p, int(xs[0]), int(ys[0]))
                break
        else: raise SystemExit(f'colour {index} is in no {SET} sprite')
    return cache[index]


def speckle(area, i, j, k):
    """One of the area's colours, the same for a voxel every time (a hash of its place)."""
    h = (i * 73856093 ^ j * 19349663 ^ k * 83492791) & 0xFFFF
    x = h % 100
    for index, share in AREAS[area]:
        if x < share: return pixel_with(index)
        x -= share
    return pixel_with(AREAS[area][0][0])


def colours(solid, role, XX, ZZ, HH):
    """Per solid voxel: (part, column, row) of its colour and its light (VrModels.h Model::light)."""
    ref, light = {}, {}
    pr = PROFILE
    count = {}
    for (i, j, k) in zip(*np.nonzero(solid)):
        X, Z, H = XX[i, j, k], ZZ[i, j, k], HH[i, j, k]
        r = math.hypot(X - AX, Z - AZ)
        az = math.degrees(math.atan2(Z - AZ, X - AX)) % 360
        ro = role[i, j, k]
        lt = 0
        if ro == 2: area = 'inside'
        elif ro == 4: area = 'floor'
        elif ro == 3: area = 'hull'
        elif H >= pr['top'] - 0.6 and r <= LIGHT_R:
            area = 'light_mid' if r <= 2.0 else 'light'
            lt = 3                                   # pulses
        elif H >= pr['top'] - 3.5 and r <= COLLAR_R: area = 'collar'
        elif RIM_BAND[0] <= H <= RIM_BAND[1] and r >= RIM_BAND[2]:
            area = 'rim'
            if RIM_LIGHT_H[0] <= H <= RIM_LIGHT_H[1]:
                step = 360 / RIM_LIGHTS
                n = round((az - 45) / step)
                d = abs(((az - 45) - n * step + 180) % 360 - 180)
                if math.radians(d) * r <= RIM_LIGHT_W: lt = 1 if n % 2 == 0 else 2
        else:
            area = 'hull'
            for m in MARK_AZ:
                s_ = math.radians(((az - m + 180) % 360) - 180) * r
                if any(a <= s_ <= b for a, b in MARK_BARS) and any(a <= r <= b for a, b in MARK_R):
                    area = 'mark'
        ref[(i, j, k)] = speckle(area, i, j, k)
        light[(i, j, k)] = lt
        count[area] = count.get(area, 0) + 1
    print('areas:', count, 'lights:', sum(1 for v in light.values() if v in (1, 2)))
    return ref, light


def write_piece(path, kgrid, refgrid, base, lightgrid=None):
    """A model and its .ref (version 2: with lights): refs in the table's order (z, then y, then x) over
    solid voxels."""
    g = Grid(kgrid.shape[0], kgrid.shape[1], kgrid.shape[2], PER)
    g.k = kgrid
    g.write_vox(path + '.vox')
    sx, sy, sz = kgrid.shape
    lc = pixel_with(RIM_LIGHT)
    out = bytearray(b'VRF2' + struct.pack('<f', base))
    body = bytearray()
    count = 0
    for z in range(sz):
        for y in range(sy):
            for x in range(sx):
                if not kgrid[x, y, z]: continue
                r = refgrid.get((x, y, z))
                lt = (lightgrid or {}).get((x, y, z), 0)
                body += bytes((255, 0, 0, lt)) if r is None else bytes((r[0], r[1], r[2], lt))
                count += 1
    out += struct.pack('<I', count) + bytes((lc[0], lc[1], lc[2], 0)) + body
    open(path + '.ref', 'wb').write(bytes(out))
    print(f'wrote {path}.vox ({count} voxels) and .ref')


# ---------------------------------------------------------------- the legs

LEG_TOP = (14.1, 25.0)   # out from the axis, up (level 0's floor)
LEG_FOOT = (20.3, 0.0)
# the leg's shape (table units), tuned against the four leg sprites by tune_legs() (--legs prints the search)
LEG = dict(top=23.0, piston_r=1.2, piston=0.45, strut_r=0.55, pad_r=3.4, pad_h=0.9, cone_r=1.4)   # pad_h 0.7 matched 20 more pixels over the four legs but leaves a pad one model voxel thin


def leg(tx, ty, shape=None):
    """One leg in its tile's model frame (level 0): a strut from under the hull to a foot pad, thicker at the
    top (the piston). Simple shapes on purpose: fitting voxels to the sprite pixel by pixel (terrain.14)
    matched it exactly from the game's angle and made the leg lumpy from every other side."""
    sh = dict(LEG, **(shape or {}))
    sx = sy = 16 * PER
    sz = 27 * PER + 1
    g = Grid(sx, sy, sz, PER)
    cxm, czm = tx * 16 + 8 - AX, ty * 16 + 8 - AZ
    n = math.hypot(cxm, czm)
    ux, uz = cxm / n, czm / n
    def at(r, y):   # table point in the tile's frame
        return (AX + ux * r - tx * 16, y, AZ + uz * r - ty * 16)
    top = np.array(at(LEG_TOP[0], sh['top']))
    foot = np.array(at(LEG_FOOT[0], sh['pad_h']))
    P = np.stack([g.X, g.Y, g.Z], -1)
    def capsule(a, b, ra, rb):
        ab = b - a
        t = np.clip(((P - a) @ ab) / (ab @ ab), 0, 1)
        d = np.linalg.norm(P - (a + t[..., None] * ab), axis=-1)
        return d <= ra + (rb - ra) * t
    knee = top + sh['piston'] * (foot - top)
    m = capsule(top, knee, sh['piston_r'], sh['piston_r']) | capsule(knee, foot, sh['strut_r'], sh['strut_r'])
    fx, fz = foot[0], foot[2]
    rp = np.hypot(g.X - fx, g.Z - fz)
    m |= (rp <= sh['pad_r']) & (g.Y >= 0) & (g.Y <= sh['pad_h'])
    m |= (rp <= sh['cone_r'] - 0.4 * (g.Y - sh['pad_h'])) & (g.Y > sh['pad_h']) & (g.Y <= sh['pad_h'] + 1.6)
    m &= g.Y >= 0
    m[:, :, 0] = False
    g.k[m] = PLAIN
    return g


def rays(g, W, H, yoff, SS=4):
    """The comparison's view (VrCompare: 4 x 4 samples a pixel, a pixel covered when half its samples meet
    the shape): for every sample, the model voxels its ray passes through. Returns (sample's pixel index,
    voxel's flat index) pairs, without repeats."""
    py_, px_ = np.mgrid[0:H, 0:W]
    pairs = []
    for sj in range(SS):
        for si in range(SS):
            u = (px_ + (si + 0.5) / SS - 16).ravel()
            base = (py_ + (sj + 0.5) / SS - 24 - yoff).ravel()
            sid = (np.arange(H * W) * SS * SS + sj * SS + si)
            for Y in np.arange(-0.5, g.sz / g.per + 0.5, 0.05):
                v = 2 * (base + Y)
                X, Z = (u + v) / 2, (v - u) / 2
                i = np.floor((X - 8) * g.per + g.sx / 2).astype(int)
                j = np.floor((Z - 8) * g.per + g.sy / 2).astype(int)
                k = int(np.floor(Y * g.per + 1))
                if not 1 <= k < g.sz: continue
                ok = (i >= 0) & (i < g.sx) & (j >= 0) & (j < g.sy)
                pairs.append(np.stack([sid[ok], (i[ok] * g.sy + j[ok]) * g.sz + k], 1))
    pairs = np.unique(np.concatenate(pairs), axis=0)
    return pairs


def wrong_pixels(g, p, SS=4, cache={}):
    """Pixels where the leg, seen exactly as the comparison draws it (rays()), differs from its sprite."""
    idx, yoff = sprite(SET, p)
    H, W = idx.shape
    key = (p, g.k.shape)
    if key not in cache: cache[key] = rays(g, W, H, yoff, SS)
    pairs = cache[key]
    solid = (g.k.ravel()[pairs[:, 1]] > 0).astype(int)
    hit = np.bincount(pairs[:, 0], weights=solid, minlength=H * W * SS * SS) > 0
    cov = hit.reshape(H * W, SS * SS).sum(1) >= SS * SS // 2
    return int((cov != (idx > 0).ravel()).sum())


def tune_legs(legs):
    """The leg's few shape numbers (LEG), searched one at a time against all four leg sprites together, so
    the shape gets the art's proportions (piston width and length, heights) but stays clean."""
    def total(shape): return sum(wrong_pixels(leg(tx, ty, shape), p) for (tx, ty), p in legs)
    best = dict(LEG); score = total(best)
    print(f'legs: {score} pixels differ with {best}')
    steps = dict(top=(22.5, 23, 23.5, 24, 24.5, 25), piston_r=(1.0, 1.2, 1.3, 1.4, 1.5, 1.6), piston=(0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6),
                 strut_r=(0.45, 0.5, 0.55, 0.6, 0.65, 0.7), pad_r=(2.8, 3.0, 3.2, 3.4, 3.6, 3.8), pad_h=(0.3, 0.4, 0.5, 0.7, 0.9), cone_r=(1.0, 1.2, 1.4, 1.6))
    for _ in range(3):
        for k, vals in steps.items():
            for v in vals:
                cand = dict(best, **{k: v}); sc = total(cand)
                if sc < score: best, score = cand, sc
    print(f'legs: {score} pixels differ with {best}')
    return best


def main():
    os.makedirs(OUT, exist_ok=True)
    placed = map_parts()
    if '--fit' in sys.argv:
        fit(placed)
        return
    at = {(tx, ty, tz): p for tx, ty, tz, slot, p in placed if slot == 3}   # objects
    floor_at = {(tx, ty, tz): p for tx, ty, tz, slot, p in placed if slot == 0}
    solid, role, XX, ZZ, HH = hull()
    ref, light = colours(solid, role, XX, ZZ, HH)
    T = 16 * PER
    for (tx, ty, tz), p in sorted(at.items()):
        if tz != 1: continue
        ox, oy = (tx - 3) * T, (ty - 3) * T
        kg = np.where(solid[ox:ox + T, oy:oy + T, :], PLAIN, 0).astype(np.uint8)
        rg = {(x - ox, y - oy, z): v for (x, y, z), v in ref.items() if ox <= x < ox + T and oy <= y < oy + T}
        lg = {(x - ox, y - oy, z): v for (x, y, z), v in light.items() if ox <= x < ox + T and oy <= y < oy + T}
        write_piece(os.path.join(OUT, f'{SET}_{p}'), kg, rg, 0.0, lg)
    # the cap: the dome over the centre tile, from level 1 height 16 (= level 2 height CAP_BASE)
    cap = floor_at.get((4, 4, 2))
    if cap is not None:
        ox = oy = T
        k0 = int(round((24 + CAP_BASE) * PER)) + 1  # the hull's layer from level 1 height 16: the cap's layer 1
        sub = solid[ox:ox + T, oy:oy + T, k0:]
        kg = np.zeros((T, T, sub.shape[2] + 1), np.uint8)
        kg[:, :, 1:] = np.where(sub, PLAIN, 0)
        rg = {(x - ox, y - oy, z - k0 + 1): v for (x, y, z), v in ref.items() if ox <= x < ox + T and oy <= y < oy + T and z >= k0}
        lg = {(x - ox, y - oy, z - k0 + 1): v for (x, y, z), v in light.items() if ox <= x < ox + T and oy <= y < oy + T and z >= k0}
        write_piece(os.path.join(OUT, f'{SET}_{cap}'), kg, rg, CAP_BASE, lg)
    # the lift (terrain.16): the floor in the middle of level 0 and of level 1 (the same part) as a thin plate
    # flush with the floor's top, coloured as its sprite draws it, flipping bright and dark with the game's
    # frames (its own frames 8, 8, 17, 17)
    lift = floor_at.get((4, 4, 0))
    if lift is not None:
        idx, yoff = sprite(SET, lift)
        kg = np.zeros((T, T, 2), np.uint8)
        rg, lg = {}, {}
        for x in range(T):
            for y in range(T):
                X, Z = (x + 0.5) / PER, (y + 0.5) / PER
                px, py = int(16 + X - Z), int(24 + (X + Z) / 2 + yoff)
                if 0 <= px < 32 and 0 <= py < 40 and idx[py, px]:
                    kg[x, y, 1] = PLAIN
                    rg[(x, y, 1)] = (lift, px, py)
                    lg[(x, y, 1)] = 4
        write_piece(os.path.join(OUT, f'{SET}_{lift}'), kg, rg, -0.5, lg)
    legs = [((tx, ty), p) for (tx, ty, tz), p in sorted(at.items()) if tz == 0]
    if '--legs' in sys.argv: tune_legs(legs)
    for (tx, ty), p in legs:
        g = leg(tx, ty)
        print(f'  leg {p}: {wrong_pixels(g, p)} pixels differ from its sprite')
        path = os.path.join(OUT, f'{SET}_{p}.vox')
        g.write_vox(path)
        print(f'wrote {path}: {int((g.k > 0).sum())} voxels')


if __name__ == '__main__':
    main()
