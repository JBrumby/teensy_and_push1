// ======================================================
//
// hardpush converted to library
//
// ======================================================
// Code from github: carlborg/hardpush
// converted to library by github: JBrumby

#pragma once
#include <Arduino.h>
#include <SD.h>
#include <USBHost_t36.h>
#include "PushDevice.h"

// Set to 1 to print every incoming Push message to the Serial Monitor.
#ifndef PUSH_DEBUG // anywhere else defined and set?
  #define PUSH_DEBUG 1
#endif

#if PUSH_DEBUG
  inline volatile uint32_t noteOnCount = 0;
  inline volatile uint32_t noteOffCount = 0;
#endif


// If pad/button LEDs don't light, the Push exposes its data on the other
// virtual cable - change this 0 <-> 1. (Input works on either; only LED/display
// OUTPUT needs the right cable.)
#ifndef PUSH_CABLE
inline const uint8_t PUSH_CABLE = 1;
#endif

inline constexpr int chipSelect = BUILTIN_SDCARD; // on-board SD
inline const char* const filename = "push1.bin"; // 

///////////////////////////////////////////////
//
// PAGE VARIABLES
//
////////////////////////////////////////////////
//Each page has a midi-channel, color scheme, octave and layout
// ===== Größen-Konstanten (vor der Klasse) =====
inline const byte numberOfPages        = 4;
inline const byte numberOfScalePresets = 4;


#if !defined(USB_MIDI_SERIAL) && !defined(USB_MIDI) && !defined(USB_MIDI4) && \
    !defined(USB_MIDI16) && !defined(USB_MIDI16_SERIAL) && !defined(USB_MIDI4_SERIAL)
#warning "Set Tools > USB Type to a MIDI option (e.g. 'MIDI' or 'Serial + MIDI')."
#endif

class Hardpush {
  public:
  
  Hardpush () {}

  void begin(USBHost& usb, MIDIDevice& midi, bool& ready);                 // user mode + poly AT + clear display + clear pads
  void loop();

  void setPushDevice(PushDevice& pd) { pPush = &pd; };
  void loadSettings(File& file);

  // Device configuration
  // void setUserMode(bool user);

  // ===== Callback-Logik (NICHT static — braucht Zugriff auf Member) =====
  // Push1 callbacks
  void handleControlChangeUSB(byte channel, byte control, byte value);
  void handleNoteOnUSB(byte channel, byte pitch, byte velocity);
  void handleNoteOffUSB(byte channel, byte pitch, byte velocity);
  void handleAfterTouchPolyUSB(byte channel, byte note, byte pressure);
  void handleAfterTouchChannelUSB(byte channel, byte pressure);
  void handlePitchBendUSB(byte channel, int bend);
  // DIN midi / Computer usb callbacks
  void handleNoteOnDIN(byte channel, byte pitch, byte velocity);
  void handleNoteOffDIN(byte channel, byte pitch, byte velocity);
  // end of callback funcs

  // Math and note functions
  int loopingNumbers(int number, int lastNumber);
  int indexInLoopingPattern(int anyIndex, int patternSize);
  int loopingPatternOffset(int index, int patternSize);
  int getRelativeScaleNote(int index);
  byte pushButtonIndexToScaleIndex(byte index);
  byte pushButtonIndexToAbsoluteNote(byte index);
  bool legalRange(byte value);

  void killAllPressedNotesDIN(byte channel);


  // Helpers
  // Getter - Helper
  byte scale()    const { return scalePresetScales[scalePresetIndex]; }
  byte tonic()    const { return scalePresetTonics[scalePresetIndex]; }
  byte channel()  const { return pageChannels[instrumentPageIndex]; }
  byte colorScheme() const { return pageColors[instrumentPageIndex]; }
  byte octave()   const { return pageOctaves[instrumentPageIndex]; }
  byte color()    const { return pageColors[instrumentPageIndex]; }
  byte layout()   const { return pageLayouts[instrumentPageIndex]; }
  byte preset()   const { return scalePresetIndex; }
  byte page()     const { return instrumentPageIndex; }
// Setter - Helper
  void setScale(byte v)   { scalePresetScales[scalePresetIndex] = v;     sd_is_dirty = true; }
  void setTonic(byte v)   { scalePresetTonics[scalePresetIndex] = v;     sd_is_dirty = true; }
  void setChannel(byte v) { pageChannels[instrumentPageIndex] = v;       sd_is_dirty = true; }
  void setColor(byte v)   { pageColors[instrumentPageIndex] = v;         sd_is_dirty = true; }
  void setLayout(byte v)  { pageLayouts[instrumentPageIndex] = v;        sd_is_dirty = true; }
  void setOctave(byte v)  { pageOctaves[instrumentPageIndex] = v;        sd_is_dirty = true; }
  void setPreset(byte v)  { scalePresetIndex = v;                        sd_is_dirty = true; }
  void setPage(byte v)    { instrumentPageIndex = v;                     sd_is_dirty = true; }

  void saveIfDirty();   // schreibt config_file, wenn sd_is_dirty und Karte frei

  // Display stuff
  void displayButton(byte index, bool triggered);
  void initPageSelectButtons();
  void initScalePresetSelectButtons();

  void initPushSettings();
  void initGrid(); // grid of pads
  void initOtherButtons();
  void initDisplay();

  void addToDisplay(String text, byte row, byte section, byte position);
  void addDisplayTop();
  void addToDisplay8Section(String text, byte row, byte section, bool center);
  void addKnobLabelAndData(byte knob, String label, String value);
  void addButtonLabelAndData(byte button, String label, String value);
  void removeDisplaySection(byte row, byte section);
  void removeDisplayRow(byte row);
  void showDisplayRow(byte row);

  // Wrapper for din-midi or computer midi
  void MIDImessage(byte command, byte MIDInote, byte MIDIvelocity);
  void mySendNoteOn(byte note, byte velocity);
  void mySendNoteOff(byte note);
  void mySendControlChange(byte number, byte value);

  // which device ???
  void mySendAfterTouch(byte value);
  void killAllNotesUSB();

  
  private:
    File config_file;
    USBHost*     pMyusb     = nullptr;
    MIDIDevice*  pPushMidi  = nullptr;
    bool*        pPushReady = nullptr;
    bool         sd_ready   = false;

    PushDevice* pPush = nullptr;

  // ===== Static Wrapper — nur für die Registrierung bei usbMIDI/pPushMidi-> =====
  static void onControlChangeUSB(byte ch, byte c, byte v);
  static void onNoteOnUSB(byte ch, byte p, byte v);
  static void onNoteOffUSB(byte ch, byte p, byte v);
  static void onAfterTouchPolyUSB(byte ch, byte n, byte pr);
  static void onAfterTouchChannelUSB(byte ch, byte pr);
  static void onPitchBendUSB(byte ch, int bend);
  static void onNoteOnDIN(byte ch, byte p, byte v);
  static void onNoteOffDIN(byte ch, byte p, byte v);

  static Hardpush* instance;   // zeigt auf die aktive Instanz

  


    bool sd_is_dirty = false;

    // initialize presets, for case, sd fails.
    // ACTIVE SCALE PRESET
    byte scalePresetIndex = 0; //what scale preset is currently selected for use

    //ACTIVE INSTRUMENT PAGE
    byte instrumentPageIndex = 0; //what instrument page is currently selected for use

    // listen to external midi feedback or internal button presses
    bool internalFeedback = true;

    byte pageChannels[numberOfPages]        = {1,2,3,4};
    byte pageColors[numberOfPages]          = {0,0,0,0};
    byte pageOctaves[numberOfPages]         = {3,3,3,3};
    byte pageLayouts[numberOfPages]         = {0,0,0,0};
    byte scalePresetTonics[numberOfScalePresets] = {0,0,0,0};
    byte scalePresetScales[numberOfScalePresets] = {1,1,1,1};

       bool pressedNotes[128];
//     byte displayBuffer[4][68];
//     byte sysexDisplayBuffer[77] = {
//   240, 71, 127, 21, 24, 0, 69, 0,
//   32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
//   32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
//   32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
//   32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
//   247
// };
    

    
    void eepromSet(byte adress, byte value, bool set_dirty = true);
    byte eepromGet(byte adress, byte minValue = 0, byte maxValue = 255, byte defaultValue = 0);

    void setLayout(byte layoutIndex, byte pageIndex);
    byte getXlayout(byte pageIndex);
    byte getYlayout(byte pageIndex);


};



// Special Dispay Characters
#define UP_ARROW                            0
#define DOWN_ARROW                          1
#define THREE_STACKED_HORIZONTAL_LINES      2
#define VERTICAL_LINE_AND_HORIZONTAL_LINE   3
#define HORIZONTAL_LINE_AND_VERTICAL_LINE   4
#define TWO_VERTICAL_LINES                  5
#define TWO_SIDE_BY_SIDE_HORIZONTAL_LINES   6
#define FOLDER_SYMBOL                       7
#define SPLIT_VERTICAL_LINES                8
#define FLAT_SYMBOLS                        27
#define THREE_SIDE_BY_SIDE_DOTS             28
#define FULL_BLOCK                          29
#define RIGHT_ARROW                         30
#define LEFT_ARROW                          31

/////////////////////////////////////////////////
//
// SCALES----------------
//
/////////////////////////////////////////////////

//if you want to add more scales please update all these three constants
inline const int numberOfScales = 26;
inline const byte scales[][numberOfScales] = { //define scales on the form 'semitones added to tonic'
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, // 0 'Chromatic'
  {0, 2, 4, 5, 7, 9, 11}, // 1 ‘Major’
  {0, 2, 3, 5, 7, 8, 10}, // 2 ‘Minor’
  {0, 2, 3, 5, 7, 9, 10}, // 3 ‘Dorian’
  {0, 2, 4, 5, 7, 9, 10}, // 4 ‘Mixolydian’
  {0, 2, 4, 6, 7, 9, 11}, // 5 ‘Lydian’
  {0, 1, 3, 5, 7, 8, 10}, // 6 ‘Phrygian’
  {0, 1, 3, 4, 7, 8, 10}, // 7 ‘Locrian’
  {0, 1, 3, 4, 6, 7, 9, 10}, // 8 ‘Diminished’
  {0, 2, 3, 5, 6, 8, 9, 11}, // 9 ‘Whole-half’
  {0, 2, 4, 6, 8, 10}, // 10 ‘Whole Tone’
  {0, 3, 5, 6, 7, 10}, // 11 ‘Minor Blues’
  {0, 3, 5, 7, 10}, // 12 ‘Minor Pentatonic’
  {0, 2, 4, 7, 9}, // 13 ‘Major Pentatonic’
  {0, 2, 3, 5, 7, 8, 11}, // 14 ‘Harmonic Minor’
  {0, 2, 3, 5, 7, 9, 11}, // 15 ‘Melodic Minor’
  {0, 1, 3, 4, 6, 8, 10}, // 16 ‘Super Locrian’
  {0, 1, 4, 5, 7, 8, 11}, // 17 ‘Bhairav’
  {0, 2, 3, 6, 7, 8, 11}, // 18 ‘Hungarian Minor’
  {0, 1, 4, 5, 7, 8, 10}, // 19 ‘Minor Gypsy’
  {0, 2, 3, 7, 8}, // 20 ‘Hirojoshi’
  {0, 1, 5, 7, 10}, // 21 ‘In-Sen’
  {0, 1, 5, 6, 10}, // 22 ‘Iwato’
  {0, 2, 3, 7, 9}, // 23 ‘Kumoi’
  {0, 1, 3, 4, 7, 8}, // 24 ‘Pelog’
  {0, 1, 3, 4, 5, 6, 8, 10} // 25 ‘Spanish’
};
inline const char* nameOfScales[numberOfScales] = {
  "Chromatic",
  "Major",
  "Minor",
  "Dorian",
  "Mixolydian",
  "Lydian",
  "Phrygian",
  "Locrian",
  "Diminished",
  "Whole-half",
  "Whole Tone",
  "Minor Blues",
  "Minor Pentatonic",
  "Major Pentatonic",
  "Harmonic Minor",
  "Melodic Minor",
  "Super Locrian",
  "Bhairav",
  "Hungarian Minor",
  "Minor Gypsy",
  "Hirojoshi",
  "In-Sen",
  "Iwato",
  "Kumoi",
  "Pelog",
  "Spanish"
};
inline const char* nameOfKeys[12] = {
  "C",
  "C#",
  "D",
  "D#",
  "E",
  "F",
  "F#",
  "G",
  "G#",
  "A",
  "A#",
  "B",
};
inline const byte scaleSizes[numberOfScales] = {12, 7, 7, 7, 7, 7, 7, 7, 8, 8, 6, 6, 5, 5, 7, 7, 7, 7, 7, 7, 5, 5, 5, 5, 6, 8}; //havn't found a way to do this prettier yet



//////////////////////////////////////////////////
//
// COLOR SCHEMES
// 
//////////////////////////////////////////////////

inline const byte numberOfColorSchemes=8;
//with passive color, rootnote color and triggered color
inline byte colors[numberOfColorSchemes][3] = { 
  {51,80,48},
  {31,9,16},
  {55,38,30},
  {43,44,77},
  {47,69,36},
  {7,42,5},
  {15,18,21},
  {23,50,9},
}; 
inline const char* nameOfColors[numberOfColorSchemes] = {
  "Viol",
  "Turtle",
  "Fungus",
  "Avatar",
  "Passific",
  "Hero",
  "Slime",
  "TMNT",
};

/////////////////////////////////////////////////
//
// LAYOUTS
//
////////////////////////////////////////////////
// LAYOUTS
inline const byte numberOfLayouts=6;
inline byte layouts[numberOfLayouts][2]={
  {1,3}, // forths up
  {3,1}, // forths across
  {1,2}, // thirds up
  {2,1}, // thirds across
  {1,8}, // seq up
  {8,1}, // seq across
};
inline const char* nameOfLayouts[numberOfLayouts] = {
  "4up",
  "4across",
  "3up",
  "3across",
  "Seq-horizontal",
  "Seq-vertical",
};


