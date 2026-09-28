# Packs the roads of an area from an OpenStreetMap extract into roads.bin, so the app
# (and later the GPS device) can find the speed limit with no internet.
#   powershell -ExecutionPolicy Bypass -File scripts/build-roads.ps1              # Madinah
#   powershell -ExecutionPolicy Bypass -File scripts/build-roads.ps1 -South 24.55 -West 46.5 -North 24.95 -East 46.95 -Out roads.bin
# The map data is the free Geofabrik extract of the Gulf states (~250 MB). It is downloaded once
# to %TEMP%\speed-limit-roads; pass -Update to download the newest one, or -Pbf to use your own file.
#
# roads.bin format (little-endian), kept simple enough to read from an SD card on a microcontroller:
#   header, 32 bytes:
#     0  'SLR1'            4 bytes
#     4  version u16 = 1,  reserved u16
#     8  south  i32        degrees * 1e6   (grid origin)
#     12 west   i32        degrees * 1e6
#     16 cell   i32        cell size in degrees * 1e6
#     20 rows u16, 22 cols u16
#     24 count u32         number of segment records
#     28 reserved u32
#   index: (rows*cols + 1) x u32   first record of each cell (row-major, row 0 = south);
#                                  cell i holds records index[i] .. index[i+1]-1
#   records: count x 20 bytes
#     lat1 i32, lon1 i32, lat2 i32, lon2 i32   degrees * 1e6
#     limit u8     km/h from the maxspeed tag, 0 = not tagged
#     class u8     road type, index into CLASSES below
#     reserved u16
# A segment is stored in every cell that comes within Margin of it, so a lookup only reads one cell.
param(
  [double]$South = 24.33, [double]$West = 39.45, [double]$North = 24.62, [double]$East = 39.80,
  [string]$Out = 'roads.bin',
  [double]$Cell = 0.005,     # ~550 m
  [double]$Margin = 0.0004,  # ~40 m
  [string]$Pbf,
  [switch]$Update
)
$ErrorActionPreference = 'Stop'

if (-not $Pbf) {
  $dir = Join-Path $env:TEMP 'speed-limit-roads'
  New-Item -ItemType Directory -Force $dir | Out-Null
  $Pbf = Join-Path $dir 'gcc-states-latest.osm.pbf'
  if ($Update -or -not (Test-Path $Pbf)) {
    Write-Host 'downloading the Gulf states map from download.geofabrik.de (~250 MB)...'
    curl.exe -L --fail -o "$Pbf.part" https://download.geofabrik.de/asia/gcc-states-latest.osm.pbf
    if ($LASTEXITCODE) { throw 'download failed' }
    Move-Item -Force "$Pbf.part" $Pbf
  }
}

Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;
using System.Text.RegularExpressions;

// Minimal reader for .osm.pbf files: just enough to pull out roads and their node positions.
public static class RoadsBuilder {
  // Same order as ROAD_CLASSES in index.html
  static readonly string[] CLASSES = { "motorway", "trunk", "primary", "secondary", "tertiary", "unclassified", "residential",
    "living_street", "service", "motorway_link", "trunk_link", "primary_link", "secondary_link", "tertiary_link" };

  // ---- protobuf ----
  static ulong Varint(byte[] b, ref int p) {
    ulong r = 0; int s = 0;
    while (true) { byte x = b[p++]; r |= (ulong)(x & 0x7f) << s; if (x < 0x80) return r; s += 7; }
  }
  static long Zz(ulong v) { return (long)(v >> 1) ^ -(long)(v & 1); }
  static void Skip(byte[] b, ref int p, int wt) {
    if (wt == 0) Varint(b, ref p);
    else if (wt == 1) p += 8;
    else if (wt == 2) { int l = (int)Varint(b, ref p); p += l; }
    else if (wt == 5) p += 4;
    else throw new Exception("bad wire type " + wt);
  }
  static byte[] ReadExact(Stream s, int n) {
    var b = new byte[n]; int got = 0;
    while (got < n) { int r = s.Read(b, got, n - got); if (r <= 0) throw new EndOfStreamException(); got += r; }
    return b;
  }

  // Calls onData with each decompressed PrimitiveBlock; stops when it returns false
  static void ReadFile(string path, Func<byte[], bool> onData) {
    using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20)) {
      var lb = new byte[4];
      while (fs.Read(lb, 0, 4) == 4) {
        int hl = (lb[0] << 24) | (lb[1] << 16) | (lb[2] << 8) | lb[3];
        var hb = ReadExact(fs, hl);
        string type = null; int size = 0, p = 0;
        while (p < hl) {
          ulong key = Varint(hb, ref p); int f = (int)(key >> 3), wt = (int)(key & 7);
          if (f == 1 && wt == 2) { int l = (int)Varint(hb, ref p); type = Encoding.ASCII.GetString(hb, p, l); p += l; }
          else if (f == 3 && wt == 0) size = (int)Varint(hb, ref p);
          else Skip(hb, ref p, wt);
        }
        var blob = ReadExact(fs, size);
        if (type != "OSMData") continue;
        byte[] raw = null; int rawSize = 0, zs = -1, zl = 0; p = 0;
        while (p < size) {
          ulong key = Varint(blob, ref p); int f = (int)(key >> 3), wt = (int)(key & 7);
          if (f == 1 && wt == 2) { int l = (int)Varint(blob, ref p); raw = new byte[l]; Buffer.BlockCopy(blob, p, raw, 0, l); p += l; }
          else if (f == 2 && wt == 0) rawSize = (int)Varint(blob, ref p);
          else if (f == 3 && wt == 2) { zl = (int)Varint(blob, ref p); zs = p; p += zl; }
          else Skip(blob, ref p, wt);
        }
        if (raw == null) {
          if (zs < 0) throw new Exception("unsupported compression in the .pbf file");
          raw = new byte[rawSize];
          // zlib = 2-byte header + deflate
          using (var z = new DeflateStream(new MemoryStream(blob, zs + 2, zl - 2), CompressionMode.Decompress)) {
            int got = 0; while (got < rawSize) { int n = z.Read(raw, got, rawSize - got); if (n <= 0) break; got += n; }
          }
        }
        if (!onData(raw)) return;
      }
    }
  }

  class Block {
    public byte[] b;
    public List<int> strStart = new List<int>(), strLen = new List<int>();
    public List<int> grpStart = new List<int>(), grpEnd = new List<int>();
    public long gran = 100, latOff = 0, lonOff = 0;
    string[] cache;
    public string S(int i) {
      if (cache == null) cache = new string[strStart.Count];
      return cache[i] ?? (cache[i] = Encoding.UTF8.GetString(b, strStart[i], strLen[i]));
    }
  }
  static Block Parse(byte[] b) {
    var k = new Block { b = b }; int p = 0;
    while (p < b.Length) {
      ulong key = Varint(b, ref p); int f = (int)(key >> 3), wt = (int)(key & 7);
      if (f == 1 && wt == 2) {
        int end = (int)Varint(b, ref p) + p;
        while (p < end) {
          ulong k2 = Varint(b, ref p);
          if ((k2 >> 3) == 1 && (k2 & 7) == 2) { int l = (int)Varint(b, ref p); k.strStart.Add(p); k.strLen.Add(l); p += l; }
          else Skip(b, ref p, (int)(k2 & 7));
        }
      }
      else if (f == 2 && wt == 2) { int l = (int)Varint(b, ref p); k.grpStart.Add(p); k.grpEnd.Add(p + l); p += l; }
      else if (f == 17 && wt == 0) k.gran = (long)Varint(b, ref p);
      else if (f == 19 && wt == 0) k.latOff = (long)Varint(b, ref p);
      else if (f == 20 && wt == 0) k.lonOff = (long)Varint(b, ref p);
      else Skip(b, ref p, wt);
    }
    return k;
  }

  static readonly Regex SPEED = new Regex(@"^(\d+(?:\.\d+)?)\s*(mph|km/?h|kph)?$", RegexOptions.IgnoreCase);
  static int ParseMaxspeed(string v) {
    if (string.IsNullOrEmpty(v)) return 0;
    var m = SPEED.Match(v.Split(';')[0].Trim());
    if (!m.Success) return 0;
    double n = double.Parse(m.Groups[1].Value, System.Globalization.CultureInfo.InvariantCulture);
    if (m.Groups[2].Value.ToLowerInvariant() == "mph") n *= 1.609;
    return Math.Min(255, (int)Math.Round(n));
  }

  public static string Build(string pbf, string outPath, double south, double west, double north, double east, double cell, double margin) {
    var classIndex = new Dictionary<string, int>();
    for (int i = 0; i < CLASSES.Length; i++) classIndex[CLASSES[i]] = i;

    // Pass 1: roads (all of the file; their nodes are not placed yet)
    var wayRefs = new List<long[]>(); var wayLimit = new List<byte>(); var wayCls = new List<byte>();
    var needList = new List<long>();
    var keys = new List<int>(); var vals = new List<int>(); var refs = new List<long>();
    ReadFile(pbf, raw => {
      var k = Parse(raw); var b = k.b;
      for (int g = 0; g < k.grpStart.Count; g++) {
        int p = k.grpStart[g], ge = k.grpEnd[g];
        while (p < ge) {
          ulong key = Varint(b, ref p); int f = (int)(key >> 3), wt = (int)(key & 7);
          if (f != 3 || wt != 2) { Skip(b, ref p, wt); continue; }
          int we = (int)Varint(b, ref p) + p;
          keys.Clear(); vals.Clear(); refs.Clear();
          while (p < we) {
            ulong k2 = Varint(b, ref p); int f2 = (int)(k2 >> 3), w2 = (int)(k2 & 7);
            if (w2 == 2 && (f2 == 2 || f2 == 3 || f2 == 8)) {
              int pe = (int)Varint(b, ref p) + p; long acc = 0;
              while (p < pe) {
                ulong v = Varint(b, ref p);
                if (f2 == 2) keys.Add((int)v); else if (f2 == 3) vals.Add((int)v); else { acc += Zz(v); refs.Add(acc); }
              }
            } else Skip(b, ref p, w2);
          }
          int cls = -1, limit = 0, fwd = 0, back = 0;
          for (int i = 0; i < keys.Count; i++) {
            string kk = k.S(keys[i]);
            if (kk == "highway") { int c; if (classIndex.TryGetValue(k.S(vals[i]), out c)) cls = c; }
            else if (kk == "maxspeed") limit = ParseMaxspeed(k.S(vals[i]));
            else if (kk == "maxspeed:forward") fwd = ParseMaxspeed(k.S(vals[i]));
            else if (kk == "maxspeed:backward") back = ParseMaxspeed(k.S(vals[i]));
          }
          if (cls < 0 || refs.Count < 2) continue;
          if (limit == 0) limit = fwd != 0 ? fwd : back;
          wayRefs.Add(refs.ToArray()); wayLimit.Add((byte)limit); wayCls.Add((byte)cls);
          needList.AddRange(refs);
        }
      }
      return true;
    });

    // Pass 2: positions of those roads' nodes (nodes come before ways in the file)
    needList.Sort();
    int u = 0;
    for (int i = 0; i < needList.Count; i++) if (i == 0 || needList[i] != needList[i - 1]) needList[u++] = needList[i];
    var need = new long[u]; needList.CopyTo(0, need, 0, u); needList = null;
    var lat = new int[u]; var lon = new int[u];
    for (int i = 0; i < u; i++) lat[i] = int.MinValue;
    ReadFile(pbf, raw => {
      var k = Parse(raw); var b = k.b; bool sawWays = false;
      for (int g = 0; g < k.grpStart.Count; g++) {
        int p = k.grpStart[g], ge = k.grpEnd[g];
        while (p < ge) {
          ulong key = Varint(b, ref p); int f = (int)(key >> 3), wt = (int)(key & 7);
          if (f == 3) sawWays = true;
          if (wt != 2 || (f != 1 && f != 2)) { Skip(b, ref p, wt); continue; }
          int ne = (int)Varint(b, ref p) + p;
          if (f == 2) {
            int ids = -1, idsE = 0, las = -1, lasE = 0, los = -1, losE = 0;
            while (p < ne) {
              ulong k2 = Varint(b, ref p); int f2 = (int)(k2 >> 3), w2 = (int)(k2 & 7);
              if (w2 == 2 && (f2 == 1 || f2 == 8 || f2 == 9)) {
                int l = (int)Varint(b, ref p);
                if (f2 == 1) { ids = p; idsE = p + l; } else if (f2 == 8) { las = p; lasE = p + l; } else { los = p; losE = p + l; }
                p += l;
              } else Skip(b, ref p, w2);
            }
            long id = 0, la = 0, lo = 0;
            while (ids >= 0 && ids < idsE) {
              id += Zz(Varint(b, ref ids)); la += Zz(Varint(b, ref las)); lo += Zz(Varint(b, ref los));
              int at = Array.BinarySearch(need, id);
              if (at >= 0) { lat[at] = (int)((k.latOff + k.gran * la) / 1000); lon[at] = (int)((k.lonOff + k.gran * lo) / 1000); }
            }
          } else {
            long id = 0, la = 0, lo = 0;
            while (p < ne) {
              ulong k2 = Varint(b, ref p); int f2 = (int)(k2 >> 3), w2 = (int)(k2 & 7);
              if (f2 == 1 && w2 == 0) id = Zz(Varint(b, ref p));
              else if (f2 == 8 && w2 == 0) la = Zz(Varint(b, ref p));
              else if (f2 == 9 && w2 == 0) lo = Zz(Varint(b, ref p));
              else Skip(b, ref p, w2);
            }
            int at = Array.BinarySearch(need, id);
            if (at >= 0) { lat[at] = (int)((k.latOff + k.gran * la) / 1000); lon[at] = (int)((k.lonOff + k.gran * lo) / 1000); }
          }
        }
      }
      return !sawWays;
    });

    // Segments inside the area
    int S = (int)Math.Round(south * 1e6), W = (int)Math.Round(west * 1e6), N = (int)Math.Round(north * 1e6), E = (int)Math.Round(east * 1e6);
    var s1a = new List<int>(); var s1o = new List<int>(); var s2a = new List<int>(); var s2o = new List<int>();
    var sLim = new List<byte>(); var sCls = new List<byte>();
    int ways = 0, tagged = 0;
    for (int w = 0; w < wayRefs.Count; w++) {
      var r = wayRefs[w]; bool any = false;
      for (int i = 0; i < r.Length - 1; i++) {
        int a = Array.BinarySearch(need, r[i]), c = Array.BinarySearch(need, r[i + 1]);
        if (a < 0 || c < 0 || lat[a] == int.MinValue || lat[c] == int.MinValue) continue;
        bool inA = lat[a] >= S && lat[a] <= N && lon[a] >= W && lon[a] <= E;
        bool inC = lat[c] >= S && lat[c] <= N && lon[c] >= W && lon[c] <= E;
        if (!inA && !inC) continue;
        s1a.Add(lat[a]); s1o.Add(lon[a]); s2a.Add(lat[c]); s2o.Add(lon[c]); sLim.Add(wayLimit[w]); sCls.Add(wayCls[w]);
        any = true;
      }
      if (any) { ways++; if (wayLimit[w] != 0) tagged++; }
    }

    // Grid: count, prefix sums, fill
    int rows = (int)Math.Ceiling((north - south) / cell), cols = (int)Math.Ceiling((east - west) / cell);
    int C = (int)Math.Round(cell * 1e6), M = (int)Math.Round(margin * 1e6);
    var index = new uint[rows * cols + 1];
    Action<int, Action<int>> eachCell = (i, fn) => {
      int r0 = Math.Max(0, (int)Math.Floor((Math.Min(s1a[i], s2a[i]) - M - S) / (double)C));
      int r1 = Math.Min(rows - 1, (int)Math.Floor((Math.Max(s1a[i], s2a[i]) + M - S) / (double)C));
      int c0 = Math.Max(0, (int)Math.Floor((Math.Min(s1o[i], s2o[i]) - M - W) / (double)C));
      int c1 = Math.Min(cols - 1, (int)Math.Floor((Math.Max(s1o[i], s2o[i]) + M - W) / (double)C));
      for (int rr = r0; rr <= r1; rr++) for (int cc = c0; cc <= c1; cc++) fn(rr * cols + cc);
    };
    for (int i = 0; i < s1a.Count; i++) eachCell(i, x => index[x + 1]++);
    for (int x = 0; x < rows * cols; x++) index[x + 1] += index[x];
    uint count = index[rows * cols];
    var recs = new int[count];
    var fill = (uint[])index.Clone();
    for (int i = 0; i < s1a.Count; i++) { int seg = i; eachCell(i, x => recs[fill[x]++] = seg); }

    using (var bw = new BinaryWriter(File.Create(outPath))) {
      bw.Write(Encoding.ASCII.GetBytes("SLR1")); bw.Write((ushort)1); bw.Write((ushort)0);
      bw.Write(S); bw.Write(W); bw.Write(C); bw.Write((ushort)rows); bw.Write((ushort)cols); bw.Write(count); bw.Write(0u);
      foreach (var v in index) bw.Write(v);
      foreach (var i in recs) {
        bw.Write(s1a[i]); bw.Write(s1o[i]); bw.Write(s2a[i]); bw.Write(s2o[i]);
        bw.Write(sLim[i]); bw.Write(sCls[i]); bw.Write((ushort)0);
      }
    }
    return string.Format("{0} ways ({1} with a speed limit), {2} segments, {3}x{4} cells, {5:N1} MB",
      ways, tagged, s1a.Count, rows, cols, new FileInfo(outPath).Length / 1048576.0);
  }
}
'@

$outPath = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }
Write-Host "reading $Pbf ..."
$sw = [Diagnostics.Stopwatch]::StartNew()
$stats = [RoadsBuilder]::Build($Pbf, $outPath, $South, $West, $North, $East, $Cell, $Margin)
Write-Host ("{0}: {1} ({2:N0} s)" -f $Out, $stats, $sw.Elapsed.TotalSeconds)
