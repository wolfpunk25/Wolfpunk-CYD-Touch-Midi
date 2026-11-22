# WOLFPUNK CYD Touch MIDI

A Bluetooth MIDI controller for the ESP32 "Cheap Yellow Display" (320x240 touch), advertising as **WOLFPUNK**.
Based on [NickCulbertson/CYD-MIDI-Controller](https://github.com/NickCulbertson/CYD-MIDI-Controller), with additions.

## Modes
KEYS, BEATS (sequencer), ZEN (bouncing balls, up to 16 with decay), DROP, RAND (random/walk generator with gate),
STRUM (swipe across scale-tuned strings), ARP, GRID (4ths grid with scale/key highlighting), CHORD, PATCH (patch list).

## Additions over upstream
Power switch/sleep, battery %, RGB LED per mode, connection status, PATCH browser (sends Program Change),
ZEN decay, GRID scale/key selector, RAND walk mode and gate length, STRUM.

## Build
Arduino CLI, board `esp32:esp32:esp32`, libraries TFT_eSPI and XPT2046_Touchscreen (`User_Setup.h` included):

    arduino-cli compile --fqbn esp32:esp32:esp32 CYDMidi

## Patch names
`CYDMidi/patch_names.h` is generated from the synth's patch library:

    python3 tools/gen_patch_names.py

Program number = patch id number minus 1 (p1-p128 only). Re-run and re-flash after patches change.

`lfo_mode.h` and `xy_pad_mode.h` are retired modes kept for reference; they are not compiled in.
