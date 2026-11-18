import os, sys, urllib.parse, urllib.request
from PIL import Image

URL = "https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPImagery/ImageServer/exportImage"
LAT0, LON0 = 25.758, -80.205
DX, DY, PX, PY = 0.03, 0.021, 3000, 2100
HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, "raw")
os.makedirs(RAW, exist_ok=True)

for name, bands, mode in (("miami", "0,1,2", "RGB"), ("nir", "3", "L")):
    full = Image.new(mode, (PX * 3, PY * 2))
    for ix in range(3):
        for iy in range(2):
            path = os.path.join(RAW, "%s_%d_%d.jpg" % (name[0], ix, iy))
            box = "%.6f,%.6f,%.6f,%.6f" % (LON0 + DX * ix, LAT0 + DY * iy, LON0 + DX * (ix + 1), LAT0 + DY * (iy + 1))
            query = urllib.parse.urlencode({"bbox": box, "bboxSR": 4326, "imageSR": 4326, "size": "%d,%d" % (PX, PY), "format": "jpg",
                                            "bandIds": bands, "interpolation": "RSP_BilinearInterpolation", "f": "image"})
            for attempt in range(8):
                if os.path.exists(path) and os.path.getsize(path) > 100000:
                    break
                try:
                    data = urllib.request.urlopen(URL + "?" + query, timeout=240).read()
                except Exception:
                    continue
                open(path, "wb").write(data)
            else:
                sys.exit("tile %s %d %d did not download" % (name, ix, iy))
            tile = Image.open(path).convert(mode)
            assert tile.size == (PX, PY)
            full.paste(tile, (ix * PX, (1 - iy) * PY))
    full.save(os.path.join(HERE, name + ".png"))
    print("stitched", name, full.size)
