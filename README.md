# 125A Arporator

Musical MIDI arpeggiator with a deliberately simple workflow and deeper musical behaviour than a conventional DAW arp.

## Development status

Current development branch: `v1.0.0-release`

The V1.0 core is being built measurement-first and GUI-last.

### Core design targets

- freely selectable pattern length: 1–32 steps
- trigger restart: a new phrase can begin at Step 1 wherever it is triggered in the DAW
- optional Continue behaviour
- Up / Down / Up-Down / Down-Up / Played / Random note order
- 1–4 octave range
- host-synchronised rate
- per-step enable, velocity, gate, ratchet (1x–4x), probability and octave offset
- Swing
- Scale policy: Chord Only / Scale / Chromatic
- deterministic behaviour whenever probability/random features are disabled
- no stuck notes on Note-Off / All-Notes-Off / reset

Implemented musical layer:

- Humanize
- Strum
- Groove
- Variate + dimension/per-step Locks
- Evolve

The custom VSTGUI editor provides the 32-step overview, selected-step editing, 100/150% zoom and separate playback playhead.

The goal is not to reproduce BlueARP/Stepic complexity. The target is immediate operation with selected high-value ideas found in modern arpeggiators such as Omnisphere, while remaining substantially more capable than a basic DAW arp.

## Engineering

125A engineering rules are defined in `challanger2000/125A-Engineering`.
