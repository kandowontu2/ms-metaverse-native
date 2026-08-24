#include "host_reaction.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

namespace metaverse {
namespace {

constexpr std::size_t kLegacyFlagSize = 4;
constexpr std::size_t kLegacyFlagCount =
    kHostReactionGroupCount * kHostReactionVariantCapacity;
constexpr std::size_t kLegacyFileSize = kLegacyFlagCount * kLegacyFlagSize;

std::uint32_t ReadU32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void WriteU32(std::ostream& output, std::uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void WriteRawLegacyHostReactionState(
    std::ostream& output,
    const HostReactionState& state
) {
    for (std::size_t variant = 0;
         variant < kHostReactionVariantCapacity; ++variant) {
        for (std::size_t group = 0; group < kHostReactionGroupCount; ++group) {
            WriteU32(output, state.used[group][variant]);
        }
    }
}

}  // namespace

std::int32_t HostReactionGroupForScore(std::int32_t score) {
    if (score >= 8) {
        return 2;
    }
    if (score >= 4) {
        return 1;
    }
    return 3;
}

std::wstring HostReactionStem(std::int32_t group, std::int32_t variant) {
    if (group < 1 ||
        group > static_cast<std::int32_t>(kHostReactionGroupCount)) {
        return {};
    }
    const auto count = kHostReactionVariantCounts[
        static_cast<std::size_t>(group - 1)
    ];
    if (variant < 1 || variant > static_cast<std::int32_t>(count)) {
        return {};
    }
    return L"X" + std::to_wstring(group) + std::to_wstring(variant);
}

bool ParseLegacyHostReactionState(
    std::istream& input,
    HostReactionState& state,
    std::string& error
) {
    state = {};
    error.clear();
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    if (!input.eof() && input.fail()) {
        error = "failed while reading legacy host-reaction state";
        return false;
    }
    if (bytes.size() != kLegacyFileSize) {
        error = "legacy host-reaction state must be exactly 180 bytes";
        return false;
    }
    for (std::size_t variant = 0;
         variant < kHostReactionVariantCapacity; ++variant) {
        for (std::size_t group = 0; group < kHostReactionGroupCount; ++group) {
            const std::size_t offset =
                (variant * kHostReactionGroupCount + group) * kLegacyFlagSize;
            const std::uint32_t flag = ReadU32(bytes.data() + offset);
            if (flag > 1) {
                error = "legacy host-reaction state contains a flag other than 0 or 1";
                state = {};
                return false;
            }
            if (variant >= kHostReactionVariantCounts[group] && flag != 0) {
                error = "legacy host-reaction state marks an unavailable variant as used";
                state = {};
                return false;
            }
            state.used[group][variant] = flag;
        }
    }
    return true;
}

bool WriteLegacyHostReactionState(
    std::ostream& output,
    const HostReactionState& state,
    std::string& error
) {
    error.clear();
    for (std::size_t variant = 0;
         variant < kHostReactionVariantCapacity; ++variant) {
        for (std::size_t group = 0; group < kHostReactionGroupCount; ++group) {
            if (state.used[group][variant] > 1) {
                error = "cannot serialize a host-reaction flag other than 0 or 1";
                return false;
            }
            if (variant >= kHostReactionVariantCounts[group] &&
                state.used[group][variant] != 0) {
                error = "cannot serialize an unavailable host-reaction variant";
                return false;
            }
            WriteU32(output, state.used[group][variant]);
        }
    }
    if (!output) {
        error = "failed while writing legacy host-reaction state";
        return false;
    }
    return true;
}

bool LoadLegacyHostReactionState(
    const std::filesystem::path& path,
    HostReactionState& state,
    std::string& error
) {
    if (!std::filesystem::exists(path)) {
        state = {};
        error.clear();
        return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open legacy host-reaction state: " + path.string();
        return false;
    }
    return ParseLegacyHostReactionState(input, state, error);
}

void LoadLegacyHostReactionStateForSelection(
    const std::filesystem::path& path,
    HostReactionState& state
) {
    state = {};
    std::array<std::uint8_t, kLegacyFileSize> bytes{};
    std::ifstream input(path, std::ios::binary);
    if (input) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
    }
    for (std::size_t variant = 0;
         variant < kHostReactionVariantCapacity; ++variant) {
        for (std::size_t group = 0; group < kHostReactionGroupCount; ++group) {
            const std::size_t offset =
                (variant * kHostReactionGroupCount + group) * kLegacyFlagSize;
            state.used[group][variant] = ReadU32(bytes.data() + offset);
        }
    }
}

bool SaveLegacyHostReactionState(
    const std::filesystem::path& path,
    const HostReactionState& state,
    std::string& error
) {
    if (!path.parent_path().empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(path.parent_path(), directory_error);
        if (directory_error) {
            error = "cannot create host-reaction state directory: " +
                    directory_error.message();
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot create legacy host-reaction state: " + path.string();
        return false;
    }
    WriteRawLegacyHostReactionState(output, state);
    if (!output) {
        error = "failed while writing legacy host-reaction state";
        return false;
    }
    error.clear();
    return true;
}

std::int32_t DrawHostReactionVariant(
    HostReactionState& state,
    std::int32_t group,
    LegacyRandom& random,
    std::string& error
) {
    error.clear();
    if (group < 1 ||
        group > static_cast<std::int32_t>(kHostReactionGroupCount)) {
        error = "host-reaction group must be between 1 and 3";
        return 0;
    }
    const std::size_t group_index = static_cast<std::size_t>(group - 1);
    const std::size_t active_count = kHostReactionVariantCounts[group_index];
    auto& used = state.used[group_index];
    std::size_t available_count = 0;
    for (std::size_t variant = 0; variant < active_count; ++variant) {
        if (used[variant] == 0) {
            ++available_count;
        }
    }
    if (available_count == 0) {
        std::fill(used.begin(), used.begin() + active_count, 0u);
        available_count = active_count;
    }

    // 0x0040ea7e-0x0040eac4 does not compact the zero entries. It calculates
    // floor(rand * zero_count / 32767), then scans the interleaved table until
    // that ordinal zero is reached. Preserve the peculiar one-past-active
    // probe and wrap as well, even though the stored 0x38000100 multiplier is
    // slightly below 1/32767 and keeps legal CRT targets below zero_count.
    const std::int32_t target = static_cast<std::int32_t>(
        random.Scale(available_count)
    );
    std::int32_t zero_ordinal = -1;
    std::size_t chosen = static_cast<std::size_t>(-1);
    for (;;) {
        if (target <= zero_ordinal) {
            break;
        }

        ++chosen;
        std::uint32_t flag = 0;
        if (chosen < active_count) {
            flag = used[chosen];
        } else if (active_count < kHostReactionVariantCapacity) {
            // X2 and X3 probe their first inactive HNRD word before wrapping.
            flag = used[active_count];
        } else {
            // X1's one-past word aliases the first field of the destroyed
            // CString immediately following the 180-byte stack table. Its
            // constructor installs the non-null 0x004378d0 empty-string
            // pointer and the destructor does not clear that field.
            flag = 1;
        }
        if (flag == 0) {
            ++zero_ordinal;
        }

        if (chosen == active_count) {
            chosen = 0;
            if (zero_ordinal != 0) {
                continue;
            }
            break;
        }
    }

    used[chosen] = 1;
    return static_cast<std::int32_t>(chosen + 1);
}

}  // namespace metaverse
