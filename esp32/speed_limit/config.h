#pragma once
// ---- Wiring: change these to match your board, nothing else needs editing ----
// ESP32-S3 pins to avoid: 0/3/45/46 (strapping), 19/20 (USB), 26-32 (flash), 33-37 (PSRAM on some boards)

// 1.28" round IPS display (GC9A01, 240x240) on the FSPI bus
#define TFT_SCK   12
#define TFT_MOSI  11
#define TFT_CS    10
#define TFT_DC     9
#define TFT_RST    8
#define TFT_BL     7     // backlight, -1 if tied to 3V3

// Where roads.bin lives: 0 = in the chip's own flash (partition "map", see partitions.csv), 1 = SD card
#define USE_SD     0

// SD card module (only used when USE_SD is 1) on its own SPI bus
#define SD_SCK    13
#define SD_MISO   14
#define SD_MOSI   15
#define SD_CS     16

// GPS module (NMEA over serial). GPS TX -> GPS_RX_PIN, GPS RX -> GPS_TX_PIN
#define GPS_RX_PIN 18
#define GPS_TX_PIN 17
#define GPS_BAUD   38400 // u-blox MAX-M10S default (checked with esp32/gps_test); 9600 for older modules

// Optional buzzer (not fitted for now: -1 = screen alert only)
#define BUZZER_PIN -1

// Hold this button while powering on to run the fake drive (no GPS or SD needed)
#define DEMO_PIN   0     // the BOOT button
