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
- Colours: the outer hull's far side isn't in any sprite, so every outer voxel takes the pixel the art
  shows at the same spot turned to the near side (mirrored through the plane across the view: same screen
  column and height), from the pieces put together. The inside takes its pixel from the far pieces'
  sprites (which show the inner wall: from the game's angle you look into them), turned to the far side;
  the inside floor likewise; the underside from the near side's bottom rim.
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
PROFILE = dict(R=23.0, rim=4.0, top=23.0, bottom=-1.0, rb=18.0, e=1.4)   # from --fit
CAP_BASE = -8.0     # the cap model stands this far below level 2's floor (level 1 height 16)
SHELL = 2           # model voxels


def radius(h, R, rim, top, bottom, rb, e):
    """The saucer's radius at height h above level 1's floor (0 outside it)."""
    h = np.asarray(h, float)
    r = np.zeros_like(h)
    lo = (h >= bottom) & (h < rim)
    t = (h[lo] - bottom) / (rim - bottom)
    r[lo] = rb + (R - rb) * np.sqrt(t)
    up = (h >= rim) & (h <= top)
    t = (h[up] - rim) / (top - rim)
    r[up] = R * np.clip(1 - t ** e, 0, 1) ** (1 / e)
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
        for rim in (2, 3, 4):
            for top in (21, 22, 23):
                for bottom in (-2, -1):
                    for rb in (16, 18):
                        for e in (1.2, 1.4, 1.6):
                            p = dict(R=R, rim=rim, top=top, bottom=bottom, rb=rb, e=e)
                            m = np.zeros_like(art)
                            sol = (R2 <= radius(H, **p)) & ok0
                            m[py[sol], px[sol]] = True
                            s = (m & art).sum() / max(1, (m | art).sum())
                            if s > best[0]: best = (s, p)
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


def references(solid, role, XX, ZZ, HH, outer_comp, inner_comp):
    """Per solid voxel: (part, column, row) of the art pixel its colour comes from."""
    ref = {}
    dx, dz = XX - AX, ZZ - AZ
    depth = dx + dz            # > 0: the near half (towards the flat game's eye)
    misses = 0
    for (i, j, k) in zip(*np.nonzero(solid)):
        X, Z, H = XX[i, j, k], ZZ[i, j, k], HH[i, j, k]
        r = role[i, j, k]
        d = depth[i, j, k]
        if r in (1, 3):
            # outer hull: turned to the near side (same screen column and height)
            if d < 0: X, Z = X - d, Z - d
            comp = outer_comp
        else:
            # the inside, the floor: turned to the far side, from the far pieces' sprites
            if d > 0: X, Z = X - d, Z - d
            comp = inner_comp
        px, py = project(X, 24 + H, Z)
        hit = lookup(comp, px, py)
        if hit is None:
            misses += 1
            hit = lookup(outer_comp, px, py, 12)
        ref[(i, j, k)] = hit
    print(f'references: {len(ref)}, {misses} looked further')
    return ref


def write_piece(path, kgrid, refgrid, base):
    """A model and its .ref: refs in the table's order (z, then y, then x) over solid voxels."""
    g = Grid(kgrid.shape[0], kgrid.shape[1], kgrid.shape[2], PER)
    g.k = kgrid
    g.write_vox(path + '.vox')
    sx, sy, sz = kgrid.shape
    out = bytearray(b'VRF1' + struct.pack('<f', base))
    body = bytearray()
    count = 0
    for z in range(sz):
        for y in range(sy):
            for x in range(sx):
                if not kgrid[x, y, z]: continue
                r = refgrid.get((x, y, z))
                body += bytes((255, 0, 0)) if r is None else bytes((r[0], r[1], r[2]))
                count += 1
    out += struct.pack('<I', count) + body
    open(path + '.ref', 'wb').write(bytes(out))
    print(f'wrote {path}.vox ({count} voxels) and .ref')


# ---------------------------------------------------------------- the legs

LEG_TOP = (14.1, 25.0)   # out from the axis, up (level 0's floor)
LEG_FOOT = (20.3, 0.0)


def leg(tx, ty):
    """One leg in its tile's model frame (level 0)."""
    sx = sy = 16 * PER
    sz = 27 * PER + 1
    g = Grid(sx, sy, sz, PER)
    # direction from the axis to this corner tile
    cxm, czm = tx * 16 + 8 - AX, ty * 16 + 8 - AZ
    n = math.hypot(cxm, czm)
    ux, uz = cxm / n, czm / n
    def at(r, y):   # table point in the tile's frame
        return (AX + ux * r - tx * 16, y, AZ + uz * r - ty * 16)
    top = np.array(at(*LEG_TOP))
    foot = np.array(at(LEG_FOOT[0], 1.0))
    P = np.stack([g.X, g.Y, g.Z], -1)
    def capsule(a, b, ra, rb):
        ab = b - a
        t = np.clip(((P - a) @ ab) / (ab @ ab), 0, 1)
        d = np.linalg.norm(P - (a + t[..., None] * ab), axis=-1)
        return d <= ra + (rb - ra) * t
    knee = top + 0.55 * (foot - top)
    m = capsule(top, knee, 1.0, 0.9) | capsule(knee, foot, 0.65, 0.6)
    m |= np.linalg.norm(P - knee, axis=-1) <= 1.15                         # the piston's end
    # the foot pad: a disc on the ground, a little cone up to the strut
    fx, fz = foot[0], foot[2]
    rp = np.hypot(g.X - fx, g.Z - fz)
    m |= (rp <= 3.2) & (g.Y >= 0) & (g.Y <= 0.9)
    m |= (rp <= 1.6 - 0.4 * (g.Y - 0.9)) & (g.Y > 0.9) & (g.Y <= 2.5)
    m &= g.Y >= 0
    m[:, :, 0] = False
    g.k[m] = PLAIN
    return g, top, foot


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


def fit_leg(g, p, top, foot, SS=4):
    """0.21.0-terrain.14 (owner: the legs should fit their art exactly), seen exactly as the comparison
    draws them (rays()). Built up from the art rather than carved from the strut: for each pixel the art
    draws, the voxel nearest the strut whose footprint falls mostly in that pixel; then, among the strut's
    own voxels, every one that changes no pixel (hidden behind those, or reaching only a few samples into a
    neighbour) goes back in, so the leg stays solid from other sides; then single voxels switched while
    that leaves fewer pixels wrong."""
    idx, yoff = sprite(SET, p)
    art = (idx > 0).ravel()
    H, W = idx.shape
    NS = SS * SS
    half = NS // 2
    pairs = rays(g, W, H, yoff, SS)
    nvox = g.sx * g.sy * g.sz
    order = np.argsort(pairs[:, 1], kind='stable')
    vs, ss = pairs[order, 1], pairs[order, 0]
    starts = np.searchsorted(vs, np.arange(nvox + 1))
    a_, b_ = np.array(top), np.array(foot)
    P = np.stack([g.X.ravel(), g.Y.ravel(), g.Z.ravel()], 1)
    ab = b_ - a_
    t = np.clip(((P - a_) @ ab) / (ab @ ab), 0, 1)
    dist = np.linalg.norm(P - (a_ + t[:, None] * ab), axis=1)
    low = P[:, 1] < 2.5
    dist[low] = np.minimum(dist[low], np.hypot(P[low, 0] - b_[0], P[low, 2] - b_[2]) * 0.4 + np.abs(P[low, 1] - 0.45))
    shape0 = g.k.ravel().copy()
    flat = np.zeros_like(shape0)
    hits = np.zeros(H * W * NS, int)
    pixcov = np.zeros(H * W, int)
    def toggle(q, on):
        sm = ss[starts[q]:starts[q + 1]]
        before = hits[sm] > 0
        hits[sm] += 1 if on else -1
        ch = sm[before != (hits[sm] > 0)]
        if len(ch): np.add.at(pixcov, ch // NS, 1 if on else -1)
        flat[q] = PLAIN if on else 0
    def status(px): return pixcov[px] >= half
    def pix_wrong(px):
        px = np.unique(px)
        return int((status(px) != art[px]).sum())
    def footprint(q):
        sm = ss[starts[q]:starts[q + 1]]
        px, n = np.unique(sm // NS, return_counts=True)
        return px, n
    # 1. a voxel for each pixel the art draws
    cand_by_pix = {}
    for q in np.nonzero((dist <= 3.5) & (starts[1:] > starts[:-1]))[0]:
        px, n = footprint(q)
        m = int(np.argmax(n))
        if n[m] >= half: cand_by_pix.setdefault(int(px[m]), []).append(q)
    for px in np.nonzero(art)[0]:
        if status(px): continue
        for q in sorted(cand_by_pix.get(int(px), ()), key=lambda q: dist[q]):
            fp, n = footprint(q)
            w0 = pix_wrong(fp)
            toggle(q, True)
            if pix_wrong(fp) < w0: break
            toggle(q, False)
    # 2. the strut's own voxels wherever they change nothing
    for q in np.nonzero(shape0 & (flat == 0))[0]:
        if starts[q + 1] == starts[q]: flat[q] = PLAIN; continue      # seen by no ray
        fp, _ = footprint(q)
        st = status(fp).copy()
        toggle(q, True)
        if (status(fp) != st).any(): toggle(q, False)
    # 3. single switches while they help
    near = np.nonzero((dist <= 3.5) & (starts[1:] > starts[:-1]))[0]
    for rnd_ in range(10):
        changed = 0
        for q in near[np.argsort(dist[near])]:
            fp, _ = footprint(q)
            w0 = pix_wrong(fp)
            on = not flat[q]
            toggle(q, on)
            if pix_wrong(fp) < w0: changed += 1
            else: toggle(q, not on)
        if not changed: break
    g.k = flat.reshape(g.k.shape)
    w = int((status(np.arange(H * W)) != art).sum())
    print(f'  leg {p}: {w} pixels differ from the art ({int((g.k > 0).sum())} voxels)')
    return g


def main():
    os.makedirs(OUT, exist_ok=True)
    placed = map_parts()
    if '--fit' in sys.argv:
        fit(placed)
        return
    at = {(tx, ty, tz): p for tx, ty, tz, slot, p in placed if slot == 3}   # objects
    floor_at = {(tx, ty, tz): p for tx, ty, tz, slot, p in placed if slot == 0}
    outer_comp = composite(placed, lambda tx, ty, tz, p: (tz == 1 and p != 9) or tz == 2)
    # the far pieces (west, north, north-west): from the game's angle their sprites show the inside
    far = {(3, 3, 1), (4, 3, 1), (3, 4, 1), (4, 4, 1)}
    inner_comp = composite(placed, lambda tx, ty, tz, p: (tx, ty, tz) in far)
    solid, role, XX, ZZ, HH = hull()
    ref = references(solid, role, XX, ZZ, HH, outer_comp, inner_comp)
    T = 16 * PER
    for (tx, ty, tz), p in sorted(at.items()):
        if tz != 1: continue
        ox, oy = (tx - 3) * T, (ty - 3) * T
        kg = np.where(solid[ox:ox + T, oy:oy + T, :], PLAIN, 0).astype(np.uint8)
        rg = {(x - ox, y - oy, z): v for (x, y, z), v in ref.items() if ox <= x < ox + T and oy <= y < oy + T}
        write_piece(os.path.join(OUT, f'{SET}_{p}'), kg, rg, 0.0)
    # the cap: the dome over the centre tile, from level 1 height 16 (= level 2 height CAP_BASE)
    cap = floor_at.get((4, 4, 2))
    if cap is not None:
        ox = oy = T
        k0 = int(round((24 + CAP_BASE) * PER)) + 1  # the hull's layer from level 1 height 16: the cap's layer 1
        sub = solid[ox:ox + T, oy:oy + T, k0:]
        kg = np.zeros((T, T, sub.shape[2] + 1), np.uint8)
        kg[:, :, 1:] = np.where(sub, PLAIN, 0)
        rg = {(x - ox, y - oy, z - k0 + 1): v for (x, y, z), v in ref.items() if ox <= x < ox + T and oy <= y < oy + T and z >= k0}
        write_piece(os.path.join(OUT, f'{SET}_{cap}'), kg, rg, CAP_BASE)
    for (tx, ty, tz), p in sorted(at.items()):
        if tz != 0: continue
        g, top, foot = leg(tx, ty)
        g = fit_leg(g, p, top, foot)
        path = os.path.join(OUT, f'{SET}_{p}.vox')
        g.write_vox(path)
        print(f'wrote {path}: {int((g.k > 0).sum())} voxels')


if __name__ == '__main__':
    main()
