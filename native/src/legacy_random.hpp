#pragma once

#include <cstddef>
#include <cstdint>

namespace metaverse {

// MM.EXE statically links the Microsoft CRT rand/srand implementation at
// 0x00417969/0x00417976. Keep its 32-bit state and 15-bit output exactly;
// gameplay callers deliberately use either modulo or the executable's
// float-scaled floor(rand * range / 32767) selection.
class LegacyRandom {
public:
    explicit LegacyRandom(std::uint32_t seed = 1) : state_(seed) {}

    void Seed(std::uint32_t seed) { state_ = seed; }
    [[nodiscard]] std::int32_t Next();
    [[nodiscard]] std::size_t Modulo(std::size_t range);
    [[nodiscard]] std::size_t Scale(std::size_t range);
    [[nodiscard]] std::uint32_t State() const { return state_; }

private:
    std::uint32_t state_ = 1;
};

}  // namespace metaverse
