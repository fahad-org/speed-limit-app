"""Pulls road facts out of the Gulf OpenStreetMap extract (needs: pip install osmium).
For every road inside the big Saudi cities it keeps: type, lanes, one-way, maxspeed (if tagged), name and where it is.
Used by learn-limits.py to find which speed limit usually goes with which kind of road.
  python scripts/osm_extract.py [path-to.osm.pbf]    ->  scripts/.osm-roads.json
"""
import json, os, re, sys
import osmium

PBF = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ['TEMP'], 'speed-limit-roads', 'gcc-states-latest.osm.pbf')
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.osm-roads.json')
CLASSES = ['motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'unclassified', 'residential', 'living_street', 'service',
           'motorway_link', 'trunk_link', 'primary_link', 'secondary_link', 'tertiary_link']
CITIES = {   # south, west, north, east
    'madinah': (24.33, 39.45, 24.62, 39.80), 'riyadh': (24.40, 46.40, 25.10, 47.10), 'jeddah': (21.25, 39.05, 21.85, 39.40),
    'makkah': (21.30, 39.70, 21.55, 39.95), 'dammam': (26.20, 49.90, 26.60, 50.30), 'taif': (21.20, 40.30, 21.35, 40.50),
    'abha': (18.10, 42.30, 18.40, 42.80), 'buraydah': (26.25, 43.90, 26.45, 44.10), 'tabuk': (28.30, 36.50, 28.50, 36.70),
    'hail': (27.45, 41.60, 27.60, 41.80), 'hofuf': (25.30, 49.50, 25.50, 49.70), 'jazan': (16.80, 42.50, 17.00, 42.70),
    'najran': (17.45, 44.05, 17.60, 44.30), 'yanbu': (24.00, 38.00, 24.20, 38.30),
}
SPEED = re.compile(r'^(\d+(?:\.\d+)?)\s*(mph|km/?h|kph)?$', re.I)

def speed(v):
    if not v: return 0
    m = SPEED.match(v.split(';')[0].strip())
    if not m: return 0
    n = float(m.group(1)) * (1.609 if (m.group(2) or '').lower() == 'mph' else 1)
    return min(255, round(n))

def lanes(v):
    try: return int(float(v))
    except (TypeError, ValueError): return 0

class H(osmium.SimpleHandler):
    def __init__(self):
        super().__init__(); self.rows = []
    def way(self, w):
        t = w.tags; hw = t.get('highway')
        if hw not in CLASSES: return
        try: n0 = w.nodes[len(w.nodes) // 2]; lat, lon = n0.lat, n0.lon
        except Exception: return
        for city, (s, we, n, e) in CITIES.items():
            if s <= lat <= n and we <= lon <= e:
                ms = speed(t.get('maxspeed')) or speed(t.get('maxspeed:forward')) or speed(t.get('maxspeed:backward'))
                ow = 1 if t.get('oneway') in ('yes', '1', 'true') else 0
                self.rows.append([city, CLASSES.index(hw), lanes(t.get('lanes')), ow, ms, round(lat, 5), round(lon, 5),
                                  t.get('name', ''), 1 if t.get('bridge') in ('yes', 'viaduct') else 0, 1 if t.get('tunnel') == 'yes' else 0,
                                  t.get('surface', ''), t.get('ref', '')])
                break

h = H()
print('reading', PBF, flush=True)
h.apply_file(PBF, locations=True, idx='flex_mem')
json.dump(h.rows, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False)
print(len(h.rows), 'roads ->', OUT)
