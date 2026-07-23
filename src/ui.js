/*
 * KR-106 UI for Schwung
 *
 * Uses the shared sound generator UI base for preset browsing;
 * parameter editing goes through the ui_hierarchy in module.json.
 *
 * GPL-3.0 License
 */

/* Shared utilities - absolute path for module location independence */
import { createSoundGeneratorUI } from '/data/UserData/schwung/shared/sound_generator_ui.mjs';

const ui = createSoundGeneratorUI({
    moduleName: 'KR-106',

    onPresetChange: (preset) => {
        /* Silence held voices so a preset switch doesn't leave a stuck chord */
        host_module_set_param('all_notes_off', '1');
    },

    showPolyphony: true,
    showOctave: true,
});

globalThis.init = ui.init;
globalThis.tick = ui.tick;
globalThis.onMidiMessageInternal = ui.onMidiMessageInternal;
globalThis.onMidiMessageExternal = ui.onMidiMessageExternal;
