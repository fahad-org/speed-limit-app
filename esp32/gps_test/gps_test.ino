// GPS wiring/baud check. Listens on each free pin at common baud rates until it hears NMEA ("$G..."),
// then prints the satellite/fix status once a second. Output on the USB serial at 115200.
#include <Arduino.h>
#include <TinyGPSPlus.h>

static const int PINS[] = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 43, 44, 47, 48, 0, 3, 45, 46};
static const uint32_t BAUDS[] = {38400, 9600, 115200};   // MAX-M10S ships at 38400
static TinyGPSPlus gps;
static int foundPin = -1;
static uint32_t foundBaud = 0;

static bool hearsNmea(int pin, uint32_t baud) {
  Serial1.begin(baud, SERIAL_8N1, pin, -1);
  uint32_t end = millis() + 1400; int hits = 0; char prev = 0;
  while (millis() < end) {
    while (Serial1.available()) {
      char c = Serial1.read();
      if (prev == '$' && c == 'G') hits++;
      prev = c;
    }
    delay(1);
  }
  Serial1.end();
  return hits >= 2;
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=== GPS check ===");
  for (uint32_t baud : BAUDS) {
    Serial.printf("trying %u baud on %d pins...\n", baud, (int)(sizeof PINS / sizeof PINS[0]));
    for (int pin : PINS) {
      if (hearsNmea(pin, baud)) { foundPin = pin; foundBaud = baud; break; }
    }
    if (foundPin >= 0) break;
  }
  if (foundPin >= 0) {
    Serial.printf("FOUND GPS: its TX wire is on GPIO %d, baud %u\n", foundPin, (unsigned)foundBaud);
    Serial1.begin(foundBaud, SERIAL_8N1, foundPin, -1);
  } else {
    Serial.println("NO NMEA FOUND on any pin (check power 3V3/GND, and that the GPS TX wire is connected to the ESP32)");
  }
}

void loop() {
  if (foundPin < 0) { delay(5000); Serial.println("NO NMEA FOUND"); return; }
  uint32_t end = millis() + 1000;
  while (millis() < end) while (Serial1.available()) gps.encode(Serial1.read());
  Serial.printf("STATUS chars=%u ok=%u bad=%u | sats=%d | fix=%s | hdop=%.1f | lat=%.6f lon=%.6f | speed=%.1f km/h\n",
    (unsigned)gps.charsProcessed(), (unsigned)gps.passedChecksum(), (unsigned)gps.failedChecksum(),
    gps.satellites.isValid() ? (int)gps.satellites.value() : -1,
    gps.location.isValid() ? "YES" : "no",
    gps.hdop.isValid() ? gps.hdop.hdop() : -1.0,
    gps.location.lat(), gps.location.lng(), gps.speed.kmph());
}
