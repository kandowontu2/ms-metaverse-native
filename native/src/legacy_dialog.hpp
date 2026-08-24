#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace metaverse {

// MM.EXE loads AFX_IDS_APP_TITLE (string resource 0xe000) into MFC's global
// application-name slot. CWnd::MessageBox substitutes that value whenever its
// callers pass a null caption.
inline constexpr wchar_t kLegacyDialogCaption[] = L"Ms. Metaverse";
inline constexpr std::uint32_t kLegacyPlainMessageBoxStyle = 0x00;
inline constexpr std::uint32_t kLegacyWarningMessageBoxStyle = 0x30;
inline constexpr std::uint32_t kLegacyConfirmMessageBoxStyle = 0x24;
inline constexpr wchar_t kLegacyBitmapReadFailure[] =
    L"Can't read bitmap file!";

// CDialog vtable slots 31/32 in the original executable are OnOK/OnCancel.
// Keeping their recovered high-level routes in one table prevents the native
// window procedures from accidentally applying one global Enter/Escape policy
// to several deliberately different dialog classes.
enum class LegacyDialogRole : std::uint8_t {
    centered_one_shot,
    cryo,
    talent,
    profile,
    brains,
    navigation_hub,
    generic_media,
    order,
    weight,
    looks,
};

enum class LegacyDialogDefaultAction : std::uint8_t {
    no_op,
    finish_centered_media,
    finish_generic_media_ok,
    finish_generic_media_cancel,
    exit_without_confirmation,
    confirm_exit,
    confirm_then_continue_navigation,
};

struct LegacyDialogDefaultRoutes {
    LegacyDialogDefaultAction on_ok;
    LegacyDialogDefaultAction on_cancel;
};

[[nodiscard]] inline constexpr LegacyDialogDefaultRoutes
LegacyDefaultRoutesFor(LegacyDialogRole role) {
    using Action = LegacyDialogDefaultAction;
    switch (role) {
        case LegacyDialogRole::centered_one_shot:
            return {Action::finish_centered_media, Action::no_op};
        case LegacyDialogRole::profile:
            return {Action::no_op, Action::no_op};
        case LegacyDialogRole::navigation_hub:
            return {Action::exit_without_confirmation, Action::confirm_exit};
        case LegacyDialogRole::generic_media:
            return {
                Action::finish_generic_media_ok,
                Action::finish_generic_media_cancel,
            };
        case LegacyDialogRole::order:
            return {Action::no_op, Action::confirm_then_continue_navigation};
        case LegacyDialogRole::cryo:
        case LegacyDialogRole::talent:
        case LegacyDialogRole::brains:
        case LegacyDialogRole::weight:
        case LegacyDialogRole::looks:
            return {Action::no_op, Action::confirm_exit};
    }
    return {Action::no_op, Action::no_op};
}

enum class LegacyDialogActivationAction : std::uint8_t {
    none,
    repaint_controls,
    finish_generic_media,
    reload_order_phrases,
    reload_weight_gauges,
    show_looks_media,
};

// Recovered WM_ACTIVATEAPP maps. Cryo/Talent/Brains rearm painter flags on
// either edge; ORDER, Weight, and Looks perform their refresh only when the
// process becomes active. The generic media dialog closes on either edge.
[[nodiscard]] inline constexpr LegacyDialogActivationAction
LegacyActivationActionFor(LegacyDialogRole role, bool active) {
    using Action = LegacyDialogActivationAction;
    switch (role) {
        case LegacyDialogRole::cryo:
        case LegacyDialogRole::talent:
        case LegacyDialogRole::brains:
            return Action::repaint_controls;
        case LegacyDialogRole::generic_media:
            return Action::finish_generic_media;
        case LegacyDialogRole::order:
            return active ? Action::reload_order_phrases : Action::none;
        case LegacyDialogRole::weight:
            return active ? Action::reload_weight_gauges : Action::none;
        case LegacyDialogRole::looks:
            return active ? Action::show_looks_media : Action::none;
        case LegacyDialogRole::centered_one_shot:
        case LegacyDialogRole::profile:
        case LegacyDialogRole::navigation_hub:
            return Action::none;
    }
    return Action::none;
}

enum class LegacyDialogCursorAction : std::uint8_t {
    use_window_default,
    preserve_current,
    set_hand_166,
    set_generic_mode_cursor,
    set_looks_child_cursor,
};

// Recovered WM_SETCURSOR routes. Several pavilion/navigation handlers return
// TRUE without changing the cursor because their WM_MOUSEMOVE handlers own the
// directional state. The generic object stores resource 162 for its ordinary
// MMS path, except that mode 4 selects 166 and the separate mode-6 loader also
// installs 166.
[[nodiscard]] inline constexpr LegacyDialogCursorAction
LegacyCursorActionFor(LegacyDialogRole role) {
    using Action = LegacyDialogCursorAction;
    switch (role) {
        case LegacyDialogRole::centered_one_shot:
        case LegacyDialogRole::order:
        case LegacyDialogRole::weight:
            return Action::set_hand_166;
        case LegacyDialogRole::cryo:
        case LegacyDialogRole::talent:
        case LegacyDialogRole::brains:
        case LegacyDialogRole::navigation_hub:
            return Action::preserve_current;
        case LegacyDialogRole::generic_media:
            return Action::set_generic_mode_cursor;
        case LegacyDialogRole::looks:
            return Action::set_looks_child_cursor;
        case LegacyDialogRole::profile:
            return Action::use_window_default;
    }
    return Action::use_window_default;
}

[[nodiscard]] inline constexpr std::uint16_t
LegacyGenericCursorResourceForMode(std::int32_t mode) {
    return mode == 4 || mode == 6 ? 166 : 162;
}

// MM.EXE has no accelerator table. These are the two explicit conveniences
// owned by the native window procedure; keeping modifier recognition here
// makes their exact routes independently testable without manufacturing
// process-global keyboard state in the Win32 UI audit.
enum class NativeShortcutAction : std::uint8_t {
    none,
    toggle_fullscreen,
    grant_credit_cheat,
};

[[nodiscard]] inline constexpr NativeShortcutAction NativeShortcutForKey(
    std::uint32_t virtual_key,
    bool alt_down,
    bool control_down
) {
    if (virtual_key == 0x70 && alt_down && control_down) {  // VK_F1
        return NativeShortcutAction::grant_credit_cheat;
    }
    if (virtual_key == 0x0d && alt_down) {  // VK_RETURN
        return NativeShortcutAction::toggle_fullscreen;
    }
    return NativeShortcutAction::none;
}

std::wstring LegacyBitmapReadFailureMessage(
    std::wstring_view filename = {}
);

}  // namespace metaverse
