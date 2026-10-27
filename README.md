# Carcosa Sim (VST3)

Unofficial emulation of the DOD *Carcosa* silicon fuzz. It is a behavioural model built from the public
description of the pedal (manual and reviews), **not** a schematic-exact copy, and is not affiliated with DOD or
Harman. The real circuit is not published, so the internals of this model are educated guesses.

## Signal flow
```
in -> Input trim -> Noise Gate -> [DEMHE: adds bass | HALI: cuts bass] -> stage 1 (BEFORE = gain)
   -> stage 2 (AFTER = bias: gating, sag, splatter) -> mid/treble emphasis -> HI-CUT -> OUTPUT -> Trim -> out
```

## Controls
| Control | What it does |
|---|---|
| Before | Amount of fuzz (stage 1 gain, 0 to +42 dB). Low = warm and thick, cleans up with the guitar volume. High = broken |
| After | Bias of the second stage. Left = loud, woody. Right = starved: gated, splattery, sagging, with broken vowel-like tones |
| Output | Level. With Before and After at zero the pedal works as a clean boost |
| Hi-Cut | Low-pass on the top end, from open (left) to dark (right). Raise it for bright pickups or when After is high |
| Mode | **Demhe** adds bass and low-mid boost (for clean amps). **Hali** cuts bass (for dirty amps, avoids a woolly low end). Default is Demhe, the better choice into a clean amp or cab simulator |
| ON | Footswitch, click-free crossfade. When off the pedal is bypassed and the gate is off too |
| Input / Trim | Host-side trims, -24 to +24 dB and -24 to +12 dB. Input sits before the gate |
| Noise Gate | Input gate, -80 (off) to -30 dBFS, default -60. Removes interface and pickup hiss that the fuzz would otherwise amplify. 1 ms lookahead, 50 ms hold, soft release |

Fuzz reacts strongly to the guitar's volume knob and pickups: turn the guitar volume down to clean it up.
DSP runs at 4x oversampling (the plugin reports the oversampler and gate latency to the host).

## Build
Requires CMake 3.22+ and a C++17 compiler. JUCE 8.0.15 is downloaded automatically.
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Result: `build/CarcosaSim_artefacts/Release/VST3/Carcosa Sim.vst3`

Install:
- Windows: copy the `.vst3` folder to `C:\Program Files\Common Files\VST3\`
- macOS: `~/Library/Audio/Plug-Ins/VST3/` (unsigned builds may need `xattr -cr` on the bundle)
- Linux: `~/.vst3/`

No compiler? Push this folder to a GitHub repository: the included workflow
(`.github/workflows/build.yml`) builds Windows, macOS and Linux VST3s and attaches them as downloadable artifacts.

Optional offline tests: `cmake -S . -B build -DCARCOSA_BUILD_TESTS=ON && cmake --build build --target dsp_test gate_test && ./build/dsp_test && ./build/gate_test`

JUCE is used under its AGPLv3/commercial licensing: if you distribute the plugin, the AGPL terms apply.
