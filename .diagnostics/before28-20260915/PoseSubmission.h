#pragma once
#include <cstdint>

namespace bvr {
inline int SelectTrackingMode(bool compositorRotation, bool positionalTracking,
                              bool automaticTurning, bool directPoseTurning) {
    if (!compositorRotation) return positionalTracking ? 2 : 1;
    if (!positionalTracking) return 0;
    return automaticTurning && directPoseTurning ? 2 : 3;
}

// Decisions must describe the image already captured, not the mode selected
// by keys at Present. Stale alternating-eye images cannot use Submit_Default.
struct CapturedPoseStamp {
    std::uint64_t sample = 0;
    bool valid = false;
    bool exactWaitPose = false;
};

inline bool UseOrdinaryPoseSubmit(const CapturedPoseStamp& captured,
                                  std::uint64_t currentSample, bool currentValid) {
    return captured.valid && currentValid && captured.exactWaitPose &&
           captured.sample != 0 && captured.sample == currentSample;
}
}
