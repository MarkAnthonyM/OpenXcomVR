#!/usr/bin/env python3
"""Hand-built model of the Skyranger (terrain set PLANE, map block PLANE): 0.21.0-terrain.18, the outside;
0.21.0-terrain.19, faceted and textured from the art (FACETED, ART_TEXTURE; texture()); 0.21.0-terrain.22, each
panel lit evenly (panels()); 0.21.0-terrain.23, the hull's top slimmer, the cockpit hood (HOOD), the beak (BEAK),
the device under the cockpit (FEATURE) and the nose textured panel by panel (tex_panels()).

Run after `dumpart PLANE` in the terrain lab (see voxlib.py). Writes bin/common/Resources/VR/models/
PLANE_<id>.vox and .ref for every part that holds a piece of the craft. Shapes and pixel references only,
never the game's pictures. Follows the hand-built notes in vr-dev/HANDOFF.md (Decisions).

The craft is built once, whole, in map voxels on one grid (2 model voxels a table voxel), as clean shapes
whose few sizes were fitted to the art put together as the map places it, then cut along the map's tiles
and levels into the parts that hold each piece. The map faces the nose south (+Z); X across, Y up.
- Fuselage: a tall arch (superellipse) 56 wide on the cabin floor (Y 24), its top 52, constant from the
  rear (Z 112) to the cockpit (Z 256: the game repeats one roof piece over that whole length), then
  narrowing and dropping to the nose tip (Z 298). A shell one table voxel thick, purple inside; a rear
  doorway where the game has no part (x4..5 under the roof), a belly hatch over the ramp, no bottom under
  the cabin's floor tiles (the game's floor is the floor).
- Nose cone (yellow) and canopy (light blue, purple frame) on the nose.
- Wings: swept plates (trailing edge across at Z 112, leading edge from the tip to the root at 37
  degrees), red tips. The front wings are the game's own copy of the main wings' outer two tiles (the
  same parts), so they come out of the same models.
- Fins: two trapezoid plates on the roof's edges, a red light on top, the "V" emblem and an orange stripe
  on their outer faces.
- Engine pods: chamfered boxes under the wings, centred in their tiles (both pods use the same parts),
  an intake at the front, a blue nozzle at the back, an orange stripe along both sides.
- Landing gear: four-wheel bogies (main) and a wheel pair with a drag brace (nose); the ramp, a grating
  from the floor's rear edge to the ground.
Markings on both sides (owner): the X-COM roundel on each side ahead of the wings.
Since terrain.19 (FACETED) the fuselage and nose are flat panels at clean angles (CROSS, NOSE_W/T/B) and the
canopy a faceted hood; with ART_TEXTURE the hull, wings, fins and pods take the art's texture, straightened
(straighten.py). The round arch and the area colours stay behind the flags for comparison.
Owner's choices (2026-11-05): the outside first, the inside's ribs next; markings both sides; nozzles
plain blue (not glowing).
"""
import math, os, struct, sys
import numpy as np
from voxlib import *

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(REPO, 'bin', 'common', 'Resources', 'VR', 'models')
DATA = os.path.join(os.environ.get('VRDEV', os.path.expanduser('~/vrdev')), 'run', 'data', 'UFO')
SET = 'PLANE'
OFFSET = 2      # map bytes -> PLANE ids (BLANKS first)
PER = 2

# ---------------------------------------------------------------- the grid (map voxels -> cells)
X0, X1 = 0, 160
Z0, Z1 = 96, 304
Y0, Y1 = 0, 80
NX, NZ, NY = (X1 - X0) * PER, (Z1 - Z0) * PER, (Y1 - Y0) * PER


def centres(lo, n): return lo + (np.arange(n) + 0.5) / PER


GX, GZ, GY = centres(X0, NX), centres(Z0, NZ), centres(Y0, NY)

# ---------------------------------------------------------------- the fitted sizes (map voxels)
CXM = 80.0                       # the craft's centre line
FUS = dict(w=28.0, y0=24.0, yt=52.0, e=2.5, rb=24.0)       # cross-section (85% of the middle's outline)
FUS_Z = (112.0, 256.0)           # constant from the rear to the cockpit
NOSE = dict(zt=298.0, p=1.5, q=2.0, ytip=28.0, tipw=4.0)    # the taper to the tip (90% of the nose's outline)
SHELL = 2                        # model voxels
WING = dict(te=113.0, le0=127.5, slope=0.754, root=64.0, y=(48.0, 49.0), tip=1.0)  # left wing (refitted, terrain.23)
FIN = dict(x=64.0, t=1.0, z=(112.0, 142.0), ztop=131.0, y=(48.0, 72.0))     # left fin; the right one mirrored
POD = dict(xc=40.0, yc=40.0, a=8.0, b=8.0, c=2.0, z=(116.0, 170.0), lip=4.0)  # left pod (89% with both)
GEAR_MAIN = dict(c=(56.0, 152.0), r=4.5, ww=2.0, sx=3.0, sz=5.0, sr=1.3)       # 89%
GEAR_NOSE = dict(c=(72.0, 250.0), r=4.5, ww=2.5, sx=3.5, sz=0.0, sr=1.3)
RAMP = dict(x=(64.0, 96.0), z=(96.0, 144.0), y=(0.0, 24.0), t=1.2)
CANOPY = dict(z=274.0, y=41.0, rx=11.0, ry=8.0, rz=17.0)   # the round bubble (FACETED off): the art's glass (64%)
CONE_Z = 294.0                   # the body's tip from here on is yellow
# terrain.23 (owner: the canopy and the nose cone like the art), fitted to the art's glass, frame and yellow from
# the game's angle: the cockpit a faceted hood (an octagon in plan) on the nose's top, centre Z 282, 10 long and 11
# wide each way, its flat top 5 above the nose's top, a solid roof (the art's grey above the glass); glass ahead of
# Z 272: the windscreen (front faces) and a side window each side, framed. The beak a rounded octagon (elliptic
# taper) from Z 293 to its tip at 302, on Y 27, 6 out (the art's yellow 61% -> 75%), its bottom the nose's level
# (nothing of the game's below level 1 there to hold more).
HOOD = dict(z=282.0, rx=11.0, rz=10.0, lift=5.0, zg=279.0, roof=0.55, solid=0.0)   # roof: the flat top; solid: its grey part
BEAK = dict(yc=27.0, r=6.0, zs=293.0, zt=302.0, p=2.0, ybot=24.0)

# ---------------------------------------------------------------- colours (palette entry, share)
AREAS = {
    'hull': [(246, 50), (245, 25), (247, 25)],      # the middle of the art's gunmetal ramp (240 lit .. 252)
    'inside': [(230, 60), (231, 40)],
    'red': [(34, 60), (35, 40)],
    'pod': [(243, 50), (242, 25), (244, 25)],
    'stripe': [(149, 60), (150, 40)],
    'intake': [(238, 60), (239, 40)],
    'nozzle': [(132, 50), (131, 25), (133, 25)],
    'tyre': [(13, 60), (12, 40)],
    'hub': [(10, 100)],
    'strut': [(9, 60), (8, 40)],
    'band': [(102, 60), (101, 40)],
    'ramp': [(88, 70), (86, 30)],
    'grate': [(8, 70), (9, 30)],
    'yellow': [(16, 70), (17, 30)],
    'glass': [(129, 50), (128, 30), (130, 20)],
    'frame': [(232, 60), (234, 40)],
    'roundel': [(144, 70), (145, 30)],
    'emblem': [(235, 100)],
    'finstripe': [(98, 60), (99, 40)],
    'panel': [(249, 100)],                           # the wings' panel lines (the art's darkest greys there)
    'hole': [(13, 70), (12, 30)],                    # the side device's ports (terrain.23)
    'slot': [(11, 60), (12, 40)],                    # its slats' grooves
    'warn': [(37, 70), (36, 30)],                    # the red warning triangle
    'warnmid': [(20, 100)],                          # its yellow middle
    'amber': [(101, 60), (100, 40)],                 # the small amber light
}
CODES = {name: i + 1 for i, name in enumerate(AREAS)}
NAMES = {v: k for k, v in CODES.items()}

# components (what a part may hold)
C_FUS, C_CONE, C_CANOPY, C_WING, C_FIN, C_POD, C_GEAR, C_RAMP = range(1, 9)


# ---------------------------------------------------------------- shapes
def fus_section(Z):
    """Per Z: (half-width, bottom, top, bottom rounding) of the fuselage's cross-section."""
    Z = np.asarray(Z, float)
    t = np.clip((Z - FUS_Z[1]) / (NOSE['zt'] - FUS_Z[1]), 0, 1)
    s = (1 - t ** NOSE['p']) ** (1 / NOSE['p'])
    ww = NOSE['tipw'] + (FUS['w'] - NOSE['tipw']) * s
    yb = FUS['y0'] + (NOSE['ytip'] - FUS['y0']) * t ** NOSE['q']
    yt = FUS['yt'] + (NOSE['ytip'] - FUS['yt']) * t ** NOSE['q']
    yt = np.maximum(yt, yb + 1.0)
    return ww, yb, yt, FUS['rb'] * ww / FUS['w']


# faceted (terrain.19, owner: to try in the headset): the hull as flat panels at clean angles, so the voxel
# steps on each panel are even (1:1, 1:2, 2:1, square to the grid). The cross-section by height (Y, half-width)
# from the floor: 1:1 out, a vertical side, 2:1 in, 1:1 in, 1:2 in, a flat top. The nose in plan (Z, half-width),
# its top and bottom in side view (Z, Y), scaled from the middle's section.
FACETED = True
ART_TEXTURE = True      # terrain.19: the hull, wings, fins and pods take the art's texture, straightened (texture())
# terrain.23: the upper facets 2 lower and in (were 36/28, 44/24, 50/18, 52/14): the art's far shoulder from the game's
# angle sits 3.5 below the old one (it hid the wing roots and the fins' feet); the 1:1 facet keeps 16 at the wing's
# height so the wings (their tiles end at X 64) still meet the hull
CROSS = [(24.0, 24.0), (28.0, 28.0), (34.0, 28.0), (42.0, 24.0), (50.0, 16.0), (52.0, 12.0)]
NOSE_W = [(256.0, 28.0), (272.0, 24.0), (284.0, 18.0), (292.0, 10.0), (298.0, 4.0)]
NOSE_T = [(256.0, 52.0), (272.0, 50.0), (284.0, 44.0), (292.0, 36.0), (298.0, 30.0)]
NOSE_B = [(256.0, 24.0), (284.0, 24.0), (292.0, 25.0), (298.0, 27.0)]


def fus_inside(X, Y, Z):
    if FACETED: return fus_faceted(X, Y, Z)
    ww, yb, yt, rb = fus_section(Z)
    h = (Y - yb) / (yt - yb)
    r = ww * np.clip(1 - np.clip(h, 0, 1) ** FUS['e'], 0, 1) ** (1 / FUS['e'])
    r = np.minimum(r, rb + (ww - rb) * np.clip(h * 6, 0, 1))
    return (np.abs(X - CXM) <= r) & (h >= 0) & (h <= 1) & (Z >= FUS_Z[0]) & (Z <= NOSE['zt'])


def fus_faceted(X, Y, Z):
    ww = np.interp(Z, *zip(*NOSE_W)); yt = np.interp(Z, *zip(*NOSE_T)); yb = np.interp(Z, *zip(*NOSE_B))
    Yn = FUS['y0'] + (Y - yb) / np.maximum(yt - yb, 0.5) * (FUS['yt'] - FUS['y0'])
    hw = np.interp(Yn, *zip(*CROSS)) * ww / FUS['w']
    return (np.abs(X - CXM) <= hw) & (Yn >= FUS['y0']) & (Yn <= FUS['yt']) & (Z >= FUS_Z[0]) & (Z <= NOSE['zt'])


def hood_L(X, Z):
    """The cockpit hood's plan (HOOD): L (1 at its foot's octagon) and which face wins (0 side, 1 front/back,
    2 diagonal)."""
    h = HOOD
    ax, az = np.abs(X - CXM) / h['rx'], np.abs(Z - h['z']) / h['rz']
    terms = np.stack(np.broadcast_arrays(ax, az, (ax + az) / 1.4))
    return terms.max(0), terms.argmax(0)


def beak_inside(X, Y, Z):
    """The yellow beak (BEAK): a regular octagon across, tapering elliptically to its tip."""
    b = BEAK
    t = np.clip((Z - b['zs']) / (b['zt'] - b['zs']), 0, 1)
    R = b['r'] * np.clip(1 - t ** b['p'], 0, 1) ** (1 / b['p'])
    dx, dy = np.abs(X - CXM), np.abs(Y - b['yc'])
    return (Z >= b['zs']) & (Z <= b['zt']) & (dx <= R) & (dy <= R) & (dx + dy <= 1.414 * R) & (Y >= b['ybot'])


# the device under the cockpit on each side (terrain.23; the art shows it on the right, the other side mirrored):
# a low pad one table voxel out from the hull with two small dark ports side by side and slats behind them (the
# art's dark block and stacked dark dashes), the red warning triangle with its yellow middle ahead of it (pointing
# down) and a small amber light above. Positions: the art's pixels mapped onto the nose.
FEATURE = dict(z=(279.0, 287.0), y=(31.0, 35.5), ports=((283.5, 285.0), (285.5, 287.0)), port_y=(33.0, 35.0),
               slats=(279.0, 282.5), tri=(289.5, 28.0, 33.0, 0.4), amber=((280.0, 283.0), (39.5, 41.0)))


def side_feature(c):
    f = FEATURE
    box = ((CXM - 26, CXM + 26), (276.0, 292.0), (26.0, 42.0))
    sl, X, Z, Y = grid(box)
    s = np.sign(X - CXM)
    inside = fus_inside(X, Y, Z)
    win = (Z >= f['z'][0]) & (Z < f['z'][1]) & (Y >= f['y'][0]) & (Y < f['y'][1])
    pad = ~inside & fus_inside(X - s * 1.0, Y, Z) & win
    outer = pad & ~fus_inside(X - s * 0.5, Y, Z)          # the pad's outer layer (one model voxel)
    port = np.zeros(X.shape, bool)
    for z0, z1 in f['ports']:
        port |= (Z >= z0) & (Z < z1) & (Y >= f['port_y'][0]) & (Y < f['port_y'][1])
    slat = (Z >= f['slats'][0]) & (Z < f['slats'][1]) & (np.floor(Y * PER) % 2 == 0)
    cur, area = c.comp[sl], c.area[sl]
    keep = pad & ~(outer & (port | slat))                   # ports and grooves: the outer layer cut away
    cur[keep] = C_FUS
    area[keep] = CODES['hull']
    area[keep & ~outer & port] = CODES['hole']
    area[keep & ~outer & slat] = CODES['slot']
    # painted on the hull's skin: the triangle and the amber light
    skin = (cur == C_FUS) & (area == CODES['hull']) & ~fus_inside(X + s * 0.5, Y, Z) & (np.abs(X - CXM) > 6)
    tz, ya, yb, k = f['tri']
    tri = skin & (Y >= ya) & (Y < yb) & (np.abs(Z - tz) <= (Y - ya) * k + 0.3) & (Z >= f['z'][1])
    mid = tri & (Y >= ya + 1.5) & (Y < yb - 1.0) & (np.abs(Z - tz) <= (Y - ya - 1.5) * k)
    area[tri] = CODES['warn']; area[mid] = CODES['warnmid']
    (az0, az1), (ay0, ay1) = f['amber']
    area[skin & (Z >= az0) & (Z < az1) & (Y >= ay0) & (Y < ay1)] = CODES['amber']


def grid(box):
    """Index slices and centre meshes for a box ((x0, x1), (z0, z1), (y0, y1)) in map voxels."""
    (x0, x1), (z0, z1), (y0, y1) = box
    i0, i1 = max(0, int((x0 - X0) * PER)), min(NX, int(math.ceil((x1 - X0) * PER)))
    k0, k1 = max(0, int((z0 - Z0) * PER)), min(NZ, int(math.ceil((z1 - Z0) * PER)))
    j0, j1 = max(0, int((y0 - Y0) * PER)), min(NY, int(math.ceil((y1 - Y0) * PER)))
    X, Z, Y = np.meshgrid(GX[i0:i1], GZ[k0:k1], GY[j0:j1], indexing='ij')
    return (slice(i0, i1), slice(k0, k1), slice(j0, j1)), X, Z, Y


def erode(m, n):
    for _ in range(n):
        c = m.copy()
        for ax in range(3):
            for s in (1, -1):
                sh = np.roll(m, s, axis=ax)
                idx = [slice(None)] * 3
                idx[ax] = 0 if s == 1 else -1
                sh[tuple(idx)] = False
                c &= sh
        m = c
    return m


def touching(m, other):
    """Cells of m with a face neighbour in other."""
    t = np.zeros_like(m)
    for ax in range(3):
        for s in (1, -1):
            sh = np.roll(other, s, axis=ax)
            idx = [slice(None)] * 3
            idx[ax] = 0 if s == 1 else -1
            sh[tuple(idx)] = False
            t |= sh
    return m & t


class Craft:
    def __init__(self):
        self.comp = np.zeros((NX, NZ, NY), np.uint8)
        self.area = np.zeros((NX, NZ, NY), np.uint8)

    def put(self, box, mask_fn, comp, area_fn, over=False):
        sl, X, Z, Y = grid(box)
        m = mask_fn(X, Y, Z)
        cur = self.comp[sl]
        if not over: m &= cur == 0
        a = area_fn(X, Y, Z, m) if callable(area_fn) else np.full(m.shape, CODES[area_fn], np.uint8)
        cur[m] = comp
        self.area[sl][m] = a[m]
        return m

    def mirror(self, box, comp):
        """Copy what lies in box (left of the centre line) to the right, mirrored about X = 80."""
        sl, X, Z, Y = grid(box)
        src_c, src_a = self.comp[sl], self.area[sl]
        i0, i1 = sl[0].start, sl[0].stop
        m0, m1 = NX - i1, NX - i0
        dst = (slice(m0, m1), sl[1], sl[2])
        sel = src_c == comp
        dc, da = self.comp[dst], self.area[dst]
        dc[sel[::-1]] = comp
        da[sel[::-1]] = src_a[::-1][sel[::-1]]


def build():
    c = Craft()
    # --- the fuselage: outer solid, hollowed one table voxel in
    box = ((CXM - 30, CXM + 30), (FUS_Z[0] - 1, NOSE['zt'] + 1), (FUS['y0'] - 1, FUS['yt'] + 1))
    sl, X, Z, Y = grid(box)
    outer = fus_inside(X, Y, Z)
    cav = erode(outer, SHELL)
    # the rear stays closed above the doorway (the doorway: no part under the roof there, see cut())
    shell = outer & ~cav
    # no bottom under the cabin's floor tiles (x4..5, rows 9..17) nor over the ramp's hatch (rows 7..8)
    bottom = (Y < FUS['y0'] + SHELL / PER) & (X >= 64) & (X < 96) & (Z >= 112) & (Z < 288)
    shell &= ~bottom
    inner = touching(shell, cav) & ~touching(shell, ~outer)
    area = np.where(inner, CODES['inside'], CODES['hull']).astype(np.uint8)
    cur = c.comp[sl]
    cur[shell] = C_FUS
    c.area[sl][shell] = area[shell]
    c.fus_outer = (sl, outer)
    # --- the roundel: a yellow disc with a red cross on each side, ahead of the wing (measured on the art)
    rz, ry, rr = ROUNDEL
    for side in (-1, 1):
        on = (c.comp[sl] == C_FUS) & (c.area[sl] == CODES['hull']) & (np.sign(X - CXM) == side)
        d = np.hypot(Y - ry, Z - rz)
        cross = (np.abs((Y - ry) - (Z - rz)) < 0.9) | (np.abs((Y - ry) + (Z - rz)) < 0.9)
        a = c.area[sl]
        a[on & (d <= rr)] = CODES['roundel']
        a[on & (d <= rr - 0.8) & cross] = CODES['red']
    # --- the nose cone: the body's tip, yellow, solid
    # and a short cone ahead of it, on the tip's axis (the art's beak reaches past the body: row 18's end)
    def cone(X, Y, Z):
        t = (Z - CONE_Z) / (303.0 - CONE_Z)
        return (fus_inside(X, Y, Z) & (Z >= CONE_Z)) | ((t >= 0) & (t <= 1) & (np.hypot(X - CXM, (Y - 29.0) * 1.2) <= 5.0 * np.clip(1 - t, 0, 1) ** 0.7))
    if FACETED:
        def cone(X, Y, Z):
            return (fus_inside(X, Y, Z) & (Z >= CONE_Z)) | beak_inside(X, Y, Z)
    c.put(((CXM - 10, CXM + 10), (CONE_Z - 1, 304), (20, 40)), cone, C_CONE, 'yellow', over=True)
    # --- the canopy: a raised bubble on the nose's top, glass with a frame round its foot
    cp = CANOPY
    def bubble(X, Y, Z):
        e = ((X - CXM) / cp['rx']) ** 2 + ((Y - cp['y']) / cp['ry']) ** 2 + ((Z - cp['z']) / cp['rz']) ** 2
        return (e <= 1.0) & ~fus_inside(X, Y - 0.6, Z)
    def canopy_area(X, Y, Z, m):
        foot = fus_inside(X, Y - 1.0, Z)       # the bubble's lowest band: the frame
        bars = (np.abs(X - CXM) < 0.6) | ((Z > cp['z'] - 6.5) & (Z < cp['z'] - 5.5))   # the middle bar, the cross bar
        return np.where(foot | bars, CODES['frame'], CODES['glass']).astype(np.uint8)
    box = ((CXM - cp['rx'] - 1, CXM + cp['rx'] + 1), (cp['z'] - cp['rz'] - 1, cp['z'] + cp['rz'] + 1), (cp['y'] - cp['ry'] - 1, 56.0))
    if FACETED:
        # the cockpit: a faceted hood on the nose's top (HOOD; terrain.19's was the round bubble's size, 2.5 high,
        # centred 8 further back): an octagon in plan, its flat top (the roof) following the nose's top, its
        # sides sloping down to the hull
        h = HOOD
        def bubble(X, Y, Z):
            L, _ = hood_L(X, Z)
            ytop = np.interp(Z, *zip(*NOSE_T))
            lift = h['lift'] * np.clip((1 - L) / (1 - h['roof']), 0, 1)
            return (L <= 1) & (Y <= ytop + lift) & (Y >= ytop - 6) & ~fus_inside(X, Y - 0.6, Z)
        def canopy_area(X, Y, Z, m):
            # glass ahead of zg: the windscreen (the front faces) and a side window each side, a bar between them
            # (the art's one bar), the flat top's front part; frame: the foot and the glass's rear edge; the roof's
            # middle solid (grey)
            L, t = hood_L(X, Z)
            foot = fus_inside(X, Y - 1.0, Z)
            rear = np.abs(Z - h['zg']) < 0.75
            ax, az = np.abs(X - CXM) / h['rx'], np.abs(Z - h['z']) / h['rz']
            d = (ax + az) / 1.4
            bar = (Z > h['z']) & (np.abs(ax - d) < 0.06 * np.maximum(ax, 0.3)) & (L > 0.5)
            solid = L < h['solid']
            a = np.full(X.shape, CODES['hull'], np.uint8)
            ahead = Z >= h['zg'] - 0.75
            a[ahead & ~solid] = CODES['glass']
            a[ahead & ~solid & (foot | rear | bar)] = CODES['frame']
            return a
        box = ((CXM - h['rx'] - 1, CXM + h['rx'] + 1), (h['z'] - h['rz'] - 1, h['z'] + h['rz'] + 1), (28.0, 60.0))
    c.put(box, bubble, C_CANOPY, canopy_area, over=True)
    if FACETED: side_feature(c)
    # --- wings (left; the right mirrored): a plate, the tip edge red
    w = WING
    def wing(X, Y, Z):
        le = w['le0'] + w['slope'] * (X - w['tip'])
        return (X >= w['tip']) & (X < w['root']) & (Z >= w['te']) & (Z <= le) & (Y >= w['y'][0]) & (Y < w['y'][1]) & ~fus_inside(X, Y, Z)
    def wing_area(X, Y, Z, m):
        # the red tip, round the leading corner too (the leading edge's steps beyond X 1 were left grey)
        x = X - w['tip']
        a = np.where((x < 1.0) | ((x < 2.5) & (Z >= w['le0'])), CODES['red'], CODES['hull']).astype(np.uint8)
        top = Y >= w['y'][1] - 0.5
        lines = ((np.abs(Z - 123.5) < 0.5) & (X >= 16) & (X <= 46)) | ((np.abs(X - 16.25) < 0.5) & (Z < 123.5)) | ((np.abs(X - 44.25) < 0.5) & (Z > 120) & (Z < 128))
        if not ART_TEXTURE: a[top & lines] = CODES['panel']      # else texture() joins the art's own lines
        return a
    c.put(((0, 64), (112, 176), w['y']), wing, C_WING, wing_area)
    c.mirror(((0, 64), (112, 176), w['y']), C_WING)
    # the front wings: the game draws them with the main wings' outer two tiles (the same parts), so they
    # are those tiles' cells, moved (left: x 0..1, rows 7..9 -> x 2..3, rows 14..16; the right mirrored)
    for (sx0, dx) in ((0, 32), (128, -32)):
        i0, i1, di = int(sx0 * PER), int((sx0 + 32) * PER), int(dx * PER)
        k0, k1, dk = int((112 - Z0) * PER), int((160 - Z0) * PER), int(112 * PER)
        jw = slice(int(w['y'][0] * PER), int(w['y'][1] * PER))
        src_c = c.comp[i0:i1, k0:k1, jw].copy(); src_a = c.area[i0:i1, k0:k1, jw].copy()
        dc, da = c.comp[i0 + di:i1 + di, k0 + dk:k1 + dk, jw], c.area[i0 + di:i1 + di, k0 + dk:k1 + dk, jw]
        sel = (src_c == C_WING) & (dc == 0)
        dc[sel] = C_WING; da[sel] = src_a[sel]
    # terrain.23: where the nose narrows (from Z 256) the slimmer hull leaves a slit (one model voxel) between the
    # front wings' roots (their tiles end at X 64) and the hull: the hull filled out to them there (the roof piece
    # above holds it)
    for side in (-1, 1):
        sl, X, Z, Y = grid(((CXM - 20, CXM + 20), (224.0, 272.0), w['y']))
        cur, ar = c.comp[sl], c.area[sl]
        root = np.abs(X - CXM) <= 16.0
        gap = (cur == 0) & (np.sign(X - CXM) == side) & root & fus_inside(X - side * 2.0, Y, Z) & ~fus_inside(X, Y, Z)
        wing_out = np.zeros_like(gap)
        step = 1 if side < 0 else -1          # the neighbour toward the tip
        wing_out[:] = np.roll(cur == C_WING, step, axis=0)
        fill = gap & wing_out
        cur[fill] = C_FUS; ar[fill] = CODES['hull']
    # --- fins (left; the right mirrored)
    f = FIN
    def fin(X, Y, Z):
        zfront = f['z'][1] + (f['ztop'] - f['z'][1]) * (Y - f['y'][0]) / (f['y'][1] - f['y'][0])
        return (X >= f['x']) & (X < f['x'] + f['t']) & (Z >= f['z'][0]) & (Z <= zfront) & (Y >= f['y'][0]) & (Y < f['y'][1])
    def fin_area(X, Y, Z, m):
        a = np.full(X.shape, CODES['hull'], np.uint8)
        outer_face = X < f['x'] + 0.5          # the left fin's outer face is its -X side
        a[outer_face & (Y >= 68.5) & (Y < 70.0) & (Z >= 118) & (Z < 127)] = CODES['finstripe']
        # the "V": a downward triangle, notched
        v = (Y >= 53) & (Y < 64) & (np.abs(Z - 124.5) <= (Y - 53) * 0.42 + 0.8)
        notch = (Y >= 57) & (np.abs(Z - 124.5) <= (Y - 57) * 0.22)
        a[outer_face & v & ~notch] = CODES['emblem']
        a[(Y >= f['y'][1] - 1.5) & (Z >= f['ztop'] - 2.0)] = CODES['red']      # the light on top
        return a
    c.put(((f['x'], f['x'] + f['t']), (f['z'][0], f['z'][1] + 1), f['y']), fin, C_FIN, fin_area)
    c.mirror(((f['x'], f['x'] + f['t']), (f['z'][0], f['z'][1] + 1), f['y']), C_FIN)
    # --- engine pods (left; the right mirrored)
    p = POD
    def pod_solid(X, Y, Z, inset=0.0):
        dx, dy = np.abs(X - p['xc']), np.abs(Y - p['yc'])
        zf = p['z'][1] + p['lip'] * (Y - (p['yc'] - p['b'])) / (2 * p['b'])     # the intake leans forward at the top
        a, b = p['a'] - inset, p['b'] - inset
        return (dx <= a) & (dy <= b) & (dx + dy <= a + b - p['c']) & (Z >= p['z'][0]) & (Z <= zf)
    def pod(X, Y, Z):
        body = pod_solid(X, Y, Z)
        zf = p['z'][1] + p['lip'] * (Y - (p['yc'] - p['b'])) / (2 * p['b'])
        mouth = pod_solid(X, Y, Z, 1.0) & (Z > zf - 3.0)       # the intake, 3 deep
        noz = (np.sqrt(((X - p['xc']) / 4.5) ** 2 + ((Y - p['yc']) / 4.5) ** 2 + ((Z - p['z'][0]) / 3.0) ** 2) <= 1) & (Z < p['z'][0])
        return (body & ~mouth) | noz
    def pod_area(X, Y, Z, m):
        a = np.full(X.shape, CODES['pod'], np.uint8)
        zf = p['z'][1] + p['lip'] * (Y - (p['yc'] - p['b'])) / (2 * p['b'])
        a[(np.abs(X - p['xc']) >= p['a'] - 1.0) & (np.abs(Y - p['yc']) < 0.75) & (Z > p['z'][0] + 6) & (Z < zf - 6)] = CODES['stripe']
        a[pod_solid(X, Y, Z, 1.0) & (Z > zf - 4.0)] = CODES['intake']
        a[Z < p['z'][0]] = CODES['nozzle']
        return a
    pbox = ((p['xc'] - p['a'], p['xc'] + p['a']), (p['z'][0] - 3, p['z'][1] + p['lip'] + 1), (p['yc'] - p['b'], p['yc'] + p['b']))
    c.put(pbox, pod, C_POD, pod_area)      # the intake's lip stays pod-coloured, its inside dark
    c.mirror(pbox, C_POD)
    # --- landing gear (left main and nose; right mirrored)
    def gear_fn(g, brace):
        cx, cz = g['c']
        def solid(X, Y, Z):
            m = (np.hypot(X - cx, Z - cz) <= g['sr']) & (Y >= g['r']) & (Y < 24.5)
            axles = (-g['sz'], g['sz']) if g['sz'] else (0.0,)
            for az in axles:
                for wx in (-g['sx'], g['sx']):
                    m |= (np.abs(X - cx - wx) <= g['ww'] / 2) & (np.hypot(Y - g['r'], Z - cz - az) <= g['r'])
                m |= (np.abs(X - cx) <= g['sx']) & (np.hypot(Y - g['r'], Z - cz - az) <= 0.8)        # the axle
            if g['sz']:
                m |= (np.abs(X - cx) <= 1.0) & (np.abs(Z - cz) <= g['sz']) & (np.abs(Y - g['r'] - 0.8) <= 0.8)   # the bogie
            if brace:
                a = np.array((cx, 9.0, cz)); b = np.array((cx, 24.0, cz - 9.0))
                P = np.stack([X, Y, Z], -1); ab = b - a
                t = np.clip(((P - a) @ ab) / (ab @ ab), 0, 1)
                m |= np.linalg.norm(P - (a + t[..., None] * ab), axis=-1) <= 0.7
            return m
        def area(X, Y, Z, m):
            a = np.full(X.shape, CODES['strut'], np.uint8)
            axles = (-g['sz'], g['sz']) if g['sz'] else (0.0,)
            for az in axles:
                d = np.hypot(Y - g['r'], Z - cz - az)
                for wx in (-g['sx'], g['sx']):
                    w_ = np.abs(X - cx - wx) <= g['ww'] / 2
                    a[w_ & (d <= g['r'])] = CODES['tyre']
                    a[w_ & (d <= g['r'] * 0.45)] = CODES['hub']
            a[(np.hypot(X - cx, Z - cz) <= g['sr'] + 0.01) & (Y >= 14) & (Y < 15.5)] = CODES['band']
            return a
        box = ((cx - g['sx'] - g['ww'], cx + g['sx'] + g['ww']), (cz - g['sz'] - g['r'] - 10, cz + g['sz'] + g['r'] + 1), (0, 24.5))
        return box, solid, area
    for g, brace in ((GEAR_MAIN, False), (GEAR_NOSE, True)):
        box, solid, area = gear_fn(g, brace)
        c.put(box, solid, C_GEAR, area)
        c.mirror(box, C_GEAR)
    # --- the ramp: a grating from the floor's rear edge down to the ground, stripes across it
    r = RAMP
    def ramp(X, Y, Z):
        d = r['y'][0] + (Z - r['z'][0]) * (r['y'][1] - r['y'][0]) / (r['z'][1] - r['z'][0]) - Y
        return (X >= r['x'][0]) & (X < r['x'][1]) & (Z >= r['z'][0]) & (Z < r['z'][1]) & (d >= 0) & (d < r['t'])
    def ramp_area(X, Y, Z, m):
        return np.where(np.floor(Z / 1.5) % 2 == 0, CODES['ramp'], CODES['grate']).astype(np.uint8)
    c.put((r['x'], r['z'], r['y']), ramp, C_RAMP, ramp_area)
    return c


ROUNDEL = (246.0, 37.0, 4.5)     # Z, Y of its middle on the side, radius (refined by --measure)


# ---------------------------------------------------------------- the map, and which part holds what
def map_parts():
    b = open(os.path.join(DATA, 'MAPS', 'PLANE.MAP'), 'rb').read()
    ry, cx, hz = b[0], b[1], b[2]
    t = b[3:]
    out = []
    for tz in range(hz):
        fz = hz - 1 - tz
        for ty in range(ry):
            for tx in range(cx):
                o = ((fz * ry + ty) * cx + tx) * 4
                for slot in range(4):
                    if t[o + slot] >= OFFSET: out.append((tx, ty, tz, slot, t[o + slot] - OFFSET))
    return out


FLOOR, WEST, NORTH, OBJECT = 0, 1, 2, 3
WING_IDS = set(range(15, 41))


def holds(tx, ty, tz, slot, p):
    """The components a part holds (none: the game's own floors stay rule-built)."""
    if tz == 0: return {C_GEAR} if p in (0, 1) else {C_RAMP}
    if tz == 1:
        if p in (41, 42, 43, 44): return {C_POD}
        if slot == FLOOR: return set()
        return {C_FUS, C_CONE, C_CANOPY}
    if p in WING_IDS: return {C_WING}     # (the hull's shoulders beside them: level 1's pieces, bands())
    if p in (7, 8, 13, 14): return {C_FIN}
    return {C_FUS, C_CONE, C_CANOPY}      # the roof pieces


def bands(placed):
    """Per (tx, ty, tz, part): the heights it holds. Level 1's pieces hold the craft up to the top where the
    level above has no piece holding the fuselage (the arch's shoulders beside the roof, the nose)."""
    roof = {(tx, ty) for tx, ty, tz, slot, p in placed if tz == 2 and C_FUS in holds(tx, ty, tz, slot, p)}
    out = {}
    for tx, ty, tz, slot, p in placed:
        lo, hi = tz * 24.0, tz * 24.0 + 24.0
        if tz == 1 and (tx, ty) not in roof: hi = Y1
        if tz == 2: hi = Y1
        out[(tx, ty, tz, slot)] = (lo, hi)
    return out


# ---------------------------------------------------------------- colours by reference
def pixel_with(index, cache={}):
    if index not in cache:
        for p in range(65):
            try: g, _ = sprite(SET, p)
            except Exception: continue
            ys, xs = np.nonzero(g == index)
            if len(xs):
                cache[index] = (p, int(xs[0]), int(ys[0]))
                break
        else: raise SystemExit(f'colour {index} is in no {SET} sprite')
    return cache[index]


def speckle(area, i, j, k):
    h = (int(i) * 73856093 ^ int(j) * 19349663 ^ int(k) * 83492791) & 0xFFFF
    x = h % 100
    for index, share in AREAS[area]:
        if x < share: return pixel_with(index)
        x -= share
    return pixel_with(AREAS[area][0][0])


def write_piece(path, kgrid, refs, pans=None, normals=()):
    """The .vox and its .ref: VRF2 (colour references, no lights here), or VRF3 when some voxels belong to
    panels (terrain.22): the piece's own table of the panels it uses (normals in model axes: x east, y south,
    z up), then a fifth byte per voxel, 0 or 1 + its index there."""
    g = Grid(kgrid.shape[0], kgrid.shape[1], kgrid.shape[2], PER)
    g.k = kgrid
    g.write_vox(path + '.vox')
    sx, sy, sz = kgrid.shape
    lc = pixel_with(AREAS['red'][0][0])
    used = sorted(set(int(v) for v in np.unique(pans[kgrid > 0])) - {0}) if pans is not None else []
    local = {gid: i + 1 for i, gid in enumerate(used)}
    body = bytearray()
    count = 0
    for z in range(sz):
        for y in range(sy):
            for x in range(sx):
                if not kgrid[x, y, z]: continue
                r = refs[(x, y, z)]
                rec = (r[0], r[1], r[2], 0)
                if used: rec += (local.get(int(pans[x, y, z]), 0),)
                body += bytes(rec)
                count += 1
    if used:
        table = b''.join(struct.pack('<3f', *(normals[gid - 1][0], normals[gid - 1][2], normals[gid - 1][1])) for gid in used)
        out = bytearray(b'VRF3' + struct.pack('<f', 0.0)) + struct.pack('<I', count) + bytes((lc[0], lc[1], lc[2], 0)) \
            + struct.pack('<I', len(used)) + table + body
    else:
        out = bytearray(b'VRF2' + struct.pack('<f', 0.0)) + struct.pack('<I', count) + bytes((lc[0], lc[1], lc[2], 0)) + body
    open(path + '.ref', 'wb').write(bytes(out))
    return count


# ---------------------------------------------------------------- panels (terrain.22)
# Each flat panel of the faceted hull lit as one plate: its voxels' faces take the panel's direction (VRF3), so
# the steps of a sloped panel don't show as lines (each step's lit top and darker side). Panels: per stretch of
# the hull along Z (the constant middle, then the nose's stretches between its knots), per side, the cross-
# section's segments (CROSS) plus the flat bottom and top. Their directions come from the shape itself (the
# gradient of fus_faceted's boundary at the panel's middle).
PANEL_LIGHT = True


def panels(c):
    """Per cell: 0, or 1 + the index of its panel in the returned list of normals (craft axes X, Y up, Z)."""
    zk = sorted(set([FUS_Z[0]] + [z for z, _ in NOSE_W] + [z for z, _ in NOSE_T] + [z for z, _ in NOSE_B]))
    zsegs = list(zip(zk[:-1], zk[1:]))                       # 112-256, 256-272, 272-284, 284-292, 292-298
    yb = lambda Z: np.interp(Z, *zip(*NOSE_B)); yt = lambda Z: np.interp(Z, *zip(*NOSE_T))
    ww = lambda Z: np.interp(Z, *zip(*NOSE_W))
    def ynorm(Y, Z): return FUS['y0'] + (Y - yb(Z)) / max(yt(Z) - yb(Z), 0.5) * (FUS['yt'] - FUS['y0'])
    def side_f(X, Y, Z):                                     # > 0 outside, across the side (|X - 80| - half-width)
        return abs(X - CXM) - np.interp(ynorm(Y, Z), *zip(*CROSS)) * ww(Z) / FUS['w']
    def grad(f, P, e=0.05):
        g = np.array([(f(*(P + d)) - f(*(P - d))) / (2 * e) for d in np.eye(3) * e])
        return g / np.linalg.norm(g)
    normals, regions = [], []                                # region: (z0, z1, sign, yn0, yn1, point, normal)
    for z0, z1 in zsegs:
        Zm = (z0 + z1) / 2
        for sign in (-1, 1):
            for (y0n, h0), (y1n, h1) in zip(CROSS[:-1], CROSS[1:]):
                ynm = (y0n + y1n) / 2
                Y = yb(Zm) + (ynm - FUS['y0']) / (FUS['yt'] - FUS['y0']) * (yt(Zm) - yb(Zm))
                X = CXM + sign * np.interp(ynm, *zip(*CROSS)) * ww(Zm) / FUS['w']
                P = np.array([X, Y, Zm])
                n = grad(side_f, P)
                normals.append(n); regions.append((z0, z1, sign, y0n, y1n, P, n, len(normals)))
        # the flat bottom and top (shared by both sides)
        for which, yn in (('bottom', FUS['y0']), ('top', FUS['yt'])):
            f = (lambda X, Y, Z: yb(Z) - Y) if which == 'bottom' else (lambda X, Y, Z: Y - yt(Z))
            P = np.array([CXM, yb(Zm) if which == 'bottom' else yt(Zm), Zm])
            n = grad(f, P)
            normals.append(n); regions.append((z0, z1, 0, yn - 0.5, yn + 0.5, P, n, len(normals)))
    sl, outer = c.fus_outer
    solid = np.zeros(c.comp.shape, bool); solid[sl] = outer
    skin = exposed(np.isin(c.comp, (C_FUS, C_CONE)), (0, 1, 2), ~solid) & (GZ[None, :, None] <= NOSE['zt'])
    skin &= ~((c.comp == C_CONE) & ~solid)                  # the beak beyond the body: its own panels (below)
    pan = np.zeros(c.comp.shape, np.uint8)
    si, sk, sj = np.nonzero(skin)
    for i, k, j in zip(si, sk, sj):
        Pv = np.array([GX[i], GY[j], GZ[k]])
        yn = ynorm(Pv[1], Pv[2])
        best, bd = 0, 1e9
        for z0, z1, sign, a0, a1, P0, n, gid in regions:
            if not (z0 <= Pv[2] <= z1): continue
            if sign and np.sign(Pv[0] - CXM) != sign: continue
            if not (a0 - 2.5 <= yn <= a1 + 2.5): continue
            d = abs(np.dot(Pv - P0, n))
            if d < bd: best, bd = gid, d
        pan[i, k, j] = best
    # the cockpit hood (HOOD): its roof follows the nose's top (that stretch's top panel); its eight sloping sides
    # each one panel (the direction at the side's middle)
    if FACETED:
        h = HOOD
        def hood_f(X, Y, Z):                                 # > 0 above the hood's surface
            L = float(hood_L(np.array(X), np.array(Z))[0])
            return Y - (yt(Z) + h['lift'] * np.clip((1 - L) / (1 - h['roof']), 0, 1))
        def octant(X, Z):
            ax, az = (X - CXM) / h['rx'], (Z - h['z']) / h['rz']
            terms = [abs(ax), abs(az), (abs(ax) + abs(az)) / 1.4]
            t = int(np.argmax(terms))
            return (t, int(np.sign(ax)) if t != 1 else 0, int(np.sign(az)) if t != 0 else 0)
        side_ids = {}
        mid = (1 + h['roof']) / 2
        for t, sx_, sz_ in [(0, -1, 0), (0, 1, 0), (1, 0, -1), (1, 0, 1), (2, -1, -1), (2, -1, 1), (2, 1, -1), (2, 1, 1)]:
            if t == 0: X, Z = CXM + sx_ * mid * h['rx'], h['z']
            elif t == 1: X, Z = CXM, h['z'] + sz_ * mid * h['rz']
            else: X, Z = CXM + sx_ * mid * 0.7 * h['rx'], h['z'] + sz_ * mid * 0.7 * h['rz']
            Y = yt(Z) + h['lift'] * 0.5
            normals.append(grad(hood_f, np.array([X, Y, Z])))
            side_ids[(t, sx_, sz_)] = len(normals)
        top_of = {(z0, z1): gid for z0, z1, sign, a0, a1, P0, n, gid in regions if sign == 0 and a0 > 40}
        hood = exposed(c.comp == C_CANOPY, (0, 1, 2), c.comp == 0)
        hi, hk, hj = np.nonzero(hood)
        for i, k, j in zip(hi, hk, hj):
            X, Y, Z = GX[i], GY[j], GZ[k]
            L = float(hood_L(np.array(X), np.array(Z))[0])
            if L < h['roof']:
                pan[i, k, j] = next((g for (z0, z1), g in top_of.items() if z0 <= Z <= z1), 0)
            else:
                pan[i, k, j] = side_ids.get(octant(X, Z), 0)
        # the beak (BEAK): its eight faces per stretch along Z, the flat bottom one more
        b = BEAK
        def beak_f(X, Y, Z):                                 # > 0 outside
            t = np.clip((Z - b['zs']) / (b['zt'] - b['zs']), 0, 0.999)
            R = b['r'] * (1 - t ** b['p']) ** (1 / b['p'])
            dx, dy = abs(X - CXM), abs(Y - b['yc'])
            return max(dx, dy, (dx + dy) / 1.414) - R
        zs_ = [b['zs'], b['zs'] + 3.0, b['zs'] + 6.0, b['zt']]
        beak_ids = {}
        for z0, z1 in zip(zs_[:-1], zs_[1:]):
            zm = (z0 + z1) / 2
            t = np.clip((zm - b['zs']) / (b['zt'] - b['zs']), 0, 0.999)
            R = b['r'] * (1 - t ** b['p']) ** (1 / b['p'])
            for face in [(0, -1, 0), (0, 1, 0), (1, 0, -1), (1, 0, 1), (2, -1, -1), (2, -1, 1), (2, 1, -1), (2, 1, 1)]:
                tt, sx_, sy_ = face
                if tt == 0: X, Y = CXM + sx_ * R * 0.98, b['yc']
                elif tt == 1: X, Y = CXM, b['yc'] + sy_ * R * 0.98
                else: X, Y = CXM + sx_ * R * 0.69, b['yc'] + sy_ * R * 0.69
                normals.append(grad(beak_f, np.array([X, Y, zm])))
                beak_ids[(z0, face)] = len(normals)
        normals.append(np.array([0.0, -1.0, 0.0])); beak_bottom = len(normals)
        beak = exposed((c.comp == C_CONE) & ~solid, (0, 1, 2), c.comp == 0)
        bi, bk, bj = np.nonzero(beak)
        for i, k, j in zip(bi, bk, bj):
            X, Y, Z = GX[i], GY[j], GZ[k]
            if Y < b['ybot'] + 0.5: pan[i, k, j] = beak_bottom; continue
            dx, dy = X - CXM, Y - b['yc']
            terms = [abs(dx), abs(dy), (abs(dx) + abs(dy)) / 1.414]
            tt = int(np.argmax(terms))
            face = (tt, int(np.sign(dx)) if tt != 1 else 0, int(np.sign(dy)) if tt != 0 else 0)
            z0 = max(z for z in zs_[:-1] if z <= max(Z, zs_[0]))
            pan[i, k, j] = beak_ids.get((z0, face), 0)
    print(f'  panels: {len(normals)} over {len(zsegs)} stretches and the canopy; {int((pan > 0).sum())} skin cells')
    return pan, normals


# ---------------------------------------------------------------- the art's texture, straightened (terrain.19)
# The owner's choices from the texture study (vr-dev/HANDOFF.md): the art's own shading kept; one colour per
# table voxel from the art pixel at the cell's middle, the cell grid's phase chosen for the straightest rows;
# the dither redrawn as a checkerboard at the art's pixel size on each face's own grid (the fins kept as
# drawn, as approved); panel lines joined into solid lines. Markings, glass, the cone, gear and ramp stay
# coloured by area. Hidden sides take the visible side mirrored (X -> 160 - X); what neither shows takes the
# nearest textured voxel.
from straighten import Canvas, proj, resolve, straightness
FUS_COMPS = (C_FUS, C_CONE, C_CANOPY)


def near_roundel(y, z):
    """Within two voxels of the roundel on the sides: the art draws a pale outline round it there, which
    taken as texture left whitish cells round the disc (owner, terrain.20); those cells take their neighbours'."""
    rz, ry, rr = ROUNDEL
    return np.hypot(z - rz, y - ry) <= rr + 2.0


def hull_grey(index):
    """The hulls' gunmetal ramp (240 lit .. 255): other colours under the hull (the canopy's glass seen
    through, markings, the cockpit's consoles) aren't texture; those cells take their neighbours'."""
    return int(index) // 16 == 15


def owners(c, comps):
    """The game's view of the given components: per canvas pixel, the nearest cell (its centre's pixel)."""
    from straighten import W as CW, H as CH
    full = np.nonzero(np.isin(c.comp, comps))
    X, Z, Y = GX[full[0]], GZ[full[1]], GY[full[2]]
    px, py = proj(X, Y, Z)
    px, py = np.floor(px).astype(int), np.floor(py).astype(int)
    o = np.argsort(X + Y + Z)
    who = np.full((CH, CW), -1, np.int64)
    who[py[o], px[o]] = o
    return who, np.stack([X, Y, Z], -1)


def seen_at(who, P, point, tol=1.2):
    """The pixel showing point, if the cell drawn there is the point's own (within tol)."""
    u, v = proj(*point)
    n = who[int(np.floor(v)), int(np.floor(u))]
    if n < 0 or np.linalg.norm(P[n] - np.array(point)) > tol: return None
    return int(np.floor(v)), int(np.floor(u))


def exposed(m, axes=(0, 1, 2), outside=None):
    """Cells of m with a face neighbour in outside (default: not m) along the given axes."""
    if outside is None: outside = ~m
    t = np.zeros_like(m)
    for ax in axes:
        for s_ in (1, -1):
            sh = np.roll(outside, s_, axis=ax)
            idx = [slice(None)] * 3
            idx[ax] = 0 if s_ == 1 else -1
            sh[tuple(idx)] = False
            t |= sh
    return m & t


def tex_fuselage(c, placed, pidx):
    """The fuselage unrolled: per table-voxel slab along Z, the skin's cells round the cross-section in order
    (s), each the art at its middle; the checkerboard by (slab + s)."""
    art = Canvas(SET, placed, lambda *t: bool(holds(*t) & set(FUS_COMPS)))
    who, P = owners(c, FUS_COMPS)
    sl, outer = c.fus_outer
    solid = np.zeros(c.comp.shape, bool); solid[sl] = outer
    hull = (c.comp == C_FUS) & (c.area == CODES['hull'])
    skin = exposed(hull, (0, 2), ~solid)                    # faces out across the section (not the ends)
    si, sk, sj = np.nonzero(skin)
    SXs, SZs, SYs = GX[si], GZ[sk], GY[sj]
    best = None
    for phz in (0.0, 0.5):
        slab = np.floor(SZs - phz).astype(int)
        for phx in (0.0, 0.5):
            for phy in (0.0, 0.5):
                cx_, cy_ = np.floor(SXs - phx).astype(int), np.floor(SYs - phy).astype(int)
                T, order = {}, {}
                for a in np.unique(slab):
                    m = slab == a
                    cells = {}
                    for x, y, kx, ky in zip(SXs[m], SYs[m], cx_[m], cy_[m]):
                        cells.setdefault((kx, ky), []).append((x, y))
                    yc = SYs[m].mean()
                    keys = sorted(cells, key=lambda k: np.mean([np.arctan2(y - yc, x - CXM) for x, y in cells[k]]))
                    row = []
                    for k in keys:
                        x, y = np.mean(cells[k], axis=0)
                        if x < CXM: x = 2 * CXM - x
                        q = seen_at(who, P, (x, y, a + phz + 0.5))
                        if near_roundel(y, a + phz + 0.5): q = None      # its outline isn't hull (owner)
                        row.append(art.value(q) if q is not None and art.ok[q] and hull_grey(art.col[q]) else None)
                    good = [i for i, v in enumerate(row) if v is not None]
                    if good:
                        row = [v if v is not None else row[min(good, key=lambda g: abs(g - i))] for i, v in enumerate(row)]
                    T[a] = row; order[a] = {k: i for i, k in enumerate(keys)}
                score = sum(sum(1 for u, v in zip(r[:-1], r[1:]) if u is not None and u == v) for r in T.values())
                score += sum(sum(1 for u, v in zip(T[a], T[a + 1]) if u is not None and u == v) for a in T if a + 1 in T and len(T[a]) == len(T[a + 1]))
                if best is None or score > best[0]: best = (score, phx, phy, phz, T, order)
    score, phx, phy, phz, T, order = best
    # lone pale cells (owner, terrain.20: whitish blocks by the roundel): a cell 4+ entries lighter than every
    # neighbour round it takes their middle
    def lv(v): return v if isinstance(v, int) else sum(v) / 2
    fixed = 0
    for a in T:
        row = T[a]
        for s_, v in enumerate(row):
            if v is None: continue
            nb = [row[t] for t in (s_ - 1, s_ + 1) if 0 <= t < len(row)]
            for b in (a - 1, a + 1):
                if b in T and len(T[b]) == len(row): nb.append(T[b][s_])
            nb = [lv(n) for n in nb if n is not None]
            if len(nb) >= 3 and all(lv(v) <= n - 4 for n in nb):
                row[s_] = int(round(np.median(nb))); fixed += 1
    print(f'  fuselage: phase {phx} {phy} {phz}, {len(T)} slabs, {fixed} lone pale cells evened')
    hi, hk, hj = np.nonzero(hull)
    for i, k, j in zip(hi, hk, hj):
        a = int(np.floor(GZ[k] - phz)); key = (int(np.floor(GX[i] - phx)), int(np.floor(GY[j] - phy)))
        if a in order and key in order[a]:
            s_ = order[a][key]; v = T[a][s_]
            if v is not None: pidx[i, k, j] = resolve(v, a + s_)


def plane_cells(art, coords, sample, phases):
    """A flat face's texture: cells of one table voxel over the face's two axes (coords: per voxel (u, v)),
    each sample(u_mid, v_mid) -> art pixel; the phase with the straightest rows. Returns per voxel (value,
    parity)."""
    best = None
    for pu in phases:
        for pv in phases:
            cu = np.floor(coords[:, 0] - pu).astype(int); cv = np.floor(coords[:, 1] - pv).astype(int)
            keys = sorted(set(zip(cu, cv)))
            vals = {}
            for ku, kv in keys:
                q = sample(ku + pu + 0.5, kv + pv + 0.5)
                vals[(ku, kv)] = q
            u0, v0 = min(k[0] for k in keys), min(k[1] for k in keys)
            G = np.full((max(k[0] for k in keys) - u0 + 1, max(k[1] for k in keys) - v0 + 1), None, object)
            for (ku, kv), q in vals.items():
                if q is not None: G[ku - u0, kv - v0] = int(art.col[q])
            sc = straightness(G)
            if best is None or sc > best[0]: best = (sc, cu, cv, vals, (pu, pv))
    return best[1], best[2], best[3], best[4]


ROOT_SHADE = 12.0    # the wings' last 12 voxels at the hull: the art paints the fuselage's shadow there


def find_lines(cells, skip, dark=248, minrun=6, share=0.45):
    """Panel lines in a face's cells: along a row (axis 1: fixed Z, along X) or a column (axis 0: fixed X,
    along Z), the darkest greys (>= dark) making up a broken line of at least minrun cells, dark on at least
    share of them. Cells where skip(key) are left out (the painted shadow at the root). Returns (axis, line,
    lo, hi, the line's usual dark grey)."""
    out = []
    for axis in (0, 1):
        lines = {}
        for k, v in cells.items():
            if isinstance(v, int) and v >= dark and not skip(k): lines.setdefault(k[axis], []).append((k[1 - axis], v))
        for line, pv in lines.items():
            pos = [p_ for p_, _ in pv]
            lo, hi = min(pos), max(pos)
            if len(pos) < minrun // 2 or hi - lo + 1 < minrun or len(pos) / (hi - lo + 1) < share: continue
            vals = [v for _, v in pv]
            out.append((axis, line, lo, hi, max(set(vals), key=vals.count)))
    return out


def tex_wings(c, placed, pidx):
    """The main wings' tops (the front wings are the same parts): cells over X and Z on one grid for both
    wings, the art at the top's plane, the checkerboard by cell. Owner (terrain.20): panel lines joined into
    solid lines of their usual grey, only where both wings have them (the craft is symmetric; the art's root
    shadow made extra lines on one side) and only as far as both reach; the painted shadow at the root
    (greys darker than the wing's gradient, ROOT_SHADE from the hull) replaced by the gradient carried on."""
    art = Canvas(SET, placed, lambda tx, ty, tz, s_, p: tz == 2 and 7 <= ty <= 10 and p in WING_IDS)
    yt = WING['y'][1]
    m = (c.comp == C_WING) & (c.area == CODES['hull'])
    m[:, int((176 - Z0) * PER):] = False                     # the main wings only (the front ones are copies)
    wi, wk, wj = np.nonzero(m)
    coords = np.stack([GX[wi], GZ[wk]], -1)
    cu, cv, vals, (px, _) = plane_cells(art, coords, lambda u, v: art.pixel(u, yt, v), (0.0, 0.5))
    mirror = lambda ku: int(round(2 * CXM - 1 - 2 * px)) - ku     # the cell across the craft's middle
    cells = {k: (art.value(q) if q is not None else None) for k, q in vals.items()}
    root = WING['root']
    left = lambda k: k[0] + px < CXM
    def shade(k):                                          # within ROOT_SHADE of the hull
        x = k[0] + px + 0.5
        return (x > root - ROOT_SHADE) if x < CXM else (x < 2 * CXM - root + ROOT_SHADE)
    # panel lines on both wings (the right one's in the left's terms)
    L = [l for l in find_lines({k: v for k, v in cells.items() if left(k)}, shade)]
    R = []
    for axis, line, lo, hi, col in find_lines({k: v for k, v in cells.items() if not left(k)}, shade):
        if axis == 0: R.append((0, mirror(line), lo, hi, col))
        else: R.append((1, line, mirror(hi), mirror(lo), col))
    pairs = []                                             # each line pairs with at most one on the other wing
    for ln1 in L:
        for ln2 in R:
            a_, l1, lo1, hi1, c1 = ln1; a2, l2, lo2, hi2, c2 = ln2
            lo, hi = max(lo1, lo2), min(hi1, hi2)
            if a_ != a2 or abs(l1 - l2) > 1 or hi - lo + 1 < 0.5 * min(hi1 - lo1 + 1, hi2 - lo2 + 1): continue
            pairs.append((abs(l1 - l2), -(hi - lo), ln1, ln2, lo, hi))
    joined, used = [], set()
    for _, _, ln1, ln2, lo, hi in sorted(pairs, key=lambda t: t[:2]):
        if ln1 in used or ln2 in used: continue
        used.add(ln1); used.add(ln2)
        joined.append((ln1[0], ln1[1], ln2[1], lo, hi, min(ln1[4], ln2[4])))   # the subtler of the two (owner)
    # lines only one wing has (the art's marks round the root's shadow): their dark cells go (below)
    stray = set()
    for side, lst in ((True, L), (False, R)):
        for ln in lst:
            if ln in used: continue
            a_, l_, lo, hi, _ = ln
            for p_ in range(lo, hi + 1):
                k = (l_, p_) if a_ == 0 else (p_, l_)
                if not side: k = (mirror(k[0]), k[1])
                v = cells.get(k)
                if isinstance(v, int) and v >= 248: stray.add(k)
    seam = set()
    for a_, l1, l2, lo, hi, col in joined:
        for p_ in range(lo, hi + 1):
            for k in (((l1, p_), (mirror(l2), p_)) if a_ == 0 else ((p_, l1), (mirror(p_), l2))):
                if k in cells: cells[k] = col; seam.add(k)
    print(f'  wings: phase X {px}; panel lines on both wings: {len(joined)} (left {len(L)}, right {len(R)}); '
          f'{len(stray)} cells of one-sided lines')
    # the root's painted shadow: the gradient carried on (per column, the mean of its lighter cells)
    def light(v): return v if isinstance(v, int) else sum(v) / 2
    for side in (True, False):
        cols = {}
        for k, v in cells.items():
            if v is None or left(k) != side or k in seam or k in stray or shade(k): continue
            cols.setdefault(k[0], []).append(light(v))
        xs = sorted(cols); means = [np.mean(cols[x]) for x in xs]
        near = xs[-10:] if side else xs[:10]                 # the ten columns nearest the root
        fit = np.polyfit(near, [means[xs.index(x)] for x in near], 1)
        mean = dict(zip(xs, means))
        for k, v in list(cells.items()):
            if v is None or left(k) != side or k in seam: continue
            if shade(k): g = min(np.polyval(fit, k[0]), 249.0)
            elif k in stray: g = mean.get(k[0], light(v))
            else: continue
            if light(v) > g + 0.75:                           # darker than the gradient: shadow or a stray mark
                lo_, f = int(np.floor(g)), g - np.floor(g)
                cells[k] = lo_ if f < 0.25 else (lo_ + 1 if f > 0.75 else (lo_, lo_ + 1))
    for i, k, j, ku, kv in zip(wi, wk, wj, cu, cv):
        v = cells.get((ku, kv))
        if v is not None: pidx[i, k, j] = resolve(v, ku + kv)


def tex_fins(c, placed, pidx):
    """Each fin face: cells over Z and Y on its plane, the art as drawn (owner, the fin study). The art shows
    the left fin's inner face and the right fin's outer face; the other two are their mirror images."""
    art = Canvas(SET, placed, lambda tx, ty, tz, s_, p: p in (7, 8, 13, 14))
    f = FIN
    m = c.comp == C_FIN
    fi, fk, fj = np.nonzero(m)
    coords = np.stack([GZ[fk], GY[fj]], -1)
    faces = {}
    for name, xs in (('inner', f['x'] + f['t']), ('outer', 2 * CXM - f['x'])):
        cu, cv, vals, _ = plane_cells(art, coords, lambda u, v, xs=xs: art.pixel(xs, v, u), (0.0, 0.5))
        faces[name] = (cu, cv, vals)
    for n_, (i, k, j) in enumerate(zip(fi, fk, fj)):
        x = GX[i]
        left = x < CXM
        outer_face = (x < f['x'] + f['t'] / 2) if left else (x > 2 * CXM - f['x'] - f['t'] / 2)
        cu, cv, vals = faces['outer' if outer_face else 'inner']
        q = vals.get((cu[n_], cv[n_]))
        if q is not None: pidx[i, k, j] = int(art.col[q])


def tex_cells(c, placed, pidx, comp, area, parts):
    """Box-like pieces (the pods): one colour per table voxel, the art where the piece's surface in that cell
    is seen (the hidden side mirrored), the checkerboard by cell."""
    art = Canvas(SET, placed, lambda tx, ty, tz, s_, p: p in parts)
    who, P = owners(c, (comp,))
    m = (c.comp == comp) & (c.area == CODES[area])
    ex = exposed(c.comp == comp) & m
    ei, ek, ej = np.nonzero(ex)
    cells = {}
    for i, k, j in zip(ei, ek, ej):
        cells.setdefault((int(np.floor(GX[i])), int(np.floor(GZ[k])), int(np.floor(GY[j]))), []).append((GX[i], GY[j], GZ[k]))
    vals = {}
    for key, pts in cells.items():
        x, y, z = np.mean(pts, axis=0)
        if x < CXM: x = 2 * CXM - x
        q = seen_at(who, P, (x, y, z), 1.5)
        if q is not None and art.ok[q] and hull_grey(art.col[q]): vals[key] = art.value(q)
    mi, mk, mj = np.nonzero(m)
    for i, k, j in zip(mi, mk, mj):
        key = (int(np.floor(GX[i])), int(np.floor(GZ[k])), int(np.floor(GY[j])))
        if key in vals: pidx[i, k, j] = resolve(vals[key], sum(key))


def glass_colour(index):
    """The canopy's glass in the art: its blues and the pale highlight on them."""
    return 128 <= index < 144 or 224 <= index <= 228


def roof_colour(index):
    """The cockpit's roof: the hull's greys (not the art's white highlight along its edge: a white patch)."""
    return hull_grey(index)


def yellow_colour(index):
    return 16 <= index < 32


def tex_panels(c, placed, pidx, sel, accept, name, even=False):
    """Each panel's cells (c.panel) among sel straightened like a flat face (terrain.23: the nose, the cockpit and
    the beak, whose shapes the unrolled hull doesn't fit): cells of one table voxel on the panel's own grid (u
    across, v up the panel), the art at each cell's middle where the game's view shows that point (the hidden
    side mirrored) and accept() takes the colour; the phase with the straightest rows; cells the art doesn't
    give take the nearest given cell of their panel; the checkerboard by cell. even: a cell 2+ entries off the
    middle of the 3 x 3 cells round it takes that middle (the art's irregular dark noise on the nose's lower
    sides came out as blotches; owner, terrain.23: the front's texture to look better)."""
    art = Canvas(SET, placed, lambda *t: bool(holds(*t) & set(FUS_COMPS)))
    who, P = owners(c, (C_FUS, C_CONE, C_CANOPY, C_WING, C_GEAR))
    normals = c.panel_normals
    done = 0
    for g in sorted(set(int(v) for v in np.unique(c.panel[sel])) - {0}):
        ii, kk, jj = np.nonzero(sel & (c.panel == g))
        n = np.asarray(normals[g - 1], float)
        up = np.array([0.0, 1.0, 0.0])
        u = np.cross(up, n) if abs(n[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
        u /= np.linalg.norm(u); v = np.cross(n, u); v /= np.linalg.norm(v)
        Pc = np.stack([GX[ii], GY[jj], GZ[kk]], -1)
        cu_, cv_ = Pc @ u, Pc @ v
        best = None
        for pu in (0.0, 0.5):
            for pv in (0.0, 0.5):
                ku, kv = np.floor(cu_ - pu).astype(int), np.floor(cv_ - pv).astype(int)
                vals, cols = {}, {}
                for key in set(zip(ku, kv)):
                    m = (ku == key[0]) & (kv == key[1])
                    x, y, z = Pc[m].mean(0)
                    if x < CXM: x = 2 * CXM - x
                    q = seen_at(who, P, (x, y, z), 1.2)
                    if q is not None and art.ok[q] and accept(int(art.col[q])):
                        vals[key] = art.value(q); cols[key] = int(art.col[q])
                if not cols: continue
                u0, v0 = min(k[0] for k in cols), min(k[1] for k in cols)
                G = np.full((max(k[0] for k in cols) - u0 + 1, max(k[1] for k in cols) - v0 + 1), None, object)
                for (a, b_), col_ in cols.items(): G[a - u0, b_ - v0] = col_
                sc = straightness(G) + 0.001 * len(cols)
                if best is None or sc > best[0]: best = (sc, ku, kv, vals)
        if best is None: continue
        _, ku, kv, vals = best
        if even:
            lv = lambda v: v if isinstance(v, int) else sum(v) / 2
            new = {}
            for (a, b_), v in vals.items():
                nb = [lv(vals[(a + da, b_ + db)]) for da in (-1, 0, 1) for db in (-1, 0, 1) if (a + da, b_ + db) in vals]
                if len(nb) < 5: continue
                med = float(np.median(nb))
                if abs(lv(v) - med) >= 2:
                    lo_, f = int(np.floor(med)), med - np.floor(med)
                    new[(a, b_)] = lo_ if f < 0.25 else (lo_ + 1 if f > 0.75 else (lo_, lo_ + 1))
            vals.update(new)
        have = list(vals)
        for a, b_, i, k, j in zip(ku, kv, ii, kk, jj):
            key = (a, b_)
            if key not in vals:
                key = min(have, key=lambda h: (h[0] - a) ** 2 + (h[1] - b_) ** 2)
            pidx[i, k, j] = resolve(vals[key], a + b_)
        done += len(ii)
    print(f'  {name}: {done} cells textured panel by panel')


def texture(c, placed):
    """Per cell: a palette index from the art (0: colour by area)."""
    import scipy.ndimage as nd
    pidx = np.zeros(c.comp.shape, np.uint8)
    tex_fuselage(c, placed, pidx)
    tex_wings(c, placed, pidx)
    tex_fins(c, placed, pidx)
    tex_cells(c, placed, pidx, C_POD, 'pod', (41, 42, 43, 44))
    if FACETED and getattr(c, 'panel', None) is not None:
        # terrain.23: the nose (from Z 256, where it tapers), the cockpit and the beak panel by panel
        Zc = GZ[None, :, None]
        hull = (c.comp == C_FUS) & (c.area == CODES['hull'])
        nose = hull & (Zc >= FUS_Z[1]) & (c.panel > 0)
        pidx[nose] = 0
        tex_panels(c, placed, pidx, nose, hull_grey, 'nose', even=True)
        tex_panels(c, placed, pidx, (c.comp == C_CANOPY) & (c.area == CODES['glass']) & (c.panel > 0), glass_colour, 'canopy glass')
        tex_panels(c, placed, pidx, (c.comp == C_CANOPY) & (c.area == CODES['hull']) & (c.panel > 0), roof_colour, 'canopy roof')
        tex_panels(c, placed, pidx, (c.comp == C_CONE) & (c.area == CODES['yellow']) & (c.panel > 0), yellow_colour, 'beak')
    # what no view textured (undersides, ends, behind other pieces): the nearest textured cell of its piece
    for comp, areas in ((C_FUS, ('hull',)), (C_WING, ('hull',)), (C_POD, ('pod',)), (C_CANOPY, ('glass',)),
                        (C_CANOPY, ('hull',)), (C_CONE, ('yellow',))):
        want = (c.comp == comp) & np.isin(c.area, [CODES[a] for a in areas])
        have = want & (pidx > 0)
        if not have.any(): continue
        miss = want & ~have
        _, ind = nd.distance_transform_edt(~have, return_indices=True)
        mi = np.nonzero(miss)
        pidx[mi] = pidx[ind[0][mi], ind[1][mi], ind[2][mi]]
    # the front wings: the main wings' outer tiles moved (as build() copies them)
    for (sx0, dx) in ((0, 32), (128, -32)):
        i0, i1, di = int(sx0 * PER), int((sx0 + 32) * PER), int(dx * PER)
        k0, k1, dk = int((112 - Z0) * PER), int((160 - Z0) * PER), int(112 * PER)
        src = pidx[i0:i1, k0:k1]; dst = pidx[i0 + di:i1 + di, k0 + dk:k1 + dk]
        sel = (c.comp[i0 + di:i1 + di, k0 + dk:k1 + dk] == C_WING) & (src > 0)
        dst[sel] = src[sel]
    return pidx


def piece(c, tx, ty, tz, comps, band):
    """The part's model: the craft's cells in its tile's column, its components, its heights."""
    T = 16 * PER
    i0, k0 = (tx * 16 - X0) * PER, (ty * 16 - Z0) * PER
    j0, j1 = int((band[0] - Y0) * PER), int((band[1] - Y0) * PER)
    if k0 < 0 or k0 + T > NZ: return None
    cc = c.comp[i0:i0 + T, k0:k0 + T, j0:j1]
    aa = c.area[i0:i0 + T, k0:k0 + T, j0:j1]
    pp = c.pidx[i0:i0 + T, k0:k0 + T, j0:j1]
    pn = c.panel[i0:i0 + T, k0:k0 + T, j0:j1]
    sel = np.isin(cc, list(comps))
    if not sel.any(): return None
    top = int(np.nonzero(sel.any(axis=(0, 1)))[0].max()) + 1
    kg = np.zeros((T, T, top + 1), np.uint8)
    kg[:, :, 1:] = np.where(sel[:, :, :top], PLAIN, 0)
    areas = np.zeros((T, T, top + 1), np.uint8)
    areas[:, :, 1:] = np.where(sel[:, :, :top], aa[:, :, :top], 0)
    pal_ = np.zeros((T, T, top + 1), np.uint8)
    pal_[:, :, 1:] = np.where(sel[:, :, :top], pp[:, :, :top], 0)
    pan_ = np.zeros((T, T, top + 1), np.uint8)
    pan_[:, :, 1:] = np.where(sel[:, :, :top], pn[:, :, :top], 0)
    return kg, (areas, pal_, pan_), (i0, k0, j0 - 1)


def main():
    os.makedirs(OUT, exist_ok=True)
    placed = map_parts()
    c = build()
    c.panel, c.panel_normals = panels(c) if PANEL_LIGHT else (np.zeros(c.comp.shape, np.uint8), [])
    c.pidx = texture(c, placed) if ART_TEXTURE else np.zeros(c.comp.shape, np.uint8)
    counts = {NAMES[a]: int((c.area == a).sum()) for a in np.unique(c.area) if a}
    print('craft:', int((c.comp > 0).sum()), 'cells;', counts)
    bnd = bands(placed)
    # a part the map uses in several places gets one model: the shape it has most often there (the roof
    # pieces repeat from the rear to the cockpit; the rear one also holds the end's top)
    places = {}
    for tx, ty, tz, slot, p in placed:
        comps = holds(tx, ty, tz, slot, p)
        if not comps: continue
        r = piece(c, tx, ty, tz, comps, bnd[(tx, ty, tz, slot)])
        if r is not None: places.setdefault(p, []).append(((tx, ty, tz, slot), r))
    for p, lst in sorted(places.items()):
        keys = [r[0].tobytes() + bytes(r[0].shape) for _, r in lst]
        best = max(keys, key=lambda k: (keys.count(k), -keys.index(k)))   # a tie: its first place (the main wings)
        (tx, ty, tz, slot), (kg, (areas, pals, pans), (i0, k0, jb)) = lst[keys.index(best)]
        for (where, r), key in zip(lst, keys):
            if key != best:
                n = min(r[0].shape[2], kg.shape[2])
                print(f'  part {p} at {where[:3]} differs from its model by {int((r[0][:, :, :n] != kg[:, :, :n]).sum())} cells')
        refs = {}
        for x, y, z in zip(*np.nonzero(kg)):
            refs[(x, y, z)] = pixel_with(int(pals[x, y, z])) if pals[x, y, z] else speckle(NAMES[areas[x, y, z]], i0 + x, k0 + y, jb + z)
        n = write_piece(os.path.join(OUT, f'{SET}_{p}'), kg, refs, pans, c.panel_normals)
        print(f'  {SET}_{p} ({tx},{ty},{tz}) slot {slot}: {n} voxels, {kg.shape[2]} layers')
    return c


if __name__ == '__main__':
    main()
