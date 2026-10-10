#include "EffectStreamReplay.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace bvr;
static void Require(bool value, const char* message) {
    if (!value) { std::printf("effect replay failed: %s\n", message); std::exit(1); }
}
struct Stream {
    uint32_t before = 0xAABBCCDD;
    uint32_t words[8]{};
    uint32_t after = 0x11223344;
};
static void Clear(Stream& s) { // Steam's 0xC5F7A0, including the flags default.
    std::memset(s.words, 0, 7 * sizeof(uint32_t)); s.words[7] = 0x1000;
}
static void Populate(Stream& s, uint32_t frame, uint32_t index) {
    for (uint32_t j = 0; j < 8; ++j) s.words[j] = (frame + 1) * 1000 + index * 10 + j;
}
int main() {
    EffectStreamReplay<8> replay;
    Stream streams[9];
    for (uint32_t frame = 0; frame < 100; ++frame) {
        uint32_t expected[8][8]{};
        replay.BeginFrame();
        for (uint32_t i = 0; i < 8; ++i) {
            Populate(streams[i], frame, i);
            std::memcpy(expected[i], streams[i].words, sizeof(expected[i]));
            Require(replay.Remember(streams[i].words), "capture left stream");
            Clear(streams[i]);
            Require(streams[i].words[0] == 0 && streams[i].words[3] == 0,
                    "left pass consumes buffer and vertex count normally");
            Require(replay.Remember(streams[i].words), "repeated pass deduplicates at capacity");
            Clear(streams[i]);
        }
        Populate(streams[8], frame, 8);
        Require(!replay.Remember(streams[8].words), "bounded overflow");
        Clear(streams[8]);
        Require(replay.RestoreForRightEye() == 8, "exactly eight distinct streams restored");
        for (uint32_t i = 0; i < 8; ++i) {
            Require(std::memcmp(expected[i], streams[i].words, sizeof(expected[i])) == 0,
                    "right eye gets original buffer, offsets, counts, stride and flags");
            Require(streams[i].before == 0xAABBCCDD && streams[i].after == 0x11223344,
                    "adjacent effect state untouched");
            Require(replay.Remember(streams[i].words), "right eye capture is disabled");
            Clear(streams[i]);
        }
        Require(streams[8].words[0] == 0, "overflow stream retains original cleanup behavior");
        Require(replay.RestoreForRightEye() == 0, "cannot resurrect right-eye consumed data");
        Require(streams[0].words[0] == 0, "right cleanup persists to next frame");
    }
    replay.BeginFrame();
    Populate(streams[0], 100, 0); replay.Remember(streams[0].words);
    replay.Reset(); Clear(streams[0]);
    Require(replay.RestoreForRightEye() == 0 && streams[0].words[0] == 0,
            "device reset cancels stale snapshots");
    replay.BeginFrame(); replay.Remember(streams[0].words);
    Populate(streams[0], 101, 0); replay.Remember(streams[0].words);
    replay.RestoreForRightEye();
    Require(streams[0].words[0] == 0 && streams[0].words[7] == 0x1000,
            "first observed descriptor wins even when initially empty");
    replay.BeginFrame(); replay.Remember(streams[0].words);
    replay.BeginFrame(); Populate(streams[0], 102, 0);
    Require(replay.RestoreForRightEye() == 0 && streams[0].words[0] == 103000,
            "new frame discards abandoned previous snapshots");
    std::puts("effect replay: 100 stereo frames, repeated passes, full descriptor restore, overflow and reset passed");
}
