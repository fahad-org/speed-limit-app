#pragma once
// Look of the round 240x240 screen. Every number here can be edited visually in esp32/theme-editor.html,
// which saves this same file. Colors are 0xRRGGBB, sizes are text magnification (1 = 6x8 px per letter),
// positions are pixels (screen is 240x240, centre at 120,120; "DY" = pixels below the sign centre).

// ---- colors ----
#define COL_BG              0x0A0E14   // screen background (corners, hidden by the round glass)
#define COL_SIGN_FACE       0xFFFFFF   // inside of the round sign
#define COL_RING            0xD62020   // sign ring
#define COL_RING_UNKNOWN    0x78808C   // sign ring when there is no limit / message
#define COL_LIMIT_TEXT      0x000000   // the street's speed limit
#define COL_MUTED_TEXT      0x78808C   // EST, NO GPS and other messages
#define COL_SPEED_TEXT      0x000000   // your car's speed
#define COL_UNIT_TEXT       0x78808C   // km/h
#define COL_SAT_TEXT        0x000000   // SAT 3/9
#define COL_BAR_OK          0x28A046   // signal bars, 4+ satellites in use
#define COL_BAR_WARN        0xE69600   // signal bars, fewer than 4
#define COL_BAR_EMPTY       0x78808C   // empty bar outline

// ---- flashing when over the limit (inside of the sign flips between its normal look and this one) ----
#define COL_FLASH_FACE      0xD62020   // inside of the sign during the red flash
#define COL_FLASH_TEXT      0xFFFFFF   // all numbers and text during the red flash
#define FLASH_ON_MS         350        // how long it stays red
#define FLASH_OFF_MS        350        // how long it stays white

// ---- the round sign ----
#define SIGN_CX             120
#define SIGN_CY             120
#define SIGN_R_OUT          120        // 120 = ring reaches the edge of the screen
#define SIGN_R_IN           102
#define DASH_COUNT          24         // dashes in the ring when the limit is an estimate
#define DASH_LEN_DEG        10         // length of each dash, degrees (360 / DASH_COUNT = no gap)
#define DASH_START_DEG      0

// ---- street limit (centre) ----
#define LIMIT_SIZE_2DIGIT   9
#define LIMIT_SIZE_3DIGIT   6
#define LIMIT_DY            -28
#define EST_SIZE            2
#define EST_DY              -82
#define TXT_EST             "EST"

// ---- your car's speed (under the limit, inside the sign) ----
#define SPEED_CX            120
#define SPEED_Y             158
#define SPEED_SIZE          4
#define UNIT_CX             120
#define UNIT_Y              188
#define UNIT_SIZE           2
#define TXT_UNIT            "km/h"

// ---- messages inside the sign ----
#define MSG_SIZE            3
#define MSG_DY_SIMPLE       -20        // NO SD / NO MAP / OFF MAP
#define MSG_DY_SEARCH       -30        // NO GPS / NO DATA (leaves room for the satellite info)
#define TXT_UNKNOWN         "--"
#define TXT_NO_SD           "NO SD"
#define TXT_NO_MAP          "NO MAP"
#define TXT_NO_GPS          "NO GPS"
#define TXT_NO_DATA         "NO DATA"
#define TXT_OFF_MAP         "OFF MAP"
#define TXT_CHECK1          "CHECK"
#define TXT_CHECK2          "WIRES"
#define CHECK_SIZE          2
#define CHECK1_DY           6
#define CHECK2_DY           28

// ---- satellites (shown under NO GPS) ----
#define TXT_SAT_PREFIX      "SAT"
#define SAT_TEXT_SIZE       2
#define SAT_TEXT_DY         6
#define BAR_X0              -22        // left edge of the first bar, from the sign centre
#define BAR_STEP            12
#define BAR_W               8
#define BAR_BASE_DY         56         // bottom of the bars
#define BAR_H0              4
#define BAR_H_STEP          4
