# Fetcher Studio captured heavy amp

Package 76 / native API 35 embeds the full RiffSmith Distorted capture by Matt
Hartman, MIT licensed. RiffSmith explicitly licenses its own bundled captures
under MIT in its README. Model/code licenses are separate from the CC BY 4.0
guitar recordings. No endorsement by the creators or any artist is implied.

Pinned source and hashes: `provenance.json`. Preserve both license files when
redistributing. The source model is retained here for audit/rebuilding; the game
uses generated fixed coefficients in `capturedampdata.hpp`. Run
`scripts/procedural-synth-assets/build-captured-amp.py` to regenerate them.
No .nam parser, external plugin, network model fetch or new runtime dependency
is required. Only the reviewed full eight-channel response is supported.

The capture includes cabinet coloration: its presets use Cabinet Off. Character
Captured high gain uses Drive 12 dB for the reviewed .45 input trim. The fixed
boost and capture operate after each lane's voices are summed, so chord notes
interact inside the nonlinear response. Existing tone/EQ/parallel controls follow
normal insert processing. The final native voice/track/master controls remain
independent of amp input. It does not recreate a commercial album or every
physical amp control.

Fixed causal histories and cached zero-input states provide bounded reset/wake
behavior. Histories allocate once during instrument construction; process/reset
allocate nothing. Silent input sleeps the model after a half-second tail. The
existing 16-frame insert delay is retained. Reference-renderer golden tests cover
noise, chords, changing input strengths and silence; extreme/reset/allocation
checks and multi-capture cost are separate tests. Offline performance alone is
not a real-time/orchestra-scale guarantee.
