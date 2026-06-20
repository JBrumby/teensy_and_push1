// =============================================================================
//  Sequencer.cpp
// =============================================================================
#include "Sequencer.h"

// Default kit: track 0 (bottom row) .. track 7 (top row).
// General MIDI drum notes; channel 10 is the GM percussion channel.
static const uint8_t kKitNotes[SEQ_TRACKS] = {
  36,  // Kick
  38,  // Snare
  42,  // Closed Hat
  46,  // Open Hat
  39,  // Clap
  50,  // Hi Tom
  49,  // Crash
  51,  // Ride
};

void Sequencer::begin() {
  for (uint8_t t = 0; t < SEQ_TRACKS; t++) {
    _tracks[t].note     = kKitNotes[t];
    _tracks[t].channel  = 10;
    _tracks[t].velocity = 110;
    _tracks[t].muted    = false;
    for (uint8_t s = 0; s < SEQ_STEPS; s++) _tracks[t].steps[s] = false;
  }
}

void Sequencer::toggleStep(uint8_t t, uint8_t s) {
  if (t < SEQ_TRACKS && s < SEQ_STEPS) _tracks[t].steps[s] = !_tracks[t].steps[s];
}

bool Sequencer::stepOn(uint8_t t, uint8_t s) const {
  return (t < SEQ_TRACKS && s < SEQ_STEPS) ? _tracks[t].steps[s] : false;
}

void Sequencer::clearTrack(uint8_t t) {
  if (t < SEQ_TRACKS) for (uint8_t s = 0; s < SEQ_STEPS; s++) _tracks[t].steps[s] = false;
}

void Sequencer::clearAll() {
  for (uint8_t t = 0; t < SEQ_TRACKS; t++) clearTrack(t);
}
