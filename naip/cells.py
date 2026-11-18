import math, os, sys
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None
HERE = os.path.dirname(os.path.abspath(__file__))
LID = os.path.join(HERE, "..", "lidar")
LAT0, LAT1, LON0, LON1 = 25.758, 25.800, -80.205, -80.115
UNIT = 2.8
KX = 111320 * math.cos(math.radians((LAT0 + LAT1) / 2)) / UNIT
KY = 110950 / UNIT
WX = int((LON1 - LON0) * KX) + 1
WY = int((LAT1 - LAT0) * KY) + 1
GW, GH = WX + 1, WY + 1

PW, PH = round((LON1 - LON0) * KX), round((LAT1 - LAT0) * KY)


def to_grid(im):
    a = np.asarray(im.resize((PW, PH), Image.BOX), np.float32)[::-1]
    out = np.zeros((GH, GW) + a.shape[2:], np.float32)
    out[:PH, :PW] = a
    return out


rgb = to_grid(Image.open(os.path.join(HERE, "miami.png")).convert("RGB"))
nir = to_grid(Image.open(os.path.join(HERE, "nir.png")).convert("L"))

top = np.load(os.path.join(LID, "top.npy"))
H, W = top.shape
ys = (np.arange(H) * 0.4).astype(np.int64)
xs = (np.arange(W) * 0.4).astype(np.int64)
idx = (ys[:, None] * GW + xs[None, :]).ravel()
t = top.ravel()
ok = t > -999
hmax = np.full(GW * GH, -1000, np.float32)
np.maximum.at(hmax, idx[ok], t[ok])
hsum = np.bincount(idx[ok], weights=t[ok], minlength=GW * GH)
hcnt = np.bincount(idx[ok], minlength=GW * GH)
del top, t, idx
hmean = np.where(hcnt > 0, hsum / np.maximum(hcnt, 1), np.nan).reshape(GH, GW).astype(np.float32)
hmax = np.where(hcnt > 0, hmax, np.nan).reshape(GH, GW)

g4 = np.load(os.path.join(LID, "ground.npy"))
fill = np.nanmedian(g4)
g4 = np.where(np.isnan(g4), fill, g4)
gy = np.minimum((np.arange(GH) / 4).astype(int), g4.shape[0] - 1)
gx = np.minimum((np.arange(GW) / 4).astype(int), g4.shape[1] - 1)
gnd = g4[gy][:, gx]

np.savez_compressed(os.path.join(HERE, "cells.npz"), rgb=rgb.astype(np.uint8), nir=nir.astype(np.uint8),
                    above=(hmean - gnd).astype(np.float32), above_max=(hmax - gnd).astype(np.float32))
print("cells", GW, GH, "photo", PW, PH, "scanned %.3f" % float(np.isfinite(hmean).mean()))
