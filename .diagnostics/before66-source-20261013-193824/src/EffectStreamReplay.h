#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bvr {
// cEspDraw's embedded stream descriptors are 32 bytes in Steam 0x591AD330.
// Their clear method only resets metadata; the effect heap owns the buffers.
// Keep normal per-pass consumption, then rewind metadata once between eyes.
template<size_t Capacity = 8192>
class EffectStreamReplay {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    struct Entry { void* address = nullptr; std::array<uint32_t, 8> words{}; };
    std::array<Entry, Capacity> entries_{};
    std::array<size_t, Capacity * 2> slots_{};
    size_t count_ = 0;
    bool recording_ = false;
public:
    void Reset() { count_ = 0; recording_ = false; }
    void BeginFrame() { Reset(); slots_.fill(0); recording_ = true; }
    bool Remember(void* stream) {
        if (!recording_ || !stream) return true;
        size_t slot = (reinterpret_cast<uintptr_t>(stream) >> 5) & (slots_.size() - 1);
        while (slots_[slot]) {
            if (entries_[slots_[slot] - 1].address == stream) return true;
            slot = (slot + 1) & (slots_.size() - 1);
        }
        if (count_ == Capacity) return false;
        auto& entry = entries_[count_];
        entry.address = stream;
        std::memcpy(entry.words.data(), stream, sizeof(entry.words));
        slots_[slot] = ++count_;
        return true;
    }
    size_t RestoreForRightEye() {
        const size_t restored = count_;
        for (size_t i = 0; i < count_; ++i)
            std::memcpy(entries_[i].address, entries_[i].words.data(), sizeof(entries_[i].words));
        Reset(); // Never retain writable addresses beyond this stereo pair.
        return restored;
    }
};
}
