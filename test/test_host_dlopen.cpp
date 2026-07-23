/* dlopen-based Schwung host harness: loads the built dsp.so exactly as
 * the Move host would, runs functional checks, then times render_block. */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <dlfcn.h>
#include "plugin_api_v1.h"

static void hostLog(const char *msg) { printf("[host] %s\n", msg); }

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./dsp.so";
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { printf("FAIL: dlopen: %s\n", dlerror()); return 1; }

    auto initFn = (move_plugin_init_v2_fn)dlsym(lib, MOVE_PLUGIN_INIT_V2_SYMBOL);
    if (!initFn) { printf("FAIL: dlsym: %s\n", dlerror()); return 1; }

    host_api_v1_t host = {};
    host.api_version = 1;
    host.sample_rate = MOVE_SAMPLE_RATE;
    host.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    host.log = hostLog;

    plugin_api_v2_t *api = initFn(&host);
    if (!api || api->api_version != 2) { printf("FAIL: init\n"); return 1; }

    void *inst = api->create_instance("/tmp/kr106", nullptr);
    if (!inst) { printf("FAIL: create_instance\n"); return 1; }

    char buf[64];
    api->get_param(inst, "preset_name", buf, sizeof buf);
    printf("preset: %s\n", buf);

    const uint8_t chord[3][3] = {{0x90,48,100},{0x90,60,100},{0x90,64,100}};
    for (auto &m : chord) api->on_midi(inst, m, 3, MOVE_MIDI_SOURCE_INTERNAL);

    int16_t out[MOVE_FRAMES_PER_BLOCK * 2];
    int16_t peak = 0;
    for (int b = 0; b < 344; b++)
    {
        api->render_block(inst, out, MOVE_FRAMES_PER_BLOCK);
        for (int i = 0; i < MOVE_FRAMES_PER_BLOCK * 2; i++)
            if (out[i] > peak) peak = out[i];
    }
    printf("1s render: peak=%d %s\n", peak, peak > 100 ? "OK" : "FAIL(silent)");
    if (peak <= 100) return 1;

    /* CPU timing: 10 seconds of audio through the module render path,
     * held chord, per-oversample setting */
    const char *osLabel[3] = {"1x", "2x", "4x"};
    for (int osIdx = 0; osIdx < 3; osIdx++)
    {
        char v[4]; snprintf(v, sizeof v, "%d", osIdx);
        api->set_param(inst, "oversample", v);
        const int blocks = 10 * MOVE_SAMPLE_RATE / MOVE_FRAMES_PER_BLOCK;
        /* warmup */
        for (int b = 0; b < 344; b++) api->render_block(inst, out, MOVE_FRAMES_PER_BLOCK);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int b = 0; b < blocks; b++)
            api->render_block(inst, out, MOVE_FRAMES_PER_BLOCK);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        printf("oversample %s: %7.1f ms for 10s audio -> %5.1f%% CPU\n",
               osLabel[osIdx], ms, ms / 100.0);
    }

    api->destroy_instance(inst);
    dlclose(lib);
    printf("PASS\n");
    return 0;
}
