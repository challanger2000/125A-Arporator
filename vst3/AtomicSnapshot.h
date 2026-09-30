#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace arporator::vst3 {

template <class T>
class AtomicSnapshot {
public:
    static_assert(std::is_trivially_copyable_v<T>,
                  "AtomicSnapshot requires trivially-copyable state");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "Arporator requires lock-free 64-bit atomics");

    static constexpr std::size_t kWordCount =
        (sizeof(T) + sizeof(std::uint64_t) - 1u) / sizeof(std::uint64_t);

    AtomicSnapshot() noexcept {
        for (auto& word : words_)
            word.store(0u, std::memory_order_relaxed);
    }

    std::uint64_t store(const T& value) noexcept {
        std::array<std::uint64_t, kWordCount> packed {};
        std::memcpy(packed.data(), &value, sizeof(T));

        sequence_.fetch_add(1u, std::memory_order_seq_cst);
        for (std::size_t i = 0; i < kWordCount; ++i)
            words_[i].store(packed[i], std::memory_order_seq_cst);
        return sequence_.fetch_add(1u, std::memory_order_seq_cst) + 1u;
    }

    bool tryLoad(T& value, std::uint64_t& sequence) const noexcept {
        const auto before = sequence_.load(std::memory_order_seq_cst);
        if ((before & 1u) != 0u)
            return false;

        std::array<std::uint64_t, kWordCount> packed {};
        for (std::size_t i = 0; i < kWordCount; ++i)
            packed[i] = words_[i].load(std::memory_order_seq_cst);

        const auto after = sequence_.load(std::memory_order_seq_cst);
        if (before != after || (after & 1u) != 0u)
            return false;

        std::memcpy(&value, packed.data(), sizeof(T));
        sequence = after;
        return true;
    }

    std::uint64_t sequence() const noexcept {
        return sequence_.load(std::memory_order_seq_cst);
    }

private:
    std::array<std::atomic<std::uint64_t>, kWordCount> words_ {};
    std::atomic<std::uint64_t> sequence_ {0u};
};

} // namespace arporator::vst3
