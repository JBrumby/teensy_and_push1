// =============================================================================
//  PushController.ino
// -----------------------------------------------------------------------------
//  Teensy 3.6 + Ableton Push 1  ->  standalone MIDI controller + step sequencer
//
//  Signal path:
//    iPad  <--USB(device)--  Teensy  --USB(host)-->  Push 1
//    MIDI TRS  <--Pins 10,GND,3.3V--  Teensy  --USB(host)-->  Push 1
//  Type B (Novation, Arturia):
//  T - source/MIDI 4 - 47 ohm - 3.3V
//  R - sink/MIDI 5   - 47 ohm - pin 10
//  S - shield/MIDI 2 - GND
//
//  REQUIRED IDE SETTINGS (Arduino IDE + Teensyduino):
//    Tools > Board    : Teensy 3.6
//    Tools > USB Type : "MIDI"  (or "Serial + MIDI" to keep the Serial Monitor)
//  REQUIRED LIBRARY : USBHost_t36 (bundled with Teensyduino)
//
//  Three modes (Note button toggles PLAY<->DRUM; Session selects SEQUENCER):
//    PLAY  - isomorphic melodic layout (+1 semitone per column, +PLAY_ROW_INTERVAL
//            per row). All pads that play a given note light green together.
//            Arrows transpose; pads out of MIDI range stay dark.
//    DRUM  - all 64 pads, one per-pad {note, channel}. The grid is 4 colored
//            quadrants; each quadrant defaults to notes 36..51 on channel 10.
//            Tap a pad to select it, then dial its channel/note with Tempo/Swing.
//    SEQ   - 8x8 grid is the pattern: rows = tracks, columns = steps.
//    KEYPAD - Hardpush scale Play
//
//  Shared across all modes:
//    The 8 display encoders (CC71..78) are assignable CC controllers. 32 values
//    are kept (4 groups x 8). Step groups with the broken arrows (CC62/63).
//
//  Controls:
//    Play (85)     start/stop transport        Record (86)   record flag (stub)
//    Note (50)     toggle PLAY <-> DRUM         Session (51)  SEQUENCER
//    Broken arrows (62/63)  previous / next CC-encoder group      (all modes)
//    Encoders 71-78         send assigned CC                      (all modes)
//    Tempo encoder (14)     SEQ: BPM   PLAY: note channel   DRUM: sel-pad channel
//    Swing encoder (15)     PLAY: root note      DRUM: sel-pad note number
//    Arrows L/R/U/D (44-47) PLAY: transpose +1 / -1 / +row / -row
//    Octave Up/Down (55/54) PLAY: transpose by +/- 12
//    Right column (36-43)   SEQ: select a track   Top row (20-27) SEQ: mute track
// =============================================================================

#include <USBHost_t36.h>
#include "Push1Defs.h"
#include "PushDevice.h"
#include "Sequencer.h"
#include <SD.h>
#include "Hardpush.h"

#if !defined(USB_MIDI_SERIAL) && !defined(USB_MIDI) && !defined(USB_MIDI4) && \
    !defined(USB_MIDI16) && !defined(USB_MIDI16_SERIAL) && !defined(USB_MIDI4_SERIAL)
#warning "Set Tools > USB Type to a MIDI option (e.g. 'MIDI' or 'Serial + MIDI')."
#endif

// Set to 1 to print every incoming Push message to the Serial Monitor.
#define PUSH_DEBUG 1

// ---------------------------------------------------------------------------
//  USB host + devices
// ---------------------------------------------------------------------------
USBHost     myusb;
USBHub      hub1(myusb);
MIDIDevice  pushMidi(myusb);

// If pad/button LEDs don't light, the Push exposes its data on the other
// virtual cable - change this 0 <-> 1. (Input works on either; only LED/display
// OUTPUT needs the right cable.)
#ifndef PUSH_CABLE
  //const uint8_t 
  #define PUSH_CABLE 1
#endif

PushDevice  push(pushMidi, PUSH_CABLE);
Sequencer   seq;
Hardpush hp;
File config_file;


// Forward declarations
// HARDPUSH-mode render/light helpers
uint8_t hardpushRestColor(byte padIdx);
void    renderHardpushPads();
void    lightHardpushNote(byte outNote, bool pressed);

// ---------------------------------------------------------------------------
//  App state
// ---------------------------------------------------------------------------
enum Mode { MODE_PLAY, MODE_DRUM, MODE_HARDPUSH, MODE_SEQ };
Mode    mode          = MODE_SEQ;
Mode    lastNoteMode  = MODE_PLAY;   // pad mode the Note button returns to from SEQ
bool    running       = false;
bool    recording     = false;
float   bpm           = 120.0f;
uint8_t selectedTrack = 0;

// Transport (24 PPQN internal clock)
const uint8_t CLOCKS_PER_STEP = 12;   // 12 = eighth notes; 6 = sixteenth notes
uint32_t usPerClock      = 0;
uint32_t lastClockMicros = 0;
uint8_t  clockCount      = 0;
uint8_t  curStep         = 0;
int      playCol         = -1;

struct ActiveNote { uint8_t note, channel; };
ActiveNote active[SEQ_TRACKS];
uint8_t    activeCount = 0;

// ---- PLAY (melodic) mode --------------------------------------------------
// Isomorphic layout: note = playBase + row*PLAY_ROW_INTERVAL + column.
const int8_t PLAY_ROW_INTERVAL = 4;   // semitones per row up (4 = major-3rd rows)
uint8_t playBase    = 36;             // MIDI note of the bottom-left pad (transposes)
uint8_t rootPitch   = 0;              // 0..11 pitch class highlighted (0 = C)
uint8_t noteChannel = 1;              // 1..16

// ---- DRUM mode ------------------------------------------------------------
// All 64 pads, each with its own note+channel. Grid = 4 colored 4x4 quadrants.
struct DrumPad { uint8_t note; uint8_t channel; };
DrumPad drumPads[64];
uint8_t selectedDrumPad = 0;          // pad currently being edited (0..63)

// ---- Assignable CC encoders (shared by every mode) ------------------------
// The 8 display encoders drive these. 32 total = CC_GROUPS x 8; step the active
// group with the broken arrows. Edit ccCtl[] defaults below to set each
// encoder's CC number and MIDI channel.
const uint8_t CC_GROUPS = 4;
const uint8_t CC_TOTAL  = CC_GROUPS * 8;   // 32
struct CCCtl { uint8_t ccNum; uint8_t channel; uint8_t value; };
CCCtl   ccCtl[CC_TOTAL];
uint8_t ccGroup = 0;                   // active group 0..CC_GROUPS-1

bool pushReady = false;

static const char* const NOTE_NAMES[12] =
  { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

// ---------------------------------------------------------------------------
//  Forward declarations
// ---------------------------------------------------------------------------
void onPushNoteOn(uint8_t ch, uint8_t note, uint8_t vel);
void onPushNoteOff(uint8_t ch, uint8_t note, uint8_t vel);
void onPushControlChange(uint8_t ch, uint8_t cc, uint8_t val);
void onPushPolyAT(uint8_t ch, uint8_t note, uint8_t val);
void onPushChannelAT(uint8_t ch, uint8_t pressure);

void recalcTempo();
void startTransport();
void stopTransport();
void onClockTick();
void fireStep();
void stopActiveNotes();

void initDrumPads();
void initCCCtl();
void handleCCEncoder(uint8_t e, int8_t d);
void transposePlay(int semis);
void selectDrumPad(uint8_t idx);

void renderAll();
void renderModeButtons();
void renderRightColumn();
void renderTopRow();
void renderSeqPads();
void renderSeqColumn(uint8_t col);
void renderNotePads();
void renderDrumPads();
void lightPlayNote(int outNote, bool pressed);
void renderDisplay();

uint8_t     trackColor(uint8_t t);
const char* trackName(uint8_t t);
int         playNoteRaw(uint8_t x, uint8_t y);
bool        playValid(int n);
uint8_t     drumQuadColor(uint8_t idx);
uint8_t     drumPadColor(uint8_t idx);

// ===========================================================================
//  setup / loop
// ===========================================================================
void setup() {
  Serial.begin(115200);
  myusb.begin();

  pushMidi.setHandleNoteOn(onPushNoteOn);
  pushMidi.setHandleNoteOff(onPushNoteOff);
  pushMidi.setHandleControlChange(onPushControlChange);
  pushMidi.setHandleAfterTouchPoly(onPushPolyAT);
  pushMidi.setHandleAfterTouch(onPushChannelAT);

  seq.begin();
  initDrumPads();
  initCCCtl();
  recalcTempo();

  // new Hardpush
  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println(F("SD-Card init failed"));
  } else {
    config_file = SD.open("push1.bin", FILE_WRITE_BEGIN);
    if (!config_file) {
      Serial.println(F("SD file open failed"));
    } else {
      hp.setPushDevice(push);
      hp.loadSettings(config_file);
      Serial.printf("Hardpush presets loaded");
    }
  }

  // hp.setPushDevice(push);
  // hp.loadSettings(config_file);
  // Serial.println(F("Hardpush presets loaded"));
}

void loop() {
  myusb.Task();
  while (pushMidi.read()) { }   // dispatch all queued Push events to the callbacks
  while (usbMIDI.read())  { }   // drain the iPad side

  bool connected = (pushMidi.idVendor() != 0);
  if (connected && !pushReady) {
    push.begin();
    renderAll();
    pushReady = true;
    if (PUSH_DEBUG) Serial.println(F("Push connected."));
  } else if (!connected && pushReady) {
    pushReady = false;
    if (PUSH_DEBUG) Serial.println(F("Push disconnected."));
  }

  if (running) {
    uint32_t now = micros();
    while ((uint32_t)(now - lastClockMicros) >= usPerClock) {
      lastClockMicros += usPerClock;
      onClockTick();
    }
  }
  hp.saveIfDirty();   // gibt es noch nicht – nur wenn du das später willst
}

// ===========================================================================
//  One-time data init
// ===========================================================================
void initDrumPads() {
  // Each 4x4 quadrant gets an identical drum-rack layout: bottom-left of the
  // quadrant = note 36, +1 per column, +4 per row. Channel 10 (GM percussion).
  for (uint8_t i = 0; i < 64; i++) {
    uint8_t x = i % 8, y = i / 8;
    uint8_t lx = x % 4, ly = y % 4;                 // position within the quadrant
    drumPads[i].note    = (uint8_t)(36 + ly * 4 + lx);
    drumPads[i].channel = 10;
  }
}

void initCCCtl() {
  // DEFAULTS - edit freely. Each entry = { CC number, MIDI channel, start value }.
  // Index i maps to group (i / 8), encoder (i % 8).
  for (uint8_t i = 0; i < CC_TOTAL; i++) {
    ccCtl[i].ccNum   = (uint8_t)(1 + i);   // CC 1..32
    ccCtl[i].channel = 1;                  // channel 1
    ccCtl[i].value   = 0;
  }
}

// ===========================================================================
//  Transport / clock
// ===========================================================================
void recalcTempo() {
  usPerClock = (uint32_t)(60000000.0f / (bpm * 24.0f));  // 24 PPQN
}

void startTransport() {
  running = true;
  clockCount = 0;
  curStep = 0;
  playCol = -1;
  lastClockMicros = micros();
  usbMIDI.sendRealTime(usbMIDI.Start);
}

void stopTransport() {
  running = false;
  usbMIDI.sendRealTime(usbMIDI.Stop);
  stopActiveNotes();
  playCol = -1;
  if (mode == MODE_SEQ) renderSeqPads();
}

void onClockTick() {
  usbMIDI.sendRealTime(usbMIDI.Clock);
  if (++clockCount >= CLOCKS_PER_STEP) {
    clockCount = 0;
    fireStep();
  }
}

void fireStep() {
  stopActiveNotes();

  int prevCol = playCol;
  playCol = curStep;

  for (uint8_t t = 0; t < seq.numTracks(); t++) {
    Track& tr = seq.track(t);
    if (!tr.muted && seq.stepOn(t, curStep)) {
      usbMIDI.sendNoteOn(tr.note, tr.velocity, tr.channel);
      if (activeCount < SEQ_TRACKS) active[activeCount++] = { tr.note, tr.channel };
    }
  }

  if (mode == MODE_SEQ) {
    if (prevCol >= 0) renderSeqColumn((uint8_t)prevCol);
    renderSeqColumn((uint8_t)playCol);
  }

  curStep = (curStep + 1) % seq.numSteps();
}

void stopActiveNotes() {
  for (uint8_t i = 0; i < activeCount; i++)
    usbMIDI.sendNoteOff(active[i].note, 0, active[i].channel);
  activeCount = 0;
}

// ===========================================================================
//  Push input callbacks
// ===========================================================================
void onPushNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  if (PUSH_DEBUG) Serial.printf("NoteOn  ch=%u note=%u vel=%u cable=%u\n", ch, note, vel, pushMidi.getCable());

  if (vel == 0) { onPushNoteOff(ch, note, 0); return; }
  if (!pushIsPadNote(note)) return;      // ignore encoder-touch notes etc.

  if (mode == MODE_PLAY) {
    int n = playNoteRaw(pushPadX(note), pushPadY(note));
    if (!playValid(n)) return;           // out-of-range (dark) pad -> silent
    usbMIDI.sendNoteOn((uint8_t)n, vel, noteChannel);
    lightPlayNote(n, true);              // light EVERY pad that plays this note
  } else if (mode == MODE_DRUM) {
    uint8_t idx = (uint8_t)(note - PUSH_PAD_BASE_NOTE);   // 0..63
    selectDrumPad(idx);
    DrumPad& p = drumPads[idx];
    usbMIDI.sendNoteOn(p.note, vel, p.channel);
    push.setPadByNote(note, CLR_GREEN);
  } else if (mode == MODE_HARDPUSH) {
      Serial.println("Mode is Hardpush");
      byte padIdx = pushPadY(note) * 8 + pushPadX(note);
      byte n = hp.pushButtonIndexToAbsoluteNote(padIdx);
      if (!hp.legalRange(n)) return;
      usbMIDI.sendNoteOn(n, vel, hp.channel());
      lightHardpushNote(n, true);
  } else {  // MODE_SEQ
    uint8_t track = pushPadY(note);
    uint8_t step  = pushPadX(note);
    seq.toggleStep(track, step);
    selectedTrack = track;
    renderSeqColumn(step);
    renderRightColumn();
    renderDisplay();
  }
}

void onPushNoteOff(uint8_t ch, uint8_t note, uint8_t vel) {
  if (!pushIsPadNote(note)) return;

  if (mode == MODE_PLAY) {
    int n = playNoteRaw(pushPadX(note), pushPadY(note));
    if (!playValid(n)) return;
    usbMIDI.sendNoteOff((uint8_t)n, 0, noteChannel);
    lightPlayNote(n, false);             // restore resting color on all its pads
  } else if (mode == MODE_HARDPUSH) {
    byte padIdx = pushPadY(note) * 8 + pushPadX(note);
    byte n = hp.pushButtonIndexToAbsoluteNote(padIdx);
    if (!hp.legalRange(n)) return;
    if (PUSH_DEBUG) Serial.printf("HARD NoteOff pad=%u n=%u ch=%u\n",
                                  padIdx, n, hp.channel());
    usbMIDI.sendNoteOff(n, 0, hp.channel());
    lightHardpushNote(n, false);

  } else if (mode == MODE_DRUM) {
    uint8_t idx = (uint8_t)(note - PUSH_PAD_BASE_NOTE);
    DrumPad& p = drumPads[idx];
    usbMIDI.sendNoteOff(p.note, 0, p.channel);
    push.setPadByNote(note, drumPadColor(idx));
  }
}

void onPushControlChange(uint8_t ch, uint8_t cc, uint8_t val) {
  if (PUSH_DEBUG) Serial.printf("CC      ch=%u cc=%u val=%u cable=%u\n", ch, cc, val, pushMidi.getCable());

  // ---- Encoders (relative) ----
  if (pushIsEncoderCC(cc)) {
    int8_t d = pushEncoderDelta(val);

    // Top 8 display encoders -> assignable CC controllers (all modes).
    if (cc >= CC_ENCODER_1 && cc <= (uint8_t)(CC_ENCODER_1 + 7)) {
      handleCCEncoder((uint8_t)(cc - CC_ENCODER_1), d);
      return;
    }

    // Tempo encoder: BPM (seq) / note channel (play) / selected-pad channel (drum)
    if (cc == CC_ENCODER_TEMPO) {
      if (mode == MODE_SEQ) {
        bpm = constrain(bpm + d, 30.0f, 300.0f);
        recalcTempo();
      } else if (mode == MODE_PLAY) {
        noteChannel = (uint8_t)constrain((int)noteChannel + d, 1, 16);
      } else if (mode == MODE_HARDPUSH) {
        int nc = constrain((int)hp.channel() + d, 1, 16);
        if ((byte)nc != hp.channel()) {
          hp.setChannel((byte)nc);
        }
      } else {
        DrumPad& p = drumPads[selectedDrumPad];
        p.channel = (uint8_t)constrain((int)p.channel + d, 1, 16);
      }
      renderDisplay();
      return;
    }

    // Swing encoder: root note (play) / selected-pad note number (drum)
    if (cc == CC_ENCODER_SWING) {
      if (mode == MODE_PLAY) {
        rootPitch = (uint8_t)((((int)rootPitch + d) % 12 + 12) % 12);
        renderNotePads();
      } else if (mode == MODE_HARDPUSH) {
        int t = (((int)hp.tonic() + d) % 12 + 12) % 12;
        if ((byte)t != hp.tonic()) {
          hp.setTonic((byte)t);
          renderHardpushPads();
        }
      } else if (mode == MODE_DRUM) {
        DrumPad& p = drumPads[selectedDrumPad];
        p.note = (uint8_t)constrain((int)p.note + d, 0, 127);
      }
      renderDisplay();
      return;
    }
    return;   // master encoder etc. - unused
  }

  // ---- Buttons: act on press (value >= 64) ----
  if (val < 64) return;

  // Hardpush
    // ===== HARDPUSH-mode buttons =====
  if (mode == MODE_HARDPUSH) {

    // Top row 20: Skala durchschalten
    if (cc == 20) {
      hp.setScale((hp.scale() + 1) % numberOfScales);
      renderHardpushPads(); renderDisplay();
      return;
    }
    // Top row 21: Tonic durchschalten
    if (cc == 21) {
      hp.setTonic((hp.tonic() + 1) % 12);
      renderHardpushPads(); renderDisplay();
      return;
    }
    // Top row 24: Channel 1..16
    if (cc == 24) {
      hp.setChannel((hp.channel() % 16) + 1);
      renderDisplay();
      return;
    }
    // Top row 25: Farbschema
    if (cc == 25) {
      hp.setColor((hp.colorScheme() + 1) % numberOfColorSchemes);
      renderHardpushPads(); renderDisplay();
      return;
    }
    // Top row 26: Layout
    if (cc == 26) {
      hp.setLayout((hp.layout() + 1) % numberOfLayouts);
      renderHardpushPads(); renderDisplay();
      return;
    }
    // Top row 27: Octave 0..8
    if (cc == 27) {
      hp.setOctave((hp.octave() + 1) % 9);
      renderHardpushPads(); renderDisplay();
      return;
    }

    // Octave up/down (55/54)
    if (cc == CC_OCTAVE_UP) {
      hp.setOctave((byte)constrain((int)hp.octave() + 1, 0, 8));
      renderHardpushPads(); renderDisplay();
      return;
    }
    if (cc == CC_OCTAVE_DN) {
      hp.setOctave((byte)constrain((int)hp.octave() - 1, 0, 8));
      renderHardpushPads(); renderDisplay();
      return;
    }

    // Preset select (102..105)
    if (cc >= 102 && cc <= 105) {
      hp.setPreset(cc - 102);
      renderHardpushPads(); renderDisplay();
      return;
    }
    // Page select (106..109)
    if (cc >= 106 && cc <= 109) {
      hp.setPage(cc - 106);
      renderHardpushPads(); renderDisplay();
      return;
    }
  }
  // end of Hardpush

  switch (cc) {
    case CC_PLAY:    running ? stopTransport() : startTransport(); renderModeButtons(); break;
    case CC_RECORD:  recording = !recording; renderModeButtons(); break;

    case CC_NOTE:
      // mode = (mode == MODE_SEQ) ? lastNoteMode
      //                           : (mode == MODE_PLAY ? MODE_DRUM : MODE_PLAY);
      if (mode == MODE_SEQ) {
        mode = lastNoteMode;                 // aus SEQ zurück in den letzten Pad-Modus
      } else {
        switch (mode) {
          case MODE_PLAY:   mode = MODE_DRUM;     break;
          case MODE_DRUM:   mode = MODE_HARDPUSH; break;
          case MODE_HARDPUSH: mode = MODE_PLAY;   break;
          default:          mode = MODE_PLAY;     break;
        }
        lastNoteMode = mode;
      }
      renderAll();
      break;
    case CC_SESSION: mode = MODE_SEQ; renderAll(); break;

    // PLAY-mode transpose
    case CC_LEFT:      if (mode == MODE_PLAY) transposePlay(+1); break;
    case CC_RIGHT:     if (mode == MODE_PLAY) transposePlay(-1); break;
    case CC_UP:        if (mode == MODE_PLAY) transposePlay(+PLAY_ROW_INTERVAL); break;
    case CC_DOWN:      if (mode == MODE_PLAY) transposePlay(-PLAY_ROW_INTERVAL); break;
    case CC_OCTAVE_UP: if (mode == MODE_PLAY) transposePlay(+12); break;
    case CC_OCTAVE_DN: if (mode == MODE_PLAY) transposePlay(-12); break;

    // CC-encoder group paging (all modes)
    case CC_PAGE_LEFT:  ccGroup = (uint8_t)((ccGroup + CC_GROUPS - 1) % CC_GROUPS); renderDisplay(); break;
    case CC_PAGE_RIGHT: ccGroup = (uint8_t)((ccGroup + 1) % CC_GROUPS);             renderDisplay(); break;

    default:
      // Right column (36..43, top..bottom) selects a track (sequencer).
      if (cc >= CC_RIGHT_COL_TOP && cc <= CC_RIGHT_COL_TOP + 7 && mode == MODE_SEQ) {
        selectedTrack = (uint8_t)(43 - cc);
        renderRightColumn();
        renderDisplay();
      }
      // Top row (20..27) mutes track 0..7 (sequencer).
      else if (cc >= CC_UPPER_ROW_1 && cc <= CC_UPPER_ROW_1 + 7 && mode == MODE_SEQ) {
        uint8_t t = (uint8_t)(cc - CC_UPPER_ROW_1);
        seq.track(t).muted = !seq.track(t).muted;
        renderTopRow();
        renderDisplay();
      }
      break;
  }
}

void onPushPolyAT(uint8_t ch, uint8_t note, uint8_t val) {
  if (!pushIsPadNote(note)) return;
  if (mode == MODE_PLAY) {
    int n = playNoteRaw(pushPadX(note), pushPadY(note));
    if (playValid(n)) usbMIDI.sendAfterTouchPoly((uint8_t)n, val, noteChannel);
  } else if (mode == MODE_HARDPUSH) {
  byte padIdx = pushPadY(note) * 8 + pushPadX(note);
  byte n = hp.pushButtonIndexToAbsoluteNote(padIdx);
  if (hp.legalRange(n))
    usbMIDI.sendAfterTouchPoly(n, val, hp.channel());
  } else if (mode == MODE_DRUM) {
    DrumPad& p = drumPads[note - PUSH_PAD_BASE_NOTE];
    usbMIDI.sendAfterTouchPoly(p.note, val, p.channel);
  }
}

void onPushChannelAT(uint8_t ch, uint8_t pressure) {
  if (mode == MODE_PLAY)
    usbMIDI.sendAfterTouch(pressure, noteChannel);
  else if (mode == MODE_HARDPUSH) usbMIDI.sendAfterTouch(pressure, hp.channel());
  else if (mode == MODE_DRUM)
    usbMIDI.sendAfterTouch(pressure, drumPads[selectedDrumPad].channel);
}

// ===========================================================================
//  Mode-specific actions
// ===========================================================================
void handleCCEncoder(uint8_t e, int8_t d) {
  uint8_t i = (uint8_t)(ccGroup * 8 + e);
  CCCtl& c = ccCtl[i];
  int v = constrain((int)c.value + d, 0, 127);
  if ((uint8_t)v != c.value) {
    c.value = (uint8_t)v;
    usbMIDI.sendControlChange(c.ccNum, c.value, c.channel);
  }
  renderDisplay();
}

void transposePlay(int semis) {
  int nb = constrain((int)playBase + semis, 0, 127);
  if (nb != (int)playBase) {
    playBase = (uint8_t)nb;
    renderNotePads();
    renderDisplay();
  }
}

void selectDrumPad(uint8_t idx) {
  if (idx >= 64) return;
  if (idx != selectedDrumPad) {
    uint8_t old = selectedDrumPad;
    selectedDrumPad = idx;
    push.setPad(old % 8, old / 8, drumQuadColor(old));  // old pad loses its highlight
  }
  renderDisplay();
}

// -----  HARDPUSH_MODE - Helpers
// ---- HARDPUSH-mode helpers ------------------------------------------------
uint8_t hardpushRestColor(byte padIdx) {
  byte n = hp.pushButtonIndexToAbsoluteNote(padIdx);
  if (!hp.legalRange(n)) return CLR_OFF;
  byte scheme = hp.colorScheme();
  if (hp.pushButtonIndexToScaleIndex(padIdx) % scaleSizes[hp.scale()] == 0)
    return colors[scheme][1];    // Skalen-Grundton (root) -> Palette-Slot 1
  return colors[scheme][0];      // anderer Skalenton      -> Palette-Slot 0
}

void renderHardpushPads() {
  for (uint8_t y = 0; y < 8; y++)
    for (uint8_t x = 0; x < 8; x++)
      push.setPad(x, y, hardpushRestColor(y * 8 + x));
}

void lightHardpushNote(byte outNote, bool pressed) {
  // byte scheme = hp.colorScheme();
  // for (uint8_t y = 0; y < 8; y++) {
  //   for (uint8_t x = 0; x < 8; x++) {
  //     byte padIdx = y * 8 + x;
  //     byte n = hp.pushButtonIndexToAbsoluteNote(padIdx);
  //     if (hp.legalRange(n) && n == outNote)
  //       push.setPad(x, y, pressed ? colors[scheme][2] : hardpushRestColor(padIdx));
  //   }
  // }

  byte scheme = hp.colorScheme();
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 8; x++) {
      byte padIdx = y * 8 + x;
      byte n = hp.pushButtonIndexToAbsoluteNote(padIdx);
      if (hp.legalRange(n) && n == outNote)
        push.setPad(x, y, pressed ? CLR_GREEN : hardpushRestColor(padIdx));
    }
  }
}




// ===========================================================================
//  Rendering (LEDs + display)
// ===========================================================================
void renderAll() {
  renderModeButtons();
  renderRightColumn();
  renderTopRow();
  if      (mode == MODE_SEQ)  renderSeqPads();
  else if (mode == MODE_DRUM) renderDrumPads();
  else if (mode == MODE_PLAY) renderNotePads();
  else /* Mode Hardpush*/     renderHardpushPads();

  renderDisplay();
}

void renderModeButtons() {
  // push.setButton(CC_NOTE,    (mode == MODE_PLAY || mode == MODE_DRUM) ? BTN_LIT : BTN_DIM);
  // push.setButton(CC_SESSION, mode == MODE_SEQ  ? BTN_LIT : BTN_DIM);
  // push.setButton(CC_PLAY,    running   ? BTN_LIT : BTN_DIM);
  // push.setButton(CC_RECORD,  recording ? BTN_LIT_BLINK : BTN_DIM);

  bool padMode = (mode == MODE_PLAY || mode == MODE_DRUM || mode == MODE_HARDPUSH);
  push.setButton(CC_NOTE,    padMode  ? BTN_LIT : BTN_DIM);
  push.setButton(CC_SESSION, (mode == MODE_SEQ) ? BTN_LIT : BTN_DIM);
  push.setButton(CC_PLAY,    running   ? BTN_LIT : BTN_DIM);
  push.setButton(CC_RECORD,  recording ? BTN_LIT_BLINK : BTN_DIM);  

  bool hp = (mode == MODE_HARDPUSH);
  push.setButton(CC_OCTAVE_UP, hp ? BTN_LIT : BTN_OFF);
  push.setButton(CC_OCTAVE_DN, hp ? BTN_LIT : BTN_OFF);
}

void renderRightColumn() {
  for (uint8_t t = 0; t < seq.numTracks(); t++) {
    uint8_t cc = (uint8_t)(43 - t);
    push.setButton(cc, (mode == MODE_SEQ && t == selectedTrack) ? BTN_LIT : BTN_DIM);
  }
}

void renderTopRow() {
  for (uint8_t t = 0; t < seq.numTracks(); t++)
    push.setButton((uint8_t)(CC_UPPER_ROW_1 + t),
                   (mode == MODE_SEQ && !seq.track(t).muted) ? BTN_LIT : BTN_DIM);
}

void renderSeqPads() {
  for (uint8_t x = 0; x < seq.numSteps(); x++) renderSeqColumn(x);
}

void renderSeqColumn(uint8_t col) {
  for (uint8_t y = 0; y < seq.numTracks(); y++) {
    bool on   = seq.stepOn(y, col);
    bool head = running && (col == (uint8_t)playCol);
    uint8_t color;
    if (head) color = on ? CLR_WHITE : CLR_DIMGREY;
    else      color = on ? trackColor(y) : CLR_OFF;
    push.setPad(col, y, color);
  }
}

void renderNotePads() {
  // PLAY mode: root pitch-class highlighted; out-of-range pads dark.
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 8; x++) {
      int n = playNoteRaw(x, y);
      if (!playValid(n))                 push.setPad(x, y, CLR_OFF);
      else if ((n % 12) == rootPitch)    push.setPad(x, y, CLR_BLUE);
      else                               push.setPad(x, y, CLR_DIMGREY);
    }
  }
}

void renderDrumPads() {
  for (uint8_t i = 0; i < 64; i++)
    push.setPad(i % 8, i / 8, drumPadColor(i));
}

void lightPlayNote(int outNote, bool pressed) {
  // Light (or restore) every pad that maps to outNote.
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 8; x++) {
      int n = playNoteRaw(x, y);
      if (playValid(n) && n == outNote) {
        uint8_t rest = ((n % 12) == rootPitch) ? CLR_BLUE : CLR_DIMGREY;
        push.setPad(x, y, pressed ? CLR_GREEN : rest);
      }
    }
  }
}

void renderDisplay() {
  char l[PUSH_DISPLAY_COLS + 1];
  push.clearText();

  const char* title = mode == MODE_SEQ  ? "TEENSY x PUSH   -   STEP SEQUENCER" :
                      mode == MODE_DRUM ? "TEENSY x PUSH   -   DRUM MODE"      :
                      mode == MODE_HARDPUSH ? "TEENSY x Push  -  HARDPUSH PADS":
                                          "TEENSY x PUSH   -   PLAY MODE";
  push.setLineCentered(0, title);

  snprintf(l, sizeof(l), "BPM %3d   %s%s", (int)bpm,
           running ? "PLAYING" : "STOPPED", recording ? "  REC" : "");
  push.setText(1, 0, l);

  if (mode == MODE_SEQ) {
    Track& tr = seq.track(selectedTrack);
    snprintf(l, sizeof(l), "Trk %u %-6s     Note %3u Vel %3u Ch %2u %s",
             selectedTrack + 1, trackName(selectedTrack),
             tr.note, tr.velocity, tr.channel, tr.muted ? "MUTE" : "");
    push.setText(2, 0, l);
  } else if (mode == MODE_DRUM) {
    DrumPad& p = drumPads[selectedDrumPad];
    snprintf(l, sizeof(l), "Pad(%u,%u)         Note %3u %-2s[Swng]Ch %2u     [Tempo]",
             selectedDrumPad % 8, selectedDrumPad / 8,
             p.note, NOTE_NAMES[p.note % 12], p.channel);
    push.setText(2, 0, l);
  } 
  
  else if (mode == MODE_HARDPUSH) {
    // snprintf(l, sizeof(l), "%-16.16s %-2.2s Ch%2u  %-14.14s  %-10.10s  Oct %u  P%u/%u",
    //      nameOfScales[hp.scale()],
    //      nameOfKeys[hp.tonic()],
    //      hp.channel(),
    //      nameOfLayouts[hp.layout()],
    //      nameOfColors[hp.colorScheme()],
    //      hp.octave(),
    //      hp.preset() + 1,
    //      hp.page() + 1);
    snprintf(l, sizeof(l),
         "%-8.8s %-8.8s %-7s %-7s Ch%-4u %-8.8s %-8.8s Oct%-3u",
         nameOfScales[hp.scale()],
         nameOfKeys[hp.tonic()],
         "",
         "",
         (unsigned)hp.channel(),
         nameOfColors[hp.colorScheme()],
         nameOfLayouts[hp.layout()],
         (unsigned)hp.octave());

    push.setText(2, 0, l);
}
  
  else {  // MODE_PLAY
    snprintf(l, sizeof(l), "Base %3u %-2s      Root %-2s  Row +%d  MIDI Ch %2u  ",
             playBase, NOTE_NAMES[playBase % 12], NOTE_NAMES[rootPitch],
             noteChannel, (int)PLAY_ROW_INTERVAL);
    push.setText(2, 0, l);
  }

  // Line 3: active CC group + its 8 live values.
  int p = snprintf(l, sizeof(l), "CC G%u ", ccGroup + 1);
  for (uint8_t i = 0; i < 8; i++)
    p += snprintf(l + p, sizeof(l) - p, " %3u", ccCtl[ccGroup * 8 + i].value);
  push.setText(3, 0, l);

  push.flushDisplay();
}

// ===========================================================================
//  Helpers
// ===========================================================================
int playNoteRaw(uint8_t x, uint8_t y) {
  return (int)playBase + (int)y * PLAY_ROW_INTERVAL + (int)x;
}

bool playValid(int n) {
  return n >= 0 && n <= 127;
}

uint8_t drumQuadColor(uint8_t idx) {
  bool right = (idx % 8) >= 4;
  bool top   = (idx / 8) >= 4;
  if (!right && !top) return CLR_AMBER;    // bottom-left
  if ( right && !top) return CLR_CYAN;     // bottom-right
  if (!right &&  top) return CLR_MAGENTA;  // top-left
  return CLR_LIME;                         // top-right
}

uint8_t drumPadColor(uint8_t idx) {
  return (idx == selectedDrumPad) ? CLR_WHITE : drumQuadColor(idx);
}

uint8_t trackColor(uint8_t t) {
  static const uint8_t c[SEQ_TRACKS] =
    { CLR_RED, CLR_AMBER, CLR_YELLOW, CLR_GREEN, CLR_CYAN, CLR_BLUE, CLR_MAGENTA, CLR_LIME };
  return c[t % SEQ_TRACKS];
}

const char* trackName(uint8_t t) {
  static const char* n[SEQ_TRACKS] =
    { "Kick", "Snare", "ClHat", "OpHat", "Clap", "Tom", "Crash", "Ride" };
  return n[t % SEQ_TRACKS];
}
