// Speed Limit for ESP32-S3: same idea as the phone app (index.html).
// GPS -> find the road in roads.bin on the SD card -> show its speed limit on the round screen,
// and turn the screen red when you are over it. Format of roads.bin: scripts/build-roads.ps1.
#include <Arduino.h>
#include <SPI.h>
#include <TinyGPSPlus.h>
#include <Arduino_GFX_Library.h>
#include "config.h"
#include "types.h"
#include "theme.h"
#if USE_SD
#include <SD.h>
#define MAP_FS SD
#else
#include <LittleFS.h>
#define MAP_FS LittleFS
#endif

// ---- same numbers as index.html ----
static const double TOLERANCE = 2;   // km/h above the limit before alerting (GPS jitter)
static const double STICK_M = 18;    // stay on current road while within this distance
static const double MATCH_M = 40;    // farthest a road can be and still count as the one you are on
static const uint32_t REPEAT_MS = 6000;
static const uint8_t NUM_CLASSES = 14;
// Same order as ROAD_CLASSES in index.html (class byte in roads.bin)
static const uint8_t DEFAULT_LIMITS[NUM_CLASSES] = {
  120, 100, 80, 70, 60, 60, 40, 20, 20,   // motorway trunk primary secondary tertiary unclassified residential living_street service
  60, 60, 50, 50, 40                      // motorway_link trunk_link primary_link secondary_link tertiary_link
};

// ---- roads.bin ----
static const int MAX_SEGS = 600;
static Seg segs[MAX_SEGS];
static File roadsFile;
static int32_t rdSouth, rdWest, rdCell;
static uint16_t rdRows, rdCols;
static uint32_t rdRecStart;

static bool loadRoads() {
  roadsFile = MAP_FS.open("/roads.bin", "r");
  if (!roadsFile) return false;
  uint8_t h[32];
  if (roadsFile.read(h, 32) != 32 || memcmp(h, "SLR1", 4) != 0) return false;
  memcpy(&rdSouth, h + 8, 4); memcpy(&rdWest, h + 12, 4); memcpy(&rdCell, h + 16, 4);
  memcpy(&rdRows, h + 20, 2); memcpy(&rdCols, h + 22, 2);
  rdRecStart = 32 + ((uint32_t)rdRows * rdCols + 1) * 4;
  return true;
}

// Reads the segments of the grid cell around p into segs[]. Returns -1 outside the map.
static int segsAt(double lat, double lon) {
  long r = floor((lat * 1e6 - rdSouth) / rdCell), c = floor((lon * 1e6 - rdWest) / rdCell);
  if (r < 0 || c < 0 || r >= rdRows || c >= rdCols) return -1;
  uint32_t idx[2];
  roadsFile.seek(32 + ((uint32_t)r * rdCols + c) * 4);
  if (roadsFile.read((uint8_t*)idx, 8) != 8) return -1;
  uint32_t n = idx[1] > idx[0] ? idx[1] - idx[0] : 0;
  if (n > MAX_SEGS) n = MAX_SEGS;
  roadsFile.seek(rdRecStart + idx[0] * 20);
  if (n && roadsFile.read((uint8_t*)segs, n * 20) != (int)(n * 20)) return -1;
  return (int)n;
}

// ---- geometry (metres) ----
static const double R_EARTH = 6371000.0;
static Hit toSeg(double plat, double plon, const Seg& s) {
  double k = cos(plat * DEG_TO_RAD);
  double ax = (s.lon1 / 1e6 - plon) * DEG_TO_RAD * R_EARTH * k, ay = (s.lat1 / 1e6 - plat) * DEG_TO_RAD * R_EARTH;
  double bx = (s.lon2 / 1e6 - plon) * DEG_TO_RAD * R_EARTH * k, by = (s.lat2 / 1e6 - plat) * DEG_TO_RAD * R_EARTH;
  double dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
  double t = len ? constrain(-(ax * dx + ay * dy) / len, 0.0, 1.0) : 0;
  return { hypot(ax + t * dx, ay + t * dy), fmod(atan2(dx, dy) * RAD_TO_DEG + 360, 360) };
}
static bool sameSeg(const Seg& a, const Seg& b) {
  return a.lat1 == b.lat1 && a.lon1 == b.lon1 && a.lat2 == b.lat2 && a.lon2 == b.lon2;
}

// ---- state ----
enum Msg : uint8_t { MSG_NONE, MSG_NO_SD, MSG_NO_MAP, MSG_NO_GPS, MSG_OUT_OF_MAP, MSG_NO_DATA };
static int satsUsed = 0, satsView = 0;   // satellites used for the fix / visible in the sky
static int limitKmh = 0;          // 0 = unknown
static bool estimated = false;
static int speedKmh = 0;
static Msg msg = MSG_NO_GPS;
static bool over = false;
static uint32_t lastAlert = 0;
static bool haveCur = false;
static Seg cur;
static bool demo = false;

// Nearest road, preferring roads that point the way you are driving (same scoring as updateRoad() in index.html)
static void updateRoad(double lat, double lon, bool headingOk, double heading) {
  int n = segsAt(lat, lon);
  if (n < 0) { haveCur = false; limitKmh = 0; estimated = false; msg = MSG_OUT_OF_MAP; return; }
  msg = MSG_NONE;
  int best = -1, curIdx = -1;
  double bestScore = 1e9, curScore = 0, curD = 0;
  for (int i = 0; i < n; i++) {
    Hit m = toSeg(lat, lon, segs[i]);
    if (m.d > MATCH_M) continue;
    double score = m.d;
    if (headingOk) {
      double diff = fmod(fmod(heading - m.dir, 180.0) + 180.0, 180.0);
      score += 25 * sin(min(diff, 180 - diff) * DEG_TO_RAD);
    }
    if (score < bestScore) { bestScore = score; best = i; }
    if (haveCur && sameSeg(segs[i], cur)) { curIdx = i; curScore = score; curD = m.d; }
  }
  // Stay on the current road unless another one is clearly better
  if (curIdx >= 0 && curD < STICK_M && curScore - bestScore < 8) best = curIdx;
  if (best < 0) { haveCur = false; limitKmh = 0; estimated = false; return; }
  cur = segs[best]; haveCur = true;
  if (cur.limit) { limitKmh = cur.limit; estimated = false; }
  else if (cur.cls < NUM_CLASSES) { limitKmh = DEFAULT_LIMITS[cur.cls]; estimated = true; }
  else { limitKmh = 0; estimated = false; }
}

// ---- alert ----
static void alertNow() {
  lastAlert = millis();
#if BUZZER_PIN >= 0
  tone(BUZZER_PIN, 880, 120); delay(150); tone(BUZZER_PIN, 880, 120);
#endif
}
static void checkOver() {
  bool now = limitKmh > 0 && speedKmh > limitKmh + TOLERANCE;
  // An estimated limit only turns the screen red; no beep for a limit we are guessing
  if (now && !estimated && (!over || millis() - lastAlert > REPEAT_MS)) alertNow();
  over = now;
}

// ---- screen ---- (all look-and-feel numbers are in theme.h)
static Arduino_DataBus* bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED, FSPI);
static Arduino_GFX* gfx = new Arduino_GC9A01(bus, TFT_RST, 0 /*rotation*/, true /*IPS*/);
#define C565(c) ((uint16_t)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x001F)))   // 0xRRGGBB -> RGB565

static void text(const char* s, int cx, int cy, int size, uint32_t fg, uint32_t bg) {
  int w = 6 * size * strlen(s), h = 8 * size;
  gfx->setTextSize(size); gfx->setTextColor(C565(fg), C565(bg));
  gfx->setCursor(cx - w / 2, cy - h / 2); gfx->print(s);
}

// While over the limit the inside of the sign alternates between its normal look and the red flash look
static bool flashOn() { return over && (millis() % (FLASH_ON_MS + FLASH_OFF_MS)) < FLASH_ON_MS; }

// Signal bars from the number of satellites in use (a fix needs 4; more = steadier)
static int satBars() { return satsUsed >= 9 ? 4 : satsUsed >= 6 ? 3 : satsUsed >= 4 ? 2 : satsUsed >= 1 ? 1 : 0; }
static void drawSats() {
  if (msg == MSG_NO_DATA) {
    text(TXT_CHECK1, SIGN_CX, SIGN_CY + CHECK1_DY, CHECK_SIZE, COL_MUTED_TEXT, COL_SIGN_FACE);
    text(TXT_CHECK2, SIGN_CX, SIGN_CY + CHECK2_DY, CHECK_SIZE, COL_MUTED_TEXT, COL_SIGN_FACE);
    return;
  }
  char b[24]; snprintf(b, sizeof b, "%s %d/%d", TXT_SAT_PREFIX, satsUsed, satsView);
  text(b, SIGN_CX, SIGN_CY + SAT_TEXT_DY, SAT_TEXT_SIZE, COL_SAT_TEXT, COL_SIGN_FACE);
  int bars = satBars();
  for (int i = 0; i < 4; i++) {   // four bars, growing taller
    int h = BAR_H0 + i * BAR_H_STEP, x = SIGN_CX + BAR_X0 + i * BAR_STEP, y = SIGN_CY + BAR_BASE_DY - h;
    if (i < bars) gfx->fillRect(x, y, BAR_W, h, C565(satsUsed >= 4 ? COL_BAR_OK : COL_BAR_WARN));
    else gfx->drawRect(x, y, BAR_W, h, C565(COL_BAR_EMPTY));
  }
}
static void drawAll(bool flash) {
  uint32_t face = flash ? COL_FLASH_FACE : COL_SIGN_FACE;
  uint32_t tLimit = flash ? COL_FLASH_TEXT : COL_LIMIT_TEXT, tMuted = flash ? COL_FLASH_TEXT : COL_MUTED_TEXT;
  gfx->fillScreen(C565(COL_BG));
  uint32_t ring = limitKmh == 0 ? COL_RING_UNKNOWN : COL_RING;
  if (estimated) {   // dashed ring = the limit is a guess from the road type
    for (int i = 0; i < DASH_COUNT; i++) {
      float a = DASH_START_DEG + i * 360.0f / DASH_COUNT;
      gfx->fillArc(SIGN_CX, SIGN_CY, SIGN_R_IN, SIGN_R_OUT, a, a + DASH_LEN_DEG, C565(ring));
    }
  } else {
    gfx->fillCircle(SIGN_CX, SIGN_CY, SIGN_R_OUT, C565(ring));
  }
  gfx->fillCircle(SIGN_CX, SIGN_CY, SIGN_R_IN, C565(face));
  char b[8];
  if (limitKmh) {
    snprintf(b, sizeof b, "%d", limitKmh);
    text(b, SIGN_CX, SIGN_CY + LIMIT_DY, limitKmh >= 100 ? LIMIT_SIZE_3DIGIT : LIMIT_SIZE_2DIGIT, tLimit, face);
    if (estimated) text(TXT_EST, SIGN_CX, SIGN_CY + EST_DY, EST_SIZE, tMuted, face);
  } else {
    const char* m = TXT_UNKNOWN;
    switch (msg) {
      case MSG_NO_SD: m = TXT_NO_SD; break;
      case MSG_NO_MAP: m = TXT_NO_MAP; break;
      case MSG_NO_GPS: m = TXT_NO_GPS; break;
      case MSG_NO_DATA: m = TXT_NO_DATA; break;
      case MSG_OUT_OF_MAP: m = TXT_OFF_MAP; break;
      default: break;
    }
    if (msg == MSG_NO_GPS || msg == MSG_NO_DATA) {
      text(m, SIGN_CX, SIGN_CY + MSG_DY_SEARCH, MSG_SIZE, COL_MUTED_TEXT, face);
      drawSats();
    } else if (msg == MSG_NONE) {
      text(m, SIGN_CX, SIGN_CY + LIMIT_DY, LIMIT_SIZE_2DIGIT, COL_MUTED_TEXT, face);
    } else {
      text(m, SIGN_CX, SIGN_CY + MSG_DY_SIMPLE, MSG_SIZE, COL_MUTED_TEXT, face);
    }
  }
  // your speed goes under the limit, except while there is no GPS fix or no map
  if (msg != MSG_NO_GPS && msg != MSG_NO_DATA && msg != MSG_NO_SD && msg != MSG_NO_MAP) {
    snprintf(b, sizeof b, "%d", speedKmh);
    text(b, SPEED_CX, SPEED_Y, SPEED_SIZE, flash ? COL_FLASH_TEXT : COL_SPEED_TEXT, face);
    text(TXT_UNIT, UNIT_CX, UNIT_Y, UNIT_SIZE, flash ? COL_FLASH_TEXT : COL_UNIT_TEXT, face);
  }
}
// Redraws the whole screen whenever anything shown on it changes (about once a second while driving,
// twice a second per flash while over the limit)
static void render() {
  static uint32_t lastKey = 0xFFFFFFFF; static int lastSpeed = -1, lastSat = -1;
  bool flash = flashOn();
  uint32_t key = limitKmh | (estimated << 8) | (flash << 9) | (msg << 10);
  int sat = (msg == MSG_NO_GPS) ? (satsUsed | (satsView << 8)) : 0;   // satellites only matter on the NO GPS screen
  if (key != lastKey || speedKmh != lastSpeed || sat != lastSat) { lastKey = key; lastSpeed = speedKmh; lastSat = sat; drawAll(flash); }
}

// ---- GPS ----
static TinyGPSPlus gps;
// "Satellites in view" is field 3 of each constellation's GSV sentence (GPS, GLONASS, Galileo, BeiDou)
static TinyGPSCustom viewGP(gps, "GPGSV", 3), viewGL(gps, "GLGSV", 3), viewGA(gps, "GAGSV", 3), viewGB(gps, "GBGSV", 3);
static void readGps() {
  while (Serial1.available()) gps.encode(Serial1.read());
  static uint32_t lastFixMs = 0, lastChars = 0, lastCharMs = 0;
  if (gps.charsProcessed() != lastChars) { lastChars = gps.charsProcessed(); lastCharMs = millis(); }
  bool alive = millis() - lastCharMs < 3000;   // is the module sending anything at all?
  satsUsed = (alive && gps.satellites.isValid()) ? (int)gps.satellites.value() : 0;
  satsView = alive ? atoi(viewGP.value()) + atoi(viewGL.value()) + atoi(viewGA.value()) + atoi(viewGB.value()) : 0;
  if (gps.location.isUpdated()) {
    lastFixMs = millis();
    double kmh = gps.speed.isValid() ? gps.speed.kmph() : 0;
    speedKmh = kmh < 3 ? 0 : (int)round(kmh);
    bool headingOk = gps.course.isValid() && speedKmh >= 15;
    bool accurate = !gps.hdop.isValid() || gps.hdop.hdop() < 6;   // roughly the "accuracy < 60 m" test in index.html
    if (accurate) updateRoad(gps.location.lat(), gps.location.lng(), headingOk, gps.course.deg());
    checkOver();
  }
  if (millis() - lastFixMs > 5000) {   // lost the satellites
    speedKmh = 0; limitKmh = 0; estimated = false; msg = alive ? MSG_NO_GPS : MSG_NO_DATA; over = false; haveCur = false;
  }
}

// ---- demo drive (hold BOOT at power-up): shows every kind of screen without GPS or SD ----
static void runDemo() {
  struct Step { int speed, limit; bool est; };
  static const Step steps[] = { {0, 0, false}, {48, 60, true}, {70, 60, true}, {75, 80, false}, {92, 80, false},
                                {112, 120, false}, {135, 120, false}, {35, 40, true}, {55, 40, true} };
  static uint32_t t0 = 0; static int i = -1;
  if (i < 0 || millis() - t0 > 3000) {
    i = (i + 1) % (sizeof steps / sizeof steps[0]); t0 = millis();
    speedKmh = steps[i].speed; limitKmh = steps[i].limit; estimated = steps[i].est;
    msg = limitKmh ? MSG_NONE : MSG_NO_GPS;
    satsUsed = limitKmh ? 9 : 3; satsView = limitKmh ? 14 : 8;   // sample numbers for the NO GPS screen
    checkOver();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(DEMO_PIN, INPUT_PULLUP);
  demo = digitalRead(DEMO_PIN) == LOW;
  if (TFT_BL >= 0) { pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH); }
  if (BUZZER_PIN >= 0) pinMode(BUZZER_PIN, OUTPUT);
  gfx->begin();
  if (demo) { Serial.println("demo mode"); return; }

#if USE_SD
  static SPIClass sdSpi(HSPI);
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool mounted = SD.begin(SD_CS, sdSpi, 20000000);
  if (!mounted) { msg = MSG_NO_SD; Serial.println("SD card not found"); }
#else
  bool mounted = LittleFS.begin(false, "/littlefs", 5, "map");
  if (!mounted) { msg = MSG_NO_MAP; Serial.println("map partition not mounted"); }
#endif
  if (!mounted) {}
  else if (!loadRoads()) { msg = MSG_NO_MAP; Serial.println("roads.bin missing or bad"); }
  else Serial.printf("roads.bin ok: %u x %u cells\n", rdRows, rdCols);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

void loop() {
  if (demo) runDemo();
  else if (msg != MSG_NO_SD && msg != MSG_NO_MAP) readGps();
  render();
  delay(20);
}
