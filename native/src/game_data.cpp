#include "game_data.hpp"

#include <charconv>
#include <deque>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <string_view>

namespace metaverse {
namespace {

constexpr std::int32_t kMaximumRecordCount = 4096;
constexpr std::size_t kMotionRecordSize = 0x2c;

class TokenReader {
public:
    explicit TokenReader(std::istream& input) : input_(input) {}

    bool ReadString(std::string& value, std::string_view field, std::string& error) {
        if (!(input_ >> value)) {
            error = "missing " + std::string(field) + " at token " +
                    std::to_string(token_index_ + 1);
            return false;
        }
        ++token_index_;
        return true;
    }

    bool ReadInteger(std::int32_t& value, std::string_view field, std::string& error) {
        std::string token;
        if (!ReadString(token, field, error)) {
            return false;
        }
        if (!ParseInteger(token, value)) {
            error = "invalid " + std::string(field) + " at token " +
                    std::to_string(token_index_) + ": " + token;
            return false;
        }
        return true;
    }

    bool ReadIntegerOrString(
        std::int32_t& value,
        std::string& token,
        bool& is_integer,
        std::string_view field,
        std::string& error
    ) {
        if (!ReadString(token, field, error)) {
            return false;
        }
        is_integer = ParseInteger(token, value);
        return true;
    }

    bool ReadOptionalInteger(
        std::int32_t& value,
        bool& present,
        std::string_view field,
        std::string& error
    ) {
        std::string token;
        if (!(input_ >> token)) {
            if (input_.eof()) {
                present = false;
                return true;
            }
            error = "failed while reading " + std::string(field);
            return false;
        }
        ++token_index_;
        present = true;
        if (!ParseInteger(token, value)) {
            error = "invalid " + std::string(field) + " at token " +
                    std::to_string(token_index_) + ": " + token;
            return false;
        }
        return true;
    }

    bool RequireEnd(std::string& error) {
        std::string token;
        if (input_ >> token) {
            ++token_index_;
            error = "unexpected trailing token " + std::to_string(token_index_) + ": " + token;
            return false;
        }
        return true;
    }

private:
    static bool ParseInteger(const std::string& token, std::int32_t& value) {
        const char* begin = token.data();
        const char* end = begin + token.size();
        const auto result = std::from_chars(begin, end, value);
        return result.ec == std::errc{} && result.ptr == end;
    }

    std::istream& input_;
    std::size_t token_index_ = 0;
};

bool OpenInput(const std::filesystem::path& path, std::ifstream& input, std::string& error) {
    input.open(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path.string();
        return false;
    }
    return true;
}

bool IsSensibleCount(std::int32_t count) {
    return count >= 0 && count <= kMaximumRecordCount;
}

std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8)
    );
}

std::int16_t ReadI16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int16_t>(ReadU16(bytes, offset));
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(ReadU16(bytes, offset)) |
           (static_cast<std::uint32_t>(ReadU16(bytes, offset + 2)) << 16);
}

std::int32_t ReadI32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int32_t>(ReadU32(bytes, offset));
}

}  // namespace

bool ParseNavigationData(
    std::istream& input,
    std::vector<NavigationEntry>& entries,
    std::string& error
) {
    entries.clear();
    error.clear();
    TokenReader reader(input);
    std::int32_t count = 0;
    if (!reader.ReadInteger(count, "navigation entry count", error)) {
        return false;
    }
    if (!IsSensibleCount(count)) {
        error = "navigation entry count is out of range: " + std::to_string(count);
        return false;
    }
    entries.reserve(static_cast<std::size_t>(count));
    for (std::int32_t index = 0; index < count; ++index) {
        NavigationEntry entry;
        if (!reader.ReadString(entry.scene, "navigation scene", error) ||
            !reader.ReadInteger(entry.variant, "navigation variant", error)) {
            entries.clear();
            return false;
        }
        entries.push_back(std::move(entry));
    }
    if (!reader.RequireEnd(error)) {
        entries.clear();
        return false;
    }
    return true;
}

bool LoadNavigationData(
    const std::filesystem::path& path,
    std::vector<NavigationEntry>& entries,
    std::string& error
) {
    std::ifstream input;
    return OpenInput(path, input, error) && ParseNavigationData(input, entries, error);
}

bool ParseSceneData(
    std::istream& input,
    SceneDefinition& scene,
    std::string& error
) {
    scene = {};
    error.clear();
    TokenReader reader(input);
    std::int32_t count = 0;
    if (!reader.ReadString(scene.background_video, "background video", error) ||
        !reader.ReadString(scene.background_sprite, "background sprite", error) ||
        !reader.ReadInteger(count, "scene object count", error)) {
        scene = {};
        return false;
    }
    if (!IsSensibleCount(count)) {
        error = "scene object count is out of range: " + std::to_string(count);
        scene = {};
        return false;
    }

    scene.objects.reserve(static_cast<std::size_t>(count));
    for (std::int32_t object_index = 0; object_index < count; ++object_index) {
        SceneObject object;
        std::int32_t raw_flags = 0;
        const auto read = [&](std::int32_t& value, std::string_view field) {
            return reader.ReadInteger(value, field, error);
        };
        if (!read(object.index, "object index") ||
            !read(raw_flags, "object flags") ||
            !read(object.ranges[0][0], "object range 1 start") ||
            !read(object.ranges[0][1], "object range 1 end") ||
            !read(object.ranges[1][0], "object range 2 start") ||
            !read(object.ranges[1][1], "object range 2 end") ||
            !read(object.ranges[2][0], "object range 3 start") ||
            !read(object.ranges[2][1], "object range 3 end") ||
            !read(object.links[0], "object link 1") ||
            !read(object.links[1], "object link 2") ||
            !read(object.links[2], "object link 3")) {
            scene = {};
            return false;
        }
        object.flags = static_cast<std::uint32_t>(raw_flags);
        std::string auxiliary_token;
        bool has_auxiliary_integer = false;
        if (!reader.ReadIntegerOrString(
                object.auxiliary,
                auxiliary_token,
                has_auxiliary_integer,
                "object auxiliary value",
                error
            )) {
            scene = {};
            return false;
        }
        if (!has_auxiliary_integer && (object.flags & kSceneObjectHasAssetMask) == 0) {
            error = "invalid object auxiliary value: " + auxiliary_token;
            scene = {};
            return false;
        }
        if ((object.flags & kSceneObjectHasAssetMask) != 0) {
            if (has_auxiliary_integer) {
                std::string asset;
                if (!reader.ReadString(asset, "object asset", error)) {
                    scene = {};
                    return false;
                }
                object.asset = std::move(asset);
            } else {
                // BRAINS.DAT object 17 goes directly from its third link to the
                // WAV token. The original fscanf-based loader ignored the short
                // assignment count and then consumed that same token as the path.
                object.auxiliary = 0;
                object.asset = std::move(auxiliary_token);
            }
        }
        if ((object.flags & kSceneObjectHasPoint) != 0) {
            std::array<std::int32_t, 2> point{};
            if (!read(point[0], "object point x") || !read(point[1], "object point y")) {
                scene = {};
                return false;
            }
            object.point = point;
        }
        scene.objects.push_back(std::move(object));
    }

    while (true) {
        std::int32_t special_index = 0;
        bool present = false;
        if (!reader.ReadOptionalInteger(
                special_index, present, "special object index", error
            )) {
            scene = {};
            return false;
        }
        if (!present) {
            break;
        }
        scene.special_object_indices.push_back(special_index);
    }
    return true;
}

bool LoadSceneData(
    const std::filesystem::path& path,
    SceneDefinition& scene,
    std::string& error
) {
    std::ifstream input;
    return OpenInput(path, input, error) && ParseSceneData(input, scene, error);
}

bool ParseMotionScript(
    std::istream& input,
    MotionScript& script,
    std::string& error
) {
    script = {};
    error.clear();
    script.raw_bytes.assign(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    );
    if (!input.eof() && input.fail()) {
        error = "failed while reading motion script";
        script = {};
        return false;
    }
    if (script.raw_bytes.empty()) {
        return true;
    }
    if (script.raw_bytes.size() < kMotionRecordSize) {
        error = "motion script is shorter than one 44-byte record";
        script = {};
        return false;
    }

    std::deque<std::uint16_t> pending{0};
    std::set<std::uint16_t> visited;
    while (!pending.empty()) {
        const std::uint16_t record_offset = pending.front();
        pending.pop_front();
        if (!visited.insert(record_offset).second) {
            continue;
        }
        const std::size_t offset = record_offset;
        if ((offset & 3u) != 0 || offset + kMotionRecordSize > script.raw_bytes.size()) {
            error = "motion record offset is invalid: " + std::to_string(offset);
            script = {};
            return false;
        }

        MotionRecord record;
        record.file_offset = record_offset;
        record.media_mode = ReadI32(script.raw_bytes, offset + 0x00);
        record.field_04 = ReadI16(script.raw_bytes, offset + 0x04);
        record.loop_frame = ReadI16(script.raw_bytes, offset + 0x06);
        record.terminal_frame = ReadI16(script.raw_bytes, offset + 0x08);
        record.hit_transition_offset = ReadU16(script.raw_bytes, offset + 0x0a);
        record.timed_transition_offset = ReadU16(script.raw_bytes, offset + 0x0c);
        record.flags = ReadU16(script.raw_bytes, offset + 0x0e);
        record.delta_x = ReadI16(script.raw_bytes, offset + 0x10);
        record.delta_y = ReadI16(script.raw_bytes, offset + 0x12);
        record.timer_interval = ReadI16(script.raw_bytes, offset + 0x14);
        record.result_code = ReadI16(script.raw_bytes, offset + 0x16);
        record.initial_x = ReadI16(script.raw_bytes, offset + 0x18);
        record.initial_y = ReadI16(script.raw_bytes, offset + 0x1a);
        record.sound_index = ReadI16(script.raw_bytes, offset + 0x1c);
        record.motion_cursor_offset = ReadU16(script.raw_bytes, offset + 0x1e);
        record.motion_end_offset = ReadU16(script.raw_bytes, offset + 0x20);
        record.field_22 = ReadU16(script.raw_bytes, offset + 0x22);
        record.background_position = ReadU32(script.raw_bytes, offset + 0x24);
        record.field_28 = ReadU32(script.raw_bytes, offset + 0x28);

        for (const std::uint16_t transition : {
                 record.hit_transition_offset, record.timed_transition_offset
             }) {
            if (transition != 0) {
                pending.push_back(transition);
            }
        }

        if ((record.flags & 0x0200) != 0) {
            const std::size_t cursor = record.motion_cursor_offset;
            const std::size_t end = record.motion_end_offset;
            if ((cursor & 3u) != 0 || (end & 3u) != 0 || cursor > end ||
                end > script.raw_bytes.size()) {
                error = "motion coordinate range is invalid at record " +
                        std::to_string(offset);
                script = {};
                return false;
            }
            // The runtime stores the record's 0x1e value as its current
            // coordinate cursor, adds four before each read, and transitions
            // when that increment reaches the exclusive 0x20 end value.
            for (std::size_t point = cursor + 4; point < end; point += 4) {
                record.trajectory_points.push_back({
                    ReadI16(script.raw_bytes, point),
                    ReadI16(script.raw_bytes, point + 2),
                });
            }
        }
        script.records.push_back(std::move(record));
    }
    return true;
}

bool LoadMotionScript(
    const std::filesystem::path& path,
    MotionScript& script,
    std::string& error
) {
    std::ifstream input;
    return OpenInput(path, input, error) && ParseMotionScript(input, script, error);
}

}  // namespace metaverse
