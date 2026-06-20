# Teensy 3.6 × Ableton Push 1 — Controller + Sequencer

A standalone MIDI controller and step sequencer. The Teensy is a USB MIDI
**host** to the Push and a USB MIDI **device** to your iPad:

```
iPad  <-- USB (Teensy = device) --  Teensy 3.6  -- USB (Teensy = host) -->  Push 1
```

## What it does (v1)

- **Note mode** — the 8×8 pads play notes (chromatic) out to the iPad. Velocity
  and aftertouch pass through. Octave Up/Down shift the range. Root notes are lit.
- **Sequencer mode** — the 8×8 grid *is* the pattern: each **row is a track**,
  each **column is a step**. Tap a pad to toggle a step; a playhead column sweeps
  across. Defaults to a GM drum kit on channel 10, so it drives a drum app out of
  the box.
- **Transport** — Play starts/stops and sends MIDI **Start/Stop/Clock** (24 PPQN)
  so iPad apps stay in sync. The Tempo encoder sets BPM.
- **Display** — shows mode, BPM, transport state, and the selected track.

## Files

| File | Role |
|------|------|
| `PushController.ino` | Transport/clock, Push callbacks, the two modes, all LED/display rendering |
| `PushDevice.h/.cpp`  | Output driver for the Push (modes, pad/button LEDs, text display) |
| `Sequencer.h/.cpp`   | Pattern data model (8 tracks × 8 steps) |
| `Push1Defs.h`        | Push 1 protocol constants and helpers |

Keep all files in a folder named `PushController` (Arduino requires the folder
name to match the `.ino`). They show up as tabs in the IDE.

## Build & flash

1. Install **Arduino IDE + Teensyduino**.
2. `Tools → Board → Teensy 3.6`.
3. `Tools → USB Type → MIDI` (or **Serial + MIDI** to keep the Serial Monitor
   for the debug output).
4. The **USBHost_t36** library ships with Teensyduino — no extra install.
5. Open `PushController.ino`, compile, upload.

## Wiring & power (read this)

- The Teensy 3.6 has a **second 5-pin USB header** for host mode. Solder the
  standard 5-pin header and use a Teensy USB host cable; the Push plugs into that.
- The iPad connects to the Teensy's **micro-USB** via a USB-C / Lightning adapter
  (Camera Connection Kit). The Teensy enumerates as a class-compliant MIDI device.
- **Power the Push from its own supply.** The Push 1 draws more current than the
  Teensy host port can comfortably provide; on bus power it runs dim / in
  low-power mode. Give it its wall adapter and let the host port carry data only.
- Powering the whole rig: the iPad generally won't power the Teensy + host bus.
  Feed the Teensy 5 V on `VIN` (and the host port's `VUSB`) from a small USB
  power source, and keep the Push on its adapter.

## How the protocol is used

- **Pads:** `note = y*8 + 36 + x`, (0,0) = bottom-left, notes 36–99. Pads send
  Note-On/Off; set a pad's color by sending its note with the **velocity = palette
  index** (0 = off, 3 = white confirmed; others are tunable, see `PushColor`).
- **Encoders:** relative CCs — 1…63 = clockwise, 127…65 = counter-clockwise.
  `pushEncoderDelta()` converts to a signed step.
- **Buttons:** CCs; LED state is the CC value (0 off, 1 dim, 4 lit, plus blink
  states). The Push does the blinking for you.
- **Display:** four lines, written per line with
  `F0 47 7F 15 (24+line) 00 <len> <offset> <chars…> F7`.

## Tuning for *your* unit

There are no official docs, and small firmware differences exist, so two knobs
are exposed:

1. **`PUSH_CABLE`** (in the `.ino`). The Push presents two virtual MIDI cables
   (a "Live" and a "User" port). Input is handled regardless of cable, but LED /
   display **output** must target the right one. If nothing lights up, flip this
   between `0` and `1`.
2. **`PUSH_DEBUG`** (set to `1`). Open the Serial Monitor and press any control —
   you'll see the exact note/CC and the cable it arrived on. Use this to confirm
   or correct the CC numbers in `Push1Defs.h` (the verified *ranges* are marked;
   the transport CCs are the standard map and worth a quick check).
3. **Display length byte** — `writeLineHW()` sends `PUSH_DISPLAY_COLS + 1` as the
   length, per the pushmod spec (`len = Nchars + 1`). If text ever looks shifted
   or truncated on your unit, this byte is the one to nudge by ±1.

## Easy next steps

- **More steps:** bump `SEQ_STEPS` to 16/32 and add a "page" variable; render a
  4×4 or strip of steps and page with the arrow buttons.
- **Per-step velocity / probability / micro-timing:** widen `Track::steps[]` from
  `bool` to a small struct.
- **External sync:** instead of the internal clock, count incoming MIDI Clock in
  the `usbMIDI` read loop and advance `fireStep()` from it.
- **Pattern save/recall:** the Teensy 3.6 has an SD slot — serialize `Sequencer`.
- **Note-mode scales:** map pads to a scale instead of chromatic and light only
  in-scale pads (use `setPadRGB()` for nicer colors).
- **Swing:** offset odd steps using the Swing encoder (CC15).
- **Record:** the `recording` flag is a stub — capture pad hits into steps while
  the transport runs.

## References

- Push 1 reverse engineering: pushmod.blogspot.com (SysEx, CC/note map, colors)
- Ableton Push 2 MIDI & display interface: github.com/Ableton/push-interface
  (pads/encoders behave like the Push 1)
- Teensy USB host MIDI: the `USBHost_t36` library examples (PJRC)
