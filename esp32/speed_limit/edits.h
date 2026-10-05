#pragma once
// Speed-limit corrections made from the phone. They are kept in /edits.bin (inside the map partition) as a log:
// 20-byte records {lat1, lon1, lat2, lon2, limit, 3 spare bytes}, newest last. limit 0 = "no correction here".
// The log is read into a small table at start-up, so a correction costs one table lookup per GPS fix.
// A correction is tied to the road piece's coordinates, so it still applies after a new roads.bin is uploaded.
// Needs (from the sketch): Seg, sameSeg(), readCell(), MAX_SEGS, pumpGps().

static const char* EDITS_FILE = "/edits.bin";
static const int MAX_EDITS = 4000;
static const int CHAIN_MAX = 40;               // pieces to follow in each direction when fixing a whole street
struct EdEntry { uint32_t h; uint8_t lim; };
static EdEntry edTab[MAX_EDITS];
static int edN = 0;
static uint32_t edRecords = 0;                 // records in the file (for the page)

// The same piece read in either direction gives the same key
static void canon(const Seg& s, int32_t k[4]) {
  bool swap = s.lat1 > s.lat2 || (s.lat1 == s.lat2 && s.lon1 > s.lon2);
  k[0] = swap ? s.lat2 : s.lat1; k[1] = swap ? s.lon2 : s.lon1; k[2] = swap ? s.lat1 : s.lat2; k[3] = swap ? s.lon1 : s.lon2;
}
static uint32_t edHash(const int32_t k[4]) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 4; i++) for (int b = 0; b < 4; b++) { h ^= (uint32_t)(k[i] >> (8 * b)) & 0xFF; h *= 16777619u; }
  return h;
}
static uint8_t edLookup(const Seg& s) {
  if (!edN) return 0;
  int32_t k[4]; canon(s, k); uint32_t h = edHash(k);
  for (int i = 0; i < edN; i++) if (edTab[i].h == h) return edTab[i].lim;
  return 0;
}
static bool edPut(uint32_t h, uint8_t lim) {      // table only
  for (int i = 0; i < edN; i++) if (edTab[i].h == h) { edTab[i].lim = lim; return true; }
  if (edN >= MAX_EDITS) return false;
  edTab[edN++] = { h, lim }; return true;
}
static uint8_t effectiveLimit(const Seg& s) { uint8_t o = edLookup(s); return o ? o : s.limit; }

static void edLoad() {
  edN = 0; edRecords = 0;
  File f = MAP_FS.open(EDITS_FILE, "r"); if (!f) return;
  uint8_t r[20];
  while (f.read(r, 20) == 20) {
    int32_t k[4]; memcpy(k, r, 16); edPut(edHash(k), r[16]); edRecords++;
  }
  f.close();
}

// ---- undo: the pieces changed by the last fix, with what they had before ----
static const int UNDO_MAX = 2 * CHAIN_MAX + 1;
static Seg undoSeg[UNDO_MAX]; static uint8_t undoPrev[UNDO_MAX]; static int undoN = 0;

static bool edAppend(const Seg* list, const uint8_t* vals, int n) {   // writes log + table
  File f = MAP_FS.open(EDITS_FILE, "a"); if (!f) return false;
  for (int i = 0; i < n; i++) {
    int32_t k[4]; canon(list[i], k);
    uint8_t r[20] = {0}; memcpy(r, k, 16); r[16] = vals[i];
    if (f.write(r, 20) != 20) { f.close(); return false; }
    edPut(edHash(k), vals[i]); edRecords++;
  }
  f.close(); return true;
}
static int edApply(const Seg* list, int n, uint8_t lim) {     // set a limit on these pieces (remembers how to undo)
  uint8_t vals[UNDO_MAX]; if (n > UNDO_MAX) n = UNDO_MAX;
  for (int i = 0; i < n; i++) { undoSeg[i] = list[i]; undoPrev[i] = edLookup(list[i]); vals[i] = lim; }
  undoN = n;
  return edAppend(list, vals, n) ? n : -1;
}
static int edUndo() {
  if (!undoN) return 0;
  int n = undoN; undoN = 0;
  return edAppend(undoSeg, undoPrev, n) ? n : -1;
}

// ---- a whole street: pieces joined end to end with no junction, same road type and same limit (like street() in map-editor.html) ----
static bool touches(const Seg& s, int32_t la, int32_t lo) { return (s.lat1 == la && s.lon1 == lo) || (s.lat2 == la && s.lon2 == lo); }
static bool sameOrReverse(const Seg& a, const Seg& b) {
  return sameSeg(a, b) || (a.lat1 == b.lat2 && a.lon1 == b.lon2 && a.lat2 == b.lat1 && a.lon2 == b.lon1);
}
static int chainOf(const Seg& seed, Seg* out, int cap) {
  static Seg cell[MAX_SEGS];
  int n = 0; out[n++] = seed;
  uint8_t seedLim = effectiveLimit(seed);
  for (int dir = 0; dir < 2; dir++) {
    int32_t pl = dir ? seed.lat2 : seed.lat1, po = dir ? seed.lon2 : seed.lon1;
    Seg prev = seed;
    for (int hop = 0; hop < CHAIN_MAX && n < cap; hop++) {
      pumpGps();                                         // keep the GPS bytes flowing while we read the map
      int c = readCell(pl / 1e6, po / 1e6, cell, MAX_SEGS);
      if (c <= 0) break;
      int others = 0, at = -1;
      for (int i = 0; i < c; i++) if (touches(cell[i], pl, po) && !sameOrReverse(cell[i], prev)) { others++; at = i; }
      if (others != 1) break;                            // end of the road or a junction
      const Seg& nx = cell[at];
      if (nx.cls != seed.cls || effectiveLimit(nx) != seedLim) break;
      bool seen = false; for (int i = 0; i < n; i++) if (sameOrReverse(out[i], nx)) { seen = true; break; }
      if (seen) break;
      out[n++] = nx; prev = nx;
      if (nx.lat1 == pl && nx.lon1 == po) { pl = nx.lat2; po = nx.lon2; } else { pl = nx.lat1; po = nx.lon1; }
    }
  }
  return n;
}
