#pragma once
// Settings that can be changed from the phone app (saved in the chip's NVS, so they survive power-off)
#include <Preferences.h>

struct Settings {
  int offset;      // km/h added to the GPS speed (car speedometers read high)
  int tol;         // km/h over a REAL limit before the screen turns red
  int estTol;      // km/h over a GUESSED limit before the screen turns red
  int bright;      // screen brightness, 5..100 %
  char pin[9];     // PIN the phone app must send first (4 to 8 digits)
};
static Settings S;
static Preferences prefs;

static void settingsLoad() {
  S.offset = SPEED_OFFSET_KMH; S.tol = 2; S.estTol = EST_TOLERANCE; S.bright = 100;
  strncpy(S.pin, BLE_PIN, sizeof S.pin - 1); S.pin[sizeof S.pin - 1] = 0;
  if (!prefs.begin("slc", true)) return;
  S.offset = prefs.getInt("off", S.offset); S.tol = prefs.getInt("tol", S.tol);
  S.estTol = prefs.getInt("etol", S.estTol); S.bright = prefs.getInt("br", S.bright);
  String p = prefs.getString("pin", S.pin);
  if (p.length() >= 4 && p.length() < sizeof S.pin) strcpy(S.pin, p.c_str());
  prefs.end();
}
static void settingsSave() {
  if (!prefs.begin("slc", false)) return;
  prefs.putInt("off", S.offset); prefs.putInt("tol", S.tol); prefs.putInt("etol", S.estTol);
  prefs.putInt("br", S.bright); prefs.putString("pin", S.pin);
  prefs.end();
}
static void applyBrightness() {
  if (TFT_BL >= 0) ledcWrite(TFT_BL, map(constrain(S.bright, 5, 100), 0, 100, 0, 255));
}
