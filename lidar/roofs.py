import glob, json, math, os, sys
import numpy as np
from scipy import ndimage
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.dirname(HERE)
Image.MAX_IMAGE_PIXELS = None
LAT0, LAT1, LON0, LON1 = 25.758, 25.800, -80.205, -80.115
UNIT = 2.8
KX = 111320 * math.cos(math.radians((LAT0 + LAT1) / 2)) / UNIT
KY = 110950 / UNIT
WX = int((LON1 - LON0) * KX) + 1
WY = int((LAT1 - LAT0) * KY) + 1
CELL = 0.4
GCELL = 4.0
MIN_PART = 60
MIN_STEP = 9.0

top = np.load(os.path.join(HERE, "top.npy"))
ground = np.load(os.path.join(HERE, "ground.npy"))
H, W = top.shape
gmask = np.isnan(ground)
near = ndimage.distance_transform_edt(gmask, return_distances=False, return_indices=True)
ground = ground[near[0], near[1]]
ground = ndimage.median_filter(ground, 5)
photo = np.asarray(Image.open(os.path.join(BUILD, "naip", "miami.png")).convert("RGB"))
PH, PW = photo.shape[:2]


def ring_of(coords):
    r = [((c[0] - LON0) * KX, (c[1] - LAT0) * KY) for c in coords]
    if len(r) > 1 and r[0] == r[-1]:
        r = r[:-1]
    return r


def area(poly):
    s = 0
    for i in range(len(poly)):
        x0, y0 = poly[i]; x1, y1 = poly[(i + 1) % len(poly)]
        s += x0 * y1 - x1 * y0
    return s / 2


def pip(poly, x, y):
    c = False
    for k in range(len(poly)):
        ax, ay = poly[k]; bx, by = poly[k - 1]
        if (ay > y) != (by > y) and x < ax + (y - ay) / (by - ay) * (bx - ax):
            c = not c
    return c


def inner(poly):
    a = area(poly) or 1e-9
    cx = cy = 0
    for k in range(len(poly)):
        x0, y0 = poly[k]; x1, y1 = poly[(k + 1) % len(poly)]
        w = x0 * y1 - x1 * y0
        cx += (x0 + x1) * w; cy += (y0 + y1) * w
    cx /= 6 * a; cy /= 6 * a
    if pip(poly, cx, cy):
        return cx, cy
    xs = []
    for k in range(len(poly)):
        ax, ay = poly[k]; bx, by = poly[k - 1]
        if (ay > cy) != (by > cy):
            xs.append(ax + (cy - ay) / (by - ay) * (bx - ax))
    xs.sort()
    spans = list(zip(xs[::2], xs[1::2]))
    if not spans:
        return cx, cy
    a0, a1 = max(spans, key=lambda v: v[1] - v[0])
    return (a0 + a1) / 2, cy


def simplify(pts, eps):
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a]; bx, by = pts[b]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy) or 1e-9
        best, bi = -1, -1
        for i in range(a + 1, b):
            px, py = pts[i]
            d = abs((px - ax) * dy - (py - ay) * dx) / L
            if d > best:
                best, bi = d, i
        if best > eps:
            keep[bi] = True
            stack += [(a, bi), (bi, b)]
    return [p for p, k in zip(pts, keep) if k]


def simplify_ring(ring, eps):
    far = max(range(len(ring)), key=lambda k: (ring[k][0] - ring[0][0]) ** 2 + (ring[k][1] - ring[0][1]) ** 2)
    return simplify(ring[:far + 1], eps)[:-1] + simplify(ring[far:] + [ring[0]], eps)[:-1]


def window(ring):
    xs = [p[0] for p in ring]; ys = [p[1] for p in ring]
    c0 = max(0, int(min(xs) / CELL) - 2); c1 = min(W, int(max(xs) / CELL) + 3)
    r0 = max(0, int(min(ys) / CELL) - 2); r1 = min(H, int(max(ys) / CELL) + 3)
    return r0, r1, c0, c1


def inside(ring, r0, r1, c0, c1):
    X = (np.arange(c0, c1) + 0.5) * CELL
    Y = ((np.arange(r0, r1) + 0.5) * CELL)[:, None]
    m = np.zeros((r1 - r0, c1 - c0), bool)
    with np.errstate(divide="ignore", invalid="ignore"):
        for k in range(len(ring)):
            ax, ay = ring[k]; bx, by = ring[k - 1]
            if ay == by:
                continue
            m ^= ((ay > Y) != (by > Y)) & (X < ax + (Y - ay) / (by - ay) * (bx - ax))
    return m


def trace(mask, r0, c0):
    p = np.pad(mask, 1)
    rr, cc = np.nonzero(p)
    nxt = {}
    for r, c in zip(rr.tolist(), cc.tolist()):
        if not p[r - 1, c]:
            nxt.setdefault((c, r), []).append((c + 1, r))
        if not p[r, c + 1]:
            nxt.setdefault((c + 1, r), []).append((c + 1, r + 1))
        if not p[r + 1, c]:
            nxt.setdefault((c + 1, r + 1), []).append((c, r + 1))
        if not p[r, c - 1]:
            nxt.setdefault((c, r + 1), []).append((c, r))
    best = []
    while nxt:
        start = next(iter(nxt))
        loop = [start]
        cur = start
        while True:
            outs = nxt[cur]
            n = outs.pop()
            if not outs:
                del nxt[cur]
            if n == start:
                break
            loop.append(n)
            cur = n
        if len(loop) > len(best):
            best = loop
    ring = [((c - 1 + c0) * CELL, (r - 1 + r0) * CELL) for c, r in best]
    out = []
    for k in range(len(ring)):
        a, b, c = ring[k - 1], ring[k], ring[(k + 1) % len(ring)]
        if abs((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])) > 1e-9:
            out.append(b)
    return out


def colour(mask, r0, c0, h):
    if h is None or h > 45:
        return None
    core = ndimage.binary_erosion(mask, iterations=2)
    if core.sum() < 6:
        core = mask
    rr, cc = np.nonzero(core)
    px = np.clip(((cc + c0 + 0.5) * CELL / KX / (LON1 - LON0) * PW).astype(int), 0, PW - 1)
    py = np.clip((PH - (rr + r0 + 0.5) * CELL / KY / (LAT1 - LAT0) * PH).astype(int), 0, PH - 1)
    v = photo[py, px].astype(np.float32)
    lum = v.sum(1)
    lo, hi = np.percentile(lum, [25, 90])
    sel = v[(lum >= lo) & (lum <= hi)]
    if len(sel) == 0:
        sel = v
    return [int(t) for t in sel.mean(0)]


def otsu(v):
    hist, edges = np.histogram(v, bins=64)
    mids = (edges[:-1] + edges[1:]) / 2
    w0 = np.cumsum(hist); w1 = w0[-1] - w0
    s0 = np.cumsum(hist * mids); s1 = s0[-1] - s0
    with np.errstate(divide="ignore", invalid="ignore"):
        between = w0 * w1 * (s0 / w0 - s1 / w1) ** 2
    between[~np.isfinite(between)] = 0
    return edges[int(np.argmax(between)) + 1]


def levels(ring, mask, rel, valid, r0, c0, depth, out, upper):
    sel = mask & valid
    n, total = int(sel.sum()), int(mask.sum())
    if total == 0 or n < max(4, 0.25 * total):
        out.append([None if not upper else ring, None, None, upper, 0.0])
        return
    vals = rel[sel]
    cap = float(np.percentile(vals, 98))
    vals = np.minimum(vals, cap)
    built = vals > 2.5
    if built.mean() < 0.35:
        out.append([None if not upper else ring, None, None, upper, n / total])
        return
    roof = vals[built]
    if depth < 3 and total >= 200 and cap - float(np.percentile(roof, 10)) >= MIN_STEP:
        t = float(otsu(roof))
        lo, hi = roof[roof < t], roof[roof >= t]
        if len(lo) >= MIN_PART and len(hi) >= MIN_PART and hi.mean() - lo.mean() >= max(MIN_STEP, 0.3 * hi.mean()):
            hm = ndimage.binary_opening(sel & (np.minimum(rel, cap) >= t), np.ones((3, 3), bool))
            lab, k = ndimage.label(hm)
            comps = []
            for i in range(1, k + 1):
                cm = ndimage.binary_fill_holes(lab == i)
                if cm.sum() < MIN_PART:
                    continue
                tr = trace(cm, r0, c0)
                if len(tr) < 3:
                    continue
                tr = simplify_ring(tr, 0.55)
                if len(tr) >= 3 and abs(area(tr)) >= MIN_PART * CELL * CELL * 0.7:
                    comps.append((cm, tr))
            if comps and sum(int(c[0].sum()) for c in comps) < 0.92 * n:
                rest = mask.copy()
                for cm, tr in comps:
                    rest &= ~cm
                rs = rest & valid
                rv = np.minimum(rel[rs], cap)
                rv = rv[rv > 2.5]
                if len(rv) >= 20:
                    h = float(np.percentile(rv, 70))
                    out.append([None if not upper else ring, h, colour(rest, r0, c0, h), upper, n / total])
                    for cm, tr in comps:
                        levels(tr, cm, rel, valid, r0, c0, depth + 1, out, 1)
                    return
    h = float(np.percentile(roof, 70))
    out.append([None if not upper else ring, h, colour(mask, r0, c0, h), upper, n / total])


def measure(ring, holes):
    r0, r1, c0, c1 = window(ring)
    if r1 - r0 < 2 or c1 - c0 < 2:
        return [[None, None, None, 0, 0.0]]
    mask = inside(ring, r0, r1, c0, c1)
    for hr in holes:
        mask &= ~inside(hr, r0, r1, c0, c1)
    z = top[r0:r1, c0:c1]
    filled = np.where(z > -999, z, ndimage.maximum_filter(z, 3))
    valid = filled > -999
    gy = np.clip(((np.arange(r0, r1) + 0.5) * CELL / GCELL).astype(int), 0, ground.shape[0] - 1)
    gx = np.clip(((np.arange(c0, c1) + 0.5) * CELL / GCELL).astype(int), 0, ground.shape[1] - 1)
    g = float(np.median(ground[np.ix_(gy, gx)]))
    rel = filled - g
    out = []
    levels(ring, mask, rel, valid, r0, c0, 0, out, 0)
    return out


county = []
for f in sorted(glob.glob(os.path.join(BUILD, "county", "raw", "bld_*.geojson"))):
    for ft in json.load(open(f, encoding="utf-8"))["features"]:
        g = ft["geometry"]
        if not g:
            continue
        for poly in ([g["coordinates"]] if g["type"] == "Polygon" else g["coordinates"]):
            r = ring_of(poly[0])
            if len(r) >= 3:
                county.append(r)
osm = []
for e in json.load(open(os.path.join(BUILD, "osm", "miami.json"), encoding="utf-8"))["elements"]:
    if "building" in e.get("tags", {}) and e["type"] == "way" and "geometry" in e:
        osm.append(ring_of([(v["lon"], v["lat"]) for v in e["geometry"]]))

areas = [abs(area(r)) for r in county]
inners = [inner(r) for r in county]
cells = {}
for k, r in enumerate(county):
    xs = [v[0] for v in r]; ys = [v[1] for v in r]
    for gx in range(int(min(xs) // 16), int(max(xs) // 16) + 1):
        for gy in range(int(min(ys) // 16), int(max(ys) // 16) + 1):
            cells.setdefault((gx, gy), []).append(k)

result = {"county": [], "osm": []}
n_parts = n_upper = n_none = 0
for k, r in enumerate(county):
    near_k = set()
    xs = [v[0] for v in r]; ys = [v[1] for v in r]
    for gx in range(int(min(xs) // 16), int(max(xs) // 16) + 1):
        for gy in range(int(min(ys) // 16), int(max(ys) // 16) + 1):
            near_k.update(cells.get((gx, gy), ()))
    holes = [county[m] for m in near_k if m != k and areas[m] < areas[k] and pip(r, *inners[m])]
    parts = measure(r, holes)
    result["county"].append(parts)
    n_parts += len(parts); n_upper += sum(p[3] for p in parts); n_none += parts[0][1] is None
    if k % 1500 == 0:
        print("county", k, len(county), flush=True)
for k, r in enumerate(osm):
    result["osm"].append(measure(r, []) if len(r) >= 3 else [[None, None, None, 0, 0.0]])
for group in result.values():
    for parts in group:
        for p in parts:
            if p[0] is not None:
                p[0] = [[round(x, 3), round(y, 3)] for x, y in p[0]]
            if p[1] is not None:
                p[1] = round(p[1], 2)
            p[4] = round(p[4], 2)
json.dump(result, open(os.path.join(HERE, "parts.json"), "w"))
print("county outlines", len(county), "parts", n_parts, "upper parts", n_upper, "no building in the scan", n_none)
print("osm outlines", len(osm), "parts", sum(len(p) for p in result["osm"]))
