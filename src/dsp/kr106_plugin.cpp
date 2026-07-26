/*
 * KR-106 Schwung module — plugin_api_v2 adapter around the Ultramaster
 * KR-106 DSP core (Juno-6/60/106 emulation, GPL-3.0).
 *
 * The DSP core is header-only and JUCE-free (see src/dsp/kr106/, synced
 * from https://github.com/kayrockscreenprinting/ultramaster_kr106).
 * This file maps Schwung's string-keyed params and raw MIDI onto
 * KR106DSP<float>, and converts float output to int16 interleaved.
 *
 * GPL-3.0 License
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <new>

#include "plugin_api_v1.h"

#include "kr106/KR106_DSP.h"
#include "kr106/KR106_DSP_SetParam.h"
#include "kr106/KR106_Presets_JUCE.h"
#include "kr106_ui_hierarchy.h"

/* EParams — must match Source/DSP/KR106_DSP_SetParam.h enum order */
enum EParams
{
  kBenderDco = 0, kBenderVcf, kArpRate, kLfoRate, kLfoDelay,
  kDcoLfo, kDcoPwm, kDcoSub, kDcoNoise, kHpfFreq,
  kVcfFreq, kVcfRes, kVcfEnv, kVcfLfo, kVcfKbd,
  kVcaLevel, kEnvA, kEnvD, kEnvS, kEnvR,
  kTranspose, kHold, kArpeggio, kDcoPulse, kDcoSaw, kDcoSubSw,
  kChorusOff, kChorusI, kChorusII,
  kOctTranspose, kArpMode, kArpRange, kLfoMode, kPwmMode,
  kVcfEnvInv, kVcaMode,
  kBender, kTuning, kPower,
  kPortaMode, kPortaRate,
  kTransposeOffset, kBenderLfo,
  kAdsrMode,
  kMasterVol,
  kNumParams
};

/* ------------------------------------------------------------------ */
/* String-keyed parameter table                                        */
/* ------------------------------------------------------------------ */

struct ParamDef {
    const char *key;
    int param;      /* EParams index, or -1 for adapter-level params */
};

static const ParamDef kParamTable[] = {
    /* sliders, 0.0-1.0 */
    {"lfo_rate",     kLfoRate},
    {"lfo_delay",    kLfoDelay},
    {"dco_lfo",      kDcoLfo},
    {"dco_pwm",      kDcoPwm},
    {"dco_sub",      kDcoSub},
    {"dco_noise",    kDcoNoise},
    {"vcf_freq",     kVcfFreq},
    {"vcf_res",      kVcfRes},
    {"vcf_env",      kVcfEnv},
    {"vcf_lfo",      kVcfLfo},
    {"vcf_kbd",      kVcfKbd},
    {"vca_level",    kVcaLevel},
    {"attack",       kEnvA},
    {"decay",        kEnvD},
    {"sustain",      kEnvS},
    {"release",      kEnvR},
    {"porta_rate",   kPortaRate},
    {"bender_dco",   kBenderDco},
    {"bender_vcf",   kBenderVcf},
    {"bender_lfo",   kBenderLfo},
    /* switches / ints, passed through as integers */
    {"hpf_freq",     kHpfFreq},
    {"dco_pulse",    kDcoPulse},
    {"dco_saw",      kDcoSaw},
    {"dco_sub_sw",   kDcoSubSw},
    {"pwm_mode",     kPwmMode},
    {"lfo_mode",     kLfoMode},
    {"vcf_env_inv",  kVcfEnvInv},
    {"vca_mode",     kVcaMode},
    {"adsr_mode",    kAdsrMode},
    {"oct_range",    kOctTranspose},
    {"porta_mode",   kPortaMode},
    {"hold",         kHold},
};
static const int kParamTableSize = (int)(sizeof(kParamTable) / sizeof(kParamTable[0]));

static int lookupParam(const char *key)
{
    for (int i = 0; i < kParamTableSize; i++)
        if (strcmp(kParamTable[i].key, key) == 0)
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Instance                                                            */
/* ------------------------------------------------------------------ */

static const host_api_v1_t *g_host = nullptr;

struct Kr106Instance {
    KR106DSP<float> *dsp;

    /* deinterleave buffers for ProcessBlock */
    float bufL[MOVE_FRAMES_PER_BLOCK];
    float bufR[MOVE_FRAMES_PER_BLOCK];

    /* shadow values so get_param can report current state */
    double shadow[kNumParams];
    int chorusMode;        /* 0=off 1=I 2=II (kChorusI/kChorusII behind it) */
    int preset;            /* 0..255 factory index */
    int polyphony;         /* 6/8/10 */
    int oversample;        /* 1/2/4 */
    int oscMode;           /* 0=wavetable 1=polyBLEP */
    int ignoreVelocity;
    int octaveTranspose;   /* -2..+2, applied to incoming MIDI notes */
    float volume;          /* 0..1 UI value; squared taper -> mMasterVol */
};

static void dspSet(Kr106Instance *inst, int param, double value)
{
    if (param < 0 || param >= kNumParams) return;
    inst->shadow[param] = value;
    inst->dsp->SetParam(param, value);
}

static void setChorusMode(Kr106Instance *inst, int mode)
{
    inst->chorusMode = mode;
    dspSet(inst, kChorusI,  mode == 1 ? 1.0 : 0.0);
    dspSet(inst, kChorusII, mode == 2 ? 1.0 : 0.0);
}

static bool isSliderParam(int i)
{
    return (i >= 0 && i <= 19) || i == kPortaRate;
}

/* Factory preset load — mirrors tools/wasm/kr106_wasm.cpp: live
 * performance params (bender, arp, hold, transpose, porta, power,
 * master vol) are not stored in presets and are left untouched. */
static void loadPreset(Kr106Instance *inst, int presetIdx)
{
    if (presetIdx < 0 || presetIdx >= kNumFactoryPresets) return;
    inst->preset = presetIdx;
    const auto &p = kFactoryPresets[presetIdx];

    for (int i = 0; i <= 19; i++)
    {
        if (i == kBenderDco || i == kBenderVcf || i == kArpRate) continue;
        if (i == kHpfFreq) { dspSet(inst, i, (double)p.values[i]); continue; } /* 0-3 int */
        dspSet(inst, i, p.values[i] / 127.0);
    }
    for (int i = 20; i <= 39; i++)
    {
        if (i == kTranspose || i == kHold || i == kArpeggio ||
            i == kArpMode || i == kArpRange || i == kBender ||
            i == kTuning || i == kPower || i == kPortaMode) continue;
        if (i == kChorusOff) continue; /* handled via chorusMode below */
        dspSet(inst, i, (double)p.values[i]);
    }
    dspSet(inst, kAdsrMode, (double)p.values[43]);

    /* chorus radio group: derive 0/1/2 from stored booleans */
    int mode = p.values[kChorusI] ? 1 : (p.values[kChorusII] ? 2 : 0);
    setChorusMode(inst, mode);
}

static void applyEngineSettings(Kr106Instance *inst)
{
    inst->dsp->SetActiveVoices(inst->polyphony);
    inst->dsp->SetOversample(inst->oversample);
    inst->dsp->mIgnoreVelocity = (inst->ignoreVelocity != 0);
    int oscMode = inst->oscMode;
    inst->dsp->ForEachVoice([oscMode](kr106::Voice<float> &v) { v.mOscMode = oscMode; });
}

/* ------------------------------------------------------------------ */
/* plugin_api_v2 callbacks                                             */
/* ------------------------------------------------------------------ */

static void *v2_create_instance(const char *module_dir, const char *json_defaults)
{
    (void)module_dir; (void)json_defaults;

    Kr106Instance *inst = new (std::nothrow) Kr106Instance();
    if (!inst) return nullptr;

    inst->dsp = new (std::nothrow) KR106DSP<float>(6);
    if (!inst->dsp) { delete inst; return nullptr; }

    inst->dsp->Reset(MOVE_SAMPLE_RATE, MOVE_FRAMES_PER_BLOCK);

    memset(inst->shadow, 0, sizeof(inst->shadow));
    inst->preset = 128;          /* first Juno-106 factory patch */
    inst->polyphony = 6;
    inst->oversample = 2;        /* embedded profile: ~22% of one A72 core */
    inst->oscMode = 1;           /* polyBLEP */
    inst->ignoreVelocity = 1;    /* hardware Juno has no velocity */
    inst->octaveTranspose = 0;
    inst->chorusMode = 0;

    /* defaults matching the plugin / WASM build.
     * Master volume is a plain member with a squared taper — SetParam
     * has no kMasterVol case (matches PluginProcessor/WASM behavior). */
    dspSet(inst, kPower, 1.0);
    dspSet(inst, kAdsrMode, 1.0);        /* J106 mode */
    inst->volume = 0.5f;
    inst->dsp->mMasterVol = inst->volume * inst->volume;
    inst->dsp->mMasterVolSmooth = inst->dsp->mMasterVol;
    dspSet(inst, kPortaMode, 1.0);       /* Poly I (hardware default) */
    applyEngineSettings(inst);
    loadPreset(inst, inst->preset);

    if (g_host && g_host->log) g_host->log("kr106: instance created");
    return inst;
}

static void v2_destroy_instance(void *instance)
{
    Kr106Instance *inst = (Kr106Instance *)instance;
    if (!inst) return;
    delete inst->dsp;
    delete inst;
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source)
{
    (void)source;
    Kr106Instance *inst = (Kr106Instance *)instance;
    if (!inst || len < 2) return;

    const uint8_t status = msg[0] & 0xF0;
    const int transpose = inst->octaveTranspose * 12;

    switch (status)
    {
    case 0x90: /* note on (vel 0 == off) */
    {
        if (len < 3) return;
        int note = msg[1] + transpose;
        if (note < 0 || note > 127) return;
        if (msg[2] > 0) inst->dsp->NoteOn(note, msg[2]);
        else            inst->dsp->NoteOff(note);
        break;
    }
    case 0x80: /* note off */
    {
        int note = msg[1] + transpose;
        if (note < 0 || note > 127) return;
        inst->dsp->NoteOff(note);
        break;
    }
    case 0xB0: /* control change */
    {
        if (len < 3) return;
        const int cc = msg[1], val = msg[2];
        if (cc == 1)                      /* mod wheel -> LFO trigger */
            inst->dsp->ControlChange(1, val / 127.f);
        else if (cc == 64)                /* sustain pedal -> Hold */
            dspSet(inst, kHold, val >= 64 ? 1.0 : 0.0);
        else if (cc == 120 || cc == 123)  /* all sound/notes off */
            inst->dsp->AllNotesOff();
        break;
    }
    case 0xE0: /* pitch bend -> raw -1..1 */
    {
        if (len < 3) return;
        int pb = ((int)msg[2] << 7) | msg[1];
        dspSet(inst, kBender, (pb - 8192) / 8192.0);
        break;
    }
    default:
        break;
    }
}

static void v2_set_param(void *instance, const char *key, const char *val)
{
    Kr106Instance *inst = (Kr106Instance *)instance;
    if (!inst || !key || !val) return;

    if (strcmp(key, "preset") == 0)          { loadPreset(inst, atoi(val)); return; }
    if (strcmp(key, "all_notes_off") == 0)   { inst->dsp->AllNotesOff(); return; }
    if (strcmp(key, "volume") == 0)
    {
        float v = (float)atof(val);
        if (v < 0.f) v = 0.f; if (v > 1.f) v = 1.f;
        inst->volume = v;
        inst->dsp->mMasterVol = v * v;   /* squared taper, smoothed in ProcessBlock */
        return;
    }
    if (strcmp(key, "chorus") == 0)          { setChorusMode(inst, atoi(val)); return; }
    if (strcmp(key, "octave_transpose") == 0)
    {
        int v = atoi(val);
        if (v < -2) v = -2; if (v > 2) v = 2;
        inst->octaveTranspose = v;
        return;
    }
    if (strcmp(key, "polyphony") == 0)
    {
        int v = atoi(val);
        if (v < 1) v = 1; if (v > 10) v = 10;
        inst->polyphony = v;
        inst->dsp->SetActiveVoices(v);
        return;
    }
    if (strcmp(key, "oversample") == 0)
    {
        /* enum option index: 0 -> 1x, 1 -> 2x, 2 -> 4x */
        int idx = atoi(val);
        if (idx < 0 || idx > 2) return;
        inst->oversample = 1 << idx;
        inst->dsp->SetOversample(inst->oversample);
        return;
    }
    if (strcmp(key, "osc_mode") == 0)
    {
        inst->oscMode = atoi(val) ? 1 : 0;
        applyEngineSettings(inst);
        return;
    }
    if (strcmp(key, "ignore_velocity") == 0)
    {
        inst->ignoreVelocity = atoi(val) ? 1 : 0;
        inst->dsp->mIgnoreVelocity = (inst->ignoreVelocity != 0);
        return;
    }

    int t = lookupParam(key);
    if (t >= 0) dspSet(inst, kParamTable[t].param, atof(val));
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    Kr106Instance *inst = (Kr106Instance *)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "preset") == 0)
        return snprintf(buf, buf_len, "%d", inst->preset);
    if (strcmp(key, "preset_count") == 0)
        return snprintf(buf, buf_len, "%d", kNumFactoryPresets);
    if (strcmp(key, "preset_name") == 0)
        return snprintf(buf, buf_len, "%s", kFactoryPresets[inst->preset].name);
    if (strcmp(key, "volume") == 0)
        return snprintf(buf, buf_len, "%g", (double)inst->volume);
    if (strcmp(key, "chorus") == 0)
        return snprintf(buf, buf_len, "%d", inst->chorusMode);
    if (strcmp(key, "octave_transpose") == 0)
        return snprintf(buf, buf_len, "%d", inst->octaveTranspose);
    if (strcmp(key, "polyphony") == 0)
        return snprintf(buf, buf_len, "%d", inst->polyphony);
    if (strcmp(key, "oversample") == 0)
        return snprintf(buf, buf_len, "%d",
                        inst->oversample == 4 ? 2 : (inst->oversample == 2 ? 1 : 0));
    if (strcmp(key, "osc_mode") == 0)
        return snprintf(buf, buf_len, "%d", inst->oscMode);
    if (strcmp(key, "ignore_velocity") == 0)
        return snprintf(buf, buf_len, "%d", inst->ignoreVelocity);

    /* Shadow UI parameter hierarchy + flat param metadata — the Schwung host
     * queries these FROM THE DSP (module.json's copy is only used for the
     * chain_params cache); without them the chain UI falls back to the bare
     * preset browser and the encoders stay dead. Generated from module.json
     * by scripts/gen_ui_hierarchy.py (single source of truth). */
    if (strcmp(key, "ui_hierarchy") == 0) {
        int len = (int)strlen(kUiHierarchyJson);
        if (len < buf_len) { memcpy(buf, kUiHierarchyJson, (size_t)len + 1); return len; }
        return -1;
    }
    if (strcmp(key, "chain_params") == 0) {
        int len = (int)strlen(kChainParamsJson);
        if (len < buf_len) { memcpy(buf, kChainParamsJson, (size_t)len + 1); return len; }
        return -1;
    }

    int t = lookupParam(key);
    if (t >= 0)
        return snprintf(buf, buf_len, "%g", inst->shadow[kParamTable[t].param]);
    return -1;
}

static int v2_get_error(void *instance, char *buf, int buf_len)
{
    (void)instance; (void)buf; (void)buf_len;
    return 0;
}

static void v2_render_block(void *instance, int16_t *out_interleaved_lr, int frames)
{
    Kr106Instance *inst = (Kr106Instance *)instance;
    if (!inst || frames > MOVE_FRAMES_PER_BLOCK)
    {
        if (out_interleaved_lr && frames > 0)
            memset(out_interleaved_lr, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }

    memset(inst->bufL, 0, (size_t)frames * sizeof(float));
    memset(inst->bufR, 0, (size_t)frames * sizeof(float));
    float *outputs[2] = { inst->bufL, inst->bufR };
    inst->dsp->ProcessBlock(nullptr, outputs, 2, frames);

    /* Headroom trim, then soft clip (tanh) and convert; hard bound as a
     * safety net. Survey of all 256 factory presets (4-note chord, vel 127)
     * showed pre-trim float peaks of median 1.44 / max 6.8 — without the
     * trim, 217/256 presets drove the tanh into audible saturation on the
     * Move. 0.25 puts the median worst-case peak at ~-9 dBFS; only the few
     * hottest presets ever brush the knee. */
    static constexpr float kOutputHeadroom = 0.25f;
    for (int i = 0; i < frames; i++)
    {
        int32_t l = (int32_t)(tanhf(inst->bufL[i] * kOutputHeadroom) * 32767.f);
        int32_t r = (int32_t)(tanhf(inst->bufR[i] * kOutputHeadroom) * 32767.f);
        if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        out_interleaved_lr[i * 2]     = (int16_t)l;
        out_interleaved_lr[i * 2 + 1] = (int16_t)r;
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static plugin_api_v2_t g_plugin_api_v2;

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    g_plugin_api_v2.api_version      = MOVE_PLUGIN_API_VERSION_2;
    g_plugin_api_v2.create_instance  = v2_create_instance;
    g_plugin_api_v2.destroy_instance = v2_destroy_instance;
    g_plugin_api_v2.on_midi          = v2_on_midi;
    g_plugin_api_v2.set_param        = v2_set_param;
    g_plugin_api_v2.get_param        = v2_get_param;
    g_plugin_api_v2.get_error        = v2_get_error;
    g_plugin_api_v2.render_block     = v2_render_block;
    return &g_plugin_api_v2;
}
