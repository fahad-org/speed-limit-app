#pragma once
// Bluetooth link to the phone app (remote/). Same idea as Nordic's UART service: the app writes lines of JSON to RX,
// and the device sends lines of JSON on TX (notifications). Lines end with '\n' and may be split over several packets.
//
//   app -> device   {"c":"auth","pin":"1234"}        must come first; nothing else works before it
//                   {"c":"get"}                      answer with the state now
//                   {"c":"fix","limit":80}           whole street (add "one":1 for just this piece; limit 0 = back to the guess)
//                   {"c":"undo"}   {"c":"flag"}   {"c":"set","off":5,"tol":2,"et":10,"br":100,"pin":"4321"}
//                   {"c":"edits"}                    sends the saved corrections: lines {"t":"e","d":"<hex>"} then {"t":"e","done":1}
//   device -> app   {"t":"s", ...}                   state, once a second
//                   {"t":"r","c":"fix","ok":1,"n":23}   answer to a command
//
// The Bluetooth callbacks run on another task, so they only collect lines; everything that touches the map or the
// screen state is done in bleLoop() on the main loop.
// Needs (from the sketch): the state variables, S (settings.h), edits.h.
#include <NimBLEDevice.h>

static const char* SVC_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* RX_UUID  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";   // app -> device
static const char* TX_UUID  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";   // device -> app
static const char* FLAGS_FILE = "/flags.csv";

static NimBLECharacteristic* txChar = nullptr;
static volatile bool bleConnected = false, bleSubscribed = false;
static volatile uint16_t bleMtu = 23;
static bool authed = false;
static bool uploading = false;           // true while a file is coming in: the screen shows UPDATING (later phases)
static String outBuf;                    // text waiting to be sent
static File exportFile; static bool exporting = false; static uint32_t exportCount = 0;

// ---- lines from the app (SPSC queue: the BLE task writes, loop() reads) ----
static const int Q_LEN = 6;
static String cmdQ[Q_LEN]; static volatile int qHead = 0, qTail = 0;
static String rxPart;
static void rxPush(const String& line) { int n = (qHead + 1) % Q_LEN; if (n == qTail) return; cmdQ[qHead] = line; qHead = n; }

// ---- tiny reader for flat JSON like {"c":"fix","limit":80} ----
static String jstr(const String& s, const char* k) {
  String key = String("\"") + k + "\":"; int i = s.indexOf(key); if (i < 0) return "";
  i += key.length(); while (i < (int)s.length() && s[i] == ' ') i++;
  if (i < (int)s.length() && s[i] == '"') { int j = s.indexOf('"', i + 1); return j < 0 ? "" : s.substring(i + 1, j); }
  int j = i; while (j < (int)s.length() && s[j] != ',' && s[j] != '}') j++;
  String v = s.substring(i, j); v.trim(); return v;
}
static bool jhas(const String& s, const char* k) { return s.indexOf(String("\"") + k + "\":") >= 0; }

static void say(const String& line) { if (outBuf.length() > 3000) outBuf = ""; outBuf += line; outBuf += '\n'; }
static void reply(const char* c, bool ok, const char* extra = "") {
  char b[120]; snprintf(b, sizeof b, "{\"t\":\"r\",\"c\":\"%s\",\"ok\":%d%s}", c, ok ? 1 : 0, extra); say(b);
}
static void sendState() {
  char b[300];
  snprintf(b, sizeof b,
    "{\"t\":\"s\",\"s\":%d,\"l\":%d,\"e\":%d,\"o\":%d,\"m\":%d,\"r\":%d,\"c\":%d,\"d\":%d,\"a\":%d,\"w\":%d,\"x\":%u,\"u\":%d,"
    "\"off\":%d,\"tol\":%d,\"et\":%d,\"br\":%d,\"v\":\"%s\",\"h\":%u}",
    speedKmh, limitKmh, estimated ? 1 : 0, over ? 1 : 0, (int)msg, haveCur ? 1 : 0, haveCur ? cur.cls : -1,
    haveCur && edLookup(cur) ? 1 : 0, satsUsed, satsView, (unsigned)edRecords, undoN,
    S.offset, S.tol, S.estTol, S.bright, FW_VERSION, (unsigned)ESP.getFreeHeap());
  say(b);
}

// ---- commands (run on the main loop) ----
static void doFix(const String& line) {
  int v = jstr(line, "limit").toInt();
  if (!jhas(line, "limit") || v < 0 || v > 200) { reply("fix", false, ",\"err\":\"range\""); return; }
  if (!haveCur) { reply("fix", false, ",\"err\":\"no road\""); return; }
  static Seg chain[UNDO_MAX];
  Seg seed = cur;
  int n = jstr(line, "one").toInt() ? 1 : chainOf(seed, chain, UNDO_MAX);
  if (n == 1) chain[0] = seed;
  int r = edApply(chain, n, (uint8_t)v);
  if (r < 0) { reply("fix", false, ",\"err\":\"storage\""); return; }
  setLimitFrom(cur); lastKey = 0xFFFFFFFF;          // show the new number now
  char x[24]; snprintf(x, sizeof x, ",\"n\":%d", r); reply("fix", true, x); sendState();
}
static void doUndo() {
  int r = edUndo();
  if (r < 0) { reply("undo", false, ",\"err\":\"storage\""); return; }
  if (haveCur) setLimitFrom(cur);
  lastKey = 0xFFFFFFFF;
  char x[24]; snprintf(x, sizeof x, ",\"n\":%d", r); reply("undo", true, x); sendState();
}
static void doFlag() {                                // "look at this place later"
  if (!gps.location.isValid()) { reply("flag", false, ",\"err\":\"no fix\""); return; }
  File f = MAP_FS.open(FLAGS_FILE, "a"); if (!f) { reply("flag", false, ",\"err\":\"storage\""); return; }
  f.printf("%.6f,%.6f,%d,%d\n", gps.location.lat(), gps.location.lng(), limitKmh, estimated ? 1 : 0); f.close();
  reply("flag", true);
}
static void doSet(const String& line) {
  auto in = [&](const char* k, int lo, int hi, int& dst) { if (jhas(line, k)) dst = constrain((int)jstr(line, k).toInt(), lo, hi); };
  in("off", 0, 20, S.offset); in("tol", 0, 20, S.tol); in("et", 0, 40, S.estTol); in("br", 5, 100, S.bright);
  if (jhas(line, "pin")) {
    String p = jstr(line, "pin");
    if (p.length() < 4 || p.length() > 8) { reply("set", false, ",\"err\":\"pin\""); return; }
    strcpy(S.pin, p.c_str());
  }
  settingsSave(); applyBrightness(); lastKey = 0xFFFFFFFF; reply("set", true); sendState();
}
static void doEdits() {
  if (exporting) { exportFile.close(); exporting = false; }
  exportFile = MAP_FS.open(EDITS_FILE, "r"); exportCount = 0;
  if (!exportFile) { say("{\"t\":\"e\",\"done\":1,\"n\":0}"); return; }
  exporting = true;
}
static void exportStep() {                            // one line per call, only when the previous one has gone out
  if (!exporting || outBuf.length() > 0) return;
  uint8_t r[80]; int got = exportFile.read(r, 80);
  if (got <= 0) { exportFile.close(); exporting = false; char b[60]; snprintf(b, sizeof b, "{\"t\":\"e\",\"done\":1,\"n\":%u}", (unsigned)exportCount); say(b); return; }
  String hex; hex.reserve(got * 2 + 24); hex = "{\"t\":\"e\",\"d\":\"";
  for (int i = 0; i < got; i++) { char h[3]; snprintf(h, sizeof h, "%02x", r[i]); hex += h; }
  hex += "\"}"; say(hex); exportCount += got / 20;
}
static void handleLine(const String& line) {
  String c = jstr(line, "c");
  if (c == "auth") {
    authed = jstr(line, "pin") == String(S.pin);
    reply("auth", authed, authed ? "" : ",\"err\":\"pin\""); if (authed) sendState(); return;
  }
  if (!authed) { reply(c.c_str(), false, ",\"err\":\"auth\""); return; }
  if (c == "get") sendState();
  else if (c == "fix") doFix(line);
  else if (c == "undo") doUndo();
  else if (c == "flag") doFlag();
  else if (c == "set") doSet(line);
  else if (c == "edits") doEdits();
  else reply(c.c_str(), false, ",\"err\":\"unknown\"");
}

// ---- Bluetooth plumbing ----
class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& ci) override { bleConnected = true; authed = false; s->updateConnParams(ci.getConnHandle(), 12, 24, 0, 400); }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& ci, int reason) override {
    bleConnected = false; bleSubscribed = false; authed = false; bleMtu = 23; NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo& ci) override { bleMtu = mtu; }
};
class RxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& ci) override {
    std::string v = c->getValue();
    for (char ch : v) {
      if (ch == '\n') { if (rxPart.length()) rxPush(rxPart); rxPart = ""; }
      else if (ch != '\r' && rxPart.length() < 200) rxPart += ch;
    }
  }
};
class TxCb : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& ci, uint16_t sub) override { bleSubscribed = sub != 0; }
};
static SrvCb srvCb; static RxCb rxCb; static TxCb txCb;

static void bleBegin() {
  NimBLEDevice::init("SpeedLimit");
  NimBLEDevice::setMTU(247);
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(&srvCb);
  NimBLEService* svc = srv->createService(SVC_UUID);
  NimBLECharacteristic* rx = svc->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&rxCb);
  txChar = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  txChar->setCallbacks(&txCb);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setName("SpeedLimit");
  adv->start();
}

static void bleLoop() {
  static uint32_t lastState = 0;
  while (qTail != qHead) { String l = cmdQ[qTail]; qTail = (qTail + 1) % Q_LEN; handleLine(l); }
  if (!bleConnected) { outBuf = ""; if (exporting) { exportFile.close(); exporting = false; } return; }
  if (authed && millis() - lastState > 1000 && outBuf.length() == 0 && !exporting) { lastState = millis(); sendState(); }
  exportStep();
  // send what is waiting, in packets that fit the connection (3 bytes of each packet go to the Bluetooth header)
  for (int i = 0; i < 4 && outBuf.length() && bleSubscribed; i++) {
    int n = min((int)outBuf.length(), (int)bleMtu - 3);
    if (n < 20) n = 20;
    n = min(n, (int)outBuf.length());
    if (!txChar->notify((const uint8_t*)outBuf.c_str(), n)) break;
    outBuf.remove(0, n);
  }
}
