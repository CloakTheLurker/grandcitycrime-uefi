import json, lzma, math, os, struct, sys, zlib
from collections import defaultdict

SRC = sys.argv[1] if len(sys.argv) > 1 else "osm/miami.json"
OUT = sys.argv[2] if len(sys.argv) > 2 else "c/map.bin"

LAT0, LAT1, LON0, LON1 = 25.758, 25.800, -80.205, -80.115
UNIT = 2.8                      # metres per game unit
KX = 111320 * math.cos(math.radians((LAT0 + LAT1) / 2)) / UNIT
KY = 110950 / UNIT
WX = int((LON1 - LON0) * KX) + 1
WY = int((LAT1 - LAT0) * KY) + 1
FIX = 16                        # coords in 16ths of a unit
CELL = 1.0


def xy(p):
    return ((p["lon"] - LON0) * KX, (p["lat"] - LAT0) * KY)


# keep the point that sticks out most then split
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


def area(poly):
    s = 0
    for i in range(len(poly)):
        x0, y0 = poly[i]; x1, y1 = poly[(i + 1) % len(poly)]
        s += x0 * y1 - x1 * y0
    return s / 2


def inside_box(x, y, m=0):
    return -m <= x < WX + m and -m <= y < WY + m


def q(v):
    return max(0, min(65535, int(round(v * FIX))))


data = json.load(open(SRC, encoding="utf-8"))["elements"]
print("elements", len(data))

ROAD_W = {"motorway": 5.0, "trunk": 5.0, "primary": 4.4, "secondary": 3.8, "tertiary": 3.2,
          "residential": 2.6, "unclassified": 2.6, "living_street": 2.3,
          "motorway_link": 2.2, "trunk_link": 2.2, "primary_link": 2.2, "secondary_link": 2.2, "tertiary_link": 2.2}
ROAD_C = {k: i for i, k in enumerate(ROAD_W)}

roads, buildings, coast, water_polys, park_polys, sand_polys = [], [], [], [], [], []
node_use = defaultdict(int)
for e in data:
    t = e.get("tags", {})
    if e["type"] == "way" and "highway" in t and t["highway"] in ROAD_W and "geometry" in e:
        roads.append(e)
        for n in e["nodes"]:
            node_use[n] += 1
    elif "building" in t and e["type"] == "way" and "geometry" in e:
        buildings.append(e)
    elif e["type"] == "way" and t.get("natural") == "coastline":
        coast.append([xy(p) for p in e["geometry"]])
    elif t.get("natural") == "water":
        if e["type"] == "way" and "geometry" in e:
            water_polys.append([xy(p) for p in e["geometry"]])
        elif e["type"] == "relation":
            for m in e.get("members", []):
                if m.get("role") == "outer" and "geometry" in m:
                    water_polys.append([xy(p) for p in m["geometry"]])
    elif t.get("natural") in ("beach", "sand") and e["type"] == "way" and "geometry" in e:
        sand_polys.append([xy(p) for p in e["geometry"]])
    elif (t.get("leisure") in ("park", "golf_course", "pitch") or t.get("landuse") in ("grass", "recreation_ground")) and e["type"] == "way" and "geometry" in e:
        park_polys.append([xy(p) for p in e["geometry"]])
print("roads", len(roads), "buildings", len(buildings), "coast", len(coast), "water", len(water_polys), "parks", len(park_polys), "sand", len(sand_polys))

# ground grid coast walls then flood the sea in
GW, GH = int(WX / CELL) + 1, int(WY / CELL) + 1
g = bytearray(GW * GH)          # 0 unknown 1 wall
def plot_line(x0, y0, x1, y1, val):
    n = int(max(abs(x1 - x0), abs(y1 - y0)) / CELL * 2) + 1
    for i in range(n + 1):
        t = i / n
        cx, cy = int((x0 + (x1 - x0) * t) / CELL), int((y0 + (y1 - y0) * t) / CELL)
        # 2 cells thick or the sea leaks it drowned an island once lol
        for ox, oy in ((0, 0), (1, 0), (0, 1)):
            if 0 <= cx + ox < GW and 0 <= cy + oy < GH:
                g[(cy + oy) * GW + cx + ox] = val
for w in coast:
    for (x0, y0), (x1, y1) in zip(w, w[1:]):
        plot_line(x0, y0, x1, y1, 1)
# land is on the left so the right side is sea
seeds = []
for w in coast:
    for (x0, y0), (x1, y1) in zip(w, w[1:]):
        dx, dy = x1 - x0, y1 - y0
        L = math.hypot(dx, dy)
        if L < 3:
            continue
        mx, my = (x0 + x1) / 2 + dy / L * 3.5, (y0 + y1) / 2 - dx / L * 3.5
        if inside_box(mx, my):
            seeds.append((int(mx / CELL), int(my / CELL)))
sea = bytearray(GW * GH)
stack = [s for s in seeds if g[s[1] * GW + s[0]] == 0]
for x, y in stack:
    sea[y * GW + x] = 1
while stack:
    x, y = stack.pop()
    for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
        if 0 <= nx < GW and 0 <= ny < GH:
            i = ny * GW + nx
            if not sea[i] and g[i] == 0:
                sea[i] = 1
                stack.append((nx, ny))
ground = bytearray(GW * GH)
for i in range(GW * GH):
    ground[i] = 0 if sea[i] else 1


def fill_poly(poly, val, only_land=False):
    if len(poly) < 3:
        return
    ys = [p[1] for p in poly]
    y0, y1 = max(0, int(min(ys) / CELL)), min(GH - 1, int(max(ys) / CELL) + 1)
    for cy in range(y0, y1 + 1):
        py = (cy + 0.5) * CELL
        xs = []
        for i in range(len(poly)):
            (ax, ay), (bx, by) = poly[i], poly[(i + 1) % len(poly)]
            if (ay <= py < by) or (by <= py < ay):
                xs.append(ax + (py - ay) / (by - ay) * (bx - ax))
        xs.sort()
        for a, b in zip(xs[::2], xs[1::2]):
            for cx in range(max(0, int(a / CELL)), min(GW, int(b / CELL) + 1)):
                if not only_land or ground[cy * GW + cx]:
                    ground[cy * GW + cx] = val


for p in water_polys:
    fill_poly(p, 0)
for p in park_polys:
    fill_poly(p, 2, True)
for p in sand_polys:
    fill_poly(p, 3, True)
land = sum(1 for v in ground if v)
print("ground", GW, GH, "land cells", land, "of", GW * GH)

def road_width(t, oneway):
    hw = t["highway"]
    lanes = 0
    for part in str(t.get("lanes", "")).replace(";", " ").split():
        if part.isdigit():
            lanes = max(lanes, int(part))
    if not lanes:
        base = hw.replace("_link", "")
        if oneway:
            lanes = {"motorway": 3, "trunk": 3, "primary": 3, "secondary": 2, "tertiary": 2}.get(base, 1)
        else:
            lanes = {"motorway": 4, "trunk": 4, "primary": 4, "secondary": 2, "tertiary": 2}.get(base, 2)
        if hw.endswith("_link"):
            lanes = 1
    lw = 3.6 if hw.startswith(("motorway", "trunk")) else 3.4 if hw.startswith(("primary", "secondary")) else 3.1
    m = lanes * lw
    park = 0
    if hw.startswith("motorway"):
        m += 2.0
    elif not oneway and hw in ("residential", "unclassified", "living_street", "tertiary") and lanes <= 2:
        m += 2 * 2.3
        park = 1
    elif not hw.endswith("_link"):
        m += 0.8
    return max(5.0, min(32.0, m)) / UNIT, park


nodes = {}
node_xy = []
edges = []
def node_index(nid, p):
    if nid not in nodes:
        nodes[nid] = len(node_xy)
        node_xy.append(p)
    return nodes[nid]
for r in roads:
    t = r["tags"]
    cls = ROAD_C[t["highway"]]
    bridge = 1 if t.get("bridge") in ("yes", "viaduct") else 0
    oneway = 1 if t.get("oneway") == "yes" or t["highway"] in ("motorway", "motorway_link") else 0
    pts = [xy(p) for p in r["geometry"]]
    ids = r["nodes"]
    wid, park = road_width(t, oneway)
    start = 0
    for i in range(1, len(ids)):
        if i == len(ids) - 1 or node_use[ids[i]] > 1:
            seg = pts[start:i + 1]
            if any(inside_box(x, y, 40) for x, y in seg):
                a = node_index(ids[start], pts[start])
                b = node_index(ids[i], pts[i])
                edges.append((cls, bridge, oneway, a, b, simplify(seg, 0.25), wid, park))
            start = i
print("nodes", len(node_xy), "edges", len(edges), "road points", sum(len(e[5]) for e in edges))

import glob
FT = 0.3048


def ring_of(coords):
    r = [((c[0] - LON0) * KX, (c[1] - LAT0) * KY) for c in coords]
    if len(r) > 1 and r[0] == r[-1]:
        r = r[:-1]
    return r


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


class Grid:
    def __init__(self, rings, size=16):
        self.rings, self.size, self.cells = rings, size, defaultdict(list)
        for k, r in enumerate(rings):
            xs = [v[0] for v in r]; ys = [v[1] for v in r]
            for gx in range(int(min(xs) // size), int(max(xs) // size) + 1):
                for gy in range(int(min(ys) // size), int(max(ys) // size) + 1):
                    self.cells[gx, gy].append(k)

    def at(self, x, y):
        return [k for k in self.cells.get((int(x // self.size), int(y // self.size)), ()) if pip(self.rings[k], x, y)]

    def near(self, ring):
        out = set()
        xs = [v[0] for v in ring]; ys = [v[1] for v in ring]
        for gx in range(int(min(xs) // self.size), int(max(xs) // self.size) + 1):
            for gy in range(int(min(ys) // self.size), int(max(ys) // self.size) + 1):
                out.update(self.cells.get((gx, gy), ()))
        return out


def county(tag):
    rows = []
    for f in sorted(glob.glob("county/raw/%s_*.geojson" % tag)):
        for ft in json.load(open(f, encoding="utf-8"))["features"]:
            g = ft["geometry"]
            if not g:
                continue
            for poly in ([g["coordinates"]] if g["type"] == "Polygon" else g["coordinates"]):
                r = ring_of(poly[0])
                if len(r) >= 3:
                    rows.append((r, ft["properties"]))
    return rows


def metres(text):
    try:
        return float(str(text).split()[0].replace("m", ""))
    except (ValueError, IndexError):
        return None


USES = [("VACANT", 0), ("PVT PARK", 0), ("SINGLE FAMILY", 1), ("LIVING QUARTERS", 1), ("2 LIVING UNITS", 2), ("MULTIFAMILY 2-9", 2),
        ("MULTIFAMILY 10", 3), ("REFERENCE FOLIO", 3), ("HOME FOR THE AGED", 3), ("MULTIFAMILY", 2), ("HOTEL", 4),
        ("OFFICE", 5), ("PROFESSIONAL SERVICE", 5), ("FINANCIAL", 5), ("STORE", 6), ("RESTAURANT", 6), ("SUPERMARKET", 6),
        ("SHOPPING", 6), ("NIGHTCLUB", 6), ("RETAIL", 6), ("SERVICE STATION", 6), ("WHOLESALE", 6), ("THEATER", 6), ("TOURIST", 6),
        ("WAREHOUSE", 7), ("MANUFACTURING", 7), ("INDUSTRIAL", 7), ("UTILITY", 7), ("AUTOMOTIVE OR MARINE", 7), ("MINERAL", 7),
        ("FOOD PROCESSING", 7), ("REPAIR", 7), ("AIRPORT", 7), ("PARKING", 8), ("MUNICIPAL", 9), ("COUNTY", 9), ("FEDERAL", 9),
        ("STATE", 9), ("PUBLIC INSTRUCTION", 9), ("RELIGIOUS", 9), ("EDUCATIONAL", 9), ("UNIVERSITY", 9), ("CHARITABLE", 9),
        ("BENEVOLENT", 9), ("LITERARY", 9), ("ARENA", 9)]
OSM_USE = {"house": 1, "detached": 1, "residential": 2, "apartments": 3, "hotel": 4, "office": 5, "commercial": 6, "retail": 6,
           "industrial": 7, "warehouse": 7, "parking": 8, "garage": 8, "garages": 8, "church": 9, "school": 9, "civic": 9,
           "public": 9, "university": 9, "hospital": 9, "government": 9}
GUESS_M = [6.0, 6.5, 9.0, 25.0, 20.0, 20.0, 5.0, 7.0, 12.0, 10.0]


def use_of(kind):
    kind = kind or ""
    for word, u in USES:
        if word in kind:
            return u
    return 0


import os
scan = json.load(open("lidar/parts.json")) if os.path.exists("lidar/parts.json") else None
base = [(k, r, p) for k, (r, p) in enumerate(county("bld")) if any(inside_box(x, y) for x, y in r)]
large = county("large")
folio = county("folio")
osm_b = []
for k, b in enumerate(buildings):
    r = ring_of([(v["lon"], v["lat"]) for v in b["geometry"]])
    if len(r) >= 3:
        osm_b.append((r, b["tags"], k))
g_large, g_folio, g_osm = Grid([r for r, _ in large]), Grid([r for r, _ in folio]), Grid([b[0] for b in osm_b])
osm_in = [inner(b[0]) for b in osm_b]
osm_area = [abs(area(b[0])) for b in osm_b]
folio_area = [abs(area(r)) for r, _ in folio]


def scanned(group, k, h, guess):
    parts = scan[group][k] if scan else [[None, None, None, 0, 0]]
    rgb, uppers = None, []
    if parts[0][1] is not None:
        h, guess, rgb = parts[0][1], 0, parts[0][2]
    for ring, ph, prgb, _, _ in parts[1:]:
        if ph is not None:
            uppers.append([[tuple(v) for v in ring], ph, prgb])
    return h, guess, rgb, uppers, parts[0][1] is not None


cand = []
src_count = defaultdict(int)
osm_seen = defaultdict(float)
for idx, ring, props in base:
    a = abs(area(ring))
    x, y = inner(ring)
    lidar = [large[k][1]["HEIGHT"] * FT for k in g_large.at(x, y) if large[k][1].get("HEIGHT")]
    fk = g_folio.at(x, y)
    fo = [folio[k][1] for k in fk]
    lidar += [f["BLDG_HEIGHT"] * FT for f in fo if f.get("BLDG_HEIGHT")]
    if props.get("HEIGHT"):
        lidar.append(props["HEIGHT"] * FT)
    h = max(lidar, default=0)
    src = "county height" if h else ""
    ok = set(g_osm.at(x, y)) | set(k for k in g_osm.near(ring) if osm_area[k] >= 0.4 * a and pip(ring, *osm_in[k]))
    tags = [osm_b[k][1] for k in ok]
    o_h = max([metres(osm_b[k][1]["height"]) or 0 for k in ok if "height" in osm_b[k][1] and (not h or a >= 0.5 * osm_area[k])], default=0)
    o_lv = max([v for v in (metres(t["building:levels"].split(";")[0]) for t in tags if "building:levels" in t) if v], default=0)
    floors = max([folio[k][1]["FLOORS"] for k in fk if folio[k][1].get("FLOORS") and (not h or a >= 0.5 * folio_area[k])], default=0)
    if o_h > 1.4 * h:
        h, src = o_h, "osm height"
    if floors * 3.2 > (1.5 * h if floors >= 4 or not h else 1e9):
        h, src = floors * 3.2, "floors"
    if not h and o_lv:
        h, src = o_lv * 3.1, "osm levels"
    use = max([use_of(f.get("BLDG_TYPE")) for f in fo], default=0)
    if not use:
        use = max([OSM_USE.get(t.get("building"), 0) for t in tags], default=0)
    guess = 0
    if not h:
        h = GUESS_M[use] if use else (4.5 if a * UNIT * UNIT < 150 else 7.0)
        guess, src = 1, "guess"
    h, guess, rgb, uppers, seen = scanned("county", idx, h, guess)
    late = []
    if seen:
        src = "laser scan"
        tallest = max([h] + [u[1] for u in uppers])
        for k in ok:
            osm_seen[k] = max(osm_seen[k], tallest)
        late = [k for k in ok if "height" in osm_b[k][1] and a >= 0.5 * osm_area[k]]
    src_count[src] += 1
    cand.append([ring, h, guess, use, a, (x, y), rgb, uppers, late])

for c in cand:
    o_big = max([metres(osm_b[k][1]["height"]) or 0 for k in c[8] if (metres(osm_b[k][1]["height"]) or 0) > 2.2 * osm_seen[k]], default=0)
    if o_big >= 40:
        src_count["laser scan"] -= 1
        src_count["osm height, built after the scan"] += 1
        if c[7]:
            max(c[7], key=lambda u: u[1])[1] = o_big
        else:
            c[1] = o_big

cand.sort(key=lambda c: -c[4])
g_cand = Grid([c[0] for c in cand])
kept = []
nested = 0
for k, c in enumerate(cand):
    over = [m for m in g_cand.at(*c[5]) if m < k]
    if any(cand[m][1] >= c[1] - 1.5 for m in over):
        nested += 1
        roof = max(cand[m][1] for m in over)
        c[7] = [u for u in c[7] if u[1] > roof + 1.5]
        c[1] = 0
    kept.append(c)
solid = [c for c in kept if c[1]]
g_kept = Grid([c[0] for c in solid])
kept_in = [c[5] for c in solid]
extra = 0
for k, (ring, t, idx) in enumerate(osm_b):
    if not any(inside_box(x, y) for x, y in ring):
        continue
    if g_kept.at(*osm_in[k]) or any(pip(ring, *kept_in[m]) for m in g_kept.near(ring)):
        continue
    use = OSM_USE.get(t.get("building"), 0)
    h = metres(t.get("height", "")) or (metres(t.get("building:levels", "").split(";")[0]) or 0) * 3.1
    guess = 0 if h else 1
    if not h:
        h = GUESS_M[use] if use else (4.5 if osm_area[k] * UNIT * UNIT < 150 else 7.0)
        sh, sg, rgb, uppers, seen = scanned("osm", idx, h, guess)
        if seen:
            h, guess = sh, sg
    else:
        rgb, uppers = None, []
    kept.append([ring, h, guess, use, osm_area[k], osm_in[k], rgb, uppers, []])
    extra += 1
print("county outlines", len(base), "heights from", dict(src_count), "parts inside taller or equal outlines dropped", nested, "only in osm", extra)


def outline(ring, eps):
    while True:
        # outlines are loops so cut at the far point first
        far = max(range(len(ring)), key=lambda k: (ring[k][0] - ring[0][0]) ** 2 + (ring[k][1] - ring[0][1]) ** 2)
        pts = simplify(ring[:far + 1], eps)[:-1] + simplify(ring[far:] + [ring[0]], eps)[:-1]
        qp = []
        for x, y in pts:
            v = (q(x + 64), q(y + 64))
            if not qp or qp[-1] != v:
                qp.append(v)
        if len(qp) > 1 and qp[0] == qp[-1]:
            qp.pop()
        if len(qp) <= 255:
            break
        eps *= 1.5
    if len(qp) >= 3 and area(qp) < 0:
        qp.reverse()
    return qp


blds = []
for ring, h, guess, use, a, _, rgb, uppers, _ in kept:
    if h and a >= 4:
        qp = outline(ring, 0.3)
        if len(qp) >= 3:
            blds.append((qp, max(1.0, min(h / UNIT, 250.0)), guess, use, 0, rgb))
    for uring, uh, urgb in uppers:
        qp = outline(uring, 0.3)
        if len(qp) >= 3:
            blds.append((qp, max(1.0, min(uh / UNIT, 250.0)), 0, use, 1, urgb))
blds.sort(key=lambda b: (b[0][0][1] >> 8, b[0][0][0]))

colours = sorted(set(tuple(b[5]) for b in blds if b[5]), key=lambda c: sum(c))
palette = [colours[int((k + 0.5) * len(colours) / 63)] for k in range(63)] if colours else [(160, 160, 160)] * 63
for _ in range(12):
    sums = [[0, 0, 0, 0] for _ in palette]
    for c in colours:
        k = min(range(63), key=lambda m: (palette[m][0] - c[0]) ** 2 + (palette[m][1] - c[1]) ** 2 + (palette[m][2] - c[2]) ** 2)
        t = sums[k]
        t[0] += c[0]; t[1] += c[1]; t[2] += c[2]; t[3] += 1
    palette = [(t[0] // t[3], t[1] // t[3], t[2] // t[3]) if t[3] else palette[k] for k, t in enumerate(sums)]
pal_of = {c: min(range(63), key=lambda m: (palette[m][0] - c[0]) ** 2 + (palette[m][1] - c[1]) ** 2 + (palette[m][2] - c[2]) ** 2) for c in colours}
print("buildings kept", len(blds), "points", sum(len(b[0]) for b in blds), "with real height", sum(1 for b in blds if not b[2]),
      "with real use", sum(1 for b in blds if b[3]), "upper parts", sum(b[4] for b in blds), "with roof colour", sum(1 for b in blds if b[5]))

# roads no wider than the gap between buildings
def cap_widths():
    import numpy as np
    from PIL import Image, ImageDraw
    R = 2
    im = Image.new("L", (GW * R, GH * R), 0)
    d = ImageDraw.Draw(im)
    for b in blds:
        d.polygon([((x / FIX - 64) * R, (y / FIX - 64) * R) for x, y in b[0]], fill=255, outline=255)
    m = np.asarray(im) > 0
    H, W = m.shape
    steps = np.arange(0.25, 14.0, 0.25)
    out, capped, unparked = [], 0, 0
    for cls, bridge, oneway, a, b, pts, wid, park in edges:
        if bridge or len(pts) < 2 or cls in (0, 1, 8, 9):
            out.append((cls, bridge, oneway, a, b, pts, wid, park))
            continue
        side = [[], []]
        total = sum(math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]) for i in range(len(pts) - 1))
        run = 0.0
        for i in range(len(pts) - 1):
            (x0, y0), (x1, y1) = pts[i], pts[i + 1]
            L = math.hypot(x1 - x0, y1 - y0)
            if L < 1e-6:
                continue
            ux, uy = (x1 - x0) / L, (y1 - y0) / L
            t = 0.5
            while t < L:
                dist = run + t
                if dist > 1.2 and total - dist > 1.2:
                    px, py = x0 + ux * t, y0 + uy * t
                    for k, sg in enumerate((1, -1)):
                        qx = ((px - uy * sg * steps) * R).astype(int)
                        qy = ((py + ux * sg * steps) * R).astype(int)
                        ok = (qx >= 0) & (qy >= 0) & (qx < W) & (qy < H)
                        hit = np.zeros(len(steps), bool)
                        hit[ok] = m[qy[ok], qx[ok]]
                        if hit.any():
                            side[k].append(steps[hit.argmax()])
                t += 1.0
            run += L
        lim = 99.0
        for k in range(2):
            if len(side[k]) >= 2:
                lim = min(lim, float(np.percentile(side[k], 2)))
            elif side[k]:
                lim = min(lim, side[k][0])
        half = max(lim - 0.55, 1.05)
        if wid / 2 > half:
            capped += 1
            nw = half * 2
            if park and nw < wid - 0.5:
                park = 0
                unparked += 1
            wid = nw
        out.append((cls, bridge, oneway, a, b, pts, wid, park))
    print("roads narrowed to fit between buildings", capped, "lost their parking lane", unparked)
    return out


edges = cap_widths()

# ground from the aerial photos
lot = None
if os.path.exists("naip/cells.npz") and os.path.exists("osm/parking.json"):
    sys.path.insert(0, "naip")
    import ground as photo
    road_m, bld_m, core_m = photo.masks(GW, GH, edges, list(ROAD_W.values()), [b[0] for b in blds], FIX)
    lot_osm, n_lot = photo.parking("osm/parking.json", xy, GW, GH)
    grass, lot, trees, st = photo.classify(ground, GW, GH, road_m, bld_m, lot_osm, core_m)
    for i in grass.ravel().nonzero()[0]:
        ground[i] = 2
    lot = lot.ravel()
    print("photo ground", st, "parking areas", n_lot)
    cover = photo.coverage(32)
else:
    trees, cover = [], None
CHAINS = ("hilton", "marriott", "ritz", "holiday inn", "loews", "hyatt", "cvs", "walgreens", "shell", "chevron", "mobil", "exxon",
          "mcdonald", "burger king", "wendy", "starbucks", "publix", "target", "walmart", "best buy", "bank of america", "chase",
          "wells fargo", "citi", "intercontinental", "four seasons", "kimpton", "standard", "sls", "mandarin", "jw ", "novotel",
          "hampton", "courtyard", "residence inn", "doubletree", "sheraton", "westin", "ac hotel", "aloft", "indigo", "ymca",
          "kaseya", "1 hotel", "w south", "w miami", "fairfield", "embassy suites", "comfort inn", "days inn", "best western",
          "radisson", "wyndham", "kfc", "subway", "dunkin", "7-eleven", "bp", "sunoco", "speedway", "autozone", "home depot", "costco")
import unicodedata
def sign_text(n):
    t = unicodedata.normalize("NFKD", n).encode("ascii", "ignore").decode().upper()
    t = "".join(ch if ch.isalnum() or ch in " &'-." else " " for ch in t)
    t = " ".join(t.split())
    return t
bld_box = [(min(x for x, _ in b[0]), min(y for _, y in b[0]), max(x for x, _ in b[0]), max(y for _, y in b[0])) for b in blds]
bgrid = defaultdict(list)
for i, (x0, y0, x1, y1) in enumerate(bld_box):
    for gx in range(x0 >> 9, (x1 >> 9) + 1):
        for gy in range(y0 >> 9, (y1 >> 9) + 1):
            bgrid[gx, gy].append(i)
names = {}
skipped = 0
for k, (r, tags, _) in enumerate(osm_b):
    n = tags.get("name")
    if not n:
        continue
    low = " " + n.lower() + " "
    if any(c in low for c in CHAINS):
        skipped += 1
        continue
    t = sign_text(n)
    if len(t) < 3 or len(t) > 24:
        continue
    px, py = osm_in[k]
    qx, qy = q(px + 64), q(py + 64)
    best = -1
    for i in bgrid.get((qx >> 9, qy >> 9), []):
        x0, y0, x1, y1 = bld_box[i]
        if not (x0 <= qx <= x1 and y0 <= qy <= y1):
            continue
        if pip(blds[i][0], qx, qy) and (best < 0 or blds[i][1] > blds[best][1]):
            best = i
    if best >= 0 and best not in names:
        names[best] = t
print("names", len(names), "chains left out", skipped)

out = bytearray()
def u16(v): out.extend(struct.pack("<H", max(0, min(65535, int(round(v))))))
def i32(v): out.extend(struct.pack("<i", v))

hdr_pos = len(out)
out.extend(b"MIAM")
i32(WX); i32(WY); i32(GW); i32(GH)
offs_pos = len(out)
out.extend(b"\0" * 4 * 7)
offs = []

offs.append(len(out))
# runs of 2 bit value and 14 bit length
for cy in range(GH):
    row = ground[cy * GW:(cy + 1) * GW]
    runs = []
    v, n = row[0], 0
    for c in row:
        if c == v and n < 16383:
            n += 1
        else:
            runs.append((v, n)); v, n = c, 1
    runs.append((v, n))
    u16(len(runs))
    for v, n in runs:
        u16(v << 14 | n)

offs.append(len(out))
i32(len(node_xy))
for x, y in node_xy:
    out.extend(struct.pack("<HH", q(x + 64), q(y + 64)))

offs.append(len(out))
i32(len(edges))
for cls, bridge, oneway, a, b, pts, wid, park in edges:
    out.extend(struct.pack("<BBBHHH", cls, bridge | oneway << 1 | park << 2, min(255, int(round(wid * 16))), a, b, len(pts)))
    for x, y in pts:
        out.extend(struct.pack("<HH", q(x + 64), q(y + 64)))

offs.append(len(out))
NB = (q(WY + 128) >> 8) + 1
band_n = [0] * NB
hl, hh, fl, rc, cnt, xl, xh, yl, dxs, dys, esc = (bytearray() for _ in range(11))
band, px = -1, 0
for pts, h, guess, use, upper, rgb in blds:
    x0, y0 = pts[0]
    if y0 >> 8 != band:
        band, px = y0 >> 8, 0
    band_n[band] += 1
    hq = int(min(h, 250) * 4)
    hl.append(hq & 255); hh.append(hq >> 8)
    fl.append(guess << 7 | upper << 6 | use)
    rc.append(pal_of[tuple(rgb)] if rgb else 255)
    cnt.append(len(pts))
    xl.append((x0 - px) & 255); xh.append((x0 - px) >> 8)
    yl.append(y0 & 255)
    px = x0
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        dx, dy = bx - ax, by - ay
        if -127 <= dx <= 127 and -127 <= dy <= 127:
            dxs.append(dx & 255); dys.append(dy & 255)
        else:
            dxs.append(128); dys.append(0)
            esc.extend(struct.pack("<hh", dx, dy))
total = sum(len(b[0]) for b in blds)
i32(len(blds)); i32(total); i32(len(esc) // 4); i32(NB)
for v in band_n:
    u16(v)
for c in palette:
    out.extend(bytes(c))
for part in (hl, hh, fl, rc, cnt, xl, xh, yl, dxs, dys, esc):
    out.extend(part)

offs.append(len(out))
for cy in range(GH):
    row = lot[cy * GW:(cy + 1) * GW] if lot is not None else bytes(GW)
    runs = []
    v, n = int(row[0]), 0
    for c in row:
        if c == v and n < 16383:
            n += 1
        else:
            runs.append((v, n)); v, n = int(c), 1
    runs.append((v, n))
    u16(len(runs))
    for v, n in runs:
        u16(v << 14 | n)

offs.append(len(out))
BW_, BH_ = WX // 32 + 1, WY // 32 + 1
bits = bytearray((BW_ * BH_ + 7) // 8)
if cover is not None:
    for by in range(BH_):
        for bx in range(BW_):
            if by < cover.shape[0] and bx < cover.shape[1] and cover[by, bx]:
                k = by * BW_ + bx
                bits[k >> 3] |= 1 << (k & 7)
trees = sorted(trees, key=lambda t: (int(t[1]), int(t[0])))
i32(len(trees)); i32(len(bits))
out.extend(bits)
gaps, attr, last = bytearray(), bytearray(), 0
for x, y, h, broad in trees:
    k = int(y) * GW + int(x)
    g, last = k - last, k
    while g >= 128:
        gaps.append(g & 127 | 128); g >>= 7
    gaps.append(g)
    attr.append(broad << 7 | min(127, int(round(h / 4))))
i32(len(gaps))
out.extend(gaps)
out.extend(attr)

offs.append(len(out))
i32(len(names))
last = 0
for i in sorted(names):
    t = names[i].encode()
    u16(i - last); last = i
    out.append(len(t)); out.extend(t)

struct.pack_into("<7i", out, offs_pos, *offs)
raw = bytes(out)
open(OUT, "wb").write(raw)
packed = struct.pack("<I", len(raw)) + lzma.compress(raw, format=lzma.FORMAT_ALONE, filters=[{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME}])
open(OUT.rsplit(".", 1)[0] + ".lz", "wb").write(packed)
print("packed", len(packed), "bytes")
print("raw", len(raw), "bytes, zlib would be", len(zlib.compress(raw, 9)))
print("world", WX, "x", WY, "units")
