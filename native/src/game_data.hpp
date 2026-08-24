#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace metaverse {

struct NavigationEntry {
    std::string scene;
    std::int32_t variant = 0;
};

struct SceneObject {
    std::int32_t index = 0;
    std::uint32_t flags = 0;
    std::array<std::array<std::int32_t, 2>, 3> ranges{};
    std::array<std::int32_t, 3> links{};
    std::int32_t auxiliary = 0;
    std::optional<std::string> asset;
    std::optional<std::array<std::int32_t, 2>> point;
};

struct SceneDefinition {
    std::string background_video;
    std::string background_sprite;
    std::vector<SceneObject> objects;
    std::vector<std::int32_t> special_object_indices;
};

struct MotionRecord {
    std::uint16_t file_offset = 0;
    std::int32_t media_mode = 0;
    std::int16_t field_04 = 0;
    std::int16_t loop_frame = 0;
    std::int16_t terminal_frame = 0;
    std::uint16_t hit_transition_offset = 0;
    std::uint16_t timed_transition_offset = 0;
    std::uint16_t flags = 0;
    std::int16_t delta_x = 0;
    std::int16_t delta_y = 0;
    std::int16_t timer_interval = 0;
    std::int16_t result_code = 0;
    std::int16_t initial_x = 0;
    std::int16_t initial_y = 0;
    std::int16_t sound_index = 0;
    std::uint16_t motion_cursor_offset = 0;
    std::uint16_t motion_end_offset = 0;
    std::uint16_t field_22 = 0;
    std::uint32_t background_position = 0;
    std::uint32_t field_28 = 0;
    std::vector<std::array<std::int16_t, 2>> trajectory_points;
};

struct MotionScript {
    std::vector<std::uint8_t> raw_bytes;
    std::vector<MotionRecord> records;
};

constexpr std::uint32_t kSceneObjectHasAssetMask = 0x0c00;
constexpr std::uint32_t kSceneObjectHasPoint = 0x0040;
// fcn.0040b4e7 follows the middle range/link after each of these branches:
// 0x0100 plain automatic, 0x0800 sound-bearing automatic, and 0x2000 the
// retired custom-sprite dialog route.
constexpr std::uint32_t kSceneObjectAutomaticMask = 0x2900;

bool ParseNavigationData(
    std::istream& input,
    std::vector<NavigationEntry>& entries,
    std::string& error
);
bool LoadNavigationData(
    const std::filesystem::path& path,
    std::vector<NavigationEntry>& entries,
    std::string& error
);

bool ParseSceneData(
    std::istream& input,
    SceneDefinition& scene,
    std::string& error
);
bool LoadSceneData(
    const std::filesystem::path& path,
    SceneDefinition& scene,
    std::string& error
);

bool ParseMotionScript(
    std::istream& input,
    MotionScript& script,
    std::string& error
);
bool LoadMotionScript(
    const std::filesystem::path& path,
    MotionScript& script,
    std::string& error
);

}  // namespace metaverse
