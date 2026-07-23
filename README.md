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

Copy to the Move (with Schwung installed) and extract into
`/data/UserData/schwung/modules/synth/`, or install through the
Schwung Manager at `http://move.local:7700`.

## License

GPL-3.0, same as the upstream KR-106 project.
