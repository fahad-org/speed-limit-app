"""Writes esp32/road-names.json: every named road of an area with its shape, so the map editor can change a whole
street by name instead of piece by piece.  (needs: pip install osmium)
  python scripts/osm_names.py [--south 24.33 --west 39.45 --north 24.62 --east 39.80] [path-to.osm.pbf]
"""
import argparse, json, os
import osmium

ap = argparse.ArgumentParser()
ap.add_argument('pbf', nargs='?', default=os.path.join(os.environ['TEMP'], 'speed-limit-roads', 'gcc-states-latest.osm.pbf'))
ap.add_argument('--south', type=float, default=24.33); ap.add_argument('--west', type=float, default=39.45)
ap.add_argument('--north', type=float, default=24.62); ap.add_argument('--east', type=float, default=39.80)
a = ap.parse_args()
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
CLASSES = ['motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'unclassified', 'residential', 'living_street', 'service',
           'motorway_link', 'trunk_link', 'primary_link', 'secondary_link', 'tertiary_link']

class H(osmium.SimpleHandler):
    def __init__(self):
        super().__init__(); self.rows = []
    def way(self, w):
        hw = w.tags.get('highway')
        if hw not in CLASSES: return
        name = w.tags.get('name') or w.tags.get('name:ar') or w.tags.get('name:en') or ''
        en = w.tags.get('name:en') or ''
        if not name and not en: return
        try: pts = [(n.lat, n.lon) for n in w.nodes]
        except Exception: return
        if not any(a.south <= la <= a.north and a.west <= lo <= a.east for la, lo in pts): return
        self.rows.append([name or en, en if en != name else '', CLASSES.index(hw), [[int(round(la * 1e6)), int(round(lo * 1e6))] for la, lo in pts]])

h = H(); h.apply_file(a.pbf, locations=True, idx='flex_mem')
out = os.path.join(ROOT, 'esp32', 'road-names.json')
json.dump(h.rows, open(out, 'w', encoding='utf-8'), ensure_ascii=False, separators=(',', ':'))
print(len(h.rows), 'named roads ->', out, round(os.path.getsize(out) / 1e6, 1), 'MB')
