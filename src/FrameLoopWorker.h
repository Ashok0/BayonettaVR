#pragma once
#include <windows.h>
#include <process.h>
#include <atomic>
namespace bvr {
// Controller methods run on the game render thread. A stop request does not
// release ownership: Poll must observe actual thread exit before game VR resumes.
class FrameLoopWorker {
public:
    using Run = void (*)(void*, const std::atomic<bool>&);
    bool Start(Run run, void* context) {
        if (thread_) return false;
        stop_.store(false); run_ = run; context_ = context;
        thread_ = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, Entry, this, 0, nullptr));
        return thread_ != nullptr;
    }
    bool OwnsFrames() const { return thread_ != nullptr; }
    void RequestStop() { stop_.store(true); }
    bool Poll() {
        if (!thread_ || WaitForSingleObject(thread_, 0) != WAIT_OBJECT_0) return false;
        CloseHandle(thread_); thread_ = nullptr; return true;
    }
    // Never call under the loader lock (DllMain).
    void StopAndJoin() {
        if (!thread_) return;
        RequestStop(); WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_); thread_ = nullptr;
    }
private:
    static unsigned __stdcall Entry(void* self) {
        auto& worker = *static_cast<FrameLoopWorker*>(self);
        worker.run_(worker.context_, worker.stop_); return 0;
    }
    HANDLE thread_ = nullptr;
    std::atomic<bool> stop_{false};
    Run run_ = nullptr;
    void* context_ = nullptr;
};
}
