#include "PoseSubmission.h"
#include <cstdio>
using namespace bvr;

int main() {
    unsigned cases = 0;
    for (int compositor = 0; compositor < 2; ++compositor)
    for (int positional = 0; positional < 2; ++positional)
    for (int turning = 0; turning < 2; ++turning)
    for (int direct = 0; direct < 2; ++direct) {
        const int mode = SelectTrackingMode(compositor != 0, positional != 0,
                                            turning != 0, direct != 0);
        const int expected = !compositor ? (positional ? 2 : 1) :
            !positional ? 0 : (turning && direct ? 2 : 3);
        if (mode != expected) return 1;
        ++cases;
    }
    for (int valid = 0; valid < 2; ++valid)
    for (int currentValid = 0; currentValid < 2; ++currentValid)
    for (int exact = 0; exact < 2; ++exact)
    for (unsigned sample = 0; sample <= 3; ++sample)
    for (unsigned current = 0; current <= 3; ++current) {
        const CapturedPoseStamp stamp{sample, valid != 0, exact != 0};
        const bool expected = valid && currentValid && exact && sample && sample == current;
        if (UseOrdinaryPoseSubmit(stamp, current, currentValid != 0) != expected) return 2;
        if (UseOrdinaryPoseSubmit(stamp, current, currentValid != 0, false)) return 15;
        ++cases;
    }
    // F6 enables direct mode AFTER the old fixed-direction eye pair is captured.
    // The old pair still needs its explicit synthetic pose.
    CapturedPoseStamp left{10, true, false}, right = left;
    int liveMode = SelectTrackingMode(true, true, true, true);
    if (liveMode != 2 || UseOrdinaryPoseSubmit(left, 10, true)) return 3;
    left = right = {11, true, true};
    if (!UseOrdinaryPoseSubmit(left, 11, true) || !UseOrdinaryPoseSubmit(right, 11, true)) return 4;
    // F6 off / F7 baseline / F8 recenter at Present must not relabel this pair.
    liveMode = SelectTrackingMode(true, true, false, true);
    if (liveMode != 3 || !UseOrdinaryPoseSubmit(left, 11, true)) return 5;
    left = right = {12, true, false};
    if (UseOrdinaryPoseSubmit(left, 12, true)) return 6;
    // Alternating fallback or a failed wait cannot implicitly submit stale eyes.
    left = {14, true, true}; right = {13, true, true};
    if (!UseOrdinaryPoseSubmit(left, 14, true) || UseOrdinaryPoseSubmit(right, 14, true)) return 7;
    if (UseOrdinaryPoseSubmit(left, 15, false) || UseOrdinaryPoseSubmit(left, 16, true)) return 8;
    EarlySubmissionGate gate;
    if (gate.ConsumeAtPresent()) return 9;
    gate.MarkSubmitted();
    // Mode/timing changes after the pair must not cause a duplicate at Present.
    if (!gate.ConsumeAtPresent() || gate.ConsumeAtPresent()) return 10;
    gate.MarkSubmitted(); gate.MarkSubmitted();
    if (!gate.ConsumeAtPresent() || gate.ConsumeAtPresent()) return 11;
    gate.MarkSubmitted(); gate.Reset();
    if (gate.ConsumeAtPresent()) return 12;

    // Model the outer + nested D3D9 Present wrappers across early/late timing,
    // early capture failure (fallback), and mode changes after early capture.
    unsigned submits = 0, handoffs = 0, desktopCalls = 0;
    bool insidePresent = false;
    for (unsigned frame = 0; frame < 120; ++frame) {
        const bool earlyEnabled = frame % 3 != 0;
        const bool completePair = frame % 7 != 0;
        if (earlyEnabled && completePair) {
            ++submits; ++handoffs; gate.MarkSubmitted();
        }
        auto nestedPresent = [&] {
            if (insidePresent) { ++desktopCalls; return; }
            // This branch must not run from the nested wrapper.
            ++submits; ++handoffs;
        };
        insidePresent = true;
        const bool alreadySubmitted = gate.ConsumeAtPresent();
        if (!alreadySubmitted) ++submits;
        nestedPresent();
        if (!alreadySubmitted) ++handoffs;
        insidePresent = false;
        if (gate.ConsumeAtPresent()) return 13;
    }
    if (submits != 120 || handoffs != 120 || desktopCalls != 120) return 14;
    std::puts("early submission gate: early/late/fallback/reset/nested-Present tests passed (one submit + handoff per pair)");
    std::printf("pose submission: %u policy combinations, explicit-native override, and captured-frame transition/stale-pose tests passed\n", cases);
    return 0;
}
