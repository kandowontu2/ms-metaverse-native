#include "scene_navigator.hpp"

#include <algorithm>
#include <cmath>

namespace metaverse {

bool NavigationTimerThresholdPassed(
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point threshold
) noexcept {
    return now > threshold;
}

bool NavigationContextMenuSuppressed(std::size_t navigation_entry) {
    return navigation_entry >= kHallMsMetaverseEntry &&
           navigation_entry <= kHallLastEntry;
}

std::size_t NavigationSlotShortcutEntry(bool secondary_asset_scene) {
    return secondary_asset_scene
        ? kSecondarySlotShortcutEntry
        : kPrimarySlotShortcutEntry;
}

NavigationSkipDisposition NavigationSkipDispositionForState(
    bool pending_navigation_resolution,
    bool slot_spin_active,
    bool bounded_range_active
) {
    if (slot_spin_active && bounded_range_active) {
        return NavigationSkipDisposition::reveal_slot_result;
    }
    if (pending_navigation_resolution) {
        return NavigationSkipDisposition::complete_transition;
    }
    return NavigationSkipDisposition::none;
}

bool NavigationSkipAheadEnabled(
    bool pending_navigation_resolution,
    bool slot_spin_active,
    bool bounded_range_active
) {
    return NavigationSkipDispositionForState(
               pending_navigation_resolution,
               slot_spin_active,
               bounded_range_active
           ) != NavigationSkipDisposition::none;
}

bool NavigationLeftButtonInterruptsPlayback(
    bool pending_navigation_resolution,
    bool slot_spin_active,
    bool bounded_range_active
) {
    return NavigationSkipAheadEnabled(
        pending_navigation_resolution,
        slot_spin_active,
        bounded_range_active
    );
}

CrossSceneTransition CrossSceneTransitionForFlags(std::uint32_t flags) {
    return (flags & 0x1000) != 0
        ? CrossSceneTransition::fade_to_white
        : CrossSceneTransition::immediate;
}

std::uint32_t NavigationPaletteFadePassForProgress(double progress) noexcept {
    if (!std::isfinite(progress) || progress <= 0.0) {
        return 0;
    }
    if (progress >= 1.0) {
        return kNavigationPaletteFadePasses;
    }
    return static_cast<std::uint32_t>(
        std::floor(progress * kNavigationPaletteFadePasses)
    );
}

std::uint32_t ApplyNavigationPaletteFadeToColor(
    std::uint32_t packed_bgra,
    std::uint32_t completed_passes
) noexcept {
    std::uint8_t blue = static_cast<std::uint8_t>(packed_bgra);
    std::uint8_t green = static_cast<std::uint8_t>(packed_bgra >> 8);
    std::uint8_t red = static_cast<std::uint8_t>(packed_bgra >> 16);
    const std::uint32_t alpha = packed_bgra & 0xff000000u;

    completed_passes = std::min(
        completed_passes, kNavigationPaletteFadePasses
    );
    for (std::uint32_t pass = 0; pass < completed_passes; ++pass) {
        const std::uint8_t previous_red = red;
        const std::uint8_t previous_green = green;
        const std::uint8_t previous_blue = blue;

        if (red < 246) {
            red = static_cast<std::uint8_t>(red + 10);
        } else {
            red = 255;
        }
        if (blue < 246) {
            blue = static_cast<std::uint8_t>(blue + 10);
        } else {
            // 0x0040cda1 writes the red byte, not the tested blue byte.
            red = 255;
        }
        if (green < 246) {
            green = static_cast<std::uint8_t>(green + 10);
        } else {
            // 0x0040cdb0 repeats the same red-byte saturation quirk.
            red = 255;
        }

        if (red == previous_red && green == previous_green &&
            blue == previous_blue) {
            break;
        }
    }
    return alpha |
        (static_cast<std::uint32_t>(red) << 16) |
        (static_cast<std::uint32_t>(green) << 8) |
        static_cast<std::uint32_t>(blue);
}

bool IsPlayableNavigationAsset(std::string_view asset) {
    return !asset.empty() && asset.front() != '0';
}

bool ShouldAdvanceNavigationOnInitialSceneEntry(std::uint32_t flags) {
    return (flags & 0x0100) != 0;
}

void ResetNavigationEncounterFlagsForScene(
    std::array<bool, 256>& encountered,
    std::size_t object_count
) {
    std::fill_n(
        encountered.begin(),
        std::min(object_count, encountered.size()),
        false
    );
}

std::int64_t ClampNavigationPlaybackLastFrame(
    std::int64_t requested_last_frame,
    std::int64_t frame_count
) {
    return frame_count > 0
        ? std::min(requested_last_frame, frame_count - 1)
        : requested_last_frame;
}

OneShotNavigationTransition OneShotTransitionForFlags(
    std::uint32_t flags,
    bool first_pass_completed
) {
    if ((flags & 0x0010) == 0) {
        return OneShotNavigationTransition::ordinary;
    }
    return first_pass_completed
        ? OneShotNavigationTransition::held_frame_after_first_pass
        : OneShotNavigationTransition::animated_first_pass;
}

std::optional<std::int32_t> NavigationArrivalHoldFrame(std::uint32_t flags) {
    if ((flags & 0x0020) == 0) {
        return std::nullopt;
    }
    return 25;
}

std::int32_t RepeatedOneShotDestinationFrame(const SceneObject& destination) {
    return destination.ranges[1][0];
}

bool ShouldPlayNavigationDepartureSound(std::uint32_t flags) {
    // 0x0040ab07-0x0040ab1c tests bits 0x0100 and 0x0800 together before
    // pumping WaveMix channel 2, which was loaded with WAV/NAV/ZPR.WAV at
    // 0x0040a83b-0x0040a85c.
    return (flags & 0x0900) == 0;
}

bool ShouldPlayNavigationArrivalSound(
    std::uint32_t flags,
    bool initial_scene_entry
) {
    return initial_scene_entry
        ? (flags & 0x0400) != 0
        : (flags & 0x0c00) != 0;
}

std::size_t ConsumeNavigationIdleAnimation(
    std::array<bool, kNavigationIdleAnimationObjects.size()>& used,
    std::size_t initial
) {
    std::size_t selected = initial % used.size();
    while (used[selected]) {
        selected = (selected + 1) % used.size();
    }
    used[selected] = true;
    if (std::all_of(used.begin(), used.end(), [](bool value) { return value; })) {
        used.fill(false);
    }
    return selected;
}

std::optional<SceneDirection> SceneDirectionAtPointer(
    std::uint32_t flags,
    int x,
    int y,
    bool navigation_playback_active
) {
    if (navigation_playback_active) {
        return std::nullopt;
    }
    if (y < 240 && (flags & 0x04) != 0) {
        return SceneDirection::up;
    }
    if (x < 320 && (flags & 0x02) != 0) {
        return SceneDirection::left;
    }
    if (x > 320 && (flags & 0x01) != 0) {
        return SceneDirection::right;
    }
    return std::nullopt;
}

std::size_t SceneNavigator::BranchIndex(SceneDirection direction) {
    switch (direction) {
        case SceneDirection::right:
            return 0;
        case SceneDirection::up:
            return 1;
        case SceneDirection::left:
            return 2;
    }
    return 0;
}

std::uint32_t SceneNavigator::DirectionFlag(SceneDirection direction) {
    switch (direction) {
        case SceneDirection::right:
            return 0x01;
        case SceneDirection::left:
            return 0x02;
        case SceneDirection::up:
            return 0x04;
    }
    return 0;
}

bool SceneNavigator::Reset(
    const SceneDefinition& scene,
    std::size_t variant,
    std::string& error
) {
    scene_ = nullptr;
    variant_ = 0;
    current_index_ = 0;
    error.clear();
    if (variant == 0 || variant > scene.special_object_indices.size()) {
        error = "scene variant is out of range: " + std::to_string(variant);
        return false;
    }
    if (scene.objects.empty()) {
        error = "scene has no objects";
        return false;
    }
    for (std::size_t object_index = 0; object_index < scene.objects.size(); ++object_index) {
        const SceneObject& object = scene.objects[object_index];
        for (const SceneDirection direction : {
                 SceneDirection::left, SceneDirection::up, SceneDirection::right
             }) {
            if ((object.flags & DirectionFlag(direction)) == 0) {
                continue;
            }
            const std::int32_t target = object.links[BranchIndex(direction)];
            if (target < 0 || static_cast<std::size_t>(target) >= scene.objects.size()) {
                error = "object " + std::to_string(object_index) +
                        " has an invalid directional link: " + std::to_string(target);
                return false;
            }
        }
        if ((object.flags & kSceneObjectAutomaticMask) != 0) {
            const std::int32_t target = object.links[1];
            if (target < 0 || static_cast<std::size_t>(target) >= scene.objects.size()) {
                error = "automatic object " + std::to_string(object_index) +
                        " has an invalid forward link: " + std::to_string(target);
                return false;
            }
        }
    }
    const std::int32_t entry = scene.special_object_indices[variant - 1];
    if (entry < 0 || static_cast<std::size_t>(entry) >= scene.objects.size()) {
        error = "scene variant entry is invalid: " + std::to_string(entry);
        return false;
    }
    scene_ = &scene;
    variant_ = variant;
    current_index_ = static_cast<std::size_t>(entry);
    return true;
}

bool SceneNavigator::CanMove(SceneDirection direction) const {
    const SceneObject* object = CurrentObject();
    return object != nullptr && (object->flags & DirectionFlag(direction)) != 0;
}

bool SceneNavigator::Move(
    SceneDirection direction,
    SceneTransition& transition,
    std::string& error
) {
    error.clear();
    const SceneObject* object = CurrentObject();
    if (object == nullptr) {
        error = "scene navigator is not initialized";
        return false;
    }
    if (!CanMove(direction)) {
        return false;
    }
    return FollowBranch(BranchIndex(direction), direction, transition, error);
}

bool SceneNavigator::CanAdvanceAutomatically() const {
    const SceneObject* object = CurrentObject();
    return object != nullptr &&
           (object->flags & kSceneObjectAutomaticMask) != 0;
}

bool SceneNavigator::AdvanceAutomatically(
    SceneTransition& transition,
    std::string& error
) {
    error.clear();
    if (!CanAdvanceAutomatically()) {
        return false;
    }
    return FollowBranch(1, SceneDirection::up, transition, error);
}

bool SceneNavigator::AdvanceMiddleUnchecked(
    SceneTransition& transition,
    std::string& error
) {
    // fcn.0040c0fe command 206 forces +0x5c to zero before entering
    // fcn.0040aa61. The settled path reaches the Up/middle branch directly;
    // the direction flag was checked only by the ordinary mouse hit tester.
    return FollowBranch(1, SceneDirection::up, transition, error);
}

bool SceneNavigator::FollowBranch(
    std::size_t branch,
    SceneDirection direction,
    SceneTransition& transition,
    std::string& error
) {
    error.clear();
    const SceneObject* object = CurrentObject();
    if (object == nullptr) {
        error = "scene navigator is not initialized";
        return false;
    }
    if (branch >= object->links.size()) {
        error = "scene branch is out of range";
        return false;
    }
    const std::int32_t target = object->links[branch];
    if (target < 0 || static_cast<std::size_t>(target) >= scene_->objects.size()) {
        error = "directional link is out of range: " + std::to_string(target);
        return false;
    }
    transition.direction = direction;
    transition.from_object = object->index;
    transition.to_object = scene_->objects[static_cast<std::size_t>(target)].index;
    transition.first_frame = object->ranges[branch][0];
    transition.last_frame = object->ranges[branch][1];
    current_index_ = static_cast<std::size_t>(target);
    return true;
}

const SceneObject* SceneNavigator::CurrentObject() const {
    if (scene_ == nullptr || current_index_ >= scene_->objects.size()) {
        return nullptr;
    }
    return &scene_->objects[current_index_];
}

std::size_t SceneNavigator::Variant() const {
    return variant_;
}

}  // namespace metaverse
