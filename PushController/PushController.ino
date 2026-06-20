// =============================================================================
//  PushController.ino
// -----------------------------------------------------------------------------
//  Teensy 3.6 + Ableton Push 1  ->  standalone MIDI controller + step sequencer
//
//  Signal path:
//    iPad  <--USB(device)--  Teensy  --USB(host)-->  Push 1
//
//  The Teensy is a USB MIDI *device* to the iPad (so iPad synth/sampler apps see
//  it as a class-compliant MIDI port) and a USB MIDI *host* to the Push.
//
//  REQUIRED IDE SETTINGS (Arduino IDE + Teensyduino):
//    Tools > Board    : Teensy 3.6
//    Tools > USB Type : "MIDI"  (or "Serial + MIDI" to keep the Serial Monitor)
//  REQUIRED LIBRARY : USBHost_t36 (bundled with Teensyduino)
//
//  Two modes (toggle with the Note / Session buttons):
//    NOTE MODE     - the 8x8 pads play notes (chromatic) out to the iPad.
//    SEQUENCER     - the 8x8 grid is the pattern: rows = tracks, columns = steps.
//                    Tap a pad to toggle a step. A playhead column sweeps across.
//
//  Controls:
//    Play (CC85)          start/stop transport (also emits MIDI Start/Stop/Clock)
//    Record (CC86)        toggles a record flag (stub - extend as you like)
//    Note (CC50)          NOTE mode      Session (CC51)  SEQUENCER mode
//    Tempo encoder (CC14) change BPM
//    Encoder 1 (CC71)     change selected track's NOTE     (sequencer)
//    Encoder 2 (CC72)     change selected track's VELOCITY (sequencer)
//    Right column (36-43) select a track                   (sequencer)
//    Top row (20-27)      mute/unmute track 0..7            (sequencer)
//    Octave Up/Down       shift the pad range by an octave  (note mode)
// =============================================================================
#include <USBHost_t36.h>
#include "Push1Defs.h"
#include "PushDevice.h"
#include "Sequencer.h"

#if !defined(USB_MIDI_SERIAL) && !defined(USB_MIDI) && !defined(USB_MIDI4) && \
    !defined(USB_MIDI16) && !defined(USB_MIDI16_SERIAL) && !defined(USB_MIDI4_SERIAL)
#warning "Set Tools > USB Type to a MIDI option (e.g. 'MIDI' or 'Serial + MIDI')."
#endif

// Set to 1 to print every incoming Push message to the Serial Monitor.
// Use this to discover the exact note/CC of any control on your unit.
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
// cable 0 is the Live port
// cable 1 is the user port that allows writing to the display
const uint8_t PUSH_CABLE = 1;

PushDevice  push(pushMidi, PUSH_CABLE);
Sequencer   seq;

// ---------------------------------------------------------------------------
//  App state
// ---------------------------------------------------------------------------
enum Mode { MODE_NOTE, MODE_SEQ };
Mode    mode          = MODE_SEQ;
bool    running       = false;
bool    recording     = false;
float   bpm           = 120.0f;
uint8_t selectedTrack = 0;

// Transport (24 PPQN internal clock)
const uint8_t CLOCKS_PER_STEP = 12;   // 12 = eighth notes; 6 = sixteenth notes
uint32_t usPerClock      = 0;
uint32_t lastClockMicros = 0;
uint8_t  clockCount      = 0;
uint8_t  curStep         = 0;         // step about to play
int      playCol         = -1;        // currently sounding column (-1 = none)

// Notes started this step, to be stopped at the next step (gate = 1 step)
struct ActiveNote { uint8_t note, channel; };
ActiveNote active[SEQ_TRACKS];
uint8_t    activeCount = 0;

// NOTE mode
uint8_t noteBase    = 36;   // note produced by the bottom-left pad
uint8_t noteChannel = 1;

bool pushReady = false;

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

void renderAll();
void renderModeButtons();
void renderRightColumn();
void renderTopRow();
void renderSeqPads();
void renderSeqColumn(uint8_t col);
void renderNotePads();
void renderDisplay();

uint8_t     trackColor(uint8_t t);
const char* trackName(uint8_t t);
uint8_t     padToOutNote(uint8_t padNote);

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
  recalcTempo();
}

void loop() {
  myusb.Task();
  while (pushMidi.read()) { }   // dispatch all queued Push events to the callbacks
  while (usbMIDI.read())  { }   // drain the iPad side (room for future sync/feedback)

  // (re)initialise the Push whenever it connects
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

  // Internal clock: emit ticks; "while" catches up if a loop ran long.
  if (running) {
    uint32_t now = micros();
    while ((uint32_t)(now - lastClockMicros) >= usPerClock) {
      lastClockMicros += usPerClock;
      onClockTick();
    }
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
  usbMIDI.sendRealTime(usbMIDI.Clock);   // keep iPad apps in sync
  if (++clockCount >= CLOCKS_PER_STEP) {
    clockCount = 0;
    fireStep();
  }
}

void fireStep() {
  stopActiveNotes();                     // end previous step's notes

  int prevCol = playCol;
  playCol = curStep;

  for (uint8_t t = 0; t < seq.numTracks(); t++) {
    Track& tr = seq.track(t);
    if (!tr.muted && seq.stepOn(t, curStep)) {
      usbMIDI.sendNoteOn(tr.note, tr.velocity, tr.channel);
      if (activeCount < SEQ_TRACKS) active[activeCount++] = { tr.note, tr.channel };
    }
  }

  // Light only the columns that changed (cheap, no full-grid refresh)
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

  // Velocity 0 == note off (running-status style)
  if (vel == 0) { onPushNoteOff(ch, note, 0); return; }

  if (!pushIsPadNote(note)) return;      // ignore encoder-touch notes etc.

  if (mode == MODE_NOTE) {
    uint8_t out = padToOutNote(note);
    usbMIDI.sendNoteOn(out, vel, noteChannel);
    push.setPadByNote(note, CLR_GREEN);
  } else {
    uint8_t track = pushPadY(note);      // bottom row = track 0
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

  if (mode == MODE_NOTE) {
    usbMIDI.sendNoteOff(padToOutNote(note), 0, noteChannel);
    // restore the resting color (root notes highlighted)
    uint8_t idx = note - PUSH_PAD_BASE_NOTE;
    push.setPadByNote(note, ((idx) % 12 == 0) ? CLR_BLUE : CLR_DIMGREY);
  }
}

void onPushControlChange(uint8_t ch, uint8_t cc, uint8_t val) {
  if (PUSH_DEBUG) Serial.printf("CC      ch=%u cc=%u val=%u cable=%u\n", ch, cc, val, pushMidi.getCable());

  // ---- Encoders (relative) ----
  if (pushIsEncoderCC(cc)) {
    int8_t d = pushEncoderDelta(val);
    if (cc == CC_ENCODER_TEMPO) {
      bpm = constrain(bpm + d, 30.0f, 300.0f);
      recalcTempo();
      renderDisplay();
    } else if (cc == CC_ENCODER_1 && mode == MODE_SEQ) {
      Track& tr = seq.track(selectedTrack);
      tr.note = (uint8_t)constrain((int)tr.note + d, 0, 127);
      renderDisplay();
    } else if (cc == (CC_ENCODER_1 + 1) && mode == MODE_SEQ) {  // encoder 2
      Track& tr = seq.track(selectedTrack);
      tr.velocity = (uint8_t)constrain((int)tr.velocity + d, 1, 127);
      renderDisplay();
    }
    return;
  }

  // ---- Buttons: act on press (value >= 64), ignore release ----
  if (val < 64) return;

  switch (cc) {
    case CC_PLAY:    running ? stopTransport() : startTransport(); renderModeButtons(); break;
    case CC_RECORD:  recording = !recording; renderModeButtons(); break;
    case CC_NOTE:    mode = MODE_NOTE; renderAll(); break;
    case CC_SESSION: mode = MODE_SEQ;  renderAll(); break;

    case CC_OCTAVE_UP:
      if (mode == MODE_NOTE && noteBase <= 115) { noteBase += 12; renderNotePads(); renderDisplay(); }
      break;
    case CC_OCTAVE_DN:
      if (mode == MODE_NOTE && noteBase >= 12)  { noteBase -= 12; renderNotePads(); renderDisplay(); }
      break;

    default:
      // Right column (36..43, top..bottom) selects a track.
      if (cc >= CC_RIGHT_COL_TOP && cc <= CC_RIGHT_COL_TOP + 7 && mode == MODE_SEQ) {
        selectedTrack = (uint8_t)(43 - cc);   // top button -> top row (track 7)
        renderRightColumn();
        renderDisplay();
      }
      // Top row (20..27) mutes track 0..7.
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
  if (mode == MODE_NOTE && pushIsPadNote(note))
    usbMIDI.sendAfterTouchPoly(padToOutNote(note), val, noteChannel);
}

void onPushChannelAT(uint8_t ch, uint8_t pressure) {
  if (mode == MODE_NOTE)
    usbMIDI.sendAfterTouch(pressure, noteChannel);
}

// ===========================================================================
//  Rendering (LEDs + display)
// ===========================================================================
void renderAll() {
  renderModeButtons();
  renderRightColumn();
  renderTopRow();
  if (mode == MODE_SEQ) renderSeqPads();
  else                  renderNotePads();
  renderDisplay();
}

void renderModeButtons() {
  push.setButton(CC_NOTE,    mode == MODE_NOTE ? BTN_LIT : BTN_DIM);
  push.setButton(CC_SESSION, mode == MODE_SEQ  ? BTN_LIT : BTN_DIM);
  push.setButton(CC_PLAY,    running   ? BTN_LIT : BTN_DIM);
  push.setButton(CC_RECORD,  recording ? BTN_LIT_BLINK : BTN_DIM);
}

void renderRightColumn() {
  for (uint8_t t = 0; t < seq.numTracks(); t++) {
    uint8_t cc = (uint8_t)(43 - t);                       // track t -> its row button
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
    bool on   = seq.stepOn(y, col);                       // row y = track y
    bool head = running && (col == (uint8_t)playCol);
    uint8_t color;
    if (head) color = on ? CLR_WHITE : CLR_DIMGREY;
    else      color = on ? trackColor(y) : CLR_OFF;
    push.setPad(col, y, color);
  }
}

void renderNotePads() {
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 8; x++) {
      uint8_t idx = (uint8_t)(y * 8 + x);                 // 0..63
      push.setPad(x, y, (idx % 12 == 0) ? CLR_BLUE : CLR_DIMGREY);
    }
  }
}

void renderDisplay() {
  char l[PUSH_DISPLAY_COLS + 1];
  push.clearText();

  push.setLineCentered(0, mode == MODE_SEQ ? "TEENSY x PUSH   -   STEP SEQUENCER"
                                           : "TEENSY x PUSH   -   NOTE MODE");

  snprintf(l, sizeof(l), "BPM %3d     %s%s", (int)bpm,
           running ? "PLAYING" : "STOPPED", recording ? "  REC" : "");
  push.setText(1, 0, l);

  if (mode == MODE_SEQ) {
    Track& tr = seq.track(selectedTrack);
    snprintf(l, sizeof(l), "Trk %u %-6s  Note %3u  Vel %3u  Ch %2u  %s",
             selectedTrack + 1, trackName(selectedTrack),
             tr.note, tr.velocity, tr.channel, tr.muted ? "MUTED" : "");
    push.setText(2, 0, l);
    push.setLineCentered(3, "Pads=steps  RightCol=select  TopRow=mute  Enc1/2=note/vel");
  } else {
    snprintf(l, sizeof(l), "Base note %3u   Channel %2u", noteBase, noteChannel);
    push.setText(2, 0, l);
    push.setLineCentered(3, "Pads=play   Octave +/-   Session=sequencer");
  }

  push.flushDisplay();
}

// ===========================================================================
//  Helpers
// ===========================================================================
uint8_t padToOutNote(uint8_t padNote) {
  int n = (int)noteBase + (padNote - PUSH_PAD_BASE_NOTE);
  return (uint8_t)constrain(n, 0, 127);
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
