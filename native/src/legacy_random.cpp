#include "legacy_random.hpp"

namespace metaverse {

std::int32_t LegacyRandom::Next() {
    state_ = state_ * 0x343fdu + 0x269ec3u;
    return static_cast<std::int32_t>((state_ >> 16) & 0x7fffu);
}

std::size_t LegacyRandom::Modulo(std::size_t range) {
    return range == 0
        ? 0
        : static_cast<std::size_t>(Next()) % range;
}

std::size_t LegacyRandom::Scale(std::size_t range) {
    if (range == 0) {
        return 0;
    }

    // The original does not use the common RAND_MAX + 1 divisor here. Its
    // fixed-range sites multiply by range / 32767.0f, while dynamic-range
    // sites multiply rand() by range and then by the single-precision
    // reciprocal stored as 0x38000100. Keep that exact binary constant and
    // retain the intermediate precision before truncating toward zero.
    constexpr float kRandMaximumReciprocal = 1.0f / 32767.0f;
    const double scaled = static_cast<double>(Next()) *
                          static_cast<double>(range) *
                          static_cast<double>(kRandMaximumReciprocal);
    return static_cast<std::size_t>(scaled);
}

}  // namespace metaverse
