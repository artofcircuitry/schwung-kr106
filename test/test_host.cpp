/* Minimal Schwung host harness: drives the kr106 module through
 * plugin_api_v2 exactly as the Move host would. */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include "plugin_api_v1.h"

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

static void hostLog(const char *msg) { printf("[host] %s\n", msg); }

int main()
{
    host_api_v1_t host = {};
    host.api_version = 1;
    host.sample_rate = MOVE_SAMPLE_RATE;
    host.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    host.log = hostLog;

    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    if (!api || api->api_version != 2) { printf("FAIL: init\n"); return 1; }

    void *inst = api->create_instance("/tmp/kr106", nullptr);
    if (!inst) { printf("FAIL: create_instance\n"); return 1; }

    char buf[64];
    api->get_param(inst, "preset", buf, sizeof buf);
    printf("preset=%s ", buf);
    api->get_param(inst, "preset_name", buf, sizeof buf);
    printf("name=\"%s\" ", buf);
    api->get_param(inst, "preset_count", buf, sizeof buf);
    printf("count=%s\n", buf);

    /* note on: C3 chord */
    const uint8_t on1[3] = {0x90, 48, 100};
    const uint8_t on2[3] = {0x90, 60, 100};
    const uint8_t on3[3] = {0x90, 64, 100};
    api->on_midi(inst, on1, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->on_midi(inst, on2, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->on_midi(inst, on3, 3, MOVE_MIDI_SOURCE_INTERNAL);

    int16_t out[MOVE_FRAMES_PER_BLOCK * 2];
    double sumAbs = 0; int16_t peak = 0;
    for (int b = 0; b < 344; b++)  /* ~1 second */
    {
        api->render_block(inst, out, MOVE_FRAMES_PER_BLOCK);
        for (int i = 0; i < MOVE_FRAMES_PER_BLOCK * 2; i++)
        {
            int16_t v = out[i];
            sumAbs += v < 0 ? -v : v;
            if (v > peak) peak = v;
        }
    }
    printf("1s render: peak=%d meanAbs=%.1f\n", peak, sumAbs / (344.0 * 256));
    if (peak < 100) { printf("FAIL: output silent\n"); return 1; }

    /* param round-trip */
    api->set_param(inst, "vcf_freq", "0.8");
    api->get_param(inst, "vcf_freq", buf, sizeof buf);
    if (strcmp(buf, "0.8") != 0) { printf("FAIL: param round-trip got %s\n", buf); return 1; }

    /* preset switch + oversample switch + render must not crash */
    api->set_param(inst, "preset", "0");
    api->set_param(inst, "oversample", "2");   /* index 2 -> 4x */
    const uint8_t off1[3] = {0x80, 48, 0};
    api->on_midi(inst, off1, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int b = 0; b < 100; b++)
        api->render_block(inst, out, MOVE_FRAMES_PER_BLOCK);

    /* all notes off -> should decay toward silence */
    api->set_param(inst, "all_notes_off", "1");
    for (int b = 0; b < 344; b++)
        api->render_block(inst, out, MOVE_FRAMES_PER_BLOCK);
    int16_t tailPeak = 0;
    api->render_block(inst, out, MOVE_FRAMES_PER_BLOCK);
    for (int i = 0; i < MOVE_FRAMES_PER_BLOCK * 2; i++)
        if (out[i] > tailPeak) tailPeak = out[i];
    printf("tail after all_notes_off: peak=%d\n", tailPeak);

    api->destroy_instance(inst);
    printf("PASS\n");
    return 0;
}
