// =============================================================================
//  PushDevice.h  -  Thin driver for talking TO an Ableton Push 1
// -----------------------------------------------------------------------------
//  Wraps a USBHost_t36 MIDIDevice and provides high-level output:
//    * mode / aftertouch / brightness setup
//    * pad LEDs (palette or exact RGB)
//    * button LEDs
//    * a 4-line text display with a dirty-line cache (only changed lines are sent)
//
//  Reading from the Push (pad/encoder/button events) is handled in the .ino via
//  the USBHost_t36 handler callbacks, because that library uses C function
//  pointers rather than C++ member functions.
// =============================================================================
#pragma once
#include <Arduino.h>
#include <USBHost_t36.h>
#include "Push1Defs.h"

class PushDevice {
public:
  PushDevice(MIDIDevice& midi, uint8_t cable = 0) : _midi(midi), _cable(cable) {}

  void begin();                 // user mode + poly AT + clear display + clear pads

  // Device configuration
  void setUserMode(bool user);
  void setPolyAftertouch(bool poly);
  void setBrightness(uint8_t v);   // 0..63 effective
  void setContrast(uint8_t v);     // 0..63 effective

  // Pads
  void setPad(uint8_t x, uint8_t y, uint8_t color);            // color = palette index
  void setPadByNote(uint8_t note, uint8_t color);
  void setPadRGB(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);
  void clearPads();

  // Buttons (CC LEDs)
  void setButton(uint8_t cc, uint8_t value);

  // Text display (4 lines x 68 cols). Writes go to a buffer; flushDisplay() sends.
  void clearText();                                   // buffer -> all spaces
  void setText(uint8_t line, uint8_t col, const char* s);
  void setLineCentered(uint8_t line, const char* s);
  void flushDisplay();                                // send only changed lines
  void clearDisplayHW();                              // blank the device now

  uint8_t cable() const { return _cable; }

private:
  void sendSysEx(const uint8_t* data, uint16_t len);  // data INCLUDES F0..F7
  void writeLineHW(uint8_t line);

  MIDIDevice& _midi;
  uint8_t     _cable;
  char        _buf[PUSH_DISPLAY_LINES][PUSH_DISPLAY_COLS];
  char        _shadow[PUSH_DISPLAY_LINES][PUSH_DISPLAY_COLS];
  bool        _dirty[PUSH_DISPLAY_LINES];
};
