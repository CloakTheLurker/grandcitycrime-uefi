import struct, sys
from PIL import Image, ImageDraw

raw = open(sys.argv[1] if len(sys.argv) > 1 else "c/map.bin", "rb").read()
assert raw[:4] == b"MIAM"
WX, WY, GW, GH = struct.unpack_from("<4i", raw, 4)
o_ground, o_nodes, o_edges, o_bld = struct.unpack_from("<4i", raw, 20)
S = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
MODE = sys.argv[4] if len(sys.argv) > 4 else "height"
STEP = 2 if S <= 0.5 else 1
img = Image.new("RGB", (int(GW * S), int(GH * S)))
px = img.load()
cols = [(40, 110, 160), (200, 196, 190), (90, 150, 80), (230, 210, 160)]
p = o_ground
for y in range(GH):
    n, = struct.unpack_from("<H", raw, p); p += 2
    x = 0
    for _ in range(n):
        r, = struct.unpack_from("<H", raw, p); p += 2
        v, ln = r >> 14, r & 16383
        if y % STEP == 0:
            for xx in range(x, x + ln, STEP):
                px[int(xx * S), int((GH - 1 - y) * S)] = cols[v]
        x += ln
d = ImageDraw.Draw(img)
def tp(x, y): return ((x / 16 - 64) * S, (GH - 1 - (y / 16 - 64)) * S)
n, = struct.unpack_from("<i", raw, o_edges); p = o_edges + 4
for _ in range(n):
    cls, fl, a, b, k = struct.unpack_from("<BBHHH", raw, p); p += 8
    pts = [tp(*struct.unpack_from("<HH", raw, p + 4 * i)) for i in range(k)]; p += 4 * k
    d.line(pts, fill=(255, 120, 60) if fl & 1 else (70, 70, 80), width=2 if cls < 3 else 1)
n, total, n_esc, nb = struct.unpack_from("<4i", raw, o_bld)
p = o_bld + 16
band_n = struct.unpack_from("<%dH" % nb, raw, p); p += nb * 2
pal = [tuple(raw[p + k * 3:p + k * 3 + 3]) for k in range(63)]; p += 63 * 3
hl, hh, fl, rc, cnt, xl, xh, yl = (raw[p + k * n:p + (k + 1) * n] for k in range(8)); p += 8 * n
dxs = struct.unpack_from("<%db" % (total - n), raw, p); p += total - n
dys = struct.unpack_from("<%db" % (total - n), raw, p); p += total - n
esc = struct.unpack_from("<%dh" % (n_esc * 2), raw, p)
USE = [(150, 110, 140), (236, 200, 150), (240, 160, 120), (230, 110, 100), (250, 120, 200), (110, 150, 240), (250, 220, 90), (150, 150, 150), (90, 90, 100), (120, 200, 200)]
i = di = ei = 0
shapes = []
for band, k in enumerate(band_n):
    px = 0
    for _ in range(k):
        x = px + (xl[i] | xh[i] << 8); y = band << 8 | yl[i]; px = x
        pts = [tp(x, y)]
        for _ in range(cnt[i] - 1):
            dx, dy = dxs[di], dys[di]; di += 1
            if dx == -128:
                dx, dy = esc[ei], esc[ei + 1]; ei += 2
            x += dx; y += dy
            pts.append(tp(x, y))
        h = (hl[i] | hh[i] << 8) / 4
        if MODE == "use":
            c = USE[fl[i] & 15]
        elif MODE == "roof":
            c = pal[rc[i]] if rc[i] < 63 else (150, 150, 156)
        else:
            v = min(255, int(60 + h * 2.2))
            c = (v, v // 2, v // 3) if not fl[i] >> 7 else (150, 110, 140)
        shapes.append((h, fl[i] >> 6 & 1, pts, c))
        i += 1
for h, upper, pts, c in sorted(shapes, key=lambda v: (v[1], v[0])):
    d.polygon(pts, fill=c, outline=(20, 20, 20) if MODE != "roof" else None)
img.save(sys.argv[2] if len(sys.argv) > 2 else "shots/map-preview.png")
print(img.size)
