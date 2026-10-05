"""Copies Mapillary speed-limit signs (esp32/mapillary-signs.json, from fetch-mapillary.py) onto roads.bin.
Same rules as the "apply signs" button in esp32/map-editor.html:
  - a sign goes to the nearest road within 30 m;
  - only roads with NO limit in OpenStreetMap are changed (tagged roads are left alone);
  - the number is carried along the rest of that street (joints with no junction, same road type), at most MAX_CHAIN pieces each way;
  - if signs on one piece disagree, the most common number wins and it must have more than half the votes.
  python scripts/apply-signs.py [roads.bin] [out.bin]     # default: roads.bin -> roads.signs.bin (original untouched)
"""
import json, math, os, struct, sys
from collections import Counter, defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'roads.bin')
dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'roads.signs.bin')
MAX_CHAIN, MATCH_M = 25, 30

buf = bytearray(open(src, 'rb').read())
assert buf[:4] == b'SLR1'
rows, cols, count = struct.unpack_from('<HH', buf, 20)[0], struct.unpack_from('<H', buf, 22)[0], struct.unpack_from('<I', buf, 24)[0]
rec0 = 32 + (rows * cols + 1) * 4
segs, rec_of, seen = [], [], {}
for r in range(count):
    o = rec0 + r * 20
    a, b, c, d, lim, cl = struct.unpack_from('<iiiiBB', buf, o)
    k = (a, b, c, d)
    if k not in seen:
        seen[k] = len(segs); segs.append([a, b, c, d, lim, cl]); rec_of.append([r])
    else: rec_of[seen[k]].append(r)

G = 2000
grid, ends = defaultdict(list), defaultdict(list)
for i, (a, b, c, d, lim, cl) in enumerate(segs):
    for rr in range(min(a, c) // G, max(a, c) // G + 1):
        for cc in range(min(b, d) // G, max(b, d) // G + 1): grid[(rr, cc)].append(i)
    ends[(a, b)].append(i); ends[(c, d)].append(i)

def dist(la, lo, s):
    k = math.cos(math.radians(la)) * 111320
    ax, ay, bx, by = (s[1] / 1e6 - lo) * k, (s[0] / 1e6 - la) * 111320, (s[3] / 1e6 - lo) * k, (s[2] / 1e6 - la) * 111320
    dx, dy = bx - ax, by - ay; l2 = dx * dx + dy * dy
    t = max(0, min(1, -(ax * dx + ay * dy) / l2)) if l2 else 0
    return math.hypot(ax + t * dx, ay + t * dy)

signs = json.load(open(os.path.join(ROOT, 'esp32', 'mapillary-signs.json')))
votes = defaultdict(Counter); matched = 0
for la, lo, v in signs:
    best, bd = -1, MATCH_M
    r, c = int(la * 1e6 // G), int(lo * 1e6 // G)
    for dr in (-1, 0, 1):
        for dc in (-1, 0, 1):
            for i in grid.get((r + dr, c + dc), ()):
                d = dist(la, lo, segs[i])
                if d < bd: bd, best = d, i
    if best >= 0: matched += 1; votes[best][v] += 1

result = {}
for i, cnt in votes.items():
    v, n = cnt.most_common(1)[0]
    if segs[i][4] == 0 and n * 2 > sum(cnt.values()): result[i] = v
direct = dict(result)

def chain(start):
    out, q = {start}, [(start, 0)]
    while q:
        i, depth = q.pop()
        if depth >= MAX_CHAIN: continue
        for k in ((segs[i][0], segs[i][1]), (segs[i][2], segs[i][3])):
            at = ends[k]
            if len(at) != 2: continue
            for j in at:
                if j not in out and segs[j][5] == segs[start][5] and segs[j][4] == 0 and j not in votes:
                    out.add(j); q.append((j, depth + 1))
    return out
for i, v in direct.items():
    for j in chain(i): result.setdefault(j, v)

changed = Counter()
for i, v in result.items():
    for r in rec_of[i]: buf[rec0 + r * 20 + 16] = v
    changed[v] += 1
open(dst, 'wb').write(buf)
print('%d signs, %d matched a road, %d pieces from signs directly, %d pieces changed in total' % (len(signs), matched, len(direct), len(result)))
print('by limit:', dict(sorted(changed.items())))
print('written to', dst)
