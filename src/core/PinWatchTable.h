#pragma once

// A lock-free table from entity pointers to a small value, for pin hooks that
// run on any thread and must not take a lock (the engine can re-enter them).
// Open addressing with linear probing, filled to half at most so a miss stops
// at an empty slot soon. Writes (Watch, Replace, Clear) happen under the
// owner's lock; Lookup and Empty from any thread. A key is never removed
// while the table is in use (that would break other keys' probe chains):
// Replace it with a value the owner ignores instead.

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fr {

// The first slot to probe for a pointer in a table of p_Slots (a power of
// two): the pointer's bits above the 16-byte alignment, Fibonacci-hashed to
// the top log2(p_Slots) bits.
constexpr size_t PointerSlot(uintptr_t p_Key, size_t p_Slots) {
    return static_cast<size_t>((static_cast<uint64_t>(p_Key) >> 4) * 0x9E3779B97F4A7C15ull >>
                               (64 - std::countr_zero(p_Slots)));
}

template <size_t N>
class PinWatchTable {
    static_assert(N >= 2 && std::has_single_bit(N), "the slot count is a power of two");

public:
    static constexpr size_t kSlots = N;
    static constexpr uint32_t kMaxWatched = N / 2;

    // Under the owner's lock: watches p_Key with p_Value, or sets the value
    // of a key already watched. False when the table is full (not watched).
    bool Watch(const void* p_Key, uint32_t p_Value) {
        const auto s_Key = reinterpret_cast<uintptr_t>(p_Key);
        size_t s_At = PointerSlot(s_Key, N);

        for (size_t n = 0; n < kMaxWatched; ++n, s_At = (s_At + 1) % N) {
            const uintptr_t s_Old = m_Slots[s_At].key.load(std::memory_order_relaxed);

            if (s_Old == s_Key) {
                m_Slots[s_At].value.store(p_Value, std::memory_order_release);
                return true;
            }

            if (s_Old != 0)
                continue;

            if (m_Count.load(std::memory_order_relaxed) >= kMaxWatched)
                return false;

            m_Slots[s_At].value.store(p_Value, std::memory_order_relaxed);
            m_Slots[s_At].key.store(s_Key, std::memory_order_release);
            m_Count.fetch_add(1, std::memory_order_release);
            return true;
        }

        return false;
    }

    // Under the owner's lock: sets the value of a watched key; nothing when
    // it is not watched.
    void Replace(const void* p_Key, uint32_t p_Value) {
        const auto s_Key = reinterpret_cast<uintptr_t>(p_Key);
        size_t s_At = PointerSlot(s_Key, N);

        for (size_t n = 0; n < kMaxWatched; ++n, s_At = (s_At + 1) % N) {
            const uintptr_t s_Found = m_Slots[s_At].key.load(std::memory_order_relaxed);

            if (s_Found == 0)
                return;

            if (s_Found == s_Key) {
                m_Slots[s_At].value.store(p_Value, std::memory_order_release);
                return;
            }
        }
    }

    // Any thread: the value of a watched key.
    std::optional<uint32_t> Lookup(const void* p_Key) const {
        const auto s_Key = reinterpret_cast<uintptr_t>(p_Key);
        size_t s_At = PointerSlot(s_Key, N);

        for (size_t n = 0; n < kMaxWatched; ++n, s_At = (s_At + 1) % N) {
            const uintptr_t s_Found = m_Slots[s_At].key.load(std::memory_order_acquire);

            if (s_Found == 0)
                return std::nullopt;

            if (s_Found == s_Key)
                return m_Slots[s_At].value.load(std::memory_order_relaxed);
        }

        return std::nullopt;
    }

    // Any thread: nothing is watched (the pin hooks' early out).
    bool Empty() const { return m_Count.load(std::memory_order_acquire) == 0; }

    uint32_t Count() const { return m_Count.load(std::memory_order_acquire); }

    // Under the owner's lock, at a new load: forgets every key. A reader
    // racing it misses, or finds a value the owner's locked check rejects.
    void Clear() {
        for (Slot& s_Slot : m_Slots) {
            s_Slot.key.store(0, std::memory_order_relaxed);
            s_Slot.value.store(0, std::memory_order_relaxed);
        }

        m_Count.store(0, std::memory_order_release);
    }

private:
    struct Slot {
        std::atomic<uintptr_t> key {0};
        std::atomic<uint32_t> value {0};
    };

    std::array<Slot, N> m_Slots {};
    std::atomic<uint32_t> m_Count {0};
};

} // namespace fr
