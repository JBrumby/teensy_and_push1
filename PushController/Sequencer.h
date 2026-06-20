// =============================================================================
//  Sequencer.h  -  Minimal multi-track step sequencer (data model)
// -----------------------------------------------------------------------------
//  This holds the pattern only. Transport/timing and MIDI output live in the
//  .ino so you can swap clocking strategies without touching the data model.
//
//  Default layout: 8 tracks x 8 steps. Each track has its own MIDI note +
//  channel, so out of the box it drives a drum app (GM kit on channel 10).
//  Expand SEQ_STEPS to 16/32 and add "pages" later without changing this API.
// =============================================================================
#pragma once
#include <Arduino.h>

static const uint8_t SEQ_TRACKS = 8;
static const uint8_t SEQ_STEPS  = 8;

struct Track {
  uint8_t note;       // MIDI note this track triggers
  uint8_t channel;    // 1..16
  uint8_t velocity;   // default hit velocity
  bool    muted;
  bool    steps[SEQ_STEPS];
};

class Sequencer {
public:
  void begin();                                  // load a default GM drum kit
  void toggleStep(uint8_t track, uint8_t step);
  bool stepOn(uint8_t track, uint8_t step) const;
  void clearTrack(uint8_t track);
  void clearAll();

  Track&       track(uint8_t t)       { return _tracks[t]; }
  const Track& track(uint8_t t) const { return _tracks[t]; }
  uint8_t      numTracks() const { return SEQ_TRACKS; }
  uint8_t      numSteps()  const { return SEQ_STEPS;  }

private:
  Track _tracks[SEQ_TRACKS];
};
