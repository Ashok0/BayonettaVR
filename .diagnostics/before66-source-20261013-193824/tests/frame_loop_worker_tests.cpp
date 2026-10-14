#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "FrameLoopWorker.h"
#include <cstdio>
using namespace bvr;
struct Probe {
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stopSeen = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE allowExit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~Probe() { CloseHandle(entered); CloseHandle(stopSeen); CloseHandle(allowExit); }
};
void HeldWorker(void* pointer, const std::atomic<bool>& stop) {
    auto& probe = *static_cast<Probe*>(pointer);
    SetEvent(probe.entered);
    while (!stop.load()) Sleep(1);
    SetEvent(probe.stopSeen);
    WaitForSingleObject(probe.allowExit, 5000);
}
void FailedInit(void*, const std::atomic<bool>&) { /* Early worker failure. */ }
bool AwaitExit(FrameLoopWorker& worker) {
    const ULONGLONG deadline = GetTickCount64()+3000;
    while (!worker.Poll()) { if (GetTickCount64()>deadline) return false; Sleep(1); }
    return !worker.OwnsFrames();
}
int main() {
    FrameLoopWorker worker;
    if (worker.OwnsFrames() || worker.Poll()) return 1;
    for (int cycle=0; cycle<25; ++cycle) {
        Probe probe;
        if (!worker.Start(HeldWorker, &probe) || !worker.OwnsFrames()) return 2;
        if (WaitForSingleObject(probe.entered, 3000)!=WAIT_OBJECT_0) return 3;
        if (worker.Start(FailedInit, nullptr)) return 4;
        worker.RequestStop();
        if (WaitForSingleObject(probe.stopSeen, 3000)!=WAIT_OBJECT_0) return 5;
        // The worker acknowledged stop but is still using its resources.
        // Returning ownership here would race WaitGetPoses/Submit.
        if (worker.Poll() || !worker.OwnsFrames()) return 6;
        SetEvent(probe.allowExit);
        if (!AwaitExit(worker)) return 7;
        if (!worker.Start(FailedInit, nullptr) || !AwaitExit(worker)) return 8;
    }
    Probe probe;
    SetEvent(probe.allowExit);
    if (!worker.Start(HeldWorker, &probe)) return 9;
    worker.StopAndJoin();
    if (worker.OwnsFrames()) return 10;
    std::puts("frame ownership: 25 start/stop/restart/failure cycles and shutdown passed");
}
