import glob, math, os, sys
import numpy as np
import laspy

RAW, OUT = sys.argv[1], sys.argv[2]
LAT0, LAT1, LON0, LON1 = 25.758, 25.800, -80.205, -80.115
UNIT = 2.8
KX = 111320 * math.cos(math.radians((LAT0 + LAT1) / 2)) / UNIT
KY = 110950 / UNIT
WX = int((LON1 - LON0) * KX) + 1
WY = int((LAT1 - LAT0) * KY) + 1
CELL = 0.4
GCELL = 4.0
R = 6378137.0
W, H = int(WX / CELL) + 1, int(WY / CELL) + 1
GW, GH = int(WX / GCELL) + 1, int(WY / GCELL) + 1

top = np.full(W * H, -1000, np.float32)
gsum = np.zeros(GW * GH, np.float64)
gcnt = np.zeros(GW * GH, np.int64)
classes = {}
files = sorted(glob.glob(os.path.join(RAW, "*.laz")))
total = 0
for n, f in enumerate(files):
    las = laspy.read(f)
    x = np.asarray(las.x); y = np.asarray(las.y); z = np.asarray(las.z, np.float32)
    c = np.asarray(las.classification)
    total += len(z)
    for k, v in zip(*np.unique(c, return_counts=True)):
        classes[int(k)] = classes.get(int(k), 0) + int(v)
    gx = (np.degrees(x / R) - LON0) * KX
    gy = (np.degrees(2 * np.arctan(np.exp(y / R)) - math.pi / 2) - LAT0) * KY
    inside = (gx >= 0) & (gx < WX) & (gy >= 0) & (gy < WY)
    keep = inside & (c != 7) & (c != 18) & (c < 40) & (z < 330) & (z > -20)
    idx = (gy[keep] / CELL).astype(np.int64) * W + (gx[keep] / CELL).astype(np.int64)
    zk = z[keep]
    order = np.argsort(zk)
    idx, zk = idx[order], zk[order]
    zk = np.maximum(zk, top[idx])
    order = np.argsort(zk)
    top[idx[order]] = zk[order]
    g = inside & (c == 2)
    gi = (gy[g] / GCELL).astype(np.int64) * GW + (gx[g] / GCELL).astype(np.int64)
    gsum += np.bincount(gi, weights=z[g], minlength=GW * GH)
    gcnt += np.bincount(gi, minlength=GW * GH)
    if n % 200 == 0:
        print(n, len(files), total, flush=True)
print("points", total, "classes", dict(sorted(classes.items())))
top = top.reshape(H, W)
ground = np.where(gcnt > 0, gsum / np.maximum(gcnt, 1), np.nan).reshape(GH, GW).astype(np.float32)
print("cells with a point %.3f" % float((top > -999).mean()), "ground cells %.3f" % float((gcnt > 0).mean()),
      "ground median", float(np.nanmedian(ground)), "top max", float(top.max()))
np.save(os.path.join(OUT, "top.npy"), top)
np.save(os.path.join(OUT, "ground.npy"), ground)
