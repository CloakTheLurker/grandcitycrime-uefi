import json, math, os, sys, urllib.request
from concurrent.futures import ThreadPoolExecutor

BASE = "https://s3-us-west-2.amazonaws.com/usgs-lidar-public/" + sys.argv[3]
LON0, LAT0, LON1, LAT1 = (float(v) for v in sys.argv[4:8])
R = 6378137
OUT = sys.argv[1]
DEPTH = int(sys.argv[2])
DRY = len(sys.argv) > 8


def merc(lon, lat):
    return math.radians(lon) * R, math.log(math.tan(math.pi / 4 + math.radians(lat) / 2)) * R


def get(url):
    for attempt in range(5):
        try:
            return urllib.request.urlopen(url, timeout=120).read()
        except Exception:
            if attempt == 4:
                raise


ept = json.loads(get(BASE + "/ept.json"))
bx0, by0, bz0, bx1, by1, bz1 = ept["bounds"]
x0, y0 = merc(LON0, LAT0)
x1, y1 = merc(LON1, LAT1)
x0 -= 60; y0 -= 60; x1 += 60; y1 += 60


def overlaps(key):
    d, x, y, z = (int(v) for v in key.split("-"))
    s = (bx1 - bx0) / 2 ** d
    return bx0 + x * s < x1 and bx0 + (x + 1) * s > x0 and by0 + y * s < y1 and by0 + (y + 1) * s > y0


nodes = {}
todo = ["0-0-0-0"]
while todo:
    with ThreadPoolExecutor(16) as ex:
        pages = list(ex.map(lambda k: json.loads(get(BASE + "/ept-hierarchy/" + k + ".json")), todo))
    todo = []
    for page in pages:
        for key, n in page.items():
            if int(key.split("-")[0]) > DEPTH or not overlaps(key):
                continue
            if n == -1:
                todo.append(key)
            elif n > 0:
                nodes[key] = n
by_depth = {}
for key, n in nodes.items():
    d = int(key.split("-")[0])
    c = by_depth.setdefault(d, [0, 0])
    c[0] += 1; c[1] += n
for d in sorted(by_depth):
    print("depth", d, "nodes", by_depth[d][0], "points", by_depth[d][1])
print("total nodes", len(nodes), "points", sum(nodes.values()))
if DRY:
    sys.exit()
os.makedirs(OUT, exist_ok=True)
json.dump({"bounds": ept["bounds"], "nodes": nodes}, open(os.path.join(OUT, "index.json"), "w"))


def fetch(key):
    path = os.path.join(OUT, key + ".laz")
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return 0
    data = get(BASE + "/ept-data/" + key + ".laz")
    with open(path + ".part", "wb") as f:
        f.write(data)
    os.replace(path + ".part", path)
    return len(data)


with ThreadPoolExecutor(24) as ex:
    total = sum(ex.map(fetch, sorted(nodes)))
print("downloaded", total, "bytes")
