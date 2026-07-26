# schwung-kr106

Roland Juno-6/60/106 emulation for the [Ableton Move](https://www.ableton.com/move/)
via [Schwung](https://schwung.dev/), wrapping the JUCE-free DSP core of
[Ultramaster KR-106](https://github.com/kayrockscreenprinting/ultramaster_kr106).

6 voices, TPT ladder filter with OTA saturation, BBD chorus, 256 factory
presets (128 Juno-60 + 128 Juno-106). Runs the embedded profile by default
(2× oversampling, polyBLEP oscillators — ~22% of one Move core at 44.1 kHz;
1×/4× selectable under Engine Settings).

## Layout

- `src/dsp/kr106_plugin.cpp` — Schwung `plugin_api_v2` adapter
- `src/dsp/kr106/` — KR-106 DSP core headers (synced, do not edit here)
- `src/module.json` — manifest + `ui_hierarchy` knob pages
- `src/ui.js` — shared sound-generator UI glue

## Building

```bash
./scripts/sync_dsp.sh    # copy DSP headers from ../ultramaster_kr106 (or $KR106_DIR)
./scripts/build.sh       # cross-compile via Docker (or CROSS_PREFIX=... natively)
```

Output: `dist/kr106-module.tar.gz`.

## Installing

With [Schwung](https://schwung.dev/) installed on your Move:

**Module Store (once cataloged):** Main menu -> Module Store ->
Sound Generators -> KR-106 -> Install.

**Manual:** copy and extract the tarball:

```bash
scp dist/kr106-module.tar.gz ableton@move.local:/tmp/
ssh ableton@move.local 'mkdir -p /data/UserData/schwung/modules/sound_generators/kr106 && \
  tar xzf /tmp/kr106-module.tar.gz -C /data/UserData/schwung/modules/sound_generators/kr106 --strip-components=1'
```

Then add KR-106 as the sound generator in a Schwung chain. After
replacing files on an already-loaded module, re-select it in the chain
(or power-cycle) — the loaded library doesn't refresh on disk changes.

## Controls

Entering the KR-106 device shows the preset browser (jog wheel browses
all 256 presets — 128 Juno-60 + 128 Juno-106) with the 8 encoders on
Volume / Cutoff / Resonance / VCF Env / Attack / Decay / Release /
Chorus. Menu pages below the presets: **DCO**, **VCF/HPF**,
**Envelope/VCA**, **LFO/Chorus**, **Performance** (portamento, bender,
hold, octave), **Arpeggiator** (Up / Up-Down / Down, 1-3 octaves,
synced to the Move's clock or free-running, divisions from 4 beats to
1/32 incl. triplets), and **Engine Settings** (voices, oversampling,
oscillator model, velocity).

## License

GPL-3.0, same as the upstream KR-106 project (see LICENSE and NOTICE).
The Move build uses the JUCE-free DSP core; JUCE is not included.

Roland, Juno, Juno-6, Juno-60, and Juno-106 are trademarks of Roland
Corporation, which is not affiliated with and does not endorse this
project.
