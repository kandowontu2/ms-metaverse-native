#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace metaverse {

inline constexpr std::size_t kCryoContestantCount = 10;
inline constexpr std::size_t kCryoInfoDetailCount = 6;
inline constexpr std::int32_t kCryoInfoFramesPerSecond = 10;

struct CryoInfoRange {
    std::int64_t first_frame = -1;
    std::int64_t last_frame = -1;

    [[nodiscard]] constexpr bool Valid() const {
        return first_frame >= 0 && last_frame >= first_frame;
    }
};

// Exact 120-DWORD table at MM.EXE 0x004364a8. Each one-based contestant
// record is 0x30 bytes: Name/Bio, Ambitions, Turn Ons, Turn Offs, Ideal Man,
// and Favorite Quote inclusive INFO.AVI frame pairs.
inline constexpr std::array<
    std::array<CryoInfoRange, kCryoInfoDetailCount>,
    kCryoContestantCount
> kCryoInfoRanges{{
    {{{0, 460}, {461, 710}, {711, 830}, {831, 970}, {971, 1170}, {1171, 1260}}},
    {{{1265, 1560}, {1561, 1620}, {1701, 1850}, {1851, 1960}, {1961, 2060}, {2061, 2108}}},
    {{{2111, 2330}, {2331, 2430}, {2501, 2560}, {2561, 2610}, {2611, 2680}, {2681, 2727}}},
    {{{2731, 3080}, {3081, 3190}, {3191, 3260}, {3261, 3360}, {3361, 3430}, {3431, 3510}}},
    {{{3531, 3850}, {3851, 4070}, {4071, 4210}, {4211, 4290}, {4291, 4400}, {4401, 4490}}},
    {{{4495, 4770}, {4771, 4920}, {4921, 5050}, {5051, 5140}, {5141, 5190}, {5191, 5234}}},
    {{{5237, 5620}, {5621, 5700}, {5701, 5830}, {5831, 5880}, {5881, 5960}, {5961, 6062}}},
    {{{6063, 6300}, {6421, 6480}, {6581, 6700}, {6701, 6760}, {6761, 6840}, {6841, 6915}}},
    {{{6918, 7400}, {7401, 7580}, {7581, 7740}, {7741, 7890}, {7891, 7970}, {7971, 8041}}},
    {{{8042, 8270}, {8341, 8420}, {8501, 8590}, {8591, 8690}, {8691, 8780}, {8781, 8830}}},
}};

enum class CryoInfoPlaybackStep {
    stop_info_child,
    play_prompt_synchronously,
    seek_first_frame,
    play_to_last_frame,
};

inline constexpr std::array<CryoInfoPlaybackStep, 4>
    kCryoInfoPlaybackOrder{{
        CryoInfoPlaybackStep::stop_info_child,
        CryoInfoPlaybackStep::play_prompt_synchronously,
        CryoInfoPlaybackStep::seek_first_frame,
        CryoInfoPlaybackStep::play_to_last_frame,
    }};

struct CryoInfoPlaybackPlan {
    std::size_t detail = 0;
    CryoInfoRange range;
};

[[nodiscard]] inline constexpr std::optional<CryoInfoPlaybackPlan>
PlanCryoInfoPlayback(std::int32_t contestant_id, std::size_t detail) {
    if (contestant_id < 1 ||
        contestant_id > static_cast<std::int32_t>(kCryoContestantCount) ||
        detail >= kCryoInfoDetailCount) {
        return std::nullopt;
    }
    return CryoInfoPlaybackPlan{
        detail,
        kCryoInfoRanges[static_cast<std::size_t>(contestant_id - 1)][detail],
    };
}

}  // namespace metaverse
