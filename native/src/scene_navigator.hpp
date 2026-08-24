#pragma once

#include "game_data.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace metaverse {

enum class SceneDirection {
    left,
    up,
    right,
};

enum class CrossSceneTransition {
    immediate,
    fade_to_white,
};

enum class OneShotNavigationTransition {
    ordinary,
    animated_first_pass,
    held_frame_after_first_pass,
};

// fcn.0040a0db constructs a fresh navigation popup and clears +0x144
// (pending WaveMix reactivation) and +0x148 (the 0x0010 transition-completed
// latch). The cross-scene wrapper at 0x0040cb7d destroys and reconstructs that
// popup, so these fields are scene-popup lifetime rather than document lifetime.
struct NavigationPopupOneShotState {
    bool audio_reactivation_pending = false;
    bool transition_completed = false;

    void ResetForConstruction() noexcept {
        audio_reactivation_pending = false;
        transition_completed = false;
    }
};

enum class NavigationSkipDisposition {
    none,
    complete_transition,
    reveal_slot_result,
};

// fcn.0040a0db appends these six commands, in this order, to the navigation
// popup shown by the WM_RBUTTONDOWN handler at 0x0040aa21.  Command 203 is the
// hidden tally action accepted by the shared dispatcher but is not a menu item.
constexpr std::uint32_t kNavigationMenuTalentCommand = 200;
constexpr std::uint32_t kNavigationMenuBrainsCommand = 201;
constexpr std::uint32_t kNavigationMenuLooksCommand = 202;
// Command 203 is accepted by fcn.0040c12a and maps to owner action 5, but the
// constructor deliberately never appends it to the visible popup.
constexpr std::uint32_t kNavigationMenuTallyCommand = 203;
constexpr std::uint32_t kNavigationMenuSlotsCommand = 204;
constexpr std::uint32_t kNavigationMenuExitCommand = 205;
constexpr std::uint32_t kNavigationMenuSkipCommand = 206;
struct NavigationPopupCommand {
    std::uint32_t command = 0;
    std::wstring_view label;

    bool operator==(const NavigationPopupCommand&) const = default;
};
inline constexpr std::array<NavigationPopupCommand, 6>
    kNavigationPopupCommands{{
        {kNavigationMenuTalentCommand, L"Talent"},
        {kNavigationMenuBrainsCommand, L"Brains"},
        {kNavigationMenuLooksCommand, L"Looks"},
        {kNavigationMenuSlotsCommand, L"Slots"},
        {kNavigationMenuExitCommand, L"Exit"},
        {kNavigationMenuSkipCommand, L"Skip Ahead"},
    }};
constexpr std::size_t kNavigationPopupSkipPosition = 5;
constexpr std::size_t kPrimarySlotShortcutEntry = 29;
constexpr std::size_t kSecondarySlotShortcutEntry = 30;
// Ordinary (non-DEMO) play enters HALL.DAT at entry 13 immediately after the
// intro. Entries 13..16 are the four Hall variants and suppress the popup.
constexpr std::size_t kHallMsMetaverseEntry = 13;
constexpr std::size_t kHallLastEntry = 16;

// The navigation popup stores all parsed object pointers at +0x70.  The four
// pointers read by the delayed 0x0020 animation branch at +0xbc..+0xc8 are
// therefore XR1 object indices 19..22.  Its used bits live separately at
// +0x618 and are cleared after the fourth distinct selection.
constexpr std::array<std::size_t, 4> kNavigationIdleAnimationObjects{
    19, 20, 21, 22
};
constexpr std::uint32_t kNavigationTimerIntervalMs = 500;
constexpr std::uint32_t kNavigationEncounterDelayMs = 1'000;
constexpr std::uint32_t kNavigationIdleAnimationDelayMs = 10'000;
// The palette-to-white handler at 0x0040cc1b executes exactly 0xc7 passes.
// Each pass visits all 256 palette entries before realizing the palette.
constexpr std::uint32_t kNavigationPaletteFadePasses = 199;

// The timer body uses unsigned elapsed comparisons followed by JBE. A trigger
// at exactly 1000/10000 ms is therefore still early and waits for the next
// 500 ms navigation-timer callback.
bool NavigationTimerThresholdPassed(
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point threshold
) noexcept;

// The right-click handler at 0x0040aa21 latches +0x628 before displaying the
// popup. The 500 ms navigation timer keeps postponing the 0x0020 idle movie
// while that field is set. fcn.0040aa61 clears it only when another
// transition (including command-206 Skip) is requested.
class NavigationPopupIdleLatch {
public:
    void MarkPopupRequested() noexcept { popup_requested_ = true; }
    void ResetForTransition() noexcept { popup_requested_ = false; }
    [[nodiscard]] bool SuppressesIdleAnimation() const noexcept {
        return popup_requested_;
    }

private:
    bool popup_requested_ = false;
};

// The wrapper at 0x0040c88c/0x0040cb7d sets the navigation object's 0x14c
// suppression field only for NAVIGATE.DAT entries 13 through 16 (the Hall).
bool NavigationContextMenuSuppressed(std::size_t navigation_entry);
std::size_t NavigationSlotShortcutEntry(bool secondary_asset_scene);

// Popup command 206 is enabled from the MCI-active field at +0x118. During
// Slots, interrupting that notified play posts the shared completion handler;
// +0x140 then makes that handler reveal the result immediately.
NavigationSkipDisposition NavigationSkipDispositionForState(
    bool pending_navigation_resolution,
    bool slot_spin_active,
    bool bounded_range_active
);
bool NavigationSkipAheadEnabled(
    bool pending_navigation_resolution,
    bool slot_spin_active,
    bool bounded_range_active
);

// The navigation class message map at 0x00431598 binds WM_LBUTTONDOWN
// directly to fcn.0040aa61. That handler tests +0x118 before pointer direction,
// so any left click interrupts an active ordinary/idle/Slots MCI range through
// the same completion path used by popup command 206.
bool NavigationLeftButtonInterruptsPlayback(
    bool pending_navigation_resolution,
    bool slot_spin_active,
    bool bounded_range_active
);

// DAT flag 0x1000 changes the parent-dialog message from 0x40b to 0x40c.
// The latter runs the original palette-to-white transition before loading the
// target.  It does not alter the target entry or test which CD is mounted.
CrossSceneTransition CrossSceneTransitionForFlags(std::uint32_t flags);

// Convert the native fade's normalized time into the number of completed
// original palette passes. A packed color is 0xAARRGGBB, matching the DWORD
// view of a little-endian 32-bit BGRA DIB pixel. The legacy loop increments
// each channel by ten while it is below 246; its two saturation branches
// accidentally force red to 255 instead of changing green/blue. Preserve that
// observable off-white endpoint rather than replacing it with a linear blend.
std::uint32_t NavigationPaletteFadePassForProgress(double progress) noexcept;
std::uint32_t ApplyNavigationPaletteFadeToColor(
    std::uint32_t packed_bgra,
    std::uint32_t completed_passes
) noexcept;

// XR2.DAT uses the literal token "0" for one 0x0400 automatic node. The
// original constructor checks only the token's first byte, so every token
// beginning with '0' is silent even though the supplied corpus uses just "0".
bool IsPlayableNavigationAsset(std::string_view asset);

// Scene construction at 0x0040a870 invokes the middle-link transition only
// when the selected entry object has 0x0100. The later completion handler also
// routes 0x0800 and 0x2000, but those bits do not auto-advance an initial entry.
bool ShouldAdvanceNavigationOnInitialSceneEntry(std::uint32_t flags);

// The loader zeros +0x578 + 4*n once for each object it parses. This state is
// therefore rearmed for the new DAT's ordinal prefix on every cross-scene load,
// not merely when the outer navigation popup is first constructed.
void ResetNavigationEncounterFlagsForScene(
    std::array<bool, 256>& encountered,
    std::size_t object_count
);

// MCI accepts a Play-To position equal to the stream length as an EOF stop.
// FFmpeg exposes zero-based frames, so only that container boundary is clamped
// to the final decodable frame. A nonpositive count means "unknown".
std::int64_t ClampNavigationPlaybackLastFrame(
    std::int64_t requested_last_frame,
    std::int64_t frame_count
);

// Recovered from fcn.0040aa61/fcn.0040b4e7. A 0x0010 automatic node plays
// its stored transition once while WaveMix is inactive. Later visits jump to
// the destination object's middle-range start frame. A 0x0020 arrival
// reactivates WaveMix and seeks the navigation background to frame 25.
OneShotNavigationTransition OneShotTransitionForFlags(
    std::uint32_t flags,
    bool first_pass_completed
);
std::optional<std::int32_t> NavigationArrivalHoldFrame(std::uint32_t flags);
std::int32_t RepeatedOneShotDestinationFrame(const SceneObject& destination);

// fcn.0040aa61 plays the preloaded ZPR.WAV WaveMix channel before a
// user-directed departure unless the source is an automatic 0x0100 node or a
// 0x0800 sound-bearing node. The latter preserves that node's own channel.
bool ShouldPlayNavigationDepartureSound(std::uint32_t flags);

// Scene construction at fcn.0040a7fa checks only 0x0400 on its selected entry
// object. The post-transition handler later accepts both 0x0400 and 0x0800.
bool ShouldPlayNavigationArrivalSound(
    std::uint32_t flags,
    bool initial_scene_entry
);

// Consume the original four-entry no-repeat pool.  A used initial draw walks
// forward with wraparound; once the fourth distinct entry is selected, all
// four bits are cleared immediately for the next cycle.
std::size_t ConsumeNavigationIdleAnimation(
    std::array<bool, kNavigationIdleAnimationObjects.size()>& used,
    std::size_t initial
);

// Recovered from fcn.0040af01. Active MCI playback forces neutral cursor state
// 4. Otherwise Up has priority across the top half and the remaining canvas is
// split exactly at x=320 for left/right.
std::optional<SceneDirection> SceneDirectionAtPointer(
    std::uint32_t flags,
    int x,
    int y,
    bool navigation_playback_active = false
);

struct SceneTransition {
    SceneDirection direction = SceneDirection::up;
    std::int32_t from_object = 0;
    std::int32_t to_object = 0;
    std::int32_t first_frame = 0;
    std::int32_t last_frame = 0;
};

class SceneNavigator {
public:
    bool Reset(
        const SceneDefinition& scene,
        std::size_t variant,
        std::string& error
    );
    bool CanMove(SceneDirection direction) const;
    bool Move(SceneDirection direction, SceneTransition& transition, std::string& error);
    bool CanAdvanceAutomatically() const;
    bool AdvanceAutomatically(SceneTransition& transition, std::string& error);
    // Popup command 206 writes direction state zero and calls the shared
    // transition executor even if its enabled-state snapshot has gone stale.
    // Once playback is settled, that executor follows the middle range/link
    // without rechecking either the Up or automatic-object flags.
    bool AdvanceMiddleUnchecked(SceneTransition& transition, std::string& error);
    const SceneObject* CurrentObject() const;
    std::size_t Variant() const;

private:
    static std::size_t BranchIndex(SceneDirection direction);
    static std::uint32_t DirectionFlag(SceneDirection direction);
    bool FollowBranch(
        std::size_t branch,
        SceneDirection direction,
        SceneTransition& transition,
        std::string& error
    );

    const SceneDefinition* scene_ = nullptr;
    std::size_t variant_ = 0;
    std::size_t current_index_ = 0;
};

}  // namespace metaverse
