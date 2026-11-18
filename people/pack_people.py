import json, os, struct, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
C = os.path.join(HERE, "..", "c")
AW, AH = 256, 128
NAMES = ["m_a", "m_b", "m_c", "f_a", "f_b", "f_c", "m_d", "m_e", "f_d"]
KEY = (255, 0, 255)


def slot_for(key):
    k = key.lower()
    if "suit" in k:
        return (128, 0, 128, 64)
    if any(h in k for h in ("short", "ponytail", "bob", "long", "afro", "braid")):
        return (128, 64, 64, 64)
    if "shoe" in k:
        return (192, 64, 32, 32)
    if "eye" in k and "brow" not in k:
        return (224, 64, 32, 32)
    if "brow" in k:
        return (192, 96, 64, 32)
    return (0, 0, 128, 128)


def lz4(data):
    n = len(data)
    out = bytearray()
    table = {}
    i = anchor = 0
    def emit(lit_start, lit_end, mlen, off):
        ll = lit_end - lit_start
        tok = (min(ll, 15) << 4) | (min(mlen - 4, 15) if mlen else 0)
        out.append(tok)
        if ll >= 15:
            r = ll - 15
            while r >= 255:
                out.append(255); r -= 255
            out.append(r)
        out.extend(data[lit_start:lit_end])
        if mlen:
            out.extend(struct.pack("<H", off))
            if mlen - 4 >= 15:
                r = mlen - 4 - 15
                while r >= 255:
                    out.append(255); r -= 255
                out.append(r)
    while i + 4 <= n - 5:
        k = data[i:i + 4]
        j = table.get(k)
        table[k] = i
        if j is not None and i - j < 65536:
            m = 4
            while i + m < n - 5 and data[j + m] == data[i + m]:
                m += 1
            emit(anchor, i, m, i - j)
            for t in range(i + 1, i + m):
                if t + 4 <= n:
                    table[data[t:t + 4]] = t
            i += m
            anchor = i
        else:
            i += 1
    emit(anchor, n, 0, 0)
    return struct.pack("<I", n) + bytes(out)


def unlz4(b):
    n = struct.unpack_from("<I", b, 0)[0]
    p = 4
    o = bytearray()
    while len(o) < n:
        tok = b[p]; p += 1
        ll = tok >> 4
        if ll == 15:
            while True:
                x = b[p]; p += 1; ll += x
                if x != 255: break
        o.extend(b[p:p + ll]); p += ll
        if len(o) >= n: break
        off = b[p] | b[p + 1] << 8; p += 2
        ml = (tok & 15)
        if ml == 15:
            while True:
                x = b[p]; p += 1; ml += x
                if x != 255: break
        ml += 4
        s = len(o) - off
        for t in range(ml):
            o.append(o[s + t])
    return bytes(o)


def build_atlas(d):
    atlas = Image.new("RGBA", (AW, AH), KEY + (0,))
    rects = []
    for im in d["images"]:
        x, y, w, h = slot_for(im["key"])
        rects.append((x, y, w, h))
        if im["file"] and os.path.isfile(im["file"]):
            src = Image.open(im["file"]).convert("RGBA").resize((w, h), Image.LANCZOS)
        else:
            c = tuple(int(255 * min(1, max(0, v))) for v in im["color"][:3])
            src = Image.new("RGBA", (w, h), c + (255,))
        atlas.paste(src, (x, y))
    rgb = Image.new("RGB", (AW, AH), (0, 0, 0))
    px, a = atlas.load(), atlas.split()[3].load()
    out = rgb.load()
    for yy in range(AH):
        for xx in range(AW):
            r, g, b, al = px[xx, yy]
            out[xx, yy] = KEY if al < 110 else (r, g, b)
    q = rgb.quantize(colors=255, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = q.getpalette()[:255 * 3]
    idx = q.load()
    keyi = None
    best = 1e9
    for i in range(255):
        dd = (pal[i * 3] - 255) ** 2 + pal[i * 3 + 1] ** 2 + (pal[i * 3 + 2] - 255) ** 2
        if dd < best: best, keyi = dd, i
    data = bytearray(AW * AH)
    for yy in range(AH):
        for xx in range(AW):
            r, g, b = out[xx, yy]
            v = idx[xx, yy]
            data[yy * AW + xx] = 0 if (r, g, b) == KEY else v + 1
    palette = [0, 0, 0] + pal
    while len(palette) < 768: palette.append(0)
    return rects, bytes(palette), bytes(data), atlas


def pack_lod(lod, rects, cut_parts=()):
    vs, ts = lod["verts"], lod["tris"]
    b = bytearray(struct.pack("<HH", len(vs), len(ts)))
    for p, uv, ii, w in vs:
        x, y, ww, hh = rects[ii]
        u = min(max(uv[0] % 1.0001, 0), 1); v = min(max(uv[1] % 1.0001, 0), 1)
        au = (x + u * (ww - 1) + 0.5) / AW; av = (y + (1 - v) * (hh - 1) + 0.5) / AH
        b.extend(struct.pack("<hhh", *[int(round(c * 16384)) for c in p]))
        b.extend(struct.pack("<BB", min(255, int(au * 256)), min(255, int(av * 256))))
        (b0, w0), (b1, w1) = w
        b.extend(struct.pack("<BBB", b0, b1, int(round(w0 * 255))))
    for t in ts:
        two = vs[t[0]][2] in cut_parts
        b.extend(struct.pack("<HHH", t[0] | (0x8000 if two else 0), t[1], t[2]))
    return b


def xz(data):
    import lzma
    z = lzma.compress(data, format=lzma.FORMAT_ALONE, filters=[{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME}])
    return struct.pack("<I", len(data)) + z


def main():
    blob = bytearray(b"PPL1" + struct.pack("<I", len(NAMES)))
    for name in NAMES:
        d = json.load(open(os.path.join(OUT, name + ".json")))
        rects, pal, tex, atlas = build_atlas(d)
        atlas.save(os.path.join(OUT, name + "_atlas.png"))
        blob.extend(pal)
        blob.extend(tex)
        for j in d["joints"]:
            blob.extend(struct.pack("<fff", *j))
        cut = set(i for i, im in enumerate(d["images"]) if any(h in im["key"].lower() for h in ("short", "ponytail", "bob", "long", "afro", "braid", "brow")))
        for lod in d["lods"]:
            blob.extend(pack_lod(lod, rects, cut))
        print(name, [(len(l["verts"]), len(l["tris"])) for l in d["lods"]])
    z = xz(bytes(blob))
    open(os.path.join(C, "people.lz"), "wb").write(z)
    open(os.path.join(C, "people.raw"), "wb").write(bytes(blob))
    print("people raw", len(blob), "lz", len(z))
    m = open(os.path.join(C, "map.bin"), "rb").read()
    mz = xz(m)
    open(os.path.join(C, "map.lz"), "wb").write(mz)
    print("map raw", len(m), "lz", len(mz))


main()
