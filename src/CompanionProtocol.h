#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <cstring>
#include <type_traits>
#include <openvr.h>
#include "ControllerInput.h"

namespace bvr::ipc {
constexpr uint32_t Magic = 0x42565248, Version = 3, SlotCount = 3;
enum SlotState : LONG { Free, Writing, Ready, Reading };
struct Tracking {
    uint64_t sample = 0, tick = 0;
    uint32_t valid = 0;
    vr::HmdMatrix34_t head{};
};
struct Calibration {
    int32_t adapter = -1;
    float hz = 0;
    vr::HmdMatrix34_t eyes[2]{};
    float tangents[2][4]{};
};
struct Frame {
    uint64_t sequence = 0, sample = 0, tick = 0;
    uint64_t revision[2]{};
    vr::HmdMatrix34_t pose[2]{};
};
struct alignas(8) Slot {
    volatile LONG state = Free;
    uint32_t width = 0, height = 0, generation = 0;
    uint64_t handle[2]{};
    Frame frame{};
};
// Only Control is accessed under the named mutex. Slot payloads are accessed
// exclusively by the owner of their Interlocked state, including GPU completion.
struct Control {
    uint32_t ready = 0, stop = 0, error = 0, grid = 0;
    uint64_t heartbeat = 0, received = 0, submitted = 0, testFailures = 0;
    Calibration calibration{};
    Tracking tracking{};
    ControllerInput controllers{};
};
struct Shared {
    uint32_t magic = Magic, version = Version, bytes = sizeof(Shared), reserved = 0;
    Control control{};
    Slot slots[SlotCount]{};
};
static_assert(std::is_trivially_copyable<Shared>::value, "IPC must contain only values");
static_assert(sizeof(vr::HmdMatrix34_t)==48, "Unexpected pose ABI");
inline bool Claim(Slot& slot, SlotState from, SlotState to) {
    return InterlockedCompareExchange(&slot.state,to,from)==from;
}
inline void Release(Slot& slot, SlotState state) { InterlockedExchange(&slot.state,state); }
class Session {
public:
    ~Session() { Close(); }
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    bool Open(const std::wstring& name, bool create) {
        Close();
        const auto mutexName=name+L"-control";
        mutex_=create ? CreateMutexW(nullptr,FALSE,mutexName.c_str()) : OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutexName.c_str());
        if (!mutex_) return false;
        mapping_=create ? CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name.c_str()) : OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
        const DWORD error=GetLastError();
        if (!mapping_ || (create && error==ERROR_ALREADY_EXISTS)) { Close(); return false; }
        shared_=static_cast<Shared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
        if (!shared_) { Close(); return false; }
        if (create) { Shared initial{}; memcpy(shared_,&initial,sizeof(initial)); }
        if (shared_->magic!=Magic || shared_->version!=Version || shared_->bytes!=sizeof(Shared)) { Close(); return false; }
        return true;
    }
    void Close() {
        if(shared_)UnmapViewOfFile(shared_); shared_=nullptr;
        if(mapping_)CloseHandle(mapping_); mapping_=nullptr;
        if(mutex_)CloseHandle(mutex_); mutex_=nullptr;
    }
    bool Lock(DWORD timeout=0) {
        if (!mutex_) return false;
        // An abandoned control mutex indicates peer failure, not valid data.
        const DWORD result=WaitForSingleObject(mutex_,timeout);
        if(result==WAIT_ABANDONED)ReleaseMutex(mutex_);
        return result==WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    Shared* Data() const { return shared_; }
    bool Read(Control& control) {
        if (!Lock()) return false;
        control=shared_->control; Unlock(); return true;
    }
private:
    HANDLE mapping_=nullptr, mutex_=nullptr;
    Shared* shared_=nullptr;
};
inline std::wstring UniqueName() {
    LARGE_INTEGER stamp{}; QueryPerformanceCounter(&stamp);
    return L"Local\\BayonettaVR-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(stamp.QuadPart);
}
}
