import os
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))

VEG = 0.18
TREE = 2.5
LOT_MIN = 30
GRASS_MIN = 10
TREE_MIN = 4.5
TREE_MAX = 32.0
BROAD = 13


def masks(GW, GH, edges, road_w, blds, fix):
    road = Image.new("L", (GW, GH), 0)
    d = ImageDraw.Draw(road)
    for cls, bridge, oneway, a, b, pts, wid, park in edges:
        w = wid + 2.0
        d.line([(x, y) for x, y in pts], fill=255, width=max(1, int(round(w))))
        r = w / 2
        for x, y in pts:
            d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    bld = Image.new("L", (GW, GH), 0)
    d = ImageDraw.Draw(bld)
    for pts in blds:
        d.polygon([(px / fix - 64, py / fix - 64) for px, py in pts], fill=255, outline=255)
    core = Image.new("L", (GW, GH), 0)
    d = ImageDraw.Draw(core)
    for cls, bridge, oneway, a, b, pts, wid, park in edges:
        w = wid + 0.4
        d.line([(x, y) for x, y in pts], fill=255, width=max(1, int(round(w))))
    return np.asarray(road) > 0, np.asarray(bld) > 0, np.asarray(core) > 0


def pick_trees(tree, amax, core, bld, land):
    h = np.nan_to_num(amax, nan=0)
    hc = np.where(tree, h, 0)
    peak = tree & (hc == ndimage.maximum_filter(hc, size=3)) & (hc > TREE_MIN)
    near = ndimage.uniform_filter(tree.astype(np.float32), 5) * 25
    _, (iy, ix) = ndimage.distance_transform_edt(core | bld, return_indices=True)
    ys, xs = np.nonzero(peak)
    order = np.argsort(-hc[ys, xs])
    taken = np.zeros(tree.shape, bool)
    out = []
    for i in order:
        y, x = ys[i], xs[i]
        if taken[y, x]:
            continue
        ht = float(hc[y, x])
        broad = int(near[y, x] >= BROAD)
        r = max(1, int(round(min(ht * 0.35, 7.0) / 2.8))) if broad else 1
        taken[max(0, y - r):y + r + 1, max(0, x - r):x + r + 1] = True
        ty, tx = int(iy[y, x]), int(ix[y, x])
        if abs(ty - y) > 2 or abs(tx - x) > 2 or not land[ty, tx]:
            continue
        if ht <= TREE_MAX:
            out.append((tx + 0.5, ty + 0.5, ht, broad))
    return out


def parking(path, xy, GW, GH):
    import json
    im = Image.new("L", (GW, GH), 0)
    d = ImageDraw.Draw(im)
    n = 0
    for e in json.load(open(path, encoding="utf-8"))["elements"]:
        t = e.get("tags", {})
        if t.get("parking") in ("multi-storey", "rooftop", "underground"):
            continue
        rings = [e["geometry"]] if e["type"] == "way" else [m["geometry"] for m in e.get("members", []) if m.get("role") == "outer" and "geometry" in m]
        for r in rings:
            if len(r) >= 3:
                d.polygon([xy(p) for p in r], fill=255)
                n += 1
    return np.asarray(im) > 0, n


def classify(ground, GW, GH, road, bld, osm_lot, core):
    c = np.load(os.path.join(HERE, "cells.npz"))
    rgb, nir, above, amax = c["rgb"].astype(np.float32), c["nir"].astype(np.float32), c["above"], c["above_max"]
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    g2 = np.frombuffer(bytes(ground), np.uint8).reshape(GH, GW)
    land = g2 == 1
    free = land & ~road & ~bld
    ndvi = (nir - r) / (nir + r + 1)
    lum = 0.3 * r + 0.59 * g + 0.11 * b
    mx, mn = rgb.max(2), rgb.min(2)
    sat = (mx - mn) / (mx + 1)
    scanned = np.isfinite(above)
    veg = ndvi > VEG
    tall = scanned & (np.nan_to_num(amax, nan=0) > TREE)
    tree = veg & tall
    vegfrac = ndimage.uniform_filter(veg.astype(np.float32), 7)
    grass = free & veg & (~tree | (vegfrac > 0.7))
    grass = ndimage.binary_closing(grass, np.ones((3, 3), bool)) & free
    grass = ndimage.binary_opening(grass, np.ones((2, 2), bool))
    lab, n = ndimage.label(grass)
    if n:
        size = ndimage.sum(grass, lab, np.arange(1, n + 1))
        grass = np.isin(lab, np.nonzero(size >= GRASS_MIN)[0] + 1)

    low = ~scanned | (np.nan_to_num(above, nan=0) < 2.0)
    paved = osm_lot & free & ~grass & low
    paved = ndimage.binary_opening(paved, np.ones((2, 2), bool))
    lab, n = ndimage.label(paved)
    lot = np.zeros(paved.shape, np.uint8)
    shade = []
    if n:
        idx = np.arange(1, n + 1)
        size = ndimage.sum(paved, lab, idx)
        lit = paved & (lum > 70) & ~((b > r + 12) & (lum < 110))
        lsum = ndimage.sum(np.where(lit, lum, 0), lab, idx)
        lcnt = ndimage.sum(lit, lab, idx)
        for k in range(n):
            if size[k] < LOT_MIN:
                continue
            m = lsum[k] / lcnt[k] if lcnt[k] > 4 else 120
            v = 1 if m < 165 else 2
            lot[lab == k + 1] = v
            shade.append(m)

    trees = pick_trees(tree, amax, core, bld, (g2 != 0))
    return grass, lot, trees, dict(veg=int((veg & free).sum()), grass=int(grass.sum()), lot=int((lot > 0).sum()), asphalt=int((lot == 1).sum()), concrete=int((lot == 2).sum()), lots=len(shade), shade=np.percentile(shade, [10, 50, 90]).round().tolist() if shade else [], tree_cells=int(tree.sum()), trees=len(trees), broad=sum(t[3] for t in trees))


def coverage(B):
    a = np.isfinite(np.load(os.path.join(HERE, "cells.npz"))["above"])
    H, W = a.shape
    out = np.zeros(((H + B - 1) // B, (W + B - 1) // B), bool)
    for by in range(out.shape[0]):
        for bx in range(out.shape[1]):
            out[by, bx] = a[by * B:(by + 1) * B, bx * B:(bx + 1) * B].mean() > 0.6
    return out
