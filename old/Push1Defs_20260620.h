// =============================================================================
//  Push1Defs.h  -  Ableton Push 1 protocol constants & helpers
// -----------------------------------------------------------------------------
//  Sources:
//   * pushmod.blogspot.com  (reverse-engineering of the Push 1)
//   * Ableton "push-interface" docs (Push 2; pads/encoders behave similarly)
//
//  Push 1 SysEx header:  F0 47 7F 15 ...
//    0x47 = Akai manufacturer ID, 0x7F = device id (all), 0x15 = product id (Push)
//
//  NOTE: The CC numbers below that are NOT in the verified ranges are the
//  "standard" Push 1 user-mode map. If a control doesn't behave as expected,
//  enable PUSH_DEBUG in the .ino and watch the Serial Monitor while you press
//  it - you'll see the exact note/CC the unit sends.
// =============================================================================
#pragma once
#include <Arduino.h>

// ---- SysEx "function" bytes ----------------------------------------------
enum PushSysEx : uint8_t {
  PUSH_SX_SET_PAD_RGB     = 4,     // F0 47 7F 15 04 00 08 <pad0-71> 00 r1 r2 g1 g2 b1 b2 F7
  PUSH_SX_WRITE_LINE_BASE = 24,    // 24..27  -> display lines 0..3
  PUSH_SX_CLEAR_LINE_BASE = 28,    // 28..31  -> clear display lines 0..3
  PUSH_SX_AFTERTOUCH      = 92,    // 0x5C  payload: 0 = poly, 1 = channel
  PUSH_SX_SET_MODE        = 98,    // 0x62  payload: 0 = Live, 1 = User
  PUSH_SX_TOUCHSTRIP_MODE = 99,
  PUSH_SX_CONTRAST        = 122,   // payload 0..63 effective
  PUSH_SX_BRIGHTNESS      = 124,   // payload 0..63 effective
};

// ---- Display geometry -----------------------------------------------------
static const uint8_t PUSH_DISPLAY_LINES = 4;
static const uint8_t PUSH_DISPLAY_COLS  = 68;

// ---- Pad grid -------------------------------------------------------------
// 8x8 pads. note = y*8 + 36 + x, with (0,0) = bottom-left pad. Notes 36..99.
static const uint8_t PUSH_PAD_BASE_NOTE = 36;
static inline uint8_t pushPadNote(uint8_t x, uint8_t y) { return (uint8_t)(y * 8 + PUSH_PAD_BASE_NOTE + x); }
static inline bool    pushIsPadNote(uint8_t n)          { return n >= 36 && n <= 99; }
static inline uint8_t pushPadX(uint8_t n)               { return (uint8_t)((n - PUSH_PAD_BASE_NOTE) % 8); }
static inline uint8_t pushPadY(uint8_t n)               { return (uint8_t)((n - PUSH_PAD_BASE_NOTE) / 8); }

// ---- Pad LED palette (sent as Note-On velocity) ---------------------------
// CONFIRMED: 0 = off, 3 = white. The rest are commonly-used palette indices;
// tune them against the Push 1 color table, or use PushDevice::setPadRGB() for
// exact 24-bit colors.
enum PushColor : uint8_t {
  CLR_OFF     = 0,
  CLR_DIMGREY = 1,
  CLR_WHITE   = 3,
  CLR_RED     = 5,
  CLR_AMBER   = 9,
  CLR_YELLOW  = 13,
  CLR_LIME    = 17,
  CLR_GREEN   = 21,
  CLR_SPRING  = 25,
  CLR_TURQUOISE = 29,
  CLR_CYAN    = 33,
  CLR_SKY  = 37,
  CLR_OCEAN  = 41,
  CLR_BLUE    = 45,
  CLR_ORCHID  = 49,
  CLR_MAGENTA = 53,
  CLR_PINK  = 57,
};

// ---- Monochrome button LED states (sent as CC value) ----------------------
enum PushBtnLed : uint8_t {
  BTN_OFF = 0, BTN_DIM = 1, BTN_DIM_BLINK = 2, BTN_DIM_BLINK_FAST = 3,
  BTN_LIT = 4, BTN_LIT_BLINK = 5, BTN_LIT_BLINK_FAST = 6,
};

// ---- Control-Change numbers (User mode) -----------------------------------
enum PushCC : uint8_t {
  // Encoders - send RELATIVE values; touching them sends Note-On/Off.
  CC_ENCODER_TEMPO = 14,
  CC_ENCODER_SWING = 15,
  CC_ENCODER_1     = 71,   // 71..78 = the 8 display encoders (left..right)
  CC_ENCODER_2     = 72,
  CC_ENCODER_3     = 73,
  CC_ENCODER_4     = 74,
  CC_ENCODER_5     = 75,
  CC_ENCODER_6     = 76,
  CC_ENCODER_7     = 77,
  CC_ENCODER_8     = 78,
  CC_ENCODER_MASTER = 79,

  // Verified button GROUPS (from pushmod):
  CC_UPPER_ROW_1   = 20,   // 20..27  row just under the display (bi-color)
  CC_LOWER_ROW_1   = 102,  // 102..109 second row under the display (RGB)
  CC_RIGHT_COL_TOP = 36,   // 36..43  column right of the pads (top..bottom)

  // Transport / common (standard user map - verify with PUSH_DEBUG):
  CC_PLAY      = 85,
  CC_RECORD    = 86,
  CC_NEW       = 87,
  CC_DUPLICATE = 88,
  CC_AUTOMATION= 89,
  CC_FIXEDLEN  = 90,
  CC_METRONOME = 9,
  CC_TAP       = 3,
  CC_UNDO      = 119,
  CC_DELETE    = 118,
  CC_QUANTIZE  = 116,
  CC_NOTE      = 50,
  CC_SESSION   = 51,
  CC_OCTAVE_UP = 55,
  CC_OCTAVE_DN = 54,
  CC_SHIFT     = 49,
  CC_SELECT    = 48,
  CC_LEFT      = 44,
  CC_RIGHT     = 45,
  CC_UP        = 46,
  CC_DOWN      = 47,
};

// Encoders are relative: 1..63 = clockwise (+), 65..127 = counter-clockwise (-).
static inline int8_t pushEncoderDelta(uint8_t value) {
  return (value < 64) ? (int8_t)value : (int8_t)((int)value - 128);
}

// True for CCs that carry a relative encoder delta (not a button press).
static inline bool pushIsEncoderCC(uint8_t cc) {
  return (cc >= CC_ENCODER_1 && cc <= 78) ||
         cc == CC_ENCODER_TEMPO || cc == CC_ENCODER_SWING || cc == CC_ENCODER_MASTER;
}
