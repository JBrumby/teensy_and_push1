// =============================================================================
//  PushDevice.cpp
// =============================================================================
#include "PushDevice.h"
#include <string.h>

// ---------------------------------------------------------------------------
void PushDevice::begin() {
  for (uint8_t l = 0; l < PUSH_DISPLAY_LINES; l++) memset(_shadow[l], 0, PUSH_DISPLAY_COLS);
  setUserMode(true);          // standalone: Push sends raw MIDI, we own the LEDs
  setPolyAftertouch(true);    // poly pressure per pad
  setBrightness(63);
  clearDisplayHW();
  clearText();
  clearPads();
}

// ---- SysEx plumbing -------------------------------------------------------
void PushDevice::sendSysEx(const uint8_t* data, uint16_t len) {
  // hasTerm = true: our buffers already contain the F0 ... F7 framing.
  _midi.sendSysEx(len, data, true, _cable);
}

void PushDevice::setUserMode(bool user) {
  uint8_t m[] = { 0xF0,0x47,0x7F,0x15, PUSH_SX_SET_MODE, 0x00,0x01, (uint8_t)(user ? 1 : 0), 0xF7 };
  sendSysEx(m, sizeof(m));
}

void PushDevice::setPolyAftertouch(bool poly) {
  // payload 0 = key/poly aftertouch, 1 = channel aftertouch
  uint8_t m[] = { 0xF0,0x47,0x7F,0x15, PUSH_SX_AFTERTOUCH, 0x00,0x01, (uint8_t)(poly ? 0 : 1), 0xF7 };
  sendSysEx(m, sizeof(m));
}

void PushDevice::setBrightness(uint8_t v) {
  uint8_t m[] = { 0xF0,0x47,0x7F,0x15, PUSH_SX_BRIGHTNESS, 0x00,0x01, (uint8_t)(v & 0x7F), 0xF7 };
  sendSysEx(m, sizeof(m));
}

void PushDevice::setContrast(uint8_t v) {
  uint8_t m[] = { 0xF0,0x47,0x7F,0x15, PUSH_SX_CONTRAST, 0x00,0x01, (uint8_t)(v & 0x7F), 0xF7 };
  sendSysEx(m, sizeof(m));
}

// ---- Pads -----------------------------------------------------------------
void PushDevice::setPad(uint8_t x, uint8_t y, uint8_t color) {
  if (x > 7 || y > 7) return;
  _midi.sendNoteOn(pushPadNote(x, y), color, 1, _cable);  // velocity 0 = off
}

void PushDevice::setPadByNote(uint8_t note, uint8_t color) {
  if (!pushIsPadNote(note)) return;
  _midi.sendNoteOn(note, color, 1, _cable);
}

void PushDevice::setPadRGB(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
  if (x > 7 || y > 7) return;
  uint8_t pad = (uint8_t)(y * 8 + x);  // 0..63 for the 8x8 grid
  uint8_t m[] = { 0xF0,0x47,0x7F,0x15, PUSH_SX_SET_PAD_RGB, 0x00, 0x08, pad, 0x00,
                  (uint8_t)(r >> 4), (uint8_t)(r & 0x0F),
                  (uint8_t)(g >> 4), (uint8_t)(g & 0x0F),
                  (uint8_t)(b >> 4), (uint8_t)(b & 0x0F), 0xF7 };
  sendSysEx(m, sizeof(m));
}

void PushDevice::clearPads() {
  for (uint8_t y = 0; y < 8; y++)
    for (uint8_t x = 0; x < 8; x++)
      setPad(x, y, CLR_OFF);
}

// ---- Buttons --------------------------------------------------------------
void PushDevice::setButton(uint8_t cc, uint8_t value) {
  _midi.sendControlChange(cc, value, 1, _cable);
}

// ---- Display --------------------------------------------------------------
void PushDevice::clearText() {
  for (uint8_t l = 0; l < PUSH_DISPLAY_LINES; l++) {
    for (uint8_t c = 0; c < PUSH_DISPLAY_COLS; c++) _buf[l][c] = ' ';
    _dirty[l] = true;
  }
}

void PushDevice::setText(uint8_t line, uint8_t col, const char* s) {
  if (line >= PUSH_DISPLAY_LINES) return;
  for (uint8_t i = 0; s[i] && (col + i) < PUSH_DISPLAY_COLS; i++)
    _buf[line][col + i] = s[i];
  _dirty[line] = true;
}

void PushDevice::setLineCentered(uint8_t line, const char* s) {
  uint8_t len = (uint8_t)strlen(s);
  if (len > PUSH_DISPLAY_COLS) len = PUSH_DISPLAY_COLS;
  setText(line, (uint8_t)((PUSH_DISPLAY_COLS - len) / 2), s);
}

void PushDevice::flushDisplay() {
  for (uint8_t l = 0; l < PUSH_DISPLAY_LINES; l++) {
    if (!_dirty[l]) continue;
    if (memcmp(_buf[l], _shadow[l], PUSH_DISPLAY_COLS) != 0) {
      writeLineHW(l);
      memcpy(_shadow[l], _buf[l], PUSH_DISPLAY_COLS);
    }
    _dirty[l] = false;
  }
}

void PushDevice::clearDisplayHW() {
  for (uint8_t l = 0; l < PUSH_DISPLAY_LINES; l++) {
    uint8_t m[] = { 0xF0,0x47,0x7F,0x15, (uint8_t)(PUSH_SX_CLEAR_LINE_BASE + l), 0x00, 0x00, 0xF7 };
    sendSysEx(m, sizeof(m));
  }
}

void PushDevice::writeLineHW(uint8_t line) {
  // F0 47 7F 15 (24+line) 00 <len> <offset=0> <68 chars> F7
  // <len> = Nchars + 1  (pushmod spec; the worked example writes the 12-char
  // "Hello World!" with len byte 13). The +1 accounts for the offset byte.
  // If text looks shifted/garbled on your unit, this byte is the thing to nudge.
  uint8_t m[8 + PUSH_DISPLAY_COLS + 1];
  uint16_t i = 0;
  m[i++] = 0xF0; m[i++] = 0x47; m[i++] = 0x7F; m[i++] = 0x15;
  m[i++] = (uint8_t)(PUSH_SX_WRITE_LINE_BASE + line);
  m[i++] = 0x00;
  m[i++] = (uint8_t)(PUSH_DISPLAY_COLS + 1);   // length = Nchars + 1
  m[i++] = 0x00;                               // offset (column)
  for (uint8_t c = 0; c < PUSH_DISPLAY_COLS; c++) {
    char ch = _buf[line][c];
    m[i++] = (ch >= 32 && ch < 127) ? (uint8_t)ch : ' ';
  }
  m[i++] = 0xF7;
  sendSysEx(m, i);
}
