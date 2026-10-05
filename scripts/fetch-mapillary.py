"""Downloads the speed-limit signs Mapillary has detected in an area, to esp32/mapillary-signs.json.
The map editor (esp32/map-editor.html) shows them and can copy their numbers onto the roads.
  python scripts/fetch-mapillary.py                 # Madinah, same area as build-roads.ps1
  python scripts/fetch-mapillary.py --south 24.55 --west 46.5 --north 24.95 --east 46.95
Needs a free Mapillary client token (read only) in a file called mapillary.key in the project folder.
It can be stopped and started again: finished pieces are remembered in scripts/.mapillary-cache.json.
Mapillary's data is CC-BY-SA: credit "Mapillary" if you share the result.
"""
import argparse, json, os, sys, time, urllib.parse, urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
ap = argparse.ArgumentParser()
ap.add_argument('--south', type=float, default=24.33); ap.add_argument('--west', type=float, default=39.45)
ap.add_argument('--north', type=float, default=24.62); ap.add_argument('--east', type=float, default=39.80)
ap.add_argument('--tile', type=float, default=0.05)      # degrees; Mapillary allows at most 0.01 square degrees per request
ap.add_argument('--workers', type=int, default=8)
ap.add_argument('--out', default=os.path.join(ROOT, 'esp32', 'mapillary-signs.json'))
a = ap.parse_args()

token = open(os.path.join(ROOT, 'mapillary.key'), encoding='utf-8-sig').read().strip()
SPEEDS = list(range(10, 130, 10)) + [25, 35, 45]
cache_path = os.path.join(ROOT, 'scripts', '.mapillary-cache.json')
cache = json.load(open(cache_path)) if os.path.exists(cache_path) else {}

def frange(lo, hi, step):
    x = lo
    while x < hi - 1e-9: yield round(x, 6); x += step

jobs = []
for s in frange(a.south, a.north, a.tile):
    for w in frange(a.west, a.east, a.tile):
        bbox = '%.5f,%.5f,%.5f,%.5f' % (w, s, min(w + a.tile, a.east), min(s + a.tile, a.north))
        for v in SPEEDS: jobs.append((bbox, v))

def fetch(bbox, v):
    # one speed value per request: asking for several at once makes the API time out
    ov = ','.join('regulatory--maximum-speed-limit-%d--g%d' % (v, g) for g in (1, 2))
    q = urllib.parse.urlencode({'access_token': token, 'fields': 'id,geometry', 'bbox': bbox, 'limit': 2000, 'object_values': ov})
    for attempt in range(4):
        try:
            with urllib.request.urlopen('https://graph.mapillary.com/map_features?' + q, timeout=120) as r:
                return [(f['id'], f['geometry']['coordinates'][1], f['geometry']['coordinates'][0]) for f in json.load(r)['data']]
        except Exception as e:
            err = e; time.sleep(3 * (attempt + 1))
    raise err

def from_centre(j):   # busiest part first: tiles nearest the middle of the area
    w, s, e, n = map(float, j[0].split(','))
    return ((s + n) / 2 - (a.south + a.north) / 2) ** 2 + ((w + e) / 2 - (a.west + a.east) / 2) ** 2
todo = sorted((j for j in jobs if '%s|%d' % j not in cache), key=from_centre)
print('%d requests, %d already done' % (len(jobs), len(jobs) - len(todo)), flush=True)
failed = 0; done = 0
with ThreadPoolExecutor(a.workers) as ex:
    futs = {ex.submit(fetch, *j): j for j in todo}
    for f in as_completed(futs):
        j = futs[f]
        try: cache['%s|%d' % j] = f.result()
        except Exception as e: failed += 1; print('failed', j, e, flush=True)
        done += 1
        if done % 20 == 0:
            json.dump(cache, open(cache_path, 'w')); print('%d/%d' % (done, len(todo)), flush=True)
json.dump(cache, open(cache_path, 'w'))

signs = {}
for key, items in cache.items():
    v = int(key.split('|')[1])
    for fid, la, lo in items: signs[fid] = [round(la, 6), round(lo, 6), v]
json.dump(sorted(signs.values()), open(a.out, 'w'))
print('%d signs saved to %s (%d requests failed)' % (len(signs), a.out, failed))
