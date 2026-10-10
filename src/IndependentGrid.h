#pragma once
#include "FrameLoopWorker.h"
#include <openvr.h>
namespace bvr {
class IndependentGrid {
public:
    bool Start(vr::IVRSystem* system, vr::IVRCompositor* compositor);
    bool OwnsFrames() const { return worker_.OwnsFrames(); }
    void RequestStop() { worker_.RequestStop(); }
    void Recenter() { recenter_.store(true); }
    void SetGameParked(bool parked) { gameParked_.store(parked); }
    bool Poll() { return worker_.Poll(); }
    void StopAndJoin() { worker_.StopAndJoin(); }
private:
    static void Run(void* self, const std::atomic<bool>& stop);
    void Render(const std::atomic<bool>& stop);
    FrameLoopWorker worker_;
    std::atomic<bool> recenter_{false};
    std::atomic<bool> gameParked_{false};
    vr::IVRSystem* system_ = nullptr;
    vr::IVRCompositor* compositor_ = nullptr;
};
IndependentGrid& IndependentVRGrid();
}
