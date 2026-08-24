#include "audio_player.hpp"
#include "category_weight.hpp"
#include "comment_order.hpp"
#include "cryo_media.hpp"
#include "cryo_pavilion.hpp"
#include "game_data.hpp"
#include "game_state.hpp"
#include "host_interstitial.hpp"
#include "host_reaction.hpp"
#include "judging_pavilion.hpp"
#include "legacy_dialog.hpp"
#include "legacy_random.hpp"
#include "looks_pavilion.hpp"
#include "media_timing.hpp"
#include "motion_player.hpp"
#include "pavilion_rotation.hpp"
#include "profile_dialog.hpp"
#include "profile_store.hpp"
#include "scene_navigator.hpp"
#include "slot_machine.hpp"
#include "video_decoder.hpp"

#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Scene {
    const wchar_t* name;
    const wchar_t* bitmap;
    const wchar_t* video;
    const wchar_t* data;
    const wchar_t* sound;
    std::size_t variant;
};

constexpr Scene kScenes[] = {
    {L"Introduction", nullptr, L"MOV\\INTRO\\MMINTRO.AVI", nullptr, nullptr, 0},
    {L"Cryo chamber", L"BMP\\CRYO\\CRYO.BMP", nullptr, nullptr, L"WAV\\CRYO.WAV", 0},
    {L"Category weights", L"BMP\\WEIGHT\\WEIGHT.BMP", nullptr, nullptr, nullptr, 0},
    {L"Comments", L"BMP\\ORDER\\ORDER.BMP", nullptr, nullptr, nullptr, 0},
    {L"Brains pavilion", nullptr, nullptr, L"DAT\\NAV\\BRAINS.DAT", L"WAV\\BRAINS.WAV", 1},
    {L"Brains judging", L"BMP\\BRAIN\\BRAIN.BMP", nullptr, nullptr, nullptr, 0},
    {L"Looks pavilion", nullptr, nullptr, L"DAT\\NAV\\LOOKS.DAT", L"WAV\\LOOKS.WAV", 1},
    {L"Looks judging", L"BMP\\LOOK\\LOOK.BMP", nullptr, nullptr, nullptr, 0},
    {L"Hall", nullptr, nullptr, L"DAT\\NAV\\HALL.DAT", nullptr, 1},
    {L"Crossroads I", nullptr, nullptr, L"DAT\\NAV\\XR1.DAT", nullptr, 1},
    {L"Slots", nullptr, nullptr, nullptr, nullptr, 0},
    {L"Talent pavilion", nullptr, nullptr, L"DAT\\NAV\\TALNAV.DAT", L"WAV\\TALENT.WAV", 1},
    {L"Talent judging", L"BMP\\TALENT\\TALENT.BMP", nullptr, nullptr, nullptr, 0},
    {L"Crossroads II", nullptr, nullptr, L"DAT\\NAV\\XR2.DAT", nullptr, 1},
    {L"Tally machine", nullptr, nullptr, L"DAT\\NAV\\TALLY.DAT", nullptr, 1},
};

constexpr int kIntroScene = 0;
constexpr int kCryoScene = 1;
constexpr int kWeightScene = 2;
constexpr int kCommentsScene = 3;
constexpr int kBrainsJudgingScene = 5;
constexpr int kLooksJudgingScene = 7;
constexpr int kCrossroadsScene = 9;
constexpr int kSlotsScene = 10;
constexpr int kTalentPavilionScene = 11;
constexpr int kTalentJudgingScene = 12;
constexpr int kCrossroads2Scene = 13;
constexpr int kTallyScene = 14;
struct HostCue {
    std::int32_t channel;
    std::int32_t take;
};

std::optional<HostCue> HostEntryCueForScene(int scene) {
    switch (scene) {
        case kCryoScene:
            return HostCue{1, 1};
        case kTalentJudgingScene:
            return HostCue{2, 1};
        case kBrainsJudgingScene:
            return HostCue{3, 1};
        case kLooksJudgingScene: {
            const auto dispatch = metaverse::PlanLooksC41MediaDispatch();
            return HostCue{dispatch.primary_index, dispatch.selector};
        }
        case kCommentsScene:
            return HostCue{5, 1};
        case kWeightScene:
            return HostCue{9, 1};
        default:
            return std::nullopt;
    }
}

std::chrono::milliseconds HostEntryDelayForScene(int scene) {
    // ORDER alone uses a two-second timer (0x0040f452-0x0040f45e).
    // Cryo, Weight, and all three judging dialogs use 500 ms timer ID 1.
    return scene == kCommentsScene
        ? std::chrono::milliseconds(2000)
        : std::chrono::milliseconds(500);
}

enum class JudgingDisposition {
    rated,
    gonged,
    disqualified,
};

enum class JudgingAction : std::uint8_t {
    none,
    accept,
    gong,
    penalty,
    rotate,
};

std::optional<metaverse::LooksControlAction> LooksControlForJudgingAction(
    JudgingAction action
) {
    switch (action) {
        case JudgingAction::accept:
            return metaverse::LooksControlAction::accept;
        case JudgingAction::penalty:
            return metaverse::LooksControlAction::penalty;
        case JudgingAction::rotate:
            return metaverse::LooksControlAction::rotate;
        case JudgingAction::none:
        case JudgingAction::gong:
            return std::nullopt;
    }
    return std::nullopt;
}

struct PendingPenaltyAdvance {
    metaverse::JudgingCategory category;
    bool play_first_host;
};

struct PendingHostJudgingAdvance {
    metaverse::JudgingCategory category;
    JudgingDisposition disposition;
    bool complete_accepted_contestant = false;
};

struct PendingJudgingEntryContinuation {
    metaverse::JudgingCategory category;
    std::chrono::steady_clock::time_point due;
};

using CenteredOneShotKind = metaverse::CenteredOneShotPurpose;

struct PendingCenteredOneShotStart {
    CenteredOneShotKind kind;
    std::filesystem::path root;
    std::filesystem::path relative;
    std::chrono::steady_clock::time_point due;
};

// Talent and Brains share the control layout built by 0x00406ada and
// 0x00408cea. Looks uses the rectangles built by 0x00412d8b. The overlay
// bitmap is the pressed state; the unpressed control is already in the base
// pavilion bitmap.
constexpr RECT kWideJudgingMeterRect{70, 67, 130, 288};
constexpr RECT kWideJudgingAcceptRect{25, 368, 161, 460};
constexpr RECT kWideJudgingGongRect{547, 164, 619, 194};
constexpr RECT kWideJudgingPenaltyRect{534, 218, 635, 276};
constexpr RECT kWidePerformanceRepaintRect{
    metaverse::kLegacyWidePerformanceRepaintBounds[0],
    metaverse::kLegacyWidePerformanceRepaintBounds[1],
    metaverse::kLegacyWidePerformanceRepaintBounds[2],
    metaverse::kLegacyWidePerformanceRepaintBounds[3],
};
constexpr RECT kLooksJudgingMeterRect{
    metaverse::kLooksMeterRect.left,
    metaverse::kLooksMeterRect.top,
    metaverse::kLooksMeterRect.right,
    metaverse::kLooksMeterRect.bottom,
};
constexpr RECT kLooksJudgingAcceptRect{
    metaverse::kLooksAcceptRect.left,
    metaverse::kLooksAcceptRect.top,
    metaverse::kLooksAcceptRect.right,
    metaverse::kLooksAcceptRect.bottom,
};
constexpr RECT kLooksJudgingPenaltyRect{
    metaverse::kLooksPenaltyRect.left,
    metaverse::kLooksPenaltyRect.top,
    metaverse::kLooksPenaltyRect.right,
    metaverse::kLooksPenaltyRect.bottom,
};
constexpr RECT kLooksJudgingRotateRect{
    metaverse::kLooksRotateRect.left,
    metaverse::kLooksRotateRect.top,
    metaverse::kLooksRotateRect.right,
    metaverse::kLooksRotateRect.bottom,
};

std::optional<RECT> JudgingActionRect(
    metaverse::JudgingCategory category,
    JudgingAction action
) {
    const bool looks = category == metaverse::JudgingCategory::looks;
    switch (action) {
        case JudgingAction::accept:
            return looks ? kLooksJudgingAcceptRect : kWideJudgingAcceptRect;
        case JudgingAction::gong:
            return looks ? std::nullopt
                         : std::optional<RECT>(kWideJudgingGongRect);
        case JudgingAction::penalty:
            return looks ? kLooksJudgingPenaltyRect : kWideJudgingPenaltyRect;
        case JudgingAction::rotate:
            return looks ? std::optional<RECT>(kLooksJudgingRotateRect)
                         : std::nullopt;
        case JudgingAction::none:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ExecutableDirectory() {
    std::vector<wchar_t> buffer(32'768);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size())
    );
    if (length == 0 || length >= buffer.size()) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer.data(), buffer.data() + length)
        .parent_path();
}

std::optional<std::filesystem::path> FindAssets(int argc, wchar_t** argv) {
    const auto valid = [](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate / L"BMP\\CRYO\\CRYO.BMP") &&
               std::filesystem::exists(candidate / L"MOV\\INTRO\\MMINTRO.AVI");
    };
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring_view(argv[index]) == L"--assets") {
            const std::filesystem::path candidate(argv[index + 1]);
            return valid(candidate)
                ? std::optional(std::filesystem::absolute(candidate))
                : std::nullopt;
        }
    }
    if (const wchar_t* environment = _wgetenv(L"MS_METAVERSE_ASSETS")) {
        const std::filesystem::path candidate(environment);
        if (valid(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }
    if (const auto executable_directory = ExecutableDirectory()) {
        const auto candidate = *executable_directory / L"assets";
        if (valid(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }
    for (const auto& candidate : {
             std::filesystem::path(L"assets"),
             std::filesystem::path(L"extracted\\iso"),
             std::filesystem::path(L"..\\extracted\\iso"),
             std::filesystem::path(L"..\\..\\extracted\\iso"),
         }) {
        if (valid(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> FindAssets2(int argc, wchar_t** argv) {
    const auto valid = [](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate / L"MOV\\TALENT\\T11.AVI") &&
               std::filesystem::exists(candidate / L"MOV\\TALLY\\W1.AVI") &&
               std::filesystem::exists(candidate / L"MOV\\CREDIT\\CREDIT.AVI") &&
               std::filesystem::exists(candidate / L"DAT\\NAV\\TALNAV.DAT");
    };
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring_view(argv[index]) == L"--assets2") {
            const std::filesystem::path candidate(argv[index + 1]);
            return valid(candidate)
                ? std::optional(std::filesystem::absolute(candidate))
                : std::nullopt;
        }
    }
    if (const wchar_t* environment = _wgetenv(L"MS_METAVERSE_ASSETS2")) {
        const std::filesystem::path candidate(environment);
        if (valid(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }
    if (const auto executable_directory = ExecutableDirectory()) {
        const auto candidate = *executable_directory / L"assets2";
        if (valid(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }
    for (const auto& candidate : {
             std::filesystem::path(L"assets2"),
             std::filesystem::path(L"extracted\\iso2"),
             std::filesystem::path(L"..\\extracted\\iso2"),
             std::filesystem::path(L"..\\..\\extracted\\iso2"),
         }) {
        if (valid(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }
    return std::nullopt;
}

bool HasArgument(int argc, wchar_t** argv, std::wstring_view argument) {
    for (int index = 1; index < argc; ++index) {
        if (std::wstring_view(argv[index]) == argument) {
            return true;
        }
    }
    return false;
}

std::filesystem::path FindProfileDataPath(int argc, wchar_t** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring_view(argv[index]) == L"--profiles") {
            return std::filesystem::absolute(std::filesystem::path(argv[index + 1]));
        }
    }
    if (const wchar_t* environment = _wgetenv(L"MS_METAVERSE_PROFILES")) {
        return std::filesystem::absolute(std::filesystem::path(environment));
    }
    if (const wchar_t* local_app_data = _wgetenv(L"LOCALAPPDATA")) {
        return std::filesystem::path(local_app_data) /
               L"Ms Metaverse Native" / L"MM.DAT";
    }
    return std::filesystem::absolute(L"MM.DAT");
}

class Application {
public:
    Application(
        std::filesystem::path assets,
        std::optional<std::filesystem::path> assets2,
        std::filesystem::path profile_path,
        std::vector<metaverse::PlayerProfile> profiles,
        metaverse::PlayerProfile player,
        metaverse::PavilionRotationState pavilion_rotation,
        metaverse::HostReactionState host_reaction_state,
        bool profile_selection_pending
    ) : assets_(std::move(assets)),
        assets2_(std::move(assets2)),
        profile_path_(std::move(profile_path)),
        profiles_(std::move(profiles)),
        player_(std::move(player)),
        pavilion_rotation_(std::move(pavilion_rotation)),
        host_reaction_state_(std::move(host_reaction_state)),
        profile_selection_pending_(profile_selection_pending),
        random_(static_cast<std::uint32_t>(std::time(nullptr))) {
        const HINSTANCE module = GetModuleHandleW(nullptr);
        navigation_up_cursor_ = LoadCursorW(module, MAKEINTRESOURCEW(160));
        navigation_neutral_cursor_a_ = LoadCursorW(module, MAKEINTRESOURCEW(161));
        generic_target_cursor_ = LoadCursorW(module, MAKEINTRESOURCEW(162));
        navigation_right_cursor_ = LoadCursorW(module, MAKEINTRESOURCEW(163));
        navigation_left_cursor_ = LoadCursorW(module, MAKEINTRESOURCEW(164));
        navigation_neutral_cursor_b_ = LoadCursorW(module, MAKEINTRESOURCEW(165));
        default_hand_cursor_ = LoadCursorW(module, MAKEINTRESOURCEW(166));
        pavilion_exit_cursor_ = LoadCursorW(module, MAKEINTRESOURCEW(174));
        looks_magnifier_cursor_ = LoadCursorW(module, MAKEINTRESOURCEW(181));
        round_.credits = player_.credits;
        round_.judge_cadet_slot = random_.Modulo(
            metaverse::kContestantsPerRound
        );
        round_.judge_cadet_category = static_cast<metaverse::JudgingCategory>(
            random_.Modulo(metaverse::kJudgingCategoryCount)
        );
        // 0x00401b38-0x00401b5d installs the DEMO roster unconditionally,
        // before MMINTRO.AVI and before the player-name branch. Ordinary play
        // replaces all five slots through Cryo; DEMO keeps these values.
        for (std::size_t index = 0; index < round_.contestants.size(); ++index) {
            round_.contestants[index].contestant_id =
                metaverse::kDemoContestantIds[index];
        }
        round_.comment_for_rating =
            metaverse::kOrchestratorInitialCommentMapping;
        // fcn.00401b03 installs the raw 5/5/5 document values before intro.
        // The Weight dialog normalizes them only for an ordinary game; DEMO
        // deliberately carries the unnormalized values into navigation.
        round_.weights = {5, 5, 5};
    }

    ~Application() {
        ResetHostInterstitial();
        if (bitmap_ != nullptr) {
            DeleteObject(bitmap_);
        }
        if (background_bitmap_ != nullptr) {
            DeleteObject(background_bitmap_);
        }
        ResetLooksAssets();
        ResetJudgingBitmaps();
        ResetCryoAssets();
        for (HBITMAP slot_bitmap : slot_bitmaps_) {
            if (slot_bitmap != nullptr) {
                DeleteObject(slot_bitmap);
            }
        }
        if (sprite_bitmap_ != nullptr) {
            DeleteObject(sprite_bitmap_);
        }
        ResetNavigationOverlay();
        ResetCommentBitmaps();
        ResetWeightBitmaps();
        PlaySoundW(nullptr, nullptr, 0);
    }

    bool Persist(std::string& error) {
        error.clear();
        if (metaverse::IsPersistentProfile(player_)) {
            player_.credits = round_.credits;
            if (metaverse::PlayerProfile* stored =
                    metaverse::FindProfile(profiles_, player_.name)) {
                *stored = player_;
            } else {
                profiles_.push_back(player_);
            }
            if (!metaverse::SaveLegacyProfile(profile_path_, player_, error)) {
                return false;
            }
        }
        // nrfPav.dat and hnrd.dat are install-global tables updated at their
        // actual draw points. The original profile-save routine does not
        // rewrite or create them merely because the process is exiting.
        return true;
    }

    void InitializeWindow(HWND window) {
        owner_window_ = window;
        // The owning controller calls fcn.0040291f at 0x00401a19 before it
        // calls the game orchestrator at 0x00401a33. The orchestrator then
        // opens MMINTRO.AVI at 0x00401bc1. Select/authenticate the profile
        // first, but keep both modal UI and playback out of the constructor so
        // neither can precede the native owner window's WM_CREATE.
        if (!CompleteProfileSelection(window)) {
            return;
        }
        LoadScene();
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    bool ToggleFullscreen(HWND window) {
        if (!fullscreen_active_) {
            RECT bounds{};
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            const HMONITOR target = MonitorFromWindow(
                window, MONITOR_DEFAULTTONEAREST
            );
            if (!GetWindowRect(window, &bounds) || target == nullptr ||
                !GetMonitorInfoW(target, &monitor)) {
                return false;
            }
            windowed_bounds_ = bounds;
            fullscreen_active_ = true;
            SetWindowPos(
                window,
                HWND_TOP,
                monitor.rcMonitor.left,
                monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left,
                monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER
            );
        } else {
            fullscreen_active_ = false;
            SetWindowPos(
                window,
                HWND_TOP,
                windowed_bounds_.left,
                windowed_bounds_.top,
                windowed_bounds_.right - windowed_bounds_.left,
                windowed_bounds_.bottom - windowed_bounds_.top,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER
            );
        }
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        return true;
    }

    void ApplyCreditCheat(HWND window) {
        metaverse::ApplyNativeCreditCheat(round_);
        // Every cash-bearing scene reads the live document balance during
        // WM_PAINT. A full repaint also handles navigation/slot composites and
        // makes the new amount visible immediately wherever cash is shown.
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
    }

    static RECT LogicalRectToClient(HWND window, const RECT& logical) {
        RECT client{};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        if (client_width <= 0 || client_height <= 0) {
            return logical;
        }
        const double scale = std::min(
            static_cast<double>(client_width) / 640.0,
            static_cast<double>(client_height) / 480.0
        );
        const int drawn_width = static_cast<int>(std::lround(640.0 * scale));
        const int drawn_height = static_cast<int>(std::lround(480.0 * scale));
        const int content_x = (client_width - drawn_width) / 2;
        const int content_y = (client_height - drawn_height) / 2;
        return {
            content_x + static_cast<LONG>(std::lround(logical.left * scale)),
            content_y + static_cast<LONG>(std::lround(logical.top * scale)),
            content_x + static_cast<LONG>(std::lround(logical.right * scale)),
            content_y + static_cast<LONG>(std::lround(logical.bottom * scale)),
        };
    }

    static void InvalidateLogicalRect(
        HWND window, const RECT& logical, BOOL erase = FALSE
    ) {
        const RECT client = LogicalRectToClient(window, logical);
        InvalidateRect(window, &client, erase);
    }

    bool CompleteProfileSelection(HWND window) {
        if (!profile_selection_pending_) {
            return true;
        }
        if (profile_selection_in_progress_) {
            return false;
        }
        profile_selection_in_progress_ = true;
        metaverse::PlayerProfile selected;
        std::string error;
        if (!metaverse::ChoosePlayerProfile(
                GetModuleHandleW(nullptr),
                window,
                assets_,
                profiles_,
                selected,
                error
            )) {
            profile_selection_in_progress_ = false;
            profile_selection_pending_ = false;
            if (!error.empty()) {
                const std::wstring message(error.begin(), error.end());
                MessageBoxW(
                    window, message.c_str(),
                    L"Ms. Metaverse Native — Profile Error",
                    MB_OK | MB_ICONERROR
                );
            }
            DestroyWindow(window);
            return false;
        }
        player_ = std::move(selected);
        round_.credits = player_.credits;
        profile_selection_in_progress_ = false;
        profile_selection_pending_ = false;
        return true;
    }

    void NextScene(int delta, HWND window) {
        navigation_hub_active_ = false;
        pending_navigation_resolution_ = false;
        pending_navigation_fade_target_.reset();
        const auto previous_category = JudgingCategoryForScene(scene_);
        const int count = static_cast<int>(std::size(kScenes));
        scene_ = (scene_ + delta + count) % count;
        const auto next_category = JudgingCategoryForScene(scene_);
        if (next_category && (!previous_category || *previous_category != *next_category)) {
            judging_contestant_ = 0;
            judging_contestant_active_ = false;
            current_rating_ = 0;
        }
        LoadScene();
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    bool TearDownCurrentPerformance(HWND window) {
        // fcn.00407c38 / fcn.00409e76 destroy the active 320x240 movie
        // child, return whether one existed, and force its rectangle to repaint
        // before Gong/Penalty mutates state or starts a synchronous sound.
        const bool had_current_performance =
            metaverse::LegacyWidePerformanceChildIsPresent(
                active_performance_category_.has_value(),
                video_decoder_.IsOpen()
            );
        ResetBitmap();
        video_decoder_.Close();
        audio_player_.Stop();
        active_performance_category_.reset();
        video_paused_ = true;
        playback_range_.reset();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;
        InvalidateLogicalRect(window, kWidePerformanceRepaintRect);
        UpdateWindow(window);
        return had_current_performance;
    }

    void FinishCurrentPerformance(
        HWND window,
        std::wstring failure = {}
    ) {
        if (!active_performance_category_) {
            return;
        }
        const auto category = *active_performance_category_;

        // The MM_MCINOTIFY handlers at 0x0040672c / 0x00408913 destroy the
        // completed movie child through the same exact rectangle-repaint
        // helper used by manual interruption, then restore the pavilion loop.
        TearDownCurrentPerformance(window);
        const auto ambient = PavilionAmbientSound(category);
        PlaySoundW(
            ambient.c_str(), nullptr,
            SND_FILENAME | SND_ASYNC | SND_LOOP
        );
        status_ = std::move(failure);
        UpdateTitle(window);
    }

    void FinishPenaltyResponseMedia() {
        // The synchronous TP/BP/LP helper destroys its temporary MCI child on
        // normal completion, cancel, and failure. In Looks this immediately
        // reveals the still-existing (but stopped) ROTATE child beneath it.
        ResetBitmap();
        video_decoder_.Close();
        audio_player_.Stop();
        video_paused_ = true;
        playback_range_.reset();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;
    }

    void RestoreLooksAfterPenaltyResponse(HWND window) {
        // After the temporary LP child is destroyed, 0x00413fbb-0x00414001
        // repaints the broad meter backing and then shows the still-existing,
        // stopped ROTATE child. In the retained compositor, showing that child
        // also requires repainting its presentation rectangle.
        RECT meter{170, 100, 490, 340};
        InvalidateLogicalRect(window, meter);
        UpdateWindow(window);
        ApplyLooksPresentationAction(
            metaverse::LooksPresentationAction::show_rotate
        );
        RepaintLooksPresentationRect(window);
    }

    // Mouse handlers translate the original on-screen controls into this
    // compact action vocabulary. It is intentionally not called from
    // WM_KEYDOWN: MM.EXE has no accelerator resource, and its recovered game
    // screens require their painted control hit/release paths.
    bool HandleControlAction(WPARAM key, HWND window) {
        if (host_interstitial_active_) {
            return true;
        }
        if (slot_spin_active_) {
            return true;
        }
        if (const auto category = JudgingCategoryForScene(scene_)) {
            const std::size_t category_index = static_cast<std::size_t>(*category);
            const bool legacy_looks_action_without_selection =
                *category == metaverse::JudgingCategory::looks &&
                !judging_contestant_active_ &&
                (key == VK_RETURN || key == 'X' || key == 'R');
            if (!judging_contestant_active_ &&
                !legacy_looks_action_without_selection) {
                if (key == VK_UP || key == VK_DOWN || key == VK_RETURN ||
                    key == 'G' || key == 'X' || key == 'R') {
                    status_ = L"Select an unfinished contestant portrait first.";
                    UpdateTitle(window);
                    return true;
                }
                return false;
            }
            if (pending_penalty_advance_) {
                status_ = L"The original Penalty Box response is still playing.";
                UpdateTitle(window);
                InvalidateRect(window, nullptr, FALSE);
                return true;
            }
            if (!performance_access_granted_ &&
                !legacy_looks_action_without_selection &&
                (key == VK_RETURN || key == 'G' || key == 'X' || key == 'R')) {
                status_ = L"Not enough cash to pay this performance's ten-percent surcharge.";
                UpdateTitle(window);
                InvalidateRect(window, nullptr, FALSE);
                return true;
            }
            if (key == VK_UP || key == VK_DOWN) {
                current_rating_ = std::clamp(
                    current_rating_ + (key == VK_UP ? 1 : -1), 0, 10
                );
                judging_comment_previewed_ = false;
                status_ = L"Rating set to " + std::to_wstring(current_rating_) +
                          L"; press Enter to accept.";
                LoadJudgingRatingBitmap(*category);
            } else if (key == VK_RETURN) {
                const std::int32_t reaction_score = current_rating_;
                judging_comment_previewed_ = false;
                const bool looks_deferred_completion =
                    *category == metaverse::JudgingCategory::looks;
                // Talent/Brains replace DIMMED and commit before the modal X
                // reaction (0x004076a2 / 0x004098d0). Looks does the reverse:
                // 0x00413c1b runs X first, then commits/dims on return.
                if (!looks_deferred_completion) {
                    CompleteAcceptedContestant(*category, window);
                    // +0xac is cleared before fcn.00407bc1 / 0x00409e37
                    // enters the modal X dispatcher. The local pending bit is
                    // already zero, so the retained portrait also loses its
                    // active red frame before the overlay begins.
                    judging_contestant_active_ = false;
                    performance_access_granted_ = false;
                }
                const std::int32_t reaction_selector =
                    looks_deferred_completion
                    ? metaverse::PlanLooksAcceptMediaDispatch(reaction_score)
                          .selector
                    : reaction_score;
                if (StartHostReaction(
                        *category, reaction_selector, window,
                        looks_deferred_completion
                    )) {
                    return true;
                }
                if (looks_deferred_completion) {
                    CompleteAcceptedContestant(*category, window);
                }
                AdvanceJudging(
                    *category, window, JudgingDisposition::rated
                );
                return true;
            } else if (key == 'G') {
                if (*category == metaverse::JudgingCategory::looks) {
                    status_ = L"The original Looks pavilion has no gong; use the Judge-O-Matic or Penalty Box.";
                    UpdateTitle(window);
                    InvalidateRect(window, nullptr, FALSE);
                    return true;
                }
                const bool interrupted_performance =
                    TearDownCurrentPerformance(window);
                const auto& gonged_contestant =
                    round_.contestants[judging_contestant_];
                // 0x00407a74 / 0x00409cae replace GONG and present any load
                // warning before the local pending and document score writes.
                ReplaceJudgingPortraitBitmap(
                    *category,
                    judging_contestant_,
                    std::filesystem::path(L"BMP\\GONG") /
                        (std::to_wstring(gonged_contestant.contestant_id) +
                         L".BMP")
                );
                gonged_[category_index][judging_contestant_] = true;
                metaverse::CompletePavilionPendingSlot(
                    judging_visit_, judging_contestant_
                );
                round_.contestants[judging_contestant_].ratings[
                    category_index
                ] = 0;
                RepaintWidePavilionResult(
                    window, judging_contestant_
                );
                wide_rating_visible_ = true;
                // 0x00407b77 / 0x00409db7 clear the active slot after the
                // GONG portrait repaint and before GONG.WAV or C61.
                judging_contestant_active_ = false;
                performance_access_granted_ = false;
                const auto gong = JudgingAssetsRoot(*category) /
                    L"WAV" / L"GONG.WAV";
                PlaySoundW(
                    gong.c_str(), nullptr,
                    SND_FILENAME | SND_SYNC | SND_NODEFAULT
                );
                if ((*category == metaverse::JudgingCategory::brains ||
                     *category == metaverse::JudgingCategory::talent) &&
                    !interrupted_performance &&
                    !first_gong_host_played_[category_index]) {
                    if (StartJudgingEventHost(
                            HostCue{6, 1}, *category,
                            JudgingDisposition::gonged, window
                        )) {
                        first_gong_host_played_[category_index] = true;
                        return true;
                    }
                    first_gong_host_played_[category_index] = true;
                }
                AdvanceJudging(
                    *category, window, JudgingDisposition::gonged
                );
                return true;
            } else if (key == 'X') {
                const bool correct =
                    judging_contestant_ == round_.judge_cadet_slot;
                const auto& root = JudgingAssetsRoot(*category);
                bool interrupted_performance = false;
                if (*category == metaverse::JudgingCategory::brains ||
                    *category == metaverse::JudgingCategory::talent) {
                    // fcn.004077d5 / fcn.00409a03 call their current-movie
                    // teardown before branching on the accusation result.
                    // Thus even WRONG5 leaves the 208,108 performance child
                    // absent; only Looks keeps its current presentation.
                    interrupted_performance =
                        TearDownCurrentPerformance(window);
                    RECT credits_rect{
                        metaverse::kWidePavilionCreditsDirtyBounds[0],
                        metaverse::kWidePavilionCreditsDirtyBounds[1],
                        metaverse::kWidePavilionCreditsDirtyBounds[2],
                        metaverse::kWidePavilionCreditsDirtyBounds[3],
                    };
                    // The original queues this region before REWARD1/WRONG5.
                    // Their synchronous playback delays its paint until after
                    // the following balance mutation.
                    InvalidateLogicalRect(window, credits_rect);
                }
                if (correct) {
                    const auto reward = root / L"WAV" / L"ANNOUNCE" / L"REWARD1.WAV";
                    const auto penalty = root / L"WAV" / L"PENALTY.WAV";
                    // The recovered handlers play REWARD1 synchronously before
                    // changing the contestant, balance, or painted portrait.
                    PlaySoundW(
                        reward.c_str(), nullptr,
                        SND_FILENAME | SND_SYNC | SND_NODEFAULT
                    );
                    // Apply the exact floating-point refund first. Looks then
                    // delays its score/disqualification writes until after the
                    // PENALTY portrait load (and any modal load warning).
                    auto& penalized = round_.contestants[
                        judging_contestant_
                    ];
                    const bool document_pending =
                        penalized.pending[category_index];
                    const std::uint8_t document_rating =
                        penalized.ratings[category_index];
                    const bool document_disqualified = penalized.disqualified;
                    metaverse::ApplyPenaltyBox(
                        round_, judging_contestant_, *category
                    );
                    penalized.pending[category_index] = document_pending;
                    penalized.ratings[category_index] = document_rating;
                    penalized.disqualified = document_disqualified;
                    // All three correct handlers refund first, then replace
                    // PENALTY/%d.BMP and present any load warning before the
                    // local pending bit, document score, or disqualification
                    // writes at 0x00407946 / 0x00409b74 / 0x00413ef0.
                    ReplaceJudgingPortraitBitmap(
                        *category,
                        judging_contestant_,
                        std::filesystem::path(L"BMP\\PENALTY") /
                            (std::to_wstring(penalized.contestant_id) +
                             L".BMP")
                    );
                    metaverse::CompletePavilionPendingSlot(
                        judging_visit_, judging_contestant_
                    );
                    // All three handlers zero the contestant's document score.
                    // Looks additionally resets its dialog-local number and
                    // hides the retained gauge. Talent/Brains never write
                    // +0xa0 or replace their current gauge in 0x004077d5 /
                    // 0x00409a03, so the chooser keeps the prior meter art.
                    penalized.ratings[category_index] = 0;
                    if (*category == metaverse::JudgingCategory::looks) {
                        current_rating_ = 0;
                        suppress_judging_rating_bitmap_once_ = false;
                        LoadJudgingRatingBitmap(*category);
                        if (judging_pressed_visual_ ==
                            JudgingAction::penalty) {
                            // The PENALTY mouse-up leaves code 1 and its pressed
                            // pixels latched. Only the correct branch dirties
                            // the Looks base before this forced repaint, which
                            // restores the control; WRONG5 deliberately does
                            // not.
                            judging_pressed_visual_ = JudgingAction::none;
                        }
                    }
                    if (*category == metaverse::JudgingCategory::looks) {
                        InvalidateRect(window, nullptr, FALSE);
                        UpdateWindow(window);
                    } else {
                        RepaintWidePavilionResult(
                            window, judging_contestant_
                        );
                        wide_rating_visible_ = true;
                    }
                    // The disqualification DWORD is written only after the
                    // forced portrait repaint in every correct handler.
                    penalized.disqualified = true;
                    if (*category != metaverse::JudgingCategory::looks) {
                        // Correct wide Penalty clears +0xac before
                        // PENALTY.WAV and TP/BP. WRONG5 intentionally keeps
                        // it active so judging can continue after the loss.
                        judging_contestant_active_ = false;
                        performance_access_granted_ = false;
                    }
                    if (*category == metaverse::JudgingCategory::looks) {
                        // 0x00413f32 hides ROTATE and repaints its entire
                        // presentation area back to the Looks background before
                        // PENALTY.WAV or the modal LP response starts.
                        looks_rotate_playing_ = false;
                        ApplyLooksPresentationAction(
                            metaverse::LooksPresentationAction::show_background
                        );
                        RepaintLooksPresentationRect(window);
                    }
                    PlaySoundW(
                        penalty.c_str(), nullptr,
                        SND_FILENAME | SND_SYNC | SND_NODEFAULT
                    );

                    const std::int32_t contestant_id = round_.contestants[
                        judging_contestant_
                    ].contestant_id;
                    const auto movie = PenaltyResponseVideoPath(
                        *category, contestant_id
                    );
                    std::string error;
                    if (!movie.empty() &&
                        PlayStandaloneVideo(root, movie, error)) {
                        pending_penalty_advance_ = PendingPenaltyAdvance{
                            *category, !interrupted_performance
                        };
                        status_ = L"Correct Penalty Box accusation; playing the "
                                  L"original contestant response.";
                        UpdateTitle(window);
                        InvalidateRect(window, nullptr, FALSE);
                        return true;
                    }
                    status_ = L"Penalty Box response movie failed: " +
                              std::wstring(error.begin(), error.end()) + L". ";
                    if (*category == metaverse::JudgingCategory::looks) {
                        RestoreLooksAfterPenaltyResponse(window);
                    }
                    if ((*category == metaverse::JudgingCategory::brains ||
                         *category == metaverse::JudgingCategory::talent) &&
                        !interrupted_performance &&
                        !first_penalty_host_played_[category_index]) {
                        if (StartJudgingEventHost(
                                HostCue{7, 1}, *category,
                                JudgingDisposition::disqualified, window
                            )) {
                            first_penalty_host_played_[category_index] = true;
                            return true;
                        }
                        first_penalty_host_played_[category_index] = true;
                    }
                    AdvanceJudging(
                        *category, window, JudgingDisposition::disqualified
                    );
                } else {
                    const auto wrong = root / L"WAV" / L"ANNOUNCE" / L"WRONG5.WAV";
                    // A bad accusation is also announced before the original
                    // applies its ten-percent balance loss.
                    PlaySoundW(
                        wrong.c_str(), nullptr,
                        SND_FILENAME | SND_SYNC | SND_NODEFAULT
                    );
                    const auto result = metaverse::ApplyPenaltyBox(
                        round_, judging_contestant_, *category
                    );
                    wchar_t loss[40] = {};
                    swprintf_s(
                        loss, L"%.2f",
                        static_cast<double>(-result.credit_change)
                    );
                    status_ = L"Wrong Penalty Box accusation. Lost $" +
                              std::wstring(loss) + L".";
                    // The wrong branches jump directly to their epilogues
                    // (0x00407874 / 0x00409aa2 / 0x00413df1), deliberately
                    // skipping the correct-path pavilion-loop restart.
                    UpdateTitle(window);
                    if (*category == metaverse::JudgingCategory::looks) {
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    // Wide handlers consume the credits invalidation queued
                    // before WRONG5; they do not queue a full-window repaint.
                    UpdateWindow(window);
                }
                return true;
            } else if (key == 'R') {
                if (*category == metaverse::JudgingCategory::looks) {
                    const bool started = StartLooksRotation();
                    if (started) {
                        status_ = looks_front_view_
                            ? L"Contestant rotating back to the front view."
                            : L"Contestant rotating to the back view.";
                    }
                } else {
                    LoadScene(false, false);
                    status_ = L"Performance restarted.";
                }
            } else {
                return false;
            }
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return true;
        }
        if (scene_ == kSlotsScene && key == VK_SPACE) {
            SpinSlotMachine(window, true);
            return true;
        }
        if (scene_ == kCryoScene) {
            if (key == VK_LEFT || key == VK_RIGHT) {
                const int delta = key == VK_RIGHT ? 1 : -1;
                BrowseCryoContestant(delta);
                status_.clear();
            } else if (key == 'R') {
                StartCryoRotation();
                status_.clear();
            } else if (key == 'I') {
                ToggleCryoInfo();
                status_.clear();
            } else if (key == VK_SPACE || key == VK_RETURN) {
                SponsorCurrentCryoContestant(window);
                return true;
            } else if (key == VK_BACK) {
                status_.clear();
            } else if (key == VK_NEXT || key == VK_PRIOR) {
                status_.clear();
            } else {
                return false;
            }
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return true;
        }
        if (scene_ == kCommentsScene) {
            if (key == VK_UP || key == VK_DOWN) {
                comment_order_.MoveSelection(key == VK_DOWN ? 1 : -1);
                const auto assigned = comment_order_.assigned_rank_by_comment[
                    static_cast<std::size_t>(comment_order_.selected_comment)
                ];
                status_ = assigned == -1
                    ? L"Choose this phrase for rank " +
                          std::to_wstring(comment_order_.next_rank) + L"."
                    : L"This phrase is already assigned rank " +
                          std::to_wstring(assigned) + L".";
            } else if (key == VK_RETURN || key == VK_SPACE) {
                AssignCurrentComment(window);
                return true;
            } else {
                return false;
            }
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return true;
        }
        if (scene_ != kWeightScene) {
            return false;
        }
        if (key == VK_LEFT || key == VK_RIGHT) {
            const int delta = key == VK_RIGHT ? 1 : -1;
            selected_weight_ =
                (selected_weight_ + delta + static_cast<int>(weight_controls_.size())) %
                static_cast<int>(weight_controls_.size());
        } else if (key == VK_UP || key == VK_DOWN) {
            const std::size_t control =
                static_cast<std::size_t>(selected_weight_) * 2 +
                (key == VK_UP ? 1u : 0u);
            const auto adjustment = metaverse::AdjustCategoryWeightControl(
                weight_controls_, control
            );
            if (!adjustment) {
                return false;
            }
            UpdateWeightGaugeBitmap(adjustment->category, window);
            // 0x004102e7 calls 0x00410e3e and forces its gauge repaint before
            // constructing/starting WAV/TT%d.WAV at 0x004102f2-0x00410317.
            const auto value_sound = assets_ / L"WAV" /
                (L"TT" + std::to_wstring(adjustment->value) + L".WAV");
            PlaySoundW(
                value_sound.c_str(), nullptr,
                SND_FILENAME | SND_ASYNC
            );
            return true;
        } else if (key == VK_RETURN) {
            std::string error;
            if (metaverse::NormalizeCategoryWeights(
                    weight_controls_, round_.weights, error
                )) {
                TearDownWeightDialogResources();
                status_ = L"Weights saved: Looks " +
                          std::to_wstring(round_.weights.looks) + L"%, Brains " +
                          std::to_wstring(round_.weights.brains) + L"%, Talent " +
                          std::to_wstring(round_.weights.talent) + L"%.";
                NextScene(1, window);
                return true;
            } else {
                if (error == "weight the categories please") {
                    MessageBoxW(
                        window,
                        L"Weight the categories please.",
                        metaverse::kLegacyDialogCaption,
                        static_cast<UINT>(
                            metaverse::kLegacyPlainMessageBoxStyle
                        )
                    );
                } else {
                    status_ = std::wstring(error.begin(), error.end());
                }
            }
            UpdateTitle(window);
            return true;
        } else {
            return false;
        }
        return true;
    }

    bool HandleMouseDown(
        int client_x, int client_y, WPARAM mouse_flags, HWND window
    ) {
        const bool centered_one_shot_active =
            scene_ == kIntroScene || pending_tally_finale_ ||
            pending_exit_after_credit_;
        if (centered_one_shot_active) {
            // The centered movie's child message map at 0x00430100 sends
            // private owner message 0x0405 from WM_LBUTTONDOWN while playback
            // is active. The owner handler at 0x004014c5 then takes the same
            // result-zero completion path as OK. During the first 100 ms, and
            // after a failed MCIWndCreate equivalent, no child exists, so a
            // click on the blank owner remains inert.
            if (metaverse::CenteredOneShotLeftClickFinishes(
                    centered_one_shot_child_created_
                )) {
                HandleDialogAccept(window);
            }
            return true;
        }
        if (LegacyPlaybackBlocksSceneInput()) {
            return true;
        }
        if (navigation_hub_active_ &&
            metaverse::NavigationLeftButtonInterruptsPlayback(
                pending_navigation_resolution_,
                slot_spin_active_,
                playback_range_.has_value()
            )) {
            // The navigation message map binds WM_LBUTTONDOWN to
            // fcn.0040aa61. Its +0x118 active-MCI branch runs before any
            // direction test, so an arbitrary left click takes the same
            // stop/seek/notify path as popup command 206. For Slots that
            // notification reveals the result inside the pumped outer wait.
            SkipNavigationTransition(window);
            return true;
        }
        if (slot_spin_active_) {
            return true;
        }
        const bool judging_composite =
            JudgingCategoryForScene(scene_).has_value() &&
            background_width_ > 0 && background_height_ > 0;
        if (!host_interstitial_active_ && !judging_composite &&
            (bitmap_width_ <= 0 || bitmap_height_ <= 0)) {
            return false;
        }

        RECT client = {};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        const bool navigation_composite = scene_data_.has_value();
        const int layout_width = (host_interstitial_active_ ||
                                  scene_ == kCryoScene || judging_composite ||
                                  navigation_composite)
            ? 640
            : bitmap_width_;
        const int layout_height = (host_interstitial_active_ ||
                                   scene_ == kCryoScene || judging_composite ||
                                   navigation_composite)
            ? 480
            : bitmap_height_;
        const double scale = std::min(
            static_cast<double>(client_width) / layout_width,
            static_cast<double>(client_height) / layout_height
        );
        const int drawn_width = static_cast<int>(std::lround(
            layout_width * scale
        ));
        const int drawn_height = static_cast<int>(std::lround(
            layout_height * scale
        ));
        const int content_x = (client_width - drawn_width) / 2;
        const int content_y = (client_height - drawn_height) / 2;
        // The original dialogs always owned an exact 640x480 client, so their
        // handlers could never receive a point in a letterbox margin.  The
        // resizable native window must reject that extra area explicitly;
        // otherwise a negative logical coordinate can satisfy the x<60
        // pavilion exit or y<240 navigation-Up comparisons.
        if (client_x < content_x || client_x >= content_x + drawn_width ||
            client_y < content_y || client_y >= content_y + drawn_height) {
            return true;
        }
        const int source_x = static_cast<int>(
            (static_cast<double>(client_x - content_x)) / scale
        );
        const int source_y = static_cast<int>(
            (static_cast<double>(client_y - content_y)) / scale
        );
        const int x = source_x * 640 / layout_width;
        const int y = source_y * 480 / layout_height;

        if (host_interstitial_active_) {
            if (!host_motion_active_) {
                // fcn.0040d6ca sets both completion flags for every mode-6
                // click. SPR.dll SpriteHitTest controls only +0xf0: an opaque
                // host pixel queues OUCH.WAV, while transparent/background
                // clicks still dismiss the C clip without that response.
                const auto outcome = metaverse::PlanMode6HostClick(
                    HostBitmapHitTest(x, y)
                );
                host_ouch_pending_ =
                    outcome.play_ouch_on_completion;
                if (outcome.finish_modal) {
                    FinishHostInterstitial(window, {});
                }
            }
            // Every reachable X-mode record carries 0x0080 and the same
            // original handler ignores its click before SpriteHitTest.
            return true;
        }

        if (navigation_hub_active_ && motion_player_.Active()) {
            // The generic handler returns immediately when the current MMS
            // record has 0x0080.  Every D-script hit reaction uses this flag,
            // so a second inside click or an outside miss is silent until the
            // authored reward animation completes.
            if (motion_player_.IgnoresMouseInput()) {
                return true;
            }
            // fcn.0040d6c2 passes (mouse_x + 16, mouse_y + 16) to the SPR
            // engine.  Its hit test is color-key aware, so blue/transparent
            // pixels inside the AVI's rectangular extent remain misses.
            const bool inside_sprite = MotionSpriteHitTest(x + 16, y + 16);
            if (inside_sprite) {
                std::string error;
                const bool stop_previous_voice =
                    motion_player_.StopsStateSoundOnHit();
                if (motion_player_.Hit(error)) {
                    if (stop_previous_voice) {
                        sprite_audio_player_.Stop();
                    }
                    last_sprite_sound_entry_ = 0;
                    PlaySpriteSound();
                    if (metaverse::SimmResultEarnsAward(
                            motion_player_.CurrentRecordResultCode()
                        )) {
                        simm_pending_award_ = metaverse::SimmCreditAward(
                            sprite_number_, simm_motion_pacing_factor_
                        );
                    }
                    // fcn.0040d966 seeks field 0x06 and repositions the
                    // sprite before the mouse handler returns.  Do not leave
                    // the root pose onscreen until the reaction timer fires.
                    metaverse::VideoFrame reaction_frame;
                    bool reaction_ended = false;
                    if (sprite_decoder_.SeekFrame(
                            motion_player_.Frame(), reaction_frame,
                            reaction_ended, error
                        ) && !reaction_ended) {
                        UploadSpriteFrame(reaction_frame);
                        if (sprite_bitmap_ == nullptr) {
                            error = "could not upload the Simm reaction entry frame";
                        }
                    } else if (error.empty()) {
                        error = "Simm reaction video ended before its entry frame";
                    }
                    next_sprite_due_ = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(
                            motion_player_.TickDelayMilliseconds(
                                simm_motion_pacing_factor_
                            )
                        );
                    status_ = error.empty()
                        ? L"Tagged the Simm; reaction playing."
                        : L"Simm reaction frame failed: " +
                              std::wstring(error.begin(), error.end());
                } else if (!error.empty()) {
                    status_ = L"Simm hit failed: " +
                              std::wstring(error.begin(), error.end());
                }
                UpdateTitle(window);
                InvalidateRect(window, nullptr, FALSE);
                return true;
            }
            // Generic media mouse handling at 0x0040d791-0x0040d7b9 tests
            // whether the click landed on the moving child. A miss pumps the
            // RICOCHET.WAV channel and remains modal instead of navigating.
            PlayNavigationRicochet();
            return true;
        }

        if (scene_ == kCryoScene) {
            std::array<bool, 5> control_available{};
            for (std::size_t index = 0;
                 index < control_available.size(); ++index) {
                control_available[index] =
                    cryo_control_highlight_bitmaps_[index] != nullptr;
            }
            std::array<bool, metaverse::kCryoInfoDetailCount>
                detail_available{};
            for (std::size_t index = 0;
                 index < detail_available.size(); ++index) {
                detail_available[index] =
                    cryo_info_highlight_bitmaps_[index] != nullptr;
            }
            const auto press = metaverse::PlanCryoMouseDown(
                static_cast<metaverse::CryoAction>(cryo_pressed_action_),
                cryo_info_visible_, x, y,
                (mouse_flags & MK_CONTROL) != 0,
                control_available, detail_available
            );
            // The undocumented Ctrl+cash action is independent of the target
            // scan. 0x00405d67 also deliberately retains an earlier +0xa8
            // press when another down lands outside every target.
            if (press.grant_hidden_cash) {
                round_.credits += metaverse::kCryoCashControlClickAward;
                InvalidateRect(window, nullptr, FALSE);
            }
            cryo_pressed_action_ = static_cast<int>(press.action_after);
            // fcn.00405d67 paints immediately but defers every action to the
            // matching release handler at 0x00405fb7.
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
            return true;
        }

        if (scene_ == kWeightScene) {
            if (metaverse::CategoryWeightOkAtPointer(x, y)) {
                weight_ok_pressed_ = true;
                weight_pressed_control_ = -1;
                weight_painter_.ok_dirty = true;
                // 0x00410230-0x0041024a paints OK synchronously before the
                // button-down handler returns.
                RepaintWeightRect(
                    window, metaverse::kCategoryWeightOkRect
                );
                return true;
            }
            if (const auto control =
                    metaverse::CategoryWeightControlAtPointer(x, y)) {
                weight_pressed_control_ = static_cast<int>(*control);
                weight_ok_pressed_ = false;
                weight_painter_.control_dirty = true;
                selected_weight_ = static_cast<int>(*control / 2);
                // fcn.00410285 invalidates and updates the pressed arrow
                // before mutating the value, replacing the gauge, or starting
                // the resulting TTn response.
                RepaintWeightRect(
                    window,
                    metaverse::kCategoryWeightControlRects[*control]
                );
                return HandleControlAction(
                    (*control % 2) == 0 ? VK_DOWN : VK_UP, window
                );
            }
            return true;
        }

        if (const auto category = JudgingCategoryForScene(scene_)) {
            const bool wide_layout =
                *category != metaverse::JudgingCategory::looks;
            // All three recovered pavilion handlers begin with this exact
            // direct-close target (Talent 0x00407003, Brains 0x00409227,
            // Looks 0x0041343d). It is independent of the current contestant
            // and is also the region that selects cursor resource 174.
            if (x < 60 && y < 382) {
                CloseJudgingPavilion(*category, window);
                return true;
            }
            if (*category == metaverse::JudgingCategory::looks) {
                const bool in_presentation =
                    x >= metaverse::kLooksPresentationLeft &&
                    x < metaverse::kLooksPresentationRight &&
                    y >= metaverse::kLooksPresentationTop &&
                    y < metaverse::kLooksPresentationBottom;
                if (looks_rotate_visible_ && !looks_rotate_playing_) {
                    const auto region = metaverse::LooksMagnifierRegionAt(
                        looks_front_view_, x - 225, y - 64
                    );
                    if (region) {
                        LoadLooksMagnification(*region, window);
                        return true;
                    }
                } else if (in_presentation &&
                           looks_rotate_child_constructed_ &&
                           looks_current_contestant_id_ != 0) {
                    // Once a magnifier bitmap has hidden the child, the next
                    // click lands on the parent. 0x0041345a-0x00413486 then
                    // simply ShowWindow(SW_SHOW)s the retained ROTATE child;
                    // it does not reconstruct or reseek it.
                    ApplyLooksPresentationAction(
                        metaverse::LooksPresentationAction::show_rotate
                    );
                    RepaintLooksPresentationRect(window);
                    return true;
                }
            }

            for (std::size_t slot = 0; slot < round_.contestants.size(); ++slot) {
                const RECT portrait_rect{
                    metaverse::kPavilionPortraitX +
                        static_cast<LONG>(slot) *
                            metaverse::kPavilionPortraitPitch,
                    metaverse::kPavilionPortraitY,
                    metaverse::kPavilionPortraitX +
                        static_cast<LONG>(slot) *
                            metaverse::kPavilionPortraitPitch +
                        metaverse::kPavilionPortraitSize,
                    metaverse::kPavilionPortraitY +
                        metaverse::kPavilionPortraitSize,
                };
                if (x < portrait_rect.left || x >= portrait_rect.right ||
                    y < portrait_rect.top || y >= portrait_rect.bottom) {
                    continue;
                }
                const std::size_t category_index =
                    static_cast<std::size_t>(*category);
                // Talent 0x00407200-0x00407222 and Brains
                // 0x00409424-0x00409453 test the entire five-entry pending
                // array only after a portrait-strip click.  Finishing the
                // fifth result therefore leaves the completed chooser visible;
                // a later strip click exits (and Brains runs C81 first).
                if (*category != metaverse::JudgingCategory::looks &&
                    !NextEligibleJudgingSlot(*category, 0)) {
                    RequestJudgingPavilionClose(*category, window);
                    return true;
                }
                if (*category != metaverse::JudgingCategory::looks &&
                    judging_contestant_active_) {
                    // Both wide pavilion handlers call AfxMessageBox with
                    // this exact text before they inspect the clicked slot's
                    // pending flag (Talent 0x00407227, Brains 0x0040945b).
                    MessageBoxW(
                        window,
                        L"Please judge the previous contestant.",
                        metaverse::kLegacyDialogCaption,
                        static_cast<UINT>(
                            metaverse::kLegacyPlainMessageBoxStyle
                        )
                    );
                    return true;
                }
                const auto& contestant = round_.contestants[slot];
                if (contestant.disqualified ||
                    !judging_visit_.pending[slot]) {
                    // Completed portrait clicks fall straight through to the
                    // original handler epilogue without a message.
                    return true;
                }
                if (*category == metaverse::JudgingCategory::looks) {
                    // Looks has no "previous contestant" modal. It permits a
                    // direct switch to any other pending portrait, but a click
                    // on the already highlighted slot is a no-op. 0x00413699
                    // also clears the newly selected contestant's Looks score.
                    if (judging_contestant_active_ &&
                        judging_contestant_ == slot) {
                        return true;
                    }
                    round_.contestants[slot].ratings[category_index] = 0;
                }
                ActivateJudgingContestant(slot, *category, window);
                return true;
            }

            const auto contains = [x, y](const RECT& rect) {
                return x >= rect.left && x < rect.right &&
                       y >= rect.top && y < rect.bottom;
            };
            constexpr std::array<JudgingAction, 4> actions{{
                JudgingAction::accept,
                JudgingAction::gong,
                JudgingAction::penalty,
                JudgingAction::rotate,
            }};
            for (const JudgingAction action : actions) {
                const auto rect = JudgingActionRect(*category, action);
                const bool wide_accept_blocked_by_movie =
                    wide_layout && action == JudgingAction::accept &&
                    metaverse::LegacyWidePerformanceChildIsPresent(
                        active_performance_category_.has_value(),
                        video_decoder_.IsOpen()
                    );
                const bool legacy_action_is_enabled =
                    judging_contestant_active_ ||
                    *category == metaverse::JudgingCategory::looks;
                if (legacy_action_is_enabled &&
                    !wide_accept_blocked_by_movie && rect && contains(*rect)) {
                    judging_pressed_action_ = action;
                    judging_pressed_visual_ = action;
                    RECT dirty = *rect;
                    InvalidateLogicalRect(window, dirty);
                    return true;
                }
            }

            const RECT& meter = wide_layout
                ? kWideJudgingMeterRect
                : kLooksJudgingMeterRect;
            const bool on_meter = contains(meter);
            if (on_meter) {
                if (wide_layout &&
                    metaverse::LegacyWidePerformanceChildIsPresent(
                        active_performance_category_.has_value(),
                        video_decoder_.IsOpen()
                    )) {
                    // MCIWndStop (message 0x808) is sent before the Talent or
                    // Brains rating is calculated. Its aborted-play notify runs
                    // the same child teardown/ambient restore path as a natural
                    // completion before the synchronous judge response starts.
                    FinishCurrentPerformance(window);
                }
                if (wide_layout) {
                    // (67-y)/((288-67)/11)+10 at 0x00407118-0x00407144
                    // and 0x0040933c-0x00409368.
                    current_rating_ = std::clamp(
                        static_cast<int>(
                            (kWideJudgingMeterRect.top - y) / 20 + 10
                        ),
                        0,
                        10
                    );
                } else {
                    // 0x00413559-0x0041359f divides the 265-pixel Looks
                    // Judge-O-Matic into eleven integer bands of 24 pixels.
                    current_rating_ =
                        metaverse::LooksMeterRatingAtPointer(x, y).value_or(0);
                }
                status_ = L"Rating set to " + std::to_wstring(current_rating_);
                status_ += judging_contestant_active_
                    ? L"; click ACCEPT."
                    : L" (chooser preview).";
                if (*category == metaverse::JudgingCategory::looks) {
                    LoadJudgingRatingBitmap(*category);
                    // Looks writes the meter value to its separately selected
                    // one-based portrait slot immediately (0x0041361a), even
                    // before ACCEPT. Rating zero deliberately plays no WAV.
                    round_.contestants[judging_contestant_].ratings[
                        static_cast<std::size_t>(*category)
                    ] = static_cast<std::uint8_t>(current_rating_);
                    if (current_rating_ > 0) {
                        PlayJudgeComment(
                            *category,
                            current_rating_,
                            static_cast<std::int32_t>(judging_contestant_ + 1),
                            window
                        );
                    }
                } else if (judging_contestant_active_) {
                    // The active wide-dialog zero branch stores zero but does
                    // not format TT0; only positive values play T/B comments.
                    if (current_rating_ > 0) {
                        PlayJudgeComment(
                            *category,
                            current_rating_,
                            static_cast<std::int32_t>(judging_contestant_ + 1),
                            window
                        );
                    }
                } else {
                    PlayGenericRatingPreview(
                        *category, current_rating_, window
                    );
                }
                if (wide_layout) {
                    // Talent/Brains start TTn or the contestant-specific
                    // comment first, then replace/warn-load the gauge DIB and
                    // synchronously repaint exactly its stored rectangle.
                    LoadJudgingRatingBitmap(*category, true);
                    wide_rating_visible_ = true;
                    RECT rating_rect{
                        metaverse::kWidePavilionRatingGraphicBounds[0],
                        metaverse::kWidePavilionRatingGraphicBounds[1],
                        metaverse::kWidePavilionRatingGraphicBounds[2],
                        metaverse::kWidePavilionRatingGraphicBounds[3],
                    };
                    InvalidateLogicalRect(window, rating_rect);
                    UpdateWindow(window);
                    UpdateTitle(window);
                    return true;
                }
                UpdateTitle(window);
                InvalidateRect(window, nullptr, FALSE);
                return true;
            }
            return true;
        }

        if (scene_ == kSlotsScene) {
            return HandleControlAction(VK_SPACE, window);
        }

        if (navigator_) {
            const metaverse::SceneObject* object = navigator_->CurrentObject();
            if (object != nullptr) {
                const auto direction = metaverse::SceneDirectionAtPointer(
                    object->flags,
                    x,
                    y,
                    pending_navigation_resolution_ ||
                        (slot_spin_active_ && playback_range_.has_value())
                );
                if (direction) {
                    return MoveScene(*direction, window);
                }
            }
            return true;
        }

        if (scene_ != kCommentsScene) {
            return false;
        }

        if (metaverse::CommentAcceptAtPointer(x, y)) {
            comment_accept_pressed_ = true;
            comment_painter_.accept_dirty = true;
            // 0x0040f55f-0x0040f574 paints ACCEPT synchronously on down.
            RepaintCommentRect(window, metaverse::CommentAcceptRect());
            return true;
        }
        if (const auto comment = metaverse::CommentPhraseAtPointer(x, y)) {
            const auto assigned = comment_order_.assigned_rank_by_comment[
                static_cast<std::size_t>(*comment)
            ];
            if (assigned != -1) {
                MessageBoxW(
                    window,
                    L"This command had been selected,\nplease choose another.",
                    metaverse::kLegacyDialogCaption,
                    static_cast<UINT>(
                        metaverse::kLegacyPlainMessageBoxStyle
                    )
                );
                return true;
            }
            comment_order_.selected_comment = *comment;
            // The hit rectangles exist during the two-second pre-C51 delay.
            // A click there calls 0x0040fd51 immediately even though the ten
            // phrase bitmaps themselves are not loaded until C51 returns.
            MoveCommentHand(*comment, window);
            status_ = L"Phrase selected; click ACCEPT to give it rank " +
                std::to_wstring(comment_order_.next_rank) + L".";
            UpdateTitle(window);
            return true;
        }
        return true;
    }

    bool HandleRightButtonDoubleClick(HWND window) {
        if (scene_ != kCommentsScene) {
            return false;
        }
        const bool owner_input_blocked =
            LegacyPlaybackBlocksSceneInput() || host_interstitial_active_ ||
            motion_player_.Active();
        if (!metaverse::CommentRightDoubleClickEndsDialog(
                owner_input_blocked
            )) {
            return true;
        }

        // ORDER is the sole offline dialog whose recovered message map binds
        // WM_RBUTTONDBLCLK. 0x0040ff2d calls the ordinary teardown with modal
        // result zero; the orchestrator ignores that result and immediately
        // enters navigation, leaving any unassigned document-table entries at
        // their existing values. No quit confirmation or sound stop occurs.
        FinishOrderDialog(window);
        return true;
    }

    void FinishOrderDialog(HWND window) {
        comment_accept_pressed_ = false;
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        TearDownOrderDialogResources();
        EnterNavigationHub(1, window, false);
    }

    bool HandleMouseUp(int client_x, int client_y, HWND window) {
        if (LegacyPlaybackBlocksSceneInput()) {
            return true;
        }
        // Generic mode 3/4/6 owns a modal dialog in MM.EXE. Its parent cannot
        // receive an unrelated release while that child is active.
        if (host_interstitial_active_ || motion_player_.Active()) {
            return true;
        }
        const bool releasing_cryo = scene_ == kCryoScene &&
            cryo_pressed_action_ != 0;
        const bool releasing_weight = scene_ == kWeightScene &&
            (weight_ok_pressed_ || weight_pressed_control_ >= 0);
        const bool releasing_comment = scene_ == kCommentsScene &&
            comment_accept_pressed_;
        const auto judging_category = JudgingCategoryForScene(scene_);
        const bool releasing_judging = judging_category &&
            judging_pressed_action_ != JudgingAction::none;
        if (!releasing_cryo && !releasing_weight && !releasing_comment &&
            !releasing_judging) {
            return false;
        }

        RECT client = {};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        // Like ORDER, Weight delegates drag-out cancellation to mouse-move.
        // 0x004103be commits OK whenever its flag survives to mouse-up and
        // performs no second PtInRect test there.
        bool release_weight_ok = releasing_weight && weight_ok_pressed_;
        const int pressed_weight_control = weight_pressed_control_;
        // ORDER's 0x0040f5ca mouse-up body tests only the surviving press
        // flag. Its 0x0040f607 mouse-move body is what permanently cancels a
        // press after the pointer leaves ACCEPT.
        bool release_comment_accept = releasing_comment;
        const JudgingAction pressed_judging_action = judging_pressed_action_;
        const int pressed_cryo_action = cryo_pressed_action_;
        int release_cryo_action = 0;
        JudgingAction release_judging_action = JudgingAction::none;
        if (client_width > 0 && client_height > 0) {
            const double scale = std::min(
                static_cast<double>(client_width) / 640.0,
                static_cast<double>(client_height) / 480.0
            );
            const int content_x = static_cast<int>(std::lround(
                (client_width - 640.0 * scale) / 2.0
            ));
            const int content_y = static_cast<int>(std::lround(
                (client_height - 480.0 * scale) / 2.0
            ));
            const int x = static_cast<int>((client_x - content_x) / scale);
            const int y = static_cast<int>((client_y - content_y) / scale);
            if (releasing_cryo) {
                std::array<bool, 5> control_available{};
                for (std::size_t index = 0;
                     index < control_available.size(); ++index) {
                    control_available[index] =
                        cryo_control_highlight_bitmaps_[index] != nullptr;
                }
                std::array<bool, metaverse::kCryoInfoDetailCount>
                    detail_available{};
                for (std::size_t index = 0;
                     index < detail_available.size(); ++index) {
                    detail_available[index] =
                        cryo_info_highlight_bitmaps_[index] != nullptr;
                }
                const auto release = metaverse::PlanCryoMouseUp(
                    static_cast<metaverse::CryoAction>(cryo_pressed_action_),
                    cryo_info_visible_, x, y,
                    control_available, detail_available
                );
                if (release.dispatch) {
                    release_cryo_action = cryo_pressed_action_;
                }
            }
            if (releasing_judging) {
                if (*judging_category !=
                    metaverse::JudgingCategory::looks) {
                    // 0x0040741e and 0x00409658 store only one shared press
                    // flag, then choose the wide action from the release
                    // position rather than the mouse-down position.
                    switch (metaverse::WidePavilionReleaseAt(
                                true, x, y
                            )) {
                        case metaverse::WidePavilionReleaseAction::accept:
                            release_judging_action = JudgingAction::accept;
                            break;
                        case metaverse::WidePavilionReleaseAction::gong:
                            release_judging_action = JudgingAction::gong;
                            break;
                        case metaverse::WidePavilionReleaseAction::penalty:
                            release_judging_action = JudgingAction::penalty;
                            break;
                        case metaverse::WidePavilionReleaseAction::none:
                            break;
                    }
                } else {
                    const auto rect = JudgingActionRect(
                        *judging_category, judging_pressed_action_
                    );
                    if (rect && x >= rect->left && x < rect->right &&
                        y >= rect->top && y < rect->bottom) {
                        release_judging_action = judging_pressed_action_;
                    }
                }
            }
        }

        weight_ok_pressed_ = false;
        weight_pressed_control_ = -1;
        comment_accept_pressed_ = false;
        cryo_pressed_action_ = 0;
        std::optional<metaverse::LooksControlReleasePlan> looks_release_plan;
        if (releasing_judging &&
            *judging_category == metaverse::JudgingCategory::looks) {
            if (const auto control = LooksControlForJudgingAction(
                    pressed_judging_action
                )) {
                looks_release_plan = metaverse::PlanLooksControlRelease(
                    *control,
                    release_judging_action == pressed_judging_action
                );
            }
        }
        const bool retain_looks_action_latch =
            looks_release_plan &&
            looks_release_plan->retain_logical_action;
        judging_pressed_action_ = retain_looks_action_latch
            ? pressed_judging_action
            : JudgingAction::none;
        const bool retain_looks_pressed_surface =
            looks_release_plan &&
            looks_release_plan->retain_pressed_surface;
        judging_pressed_visual_ = retain_looks_pressed_surface
            ? pressed_judging_action
            : JudgingAction::none;
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        if (releasing_weight) {
            if (pressed_weight_control >= 0) {
                weight_painter_.base_dirty = true;
                RepaintWeightRect(
                    window,
                    metaverse::kCategoryWeightControlRects[
                        static_cast<std::size_t>(pressed_weight_control)
                    ]
                );
            }
            if (release_weight_ok) {
                weight_painter_.base_dirty = true;
                RepaintWeightRect(
                    window, metaverse::kCategoryWeightOkRect
                );
            }
        } else if (releasing_judging) {
            if (*judging_category == metaverse::JudgingCategory::looks) {
                const auto dirty = JudgingActionRect(
                    *judging_category, pressed_judging_action
                );
                if (dirty) {
                    RECT rect = *dirty;
                    InvalidateLogicalRect(window, rect);
                }
            } else {
                // The shared wide-pavilion mouse-up handler invalidates all
                // three control rectangles before its forced repaint.
                for (const JudgingAction action : {
                         JudgingAction::accept,
                         JudgingAction::gong,
                         JudgingAction::penalty,
                     }) {
                    if (const auto dirty = JudgingActionRect(
                            *judging_category, action
                        )) {
                        RECT rect = *dirty;
                        InvalidateLogicalRect(window, rect);
                    }
                }
            }
        } else if (releasing_comment) {
            comment_painter_.base_dirty = true;
            RepaintCommentRect(window, metaverse::CommentAcceptRect());
        } else {
            const auto& dirty = metaverse::kCryoControlReleaseDirtyRect;
            RECT rect{dirty.left, dirty.top, dirty.right, dirty.bottom};
            InvalidateLogicalRect(window, rect);
        }
        if (releasing_cryo) {
            // Cryo 0x00405fb7, Weight 0x004103be, and ORDER 0x0040f5ca
            // synchronously restore the unpressed art before dispatch can
            // open a modal warning or start the next response sound/movie.
            UpdateWindow(window);
        }
        const bool repaint_cryo_information_after_dispatch =
            releasing_cryo && cryo_info_visible_ &&
            pressed_cryo_action >=
                static_cast<int>(metaverse::CryoAction::name_bio) &&
            pressed_cryo_action <=
                static_cast<int>(metaverse::CryoAction::favorite_quote);
        const bool update_looks_before_dispatch =
            looks_release_plan &&
            looks_release_plan->force_update_before_dispatch;
        if (releasing_judging &&
            (*judging_category != metaverse::JudgingCategory::looks ||
             update_looks_before_dispatch)) {
            // Talent/Brains 0x0040741e/0x00409658 invalidate all three
            // shared-press controls and force their unpressed art onscreen
            // before dispatching the action selected at the release point.
            // Looks calls UpdateWindow for ROTATE before its release hit test,
            // but has not dirtied the base and therefore deliberately leaves
            // the pressed ROTATE pixels onscreen. Even an inside release jumps
            // straight out after its bounded turn and never enters ACCEPT's
            // common base-dirty epilogue.
            UpdateWindow(window);
        }
        if (looks_release_plan &&
            looks_release_plan->restore_after_dispatch_phase &&
            !looks_release_plan->dispatch) {
            // Cancelled ACCEPT skips its handler and immediately enters the
            // common base-dirty/update epilogue. Cancelled ROTATE has no such
            // epilogue, leaving its already-painted pressed surface orphaned.
            UpdateWindow(window);
        }
        if (release_cryo_action >=
            static_cast<int>(metaverse::CryoAction::browse_left)) {
            switch (release_cryo_action - static_cast<int>(
                        metaverse::CryoAction::browse_left
                    )) {
                case 0:
                    return HandleControlAction(VK_LEFT, window);
                case 1:
                    return HandleControlAction(VK_RIGHT, window);
                case 2:
                    return HandleControlAction('R', window);
                case 3:
                    return HandleControlAction('I', window);
                case 4:
                    return HandleControlAction(VK_SPACE, window);
                default:
                    break;
            }
        }
        if (release_cryo_action >=
                static_cast<int>(metaverse::CryoAction::name_bio) &&
            release_cryo_action <
                static_cast<int>(metaverse::CryoAction::browse_left)) {
            StartCryoInfoSegment(static_cast<std::size_t>(
                release_cryo_action - static_cast<int>(
                    metaverse::CryoAction::name_bio
                )
            ));
            const auto& dirty =
                metaverse::kCryoInformationReleaseDirtyRect;
            RECT rect{dirty.left, dirty.top, dirty.right, dirty.bottom};
            InvalidateLogicalRect(window, rect);
            UpdateWindow(window);
            UpdateTitle(window);
            return true;
        }
        if (repaint_cryo_information_after_dispatch) {
            // Every visible detail release reaches 0x004061d3, including a
            // drag-out. Only a same-target release runs its prompt/movie
            // handler before the label is restored synchronously.
            const auto& dirty =
                metaverse::kCryoInformationReleaseDirtyRect;
            RECT rect{dirty.left, dirty.top, dirty.right, dirty.bottom};
            InvalidateLogicalRect(window, rect);
            UpdateWindow(window);
            return true;
        }
        if (release_weight_ok) {
            return HandleControlAction(VK_RETURN, window);
        }
        if (release_comment_accept) {
            AssignCurrentComment(window);
            return true;
        }
        switch (release_judging_action) {
            case JudgingAction::accept:
                return HandleControlAction(VK_RETURN, window);
            case JudgingAction::gong:
                return HandleControlAction('G', window);
            case JudgingAction::penalty:
                return HandleControlAction('X', window);
            case JudgingAction::rotate:
                return HandleControlAction('R', window);
            case JudgingAction::none:
                break;
        }
        return true;
    }

    bool HandleMouseMove(int client_x, int client_y, HWND window) {
        if (LegacyPlaybackBlocksSceneInput()) {
            navigation_neutral_cursor_active_ = false;
            return true;
        }
        if (host_interstitial_active_) {
            navigation_neutral_cursor_active_ = false;
            return true;
        }
        if (motion_player_.Active()) {
            navigation_neutral_cursor_active_ = false;
            return true;
        }
        if (const auto category = JudgingCategoryForScene(scene_)) {
            // Talent/Brains 0x004074e8/0x00409722 and Looks 0x00413850
            // select the original right-pointing resource 174 over the
            // x<60,y<382 direct pavilion-exit area and resource 166 elsewhere.
            // Looks' recovered magnifier cursor is resource 181.
            RECT client = {};
            GetClientRect(window, &client);
            const int client_width = client.right - client.left;
            const int client_height = client.bottom - client.top;
            if (client_width > 0 && client_height > 0) {
                const double scale = std::min(
                    static_cast<double>(client_width) / 640.0,
                    static_cast<double>(client_height) / 480.0
                );
                const int content_x = static_cast<int>(std::lround(
                    (client_width - 640.0 * scale) / 2.0
                ));
                const int content_y = static_cast<int>(std::lround(
                    (client_height - 480.0 * scale) / 2.0
                ));
                const int drawn_width = static_cast<int>(std::lround(
                    640.0 * scale
                ));
                const int drawn_height = static_cast<int>(std::lround(
                    480.0 * scale
                ));
                if (client_x < content_x ||
                    client_x >= content_x + drawn_width ||
                    client_y < content_y ||
                    client_y >= content_y + drawn_height) {
                    if (judging_hovered_portrait_) {
                        judging_hovered_portrait_.reset();
                        RECT dirty{
                            metaverse::kPavilionPortraitHoverDirtyBounds[0],
                            metaverse::kPavilionPortraitHoverDirtyBounds[1],
                            metaverse::kPavilionPortraitHoverDirtyBounds[2],
                            metaverse::kPavilionPortraitHoverDirtyBounds[3],
                        };
                        InvalidateLogicalRect(window, dirty);
                        UpdateWindow(window);
                    }
                    if (default_hand_cursor_ != nullptr) {
                        SetCursor(default_hand_cursor_);
                    }
                    navigation_neutral_cursor_active_ = false;
                    return true;
                }
                const int x = static_cast<int>((client_x - content_x) / scale);
                const int y = static_cast<int>((client_y - content_y) / scale);
                const auto hovered = metaverse::PavilionPortraitAt(x, y);
                if (hovered != judging_hovered_portrait_) {
                    judging_hovered_portrait_ = hovered;
                    RECT dirty{
                        metaverse::kPavilionPortraitHoverDirtyBounds[0],
                        metaverse::kPavilionPortraitHoverDirtyBounds[1],
                        metaverse::kPavilionPortraitHoverDirtyBounds[2],
                        metaverse::kPavilionPortraitHoverDirtyBounds[3],
                    };
                    // 0x004074e8/0x00409722/0x00413850 repaint only the
                    // 22-pixel name band. This does not touch or consume a
                    // previously painted one-shot red portrait frame.
                    InvalidateLogicalRect(window, dirty);
                    UpdateWindow(window);
                }
                HCURSOR cursor = nullptr;
                if (*category == metaverse::JudgingCategory::looks &&
                           looks_rotate_visible_ && !looks_rotate_playing_ &&
                           x >= 225 && x < 436 &&
                           y >= 64 && y < 376) {
                    // fcn.004125b5 owns a 211x312 child and its set-cursor
                    // handler selects 181 whenever either stable front/back
                    // view flag is active. The two one-pixel band gaps affect
                    // click dispatch only, not the child cursor.
                    cursor = looks_magnifier_cursor_;
                } else if (*category == metaverse::JudgingCategory::looks) {
                    switch (metaverse::LooksParentCursorActionAt(x, y)) {
                        case metaverse::LooksParentCursorAction::preserve_current:
                            break;
                        case metaverse::LooksParentCursorAction::set_hand_166:
                            cursor = default_hand_cursor_;
                            break;
                        case metaverse::LooksParentCursorAction::set_exit_174:
                            cursor = pavilion_exit_cursor_;
                            break;
                    }
                } else if (x < 60 && y < 382) {
                    cursor = pavilion_exit_cursor_;
                } else {
                    cursor = default_hand_cursor_;
                }
                if (cursor != nullptr) {
                    SetCursor(cursor);
                }
            }
        }
        if (scene_ == kCryoScene && !cryo_base_only_exposure_) {
            // Cryo has no hover state in the recovered handlers. The bitmap
            // selected by fcn.0040405d remains tied to mouse-down action +0xa8
            // until 0x00405fb7 clears it on release.
            navigation_neutral_cursor_active_ = false;
            return true;
        }
        if (scene_ == kWeightScene) {
            if (!weight_ok_pressed_ && weight_pressed_control_ < 0) {
                navigation_neutral_cursor_active_ = false;
                return true;
            }
            RECT client = {};
            GetClientRect(window, &client);
            const int client_width = client.right - client.left;
            const int client_height = client.bottom - client.top;
            if (client_width <= 0 || client_height <= 0) {
                return true;
            }
            const double scale = std::min(
                static_cast<double>(client_width) / 640.0,
                static_cast<double>(client_height) / 480.0
            );
            const int content_x = static_cast<int>(std::lround(
                (client_width - 640.0 * scale) / 2.0
            ));
            const int content_y = static_cast<int>(std::lround(
                (client_height - 480.0 * scale) / 2.0
            ));
            const int x = static_cast<int>((client_x - content_x) / scale);
            const int y = static_cast<int>((client_y - content_y) / scale);
            int cancelled_control = -1;
            bool cancelled_ok = false;
            if (weight_pressed_control_ >= 0 &&
                !metaverse::kCategoryWeightControlRects[
                    static_cast<std::size_t>(weight_pressed_control_)
                ].Contains(x, y)) {
                cancelled_control = weight_pressed_control_;
                weight_pressed_control_ = -1;
            }
            if (weight_ok_pressed_ &&
                !metaverse::CategoryWeightOkAtPointer(x, y)) {
                weight_ok_pressed_ = false;
                cancelled_ok = true;
            }
            if (cancelled_control >= 0 || cancelled_ok) {
                if (!weight_ok_pressed_ && weight_pressed_control_ < 0 &&
                    GetCapture() == window) {
                    ReleaseCapture();
                }
                // 0x0041043d immediately exposes the restored backing after
                // cancelling either the active arrow or OK press.
                if (cancelled_control >= 0) {
                    weight_painter_.base_dirty = true;
                    RepaintWeightRect(
                        window,
                        metaverse::kCategoryWeightControlRects[
                            static_cast<std::size_t>(cancelled_control)
                        ]
                    );
                }
                if (cancelled_ok) {
                    weight_painter_.base_dirty = true;
                    RepaintWeightRect(
                        window, metaverse::kCategoryWeightOkRect
                    );
                }
            }
            navigation_neutral_cursor_active_ = false;
            return true;
        }
        if (scene_ == kCommentsScene) {
            if (!comment_accept_pressed_) {
                navigation_neutral_cursor_active_ = false;
                return true;
            }
            RECT client = {};
            GetClientRect(window, &client);
            const int client_width = client.right - client.left;
            const int client_height = client.bottom - client.top;
            if (client_width <= 0 || client_height <= 0) {
                return true;
            }
            const double scale = std::min(
                static_cast<double>(client_width) / 640.0,
                static_cast<double>(client_height) / 480.0
            );
            const int content_x = static_cast<int>(std::lround(
                (client_width - 640.0 * scale) / 2.0
            ));
            const int content_y = static_cast<int>(std::lround(
                (client_height - 480.0 * scale) / 2.0
            ));
            const int x = static_cast<int>((client_x - content_x) / scale);
            const int y = static_cast<int>((client_y - content_y) / scale);
            if (!metaverse::CommentAcceptAtPointer(x, y)) {
                comment_accept_pressed_ = false;
                comment_painter_.base_dirty = true;
                if (GetCapture() == window) {
                    ReleaseCapture();
                }
                // ORDER's 0x0040f607 likewise restores ACCEPT synchronously
                // when pointer movement cancels its surviving press flag.
                RepaintCommentRect(window, metaverse::CommentAcceptRect());
            }
            navigation_neutral_cursor_active_ = false;
            return true;
        }
        if (const auto category = JudgingCategoryForScene(scene_);
            category && judging_pressed_action_ != JudgingAction::none) {
            if (*category != metaverse::JudgingCategory::looks) {
                // Talent/Brains mouse-move handlers never cancel their shared
                // press flag. All three controls remain painted until release.
                navigation_neutral_cursor_active_ = false;
                return true;
            }
            RECT client = {};
            GetClientRect(window, &client);
            const int client_width = client.right - client.left;
            const int client_height = client.bottom - client.top;
            bool inside = false;
            if (client_width > 0 && client_height > 0) {
                const double scale = std::min(
                    static_cast<double>(client_width) / 640.0,
                    static_cast<double>(client_height) / 480.0
                );
                const int content_x = static_cast<int>(std::lround(
                    (client_width - 640.0 * scale) / 2.0
                ));
                const int content_y = static_cast<int>(std::lround(
                    (client_height - 480.0 * scale) / 2.0
                ));
                const int x = static_cast<int>((client_x - content_x) / scale);
                const int y = static_cast<int>((client_y - content_y) / scale);
                const auto rect = JudgingActionRect(
                    *category, judging_pressed_action_
                );
                inside = rect && x >= rect->left && x < rect->right &&
                    y >= rect->top && y < rect->bottom;
            }
            if (!inside) {
                const auto dirty = JudgingActionRect(
                    *category, judging_pressed_action_
                );
                judging_pressed_action_ = JudgingAction::none;
                judging_pressed_visual_ = JudgingAction::none;
                if (GetCapture() == window) {
                    ReleaseCapture();
                }
                if (dirty) {
                    RECT rect = *dirty;
                    InvalidateLogicalRect(window, rect);
                }
                // Looks 0x00413a0b-0x00413a23 exposes the restored control
                // immediately when a drag leaves its action rectangle.
                UpdateWindow(window);
            }
            navigation_neutral_cursor_active_ = false;
            return true;
        }
        if (!navigator_ || pending_navigation_fade_target_) {
            navigation_neutral_cursor_active_ = false;
            return false;
        }
        const metaverse::SceneObject* object = navigator_->CurrentObject();
        if (object == nullptr) {
            navigation_neutral_cursor_active_ = false;
            return false;
        }

        RECT client = {};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        if (client_width <= 0 || client_height <= 0) {
            navigation_neutral_cursor_active_ = false;
            return false;
        }
        const double scale = std::min(
            static_cast<double>(client_width) / 640.0,
            static_cast<double>(client_height) / 480.0
        );
        const int content_x = static_cast<int>(std::lround(
            (client_width - 640.0 * scale) / 2.0
        ));
        const int content_y = static_cast<int>(std::lround(
            (client_height - 480.0 * scale) / 2.0
        ));
        const int drawn_width = static_cast<int>(std::lround(640.0 * scale));
        const int drawn_height = static_cast<int>(std::lround(480.0 * scale));
        if (client_x < content_x || client_x >= content_x + drawn_width ||
            client_y < content_y || client_y >= content_y + drawn_height) {
            navigation_neutral_cursor_active_ = false;
            return true;
        }
        const int x = static_cast<int>((client_x - content_x) / scale);
        const int y = static_cast<int>((client_y - content_y) / scale);
        const auto direction = metaverse::SceneDirectionAtPointer(
            object->flags,
            x,
            y,
            pending_navigation_resolution_ ||
                (slot_spin_active_ && playback_range_.has_value())
        );

        if (direction == metaverse::SceneDirection::up) {
            navigation_neutral_cursor_active_ = false;
            SetCursor(navigation_up_cursor_ != nullptr
                ? navigation_up_cursor_
                : LoadCursorW(nullptr, IDC_UPARROW));
        } else if (direction == metaverse::SceneDirection::left) {
            navigation_neutral_cursor_active_ = false;
            SetCursor(navigation_left_cursor_ != nullptr
                ? navigation_left_cursor_
                : LoadCursorW(nullptr, IDC_SIZEWE));
        } else if (direction == metaverse::SceneDirection::right) {
            navigation_neutral_cursor_active_ = false;
            SetCursor(navigation_right_cursor_ != nullptr
                ? navigation_right_cursor_
                : LoadCursorW(nullptr, IDC_SIZEWE));
        } else {
            // The original mouse handler records neutral state 4 without
            // changing the cursor immediately. Timer ID 2 supplies the next
            // sparkling-hand frame on its 500 ms tick.
            navigation_neutral_cursor_active_ = true;
        }
        return true;
    }

    bool HandleSetCursor(HWND window) {
        using CursorAction = metaverse::LegacyDialogCursorAction;
        using DialogRole = metaverse::LegacyDialogRole;

        // Generic mode 3 is the modal navigation Simm object. Its common
        // loader stores original crosshair resource 162 at +0xd0; modes 4 and
        // 6 instead store the ordinary hand resource 166.
        if (navigation_hub_active_ && motion_player_.Active()) {
            const std::uint16_t resource =
                metaverse::LegacyGenericCursorResourceForMode(3);
            HCURSOR cursor = resource == 162
                ? generic_target_cursor_
                : default_hand_cursor_;
            if (cursor != nullptr) {
                SetCursor(cursor);
            }
            return true;
        }
        if (host_interstitial_active_) {
            const int mode = host_motion_active_ ? 4 : 6;
            const std::uint16_t resource =
                metaverse::LegacyGenericCursorResourceForMode(mode);
            HCURSOR cursor = resource == 162
                ? generic_target_cursor_
                : default_hand_cursor_;
            if (cursor != nullptr) {
                SetCursor(cursor);
            }
            return true;
        }

        DialogRole role = DialogRole::centered_one_shot;
        if (navigation_hub_active_) {
            role = DialogRole::navigation_hub;
        } else if (scene_ == kCryoScene) {
            role = DialogRole::cryo;
        } else if (scene_ == kTalentJudgingScene) {
            role = DialogRole::talent;
        } else if (scene_ == kBrainsJudgingScene) {
            role = DialogRole::brains;
        } else if (scene_ == kLooksJudgingScene) {
            role = DialogRole::looks;
        } else if (scene_ == kCommentsScene) {
            role = DialogRole::order;
        } else if (scene_ == kWeightScene) {
            role = DialogRole::weight;
        }

        const CursorAction action = metaverse::LegacyCursorActionFor(role);
        if (action == CursorAction::preserve_current) {
            return true;
        }
        if (action == CursorAction::set_looks_child_cursor) {
            // The original ROTATE child owns (225,64)-(436,376). Its handler
            // sets resource 181 while either stable front/back flag is set;
            // the pavilion parent otherwise preserves the current cursor.
            if (looks_rotate_visible_ && !looks_rotate_playing_ &&
                looks_magnifier_cursor_ != nullptr) {
                POINT point{};
                RECT client{};
                if (GetCursorPos(&point) && ScreenToClient(window, &point) &&
                    GetClientRect(window, &client)) {
                    const int client_width = client.right - client.left;
                    const int client_height = client.bottom - client.top;
                    if (client_width > 0 && client_height > 0) {
                        const double scale = std::min(
                            static_cast<double>(client_width) / 640.0,
                            static_cast<double>(client_height) / 480.0
                        );
                        const int content_x = static_cast<int>(std::lround(
                            (client_width - 640.0 * scale) / 2.0
                        ));
                        const int content_y = static_cast<int>(std::lround(
                            (client_height - 480.0 * scale) / 2.0
                        ));
                        const int x = static_cast<int>(
                            (point.x - content_x) / scale
                        );
                        const int y = static_cast<int>(
                            (point.y - content_y) / scale
                        );
                        if (x >= 225 && x < 436 && y >= 64 && y < 376) {
                            SetCursor(looks_magnifier_cursor_);
                        }
                    }
                }
            }
            return true;
        }
        if (action == CursorAction::set_hand_166 &&
            default_hand_cursor_ != nullptr) {
            SetCursor(default_hand_cursor_);
            return true;
        }
        return false;
    }

    bool TickNavigationCursor(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (now < navigation_cursor_due_) {
            return false;
        }
        navigation_cursor_due_ = now + kNavigationCursorInterval;
        if (!navigation_hub_active_ || !navigator_ ||
            pending_navigation_fade_target_ ||
            host_interstitial_active_ || motion_player_.Active()) {
            navigation_neutral_cursor_active_ = false;
            return false;
        }

        const bool playback_active = playback_range_.has_value();
        if (GetForegroundWindow() == window) {
            POINT pointer{};
            RECT client{};
            if (GetCursorPos(&pointer) && ScreenToClient(window, &pointer) &&
                GetClientRect(window, &client) && PtInRect(&client, pointer)) {
                HandleMouseMove(pointer.x, pointer.y, window);
                if (navigation_neutral_cursor_active_) {
                    // fcn.0040af96 alternates 165,161,165,161... from its
                    // zeroed frame bit. Preserve the bit across directional
                    // cursor periods and during active navigation MCI.
                    HCURSOR cursor = navigation_neutral_cursor_frame_
                        ? navigation_neutral_cursor_a_
                        : navigation_neutral_cursor_b_;
                    navigation_neutral_cursor_frame_ =
                        !navigation_neutral_cursor_frame_;
                    if (cursor != nullptr) {
                        SetCursor(cursor);
                    }
                }
            } else {
                navigation_neutral_cursor_active_ = false;
            }
        } else {
            navigation_neutral_cursor_active_ = false;
        }

        // 0x0040b094 tests the MCI-active field after updating the sparkling
        // cursor but before overlays, Simms, or delayed 0x0020 movies.
        if (playback_active) {
            return false;
        }

        // 0x0040b184-0x0040b207 follows that active-MCI early return. For a
        // settled 0x0040 node it alternates the captured backing first and
        // Nn.BMP second, independent of pointer position.
        if (navigation_overlay_bitmap_ != nullptr &&
            navigation_overlay_width_ > 0 &&
            navigation_overlay_height_ > 0) {
            navigation_overlay_visible_ = navigation_overlay_next_visible_;
            RepaintNavigationOverlayRect(window);
            navigation_overlay_next_visible_ =
                !navigation_overlay_next_visible_;
        }
        return true;
    }

    void Tick(HWND window) {
        const auto now = std::chrono::steady_clock::now();
        if (TickPendingHostEntry(window, now)) {
            return;
        }
        if (pending_judging_entry_continuation_ &&
            now >= pending_judging_entry_continuation_->due) {
            const auto category =
                pending_judging_entry_continuation_->category;
            pending_judging_entry_continuation_.reset();
            // A consumed C21/C31/C41 suppresses only the modal host call. The
            // original timer still reaches its common continuation after 500
            // ms. For Talent/Brains that starts ambience; Looks additionally
            // constructs ROTATE, selects the first pending slot, and seeks it.
            FinishJudgingHostEntry(category, window);
        }
        if (TickHostInterstitial(window, now)) {
            // Generic mode 4 runs its own modal message pump. The underlying
            // MCI performance therefore keeps painting and may deliver its
            // completion notify while an X reaction moves over it.
            if (!host_interstitial_active_ || !host_motion_active_ ||
                !active_performance_category_) {
                return;
            }
        }
        const bool navigation_timer_body_ready =
            TickNavigationCursor(window, now);
        if (TickSlotMachine(window, now)) {
            return;
        }
        if (pending_navigation_fade_target_) {
            TickNavigationFade(window, now);
            return;
        }
        if (navigation_timer_body_ready) {
            // fcn.0040af96 tests the >1000 ms 0x0080 Simm branch before the
            // >10000 ms 0x0020 idle-movie branch on this same 500 ms tick.
            TickNavigationEncounter(window, now);
            if (motion_player_.Active()) {
                return;
            }
            if (TickNavigationIdleAnimation(window, now)) {
                return;
            }
        }
        TickMotionSprite(window, now);
        TickCryo(window, now);
        TickLooks(window, now);
        if (TickPendingCenteredOneShotStart(window, now)) {
            return;
        }
        if (!video_decoder_.IsOpen() || video_paused_) {
            return;
        }
        if (now < next_frame_due_) {
            return;
        }

        metaverse::VideoFrame frame;
        bool ended = false;
        std::string error;
        bool decoded = false;
        if (video_audio_clocked_) {
            const bool audio_playing = audio_player_.IsPlaying();
            if (!audio_playing) {
                // MCI continues an authored silent visual tail after the PCM
                // stream ends. BP/TP clips retain about one second here and
                // ordinary presentations can retain two frames. Return to the
                // AVI cadence instead of collapsing that tail into 10ms ticks.
                video_audio_clocked_ = false;
                decoded = video_decoder_.DecodeNext(frame, ended, error);
            } else {
                const std::int64_t relative_frame =
                    video_decoder_.FrameForAudioSamples(
                        audio_player_.PositionSamples(),
                        video_audio_sample_rate_
                    );
                std::int64_t target_frame = relative_frame < 0
                    ? -1
                    : video_audio_clock_start_frame_ + relative_frame;
                if (target_frame >= 0 && playback_range_) {
                    // MCI's media clock cannot run past the requested Play-To
                    // endpoint. Clamp a delayed native audio-clock sample so
                    // the final authored image is uploaded before completion.
                    target_frame = std::min(
                        target_frame, playback_range_->second
                    );
                }
                if (target_frame < 0) {
                    video_audio_clocked_ = false;
                    decoded = video_decoder_.DecodeNext(frame, ended, error);
                } else if (target_frame <= video_audio_clocked_frame_) {
                    next_frame_due_ = now + std::chrono::milliseconds(10);
                    return;
                } else if (target_frame == video_audio_clocked_frame_ + 1) {
                    decoded = video_decoder_.DecodeNext(frame, ended, error);
                } else {
                    decoded = video_decoder_.SeekFrame(
                        target_frame, frame, ended, error
                    );
                }
            }
        } else {
            decoded = video_decoder_.DecodeNext(frame, ended, error);
        }
        if (!decoded) {
            HandleVideoDecodeFailure(window, error);
            return;
        }
        if (ended) {
            if (playback_range_) {
                // A DAT Play-To may name the MCI stream-length boundary (the
                // supplied XR1 idle range ends at 1686 for a 1686-frame AVI).
                // MCI reports successful completion there; never leave native
                // navigation blocked merely because FFmpeg reports EOF.
                audio_player_.Stop();
                playback_range_.reset();
                video_paused_ = true;
                video_audio_clocked_ = false;
                video_audio_sample_rate_ = 0;
                video_audio_clocked_frame_ = -1;
                video_audio_clock_start_frame_ = 0;
                if (pending_navigation_resolution_) {
                    CompleteNavigationPlayback(window);
                }
                return;
            }
            if (!video_looping_) {
                if (scene_ == kIntroScene) {
                    FinishIntroModal(window, 0);
                    return;
                }
                if (active_performance_category_) {
                    FinishCurrentPerformance(window);
                    return;
                }
                if (pending_penalty_advance_) {
                    const auto pending = *pending_penalty_advance_;
                    pending_penalty_advance_.reset();
                    FinishPenaltyResponseMedia();
                    const auto category = pending.category;
                    if (category == metaverse::JudgingCategory::looks) {
                        RestoreLooksAfterPenaltyResponse(window);
                    }
                    const auto category_index = static_cast<std::size_t>(category);
                    if ((category == metaverse::JudgingCategory::brains ||
                         category == metaverse::JudgingCategory::talent) &&
                        pending.play_first_host &&
                        !first_penalty_host_played_[category_index]) {
                        if (StartJudgingEventHost(
                                HostCue{7, 1}, category,
                                JudgingDisposition::disqualified, window
                            )) {
                            first_penalty_host_played_[category_index] = true;
                            return;
                        }
                        first_penalty_host_played_[category_index] = true;
                    }
                    AdvanceJudging(
                        category,
                        window,
                        JudgingDisposition::disqualified
                    );
                    return;
                }
                if (pending_tally_finale_) {
                    FinishCenteredWinnerModal(window);
                    return;
                }
                if (pending_exit_after_credit_) {
                    FinishCenteredCreditModal(window);
                    return;
                }
                video_paused_ = true;
                return;
            }
            if (!video_decoder_.Restart(error) ||
                !video_decoder_.DecodeNext(frame, ended, error) || ended) {
                status_ = L"Video restart failed: " + std::wstring(error.begin(), error.end());
                video_decoder_.Close();
                UpdateTitle(window);
                return;
            }
        }
        if (playback_range_ && frame.frame_index > playback_range_->second) {
            audio_player_.Stop();
            playback_range_.reset();
            video_paused_ = true;
            video_audio_clocked_ = false;
            video_audio_sample_rate_ = 0;
            video_audio_clocked_frame_ = -1;
            video_audio_clock_start_frame_ = 0;
            if (pending_navigation_resolution_) {
                CompleteNavigationPlayback(window);
            }
            return;
        }
        UploadVideoFrame(frame);
        if (video_audio_clocked_) {
            video_audio_clocked_frame_ = frame.frame_index;
        }
        if (playback_range_ && frame.frame_index >= playback_range_->second) {
            audio_player_.Stop();
            playback_range_.reset();
            video_paused_ = true;
            video_audio_clocked_ = false;
            video_audio_sample_rate_ = 0;
            video_audio_clocked_frame_ = -1;
            video_audio_clock_start_frame_ = 0;
            if (pending_navigation_resolution_) {
                CompleteNavigationPlayback(window);
                return;
            }
        }
        if (video_audio_clocked_) {
            next_frame_due_ = now + std::chrono::milliseconds(10);
        } else {
            const double duration = std::clamp(
                frame.duration_seconds, 0.01, 0.25
            );
            next_frame_due_ = now +
                std::chrono::duration_cast<
                    std::chrono::steady_clock::duration
                >(std::chrono::duration<double>(duration));
        }
        InvalidateRect(window, nullptr, FALSE);
    }

    void HandleVideoDecodeFailure(HWND window, const std::string& error) {
        const std::wstring failure = L"Video decode failed: " +
            std::wstring(error.begin(), error.end());
        video_decoder_.Close();
        audio_player_.Stop();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;

        // MM.EXE's one-shot movies are modal state boundaries. Their decoder
        // failure path deliberately retains that boundary for the active OK
        // handler; other movie families resolve through their own callbacks.
        if (scene_ == kIntroScene || pending_tally_finale_ ||
            pending_exit_after_credit_) {
            // Resource 158 creates its MCIWnd with style 0x0a, including
            // MCIWNDF_NOERRORDLG. A failed intro/winner/credit remains a blank
            // active modal until Enter/OK follows its normal completion route.
            static_assert(metaverse::CenteredOneShotFailureWaitsForOk());
            status_.clear();
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        if (active_performance_category_) {
            FinishCurrentPerformance(window, failure);
            return;
        }

        if (pending_penalty_advance_) {
            const auto pending = *pending_penalty_advance_;
            pending_penalty_advance_.reset();
            FinishPenaltyResponseMedia();
            const auto category = pending.category;
            if (category == metaverse::JudgingCategory::looks) {
                RestoreLooksAfterPenaltyResponse(window);
            }
            const auto category_index = static_cast<std::size_t>(category);
            if ((category == metaverse::JudgingCategory::brains ||
                 category == metaverse::JudgingCategory::talent) &&
                pending.play_first_host &&
                !first_penalty_host_played_[category_index]) {
                if (StartJudgingEventHost(
                        HostCue{7, 1},
                        category,
                        JudgingDisposition::disqualified,
                        window
                    )) {
                    first_penalty_host_played_[category_index] = true;
                    status_ = failure;
                    UpdateTitle(window);
                    return;
                }
                first_penalty_host_played_[category_index] = true;
            }
            AdvanceJudging(
                category,
                window,
                JudgingDisposition::disqualified
            );
            status_ = failure;
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        status_ = failure;
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    bool MoveScene(
        metaverse::SceneDirection direction,
        HWND window,
        bool force_middle_branch = false
    ) {
        if (host_interstitial_active_) {
            return true;
        }
        if (slot_spin_active_) {
            return true;
        }
        if (motion_player_.Active()) {
            return true;
        }
        if (!navigator_) {
            return false;
        }
        if (pending_navigation_resolution_ || pending_navigation_fade_target_) {
            return true;
        }
        // fcn.0040aa61 clears the popup-activity latch at +0x628 before it
        // executes any new transition branch.
        navigation_popup_idle_latch_.ResetForTransition();
        const metaverse::SceneObject* source = navigator_->CurrentObject();
        if (source != nullptr && (source->flags & 0x4000) != 0 &&
            direction == metaverse::SceneDirection::up) {
            PlayNavigationDepartureSound(*source);
            StartSlotMachineSpin(window);
            return true;
        }
        const auto one_shot = source == nullptr
            ? metaverse::OneShotNavigationTransition::ordinary
            : metaverse::OneShotTransitionForFlags(
                  source->flags,
                  navigation_popup_one_shot_state_.transition_completed
              );
        metaverse::SceneTransition transition;
        std::string error;
        const bool moved = force_middle_branch
            ? navigator_->AdvanceMiddleUnchecked(transition, error)
            : navigator_->Move(direction, transition, error);
        if (!moved) {
            if (!error.empty()) {
                status_ = std::wstring(error.begin(), error.end());
                UpdateTitle(window);
            }
            return true;
        }
        if (source != nullptr) {
            PlayNavigationDepartureSound(*source);
        }
        slot_result_visible_ = false;
        slot_no_credit_visible_ = false;
        slot_outcome_.reset();
        ApplyOneShotNavigationDeparture(one_shot, transition);
        status_.clear();
        pending_navigation_encounter_node_.reset();
        ResetMotionSprite();
        navigation_arrival_pending_ = true;
        pending_navigation_resolution_ = true;
        ResetNavigationOverlay();
        PlayTransition(transition);
        if (video_paused_ && !playback_range_) {
            pending_navigation_resolution_ = false;
            ResolveNavigationNode(window);
        }
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }

    bool ShowNavigationContextMenu(
        int client_x,
        int client_y,
        HWND window
    ) {
        if (LegacyPlaybackBlocksSceneInput()) {
            return true;
        }
        // A mode-3 Simm is a separate modal generic-media dialog in the
        // original. Do not let its disabled navigation parent open a menu.
        if (host_interstitial_active_ || motion_player_.Active()) {
            return true;
        }
        if (!navigation_hub_active_ || !navigator_ ||
            metaverse::NavigationContextMenuSuppressed(
                navigation_entry_index_
            )) {
            return false;
        }

        // 0x0040aa2d sets +0x628 before asking the persistent popup to track.
        // If the menu is dismissed, the original navigation timer continues
        // postponing 0x0020 ambient movies until a transition resets it.
        navigation_popup_idle_latch_.MarkPopupRequested();

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) {
            return true;
        }
        for (const auto& item : metaverse::kNavigationPopupCommands) {
            AppendMenuW(
                menu, MF_STRING, item.command, item.label.data()
            );
        }
        const bool transition_active =
            metaverse::NavigationSkipAheadEnabled(
                pending_navigation_resolution_,
                slot_spin_active_,
                playback_range_.has_value()
            );
        EnableMenuItem(
            menu,
            static_cast<UINT>(metaverse::kNavigationPopupSkipPosition),
            MF_BYPOSITION |
                (transition_active ? MF_ENABLED : MF_GRAYED)
        );

        POINT point{client_x, client_y};
        if (ClientToScreen(window, &point)) {
            TrackPopupMenu(
                menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr
            );
        }
        DestroyMenu(menu);
        return true;
    }

    bool HandleNavigationMenuCommand(UINT command, HWND window) {
        if (!navigation_hub_active_) {
            return false;
        }
        if (LegacyPlaybackBlocksSceneInput() || host_interstitial_active_ ||
            motion_player_.Active()) {
            return true;
        }
        switch (command) {
            case metaverse::kNavigationMenuTalentCommand:
                EnterJudging(metaverse::JudgingCategory::talent, window);
                return true;
            case metaverse::kNavigationMenuBrainsCommand:
                EnterJudging(metaverse::JudgingCategory::brains, window);
                return true;
            case metaverse::kNavigationMenuLooksCommand:
                EnterJudging(metaverse::JudgingCategory::looks, window);
                return true;
            case metaverse::kNavigationMenuTallyCommand:
                // The original dispatcher accepts hidden command 203 and
                // forwards owner action 5 although no popup item exposes it.
                // Its disc-II prompt is unnecessary because the packaged
                // secondary tree is already available.
                StartTallySequence(window);
                return true;
            case metaverse::kNavigationMenuSlotsCommand: {
                const std::size_t target =
                    metaverse::NavigationSlotShortcutEntry(
                        IsSecondaryAssetScene()
                    );
                std::string error;
                if (!LoadNavigationEntry(target, error)) {
                    status_ = L"Slots shortcut failed: " +
                              std::wstring(error.begin(), error.end());
                } else {
                    status_.clear();
                    ResolveNavigationNode(window);
                }
                UpdateTitle(window);
                InvalidateRect(window, nullptr, FALSE);
                return true;
            }
            case metaverse::kNavigationMenuExitCommand:
                RequestQuit(window);
                return true;
            case metaverse::kNavigationMenuSkipCommand:
                SkipNavigationTransition(window);
                return true;
            default:
                return false;
        }
    }

    bool FinishGenericMediaModal(HWND window, bool run_completion_callback) {
        if (motion_player_.Active()) {
            FinishSimmEncounterReturn(std::chrono::steady_clock::now());
            status_.clear();
            navigation_neutral_cursor_active_ = false;
            InvalidateRect(window, nullptr, FALSE);
            return true;
        }
        if (!host_interstitial_active_) {
            return false;
        }
        if (!run_completion_callback) {
            // Inherited OnOK ends the dialog with IDOK without calling the
            // derived completion callback that owns mode-6 synchronous OUCH.
            host_ouch_pending_ = false;
        }
        FinishHostInterstitial(window, {});
        return true;
    }

    void FinishIntroModal(HWND window, std::int32_t modal_result) {
        pending_centered_one_shot_start_.reset();
        centered_one_shot_child_created_ = false;
        ResetStandalonePlaybackState();
        if (metaverse::IntroModalResultExits(modal_result)) {
            DestroyWindow(window);
            return;
        }

        if (player_.name != "DEMO") {
            // 0x00401d63 does not construct Cryo directly. It opens the
            // navigation wrapper at entry 13 (HALL.DAT) and waits for the Ms.
            // Metaverse door to return action 13.
            EnterNavigationHub(metaverse::kHallMsMetaverseEntry, window);
            return;
        }
        round_.weights = {5, 5, 5};
        selected_contestants_.assign(
            metaverse::kDemoContestantIds.begin(),
            metaverse::kDemoContestantIds.end()
        );
        std::string variant_error;
        if (!PreparePavilionVariants(variant_error)) {
            status_ = L"DEMO pavilion setup failed: " + std::wstring(
                variant_error.begin(), variant_error.end()
            );
            UpdateTitle(window);
            MessageBoxW(
                window,
                status_.c_str(),
                L"Ms. Metaverse — DEMO Setup Error",
                MB_OK | MB_ICONERROR
            );
            DestroyWindow(window);
            return;
        }
        // The original DEMO branch sets navigation state one at 0x00401d57
        // and jumps over Cryo, Weight, and ORDER after either notify or Cancel.
        EnterNavigationHub(1, window);
    }

    void FinishCenteredWinnerModal(HWND window) {
        pending_centered_one_shot_start_.reset();
        pending_tally_finale_ = false;
        centered_one_shot_child_created_ = false;
        ResetStandalonePlaybackState();

        // 0x0040152e closes/deletes the MCI child before EndDialog returns to
        // the tally caller. Expose resource 158's black owner before the
        // synchronous replay prompt or END2.WAV; retaining W#.AVI's last
        // frame here is visibly different from the original ordering.
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        FinishTallySequence(window);
    }

    void FinishCenteredCreditModal(HWND window) {
        pending_centered_one_shot_start_.reset();
        pending_exit_after_credit_ = false;
        centered_one_shot_child_created_ = false;
        ResetStandalonePlaybackState();
        DestroyWindow(window);
    }

    bool RequestQuit(HWND window) {
        if (MessageBoxW(
                window,
                L"Are you sure you want to quit the game now?",
                metaverse::kLegacyDialogCaption,
                static_cast<UINT>(
                    metaverse::kLegacyConfirmMessageBoxStyle
                )
            ) == IDYES) {
            if (scene_ == kWeightScene) {
                // 0x00410f5b destroys every Weight-owned wrapper before its
                // result-one EndDialog returns to the orchestrator's exit path.
                TearDownWeightDialogResources();
            }
            DestroyWindow(window);
        }
        return true;
    }

    bool HandleDialogCancel(HWND window) {
        // Generic C/X/Simm media overrides CDialog::OnCancel at 0x0040e6e0.
        // The loop observes its completion flags, calls fcn.0040e4bd(0), and
        // then returns through the ordinary caller tail.
        if (FinishGenericMediaModal(window, true)) {
            return true;
        }
        // TP/BP/LP responses and Looks' twenty-frame rotate helper issue an
        // MCI "Play ... Wait" from inside the pavilion input handler. The
        // original owner cannot receive Escape or WM_CLOSE until that call
        // returns. The native decoder is asynchronous, so enforce the same
        // boundary explicitly rather than truncating the authored movie.
        if (SynchronousMciWaitActive()) {
            return true;
        }
        // The centered one-shot class overrides OnCancel with the bare return
        // at 0x0040152d. Escape and the caption close button therefore cannot
        // skip intro, winner, or credits.
        if (scene_ == kIntroScene || pending_tally_finale_ ||
            pending_exit_after_credit_) {
            return true;
        }
        if (scene_ == kCommentsScene) {
            // ORDER's OnCancel at 0x0040f517 confirms, then ends that dialog
            // with result one. Unlike every other game dialog, the caller at
            // 0x00402118 ignores the result and enters navigation state one.
            // Preserve the original misleading "quit" prompt and continuation.
            static_assert(
                metaverse::LegacyDefaultRoutesFor(
                    metaverse::LegacyDialogRole::order
                ).on_cancel ==
                metaverse::LegacyDialogDefaultAction::
                    confirm_then_continue_navigation
            );
            if (MessageBoxW(
                    window,
                    L"Are you sure you want to quit the game now?",
                    metaverse::kLegacyDialogCaption,
                    static_cast<UINT>(
                        metaverse::kLegacyConfirmMessageBoxStyle
                    )
                ) == IDYES) {
                FinishOrderDialog(window);
            }
            return true;
        }
        return RequestQuit(window);
    }

    bool HandleDialogAccept(HWND window) {
        // Generic OnOK is inherited and returns IDOK without invoking the
        // result-zero completion callback. Its callers still run their common
        // no-award/advance tail.
        if (FinishGenericMediaModal(window, false)) {
            return true;
        }
        if (SynchronousMciWaitActive()) {
            return true;
        }
        // The one-shot override at 0x004014d3 stops active MCI and calls
        // EndDialog(0). Its pre-play branch attempts Play Notify instead of
        // closing, so keep the initial 100 ms modal delay intact.
        if (scene_ == kIntroScene) {
            if (!CenteredOneShotStartPending(CenteredOneShotKind::intro)) {
                FinishIntroModal(window, 0);
            }
            return true;
        }
        if (pending_tally_finale_) {
            if (!CenteredOneShotStartPending(CenteredOneShotKind::winner)) {
                FinishCenteredWinnerModal(window);
            }
            return true;
        }
        if (pending_exit_after_credit_) {
            if (!CenteredOneShotStartPending(CenteredOneShotKind::credit)) {
                FinishCenteredCreditModal(window);
            }
            return true;
        }
        if (navigation_hub_active_) {
            // The hub vtable at 0x00431ab0 maps OnOK to 0x0040cb12. Default
            // dialog Enter stops/destroys its navigation scene and returns
            // modal result zero; both callers fall through to profile save and
            // application exit without showing the OnCancel confirmation.
            static_assert(
                metaverse::LegacyDefaultRoutesFor(
                    metaverse::LegacyDialogRole::navigation_hub
                ).on_ok ==
                metaverse::LegacyDialogDefaultAction::
                    exit_without_confirmation
            );
            DestroyWindow(window);
            return true;
        }
        return false;
    }

    bool HandleApplicationActivationChanged(HWND window, bool active) {
        // The derived WM_ACTIVATEAPP handler at 0x0040ef28 ignores bActive and
        // is a second generic-media cancellation route on either activation
        // change. Unlike inherited dialog Cancel, it calls fcn.0040e4bd with
        // result zero, including the mode-6 completion-owned OUCH side effect, before
        // its caller resumes.
        if (FinishGenericMediaModal(window, true)) {
            return true;
        }

        using ActivationAction = metaverse::LegacyDialogActivationAction;
        ActivationAction action = ActivationAction::none;
        if (scene_ == kCryoScene) {
            action = metaverse::LegacyActivationActionFor(
                metaverse::LegacyDialogRole::cryo, active
            );
        } else if (scene_ == kTalentJudgingScene) {
            action = metaverse::LegacyActivationActionFor(
                metaverse::LegacyDialogRole::talent, active
            );
        } else if (scene_ == kBrainsJudgingScene) {
            action = metaverse::LegacyActivationActionFor(
                metaverse::LegacyDialogRole::brains, active
            );
        } else if (scene_ == kCommentsScene) {
            action = metaverse::LegacyActivationActionFor(
                metaverse::LegacyDialogRole::order, active
            );
        } else if (scene_ == kWeightScene) {
            action = metaverse::LegacyActivationActionFor(
                metaverse::LegacyDialogRole::weight, active
            );
        } else if (scene_ == kLooksJudgingScene) {
            action = metaverse::LegacyActivationActionFor(
                metaverse::LegacyDialogRole::looks, active
            );
        }

        switch (action) {
            case ActivationAction::repaint_controls:
                // 0x004063ce, 0x00407dd2, and 0x0040a010 set their painter
                // dirty flags on both activation edges before calling the base.
                if (scene_ == kTalentJudgingScene ||
                    scene_ == kBrainsJudgingScene) {
                    wide_rating_visible_ = true;
                }
                InvalidateRect(window, nullptr, FALSE);
                return true;
            case ActivationAction::reload_order_phrases:
                // 0x0040ff37 reloads all ten COMMENT DIBs on reactivation,
                // even if the two-second C51 entry sequence has not completed.
                comment_painter_.base_dirty = true;
                comment_painter_.thermometer_dirty = true;
                for (std::size_t comment = 0;
                     comment < comment_phrase_bitmaps_.size(); ++comment) {
                    LoadCommentPhrase(comment, window);
                }
                return true;
            case ActivationAction::reload_weight_gauges:
                // 0x00410fd9 recreates TH1/TH2/TH3 from the three current
                // integer values, including during the normally blank C91 delay.
                for (std::size_t category = 0;
                     category < weight_gauge_bitmaps_.size(); ++category) {
                    UpdateWeightGaugeBitmap(category, window);
                }
                return true;
            case ActivationAction::show_looks_media:
                // 0x0041449d calls ShowWindow(SW_SHOW) on both the pavilion and
                // an existing ROTATE child whenever the app becomes active.
                ApplyLooksPresentationAction(
                    metaverse::LooksPresentationAction::show_rotate
                );
                ShowWindow(window, SW_SHOW);
                InvalidateRect(window, nullptr, FALSE);
                return true;
            case ActivationAction::finish_generic_media:
            case ActivationAction::none:
                return false;
        }
        return false;
    }

    void Paint(HWND window) {
        PAINTSTRUCT paint = {};
        HDC paint_target = BeginPaint(window, &paint);
        RECT client = {};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        if (client_width <= 0 || client_height <= 0) {
            EndPaint(window, &paint);
            return;
        }

        HDC target = CreateCompatibleDC(paint_target);
        HBITMAP target_bitmap = CreateCompatibleBitmap(
            paint_target, client_width, client_height
        );
        if (target == nullptr || target_bitmap == nullptr) {
            if (target_bitmap != nullptr) {
                DeleteObject(target_bitmap);
            }
            if (target != nullptr) {
                DeleteDC(target);
            }
            FillRect(paint_target, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(window, &paint);
            return;
        }
        HGDIOBJ previous_target_bitmap = SelectObject(target, target_bitmap);
        FillRect(target, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        double content_scale = 1.0;
        int content_x = 0;
        int content_y = 0;
        const bool judging_composite = background_bitmap_ != nullptr &&
            JudgingCategoryForScene(scene_).has_value();
        const bool navigation_composite = scene_data_.has_value();
        const bool weight_composite = scene_ == kWeightScene;
        const bool comment_composite = scene_ == kCommentsScene;
        if (!weight_composite && !comment_composite &&
            ((bitmap_ != nullptr && bitmap_width_ > 0 && bitmap_height_ > 0) ||
             judging_composite)) {
            const int client_width = client.right - client.left;
            const int client_height = client.bottom - client.top;
            const int layout_width = (scene_ == kCryoScene || judging_composite ||
                                      navigation_composite)
                ? 640
                : bitmap_width_;
            const int layout_height = (scene_ == kCryoScene || judging_composite ||
                                       navigation_composite)
                ? 480
                : bitmap_height_;
            content_scale = std::min(
                static_cast<double>(client_width) / layout_width,
                static_cast<double>(client_height) / layout_height
            );
            const int layout_drawn_width = static_cast<int>(std::lround(
                layout_width * content_scale
            ));
            const int layout_drawn_height = static_cast<int>(std::lround(
                layout_height * content_scale
            ));
            content_x = (client_width - layout_drawn_width) / 2;
            content_y = (client_height - layout_drawn_height) / 2;
            const auto draw_bitmap = [&](HBITMAP source_bitmap,
                                         int source_width,
                                         int source_height,
                                         int logical_x,
                                         int logical_y,
                                         int logical_width = 0,
                                         int logical_height = 0) {
                if (source_bitmap == nullptr || source_width <= 0 ||
                    source_height <= 0) {
                    return;
                }
                HDC source = CreateCompatibleDC(target);
                HGDIOBJ previous = SelectObject(source, source_bitmap);
                SetStretchBltMode(target, COLORONCOLOR);
                const int destination_width = logical_width > 0
                    ? logical_width
                    : source_width;
                const int destination_height = logical_height > 0
                    ? logical_height
                    : source_height;
                StretchBlt(
                    target,
                    content_x + static_cast<int>(std::lround(
                        logical_x * content_scale
                    )),
                    content_y + static_cast<int>(std::lround(
                        logical_y * content_scale
                    )),
                    static_cast<int>(std::lround(destination_width * content_scale)),
                    static_cast<int>(std::lround(destination_height * content_scale)),
                    source,
                    0,
                    0,
                    source_width,
                    source_height,
                    SRCCOPY
                );
                SelectObject(source, previous);
                DeleteDC(source);
            };
            if (judging_composite) {
                draw_bitmap(
                    background_bitmap_, background_width_, background_height_, 0, 0
                );
                const auto category = JudgingCategoryForScene(scene_);
                if (category && *category == metaverse::JudgingCategory::looks) {
                    // ROTATE.AVI is shown at 212x312 (0x00413afc). The separate
                    // modal LP penalty child uses 200x320 at the same origin
                    // (0x00413b5e), then the normal ROTATE child is shown again.
                    const bool looks_penalty_response =
                        pending_penalty_advance_ &&
                        pending_penalty_advance_->category ==
                            metaverse::JudgingCategory::looks;
                    draw_bitmap(
                        bitmap_, bitmap_width_, bitmap_height_,
                        metaverse::kLooksPresentationLeft,
                        metaverse::kLooksPresentationTop,
                        looks_penalty_response
                            ? metaverse::kLooksPenaltyResponseWidth
                            : metaverse::kLooksRotateWindowWidth,
                        looks_penalty_response
                            ? metaverse::kLooksPenaltyResponseHeight
                            : metaverse::kLooksRotateHeight
                    );
                } else {
                    // Both original Brains and Talent MCI children are moved to
                    // (208,108) at 320x240 (0x00407682 / 0x004098b0).
                    if (active_performance_category_ && bitmap_ == nullptr) {
                        // Their wrapper object survives an MCI open failure.
                        // The still-visible child owns an empty black surface
                        // until a meter, Gong, or Penalty action tears it down.
                        RECT empty_child{
                            content_x + static_cast<LONG>(std::lround(
                                metaverse::kLegacyWidePerformanceChildBounds[0] *
                                content_scale
                            )),
                            content_y + static_cast<LONG>(std::lround(
                                metaverse::kLegacyWidePerformanceChildBounds[1] *
                                content_scale
                            )),
                            content_x + static_cast<LONG>(std::lround(
                                metaverse::kLegacyWidePerformanceChildBounds[2] *
                                content_scale
                            )),
                            content_y + static_cast<LONG>(std::lround(
                                metaverse::kLegacyWidePerformanceChildBounds[3] *
                                content_scale
                            )),
                        };
                        FillRect(
                            target, &empty_child,
                            static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))
                        );
                    }
                    draw_bitmap(
                        bitmap_, bitmap_width_, bitmap_height_,
                        metaverse::kLegacyWidePerformanceChildBounds[0],
                        metaverse::kLegacyWidePerformanceChildBounds[1]
                    );
                }
            } else if (navigation_composite) {
                // All navigation AVIs are 320x240, but MM.EXE places their
                // DAT-driven bitmaps, text, cursors, and sprites in a 640x480
                // client canvas. Stretch the movie first so those recovered
                // coordinates share the same logical space.
                draw_bitmap(
                    bitmap_, bitmap_width_, bitmap_height_, 0, 0, 640, 480
                );
            } else {
                draw_bitmap(bitmap_, bitmap_width_, bitmap_height_, 0, 0);
            }
        } else if (scene_ == kSlotsScene || weight_composite ||
                   comment_composite) {
            const int client_width = client.right - client.left;
            const int client_height = client.bottom - client.top;
            content_scale = std::min(
                static_cast<double>(client_width) / 640.0,
                static_cast<double>(client_height) / 480.0
            );
            content_x = static_cast<int>(std::lround(
                (client_width - 640.0 * content_scale) / 2.0
            ));
            content_y = static_cast<int>(std::lround(
                (client_height - 480.0 * content_scale) / 2.0
            ));
        }

        const auto draw_logical_overlay = [&](HBITMAP overlay,
                                              int width,
                                              int height,
                                              int x,
                                              int y,
                                              int logical_width = 0,
                                              int logical_height = 0) {
            if (overlay == nullptr || width <= 0 || height <= 0) {
                return;
            }
            HDC source = CreateCompatibleDC(target);
            HGDIOBJ previous = SelectObject(source, overlay);
            SetStretchBltMode(target, COLORONCOLOR);
            const int destination_width = logical_width > 0
                ? logical_width
                : width;
            const int destination_height = logical_height > 0
                ? logical_height
                : height;
            StretchBlt(
                target,
                content_x + static_cast<int>(std::lround(x * content_scale)),
                content_y + static_cast<int>(std::lround(y * content_scale)),
                static_cast<int>(std::lround(destination_width * content_scale)),
                static_cast<int>(std::lround(destination_height * content_scale)),
                source, 0, 0, width, height, SRCCOPY
            );
            SelectObject(source, previous);
            DeleteDC(source);
        };

        if (navigation_overlay_visible_) {
            draw_logical_overlay(
                navigation_overlay_bitmap_, navigation_overlay_width_,
                navigation_overlay_height_, navigation_overlay_x_,
                navigation_overlay_y_
            );
        }

        if (scene_ == kLooksJudgingScene && bitmap_ == nullptr) {
            if (looks_rotate_visible_) {
                if (looks_rotate_bitmap_ != nullptr) {
                    draw_logical_overlay(
                        looks_rotate_bitmap_, looks_rotate_width_,
                        looks_rotate_height_,
                        metaverse::kLooksPresentationLeft,
                        metaverse::kLooksPresentationTop,
                        metaverse::kLooksRotateWindowWidth,
                        metaverse::kLooksRotateHeight
                    );
                } else if (looks_rotate_child_constructed_) {
                    // The wrapper remains the active child even if MCI open
                    // failed. Its empty child surface still covers the parent.
                    RECT blank_child{
                        content_x + static_cast<LONG>(std::lround(
                            metaverse::kLooksPresentationLeft * content_scale
                        )),
                        content_y + static_cast<LONG>(std::lround(
                            metaverse::kLooksPresentationTop * content_scale
                        )),
                        content_x + static_cast<LONG>(std::lround(
                            (metaverse::kLooksPresentationLeft +
                             metaverse::kLooksRotateWindowWidth) *
                            content_scale
                        )),
                        content_y + static_cast<LONG>(std::lround(
                            (metaverse::kLooksPresentationTop +
                             metaverse::kLooksRotateHeight) * content_scale
                        )),
                    };
                    FillRect(
                        target, &blank_child,
                        static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))
                    );
                }
            } else if (looks_magnifier_visible_) {
                draw_logical_overlay(
                    looks_bitmap_, looks_width_, looks_height_, 225, 64
                );
            }
        }
        if (const auto category = JudgingCategoryForScene(scene_)) {
            // The three original painters issue pressed controls before cash,
            // gauge, portraits/frames, and hover text. Keep that operation
            // order even though the ordinary authored rectangles do not
            // overlap: clipped paints still consume their one-shot flags.
            if (judging_pressed_visual_ != JudgingAction::none) {
                const auto draw_pressed_action = [&](JudgingAction action) {
                    const std::size_t action_index =
                        static_cast<std::size_t>(action) - 1;
                    const auto rect = JudgingActionRect(*category, action);
                    if (rect && action_index <
                        judging_action_bitmaps_.size()) {
                        draw_logical_overlay(
                            judging_action_bitmaps_[action_index],
                            rect->right - rect->left,
                            rect->bottom - rect->top,
                            rect->left,
                            rect->top
                        );
                    }
                };
                // Talent/Brains use one +0x68 flag for all three painter
                // branches. A normal mouse-down invalidates only the source
                // rectangle, so clipping reveals just that control; a full
                // repaint while the flag survives reveals all three.
                if (*category == metaverse::JudgingCategory::looks) {
                    draw_pressed_action(judging_pressed_visual_);
                } else {
                    draw_pressed_action(JudgingAction::accept);
                    draw_pressed_action(JudgingAction::gong);
                    draw_pressed_action(JudgingAction::penalty);
                }
            }

            // Talent/Brains fcn.004066c0/0x004088a7 and Looks
            // fcn.004129d3 paint live cash before the gauge at (465,25).
            const int font_height = -std::max(
                1, static_cast<int>(std::lround(11.0 * content_scale))
            );
            HFONT cash_font = CreateFontW(
                font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS,
                L"MS Sans Serif"
            );
            HGDIOBJ previous_cash_font = cash_font != nullptr
                ? SelectObject(target, cash_font)
                : nullptr;
            const int previous_cash_background = SetBkMode(
                target, TRANSPARENT
            );
            const COLORREF previous_cash_color = SetTextColor(
                target, RGB(0, 255, 0)
            );
            const std::wstring cash = metaverse::PavilionCreditText(
                round_.credits, *category
            );
            TextOutW(
                target,
                content_x + static_cast<int>(std::lround(
                    metaverse::kPavilionCreditsX * content_scale
                )),
                content_y + static_cast<int>(std::lround(
                    metaverse::kPavilionCreditsY * content_scale
                )),
                cash.c_str(), static_cast<int>(cash.size())
            );

            const int rating_x = *category == metaverse::JudgingCategory::looks
                ? 56
                : 69;
            const int rating_y = *category == metaverse::JudgingCategory::looks
                ? 11
                : 63;
            if ((*category == metaverse::JudgingCategory::looks &&
                 looks_rating_visible_) ||
                (*category != metaverse::JudgingCategory::looks &&
                 wide_rating_visible_)) {
                draw_logical_overlay(
                    judging_rating_bitmap_, judging_rating_width_,
                    judging_rating_height_, rating_x, rating_y
                );
            }
            for (std::size_t slot = 0;
                 slot < judging_portrait_bitmaps_.size(); ++slot) {
                draw_logical_overlay(
                    judging_portrait_bitmaps_[slot],
                    metaverse::kPavilionPortraitSize,
                    metaverse::kPavilionPortraitSize,
                    metaverse::kPavilionPortraitX +
                        static_cast<int>(slot) *
                            metaverse::kPavilionPortraitPitch,
                    metaverse::kPavilionPortraitY
                );
            }

            // Every invocation frames all portraits in white. Looks +0x70 and
            // Talent/Brains +0x64 add red only during their one armed paint;
            // a later paint intersecting the strip deliberately erases it.
            const bool draw_looks_selection_frame =
                *category == metaverse::JudgingCategory::looks &&
                looks_selection_frame_dirty_;
            const bool draw_wide_selection_frame =
                *category != metaverse::JudgingCategory::looks &&
                metaverse::ConsumeWidePavilionActiveFrame(
                    wide_selection_frame_dirty_, judging_visit_,
                    judging_contestant_active_, judging_contestant_
                );
            const auto scaled_rect = [&](int left, int top, int right, int bottom) {
                return RECT{
                    content_x + static_cast<LONG>(std::lround(
                        left * content_scale
                    )),
                    content_y + static_cast<LONG>(std::lround(
                        top * content_scale
                    )),
                    content_x + static_cast<LONG>(std::lround(
                        right * content_scale
                    )),
                    content_y + static_cast<LONG>(std::lround(
                        bottom * content_scale
                    )),
                };
            };
            HBRUSH white_frame = static_cast<HBRUSH>(
                GetStockObject(WHITE_BRUSH)
            );
            HBRUSH active_frame = CreateSolidBrush(RGB(255, 0, 0));
            for (std::size_t slot = 0;
                 slot < judging_portrait_bitmaps_.size(); ++slot) {
                const int left = metaverse::kPavilionPortraitX +
                    static_cast<int>(slot) * metaverse::kPavilionPortraitPitch;
                RECT frame = scaled_rect(
                    left,
                    metaverse::kPavilionPortraitY,
                    left + metaverse::kPavilionPortraitSize,
                    metaverse::kPavilionPortraitY +
                        metaverse::kPavilionPortraitSize
                );
                FrameRect(target, &frame, white_frame);
                if (((*category != metaverse::JudgingCategory::looks &&
                      draw_wide_selection_frame) ||
                     (*category == metaverse::JudgingCategory::looks &&
                      draw_looks_selection_frame)) &&
                    metaverse::PavilionPortraitHasActiveFrame(
                        judging_visit_, judging_contestant_active_,
                        judging_contestant_, slot
                    ) && active_frame != nullptr) {
                    FrameRect(target, &frame, active_frame);
                }
            }
            if (active_frame != nullptr) {
                DeleteObject(active_frame);
            }
            if (*category == metaverse::JudgingCategory::looks) {
                // 0x00412c93 clears +0x70 even if clipping prevented the
                // requested FrameRect calls from changing visible pixels.
                looks_selection_frame_dirty_ = false;
            }

            if (judging_hovered_portrait_ &&
                *judging_hovered_portrait_ < round_.contestants.size()) {
                const std::size_t slot = *judging_hovered_portrait_;
                const std::int32_t contestant_id =
                    round_.contestants[slot].contestant_id;
                if (contestant_id >= 1 && contestant_id <= 10) {
                    const std::wstring_view name =
                        metaverse::kContestantNames[
                            static_cast<std::size_t>(contestant_id - 1)
                        ];
                    TextOutW(
                        target,
                        content_x + static_cast<int>(std::lround(
                            (metaverse::kPavilionPortraitX +
                             static_cast<int>(slot) *
                                 metaverse::kPavilionPortraitPitch) *
                            content_scale
                        )),
                        content_y + static_cast<int>(std::lround(
                            metaverse::kPavilionPortraitNameY * content_scale
                        )),
                        name.data(), static_cast<int>(name.size())
                    );
                }
            }
            SetTextColor(target, previous_cash_color);
            SetBkMode(target, previous_cash_background);
            if (previous_cash_font != nullptr) {
                SelectObject(target, previous_cash_font);
            }
            if (cash_font != nullptr) {
                DeleteObject(cash_font);
            }
        }

        if (scene_ == kWeightScene) {
            metaverse::CategoryWeightPaintPlan plan;
            if (host_interstitial_active_) {
                // MM.EXE's C91 runner is a separate modal window over the
                // already-painted Weight dialog. Native composites it in this
                // HWND, so reproduce that retained parent surface explicitly.
                plan.draw_base = true;
            } else {
                plan = metaverse::ConsumeCategoryWeightPaint(
                    weight_painter_, weight_pressed_control_
                );
            }
            // 0x004105bb consumes dirty layers in this strict order. It checks
            // only current_gauge, leaving any other gauge flags pending.
            if (plan.draw_base) {
                draw_logical_overlay(
                    bitmap_, bitmap_width_, bitmap_height_, 0, 0
                );
            }
            if (plan.draw_ok) {
                draw_logical_overlay(
                    weight_ok_bitmap_,
                    metaverse::kCategoryWeightOkRect.Width(),
                    metaverse::kCategoryWeightOkRect.Height(),
                    metaverse::kCategoryWeightOkRect.left,
                    metaverse::kCategoryWeightOkRect.top
                );
            }
            if (plan.control) {
                const std::size_t control = *plan.control;
                const auto& rect =
                    metaverse::kCategoryWeightControlRects[control];
                draw_logical_overlay(
                    weight_control_bitmaps_[control],
                    rect.Width(),
                    rect.Height(),
                    rect.left,
                    rect.top
                );
            }
            if (plan.gauge && weight_gauge_visible_[*plan.gauge]) {
                const std::size_t category = *plan.gauge;
                draw_logical_overlay(
                    weight_gauge_bitmaps_[category],
                    metaverse::kCategoryWeightGaugeSizes[category].width,
                    metaverse::kCategoryWeightGaugeSizes[category].height,
                    metaverse::kCategoryWeightGaugePositions[category].x,
                    metaverse::kCategoryWeightGaugePositions[category].y
                );
            }
        }

        if (navigation_hub_active_ && simm_pending_award_) {
            // The mode-3 hit handler paints the computed award immediately at
            // parent-client (500,20), while the reaction child continues. It
            // does not commit the document balance until the modal runner
            // returns. The original uses transparent white 24-point Arial.
            const int font_height = -std::max(
                1, static_cast<int>(std::lround(32.0 * content_scale))
            );
            HFONT font = CreateFontW(
                font_height, 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, ANSI_CHARSET,
                OUT_STROKE_PRECIS, CLIP_STROKE_PRECIS, DRAFT_QUALITY,
                0x22, L"Arial"
            );
            HGDIOBJ previous_font = font != nullptr
                ? SelectObject(target, font)
                : nullptr;
            const int previous_background = SetBkMode(target, TRANSPARENT);
            const COLORREF previous_color = SetTextColor(
                target, RGB(255, 255, 255)
            );
            wchar_t amount[40] = {};
            swprintf_s(
                amount, L"$%4.2f", static_cast<double>(*simm_pending_award_)
            );
            TextOutW(
                target,
                content_x + static_cast<int>(std::lround(
                    500.0 * content_scale
                )),
                content_y + static_cast<int>(std::lround(
                    20.0 * content_scale
                )),
                amount, static_cast<int>(wcslen(amount))
            );
            SetTextColor(target, previous_color);
            SetBkMode(target, previous_background);
            if (previous_font != nullptr) {
                SelectObject(target, previous_font);
            }
            if (font != nullptr) {
                DeleteObject(font);
            }
        }

        if (navigation_hub_active_ && sprite_bitmap_ != nullptr &&
            sprite_width_ > 0 && sprite_height_ > 0) {
            const int client_width = client.right - client.left;
            const int client_height = client.bottom - client.top;
            const double logical_scale = std::min(
                static_cast<double>(client_width) / 640.0,
                static_cast<double>(client_height) / 480.0
            );
            const int logical_x = static_cast<int>(
                std::lround((client_width - 640.0 * logical_scale) / 2.0)
            );
            const int logical_y = static_cast<int>(
                std::lround((client_height - 480.0 * logical_scale) / 2.0)
            );
            const int destination_x = logical_x + static_cast<int>(std::lround(
                static_cast<double>(motion_player_.X()) * logical_scale
            ));
            const auto sprite_bounds =
                metaverse::MotionSpriteBoundsFromTopLeft(
                    motion_player_.X(), motion_player_.Y(),
                    sprite_width_, sprite_height_
                );
            const int destination_y = logical_y + static_cast<int>(std::lround(
                static_cast<double>(sprite_bounds.top) * logical_scale
            ));
            const int destination_width = static_cast<int>(std::lround(
                static_cast<double>(sprite_width_) * logical_scale
            ));
            const int destination_height = static_cast<int>(std::lround(
                static_cast<double>(sprite_height_) * logical_scale
            ));
            HDC source = CreateCompatibleDC(target);
            HGDIOBJ previous = SelectObject(source, sprite_bitmap_);
            const COLORREF key = GetPixel(source, 0, 0);
            SetStretchBltMode(target, COLORONCOLOR);
            TransparentBlt(
                target,
                destination_x,
                destination_y,
                destination_width,
                destination_height,
                source,
                0,
                0,
                sprite_width_,
                sprite_height_,
                key
            );
            SelectObject(source, previous);
            DeleteDC(source);
        }

        if (scene_ == kCommentsScene) {
            const auto draw_bitmap = [&](HBITMAP bitmap, int x, int y, bool transparent) {
                if (bitmap == nullptr) {
                    return;
                }
                BITMAP details = {};
                if (!GetObjectW(bitmap, sizeof(details), &details)) {
                    return;
                }
                HDC source = CreateCompatibleDC(target);
                HGDIOBJ previous = SelectObject(source, bitmap);
                const int destination_x = content_x + static_cast<int>(
                    std::lround(static_cast<double>(x) * content_scale)
                );
                const int destination_y = content_y + static_cast<int>(
                    std::lround(static_cast<double>(y) * content_scale)
                );
                const int destination_width = static_cast<int>(
                    std::lround(static_cast<double>(details.bmWidth) * content_scale)
                );
                const int destination_height = static_cast<int>(
                    std::lround(static_cast<double>(details.bmHeight) * content_scale)
                );
                SetStretchBltMode(target, COLORONCOLOR);
                if (transparent) {
                    const COLORREF key = GetPixel(source, 0, 0);
                    TransparentBlt(
                        target, destination_x, destination_y,
                        destination_width, destination_height,
                        source, 0, 0, details.bmWidth, details.bmHeight, key
                    );
                } else {
                    StretchBlt(
                        target, destination_x, destination_y,
                        destination_width, destination_height,
                        source, 0, 0, details.bmWidth, details.bmHeight, SRCCOPY
                    );
                }
                SelectObject(source, previous);
                DeleteDC(source);
            };

            const bool restore_modal_parent = host_interstitial_active_;
            metaverse::CommentPaintPlan plan;
            if (restore_modal_parent) {
                // C51 is a child modal in MM.EXE. Native composites it in the
                // owner HWND, so rebuild the already-painted parent surface
                // beneath each host frame without consuming dirty state.
                plan.draw_base = true;
                plan.draw_accept = comment_accept_pressed_;
                plan.draw_hand = comment_hand_visible_;
                plan.draw_thermometer = true;
            } else {
                plan = metaverse::ConsumeCommentPaint(comment_painter_);
            }

            if (plan.draw_base) {
                draw_bitmap(bitmap_, 0, 0, false);
            }
            if (plan.draw_accept) {
                draw_bitmap(
                    comment_accept_bitmap_,
                    metaverse::kCommentAcceptLeft,
                    metaverse::kCommentAcceptTop,
                    false
                );
            }
            if (restore_modal_parent) {
                for (std::size_t comment = 0;
                     comment < comment_phrase_bitmaps_.size(); ++comment) {
                    draw_bitmap(
                        comment_phrase_bitmaps_[comment],
                        metaverse::kCommentPhraseLeft,
                        metaverse::kCommentPhraseTop +
                            static_cast<int>(comment) *
                                metaverse::kCommentPhrasePitch,
                        false
                    );
                }
            } else if (plan.comment) {
                const std::size_t comment = *plan.comment;
                draw_bitmap(
                    comment_phrase_bitmaps_[comment],
                    metaverse::kCommentPhraseLeft,
                    metaverse::kCommentPhraseTop +
                        static_cast<int>(comment) *
                            metaverse::kCommentPhrasePitch,
                    false
                );
            }
            if (plan.draw_hand) {
                draw_bitmap(
                    comment_hand_bitmap_, metaverse::kCommentHandLeft,
                    metaverse::kCommentHandTop +
                        comment_hand_row_ *
                            metaverse::kCommentPhrasePitch,
                    true
                );
            }
            if (plan.draw_thermometer) {
                draw_bitmap(
                    comment_thermometer_bitmap_,
                    metaverse::kCommentThermometerLeft,
                    metaverse::kCommentThermometerTop,
                    false
                );
            }

            const auto draw_rank = [&](std::int32_t comment) {
                if (comment < 0 || comment >=
                    static_cast<std::int32_t>(
                        comment_order_.assigned_rank_by_comment.size()
                    )) {
                    return;
                }
                const std::int32_t rank =
                    comment_order_.assigned_rank_by_comment[
                        static_cast<std::size_t>(comment)
                    ];
                if (rank < 0) {
                    return;
                }
                const std::wstring text = std::to_wstring(rank);
                TextOutW(
                    target,
                    content_x + static_cast<int>(std::lround(
                        static_cast<double>(metaverse::kCommentRankLeft) *
                        content_scale
                    )),
                    content_y + static_cast<int>(std::lround(
                        (static_cast<double>(metaverse::kCommentRankTop) +
                         static_cast<double>(comment) *
                             metaverse::kCommentPhrasePitch) *
                        content_scale
                    )),
                    text.c_str(),
                    static_cast<int>(text.size())
                );
            };
            if (restore_modal_parent || plan.rank_comment) {
                const int font_height = std::max(
                    12, static_cast<int>(std::lround(20.0 * content_scale))
                );
                HFONT font = CreateFontW(
                    font_height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial"
                );
                HGDIOBJ previous_font = SelectObject(target, font);
                const int previous_background = SetBkMode(target, TRANSPARENT);
                const COLORREF previous_color = SetTextColor(
                    target, RGB(255, 0, 0)
                );
                if (restore_modal_parent) {
                    for (std::int32_t comment = 0;
                         comment < static_cast<std::int32_t>(
                             comment_order_.assigned_rank_by_comment.size()
                         ); ++comment) {
                        draw_rank(comment);
                    }
                } else {
                    draw_rank(*plan.rank_comment);
                }
                SetTextColor(target, previous_color);
                SetBkMode(target, previous_background);
                SelectObject(target, previous_font);
                DeleteObject(font);
            }
        }

        if (scene_ == kCryoScene) {
            const auto draw_cryo_bitmap = [&](HBITMAP bitmap, int x, int y) {
                if (bitmap == nullptr) {
                    return;
                }
                BITMAP details = {};
                if (!GetObjectW(bitmap, sizeof(details), &details)) {
                    return;
                }
                HDC source = CreateCompatibleDC(target);
                HGDIOBJ previous = SelectObject(source, bitmap);
                const int destination_x = content_x + static_cast<int>(
                    std::lround(static_cast<double>(x) * content_scale)
                );
                const int destination_y = content_y + static_cast<int>(
                    std::lround(static_cast<double>(y) * content_scale)
                );
                const int destination_width = static_cast<int>(
                    std::lround(static_cast<double>(details.bmWidth) * content_scale)
                );
                const int destination_height = static_cast<int>(
                    std::lround(static_cast<double>(details.bmHeight) * content_scale)
                );
                SetStretchBltMode(target, COLORONCOLOR);
                StretchBlt(
                    target, destination_x, destination_y,
                    destination_width, destination_height,
                    source, 0, 0, details.bmWidth, details.bmHeight, SRCCOPY
                );
                SelectObject(source, previous);
                DeleteDC(source);
            };
            const auto draw_cryo_highlight = [&](HBITMAP bitmap, int x, int y) {
                if (bitmap == nullptr) {
                    return;
                }
                BITMAP details = {};
                if (!GetObjectW(bitmap, sizeof(details), &details)) {
                    return;
                }
                HDC source = CreateCompatibleDC(target);
                HGDIOBJ previous = SelectObject(source, bitmap);
                const int destination_x = content_x + static_cast<int>(
                    std::lround(static_cast<double>(x) * content_scale)
                );
                const int destination_y = content_y + static_cast<int>(
                    std::lround(static_cast<double>(y) * content_scale)
                );
                const int destination_width = static_cast<int>(
                    std::lround(static_cast<double>(details.bmWidth) * content_scale)
                );
                const int destination_height = static_cast<int>(
                    std::lround(static_cast<double>(details.bmHeight) * content_scale)
                );
                TransparentBlt(
                    target, destination_x, destination_y,
                    destination_width, destination_height,
                    source, 0, 0, details.bmWidth, details.bmHeight,
                    RGB(0, 0, 0)
                );
                SelectObject(source, previous);
                DeleteDC(source);
            };

            if (cryo_media_children_constructed_) {
                draw_cryo_bitmap(
                    cryo_rotate_bitmap_,
                    metaverse::kCryoRotationChildPlan.x,
                    metaverse::kCryoRotationChildPlan.y
                );
            }
            draw_cryo_bitmap(cryo_controls_bitmap_, 0, 0);
            if (cryo_pressed_action_ >=
                    static_cast<int>(metaverse::CryoAction::browse_left) &&
                cryo_pressed_action_ <=
                    static_cast<int>(metaverse::CryoAction::sponsor)) {
                const std::size_t highlight = static_cast<std::size_t>(
                    cryo_pressed_action_ - static_cast<int>(
                        metaverse::CryoAction::browse_left
                    )
                );
                draw_cryo_highlight(
                    cryo_control_highlight_bitmaps_[highlight],
                    metaverse::kCryoControlRects[highlight].left,
                    metaverse::kCryoControlRects[highlight].top
                );
            }
            draw_cryo_bitmap(cryo_money_bitmap_, 358, 3);
            if (cryo_info_visible_) {
                draw_cryo_bitmap(cryo_info_background_bitmap_, 432, 61);
                draw_cryo_bitmap(
                    cryo_info_bitmap_,
                    metaverse::kCryoInformationChildPlan.x,
                    metaverse::kCryoInformationChildPlan.y
                );
                if (cryo_pressed_action_ >=
                        static_cast<int>(metaverse::CryoAction::name_bio) &&
                    cryo_pressed_action_ <= static_cast<int>(
                        metaverse::CryoAction::favorite_quote
                    )) {
                    const std::size_t highlight = static_cast<std::size_t>(
                        cryo_pressed_action_ - static_cast<int>(
                            metaverse::CryoAction::name_bio
                        )
                    );
                    draw_cryo_highlight(
                        cryo_info_highlight_bitmaps_[highlight],
                        metaverse::kCryoDetailRects[highlight].left,
                        metaverse::kCryoDetailRects[highlight].top
                    );
                }
            }
            for (std::size_t slot = 0; slot < selected_contestants_.size(); ++slot) {
                if (slot < cryo_portrait_bitmaps_.size()) {
                    HBITMAP portrait = cryo_portrait_bitmaps_[slot];
                    draw_cryo_bitmap(
                        portrait,
                        287 + static_cast<int>(slot) * 70,
                        412
                    );
                }
            }

            // Dialog resource 158 is DS_SETFONT, 8-point MS Sans Serif. The
            // original painter uses transparent TextOut at exact point
            // coordinates, with normal weight and no vertical centering.
            const int font_height = -std::max(
                1, static_cast<int>(std::lround(11.0 * content_scale))
            );
            HFONT font = CreateFontW(
                font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS,
                L"MS Sans Serif"
            );
            HGDIOBJ previous_font = font != nullptr
                ? SelectObject(target, font)
                : nullptr;
            SetBkMode(target, TRANSPARENT);
            SetTextColor(target, RGB(0, 255, 0));
            wchar_t candidate_cost[32] = {};
            swprintf_s(
                candidate_cost, L"$%d",
                static_cast<int>(metaverse::ContestantSponsorCost(
                    browsed_contestant_ + 1
                ))
            );
            TextOutW(
                target,
                content_x + static_cast<int>(305 * content_scale),
                content_y + static_cast<int>(129 * content_scale),
                candidate_cost,
                static_cast<int>(wcslen(candidate_cost))
            );
            SetTextColor(target, RGB(0, 0, 0));
            wchar_t cash[40] = {};
            swprintf_s(cash, L"$%.2f", static_cast<double>(round_.credits));
            TextOutW(
                target,
                content_x + static_cast<int>(465 * content_scale),
                content_y + static_cast<int>(24 * content_scale),
                cash,
                static_cast<int>(wcslen(cash))
            );
            if (previous_font != nullptr) {
                SelectObject(target, previous_font);
            }
            if (font != nullptr) {
                DeleteObject(font);
            }
        }

        if (AtSlotMachineNode() || scene_ == kSlotsScene) {
            if (slot_result_visible_ && slot_outcome_ && slot_outcome_->spun) {
                for (std::size_t reel = 0; reel < slot_bitmaps_.size(); ++reel) {
                    if (slot_bitmaps_[reel] == nullptr) {
                        continue;
                    }
                    BITMAP symbol = {};
                    if (!GetObjectW(
                            slot_bitmaps_[reel], sizeof(symbol), &symbol
                        )) {
                        continue;
                    }
                    draw_logical_overlay(
                        slot_bitmaps_[reel], symbol.bmWidth, symbol.bmHeight,
                        metaverse::kSlotSymbolX[reel],
                        metaverse::kSlotSymbolY
                    );
                }
            }

            const int font_height = -std::max(
                1, static_cast<int>(std::lround(32.0 * content_scale))
            );
            HFONT font = CreateFontW(
                font_height, 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, ANSI_CHARSET,
                OUT_STROKE_PRECIS, CLIP_STROKE_PRECIS, DRAFT_QUALITY,
                0x22, L"Arial"
            );
            HGDIOBJ previous_font = SelectObject(target, font);
            const int previous_background = SetBkMode(target, TRANSPARENT);
            const auto draw_slot_text = [&](const std::wstring& text,
                                            int logical_x,
                                            int logical_y,
                                            COLORREF color) {
                SetTextColor(target, color);
                TextOutW(
                    target,
                    content_x + static_cast<int>(std::lround(
                        logical_x * content_scale
                    )),
                    content_y + static_cast<int>(std::lround(
                        logical_y * content_scale
                    )),
                    text.c_str(), static_cast<int>(text.size())
                );
            };

            wchar_t credits[40] = {};
            swprintf_s(
                credits, L"$%4.2f", static_cast<double>(round_.credits)
            );
            draw_slot_text(
                credits,
                metaverse::kSlotCreditsX,
                metaverse::kSlotCreditsY,
                RGB(0, 0, 255)
            );
            if (slot_no_credit_visible_) {
                draw_slot_text(
                    L"NO CREDIT",
                    metaverse::kSlotNoCreditX,
                    metaverse::kSlotCreditsY,
                    RGB(255, 0, 48)
                );
            } else if (slot_result_visible_ && slot_outcome_ &&
                       slot_outcome_->spun) {
                wchar_t amount[40] = {};
                swprintf_s(
                    amount, L"$%4.2f",
                    static_cast<double>(slot_outcome_->credit_change)
                );
                draw_slot_text(
                    amount,
                    metaverse::SlotResultAmountX(*slot_outcome_),
                    metaverse::kSlotCreditsY,
                    RGB(255, 0, 48)
                );
            }
            SetBkMode(target, previous_background);
            SelectObject(target, previous_font);
            DeleteObject(font);
        }

        if (host_interstitial_active_ && host_bitmap_ != nullptr &&
            host_bitmap_width_ > 0 && host_bitmap_height_ > 0) {
            HDC source = CreateCompatibleDC(target);
            HGDIOBJ previous = SelectObject(source, host_bitmap_);
            const COLORREF key = GetPixel(source, 0, 0);
            SetStretchBltMode(target, COLORONCOLOR);
            TransparentBlt(
                target,
                content_x + static_cast<int>(std::lround(
                    host_bitmap_x_ * content_scale
                )),
                content_y + static_cast<int>(std::lround(
                    host_bitmap_y_ * content_scale
                )),
                static_cast<int>(std::lround(
                    host_bitmap_width_ * content_scale
                )),
                static_cast<int>(std::lround(
                    host_bitmap_height_ * content_scale
                )),
                source,
                0,
                0,
                host_bitmap_width_,
                host_bitmap_height_,
                key
            );
            SelectObject(source, previous);
            DeleteDC(source);
        }

        // Keep the 640x480 game canvas free of port-only help/status text.
        // Diagnostic text remains available in the native window title.
        if (pending_navigation_fade_target_) {
            const RECT fade_rect{
                content_x,
                content_y,
                content_x + static_cast<LONG>(std::lround(
                    640.0 * content_scale
                )),
                content_y + static_cast<LONG>(std::lround(
                    480.0 * content_scale
                )),
            };
            PaintNavigationPaletteFade(target, fade_rect);
        }

        BitBlt(
            paint_target, 0, 0, client_width, client_height,
            target, 0, 0, SRCCOPY
        );
        SelectObject(target, previous_target_bitmap);
        DeleteObject(target_bitmap);
        DeleteDC(target);
        EndPaint(window, &paint);
    }

    void PaintNavigationPaletteFade(HDC target, const RECT& client) const {
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0) {
            return;
        }

        const auto elapsed = std::chrono::steady_clock::now() -
            navigation_fade_started_;
        const double progress = std::clamp(
            std::chrono::duration<double>(elapsed).count() /
                std::chrono::duration<double>(kNavigationFadeDuration).count(),
            0.0,
            1.0
        );
        const std::uint32_t completed_passes =
            metaverse::NavigationPaletteFadePassForProgress(progress);
        if (completed_passes == 0) {
            return;
        }

        BITMAPINFO information = {};
        information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        information.bmiHeader.biWidth = width;
        information.bmiHeader.biHeight = -height;
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;
        void* raw_pixels = nullptr;
        HBITMAP surface = CreateDIBSection(
            target, &information, DIB_RGB_COLORS, &raw_pixels, nullptr, 0
        );
        if (surface == nullptr || raw_pixels == nullptr) {
            if (surface != nullptr) {
                DeleteObject(surface);
            }
            return;
        }
        HDC buffer = CreateCompatibleDC(target);
        if (buffer == nullptr) {
            DeleteObject(surface);
            return;
        }
        HGDIOBJ previous = SelectObject(buffer, surface);
        BitBlt(
            buffer, 0, 0, width, height,
            target, client.left, client.top, SRCCOPY
        );
        auto* pixels = static_cast<std::uint32_t*>(raw_pixels);
        const std::size_t pixel_count = static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height);
        for (std::size_t index = 0; index < pixel_count; ++index) {
            pixels[index] = metaverse::ApplyNavigationPaletteFadeToColor(
                pixels[index], completed_passes
            );
        }
        BitBlt(
            target, client.left, client.top, width, height,
            buffer, 0, 0, SRCCOPY
        );
        SelectObject(buffer, previous);
        DeleteDC(buffer);
        DeleteObject(surface);
    }

    void UpdateTitle(HWND window) const {
        // The shared movie-window setup at 0x0040802f and each recovered
        // top-level scene constructor set this exact title. Port diagnostics
        // must not leak into the player-visible caption.
        SetWindowTextW(window, L"Ms. Metaverse");
    }

private:
    [[nodiscard]] bool SynchronousMciWaitActive() const {
        return metaverse::LegacyPlaybackBlocksOwnerInput(
                   metaverse::LegacyPlaybackWaitStyle::synchronous_mci_wait
               ) &&
               (pending_penalty_advance_.has_value() ||
                looks_rotate_playing_);
    }

    [[nodiscard]] bool LegacyPlaybackBlocksSceneInput() const {
        // Resource-158 intro/finale media is modal. TP/BP/LP and Looks rotate
        // are completion-gated MCI Wait calls. Keep pointer/menu input out of
        // the covered owner scene while the asynchronous native decoder
        // reproduces either boundary. Slots is deliberately absent: its
        // original 4.5-second loop pumps messages at 0x0040ad0f.
        return scene_ == kIntroScene || pending_tally_finale_ ||
               pending_exit_after_credit_ || SynchronousMciWaitActive();
    }

    [[nodiscard]] bool HasSecondaryAssets() const {
        return assets2_.has_value();
    }

    [[nodiscard]] bool IsSecondaryAssetScene() const {
        return scene_ == kTalentPavilionScene ||
               scene_ == kTalentJudgingScene ||
               scene_ == kCrossroads2Scene || scene_ == kTallyScene;
    }

    [[nodiscard]] bool IsCrossroadsScene() const {
        return scene_ == kCrossroadsScene || scene_ == kCrossroads2Scene;
    }

    [[nodiscard]] const std::filesystem::path& SceneAssetsRoot() const {
        return IsSecondaryAssetScene() && assets2_ ? *assets2_ : assets_;
    }

    [[nodiscard]] const std::filesystem::path& JudgingAssetsRoot(
        metaverse::JudgingCategory category
    ) const {
        return category == metaverse::JudgingCategory::talent && assets2_
            ? *assets2_
            : assets_;
    }

    bool LoadNavigationEntry(
        std::size_t entry_index,
        std::string& error,
        bool stop_legacy_sound = true
    ) {
        error.clear();
        if (navigation_entries_.empty() && !metaverse::LoadNavigationData(
                assets_ / L"DAT" / L"NAV" / L"NAVIGATE.DAT",
                navigation_entries_, error
            )) {
            return false;
        }
        if (entry_index >= navigation_entries_.size()) {
            error = "navigation entry is out of range: " +
                    std::to_string(entry_index);
            return false;
        }

        const auto& entry = navigation_entries_[entry_index];
        if (entry.scene == "xr1") {
            scene_ = kCrossroadsScene;
        } else if (entry.scene == "xr2") {
            scene_ = kCrossroads2Scene;
        } else if (entry.scene == "talnav") {
            scene_ = kTalentPavilionScene;
        } else if (entry.scene == "brains") {
            scene_ = 4;
        } else if (entry.scene == "looks") {
            scene_ = 6;
        } else if (entry.scene == "hall") {
            scene_ = 8;
        } else if (entry.scene == "tally") {
            scene_ = kTallyScene;
        } else {
            error = "unsupported navigation scene: " + entry.scene;
            return false;
        }
        if (IsSecondaryAssetScene() && !HasSecondaryAssets()) {
            error = "supplemental asset directory is unavailable for navigation scene " +
                    entry.scene;
            return false;
        }

        navigation_hub_active_ = true;
        navigation_entry_index_ = entry_index;
        navigation_variant_ = static_cast<std::size_t>(entry.variant);
        pending_navigation_resolution_ = false;
        navigation_idle_animation_active_ = false;
        navigation_idle_animation_used_.fill(false);
        navigation_popup_idle_latch_.ResetForTransition();
        // fcn.0040cb7d destroys the old navigation popup and calls the full
        // fcn.0040a0db constructor before every cross-scene DAT load. That
        // constructor zeros +0x144/+0x148, so returning to XR1 must re-arm
        // object 18's long 0x0010 transition instead of carrying its consumed
        // state across native scene loads.
        navigation_popup_one_shot_state_.ResetForConstruction();
        LoadScene(true, true, stop_legacy_sound);
        if (!navigator_) {
            error = "navigation scene failed to initialize";
            return false;
        }
        // 0x0040a6ef clears one +0x578 consumed-state DWORD after every
        // successfully parsed object. Cross-scene reloads reuse this native
        // window, so explicitly rearm the same new-scene ordinal prefix.
        metaverse::ResetNavigationEncounterFlagsForScene(
            navigation_encountered_nodes_,
            scene_data_ ? scene_data_->objects.size() : 0
        );
        // Construction plays an initial 0x0400 cue and starts an initial
        // 0x0100 range before its final srand(timeGetTime()). Defer the seed
        // into ResolveNavigationNode so that media-trigger order remains exact.
        navigation_loader_seed_pending_ = true;
        navigation_idle_due_ = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(
                metaverse::kNavigationIdleAnimationDelayMs
            );
        return true;
    }

    void SeedNavigationRandomAfterSceneConstruction() {
        if (!navigation_loader_seed_pending_) {
            return;
        }
        // fcn.0040a43f performs this after the omitted physical-CD validator.
        // Simm and Slots draws begin from this millisecond seed unless another
        // generic-media object subsequently reseeds the shared CRT stream.
        random_.Seed(timeGetTime());
        navigation_loader_seed_pending_ = false;
    }

    void EnterNavigationHub(
        std::size_t state,
        HWND window,
        bool stop_legacy_sound = true
    ) {
        // The outer orchestrator destroys and reconstructs the navigation
        // popup after every pavilion. These fields belong to that popup, not
        // the round document, so each new visit rearms flagged object IDs and
        // XR1's long one-shot transition.
        navigation_encountered_nodes_.fill(false);
        pending_navigation_encounter_node_.reset();
        if (state == 1) {
            std::array<std::int32_t, metaverse::kContestantsPerRound>
                contestant_ids{};
            for (std::size_t index = 0; index < contestant_ids.size(); ++index) {
                contestant_ids[index] = round_.contestants[index].contestant_id;
            }
            metaverse::ArmSimmSelectionPool(
                simm_selection_table_, contestant_ids
            );
        }
        pending_navigation_fade_target_.reset();
        std::string error;
        if (!LoadNavigationEntry(state, error, stop_legacy_sound)) {
            status_ = L"Navigation hub failed: " +
                      std::wstring(error.begin(), error.end());
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        status_.clear();
        ResolveNavigationNode(window);
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void EnterJudging(
        metaverse::JudgingCategory category,
        HWND window
    ) {
        navigation_hub_active_ = false;
        pending_navigation_resolution_ = false;
        pending_navigation_fade_target_.reset();
        // The document's +0x230/+0x234/+0x238 C-entry flags survive dialog
        // destruction, but each Talent/Brains constructor initializes its own
        // +0x6c/+0x70 C71/C61 availability fields to one. Re-entering an
        // incomplete pavilion therefore rearms its first event clips without
        // replaying C21/C31/C41.
        const std::size_t category_index = static_cast<std::size_t>(category);
        first_gong_host_played_[category_index] = false;
        first_penalty_host_played_[category_index] = false;
        // GONG/%d.BMP is installed directly into the current dialog's
        // portrait child by the Gong handler.  The pavilion constructor has
        // no persistent gong-art field: on a later visit it rebuilds any
        // completed, non-disqualified portrait from DIMMED/%d.BMP.  Keep the
        // marker for repainting during this visit only.
        gonged_[category_index].fill(false);
        scene_ = category == metaverse::JudgingCategory::brains
            ? kBrainsJudgingScene
            : category == metaverse::JudgingCategory::looks
                ? kLooksJudgingScene
                : kTalentJudgingScene;
        // The outer orchestrator constructs the requested pavilion for every
        // hub result 2/3/4; it does not skip a dialog whose copied five-entry
        // pending array is already all zero. +0xac starts at zero, so retain
        // no active contestant even when no eligible portrait remains.
        judging_contestant_ = 0;
        judging_contestant_active_ = false;
        current_rating_ = 0;
        wide_selection_frame_dirty_ = false;
        wide_rating_visible_ = true;
        judging_visit_ = metaverse::BeginPavilionVisit(round_, category);
        if (category == metaverse::JudgingCategory::looks) {
            // The entry timer explicitly clears the current contestant ID
            // before its first activation scan. If the local pending array is
            // already empty, it remains zero even though ROTATE.AVI itself is
            // still constructed and retained as a hidden child.
            looks_current_contestant_id_ = 0;
            // Split Looks OnInit body 0x00412834-0x00412846 resets all four
            // region counters for each newly constructed pavilion dialog.
            // They persist across contestants only for the duration of this
            // one visit and must not leak through a hub exit/re-entry.
            looks_magnification_levels_.fill(
                metaverse::kLooksInitialMagnificationLevel
            );
            looks_selection_frame_dirty_ = false;
        }
        // The entry cue runs over the pavilion's inactive presentation area.
        // Looks leaves that rectangle as bare background and auto-selects only
        // after C41; Talent/Brains wait for a bottom-portrait click.
        LoadScene(false);
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void ActivateJudgingContestant(
        std::size_t slot,
        metaverse::JudgingCategory category,
        HWND window
    ) {
        judging_contestant_ = slot;
        judging_contestant_active_ = true;
        judging_comment_previewed_ = false;
        // A newly selected contestant always starts from a clean zero meter.
        // Looks has no 0.BMP, so its zero state is the repainted base beneath
        // the previous gauge.
        if (category == metaverse::JudgingCategory::looks) {
            current_rating_ = 0;
            suppress_judging_rating_bitmap_once_ = true;
            // Manual selection sets +0x70. The next painter invocation draws
            // the current pending slot red once, then consumes that flag.
            looks_selection_frame_dirty_ = true;
            // 0x00413688-0x00413749 restores the meter backing, updates the
            // selected ID, then shows/seeks the one dialog-lifetime ROTATE
            // child. It does not tear down/reconstruct the pavilion or restart
            // LOOKS.WAV on a manual portrait switch.
            LoadJudgingRatingBitmap(category);
            SeekLooksContestantFrame();
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        // Talent 0x00407266-0x0040740f and Brains
        // 0x0040948a-0x00409639 activate a contestant inside the retained
        // dialog. They do not reconstruct its background, controls, or five
        // portrait wrappers. The rating becomes zero only after the surcharge
        // succeeds; NOCASH2 clears the active slot while preserving the
        // chooser's prior numeric value and gauge. Its synchronous sound
        // replaces the pavilion loop, which is not restarted on this branch.
        float surcharge = 0.0f;
        std::string error;
        const std::int32_t contestant_id =
            round_.contestants[judging_contestant_].contestant_id;
        if (!metaverse::ChargePerformanceSurcharge(
                round_, contestant_id, surcharge, error
            )) {
            const auto transition =
                metaverse::PlanWidePavilionRatingTransition(
                    metaverse::WidePavilionRatingEvent::
                        performance_charge_failed,
                    current_rating_
                );
            current_rating_ = transition.numeric_rating_after;
            judging_contestant_active_ = false;
            performance_access_granted_ = false;
            active_performance_category_.reset();
            status_ = std::wstring(error.begin(), error.end()) + L".";
            const auto no_cash = JudgingAssetsRoot(category) / L"WAV" /
                L"ANNOUNCE" / L"NOCASH2.WAV";
            PlaySoundW(
                no_cash.c_str(), nullptr,
                SND_FILENAME | SND_SYNC | SND_NODEFAULT
            );
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        const auto transition =
            metaverse::PlanWidePavilionRatingTransition(
                metaverse::WidePavilionRatingEvent::
                    performance_charge_succeeded,
                current_rating_
            );
        current_rating_ = transition.numeric_rating_after;
        performance_access_granted_ = true;
        if (transition.replace_rating_bitmap) {
            LoadJudgingRatingBitmap(category);
            wide_rating_visible_ = true;
        }
        if (transition.hide_rating_surface) {
            wide_rating_visible_ = false;
        }
        // The original queues the updated cash, synchronously paints the
        // active portrait frame, then paints the reset 0.BMP gauge.
        RECT cash_rect{
            metaverse::kWidePavilionActivationCreditsBounds[0],
            metaverse::kWidePavilionActivationCreditsBounds[1],
            metaverse::kWidePavilionActivationCreditsBounds[2],
            metaverse::kWidePavilionActivationCreditsBounds[3],
        };
        InvalidateLogicalRect(window, cash_rect);
        wide_selection_frame_dirty_ = true;
        RECT portrait_strip{
            metaverse::kWidePavilionPortraitStripBounds[0],
            metaverse::kWidePavilionPortraitStripBounds[1],
            metaverse::kWidePavilionPortraitStripBounds[2],
            metaverse::kWidePavilionPortraitStripBounds[3],
        };
        InvalidateLogicalRect(window, portrait_strip);
        UpdateWindow(window);
        RECT rating_rect{
            metaverse::kWidePavilionRatingGraphicBounds[0],
            metaverse::kWidePavilionRatingGraphicBounds[1],
            metaverse::kWidePavilionRatingGraphicBounds[2],
            metaverse::kWidePavilionRatingGraphicBounds[3],
        };
        InvalidateLogicalRect(window, rating_rect);
        UpdateWindow(window);

        const auto video_path = JudgingVideoPath(category);
        active_performance_category_ = category;
        PlaySoundW(nullptr, nullptr, 0);
        if (video_path.empty() || !PlayStandaloneVideo(
                JudgingAssetsRoot(category), video_path, error
            )) {
            if (error.empty()) {
                error = "performance movie path could not be selected";
            }
            status_ = L"Video open failed: " +
                std::wstring(error.begin(), error.end());
        } else {
            wchar_t amount[32] = {};
            swprintf_s(amount, L"%.2f", static_cast<double>(surcharge));
            status_ = L"Performance surcharge: $" +
                std::wstring(amount) + L".";
        }
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    std::optional<std::size_t> NextEligibleJudgingSlot(
        metaverse::JudgingCategory category,
        std::size_t first
    ) const {
        (void)category;
        return metaverse::NextPavilionPendingSlot(judging_visit_, first);
    }

    void SkipNavigationTransition(HWND window) {
        // Command 206 reaches fcn.0040aa61, whose first state mutation clears
        // the popup activity latch even when a synthetic settled-state
        // command would have no active range to skip.
        navigation_popup_idle_latch_.ResetForTransition();
        const auto disposition = metaverse::NavigationSkipDispositionForState(
            pending_navigation_resolution_,
            slot_spin_active_,
            playback_range_.has_value()
        );
        if (disposition == metaverse::NavigationSkipDisposition::none) {
            // TrackPopupMenu runs a nested message loop. A range can finish
            // after command 206 was enabled but before the user selects it.
            // fcn.0040c0fe still writes +0x5c=0 and calls fcn.0040aa61; with
            // +0x118 now clear, that shared executor forcibly follows the
            // current object's middle range/link without checking its Up bit.
            MoveScene(metaverse::SceneDirection::up, window, true);
            return;
        }

        // Command 206 pauses MCI, seeks the active navigation movie to its
        // end, clears the transition flag, and relies on the aborted-play
        // notification to execute normal arrival processing. Preserve the
        // bounded range's last visible frame, then run that arrival work in
        // the native event loop.
        if (playback_range_ && video_decoder_.IsOpen()) {
            metaverse::VideoFrame frame;
            bool ended = false;
            std::string error;
            if (video_decoder_.SeekFrame(
                    playback_range_->second, frame, ended, error
                ) && !ended) {
                UploadVideoFrame(frame);
            }
        }
        playback_range_.reset();
        video_paused_ = true;
        audio_player_.Stop();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;

        if (disposition ==
            metaverse::NavigationSkipDisposition::reveal_slot_result) {
            // The seek aborts the notified Slots play. Its posted MCI notify is
            // pumped by the outer 4.5-second loop; fcn.0040b4e7 clears +0x140
            // and computes the result immediately, so input is live again even
            // though the original outer call does not return until its deadline.
            slot_spin_active_ = false;
            RevealSlotMachineResult(window);
        } else {
            CompleteNavigationPlayback(window);
        }
        UpdateTitle(window);
    }

    void CompleteNavigationPlayback(HWND window) {
        pending_navigation_resolution_ = false;
        if (navigation_idle_animation_active_) {
            navigation_idle_animation_active_ = false;
            // fcn.0040b4e7 handles an idle range exactly like a completed
            // arrival even though the current DAT object did not change: it
            // reactivates navigation sound, seeks frame 25, and may replay
            // that settled object's destination cue.
            navigation_arrival_pending_ = true;
        }
        ResolveNavigationNode(window);
        // ResolveNavigationNode may seek an authored destination hold frame,
        // start the next automatic range, load another DAT scene, or enter a
        // gameplay dialog. The decoder bitmap changes synchronously, but this
        // completion path used to return before posting another WM_PAINT. That
        // left the retained client surface showing the transition's final
        // frame after both natural completion and Skip Ahead/left-click abort.
        if (IsWindow(window)) {
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
        }
    }

    void ResolveNavigationNode(HWND window) {
        for (std::size_t guard = 0;
             navigation_hub_active_ && navigator_ && guard < 64;
             ++guard) {
            const metaverse::SceneObject* object = navigator_->CurrentObject();
            if (object == nullptr) {
                SeedNavigationRandomAfterSceneConstruction();
                status_ = L"Navigation hub reached a missing object.";
                return;
            }

            if ((object->flags & 0x0008) != 0) {
                SeedNavigationRandomAfterSceneConstruction();
                const std::int32_t action = object->auxiliary;
                if (action == 2) {
                    EnterJudging(metaverse::JudgingCategory::talent, window);
                } else if (action == 3) {
                    EnterJudging(metaverse::JudgingCategory::brains, window);
                } else if (action == 4) {
                    EnterJudging(metaverse::JudgingCategory::looks, window);
                } else if (action == 5) {
                    navigation_hub_active_ = false;
                    pending_navigation_resolution_ = false;
                    StartTallySequence(window);
                } else if (action == 6 || action == 16) {
                    // Crossroads action 6 and Hall action 16 both fall through
                    // the outer orchestrator's unhandled-result exit path.
                    DestroyWindow(window);
                } else if (action == 13) {
                    // The first-round Hall branch at 0x00401da5 jumps straight
                    // to Cryo construction at 0x004025e4. Unlike the later
                    // replay branch, it clears no document fields and consumes
                    // no additional rand() calls.
                    EnterInitialRoundFromHall(window);
                } else if (action == 14 || action == 15) {
                    // These Hall doors launched retired Virtual Vegas games
                    // through physical-disc loops. Return to the corresponding
                    // Hall variant without probing a drive or prompting for CD.
                    EnterNavigationHub(
                        static_cast<std::size_t>(action), window
                    );
                    status_ = L"That retired Virtual Vegas game is not part "
                              L"of the native offline port.";
                    UpdateTitle(window);
                } else {
                    status_ = L"Unsupported navigation action " +
                              std::to_wstring(action) + L".";
                }
                return;
            }

            if ((object->flags & 0x0200) != 0) {
                SeedNavigationRandomAfterSceneConstruction();
                const std::size_t target =
                    static_cast<std::size_t>(object->auxiliary);
                if (metaverse::CrossSceneTransitionForFlags(object->flags) ==
                    metaverse::CrossSceneTransition::fade_to_white) {
                    StartNavigationFade(target, window);
                    return;
                }
                std::string error;
                if (!LoadNavigationEntry(target, error)) {
                    status_ = L"Navigation scene switch failed: " +
                              std::wstring(error.begin(), error.end());
                    return;
                }
                continue;
            }

            // fcn.0040b4e7 evaluates the newly current object after the
            // transition executor has updated the DAT object pointer.  Its
            // 0x0400/0x0800 branches therefore play the destination object's
            // WAV, not the object that was just left.  Consume this state once
            // so repainting or re-resolving a settled node cannot replay it.
            bool initial_scene_entry = false;
            if (navigation_arrival_pending_) {
                navigation_arrival_pending_ = false;
                initial_scene_entry = navigation_initial_entry_pending_;
                navigation_initial_entry_pending_ = false;
                if (navigation_popup_one_shot_state_.
                        audio_reactivation_pending) {
                    // WaveMixActivate(session, TRUE) precedes all destination
                    // handling in fcn.0040b4e7. PlaySound has no persistent
                    // session to reactivate, so clearing the suspended state
                    // here makes the destination cue the first resumed sound.
                    navigation_popup_one_shot_state_.
                        audio_reactivation_pending = false;
                    navigation_popup_one_shot_state_.transition_completed =
                        true;
                }
                if (!initial_scene_entry) {
                    if (const auto hold_frame =
                            metaverse::NavigationArrivalHoldFrame(object->flags)) {
                        metaverse::SceneTransition hold;
                        hold.first_frame = *hold_frame;
                        hold.last_frame = *hold_frame;
                        PlayTransition(hold);
                    }
                }
                if (metaverse::ShouldPlayNavigationArrivalSound(
                        object->flags, initial_scene_entry
                    ) && object->asset &&
                    metaverse::IsPlayableNavigationAsset(*object->asset)) {
                    const auto sound_path = SceneAssetsRoot() / *object->asset;
                    PlaySoundW(
                        sound_path.c_str(), nullptr,
                        SND_FILENAME | SND_ASYNC | SND_NODEFAULT
                    );
                }
            }

            const bool advances_automatically =
                navigator_->CanAdvanceAutomatically() &&
                (!initial_scene_entry ||
                 metaverse::ShouldAdvanceNavigationOnInitialSceneEntry(
                     object->flags
                 ));
            if (!advances_automatically) {
                SeedNavigationRandomAfterSceneConstruction();
                UpdateNavigationOverlay(*object);
                ScheduleNavigationEncounter(*object);
                // The common post-notify tail at 0x0040c0ab stores
                // timeGetTime() after a transition settles.  This also rearms
                // the ten-second 0x0020 ambient-animation delay.
                navigation_idle_due_ = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(
                        metaverse::kNavigationIdleAnimationDelayMs
                    );
                return;
            }

            // fcn.0040b4e7 has three middle-link routing branches. 0x0800
            // plays its asset above, 0x0100 is a plain automatic node, and
            // 0x2000 opened the retired FTP custom-sprite dialog before the
            // same hop. The network dialog is intentionally absent offline,
            // but its unconditional DAT transition remains required.
            const auto one_shot = metaverse::OneShotTransitionForFlags(
                object->flags,
                navigation_popup_one_shot_state_.transition_completed
            );
            metaverse::SceneTransition transition;
            std::string error;
            if (!navigator_->AdvanceAutomatically(transition, error)) {
                SeedNavigationRandomAfterSceneConstruction();
                status_ = L"Automatic navigation failed: " +
                          std::wstring(error.begin(), error.end());
                return;
            }
            ApplyOneShotNavigationDeparture(one_shot, transition);
            navigation_arrival_pending_ = true;
            pending_navigation_resolution_ = true;
            ResetNavigationOverlay();
            PlayTransition(transition);
            SeedNavigationRandomAfterSceneConstruction();
            if (video_paused_ && !playback_range_) {
                pending_navigation_resolution_ = false;
                continue;
            }
            return;
        }
    }

    void ApplyOneShotNavigationDeparture(
        metaverse::OneShotNavigationTransition behavior,
        metaverse::SceneTransition& transition
    ) {
        if (behavior ==
            metaverse::OneShotNavigationTransition::animated_first_pass) {
            // The original deactivates its WaveMix session for the one-time
            // long XR1 transition and reactivates it on destination arrival.
            PlaySoundW(nullptr, nullptr, 0);
            navigation_effect_player_.Stop();
            navigation_popup_one_shot_state_.audio_reactivation_pending = true;
            return;
        }
        if (behavior !=
                metaverse::OneShotNavigationTransition::held_frame_after_first_pass ||
            !navigator_) {
            return;
        }
        const metaverse::SceneObject* destination = navigator_->CurrentObject();
        if (destination == nullptr) {
            return;
        }
        const std::int32_t frame =
            metaverse::RepeatedOneShotDestinationFrame(*destination);
        transition.first_frame = frame;
        transition.last_frame = frame;
    }

    void PlayNavigationDepartureSound(const metaverse::SceneObject& source) {
        if (!metaverse::ShouldPlayNavigationDepartureSound(source.flags)) {
            return;
        }
        PlayNavigationEffect(L"ZPR.WAV", navigation_departure_audio_);
    }

    void PlayNavigationRicochet() {
        PlayNavigationEffect(L"RICOCHET.WAV", navigation_ricochet_audio_);
    }

    void PlayNavigationEffect(
        const wchar_t* filename,
        std::optional<metaverse::AudioTrack>& cached
    ) {
        if (!cached) {
            metaverse::AudioTrack track;
            bool has_audio = false;
            std::string error;
            if (!metaverse::DecodeAudioTrack(
                    SceneAssetsRoot() / L"WAV" / L"NAV" / filename,
                    track, has_audio, error
                ) || !has_audio) {
                return;
            }
            cached = std::move(track);
        }
        std::string ignored;
        navigation_effect_player_.Play(*cached, false, ignored);
    }

    void ScheduleNavigationEncounter(const metaverse::SceneObject& object) {
        pending_navigation_encounter_node_.reset();
        if ((object.flags & 0x0080) == 0 || object.index < 0 ||
            object.index >= static_cast<std::int32_t>(
                navigation_encountered_nodes_.size()
            ) ||
            navigation_encountered_nodes_[
                static_cast<std::size_t>(object.index)
            ]) {
            return;
        }
        // The original navigation timer waits 1000 ms at a settled 0x0080
        // object before entering generic media mode 3.
        pending_navigation_encounter_node_ = object.index;
        navigation_encounter_due_ = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(
                metaverse::kNavigationEncounterDelayMs
            );
    }

    bool TickNavigationIdleAnimation(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (navigation_popup_idle_latch_.SuppressesIdleAnimation()) {
            // At 0x0040b253-0x0040b267 the timer stores timeGetTime() on every
            // tick while +0x628 remains set, so the ten-second threshold can
            // never expire after a popup was dismissed without a command.
            navigation_idle_due_ = now + std::chrono::milliseconds(
                metaverse::kNavigationIdleAnimationDelayMs
            );
            return false;
        }
        if (!metaverse::NavigationTimerThresholdPassed(
                now, navigation_idle_due_
            ) || !navigation_hub_active_ ||
            !navigator_ || !scene_data_ || pending_navigation_resolution_ ||
            pending_navigation_fade_target_ || slot_spin_active_ ||
            host_interstitial_active_ || motion_player_.Active()) {
            return false;
        }
        const metaverse::SceneObject* current = navigator_->CurrentObject();
        if (current == nullptr || (current->flags & 0x0020) == 0) {
            return false;
        }
        const std::size_t initial = random_.Scale(
            metaverse::kNavigationIdleAnimationObjects.size()
        );
        const std::size_t selected = metaverse::ConsumeNavigationIdleAnimation(
            navigation_idle_animation_used_, initial
        );
        const std::size_t object_index =
            metaverse::kNavigationIdleAnimationObjects[selected];
        if (object_index >= scene_data_->objects.size()) {
            status_ = L"Navigation idle animation object is missing.";
            UpdateTitle(window);
            return false;
        }

        const metaverse::SceneObject& animation =
            scene_data_->objects[object_index];
        metaverse::SceneTransition transition;
        transition.from_object = current->index;
        transition.to_object = current->index;
        transition.first_frame = animation.ranges[1][0];
        transition.last_frame = animation.ranges[1][1];

        // WaveMixActivate(FALSE) precedes the delayed range in the original.
        PlaySoundW(nullptr, nullptr, 0);
        navigation_effect_player_.Stop();
        pending_navigation_resolution_ = true;
        navigation_idle_animation_active_ = true;
        PlayTransition(transition);
        if (video_paused_ && !playback_range_) {
            CompleteNavigationPlayback(window);
        }
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }

    void StartNavigationFade(std::size_t target, HWND window) {
        // The 0x1000 branch sent message 0x40c, whose outer handler brightened
        // the current 8-bit palette to white before constructing the next DAT
        // scene.  The original handler also called the two-CD swap routine for
        // entries 21-24.  Bundled assets make that routine intentionally
        // unnecessary: the requested target is preserved verbatim.
        pending_navigation_resolution_ = false;
        playback_range_.reset();
        video_paused_ = true;
        // 0x0040cc42 calls the shared MCI stop/seek helper before touching the
        // palette. Stop the decoded range and its embedded PCM at the same
        // boundary; ordinary DAT WAV channels retain their separate lifetime.
        audio_player_.Stop();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;
        pending_navigation_fade_target_ = target;
        navigation_fade_started_ = std::chrono::steady_clock::now();
        InvalidateRect(window, nullptr, FALSE);
    }

    void TickNavigationFade(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (!pending_navigation_fade_target_) {
            return;
        }
        if (now - navigation_fade_started_ < kNavigationFadeDuration) {
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        const std::size_t target = *pending_navigation_fade_target_;
        pending_navigation_fade_target_.reset();
        std::string error;
        if (!LoadNavigationEntry(target, error)) {
            status_ = L"Navigation scene switch failed: " +
                      std::wstring(error.begin(), error.end());
        } else {
            ResolveNavigationNode(window);
        }
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void ResetNavigationOverlay() {
        ReleaseBitmap(navigation_overlay_bitmap_);
        navigation_overlay_width_ = 0;
        navigation_overlay_height_ = 0;
        navigation_overlay_x_ = 0;
        navigation_overlay_y_ = 0;
        navigation_overlay_object_ = -1;
        navigation_overlay_visible_ = false;
        navigation_overlay_next_visible_ = false;
    }

    void RepaintNavigationOverlayRect(HWND window) const {
        RECT client = {};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        if (client_width <= 0 || client_height <= 0 ||
            navigation_overlay_width_ <= 0 ||
            navigation_overlay_height_ <= 0) {
            return;
        }
        const double scale = std::min(
            static_cast<double>(client_width) / 640.0,
            static_cast<double>(client_height) / 480.0
        );
        const int drawn_width = static_cast<int>(std::lround(640.0 * scale));
        const int drawn_height = static_cast<int>(std::lround(480.0 * scale));
        const int content_x = (client_width - drawn_width) / 2;
        const int content_y = (client_height - drawn_height) / 2;
        RECT dirty{
            content_x + static_cast<LONG>(std::lround(
                navigation_overlay_x_ * scale
            )),
            content_y + static_cast<LONG>(std::lround(
                navigation_overlay_y_ * scale
            )),
            content_x + static_cast<LONG>(std::lround(
                (navigation_overlay_x_ + navigation_overlay_width_) * scale
            )),
            content_y + static_cast<LONG>(std::lround(
                (navigation_overlay_y_ + navigation_overlay_height_) * scale
            )),
        };
        InvalidateRect(window, &dirty, FALSE);
        UpdateWindow(window);
    }

    void UpdateNavigationOverlay(const metaverse::SceneObject& object) {
        if ((object.flags & 0x0040) == 0 || !object.point) {
            ResetNavigationOverlay();
            return;
        }
        if (navigation_overlay_object_ == object.index &&
            navigation_overlay_bitmap_ != nullptr) {
            return;
        }

        ResetNavigationOverlay();
        const auto path = SceneAssetsRoot() / L"BMP" / L"NAV" /
            (L"N" + std::to_wstring(object.index) + L".BMP");
        navigation_overlay_bitmap_ = static_cast<HBITMAP>(LoadImageW(
            nullptr, path.c_str(), IMAGE_BITMAP, 0, 0,
            LR_LOADFROMFILE | LR_CREATEDIBSECTION
        ));
        if (navigation_overlay_bitmap_ == nullptr) {
            status_ = L"Navigation overlay is missing: " + path.wstring();
            return;
        }
        BITMAP details = {};
        if (!GetObjectW(
                navigation_overlay_bitmap_, sizeof(details), &details
            )) {
            ResetNavigationOverlay();
            status_ = L"Navigation overlay dimensions could not be read.";
            return;
        }
        navigation_overlay_width_ = details.bmWidth;
        navigation_overlay_height_ = details.bmHeight;
        navigation_overlay_x_ = (*object.point)[0];
        navigation_overlay_y_ = (*object.point)[1];
        navigation_overlay_object_ = object.index;
        // The zeroed +0x638 selector chooses the captured background on the
        // first timer pass; Nn.BMP appears on the second pass.
        navigation_overlay_visible_ = false;
        navigation_overlay_next_visible_ = false;
    }

    [[nodiscard]] bool AtSlotMachineNode() const {
        if (!navigation_hub_active_ || !navigator_) {
            return false;
        }
        const metaverse::SceneObject* object = navigator_->CurrentObject();
        return object != nullptr && (object->flags & 0x4000) != 0;
    }

    void RevealSlotMachineResult(HWND window) {
        slot_outcome_ = metaverse::SpinSlots(round_.credits, random_);
        if (!slot_outcome_->spun) {
            slot_result_visible_ = false;
            slot_no_credit_visible_ = true;
            status_ = L"NO CREDIT";
            PlaySoundW(
                (SceneAssetsRoot() / L"WAV\\NAV\\SLOTLOSE.WAV").c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_NODEFAULT
            );
        } else {
            slot_no_credit_visible_ = false;
            slot_result_visible_ = true;
            LoadSlotBitmaps();
            const wchar_t* sound = slot_outcome_->jackpot
                ? L"WAV\\NAV\\WINBIG.WAV"
                : slot_outcome_->won
                ? L"WAV\\NAV\\WINDING.WAV"
                : L"WAV\\NAV\\SLOTLOSE.WAV";
            PlaySoundW(
                (SceneAssetsRoot() / sound).c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_NODEFAULT
            );
            status_ = slot_outcome_->jackpot
                ? L"Jackpot! "
                : slot_outcome_->won
                ? L"Match! "
                : L"No match. ";
            status_ += slot_outcome_->credit_change >= 0.0f ? L"+$" : L"-$";
            const float magnitude = slot_outcome_->credit_change >= 0.0f
                ? slot_outcome_->credit_change
                : -slot_outcome_->credit_change;
            wchar_t amount[32] = {};
            swprintf_s(amount, L"%.2f", static_cast<double>(magnitude));
            status_ += amount;
        }
        UpdateTitle(window);
        // An interrupted reel clears its active timer before this callback,
        // so no later video tick is guaranteed to repaint the decoded result
        // symbols. Publish the completed node immediately just like the shared
        // navigation arrival notification does.
        if (IsWindow(window)) {
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
        }
    }

    void SpinSlotMachine(HWND window, bool play_spin_sound) {
        slot_spin_active_ = false;
        slot_result_visible_ = false;
        slot_no_credit_visible_ = false;
        if (play_spin_sound && round_.credits != 0.0f) {
            PlaySoundW(
                (SceneAssetsRoot() / L"WAV\\NAV\\SLOTSPIN.WAV").c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_NODEFAULT
            );
        }
        RevealSlotMachineResult(window);
    }

    void StartSlotMachineSpin(HWND window) {
        if (!AtSlotMachineNode() || slot_spin_active_) {
            return;
        }
        if (round_.credits == 0.0f) {
            slot_outcome_.reset();
            slot_result_visible_ = false;
            slot_no_credit_visible_ = true;
            status_ = L"NO CREDIT";
            PlaySoundW(
                (SceneAssetsRoot() / L"WAV\\NAV\\SLOTLOSE.WAV").c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_NODEFAULT
            );
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        metaverse::SceneTransition transition;
        std::string error;
        if (!navigator_->Move(
                metaverse::SceneDirection::up, transition, error
            )) {
            status_ = error.empty()
                ? L"The Slots animation is unavailable at this node."
                : std::wstring(error.begin(), error.end());
            UpdateTitle(window);
            return;
        }

        // The slot node's Up link points back to itself. MM.EXE starts its
        // middle reel range, pumps normal messages for exactly 0x1194 ms, and
        // stops MCI playback without running destination-arrival handling.
        slot_outcome_.reset();
        slot_result_visible_ = false;
        slot_no_credit_visible_ = false;
        slot_spin_active_ = true;
        pending_navigation_resolution_ = false;
        navigation_arrival_pending_ = false;
        pending_navigation_encounter_node_.reset();
        ResetMotionSprite();
        PlaySoundW(
            (SceneAssetsRoot() / L"WAV\\NAV\\SLOTSPIN.WAV").c_str(), nullptr,
            SND_FILENAME | SND_ASYNC | SND_NODEFAULT
        );
        PlayTransition(transition);
        // fcn.0040aa61 takes its timeGetTime() baseline only after the MCI
        // range-play call returns.  Starting this deadline before the native
        // seek/audio setup would shorten the authored 4.5-second reel dwell
        // by however long that setup took on the local machine.
        slot_spin_due_ = std::chrono::steady_clock::now() +
            metaverse::kSlotSpinDuration;
        status_.clear();
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    bool TickSlotMachine(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (!slot_spin_active_ || now < slot_spin_due_) {
            return false;
        }
        slot_spin_active_ = false;
        playback_range_.reset();
        video_paused_ = true;
        // fcn.0040aa61 issues the MCI stop at timeGetTime()+0x1194.  That
        // stops both the crossroads frames and their embedded PCM before the
        // slot painter reveals the result.
        audio_player_.Stop();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;
        RevealSlotMachineResult(window);
        return true;
    }

    void StartTallySequence(HWND window) {
        if (round_tallied_) {
            return;
        }
        tally_result_ = metaverse::TallyRound(round_);
        round_tallied_ = true;

        const std::size_t winner_slot = tally_result_->winner_index;
        const std::int32_t winner_id =
            round_.contestants[winner_slot].contestant_id;
        const std::wstring_view winner = winner_id >= 1 && winner_id <= 10
            ? metaverse::kContestantNames[winner_id - 1]
            : L"Unknown";
        status_ = L"Winner: " + std::wstring(winner) + L" (score " +
                  std::to_wstring(tally_result_->winning_score) + L"). Award: $" +
                  std::to_wstring(
                      static_cast<std::int32_t>(tally_result_->credit_award)
                  ) + L".";

        // The outer round orchestrator, immediately after fcn.004031d0,
        // treats a judge-cadet victory as invalid: WRONG4 plays, the original
        // sponsorship stake is removed, and no W#.AVI is shown.
        if (winner_slot == round_.judge_cadet_slot) {
            const auto wrong = SceneAssetsRoot() /
                L"WAV" / L"ANNOUNCE" / L"WRONG4.WAV";
            PlaySoundW(
                wrong.c_str(), nullptr, SND_FILENAME | SND_SYNC | SND_NODEFAULT
            );
            round_.credits = metaverse::ClampJudgeCadetBalance(
                round_.credits - metaverse::ContestantSponsorCost(winner_id)
            );
            status_ += L" The winner was the judge cadet; the stake was removed.";
            FinishTallySequence(window);
            return;
        }

        if (!HasSecondaryAssets()) {
            status_ += L" Packaged winner and finale movies are unavailable.";
            FinishTallySequence(window);
            return;
        }

        const auto movie = std::filesystem::path(L"MOV\\TALLY") /
            (L"W" + std::to_wstring(winner_id) + L".AVI");
        pending_tally_finale_ = true;
        ScheduleCenteredOneShotStart(
            CenteredOneShotKind::winner, *assets2_, movie
        );
        status_ += L" Playing the original winner movie.";
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void FinishTallySequence(HWND window) {
        if (metaverse::ReplayCreditThresholdReached(round_.credits)) {
            wchar_t prompt[160] = {};
            swprintf_s(
                prompt,
                L"You now have $%.2f. \nDo you want to play Ms. Metaverse again?",
                static_cast<double>(round_.credits)
            );
            if (MessageBoxW(
                    window,
                    prompt,
                    metaverse::kLegacyDialogCaption,
                    static_cast<UINT>(
                        metaverse::kLegacyConfirmMessageBoxStyle
                    )
                ) == IDYES) {
                ResetRoundForReplay(window);
                return;
            }
        } else {
            // The original is already running from Disk II here. END2.WAV is
            // present only in that asset tree; resolving it from Disk I made
            // the low-credit closing silently skip its required announcement.
            if (assets2_) {
                const auto end_sound = *assets2_ / L"WAV" / L"END2.WAV";
                PlaySoundW(
                    end_sound.c_str(), nullptr,
                    SND_FILENAME | SND_SYNC | SND_NODEFAULT
                );
            }
        }

        if (!HasSecondaryAssets()) {
            const std::wstring message = status_ +
                L" The packaged original credit movie is unavailable.";
            MessageBoxW(
                window,
                message.c_str(),
                L"Ms. Metaverse — Credit Movie Error",
                MB_OK | MB_ICONERROR
            );
            DestroyWindow(window);
            return;
        }
        pending_exit_after_credit_ = true;
        ScheduleCenteredOneShotStart(
            CenteredOneShotKind::credit,
            *assets2_,
            L"MOV\\CREDIT\\CREDIT.AVI"
        );
        status_ = L"Playing the original closing credit movie.";
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void EnterInitialRoundFromHall(HWND window) {
        navigation_hub_active_ = false;
        pending_navigation_resolution_ = false;
        pending_navigation_fade_target_.reset();
        scene_ = kCryoScene;
        LoadScene();
        status_ = L"Sponsor five contestants.";
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void ResetRoundForReplay(HWND window) {
        const float credits = round_.credits;
        const float accumulated_reward = round_.accumulated_reward;
        const auto weights = round_.weights;
        const auto comment_for_rating = round_.comment_for_rating;
        round_ = {};
        round_.credits = credits;
        round_.accumulated_reward = accumulated_reward;
        round_.weights = weights;
        round_.comment_for_rating = comment_for_rating;
        // The executable performs both cadet draws inside its five-entry
        // contestant reset loop. Only the fifth pair survives, but all ten
        // rand calls affect the later navigation, pavilion, and slot draws.
        for (std::size_t index = 0;
             index < metaverse::kContestantsPerRound; ++index) {
            round_.judge_cadet_slot = random_.Modulo(
                metaverse::kContestantsPerRound
            );
            round_.judge_cadet_category =
                static_cast<metaverse::JudgingCategory>(
                    random_.Modulo(metaverse::kJudgingCategoryCount)
                );
        }
        weight_controls_ = {5, 5, 5};
        comment_order_ = metaverse::CommentOrderState{};
        selected_weight_ = 0;
        browsed_contestant_ = 0;
        selected_contestants_.clear();
        brain_variants_.fill(0);
        talent_variants_.fill(0);
        pavilion_variants_ready_ = false;
        judging_contestant_ = 0;
        judging_contestant_active_ = false;
        current_rating_ = 0;
        for (auto& category : gonged_) {
            category.fill(false);
        }
        round_tallied_ = false;
        tally_result_.reset();
        slot_outcome_.reset();
        navigation_hub_active_ = false;
        pending_navigation_resolution_ = false;
        pending_tally_finale_ = false;
        pending_exit_after_credit_ = false;
        // Replay reuses the document object. The reset loop at 0x0040254c
        // clears contestant/rating state and rerolls the judge cadet, but it
        // never restores the document's +0x234/+0x230/+0x238 entry flags.
        // Consequently C21/C31/C41 remain consumed even across a replayed
        // round. C11/C51/C91 belong to newly constructed round dialogs and do
        // play again.
        const bool talent_entry_played = host_entry_played_[2];
        const bool brains_entry_played = host_entry_played_[3];
        const bool looks_entry_played = host_entry_played_[4];
        host_entry_played_.fill(false);
        host_entry_played_[2] = talent_entry_played;
        host_entry_played_[3] = brains_entry_played;
        host_entry_played_[4] = looks_entry_played;
        first_gong_host_played_.fill(false);
        first_penalty_host_played_.fill(false);
        navigation_encountered_nodes_.fill(false);
        navigation_popup_one_shot_state_.ResetForConstruction();
        cryo_first_sponsor_host_played_ = false;
        scene_ = kCryoScene;
        LoadScene();
        status_ = L"New round started; sponsor five contestants.";
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void ShowLegacyBitmapReadFailure(
        std::wstring_view filename = {}
    ) const {
        const std::wstring message =
            metaverse::LegacyBitmapReadFailureMessage(filename);
        MessageBoxW(
            owner_window_,
            message.c_str(),
            metaverse::kLegacyDialogCaption,
            static_cast<UINT>(metaverse::kLegacyWarningMessageBoxStyle)
        );
    }

    HBITMAP LoadBitmapWithLegacyWarning(
        const std::filesystem::path& path,
        std::wstring_view warning_filename = {}
    ) const {
        HBITMAP bitmap = static_cast<HBITMAP>(LoadImageW(
            nullptr, path.c_str(), IMAGE_BITMAP, 0, 0,
            LR_LOADFROMFILE | LR_CREATEDIBSECTION
        ));
        if (bitmap == nullptr) {
            ShowLegacyBitmapReadFailure(warning_filename);
        }
        return bitmap;
    }

    HBITMAP LoadAssetBitmap(const std::filesystem::path& relative) const {
        return LoadBitmapWithLegacyWarning(assets_ / relative);
    }

    void ResetCommentBitmaps() {
        // 0x0040fec4 releases ACCEPT, HAND, TH, and the final shared COMMENT
        // wrapper in that order after its generic/base objects. Native keeps
        // all ten COMMENT DIBs so it can reconstruct an exposed window, but
        // releases that retained set only after the three static wrappers.
        for (HBITMAP* bitmap : {
                 &comment_accept_bitmap_, &comment_hand_bitmap_,
                 &comment_thermometer_bitmap_
             }) {
            if (*bitmap != nullptr) {
                DeleteObject(*bitmap);
                *bitmap = nullptr;
            }
        }
        for (HBITMAP& bitmap : comment_phrase_bitmaps_) {
            ReleaseBitmap(bitmap);
        }
        comment_accept_pressed_ = false;
        comment_hand_visible_ = false;
        comment_hand_row_ = 0;
        comment_painter_ = {};
    }

    void RepaintCommentRect(
        HWND window,
        const metaverse::CommentRect& logical
    ) const {
        RECT client = {};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        if (client_width <= 0 || client_height <= 0) {
            return;
        }
        const double scale = std::min(
            static_cast<double>(client_width) / 640.0,
            static_cast<double>(client_height) / 480.0
        );
        const int drawn_width = static_cast<int>(std::lround(640.0 * scale));
        const int drawn_height = static_cast<int>(std::lround(480.0 * scale));
        const int content_x = (client_width - drawn_width) / 2;
        const int content_y = (client_height - drawn_height) / 2;
        RECT dirty{
            content_x + static_cast<LONG>(std::lround(logical.left * scale)),
            content_y + static_cast<LONG>(std::lround(logical.top * scale)),
            content_x + static_cast<LONG>(std::lround(logical.right * scale)),
            content_y + static_cast<LONG>(std::lround(logical.bottom * scale)),
        };
        InvalidateRect(window, &dirty, FALSE);
        UpdateWindow(window);
    }

    void LoadCommentThermometer(HWND window = nullptr) {
        ReleaseBitmap(comment_thermometer_bitmap_);
        comment_thermometer_bitmap_ = LoadAssetBitmap(
            std::filesystem::path(L"BMP/ORDER") /
            (L"TH" + std::to_wstring(comment_order_.next_rank) + L".BMP")
        );
        if (window != nullptr) {
            comment_painter_.thermometer_dirty = true;
            RepaintCommentRect(window, metaverse::CommentThermometerRect());
        }
    }

    void LoadCommentPhrase(std::size_t comment, HWND window = nullptr) {
        if (comment >= comment_phrase_bitmaps_.size()) {
            return;
        }
        ReleaseBitmap(comment_phrase_bitmaps_[comment]);
        comment_phrase_bitmaps_[comment] = LoadAssetBitmap(
            std::filesystem::path(L"BMP/ORDER/COMMENT") /
            (std::to_wstring(comment) + L".BMP")
        );
        if (window != nullptr) {
            comment_painter_.current_comment = comment;
            comment_painter_.comment_dirty = true;
            RepaintCommentRect(
                window, metaverse::CommentPhraseBitmapRect(comment)
            );
        }
    }

    void MoveCommentHand(std::int32_t comment, HWND window) {
        const auto old_rect = metaverse::CommentHandRect(comment_hand_row_);
        const auto new_rect = metaverse::CommentHandRect(comment);
        comment_hand_row_ = comment;
        comment_hand_visible_ = true;
        comment_painter_.base_dirty = true;
        comment_painter_.hand_dirty = true;
        RepaintCommentRect(
            window, metaverse::UnionCommentRects(old_rect, new_rect)
        );
    }

    void FinishOrderHostEntry(HWND window) {
        // 0x0040f4d3-0x0040f50c stays inside the existing dialog: COMMENT/0
        // through COMMENT/9 are loaded and synchronously painted one at a
        // time, then 0x0040fd51 restores/moves HAND through the union of its
        // old and current rectangles.
        comment_painter_.base_dirty = true;
        for (std::size_t comment = 0;
             comment < comment_phrase_bitmaps_.size(); ++comment) {
            LoadCommentPhrase(comment, window);
        }
        MoveCommentHand(comment_order_.selected_comment, window);
    }

    void LoadCommentBitmaps(bool board_ready) {
        ResetCommentBitmaps();
        // Constructor 0x0040fa08 owns this exact order. ORDER.BMP follows in
        // the first base-paint branch after OnInit returns.
        comment_accept_bitmap_ = LoadAssetBitmap(L"BMP/ORDER/ACCEPT.BMP");
        LoadCommentThermometer();
        comment_hand_bitmap_ = LoadAssetBitmap(L"BMP/ORDER/HAND.BMP");
        if (board_ready) {
            for (std::size_t comment = 0;
                 comment < comment_phrase_bitmaps_.size(); ++comment) {
                LoadCommentPhrase(comment);
            }
        }
        comment_hand_visible_ = board_ready;
        comment_painter_.base_dirty = true;
        comment_painter_.thermometer_dirty = true;
    }

    void TearDownOrderDialogResources() {
        ResetHostInterstitial();
        ResetBitmap();
        ResetCommentBitmaps();
    }

    void ResetWeightBitmaps() {
        // 0x00410f5b owns these in OK, six-arrow, then three-gauge order.
        // The latter includes any old gauge retained by the value-zero branch.
        ReleaseBitmap(weight_ok_bitmap_);
        for (HBITMAP& bitmap : weight_control_bitmaps_) {
            ReleaseBitmap(bitmap);
        }
        for (HBITMAP& bitmap : weight_gauge_bitmaps_) {
            ReleaseBitmap(bitmap);
        }
        weight_gauge_visible_.fill(false);
        weight_painter_ = {};
        weight_pressed_control_ = -1;
        weight_ok_pressed_ = false;
    }

    void TearDownWeightDialogResources() {
        // The original helper releases its optional generic-media wrapper and
        // base DIB before the Weight-specific OK/control/gauge objects.
        ResetHostInterstitial();
        ResetBitmap();
        ResetWeightBitmaps();
    }

    void RepaintWeightRect(
        HWND window,
        const metaverse::CategoryWeightRect& logical
    ) const {
        RECT client = {};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        if (client_width <= 0 || client_height <= 0) {
            return;
        }

        // WEIGHT.BMP is the original 640x480 dialog canvas. Convert the
        // recovered CRect through the same centered scaling used by Paint().
        const double scale = std::min(
            static_cast<double>(client_width) / 640.0,
            static_cast<double>(client_height) / 480.0
        );
        const int drawn_width = static_cast<int>(std::lround(640.0 * scale));
        const int drawn_height = static_cast<int>(std::lround(480.0 * scale));
        const int content_x = (client_width - drawn_width) / 2;
        const int content_y = (client_height - drawn_height) / 2;
        const LONG left = content_x +
            static_cast<LONG>(std::lround(logical.left * scale));
        const LONG top = content_y +
            static_cast<LONG>(std::lround(logical.top * scale));
        RECT dirty{
            left,
            top,
            left + static_cast<LONG>(std::lround(logical.Width() * scale)),
            top + static_cast<LONG>(std::lround(logical.Height() * scale)),
        };
        InvalidateRect(window, &dirty, FALSE);
        UpdateWindow(window);
    }

    void UpdateWeightGaugeBitmap(
        std::size_t category,
        HWND window = nullptr,
        std::optional<std::int32_t> requested_value = std::nullopt
    ) {
        if (category >= weight_controls_.size()) {
            return;
        }
        const std::int32_t value = requested_value.value_or(
            weight_controls_[category]
        );
        const auto plan = metaverse::PlanCategoryWeightGauge(category, value);
        if (!plan) {
            return;
        }
        weight_painter_.current_gauge = category;
        if (plan->replace_existing_bitmap) {
            ReleaseBitmap(weight_gauge_bitmaps_[category]);
            weight_gauge_bitmaps_[category] = LoadAssetBitmap(
                std::filesystem::path(L"BMP/WEIGHT") /
                plan->bitmap_filename
            );
            weight_gauge_visible_[category] = true;
            weight_painter_.gauge_dirty[category] = true;
        } else {
            weight_gauge_visible_[category] = false;
            weight_painter_.base_dirty = true;
        }
        if (window != nullptr) {
            RepaintWeightRect(window, plan->dirty_rect);
        }
    }

    void LoadWeightBitmaps(bool gauges_ready) {
        ResetWeightBitmaps();
        // 0x004106e6 loads these in this exact ownership order. TH*_1 are
        // hidden backing wrappers during the 500 ms delay and C91; the timer
        // later replaces them through 0x00410e3e rather than constructing an
        // initially empty gauge array.
        weight_ok_bitmap_ = LoadAssetBitmap(
            std::filesystem::path(L"BMP/WEIGHT") /
            metaverse::kCategoryWeightOkBitmap
        );
        for (std::size_t category = 0;
             category < weight_gauge_bitmaps_.size(); ++category) {
            weight_gauge_bitmaps_[category] = LoadAssetBitmap(
                std::filesystem::path(L"BMP/WEIGHT") /
                metaverse::kCategoryWeightInitialGaugeBitmaps[category]
            );
        }
        for (std::size_t control = 0;
             control < weight_control_bitmaps_.size(); ++control) {
            weight_control_bitmaps_[control] = LoadAssetBitmap(
                std::filesystem::path(L"BMP/WEIGHT") /
                metaverse::kCategoryWeightControlBitmaps[control]
            );
        }
        if (gauges_ready) {
            for (std::size_t category = 0;
                 category < weight_gauge_bitmaps_.size(); ++category) {
                UpdateWeightGaugeBitmap(category);
            }
        }
    }

    void FinishWeightHostEntry(HWND window) {
        // Timer body 0x00410152 calls the three value-5 replacement helpers
        // while the stored controls are still zero, then commits 5/5/5. The
        // independent visibility flags preserve those synchronous repaints.
        for (std::size_t category = 0;
             category < weight_gauge_bitmaps_.size(); ++category) {
            UpdateWeightGaugeBitmap(category, window, 5);
        }
        weight_controls_ = {5, 5, 5};
    }

    void AssignCurrentComment(HWND window) {
        std::string error;
        const auto assignment = comment_order_.BeginSelectedAssignment(error);
        if (!assignment) {
            if (error == "this comment has already been selected; choose another") {
                MessageBoxW(
                    window,
                    L"This command had been selected,\nplease choose another.",
                    metaverse::kLegacyDialogCaption,
                    static_cast<UINT>(
                        metaverse::kLegacyPlainMessageBoxStyle
                    )
                );
            } else {
                status_ = std::wstring(error.begin(), error.end());
            }
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        // 0x0040f687-0x0040f6ea paints the red rank before starting TTn.
        comment_painter_.base_dirty = true;
        comment_painter_.current_rank_comment = assignment->comment;
        comment_painter_.rank_dirty = true;
        RepaintCommentRect(
            window, metaverse::CommentRankRect(assignment->comment)
        );

        const auto sound = assets_ / L"WAV" /
            (L"TT" + std::to_wstring(assignment->rank) + L".WAV");
        PlaySoundW(
            sound.c_str(), nullptr,
            SND_FILENAME | SND_ASYNC
        );
        LoadCommentThermometer(window);
        // The recovered document table write follows the thermometer helper,
        // rather than being bundled into the initial selection mutation.
        metaverse::CommentOrderState::CommitAssignment(
            *assignment, round_.comment_for_rating
        );
        if (comment_order_.Complete()) {
            // fcn.0040f653 plays the final TT1 response with flag 1, then
            // immediately tears down ORDER.  Its caller enters navigation
            // without sndPlaySound(NULL), so the final phrase continues over
            // the first hub frame rather than being clipped at dialog close.
            TearDownOrderDialogResources();
            EnterNavigationHub(1, window, false);
            return;
        }
        status_ = L"Rank " + std::to_wstring(assignment->rank) +
                  L" assigned. Choose a phrase for rank " +
                  std::to_wstring(comment_order_.next_rank) + L".";
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void LoadSlotBitmaps() {
        const std::array<std::int32_t, 3> symbols = slot_outcome_
            ? slot_outcome_->symbols
            : std::array<std::int32_t, 3>{0, 0, 0};
        for (std::size_t reel = 0; reel < slot_bitmaps_.size(); ++reel) {
            if (slot_bitmaps_[reel] != nullptr) {
                DeleteObject(slot_bitmaps_[reel]);
                slot_bitmaps_[reel] = nullptr;
            }
            const auto path = SceneAssetsRoot() / L"BMP" / L"SLOT" /
                (L"S" + std::to_wstring(symbols[reel]) +
                 std::to_wstring(reel) + L".BMP");
            slot_bitmaps_[reel] = static_cast<HBITMAP>(LoadImageW(
                nullptr, path.c_str(), IMAGE_BITMAP, 0, 0,
                LR_LOADFROMFILE | LR_CREATEDIBSECTION
            ));
        }
    }

    static std::optional<metaverse::JudgingCategory> JudgingCategoryForScene(
        int scene
    ) {
        if (scene == kBrainsJudgingScene) {
            return metaverse::JudgingCategory::brains;
        }
        if (scene == kLooksJudgingScene) {
            return metaverse::JudgingCategory::looks;
        }
        if (scene == kTalentJudgingScene) {
            return metaverse::JudgingCategory::talent;
        }
        return std::nullopt;
    }

    std::filesystem::path PavilionDataPath() const {
        return profile_path_.parent_path() /
               metaverse::kLegacyPavilionRotationFileName;
    }

    std::filesystem::path HostReactionDataPath() const {
        return profile_path_.parent_path() / L"hnrd.dat";
    }

    bool PreparePavilionVariants(std::string& error) {
        error.clear();
        // fcn.0040304e constructs five Brains and then five Talent non-repeat
        // objects before any object opens NRFPAV.DAT. Every fcn.00416999
        // constructor calls srand(time(0)); preserve all ten calls and their
        // position before file I/O, since a slow read crossing a one-second
        // boundary must not change the variant stream.
        for (std::size_t object = 0; object < 10; ++object) {
            random_.Seed(static_cast<std::uint32_t>(std::time(nullptr)));
        }

        // Each object opens only its own selected line. Keep these reads inside
        // the contestant loop so another legacy/native process's immediately
        // preceding line update is visible just as it is to MM.EXE.
        pavilion_rotation_ = {};
        for (std::size_t index = 0; index < round_.contestants.size(); ++index) {
            const std::int32_t contestant_id =
                round_.contestants[index].contestant_id;
            metaverse::LoadLegacyPavilionRotationLineForSelection(
                PavilionDataPath(), pavilion_rotation_,
                metaverse::PavilionRotationCategory::brains, contestant_id
            );
            metaverse::LoadLegacyPavilionRotationLineForSelection(
                PavilionDataPath(), pavilion_rotation_,
                metaverse::PavilionRotationCategory::talent, contestant_id
            );
            brain_variants_[index] = metaverse::DrawBrainVariant(
                pavilion_rotation_, contestant_id, random_, error
            );
            talent_variants_[index] = metaverse::DrawTalentVariant(
                pavilion_rotation_, contestant_id, random_, error
            );
            // fcn.0040304e draws both choices, then fcn.00416ac4 performs two
            // r+ line updates before advancing. Both helpers are void and
            // their fopen/fseek/write failures are ignored by the caller.
            std::string ignored_write_error;
            metaverse::SaveLegacyPavilionRotationLine(
                PavilionDataPath(), pavilion_rotation_,
                metaverse::PavilionRotationCategory::brains,
                contestant_id, ignored_write_error
            );
            metaverse::SaveLegacyPavilionRotationLine(
                PavilionDataPath(), pavilion_rotation_,
                metaverse::PavilionRotationCategory::talent,
                contestant_id, ignored_write_error
            );
        }
        pavilion_variants_ready_ = true;
        return true;
    }

    std::filesystem::path JudgingVideoPath(
        metaverse::JudgingCategory category
    ) {
        const std::int32_t contestant_id =
            round_.contestants[judging_contestant_].contestant_id;
        if (metaverse::IsJudgeCadetPresentation(
                round_, judging_contestant_, category
            )) {
            if (category == metaverse::JudgingCategory::brains) {
                return std::filesystem::path(L"MOV\\BRAIN") /
                    (L"BI" + std::to_wstring(contestant_id) + L".AVI");
            }
            if (category == metaverse::JudgingCategory::talent) {
                return std::filesystem::path(L"MOV\\TALENT") /
                    (L"TI" + std::to_wstring(contestant_id) + L".AVI");
            }
        }
        if (!pavilion_variants_ready_) {
            std::string error;
            if (!PreparePavilionVariants(error)) {
                status_ = std::wstring(error.begin(), error.end());
                return {};
            }
        }
        if (category == metaverse::JudgingCategory::brains) {
            return std::filesystem::path(L"MOV\\BRAIN") /
                (L"B" + std::to_wstring(contestant_id) +
                 std::to_wstring(brain_variants_[judging_contestant_]) + L".AVI");
        }
        if (category == metaverse::JudgingCategory::looks) {
            // Looks is bitmap-driven. LP%d.AVI is exclusively the correct
            // Penalty Box response, not the contestant's normal presentation.
            return {};
        }
        return std::filesystem::path(L"MOV\\TALENT") /
            (L"T" + std::to_wstring(contestant_id) +
             std::to_wstring(talent_variants_[judging_contestant_]) + L".AVI");
    }

    static std::filesystem::path PenaltyResponseVideoPath(
        metaverse::JudgingCategory category,
        std::int32_t contestant_id
    ) {
        if (category == metaverse::JudgingCategory::brains) {
            return std::filesystem::path(L"MOV\\BRAIN") /
                (L"BP" + std::to_wstring(contestant_id) + L".AVI");
        }
        if (category == metaverse::JudgingCategory::looks) {
            return std::filesystem::path(L"MOV\\LOOK") /
                (L"LP" + std::to_wstring(contestant_id) + L".AVI");
        }
        return std::filesystem::path(L"MOV\\TALENT") /
            (L"TP" + std::to_wstring(contestant_id) + L".AVI");
    }

    std::filesystem::path PavilionAmbientSound(
        metaverse::JudgingCategory category
    ) const {
        const wchar_t* filename = category == metaverse::JudgingCategory::brains
            ? L"BRAINS.WAV"
            : category == metaverse::JudgingCategory::looks
                ? L"LOOKS.WAV"
                : L"TALENT.WAV";
        return JudgingAssetsRoot(category) / L"WAV" / filename;
    }

    void PlayJudgeComment(
        metaverse::JudgingCategory category,
        std::int32_t rating,
        std::int32_t contestant_slot,
        HWND window
    ) {
        const char prefix = category == metaverse::JudgingCategory::brains
            ? 'b'
            : category == metaverse::JudgingCategory::looks ? 'l' : 't';
        std::string relative;
        std::string error;
        if (!metaverse::BuildJudgeCommentPath(
                prefix, rating, contestant_slot, round_.comment_for_rating,
                relative, error
            )) {
            status_ += L" Host comment failed: " +
                       std::wstring(error.begin(), error.end()) + L".";
        } else {
            const std::filesystem::path sound =
                JudgingAssetsRoot(category) / relative;
            if (std::filesystem::exists(sound)) {
                PlaySoundW(
                    sound.c_str(), nullptr,
                    SND_FILENAME | SND_ASYNC
                );
            } else if (category == metaverse::JudgingCategory::talent &&
                       !HasSecondaryAssets()) {
                status_ += L" Talent host-comment audio is absent from the configured assets.";
            } else {
                status_ += L" Host-comment WAV is missing from the asset tree.";
            }
        }
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void PlayGenericRatingPreview(
        metaverse::JudgingCategory category,
        std::int32_t rating,
        HWND window
    ) {
        const auto sound = JudgingAssetsRoot(category) / L"WAV" /
            (L"TT" + std::to_wstring(rating) + L".WAV");
        if (std::filesystem::exists(sound)) {
            PlaySoundW(sound.c_str(), nullptr, SND_FILENAME | SND_ASYNC);
        } else {
            status_ += L" Generic Judge-O-Matic WAV is missing from the asset tree.";
        }
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    bool StartJudgingEventHost(
        const HostCue& cue,
        metaverse::JudgingCategory category,
        JudgingDisposition disposition,
        HWND window
    ) {
        ResetBitmap();
        audio_player_.Stop();
        active_performance_category_.reset();
        std::string error;
        if (!StartHostInterstitial(cue, error)) {
            status_ = L"Host event failed: " +
                      std::wstring(error.begin(), error.end()) + L".";
            // C61/C71 run through fcn.00407bc1 / fcn.00409dff. Even a
            // generic-media construction failure executes the dispatcher's
            // first ambient restart before the outer Gong/Penalty tail starts
            // that same loop a second time in AdvanceJudging.
            const auto ambient = PavilionAmbientSound(category);
            PlaySoundW(
                ambient.c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_LOOP
            );
            if (disposition == JudgingDisposition::disqualified) {
                // The C71 caller alone forces an UpdateWindow between the
                // dispatcher's restart and its own second ambient start
                // (0x00407a3f / 0x00409c79). C61 has no equivalent update.
                InvalidateRect(window, nullptr, FALSE);
                UpdateWindow(window);
            }
            return false;
        }
        pending_host_judging_advance_ = PendingHostJudgingAdvance{
            category, disposition
        };
        status_ = L"Playing original host event " +
                  metaverse::HostClipStem(cue.channel, cue.take) + L".";
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }

    void CompleteAcceptedContestant(
        metaverse::JudgingCategory category,
        HWND window
    ) {
        auto& contestant = round_.contestants[judging_contestant_];
        const std::size_t category_index = static_cast<std::size_t>(category);
        // All three ACCEPT handlers allocate/load DIMMED and present its
        // warning, if any, before either the document rating byte or the
        // dialog-local pending value changes (0x004076aa / 0x004098d8 /
        // 0x00413c3d).
        ReplaceJudgingPortraitBitmap(
            category,
            judging_contestant_,
            std::filesystem::path(L"BMP\\DIMMED") /
                (std::to_wstring(contestant.contestant_id) + L".BMP")
        );
        contestant.ratings[category_index] =
            static_cast<std::uint8_t>(current_rating_);
        metaverse::CompletePavilionPendingSlot(
            judging_visit_, judging_contestant_
        );
        // Each recovered result handler has replaced only its current
        // portrait wrapper. Reloading all five would produce spurious warnings
        // and resource churn that the modal dialog never performs.
        if (category == metaverse::JudgingCategory::talent ||
            category == metaverse::JudgingCategory::brains) {
            RepaintWidePavilionResult(window, judging_contestant_);
            wide_rating_visible_ = true;
        } else {
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
        }
    }

    static void RepaintWidePavilionResult(
        HWND window,
        std::size_t slot
    ) {
        const auto portrait_bounds = metaverse::PavilionPortraitBounds(slot);
        RECT portrait_rect{
            portrait_bounds[0], portrait_bounds[1],
            portrait_bounds[2], portrait_bounds[3],
        };
        RECT rating_rect{
            metaverse::kWidePavilionRatingGraphicBounds[0],
            metaverse::kWidePavilionRatingGraphicBounds[1],
            metaverse::kWidePavilionRatingGraphicBounds[2],
            metaverse::kWidePavilionRatingGraphicBounds[3],
        };
        InvalidateLogicalRect(window, portrait_rect);
        InvalidateLogicalRect(window, rating_rect);
        UpdateWindow(window);
    }

    void AdvanceJudging(
        metaverse::JudgingCategory category,
        HWND window,
        JudgingDisposition disposition
    ) {
        const std::int32_t completed_id =
            round_.contestants[judging_contestant_].contestant_id;
        const std::wstring_view completed_name =
            completed_id >= 1 && completed_id <= 10
                ? metaverse::kContestantNames[completed_id - 1]
                : L"Unknown";
        auto next = NextEligibleJudgingSlot(category, 0);
        if (category == metaverse::JudgingCategory::looks) {
            const auto activation =
                metaverse::PlanLooksAutomaticActivation(judging_visit_);
            current_rating_ = activation.numeric_rating_after;
            next = activation.next_slot;
            if (activation.replace_rating_bitmap) {
                LoadJudgingRatingBitmap(category);
            }
            // fcn.004140bf automatically selects the first remaining Looks
            // portrait after ACCEPT or a correct Penalty result. If there is
            // no next slot, the final presentation remains selected and the
            // player leaves through the direct left-edge exit.
            if (next) {
                judging_contestant_ = *next;
                judging_contestant_active_ = true;
                looks_selection_frame_dirty_ = true;
                // Seek the retained child in place after clearing the prior
                // contestant's meter surface.
                RECT portrait_strip{
                    metaverse::kPavilionPortraitX,
                    metaverse::kPavilionPortraitY,
                    640,
                    480,
                };
                InvalidateLogicalRect(window, portrait_strip);
                UpdateWindow(window);
                SeekLooksContestantFrame();
                const auto ambient = PavilionAmbientSound(category);
                PlaySoundW(
                    ambient.c_str(), nullptr,
                    SND_FILENAME | SND_ASYNC | SND_LOOP
                );
                status_ = disposition == JudgingDisposition::disqualified
                    ? std::wstring(completed_name) + L" was the judge cadet and has been disqualified. The next Looks contestant is selected."
                    : std::wstring(completed_name) + L" rated. The next Looks contestant is selected.";
            } else {
                judging_contestant_active_ = true;
                // Keep the exhausted chooser on the same clean zero surface.
                const auto ambient = PavilionAmbientSound(category);
                PlaySoundW(
                    ambient.c_str(), nullptr,
                    SND_FILENAME | SND_ASYNC | SND_LOOP
                );
                status_ = disposition == JudgingDisposition::disqualified
                    ? std::wstring(completed_name) + L" was the judge cadet and has been disqualified. Use the left-edge exit."
                    : std::wstring(completed_name) + L" rated. Use the left-edge exit.";
            }
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        const auto transition =
            metaverse::PlanWidePavilionRatingTransition(
                metaverse::WidePavilionRatingEvent::result_completed,
                current_rating_
            );
        current_rating_ = transition.numeric_rating_after;
        judging_contestant_active_ = false;
        performance_access_granted_ = false;
        if (!transition.preserve_current_performance) {
            active_performance_category_.reset();
        }
        // ACCEPT's X dispatcher and both Gong/Penalty tails all restore the
        // pavilion loop before returning. They retain the same dialog and its
        // current gauge/portrait wrappers; ACCEPT also leaves the underlying
        // 320x240 performance wrapper alive until its own notify arrives.
        if (transition.restart_ambient) {
            const auto ambient = PavilionAmbientSound(category);
            PlaySoundW(
                ambient.c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_LOOP
            );
        }
        if (next) {
            status_ = disposition == JudgingDisposition::gonged
                ? std::wstring(completed_name) + L" received a gong and a zero. Choose another contestant."
                : disposition == JudgingDisposition::disqualified
                ? std::wstring(completed_name) + L" was the judge cadet and has been disqualified. Choose another contestant."
                : std::wstring(completed_name) + L" rated. Choose another contestant.";
        } else {
            // The original leaves the now-complete pavilion open. Its input
            // handler performs the all-zero test on the next portrait-strip
            // click, not in ACCEPT/Gong/Penalty completion.
            status_ = disposition == JudgingDisposition::gonged
                ? std::wstring(completed_name) + L" received a gong and a zero. Click the portrait strip to leave the pavilion."
                : disposition == JudgingDisposition::disqualified
                ? std::wstring(completed_name) + L" was the judge cadet and has been disqualified. Click the portrait strip to leave the pavilion."
                : std::wstring(completed_name) + L" rated. Click the portrait strip to leave the pavilion.";
        }
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void TearDownLooksPavilionResources() {
        using Owner = metaverse::LooksBitmapOwner;
        for (const Owner owner : metaverse::kLooksBitmapTeardownOrder) {
            switch (owner) {
                case Owner::accept:
                    ReleaseBitmap(judging_action_bitmaps_[0]);
                    break;
                case Owner::penalty:
                    ReleaseBitmap(judging_action_bitmaps_[2]);
                    break;
                case Owner::rotate_control:
                    ReleaseBitmap(judging_action_bitmaps_[3]);
                    break;
                case Owner::magnifier:
                    ResetLooksBitmap();
                    break;
                case Owner::base:
                    ResetBackgroundBitmap();
                    break;
                case Owner::rating:
                    ReleaseBitmap(judging_rating_bitmap_);
                    judging_rating_width_ = 0;
                    judging_rating_height_ = 0;
                    looks_rating_visible_ = false;
                    break;
                case Owner::portrait_0:
                case Owner::portrait_1:
                case Owner::portrait_2:
                case Owner::portrait_3:
                case Owner::portrait_4: {
                    const std::size_t slot = static_cast<std::size_t>(
                        static_cast<int>(owner) -
                        static_cast<int>(Owner::portrait_0)
                    );
                    ReleaseBitmap(judging_portrait_bitmaps_[slot]);
                    break;
                }
            }
        }

        // 0x00414410 stops and destroys the retained ROTATE child only after
        // every bitmap wrapper above has been released.
        looks_rotate_playing_ = false;
        looks_rotate_decoder_.Close();
        ReleaseBitmap(looks_rotate_bitmap_);
        looks_rotate_width_ = 0;
        looks_rotate_height_ = 0;
        looks_rotate_last_frame_ = 0;
        looks_rotate_visible_ = false;
        looks_rotate_child_constructed_ = false;
        looks_magnifier_visible_ = false;
        looks_front_view_ = true;
        ReleaseBitmap(judging_action_bitmaps_[1]);
        judging_pressed_action_ = JudgingAction::none;
        judging_pressed_visual_ = JudgingAction::none;
        looks_selection_frame_dirty_ = false;
        judging_hovered_portrait_.reset();
        judging_comment_previewed_ = false;
    }

    void TearDownWidePavilionResources() {
        using Owner = metaverse::WidePavilionCloseOwner;
        for (const Owner owner : metaverse::kWidePavilionCloseOrder) {
            switch (owner) {
                case Owner::palette:
                    // The native 32-bit compositor has no retained palette.
                    break;
                case Owner::accept:
                    ReleaseBitmap(judging_action_bitmaps_[0]);
                    break;
                case Owner::gong:
                    ReleaseBitmap(judging_action_bitmaps_[1]);
                    break;
                case Owner::penalty:
                    ReleaseBitmap(judging_action_bitmaps_[2]);
                    break;
                case Owner::base:
                    ResetBackgroundBitmap();
                    break;
                case Owner::rating:
                    ReleaseBitmap(judging_rating_bitmap_);
                    judging_rating_width_ = 0;
                    judging_rating_height_ = 0;
                    break;
                case Owner::portrait_0:
                case Owner::portrait_1:
                case Owner::portrait_2:
                case Owner::portrait_3:
                case Owner::portrait_4: {
                    const std::size_t slot = static_cast<std::size_t>(
                        static_cast<int>(owner) -
                        static_cast<int>(Owner::portrait_0)
                    );
                    ReleaseBitmap(judging_portrait_bitmaps_[slot]);
                    break;
                }
            }
        }

        // The legacy +0x17c movie wrapper is destroyed only after every bitmap
        // owner.  Do not use TearDownCurrentPerformance here: pavilion close
        // ends the dialog and therefore performs no intermediate repaint.
        ResetBitmap();
        video_decoder_.Close();
        audio_player_.Stop();
        active_performance_category_.reset();
        video_paused_ = true;
        playback_range_.reset();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;
        judging_pressed_action_ = JudgingAction::none;
        judging_pressed_visual_ = JudgingAction::none;
        judging_hovered_portrait_.reset();
        judging_comment_previewed_ = false;
    }

    void CloseJudgingPavilion(
        metaverse::JudgingCategory category,
        HWND window
    ) {
        // Talent 0x00407cb7, Brains 0x00409ef5, and Looks 0x0041434a copy
        // the complete five-entry modal pending array back to the document
        // before stopping audio or releasing any dialog-owned resource.
        metaverse::CommitPavilionVisit(round_, category, judging_visit_);
        PlaySoundW(nullptr, nullptr, 0);
        if (category == metaverse::JudgingCategory::looks) {
            video_paused_ = true;
            audio_player_.Stop();
            TearDownLooksPavilionResources();
        } else {
            TearDownWidePavilionResources();
        }
        const std::size_t navigation_state =
            category == metaverse::JudgingCategory::talent ? 2
            : category == metaverse::JudgingCategory::brains ? 3
            : 4;
        EnterNavigationHub(navigation_state, window, false);
        status_ = L"Pavilion complete.";
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void RequestJudgingPavilionClose(
        metaverse::JudgingCategory category,
        HWND window
    ) {
        if (category == metaverse::JudgingCategory::brains) {
            std::string error;
            if (StartHostInterstitial(HostCue{8, 1}, error)) {
                pending_host_pavilion_close_ = category;
                status_ = L"Playing original Brains completion event C81.";
                UpdateTitle(window);
                InvalidateRect(window, nullptr, FALSE);
                return;
            }
            status_ = L"Brains completion event failed: " +
                      std::wstring(error.begin(), error.end()) + L".";
            // fcn.00409dff restores BRAINS.WAV on every C81 return, including
            // loader failure, before the caller enters 0x00409ef5 and stops
            // the ordinary sound channel as its first teardown operation.
            const auto ambient = PavilionAmbientSound(category);
            PlaySoundW(
                ambient.c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_LOOP
            );
        }
        CloseJudgingPavilion(category, window);
    }

    static void ReleaseBitmap(HBITMAP& bitmap) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
            bitmap = nullptr;
        }
    }

    HBITMAP LoadCryoBitmap(const wchar_t* name) const {
        const auto path = assets_ / L"BMP" / L"CRYO" / name;
        return LoadBitmapWithLegacyWarning(path);
    }

    void UploadCryoFrame(
        const metaverse::VideoFrame& frame,
        HBITMAP& destination,
        int& width,
        int& height
    ) {
        ReleaseBitmap(destination);
        BITMAPINFO information = {};
        information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        information.bmiHeader.biWidth = frame.width;
        information.bmiHeader.biHeight = -frame.height;
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HDC screen = GetDC(nullptr);
        destination = CreateDIBSection(
            screen, &information, DIB_RGB_COLORS, &pixels, nullptr, 0
        );
        ReleaseDC(nullptr, screen);
        if (destination == nullptr || pixels == nullptr) {
            ReleaseBitmap(destination);
            width = 0;
            height = 0;
            return;
        }
        std::memcpy(
            pixels,
            frame.bgra.data(),
            static_cast<std::size_t>(frame.stride) *
                static_cast<std::size_t>(frame.height)
        );
        width = frame.width;
        height = frame.height;
    }

    void ResetCryoAssets(bool stop_legacy_sound = false) {
        // Destructor 0x0040621f closes the +0x210 ROTATE child first and the
        // +0x20c INFO child second. Closing INFO also ends any AVI-embedded
        // PCM before the shared legacy channel is stopped.
        cryo_rotate_decoder_.Close();
        ReleaseBitmap(cryo_rotate_bitmap_);
        cryo_info_decoder_.Close();
        if (cryo_info_playing_ || cryo_info_audio_clocked_ ||
            cryo_info_audio_) {
            audio_player_.Stop();
        }
        ReleaseBitmap(cryo_info_bitmap_);
        if (stop_legacy_sound) {
            PlaySoundW(nullptr, nullptr, 0);
        }

        // The original then deletes BROWSL..SELECT and NAMEBIO..FAVQUOTE.
        for (HBITMAP& highlight : cryo_control_highlight_bitmaps_) {
            ReleaseBitmap(highlight);
        }
        for (HBITMAP& highlight : cryo_info_highlight_bitmaps_) {
            ReleaseBitmap(highlight);
        }

        // Its +0x1c4 backing store has no separate object in the native
        // retained compositor. The remaining ownership order is base,
        // BUTTONS, sponsored portrait, INFOBK, then MONEY. We retain one
        // portrait per slot to reconstruct the original non-erasing pixels,
        // so all five native reconstruction handles are released at the
        // single original portrait-wrapper boundary.
        ResetBitmap();
        ReleaseBitmap(cryo_controls_bitmap_);
        for (HBITMAP& portrait : cryo_portrait_bitmaps_) {
            ReleaseBitmap(portrait);
        }
        ReleaseBitmap(cryo_info_background_bitmap_);
        ReleaseBitmap(cryo_money_bitmap_);
        cryo_info_audio_.reset();
        cryo_info_visible_ = false;
        cryo_rotate_playing_ = false;
        cryo_info_playing_ = false;
        cryo_info_audio_clocked_ = false;
        cryo_info_audio_sample_rate_ = 0;
        cryo_info_first_frame_ = 0;
        cryo_info_clocked_frame_ = -1;
        cryo_rotate_width_ = 0;
        cryo_rotate_height_ = 0;
        cryo_info_width_ = 0;
        cryo_info_height_ = 0;
        cryo_pressed_action_ = 0;
        cryo_media_children_constructed_ = false;
        cryo_base_only_exposure_ = false;
    }

    void LoadCryoAssets(bool construct_media_children) {
        // fcn.0040521d's warning/load order is CRYO, INFOBK, BUTTONS, MONEY,
        // then the five controls and six detail labels. Keeping the base in
        // the shared bitmap slot lets the ordinary painter retain it across
        // C11 without reconstructing the dialog.
        if (bitmap_ == nullptr) {
            bitmap_ = LoadCryoBitmap(L"CRYO.BMP");
            BITMAP details = {};
            if (bitmap_ != nullptr &&
                GetObjectW(bitmap_, sizeof(details), &details)) {
                bitmap_width_ = details.bmWidth;
                bitmap_height_ = details.bmHeight;
            }
            // 0x00405319 sets only the base-dirty flag, then immediately
            // invalidates and updates the owner before INFOBK or any control
            // wrapper exists. Suppress the native compositor's unconditional
            // cash/control overlays for this one synchronous base exposure.
            cryo_base_only_exposure_ = true;
            if (owner_window_ != nullptr && IsWindow(owner_window_)) {
                InvalidateRect(owner_window_, nullptr, FALSE);
                UpdateWindow(owner_window_);
            }
            cryo_base_only_exposure_ = false;
        }
        if (cryo_info_background_bitmap_ == nullptr) {
            cryo_info_background_bitmap_ = LoadCryoBitmap(L"INFOBK.BMP");
        }
        if (cryo_controls_bitmap_ == nullptr) {
            cryo_controls_bitmap_ = LoadCryoBitmap(L"BUTTONS.BMP");
        }
        if (cryo_money_bitmap_ == nullptr) {
            cryo_money_bitmap_ = LoadCryoBitmap(L"MONEY.BMP");
        }
        for (std::size_t index = 0;
             index < cryo_control_highlight_bitmaps_.size(); ++index) {
            if (cryo_control_highlight_bitmaps_[index] == nullptr) {
                cryo_control_highlight_bitmaps_[index] =
                    LoadCryoBitmap(
                        metaverse::kCryoControlHighlightFiles[index].data()
                    );
            }
        }
        for (std::size_t index = 0;
             index < cryo_info_highlight_bitmaps_.size(); ++index) {
            if (cryo_info_highlight_bitmaps_[index] == nullptr) {
                cryo_info_highlight_bitmaps_[index] =
                    LoadCryoBitmap(
                        metaverse::kCryoDetailHighlightFiles[index].data()
                    );
            }
        }
        if (construct_media_children) {
            ConstructCryoMediaChildren();
        }
    }

    bool EnsureCryoDecoder(
        metaverse::VideoDecoder& decoder,
        const wchar_t* name,
        std::string& error
    ) {
        if (decoder.IsOpen()) {
            error.clear();
            return true;
        }
        return decoder.Open(assets_ / L"MOV" / L"CRYO" / name, error);
    }

    void LoadCryoRotationStill() {
        std::string error;
        if (!EnsureCryoDecoder(cryo_rotate_decoder_, L"ROTATE.AVI", error)) {
            status_ = L"Cryo contestant video failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        const std::int64_t first_frame =
            static_cast<std::int64_t>(browsed_contestant_) *
            metaverse::kCryoCandidateFrameCount;
        if (!cryo_rotate_decoder_.SeekFrame(first_frame, frame, ended, error) || ended) {
            status_ = L"Cryo contestant seek failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }
        UploadCryoFrame(
            frame, cryo_rotate_bitmap_, cryo_rotate_width_, cryo_rotate_height_
        );
        cryo_rotation_next_frame_ = first_frame;
        cryo_rotate_playing_ = false;
    }

    void ConstructCryoMediaChildren() {
        // The timer callback creates both logical wrappers after C11 returns.
        // Each wrapper remains owned even when MCI create/open fails, so mark
        // construction before attempting either native decoder.
        cryo_media_children_constructed_ = true;
        LoadCryoRotationStill();

        std::string error;
        if (!EnsureCryoDecoder(cryo_info_decoder_, L"INFO.AVI", error)) {
            status_ = L"Cryo information video failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!cryo_info_decoder_.SeekFrame(0, frame, ended, error) || ended) {
            status_ = L"Cryo information seek failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }
        UploadCryoFrame(
            frame, cryo_info_bitmap_, cryo_info_width_, cryo_info_height_
        );
        cryo_info_playing_ = false;
        cryo_info_audio_clocked_ = false;
    }

    void BrowseCryoContestant(std::int32_t direction) {
        const auto plan = metaverse::PlanCryoBrowse(
            browsed_contestant_ + 1, direction, cryo_info_visible_
        );
        if (!plan) {
            return;
        }
        browsed_contestant_ = plan->contestant_id_after - 1;

        // fcn.00404714/fcn.0040483b update the visible INFO child first,
        // including its stop and NAME/BIO seek, before stopping and seeking
        // ROTATE. Keep that order even though the native players are split.
        if (cryo_info_visible_) {
            LoadCryoInfoStill();
        }
        LoadCryoRotationStill();

        // The handlers pass 0x19: ASYNC | LOOP | NOSTOP. This preserves an
        // active announcement but restores CRYO.WAV after a completed
        // synchronous prompt or NOCASH1 response. NODEFAULT is absent.
        const auto ambient = assets_ / L"WAV" / L"CRYO.WAV";
        PlaySoundW(
            ambient.c_str(), nullptr,
            SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NOSTOP
        );
    }

    void LoadCryoInfoStill() {
        // Both browse handlers stop the visible INFO MCI child before seeking
        // the newly selected contestant's first information frame. In the
        // native split decoder/player path, stop its embedded audio as well.
        cryo_info_playing_ = false;
        cryo_info_audio_clocked_ = false;
        audio_player_.Stop();
        std::string error;
        if (!EnsureCryoDecoder(cryo_info_decoder_, L"INFO.AVI", error)) {
            status_ = L"Cryo information video failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }
        const auto plan = metaverse::PlanCryoInfoPlayback(
            browsed_contestant_ + 1, 0
        );
        if (!plan) {
            return;
        }
        const metaverse::CryoInfoRange range = plan->range;
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!cryo_info_decoder_.SeekFrame(
                range.first_frame, frame, ended, error
            ) || ended) {
            status_ = L"Cryo information seek failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }
        UploadCryoFrame(
            frame, cryo_info_bitmap_, cryo_info_width_, cryo_info_height_
        );
        cryo_info_playing_ = false;
    }

    void StartCryoRotation() {
        std::string error;
        if (!EnsureCryoDecoder(cryo_rotate_decoder_, L"ROTATE.AVI", error)) {
            status_ = L"Cryo contestant video failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }

        const metaverse::CryoRotationSegment segment =
            metaverse::NextCryoRotationSegment(
                browsed_contestant_ + 1,
                cryo_rotation_next_frame_
            );
        if (!segment.Valid()) {
            return;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!cryo_rotate_decoder_.SeekFrame(
                segment.first_frame, frame, ended, error
            ) || ended) {
            status_ = L"Cryo contestant seek failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }
        UploadCryoFrame(
            frame, cryo_rotate_bitmap_, cryo_rotate_width_, cryo_rotate_height_
        );
        cryo_rotation_next_frame_ = segment.next_frame;
        cryo_rotate_last_frame_ = segment.last_frame;
        cryo_rotate_playing_ =
            segment.HasForwardPlayback() &&
            segment.first_frame < segment.last_frame;
        cryo_rotate_due_ = std::chrono::steady_clock::now();
    }

    void ToggleCryoInfo() {
        const auto plan = metaverse::PlanCryoInformationToggle(
            cryo_info_visible_, browsed_contestant_ + 1
        );
        if (!plan) {
            return;
        }
        cryo_info_visible_ = plan->visible_after;
        cryo_info_playing_ = false;
        cryo_info_audio_clocked_ = false;
        audio_player_.Stop();
        if (cryo_info_visible_) {
            LoadCryoInfoStill();
        }
    }

    void StartCryoInfoSegment(std::size_t detail) {
        const auto plan = metaverse::PlanCryoInfoPlayback(
            browsed_contestant_ + 1, detail
        );
        if (!plan) {
            return;
        }
        // Each of the six original Cryo detail handlers stops the INFO child,
        // plays TT0.WAV..TT5.WAV synchronously, and only then starts its
        // contestant-specific INFO.AVI range.
        cryo_info_playing_ = false;
        cryo_info_audio_clocked_ = false;
        audio_player_.Stop();
        const auto prompt = assets_ / L"WAV" /
            (L"TT" + std::to_wstring(detail) + L".WAV");
        PlaySoundW(
            prompt.c_str(), nullptr,
            // The six handlers pass flags zero to sndPlaySoundA: synchronous,
            // with default-sound fallback still enabled. SND_FILENAME is the
            // Unicode API's explicit equivalent for the already-resolved path.
            SND_FILENAME | SND_SYNC
        );
        std::string error;
        if (!EnsureCryoDecoder(cryo_info_decoder_, L"INFO.AVI", error)) {
            status_ = L"Cryo information video failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }
        const metaverse::CryoInfoRange range = plan->range;
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!cryo_info_decoder_.SeekFrame(
                range.first_frame, frame, ended, error
            ) || ended) {
            status_ = L"Cryo information seek failed: " +
                      std::wstring(error.begin(), error.end());
            return;
        }
        UploadCryoFrame(
            frame, cryo_info_bitmap_, cryo_info_width_, cryo_info_height_
        );
        cryo_info_last_frame_ = range.last_frame;
        cryo_info_playing_ = range.first_frame < range.last_frame;
        cryo_info_due_ = std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(
                    std::clamp(frame.duration_seconds, 0.01, 0.25)
                )
            );

        if (!cryo_info_audio_) {
            metaverse::AudioTrack audio;
            bool has_audio = false;
            if (metaverse::DecodeAudioTrack(
                    assets_ / L"MOV" / L"CRYO" / L"INFO.AVI",
                    audio, has_audio, error
                ) && has_audio) {
                cryo_info_audio_ = std::move(audio);
            }
        }
        if (cryo_info_audio_) {
            const auto& source = *cryo_info_audio_;
            const double first_second =
                static_cast<double>(range.first_frame) /
                metaverse::kCryoInfoFramesPerSecond;
            const double last_second =
                static_cast<double>(range.last_frame + 1) /
                metaverse::kCryoInfoFramesPerSecond;
            const std::size_t first_sample = std::min(
                source.samples.size(),
                static_cast<std::size_t>(first_second * source.sample_rate) *
                    static_cast<std::size_t>(source.channels)
            );
            const std::size_t last_sample = std::min(
                source.samples.size(),
                static_cast<std::size_t>(last_second * source.sample_rate) *
                    static_cast<std::size_t>(source.channels)
            );
            if (last_sample > first_sample) {
                metaverse::AudioTrack segment;
                segment.sample_rate = source.sample_rate;
                segment.channels = source.channels;
                segment.samples.assign(
                    source.samples.begin() + static_cast<std::ptrdiff_t>(first_sample),
                    source.samples.begin() + static_cast<std::ptrdiff_t>(last_sample)
                );
                if (!audio_player_.Play(segment, false, error)) {
                    status_ = L"Cryo information audio failed: " +
                              std::wstring(error.begin(), error.end());
                } else {
                    cryo_info_audio_clocked_ = true;
                    cryo_info_audio_sample_rate_ = segment.sample_rate;
                    cryo_info_first_frame_ = range.first_frame;
                    cryo_info_clocked_frame_ = frame.frame_index;
                    cryo_info_due_ = std::chrono::steady_clock::now();
                    status_.clear();
                }
            }
        }
    }

    void TickCryo(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (scene_ != kCryoScene) {
            return;
        }
        const auto tick_decoder = [&]<typename Decoder>(
            Decoder& decoder,
            bool& playing,
            std::int64_t last_frame,
            std::chrono::steady_clock::time_point& due,
            HBITMAP& bitmap,
            int& width,
            int& height
        ) {
            if (!playing || now < due) {
                return;
            }
            metaverse::VideoFrame frame;
            bool ended = false;
            std::string error;
            if (!decoder.DecodeNext(frame, ended, error) || ended ||
                frame.frame_index > last_frame) {
                playing = false;
                if (!error.empty()) {
                    status_ = L"Cryo video decode failed: " +
                              std::wstring(error.begin(), error.end());
                    UpdateTitle(window);
                }
                return;
            }
            UploadCryoFrame(frame, bitmap, width, height);
            if (frame.frame_index >= last_frame) {
                playing = false;
            }
            due = now +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(
                        std::clamp(frame.duration_seconds, 0.01, 0.25)
                    )
                );
            InvalidateRect(window, nullptr, FALSE);
        };
        tick_decoder(
            cryo_rotate_decoder_, cryo_rotate_playing_, cryo_rotate_last_frame_,
            cryo_rotate_due_, cryo_rotate_bitmap_,
            cryo_rotate_width_, cryo_rotate_height_
        );
        const auto tick_info = [&]() {
            if (!cryo_info_playing_ || now < cryo_info_due_) {
                return;
            }
            metaverse::VideoFrame frame;
            bool ended = false;
            std::string error;
            bool decoded = false;
            if (cryo_info_audio_clocked_) {
                if (!audio_player_.IsPlaying()) {
                    cryo_info_audio_clocked_ = false;
                    decoded = cryo_info_decoder_.DecodeNext(
                        frame, ended, error
                    );
                } else {
                    const std::int64_t relative =
                        cryo_info_decoder_.FrameForAudioSamples(
                            audio_player_.PositionSamples(),
                            static_cast<std::uint32_t>(
                                cryo_info_audio_sample_rate_
                            )
                        );
                    const std::int64_t target = relative < 0
                        ? -1
                        : cryo_info_first_frame_ + relative;
                    if (target < 0) {
                        cryo_info_audio_clocked_ = false;
                        decoded = cryo_info_decoder_.DecodeNext(
                            frame, ended, error
                        );
                    } else if (target <= cryo_info_clocked_frame_) {
                        cryo_info_due_ = now + std::chrono::milliseconds(10);
                        return;
                    } else if (target == cryo_info_clocked_frame_ + 1) {
                        decoded = cryo_info_decoder_.DecodeNext(
                            frame, ended, error
                        );
                    } else {
                        decoded = cryo_info_decoder_.SeekFrame(
                            target, frame, ended, error
                        );
                    }
                }
            } else {
                decoded = cryo_info_decoder_.DecodeNext(frame, ended, error);
            }
            if (!decoded || ended ||
                frame.frame_index > cryo_info_last_frame_) {
                cryo_info_playing_ = false;
                cryo_info_audio_clocked_ = false;
                audio_player_.Stop();
                if (!error.empty()) {
                    status_ = L"Cryo video decode failed: " +
                              std::wstring(error.begin(), error.end());
                    UpdateTitle(window);
                }
                return;
            }
            UploadCryoFrame(
                frame, cryo_info_bitmap_, cryo_info_width_, cryo_info_height_
            );
            cryo_info_clocked_frame_ = frame.frame_index;
            if (frame.frame_index >= cryo_info_last_frame_) {
                cryo_info_playing_ = false;
                cryo_info_audio_clocked_ = false;
                audio_player_.Stop();
            }
            if (cryo_info_audio_clocked_) {
                cryo_info_due_ = now + std::chrono::milliseconds(10);
            } else {
                cryo_info_due_ = now +
                    std::chrono::duration_cast<
                        std::chrono::steady_clock::duration
                    >(std::chrono::duration<double>(
                        std::clamp(frame.duration_seconds, 0.01, 0.25)
                    ));
            }
            InvalidateRect(window, nullptr, FALSE);
        };
        tick_info();
    }

    void SponsorCurrentCryoContestant(HWND window) {
        const std::int32_t candidate = browsed_contestant_ + 1;
        std::string error;
        if (!metaverse::SponsorContestant(
                round_, selected_contestants_, candidate, error
            )) {
            status_ = std::wstring(error.begin(), error.end()) + L".";
            if (error == "this contestant is already selected") {
                // fcn.00404a93 uses AfxMessageBox(0x30) with this exact
                // punctuation/grammar before returning without changing cash.
                MessageBoxW(
                    window,
                    L"This girl has be selected,\n please select another one.",
                    metaverse::kLegacyDialogCaption,
                    static_cast<UINT>(
                        metaverse::kLegacyWarningMessageBoxStyle
                    )
                );
            } else if (error.find("cash") != std::string::npos) {
                const auto no_cash = assets_ / L"WAV" / L"ANNOUNCE" /
                    L"NOCASH1.WAV";
                PlaySoundW(
                    no_cash.c_str(), nullptr,
                    SND_FILENAME | SND_SYNC
                );
            }
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        selected_contestants_.push_back(candidate);
        const auto portrait = metaverse::PlanCryoPortraitInsertion(
            candidate, selected_contestants_.size()
        );
        if (portrait) {
            // 0x00405121 formats and loads GIRL<n>.BMP only after the debit
            // and roster insertion. The original retains just the latest DIB
            // because prior pixels remain in its non-erasing window DC; keep
            // one handle per slot solely to reconstruct those pixels after a
            // modern Win32 exposure, without eagerly loading unused girls.
            ReleaseBitmap(cryo_portrait_bitmaps_[portrait->slot]);
            cryo_portrait_bitmaps_[portrait->slot] = LoadCryoBitmap(
                portrait->relative_filename.data()
            );
        }
        // 0x00404c19 queues the changed cash rectangle, then 0x00405121
        // installs the new portrait and forces an UpdateWindow before C12 is
        // invoked. Preserve that visible intermediate state instead of first
        // revealing both the portrait and host overlay in one repaint.
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        if (selected_contestants_.size() == 1 &&
            !cryo_first_sponsor_host_played_) {
            std::string host_error;
            if (StartHostInterstitial(HostCue{1, 2}, host_error)) {
                cryo_first_sponsor_host_played_ = true;
                // fcn.00404a93 resumes at 0x00404c69 after modal C12, while
                // fcn.00403fe7 restarts CRYO.WAV after the host object (and
                // any synchronous OUCH response) has finished.
                pending_host_resume_cryo_ambient_ = true;
                status_ = L"Playing original first-sponsorship host response C12.";
                UpdateTitle(window);
                InvalidateRect(window, nullptr, FALSE);
                return;
            }
            cryo_first_sponsor_host_played_ = true;
            // fcn.00403fe7 always reissues CRYO.WAV after its modal host
            // helper returns, including an allocation/media failure. The
            // native host starter may already have silenced that legacy
            // channel before reporting an audio failure, so restore it here.
            const auto ambient = assets_ / L"WAV" / L"CRYO.WAV";
            PlaySoundW(
                ambient.c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_LOOP
            );
            status_ = L"First-sponsorship host response failed: " +
                      std::wstring(host_error.begin(), host_error.end()) + L".";
        }
        if (selected_contestants_.size() < metaverse::kContestantsPerRound) {
            status_.clear();
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        for (std::size_t index = 0; index < selected_contestants_.size(); ++index) {
            round_.contestants[index].contestant_id = selected_contestants_[index];
        }
        // The fifth selection closes the modal Cryo dialog. Destructor
        // 0x0040621f closes ROTATE, then INFO (including embedded PCM), stops
        // CRYO.WAV, and releases every owned bitmap before control returns to
        // 0x00401b03. Only then may fcn.0040304e perform its ten Brains/Talent
        // variant draws and writes.
        ResetCryoAssets(true);
        std::string variant_error;
        if (!PreparePavilionVariants(variant_error)) {
            status_ = std::wstring(variant_error.begin(), variant_error.end());
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        round_tallied_ = false;
        tally_result_.reset();
        NextScene(1, window);
        status_ = L"Five contestants sponsored; set the category weights.";
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void PlayTransition(const metaverse::SceneTransition& transition) {
        // MCIWndPlayTo replaces any prior bounded playback. Its embedded AVI
        // audio stops with that range; frame-only seeks remain silent.  The
        // original helper also destroys both saved positional-overlay DIBs
        // before issuing any seek/play command, including idle and slot ranges.
        ResetNavigationOverlay();
        audio_player_.Stop();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;
        if (!video_decoder_.IsOpen()) {
            return;
        }
        const std::int64_t last_frame =
            metaverse::ClampNavigationPlaybackLastFrame(
                transition.last_frame, video_decoder_.FrameCount()
            );
        if (last_frame < transition.first_frame) {
            status_ = L"Transition range begins beyond the video stream.";
            video_paused_ = true;
            playback_range_.reset();
            return;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        std::string error;
        if (!video_decoder_.SeekFrame(transition.first_frame, frame, ended, error) || ended) {
            status_ = L"Transition seek failed: " +
                      std::wstring(error.begin(), error.end());
            video_paused_ = true;
            playback_range_.reset();
            return;
        }
        UploadVideoFrame(frame);
        if (frame.frame_index >= last_frame) {
            video_paused_ = true;
            playback_range_.reset();
        } else {
            if (navigation_media_audio_) {
                const auto& source = *navigation_media_audio_;
                const double frame_duration = std::max(
                    frame.duration_seconds, 0.0
                );
                const double first_second = std::max(
                    frame.timestamp_seconds, 0.0
                );
                const double last_second = first_second +
                    static_cast<double>(
                        last_frame - transition.first_frame + 1
                    ) * frame_duration;
                const std::size_t channel_count = static_cast<std::size_t>(
                    source.channels
                );
                const auto sample_offset = [&](double seconds) {
                    return std::min(
                        source.samples.size(),
                        static_cast<std::size_t>(
                            seconds * static_cast<double>(source.sample_rate)
                        ) * channel_count
                    );
                };
                const std::size_t first_sample = sample_offset(first_second);
                const std::size_t last_sample = sample_offset(last_second);
                if (last_sample > first_sample) {
                    metaverse::AudioTrack segment;
                    segment.sample_rate = source.sample_rate;
                    segment.channels = source.channels;
                    segment.samples.assign(
                        source.samples.begin() +
                            static_cast<std::ptrdiff_t>(first_sample),
                        source.samples.begin() +
                            static_cast<std::ptrdiff_t>(last_sample)
                    );
                    std::string audio_error;
                    if (!audio_player_.Play(segment, false, audio_error)) {
                        status_ = L"Navigation range audio failed: " +
                            std::wstring(
                                audio_error.begin(), audio_error.end()
                            );
                    } else {
                        video_audio_clocked_ = true;
                        video_audio_sample_rate_ =
                            static_cast<std::uint32_t>(segment.sample_rate);
                        video_audio_clocked_frame_ = frame.frame_index;
                        video_audio_clock_start_frame_ =
                            transition.first_frame;
                    }
                }
            }
            video_paused_ = false;
            playback_range_ = std::pair<std::int64_t, std::int64_t>{
                transition.first_frame, last_frame
            };
            next_frame_due_ = std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(
                        std::clamp(frame.duration_seconds, 0.01, 0.25)
                    )
                );
        }
    }

    std::filesystem::path HostAssetsRoot(const HostCue& cue) const {
        if (cue.channel == 2 && assets2_) {
            return *assets2_;
        }
        return assets_;
    }

    void ResetHostInterstitial() {
        host_audio_player_.Stop();
        host_video_decoder_.Close();
        host_motion_player_ = {};
        host_motion_active_ = false;
        host_motion_sync_anchor_.reset();
        host_visual_complete_ = false;
        host_audio_clocked_frame_ = -1;
        if (host_bitmap_ != nullptr) {
            DeleteObject(host_bitmap_);
            host_bitmap_ = nullptr;
        }
        host_bitmap_width_ = 0;
        host_bitmap_height_ = 0;
        host_ouch_pending_ = false;
        host_interstitial_active_ = false;
    }

    void ClearHostInterstitialVisual(HWND window) {
        ResetHostInterstitial();

        // Mode 6 is a child modal in MM.EXE, so destroying it exposes an intact
        // owner surface. Native composites that child into the owner HWND.
        // Weight and ORDER consume retained painter flags one dirty rectangle
        // at a time; if their continuation paints first, only the gauges or
        // phrases replace the final host sprite. Rearm and synchronously paint
        // the complete parent before any continuation can consume those flags.
        if (scene_ == kWeightScene) {
            weight_painter_.base_dirty = true;
        } else if (scene_ == kCommentsScene) {
            comment_painter_.base_dirty = true;
        }
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
    }

    bool HostBitmapHitTest(int logical_x, int logical_y) const {
        const auto bounds = metaverse::MotionSpriteBoundsFromTopLeft(
            host_bitmap_x_, host_bitmap_y_,
            host_bitmap_width_, host_bitmap_height_
        );
        if (host_bitmap_ == nullptr || host_bitmap_width_ <= 0 ||
            host_bitmap_height_ <= 0 ||
            !metaverse::MotionSpritePointInsideLegacyHitBounds(
                bounds, logical_x, logical_y
            )) {
            return false;
        }
        HDC source = CreateCompatibleDC(nullptr);
        if (source == nullptr) {
            return false;
        }
        HGDIOBJ previous = SelectObject(source, host_bitmap_);
        const COLORREF key = GetPixel(source, 0, 0);
        const COLORREF pixel = GetPixel(
            source,
            logical_x - bounds.left,
            logical_y - bounds.top
        );
        SelectObject(source, previous);
        DeleteDC(source);
        return pixel != CLR_INVALID && key != CLR_INVALID && pixel != key;
    }

    void UploadHostFrame(const metaverse::VideoFrame& frame) {
        if (host_bitmap_ != nullptr) {
            DeleteObject(host_bitmap_);
            host_bitmap_ = nullptr;
        }
        host_bitmap_width_ = 0;
        host_bitmap_height_ = 0;

        BITMAPINFO information = {};
        information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        information.bmiHeader.biWidth = frame.width;
        information.bmiHeader.biHeight = -frame.height;
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HDC screen = GetDC(nullptr);
        host_bitmap_ = CreateDIBSection(
            screen, &information, DIB_RGB_COLORS, &pixels, nullptr, 0
        );
        ReleaseDC(nullptr, screen);
        if (host_bitmap_ == nullptr || pixels == nullptr) {
            if (host_bitmap_ != nullptr) {
                DeleteObject(host_bitmap_);
                host_bitmap_ = nullptr;
            }
            return;
        }
        std::memcpy(
            pixels,
            frame.bgra.data(),
            static_cast<std::size_t>(frame.stride) *
                static_cast<std::size_t>(frame.height)
        );
        host_bitmap_width_ = frame.width;
        host_bitmap_height_ = frame.height;
    }

    bool StartHostInterstitial(const HostCue& cue, std::string& error) {
        ResetHostInterstitial();
        // Each recovered C-entry timer clears sndPlaySound before allocating
        // or opening its generic-media child. This also clips an ORDER TTn
        // started during C51's two-second pre-entry window on loader failure.
        PlaySoundW(nullptr, nullptr, 0);
        // Cryo helper 0x00403fe7 uses this same generic mode-6 boundary for
        // channel 1 take 1 (C11) and take 2 (C12): legacy audio is stopped,
        // the modal host runner owns input until its companion WAV ends, its
        // wrapper is destroyed, and CRYO.WAV is restarted unconditionally by
        // FinishCryoHostEntry or the first-sponsor continuation—even after a
        // media-open failure.
        if (cue.channel < 0 ||
            cue.channel >= static_cast<std::int32_t>(metaverse::kHostPointCount)) {
            error = "host cue channel has no HPNT.DAT coordinate";
            return false;
        }
        const std::wstring stem = metaverse::HostClipStem(cue.channel, cue.take);
        if (stem.empty()) {
            error = "host cue number is invalid";
            return false;
        }
        const auto root = HostAssetsRoot(cue);
        metaverse::HostPointTable points{};
        if (!metaverse::LoadHostPointData(
                root / L"DAT" / L"HOST" / L"HPNT.DAT", points, error
            )) {
            return false;
        }
        const auto movie = root / L"MOV" / L"HOST" / (stem + L".AVI");
        if (!host_video_decoder_.Open(movie, error)) {
            return false;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!host_video_decoder_.DecodeNext(frame, ended, error) || ended) {
            if (error.empty()) {
                error = "host movie ended before its first frame";
            }
            ResetHostInterstitial();
            return false;
        }
        UploadHostFrame(frame);
        if (host_bitmap_ == nullptr) {
            error = "could not create the host overlay bitmap";
            ResetHostInterstitial();
            return false;
        }
        const auto point = points[static_cast<std::size_t>(cue.channel)];
        host_bitmap_x_ = point.x;
        host_bitmap_y_ = point.y;
        const auto sound = root / L"WAV" / L"HOST" / (stem + L".WAV");
        metaverse::AudioTrack host_audio;
        bool has_audio = false;
        if (!metaverse::DecodeAudioTrack(
                sound, host_audio, has_audio, error
            ) || !has_audio ||
            !host_audio_player_.Play(host_audio, false, error)) {
            error = "could not play the companion host WAV " +
                    sound.string() + ": " + error;
            ResetHostInterstitial();
            return false;
        }
        host_interstitial_active_ = true;
        host_audio_clocked_frame_ = frame.frame_index;
        host_next_frame_due_ = std::chrono::steady_clock::now();
        host_entry_played_[static_cast<std::size_t>(cue.channel)] = true;
        error.clear();
        return true;
    }

    void ScheduleHostEntry(const HostCue& cue, bool charge_performance) {
        pending_host_entry_cue_ = cue;
        pending_host_entry_charge_ = charge_performance;
        pending_host_entry_due_ = std::chrono::steady_clock::now() +
            HostEntryDelayForScene(scene_);
    }

    void FinishJudgingHostEntry(
        metaverse::JudgingCategory category,
        HWND window
    ) {
        // The C21/C31/C41 timer bodies resume inside their existing dialog.
        // Background, controls, gauges, and portraits are not reconstructed.
        const auto continuation = metaverse::PlanJudgingEntryTimer(
            category, false
        );
        if (continuation.construct_rotate_after_timer) {
            current_rating_ = 0;
            // Looks has no performance-surcharge gate. Its controls are live
            // even when the legacy +0xac contestant ID remains zero because
            // the copied pending list was already exhausted.
            performance_access_granted_ = true;
            const auto first = continuation.auto_select_after_timer
                ? NextEligibleJudgingSlot(category, 0)
                : std::optional<std::size_t>{};
            if (first) {
                judging_contestant_ = *first;
                judging_contestant_active_ = true;
                looks_selection_frame_dirty_ = true;
                LoadLooksContestantFrame();
            } else {
                judging_contestant_active_ = false;
                LoadLooksRotateFrame(0, false);
            }
        }
        if (continuation.start_ambient_after_timer) {
            const auto ambient = PavilionAmbientSound(category);
            PlaySoundW(
                ambient.c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_LOOP
            );
        }
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
    }

    void FinishCryoHostEntry(HWND window) {
        // fcn.00403fe7 restarts CRYO.WAV before the timer resumes at
        // 0x00403f6c and creates ROTATE followed by INFO. The existing base,
        // controls, and money DIBs remain owned by this same dialog.
        const auto ambient = assets_ / L"WAV" / L"CRYO.WAV";
        PlaySoundW(
            ambient.c_str(), nullptr,
            SND_FILENAME | SND_ASYNC | SND_LOOP
        );
        ConstructCryoMediaChildren();
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
    }

    bool TickPendingHostEntry(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (!pending_host_entry_cue_ || now < pending_host_entry_due_) {
            return false;
        }
        const HostCue cue = *pending_host_entry_cue_;
        const bool charge_performance = pending_host_entry_charge_;
        pending_host_entry_cue_.reset();
        // The original timer clears its entry flag before the generic modal
        // object is created.  Preserve that ordering so an MMS/AVI/WAV/open
        // failure cannot replay C21/C31/C41 on a later pavilion visit.
        if (cue.channel >= 0 &&
            cue.channel < static_cast<std::int32_t>(host_entry_played_.size())) {
            metaverse::ConsumeLegacyHostEntryAttempt(
                host_entry_played_[static_cast<std::size_t>(cue.channel)]
            );
        }
        std::string error;
        if (StartHostInterstitial(cue, error)) {
            pending_host_scene_load_charge_ = charge_performance;
            status_ = L"Playing original host introduction " +
                metaverse::HostClipStem(cue.channel, cue.take) + L".";
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return true;
        }
        const std::wstring failure = L"Host introduction failed: " +
            std::wstring(error.begin(), error.end()) + L".";
        // The original timer callback continues the dialog after the modal
        // host helper returns.  Re-enter without another cue on failure too.
        if (scene_ == kWeightScene) {
            FinishWeightHostEntry(window);
        } else if (scene_ == kCommentsScene) {
            FinishOrderHostEntry(window);
        } else if (scene_ == kCryoScene) {
            FinishCryoHostEntry(window);
        } else if (const auto category = JudgingCategoryForScene(scene_)) {
            FinishJudgingHostEntry(*category, window);
        } else {
            LoadScene(charge_performance, false);
        }
        status_ = failure;
        UpdateTitle(window);
        if (scene_ != kWeightScene && scene_ != kCommentsScene &&
            scene_ != kCryoScene) {
            InvalidateRect(window, nullptr, FALSE);
        }
        return true;
    }

    bool StartHostReaction(
        metaverse::JudgingCategory category,
        std::int32_t score,
        HWND window,
        bool complete_accepted_contestant = false
    ) {
        ResetHostInterstitial();
        // Both recovered X dispatchers clear the ordinary sndPlaySound
        // channel before allocating/loading the generic media object.
        PlaySoundW(nullptr, nullptr, 0);
        const std::int32_t group =
            metaverse::HostReactionGroupForScore(score);
        std::string error;
        // fcn.0040e8ae opens and reads HNRD.DAT for every X selection. Do not
        // keep using the snapshot loaded before the profile/intro/round flow.
        metaverse::HostReactionState latest_reactions;
        // 0x0040e9c5 zeroes the table and the CFile open/read return is not
        // consulted. Missing or short state therefore still selects an X clip;
        // malformed and inactive raw words survive the subsequent full rewrite.
        metaverse::LoadLegacyHostReactionStateForSelection(
            HostReactionDataPath(), latest_reactions
        );
        host_reaction_state_ = std::move(latest_reactions);
        // X selection is performed inside the motion-backed loader, after its
        // timeGetTime reseed at 0x0040ddc0.  This ordering is distinct from
        // navigation Simms, whose D-number is selected by the caller first,
        // and mode-6 C clips, whose early loader branch never reaches it.
        random_.Seed(timeGetTime());
        const std::int32_t variant = metaverse::DrawHostReactionVariant(
            host_reaction_state_, group, random_, error
        );
        const std::wstring stem =
            metaverse::HostReactionStem(group, variant);
        if (variant == 0 || stem.empty()) {
            status_ = L"Host reaction selection failed: " +
                      std::wstring(error.begin(), error.end()) + L".";
            UpdateTitle(window);
            return false;
        }
        if (!metaverse::SaveLegacyHostReactionState(
                HostReactionDataPath(), host_reaction_state_, error
            )) {
            status_ = L"Host reaction state save failed: " +
                      std::wstring(error.begin(), error.end()) + L".";
            UpdateTitle(window);
            return false;
        }

        const auto root = JudgingAssetsRoot(category);
        metaverse::MotionScript script;
        if (!metaverse::LoadMotionScript(
                root / L"DAT" / L"HOST" / (stem + L".MMS"), script, error
            ) ||
            !host_motion_player_.Reset(script, error, 4) ||
            !host_video_decoder_.Open(
                root / L"MOV" / L"HOST" / (stem + L".AVI"), error
            )) {
            ResetHostInterstitial();
            status_ = L"Host reaction " + stem + L" failed: " +
                      std::wstring(error.begin(), error.end()) + L".";
            UpdateTitle(window);
            return false;
        }

        metaverse::VideoFrame frame;
        bool ended = false;
        if (!host_video_decoder_.SeekFrame(
                host_motion_player_.Frame(), frame, ended, error
            ) || ended) {
            if (error.empty()) {
                error = "host reaction movie ended before its initial MMS frame";
            }
            ResetHostInterstitial();
            status_ = L"Host reaction " + stem + L" failed: " +
                      std::wstring(error.begin(), error.end()) + L".";
            UpdateTitle(window);
            return false;
        }
        UploadHostFrame(frame);
        if (host_bitmap_ == nullptr) {
            ResetHostInterstitial();
            status_ = L"Host reaction " + stem +
                      L" failed: could not upload its initial frame.";
            UpdateTitle(window);
            return false;
        }
        host_bitmap_x_ = host_motion_player_.X();
        host_bitmap_y_ = host_motion_player_.Y();
        const auto sound = root / L"WAV" / L"HOST" / (stem + L".WAV");
        metaverse::AudioTrack host_audio;
        bool has_audio = false;
        std::string companion_error;
        if (metaverse::DecodeAudioTrack(
                sound, host_audio, has_audio, companion_error
            ) && has_audio) {
            // 0x0040de9c calls the void WaveMix channel-replacement helper and
            // never tests whether it opened the X WAV. The MMS graph remains
            // the mode-4 master and therefore runs silently on audio failure.
            host_audio_player_.Play(host_audio, false, companion_error);
        }
        host_motion_active_ = true;
        host_interstitial_active_ = true;
        host_motion_sync_anchor_.reset();
        host_next_frame_due_ = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(
                host_motion_player_.TickDelayMilliseconds(5)
            );
        pending_host_judging_advance_ = PendingHostJudgingAdvance{
            category, JudgingDisposition::rated,
            complete_accepted_contestant
        };
        status_ = L"Playing original animated host reaction " + stem + L".";
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }

    bool TickHostInterstitial(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (!host_interstitial_active_) {
            return false;
        }
        if (now < host_next_frame_due_) {
            return true;
        }

        const auto finish = [&](std::wstring failure) {
            return FinishHostInterstitial(window, std::move(failure));
        };

        if (host_visual_complete_) {
            if (host_audio_player_.IsPlaying()) {
                host_next_frame_due_ = now + std::chrono::milliseconds(10);
                return true;
            }
            return finish({});
        }

        if (!host_motion_active_) {
            // The mode-6 host loader at 0x0040EB52 ignores the AVI clock.
            // Its 11,025 Hz companion WAV is the master, with a fixed
            // floor(samples * 10 / 11025) + 1 frame mapping.  It also returns
            // as soon as that WAV ends, even when the AVI has a silent tail.
            if (!host_audio_player_.IsPlaying()) {
                return finish({});
            }
            const std::int64_t wanted_frame =
                metaverse::HostFrameForAudioSamples(
                    host_audio_player_.PositionSamples()
                );
            if (wanted_frame != host_audio_clocked_frame_) {
                metaverse::VideoFrame frame;
                bool ended = false;
                std::string error;
                if (!host_video_decoder_.SeekFrame(
                        wanted_frame, frame, ended, error
                    )) {
                    return finish(
                        L"Host interstitial frame failed: " +
                        std::wstring(error.begin(), error.end())
                    );
                }
                if (ended) {
                    host_visual_complete_ = true;
                } else {
                    UploadHostFrame(frame);
                    if (host_bitmap_ == nullptr) {
                        return finish(
                            L"Host interstitial failed: could not upload a video frame."
                        );
                    }
                    host_audio_clocked_frame_ = frame.frame_index;
                    InvalidateRect(window, nullptr, FALSE);
                }
            }
            host_next_frame_due_ = now + std::chrono::milliseconds(10);
            return true;
        }

        if (host_motion_active_) {
            std::string error;
            if (host_motion_player_.UsesMode4TimedSynchronization()) {
                if (host_motion_sync_anchor_) {
                    const auto elapsed = std::chrono::duration_cast<
                        std::chrono::milliseconds
                    >(now - *host_motion_sync_anchor_).count();
                    host_motion_player_.ApplyMode4TimedCatchUp(
                        elapsed > 0
                            ? static_cast<std::uint32_t>(std::min<
                                  std::int64_t
                              >(elapsed, UINT32_MAX))
                            : 0U
                    );
                }
                host_motion_sync_anchor_ = now;
            } else {
                host_motion_sync_anchor_.reset();
            }
            if (!host_motion_player_.Tick(error)) {
                if (error.empty()) {
                    error = "motion script stopped before its completion record";
                }
                return finish(
                    L"Host reaction motion failed: " +
                    std::wstring(error.begin(), error.end())
                );
            }
            if (host_motion_player_.Complete()) {
                // fcn.0040d2ab calls 0x0040e4bd immediately when the MMS
                // result record completes. That common teardown stops the X
                // WAV; it never waits for a longer audio tail (X18 is 420 ms
                // longer than its graph). Mode 6 remains audio-master above.
                const auto completion =
                    metaverse::PlanMode4HostCompletion(
                        true, host_audio_player_.IsPlaying()
                    );
                if (completion.finish_modal) {
                    return finish({});
                }
            }

            metaverse::VideoFrame frame;
            bool ended = false;
            if (!host_video_decoder_.SeekFrame(
                    host_motion_player_.Frame(), frame, ended, error
                )) {
                return finish(
                    L"Host reaction frame failed: " +
                    std::wstring(error.begin(), error.end())
                );
            }
            // MCIWndSeek clamps a request past the last AVI frame. Preserve
            // that appearance by holding the previous bitmap while the MMS
            // trajectory continues (X24 is the observed case).
            if (!ended) {
                UploadHostFrame(frame);
                if (host_bitmap_ == nullptr) {
                    return finish(
                        L"Host reaction failed: could not upload a video frame."
                    );
                }
            }
            host_bitmap_x_ = host_motion_player_.X();
            host_bitmap_y_ = host_motion_player_.Y();
            const auto next_delay =
                host_motion_player_.TickDelayMilliseconds(5);
            if (host_motion_player_.UsesMode4TimedSynchronization() &&
                !host_motion_sync_anchor_) {
                host_motion_sync_anchor_ = now;
            } else if (!host_motion_player_.UsesMode4TimedSynchronization()) {
                host_motion_sync_anchor_.reset();
            }
            host_next_frame_due_ = now +
                std::chrono::milliseconds(next_delay);
            InvalidateRect(window, nullptr, FALSE);
            return true;
        }

        return true;
    }

    bool FinishHostInterstitial(HWND window, std::wstring failure) {
        const auto resume_charge = pending_host_scene_load_charge_;
        const bool finishing_weight_entry =
            resume_charge.has_value() && scene_ == kWeightScene;
        const bool finishing_order_entry =
            resume_charge.has_value() && scene_ == kCommentsScene;
        const bool finishing_cryo_entry =
            resume_charge.has_value() && scene_ == kCryoScene;
        const auto judging_advance = pending_host_judging_advance_;
        const auto pavilion_close = pending_host_pavilion_close_;
        const bool resume_cryo_ambient = pending_host_resume_cryo_ambient_;
        const bool play_ouch = host_ouch_pending_ && !host_motion_active_;
        pending_host_scene_load_charge_.reset();
        pending_host_judging_advance_.reset();
        pending_host_pavilion_close_.reset();
        pending_host_resume_cryo_ambient_ = false;
        pending_judging_entry_continuation_.reset();
        ClearHostInterstitialVisual(window);
        if (play_ouch) {
            const auto ouch = SceneAssetsRoot() / L"WAV" / L"HOST" /
                L"OUCH.WAV";
            PlaySoundW(
                ouch.c_str(), nullptr,
                SND_FILENAME | SND_SYNC | SND_NODEFAULT
            );
        }
        if (resume_charge) {
            if (scene_ == kWeightScene) {
                FinishWeightHostEntry(window);
            } else if (scene_ == kCommentsScene) {
                FinishOrderHostEntry(window);
            } else if (scene_ == kCryoScene) {
                FinishCryoHostEntry(window);
            } else if (const auto category = JudgingCategoryForScene(scene_)) {
                FinishJudgingHostEntry(*category, window);
            } else {
                LoadScene(*resume_charge, false);
            }
        } else if (judging_advance) {
            if (judging_advance->disposition != JudgingDisposition::rated) {
                // The mode-6 C61/C71 dispatcher restores the ambient loop
                // before returning to its outer result handler. That handler
                // starts it again after this continuation advances judging.
                const auto ambient = PavilionAmbientSound(
                    judging_advance->category
                );
                PlaySoundW(
                    ambient.c_str(), nullptr,
                    SND_FILENAME | SND_ASYNC | SND_LOOP
                );
                if (judging_advance->disposition ==
                    JudgingDisposition::disqualified) {
                    InvalidateRect(window, nullptr, FALSE);
                    UpdateWindow(window);
                }
            }
            if (judging_advance->complete_accepted_contestant) {
                CompleteAcceptedContestant(
                    judging_advance->category, window
                );
            }
            AdvanceJudging(
                judging_advance->category,
                window,
                judging_advance->disposition
            );
        } else if (pavilion_close) {
            // C81 uses the same Brains dispatcher as C31/C61/C71. Its
            // unconditional BRAINS.WAV tail precedes the outer close helper,
            // which immediately stops that newly restored loop.
            const auto ambient = PavilionAmbientSound(*pavilion_close);
            PlaySoundW(
                ambient.c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_LOOP
            );
            CloseJudgingPavilion(*pavilion_close, window);
        } else if (resume_cryo_ambient) {
            const auto ambient = assets_ / L"WAV" / L"CRYO.WAV";
            PlaySoundW(
                ambient.c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_LOOP
            );
        } else if (failure.empty()) {
            status_.clear();
        }
        if (!failure.empty()) {
            status_ = std::move(failure);
        }
        UpdateTitle(window);
        if (!finishing_weight_entry && !finishing_order_entry &&
            !finishing_cryo_entry) {
            InvalidateRect(window, nullptr, FALSE);
        }
        return true;
    }

    [[nodiscard]] bool CenteredOneShotStartPending(
        CenteredOneShotKind kind
    ) const {
        return pending_centered_one_shot_start_ &&
               pending_centered_one_shot_start_->kind == kind;
    }

    void ResetStandalonePlaybackState() {
        ResetBitmap();
        video_decoder_.Close();
        audio_player_.Stop();
        navigation_media_audio_.reset();
        video_looping_ = false;
        video_paused_ = false;
        playback_range_.reset();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;
    }

    void ScheduleCenteredOneShotStart(
        CenteredOneShotKind kind,
        const std::filesystem::path& root,
        const std::filesystem::path& relative
    ) {
        // Resource 158 creates a blank centered modal immediately, but its
        // timer callback does not construct/start the MCI child for 100 ms.
        // Clear the covered scene now so winner/credit do not leak their
        // underlying navigation frame during that interval.
        ResetStandalonePlaybackState();
        centered_one_shot_child_created_ = false;
        pending_centered_one_shot_start_ = PendingCenteredOneShotStart{
            kind,
            root,
            relative,
            std::chrono::steady_clock::now() +
                metaverse::kCenteredOneShotPlaybackDelay,
        };
    }

    bool TickPendingCenteredOneShotStart(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (!pending_centered_one_shot_start_ ||
            now < pending_centered_one_shot_start_->due) {
            return false;
        }

        PendingCenteredOneShotStart pending =
            std::move(*pending_centered_one_shot_start_);
        pending_centered_one_shot_start_.reset();
        std::string error;
        if (PlayStandaloneVideo(pending.root, pending.relative, error)) {
            centered_one_shot_child_created_ = true;
            InvalidateRect(window, nullptr, FALSE);
            return false;
        }

        // MCIWNDF_NOERRORDLG suppresses any original error window. The shared
        // class marks the one-shot active after its play request regardless,
        // so retain the owning intro/winner/credit state and the blank surface.
        // No child HWND exists to receive WM_LBUTTONDOWN in this branch;
        // Enter/OK on the owner still follows the normal completion path.
        static_assert(metaverse::CenteredOneShotFailureWaitsForOk());
        centered_one_shot_child_created_ = false;
        status_.clear();
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
        return false;
    }

    bool PlayStandaloneVideo(
        const std::filesystem::path& root,
        const std::filesystem::path& relative,
        std::string& error
    ) {
        ResetStandalonePlaybackState();
        const auto path = root / relative;
        if (!video_decoder_.Open(path, error)) {
            return false;
        }
        metaverse::AudioTrack audio;
        bool has_audio = false;
        if (!metaverse::DecodeAudioTrack(path, audio, has_audio, error)) {
            video_decoder_.Close();
            return false;
        }
        if (has_audio) {
            if (!audio_player_.Play(audio, false, error)) {
                video_decoder_.Close();
                return false;
            }
            video_audio_clocked_ = true;
            video_audio_sample_rate_ = static_cast<std::uint32_t>(
                audio.sample_rate
            );
            video_audio_clock_start_frame_ = 0;
        }
        next_frame_due_ = std::chrono::steady_clock::now();
        error.clear();
        return true;
    }

    void ResetMotionSprite() {
        sprite_audio_player_.Stop();
        if (sprite_bitmap_ != nullptr) {
            DeleteObject(sprite_bitmap_);
            sprite_bitmap_ = nullptr;
        }
        sprite_decoder_.Close();
        motion_player_ = {};
        sprite_width_ = 0;
        sprite_height_ = 0;
        sprite_number_ = -1;
        last_sprite_sound_entry_ = 0;
        simm_motion_pacing_factor_ = 0;
        simm_pending_award_.reset();
    }

    bool MotionSpriteHitTest(int logical_x, int logical_y) const {
        const auto bounds = metaverse::MotionSpriteBoundsFromTopLeft(
            motion_player_.X(), motion_player_.Y(),
            sprite_width_, sprite_height_
        );
        if (sprite_bitmap_ == nullptr || sprite_width_ <= 0 ||
            sprite_height_ <= 0 ||
            !metaverse::MotionSpritePointInsideLegacyHitBounds(
                bounds, logical_x, logical_y
            )) {
            return false;
        }
        HDC source = CreateCompatibleDC(nullptr);
        if (source == nullptr) {
            return false;
        }
        HGDIOBJ previous = SelectObject(source, sprite_bitmap_);
        const COLORREF key = GetPixel(source, 0, 0);
        const COLORREF pixel = GetPixel(
            source,
            logical_x - bounds.left,
            logical_y - bounds.top
        );
        SelectObject(source, previous);
        DeleteDC(source);
        return pixel != CLR_INVALID && key != CLR_INVALID && pixel != key;
    }

    void FinishSimmEncounterReturn(
        std::chrono::steady_clock::time_point now
    ) {
        ResetMotionSprite();
        // The common caller tail at 0x0040b3bd runs after every generic-mode
        // return, not only its successful terminal: it refreshes the idle
        // timestamp, reactivates the mix session, and starts ZPR on channel 2.
        PlayNavigationEffect(L"ZPR.WAV", navigation_departure_audio_);
        navigation_idle_due_ = now + std::chrono::milliseconds(
            metaverse::kNavigationIdleAnimationDelayMs
        );
    }

    void UploadSpriteFrame(const metaverse::VideoFrame& frame) {
        if (sprite_bitmap_ != nullptr) {
            DeleteObject(sprite_bitmap_);
            sprite_bitmap_ = nullptr;
        }
        sprite_width_ = 0;
        sprite_height_ = 0;

        BITMAPINFO information = {};
        information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        information.bmiHeader.biWidth = frame.width;
        information.bmiHeader.biHeight = -frame.height;
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HDC screen = GetDC(nullptr);
        sprite_bitmap_ = CreateDIBSection(
            screen, &information, DIB_RGB_COLORS, &pixels, nullptr, 0
        );
        ReleaseDC(nullptr, screen);
        if (sprite_bitmap_ == nullptr || pixels == nullptr) {
            if (sprite_bitmap_ != nullptr) {
                DeleteObject(sprite_bitmap_);
                sprite_bitmap_ = nullptr;
            }
            status_ = L"Could not create the Simm sprite bitmap.";
            return;
        }
        const std::size_t destination_stride =
            static_cast<std::size_t>(frame.width) * 4;
        const std::size_t source_stride = static_cast<std::size_t>(frame.stride);
        for (int row = 0; row < frame.height; ++row) {
            std::memcpy(
                static_cast<std::uint8_t*>(pixels) +
                    static_cast<std::size_t>(row) * destination_stride,
                frame.bgra.data() + static_cast<std::size_t>(row) * source_stride,
                std::min(destination_stride, source_stride)
            );
        }
        sprite_width_ = frame.width;
        sprite_height_ = frame.height;
    }

    void PlaySpriteSound() {
        if (!motion_player_.Active() || sprite_number_ < 0) {
            return;
        }
        const std::uint64_t entry = motion_player_.RecordEntrySerial();
        if (entry == last_sprite_sound_entry_) {
            return;
        }
        last_sprite_sound_entry_ = entry;
        // The generic loader starts an initial 0x0040 voice, and the timer
        // transition path can start another.  The mouse-hit handler only
        // stops the prior 0x0040 voice before following its link; it does not
        // start the target record's voice flags.
        if (!motion_player_.LastEntryWasHit() &&
            motion_player_.PlaysStateSound()) {
            const std::int16_t sound = motion_player_.SoundIndex();
            if (sound >= 0) {
                const auto path = SceneAssetsRoot() / L"WAV" / L"NAV" /
                    (L"T" + std::to_wstring(sprite_number_) + L"S" +
                     std::to_wstring(sound) + L".WAV");
                if (std::filesystem::exists(path)) {
                    metaverse::AudioTrack voice;
                    bool has_audio = false;
                    std::string error;
                    if (metaverse::DecodeAudioTrack(
                            path, voice, has_audio, error
                        ) && has_audio) {
                        sprite_audio_player_.Play(voice, false, error);
                    }
                }
            }
        }
        // Timed-transition fcn.0040d2ab starts a 0x0040 cue before applying
        // 0x0020. Preserve that order if an authored record combines them.
        if (!motion_player_.LastEntryWasHit() &&
            motion_player_.StopsStateSound()) {
            sprite_audio_player_.Stop();
        }
    }

    bool LoadCrossroadsSprite(std::string& error) {
        ResetMotionSprite();
        simm_motion_pacing_factor_ =
            metaverse::SimmMotionPacingFactor(round_);
        const std::size_t available_count =
            metaverse::RearmAndCountEligibleSimms(simm_selection_table_);
        if (available_count == 0) {
            error = "the original Simm selection table has no eligible scripts";
            return false;
        }
        sprite_number_ = metaverse::ConsumeEligibleSimm(
            simm_selection_table_, random_.Scale(available_count)
        );
        if (sprite_number_ < 0) {
            error = "could not consume the selected Simm script";
            return false;
        }

        // fcn.0040c3ed selected D# above with the pre-loader stream.  The
        // generic media loader then reseeds at 0x0040ddc0 before it resolves
        // and opens D#/T# media, replacing the global stream for later draws.
        random_.Seed(timeGetTime());

        metaverse::MotionScript script;
        const auto script_path = SceneAssetsRoot() / L"DAT" / L"NAV" /
            (L"D" + std::to_wstring(sprite_number_) + L".MMS");
        if (!metaverse::LoadMotionScript(script_path, script, error) ||
            !motion_player_.Reset(script, error, 3)) {
            ResetMotionSprite();
            return false;
        }

        const auto video_path = SceneAssetsRoot() / L"MOV" / L"NAV" /
            (L"T" + std::to_wstring(sprite_number_) + L".AVI");
        if (!sprite_decoder_.Open(video_path, error)) {
            ResetMotionSprite();
            return false;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!sprite_decoder_.SeekFrame(
                motion_player_.Frame(), frame, ended, error
            ) || ended) {
            if (error.empty()) {
                error = "Simm sprite video ended before its initial frame";
            }
            ResetMotionSprite();
            return false;
        }
        UploadSpriteFrame(frame);
        if (sprite_bitmap_ == nullptr) {
            error = "could not upload the initial Simm sprite frame";
            ResetMotionSprite();
            return false;
        }
        last_sprite_sound_entry_ = 0;
        PlaySpriteSound();
        next_sprite_due_ = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(
                motion_player_.TickDelayMilliseconds(
                    simm_motion_pacing_factor_
                )
            );
        error.clear();
        return true;
    }

    void TickNavigationEncounter(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (!pending_navigation_encounter_node_ ||
            !metaverse::NavigationTimerThresholdPassed(
                now, navigation_encounter_due_
            ) || motion_player_.Active()) {
            return;
        }
        const std::int32_t expected = *pending_navigation_encounter_node_;
        pending_navigation_encounter_node_.reset();
        if (!navigation_hub_active_ || !navigator_) {
            return;
        }
        const metaverse::SceneObject* object = navigator_->CurrentObject();
        if (object == nullptr || object->index != expected ||
            (object->flags & 0x0080) == 0 || expected < 0 ||
            expected >= static_cast<std::int32_t>(
                navigation_encountered_nodes_.size()
            )) {
            return;
        }
        navigation_encountered_nodes_[static_cast<std::size_t>(expected)] = true;
        std::string error;
        if (LoadCrossroadsSprite(error)) {
            status_ = L"A wandering Simm appeared at this navigation node; click it to tag.";
        } else {
            // The original common post-run tail executes even when generic
            // media cannot construct its object: rearm navigation audio and
            // timestamp after reporting the loader failure.
            FinishSimmEncounterReturn(now);
            status_ = L"Simm encounter failed: " +
                      std::wstring(error.begin(), error.end());
        }
        UpdateTitle(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void TickMotionSprite(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (!navigation_hub_active_ || !motion_player_.Active() ||
            now < next_sprite_due_) {
            return;
        }

        std::string error;
        if (!motion_player_.Tick(error)) {
            if (!error.empty()) {
                status_ = L"Simm motion failed: " +
                          std::wstring(error.begin(), error.end());
                FinishSimmEncounterReturn(now);
                UpdateTitle(window);
            }
            return;
        }

        if (motion_player_.Complete()) {
            const bool caught = metaverse::SimmResultEarnsAward(
                motion_player_.ResultCode()
            );
            const float award = caught
                ? simm_pending_award_.value_or(
                      metaverse::SimmCreditAward(
                          sprite_number_, simm_motion_pacing_factor_
                      )
                  )
                : 0.0f;
            if (caught) {
                round_.credits += award;
            }
            wchar_t formatted_award[40] = {};
            swprintf_s(
                formatted_award, L"Tagged a Simm: +$%.2f.",
                static_cast<double>(award)
            );
            const std::wstring completion = caught
                ? formatted_award
                : L"That Simm got away.";
            FinishSimmEncounterReturn(now);
            status_ = completion;
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }

        PlaySpriteSound();
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!sprite_decoder_.SeekFrame(
                motion_player_.Frame(), frame, ended, error
            )) {
            status_ = L"Simm frame failed: " +
                      std::wstring(error.begin(), error.end());
            FinishSimmEncounterReturn(now);
            UpdateTitle(window);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        if (!ended) {
            UploadSpriteFrame(frame);
        }
        next_sprite_due_ = now + std::chrono::milliseconds(
            motion_player_.TickDelayMilliseconds(
                simm_motion_pacing_factor_
            )
        );
        InvalidateRect(window, nullptr, FALSE);
    }

    void ResetBitmap() {
        if (bitmap_ != nullptr) {
            DeleteObject(bitmap_);
            bitmap_ = nullptr;
        }
        bitmap_width_ = 0;
        bitmap_height_ = 0;
    }

    void ResetBackgroundBitmap() {
        if (background_bitmap_ != nullptr) {
            DeleteObject(background_bitmap_);
            background_bitmap_ = nullptr;
        }
        background_width_ = 0;
        background_height_ = 0;
    }

    void ResetLooksBitmap() {
        if (looks_bitmap_ != nullptr) {
            DeleteObject(looks_bitmap_);
            looks_bitmap_ = nullptr;
        }
        looks_width_ = 0;
        looks_height_ = 0;
        looks_magnifier_visible_ = false;
    }

    void ApplyLooksPresentationAction(
        metaverse::LooksPresentationAction action
    ) {
        const auto plan = metaverse::PlanLooksPresentation(
            action,
            looks_rotate_child_constructed_,
            looks_bitmap_ != nullptr
        );
        looks_rotate_visible_ = plan.rotate_visible;
        looks_magnifier_visible_ = plan.magnifier_visible;
    }

    void ResetLooksAssets() {
        ResetLooksBitmap();
        ReleaseBitmap(looks_rotate_bitmap_);
        looks_rotate_decoder_.Close();
        looks_rotate_width_ = 0;
        looks_rotate_height_ = 0;
        looks_rotate_last_frame_ = 0;
        looks_rotate_playing_ = false;
        looks_rotate_visible_ = false;
        looks_rotate_child_constructed_ = false;
        looks_front_view_ = true;
    }

    void RepaintLooksPresentationRect(HWND window) const {
        RECT client = {};
        GetClientRect(window, &client);
        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        if (client_width <= 0 || client_height <= 0) {
            return;
        }
        const double scale = std::min(
            static_cast<double>(client_width) / 640.0,
            static_cast<double>(client_height) / 480.0
        );
        const int drawn_width = static_cast<int>(std::lround(640.0 * scale));
        const int drawn_height = static_cast<int>(std::lround(480.0 * scale));
        const int content_x = (client_width - drawn_width) / 2;
        const int content_y = (client_height - drawn_height) / 2;
        RECT dirty{
            content_x + static_cast<LONG>(std::lround(
                metaverse::kLooksPresentationLeft * scale
            )),
            content_y + static_cast<LONG>(std::lround(
                metaverse::kLooksPresentationTop * scale
            )),
            content_x + static_cast<LONG>(std::lround(
                metaverse::kLooksPresentationRight * scale
            )),
            content_y + static_cast<LONG>(std::lround(
                metaverse::kLooksPresentationBottom * scale
            )),
        };
        InvalidateRect(window, &dirty, FALSE);
        UpdateWindow(window);
    }

    void LoadLooksContestantFrame() {
        // 0x00412944 constructs ROTATE at frame zero first; 0x004140bf then
        // shows and seeks that same child to the selected contestant.
        LoadLooksRotateFrame(0, false);
        SeekLooksContestantFrame();
    }

    void SeekLooksContestantFrame() {
        const std::int32_t contestant_id = round_.contestants[
            judging_contestant_
        ].contestant_id;
        looks_current_contestant_id_ = contestant_id;
        looks_rotate_playing_ = false;
        looks_front_view_ = true;
        // ShowWindow precedes the unchecked seek at 0x00414139. A failed seek
        // therefore leaves the retained child visible over any MAG bitmap.
        ApplyLooksPresentationAction(
            metaverse::LooksPresentationAction::show_rotate
        );
        if (!looks_rotate_decoder_.IsOpen()) {
            return;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        std::string error;
        if (!looks_rotate_decoder_.SeekFrame(
                metaverse::LooksInitialFrame(contestant_id),
                frame,
                ended,
                error
            ) || ended) {
            // The original ShowWindow precedes its unchecked MCI seek. Keep
            // the previous retained surface visible on a seek error.
            status_ += L" Looks contestant frame seek failed: " +
                std::wstring(error.begin(), error.end()) + L".";
            return;
        }
        UploadCryoFrame(
            frame,
            looks_rotate_bitmap_,
            looks_rotate_width_,
            looks_rotate_height_
        );
    }

    void LoadLooksRotateFrame(std::int64_t initial_frame, bool visible) {
        ReleaseBitmap(looks_rotate_bitmap_);
        looks_rotate_decoder_.Close();
        looks_rotate_playing_ = false;
        looks_rotate_child_constructed_ = true;
        ApplyLooksPresentationAction(
            visible
                ? metaverse::LooksPresentationAction::show_rotate
                : metaverse::LooksPresentationAction::show_background
        );
        looks_front_view_ = true;

        std::string error;
        if (!looks_rotate_decoder_.Open(
                assets_ / L"MOV" / L"LOOK" / L"ROTATE.AVI", error
            )) {
            status_ += L" Looks rotation movie failed: " +
                std::wstring(error.begin(), error.end()) + L".";
            return;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!looks_rotate_decoder_.SeekFrame(
                initial_frame,
                frame,
                ended,
                error
            ) || ended) {
            status_ += L" Looks contestant frame seek failed: " +
                std::wstring(error.begin(), error.end()) + L".";
            looks_rotate_decoder_.Close();
            return;
        }
        UploadCryoFrame(
            frame,
            looks_rotate_bitmap_,
            looks_rotate_width_,
            looks_rotate_height_
        );
        ApplyLooksPresentationAction(
            visible
                ? metaverse::LooksPresentationAction::show_rotate
                : metaverse::LooksPresentationAction::show_background
        );
    }

    bool LoadLooksMagnification(std::int32_t region, HWND window) {
        if (region < 1 || region > 4) {
            return false;
        }
        const std::int32_t contestant_id = looks_current_contestant_id_;
        std::int32_t& level = looks_magnification_levels_[
            static_cast<std::size_t>(region - 1)
        ];
        const bool judge_cadet = looks_current_contestant_id_ >= 1 &&
            looks_current_contestant_id_ <= 10 &&
            metaverse::IsJudgeCadetPresentation(
                round_, judging_contestant_, metaverse::JudgingCategory::looks
            );
        auto filename = metaverse::LooksMagnifierFilename(
            contestant_id, region, level, judge_cadet
        );
        const auto directory = assets_ / L"BMP" / L"LOOK" / L"MAG";
        auto loaded = static_cast<HBITMAP>(LoadImageW(
            nullptr, (directory / filename).c_str(), IMAGE_BITMAP, 0, 0,
            LR_LOADFROMFILE | LR_CREATEDIBSECTION
        ));
        if (loaded == nullptr &&
            !(judge_cadet && region ==
                metaverse::LooksJudgeCadetSpecialRegion(contestant_id))) {
            // 0x0041426c wraps only after the attempted level is absent.
            level = 1;
            filename = metaverse::LooksMagnifierFilename(
                contestant_id, region, level, false
            );
            loaded = static_cast<HBITMAP>(LoadImageW(
                nullptr, (directory / filename).c_str(), IMAGE_BITMAP, 0, 0,
                LR_LOADFROMFILE | LR_CREATEDIBSECTION
            ));
        }
        bool load_succeeded = false;
        BITMAP details = {};
        if (loaded != nullptr &&
            GetObjectW(loaded, sizeof(details), &details)) {
            load_succeeded = true;
        } else {
            ReleaseBitmap(loaded);
        }

        // 0x0041429e-0x0041433a releases the previous DIB before the final
        // load and performs the rest of the state transition even when that
        // load fails: warn, hide ROTATE, synchronously repaint only +0x108,
        // and increment the region counter. A failed image therefore leaves
        // the presentation area blank instead of restoring the movie.
        ResetLooksBitmap();
        looks_bitmap_ = loaded;
        if (load_succeeded) {
            looks_width_ = details.bmWidth;
            looks_height_ = details.bmHeight;
        } else {
            ShowLegacyBitmapReadFailure();
            status_ = L"Looks magnifier asset is missing: " + filename;
        }
        ApplyLooksPresentationAction(
            metaverse::LooksPresentationAction::show_magnifier
        );
        ++level;
        if (load_succeeded) {
            status_ = judge_cadet && region ==
                    metaverse::LooksJudgeCadetSpecialRegion(contestant_id)
                ? L"The Looks magnifier revealed the judge-cadet image."
                : L"Looks magnifier region " + std::to_wstring(region) +
                    L" loaded (" + filename + L").";
        }
        UpdateTitle(window);
        RepaintLooksPresentationRect(window);
        return load_succeeded;
    }

    bool StartLooksRotation() {
        if (looks_rotate_playing_) {
            return false;
        }
        const std::int32_t contestant_id = looks_current_contestant_id_;
        const auto range = metaverse::LooksRotationFrames(
            contestant_id, looks_front_view_
        );
        // fcn.0041402a clears both stable flags, issues its MCI Wait command,
        // then installs the destination flag and toggles +0x74 without ever
        // checking the command result. Preserve that state transition even
        // when the retained native decoder has no open child or cannot seek.
        looks_front_view_ = range.front_after;
        ApplyLooksPresentationAction(
            metaverse::LooksPresentationAction::show_rotate
        );
        if (!looks_rotate_decoder_.IsOpen()) {
            looks_rotate_playing_ = false;
            return true;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        std::string error;
        if (!looks_rotate_decoder_.SeekFrame(
                range.first_frame, frame, ended, error
            ) || ended) {
            status_ = L"Looks rotation seek failed: " +
                      std::wstring(error.begin(), error.end());
            looks_rotate_playing_ = false;
            return true;
        }
        UploadCryoFrame(
            frame,
            looks_rotate_bitmap_,
            looks_rotate_width_,
            looks_rotate_height_
        );
        looks_rotate_last_frame_ = range.last_frame;
        looks_rotate_playing_ = range.first_frame < range.last_frame;
        looks_rotate_due_ = std::chrono::steady_clock::now();
        return true;
    }

    void TickLooks(
        HWND window,
        std::chrono::steady_clock::time_point now
    ) {
        if (scene_ != kLooksJudgingScene || !looks_rotate_playing_ ||
            now < looks_rotate_due_) {
            return;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        std::string error;
        if (!looks_rotate_decoder_.DecodeNext(frame, ended, error) || ended ||
            frame.frame_index > looks_rotate_last_frame_) {
            looks_rotate_playing_ = false;
            if (!error.empty()) {
                status_ = L"Looks rotation decode failed: " +
                          std::wstring(error.begin(), error.end());
                UpdateTitle(window);
            }
            return;
        }
        UploadCryoFrame(
            frame,
            looks_rotate_bitmap_,
            looks_rotate_width_,
            looks_rotate_height_
        );
        if (frame.frame_index >= looks_rotate_last_frame_) {
            looks_rotate_playing_ = false;
        }
        looks_rotate_due_ = now +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(
                    std::clamp(frame.duration_seconds, 0.01, 0.25)
                )
            );
        InvalidateRect(window, nullptr, FALSE);
    }

    void ResetJudgingBitmaps() {
        if (judging_rating_bitmap_ != nullptr) {
            DeleteObject(judging_rating_bitmap_);
            judging_rating_bitmap_ = nullptr;
        }
        judging_rating_width_ = 0;
        judging_rating_height_ = 0;
        looks_rating_visible_ = false;
        suppress_judging_rating_bitmap_once_ = false;
        for (HBITMAP& portrait : judging_portrait_bitmaps_) {
            if (portrait != nullptr) {
                DeleteObject(portrait);
                portrait = nullptr;
            }
        }
        for (HBITMAP& action : judging_action_bitmaps_) {
            ReleaseBitmap(action);
        }
        judging_pressed_action_ = JudgingAction::none;
        judging_pressed_visual_ = JudgingAction::none;
        judging_hovered_portrait_.reset();
        judging_comment_previewed_ = false;
    }

    void LoadJudgingActionBitmaps(metaverse::JudgingCategory category) {
        const wchar_t* directory = category == metaverse::JudgingCategory::brains
            ? L"BRAIN"
            : category == metaverse::JudgingCategory::looks ? L"LOOK" : L"TALENT";
        constexpr std::array<const wchar_t*, 4> filenames{{
            L"ACCEPT.BMP", L"GONG.BMP", L"PENALTY.BMP", L"ROTATE.BMP",
        }};
        for (std::size_t index = 0; index < filenames.size(); ++index) {
            ReleaseBitmap(judging_action_bitmaps_[index]);
            const JudgingAction action = static_cast<JudgingAction>(index + 1);
            if (!JudgingActionRect(category, action)) {
                continue;
            }
            const auto path = JudgingAssetsRoot(category) / L"BMP" / directory /
                filenames[index];
            judging_action_bitmaps_[index] =
                LoadBitmapWithLegacyWarning(path);
        }
    }

    void LoadLooksConstructionHiddenBitmaps() {
        // fcn.00412d8b creates these owners after ACCEPT/PENALTY/ROTATE and
        // before the five portraits. Neither +0x54 nor +0x48 is set, so both
        // images remain hidden until a later state transition.
        ResetLooksBitmap();
        const auto magnifier = assets_ / L"BMP" / L"LOOK" / L"MAG" /
            L"111.BMP";
        looks_bitmap_ = LoadBitmapWithLegacyWarning(magnifier);
        BITMAP details = {};
        if (looks_bitmap_ != nullptr &&
            GetObjectW(looks_bitmap_, sizeof(details), &details)) {
            looks_width_ = details.bmWidth;
            looks_height_ = details.bmHeight;
        }
        looks_magnifier_visible_ = false;

        if (judging_rating_bitmap_ != nullptr) {
            DeleteObject(judging_rating_bitmap_);
            judging_rating_bitmap_ = nullptr;
        }
        judging_rating_width_ = 0;
        judging_rating_height_ = 0;
        const auto rating_plan = metaverse::PlanLooksRatingSurface(
            metaverse::LooksRatingSurfaceAction::construction,
            current_rating_,
            looks_rating_visible_
        );
        const auto initial_rating = rating_plan.replacement_rating.value_or(1);
        judging_rating_bitmap_ = LoadBitmapWithLegacyWarning(
            assets_ / L"BMP" / L"LOOK" /
                (std::to_wstring(initial_rating) + L".BMP")
        );
        details = {};
        if (judging_rating_bitmap_ != nullptr &&
            GetObjectW(judging_rating_bitmap_, sizeof(details), &details)) {
            judging_rating_width_ = details.bmWidth;
            judging_rating_height_ = details.bmHeight;
        }
        looks_rating_visible_ = rating_plan.visible;
    }

    void LoadJudgingRatingBitmap(
        metaverse::JudgingCategory category,
        bool dynamic_wide_selection = false
    ) {
        std::int32_t requested_rating = current_rating_;
        if (category == metaverse::JudgingCategory::looks) {
            const auto action = suppress_judging_rating_bitmap_once_
                ? metaverse::LooksRatingSurfaceAction::manual_contestant_switch
                : current_rating_ > 0
                    ? metaverse::LooksRatingSurfaceAction::select_value
                    : metaverse::LooksRatingSurfaceAction::correct_penalty;
            suppress_judging_rating_bitmap_once_ = false;
            const auto plan = metaverse::PlanLooksRatingSurface(
                action, current_rating_, looks_rating_visible_
            );
            looks_rating_visible_ = plan.visible;
            if (!plan.replacement_rating) {
                // Zero, a manual portrait switch, and correct Penalty repaint
                // the LOOK backing while retaining the last +0x184 wrapper.
                return;
            }
            requested_rating = *plan.replacement_rating;
        }
        if (judging_rating_bitmap_ != nullptr) {
            DeleteObject(judging_rating_bitmap_);
            judging_rating_bitmap_ = nullptr;
        }
        judging_rating_width_ = 0;
        judging_rating_height_ = 0;
        const wchar_t* directory = category == metaverse::JudgingCategory::brains
            ? L"BRAIN"
            : category == metaverse::JudgingCategory::looks ? L"LOOK" : L"TALENT";
        const auto path = JudgingAssetsRoot(category) / L"BMP" / directory /
            (std::to_wstring(requested_rating) + L".BMP");
        std::wstring warning_filename;
        if (category == metaverse::JudgingCategory::brains &&
            dynamic_wide_selection) {
            // fcn.00409187's dynamic Brains meter is the one bitmap path that
            // formats the attempted relative name into the legacy warning,
            // including a chooser-preview click that selects rating zero.
            warning_filename = L"bmp\\Brain\\" +
                std::to_wstring(requested_rating) + L".bmp";
        }
        judging_rating_bitmap_ = LoadBitmapWithLegacyWarning(
            path, warning_filename
        );
        BITMAP details = {};
        if (judging_rating_bitmap_ != nullptr &&
            GetObjectW(judging_rating_bitmap_, sizeof(details), &details)) {
            judging_rating_width_ = details.bmWidth;
            judging_rating_height_ = details.bmHeight;
        }
    }

    void LoadJudgingPortraits(metaverse::JudgingCategory category) {
        for (std::size_t slot = 0; slot < judging_portrait_bitmaps_.size(); ++slot) {
            LoadJudgingPortrait(category, slot);
        }
    }

    void LoadJudgingPortrait(
        metaverse::JudgingCategory category,
        std::size_t slot
    ) {
        if (slot >= judging_portrait_bitmaps_.size()) {
            return;
        }
        if (judging_portrait_bitmaps_[slot] != nullptr) {
            DeleteObject(judging_portrait_bitmaps_[slot]);
            judging_portrait_bitmaps_[slot] = nullptr;
        }
        const std::size_t category_index = static_cast<std::size_t>(category);
        const auto& contestant = round_.contestants[slot];
        std::filesystem::path relative;
        if (contestant.disqualified) {
            relative = std::filesystem::path(L"BMP\\PENALTY") /
                (std::to_wstring(contestant.contestant_id) + L".BMP");
        } else if (!judging_visit_.pending[slot] &&
                   gonged_[category_index][slot]) {
            relative = std::filesystem::path(L"BMP\\GONG") /
                (std::to_wstring(contestant.contestant_id) + L".BMP");
        } else if (!judging_visit_.pending[slot]) {
            relative = std::filesystem::path(L"BMP\\DIMMED") /
                (std::to_wstring(contestant.contestant_id) + L".BMP");
        } else {
            relative = std::filesystem::path(L"BMP\\CRYO") /
                (L"GIRL" + std::to_wstring(contestant.contestant_id) + L".BMP");
        }
        ReplaceJudgingPortraitBitmap(category, slot, relative);
    }

    void ReplaceJudgingPortraitBitmap(
        metaverse::JudgingCategory category,
        std::size_t slot,
        const std::filesystem::path& relative
    ) {
        if (slot >= judging_portrait_bitmaps_.size()) {
            return;
        }
        if (judging_portrait_bitmaps_[slot] != nullptr) {
            DeleteObject(judging_portrait_bitmaps_[slot]);
            judging_portrait_bitmaps_[slot] = nullptr;
        }
        judging_portrait_bitmaps_[slot] = LoadBitmapWithLegacyWarning(
            JudgingAssetsRoot(category) / relative
        );
    }

    void LoadJudgingBackground(
        const std::filesystem::path& root,
        const wchar_t* relative
    ) {
        ResetBackgroundBitmap();
        if (relative == nullptr) {
            return;
        }
        background_bitmap_ = LoadBitmapWithLegacyWarning(root / relative);
        BITMAP details = {};
        if (background_bitmap_ != nullptr &&
            GetObjectW(background_bitmap_, sizeof(details), &details)) {
            background_width_ = details.bmWidth;
            background_height_ = details.bmHeight;
        } else {
            ResetBackgroundBitmap();
        }
    }

    void UploadVideoFrame(const metaverse::VideoFrame& frame) {
        ResetBitmap();
        BITMAPINFO information = {};
        information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        information.bmiHeader.biWidth = frame.width;
        information.bmiHeader.biHeight = -frame.height;
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HDC screen = GetDC(nullptr);
        bitmap_ = CreateDIBSection(
            screen, &information, DIB_RGB_COLORS, &pixels, nullptr, 0
        );
        ReleaseDC(nullptr, screen);
        if (bitmap_ == nullptr || pixels == nullptr) {
            ResetBitmap();
            status_ = L"Could not create the video frame bitmap.";
            return;
        }
        std::memcpy(
            pixels,
            frame.bgra.data(),
            static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(frame.height)
        );
        bitmap_width_ = frame.width;
        bitmap_height_ = frame.height;
    }

    void LoadScene(
        bool charge_performance = true,
        bool play_host_interstitial = true,
        bool stop_legacy_sound = true
    ) {
        const bool replay_access = performance_access_granted_;
        ResetHostInterstitial();
        // sndPlaySound(NULL, 1) is issued when a pavilion contestant is
        // activated (0x004073a4 / 0x004095de). Stop any chooser/scene ambience
        // before loading the next screen or performance.
        if (stop_legacy_sound) {
            PlaySoundW(nullptr, nullptr, 0);
        }
        pending_host_entry_cue_.reset();
        pending_host_scene_load_charge_.reset();
        pending_host_judging_advance_.reset();
        pending_host_pavilion_close_.reset();
        pending_host_resume_cryo_ambient_ = false;
        pending_navigation_encounter_node_.reset();
        ResetMotionSprite();
        if (scene_ != kCryoScene) {
            // Cryo's original modal teardown precedes every outer-flow side
            // effect. Keep that ownership order even on a defensive scene
            // exit; the ordinary fifth-sponsorship path already performed
            // this exact reset before generating Pavilion variants.
            ResetCryoAssets();
        } else {
            ResetBitmap();
        }
        ResetBackgroundBitmap();
        ResetLooksAssets();
        ResetJudgingBitmaps();
        ResetNavigationOverlay();
        ResetWeightBitmaps();
        video_decoder_.Close();
        audio_player_.Stop();
        navigation_effect_player_.Stop();
        video_looping_ = false;
        video_paused_ = false;
        playback_range_.reset();
        video_audio_clocked_ = false;
        video_audio_sample_rate_ = 0;
        video_audio_clocked_frame_ = -1;
        video_audio_clock_start_frame_ = 0;
        pending_centered_one_shot_start_.reset();
        centered_one_shot_child_created_ = false;
        navigator_.reset();
        scene_data_.reset();
        slot_spin_active_ = false;
        slot_result_visible_ = false;
        slot_no_credit_visible_ = false;
        slot_outcome_.reset();
        navigation_arrival_pending_ = false;
        navigation_initial_entry_pending_ = false;
        navigation_loader_seed_pending_ = false;
        navigation_neutral_cursor_active_ = false;
        navigation_cursor_due_ = std::chrono::steady_clock::now() +
            kNavigationCursorInterval;
        performance_access_granted_ = false;
        pending_penalty_advance_.reset();
        active_performance_category_.reset();
        judging_comment_previewed_ = false;
        status_.clear();
        const Scene& selected = kScenes[scene_];
        std::optional<HostCue> host_cue = play_host_interstitial
            ? HostEntryCueForScene(scene_)
            : std::nullopt;
        if (host_cue && host_entry_played_[
                static_cast<std::size_t>(host_cue->channel)
            ]) {
            host_cue.reset();
        }
        const auto loading_judging_category =
            JudgingCategoryForScene(scene_);
        const bool delay_consumed_judging_entry =
            play_host_interstitial && loading_judging_category && !host_cue;
        const bool construct_weight_resources = scene_ == kWeightScene;
        bool weight_gauges_ready = false;
        std::filesystem::path media_root = SceneAssetsRoot();
        if (IsSecondaryAssetScene() && !HasSecondaryAssets()) {
            status_ = L"Supplemental packaged assets were not found; use --assets2 <directory>.";
        }
        if (scene_ == kCryoScene) {
            cryo_info_visible_ = false;
            // 0x00403f47 runs C11 first and constructs ROTATE/INFO only after
            // that modal helper returns, inside this same retained dialog. A
            // re-entry whose C11 flag was already consumed constructs both
            // retained children immediately in their recovered order.
            if (host_cue) {
                cryo_media_children_constructed_ = false;
            }
            LoadCryoAssets(!host_cue.has_value());
        }
        if (scene_ == kCommentsScene) {
            // Before C51, 0x0040f35c/0x0040fa08 expose only ORDER.BMP and
            // TH10. The timer loads all ten phrases and positions HAND only
            // after the modal host runner returns.
            LoadCommentBitmaps(!host_cue.has_value());
            status_ = L"Order the ten host comments from rank 10 down to rank 1.";
        }
        if (scene_ == kWeightScene) {
            // Weight's constructor starts the three controls at zero and
            // paints no gauges during its 500 ms delay or C91. The timer sets
            // all three to five and installs TH1_5/TH2_5/TH3_5 after C91.
            if (host_cue) {
                weight_controls_ = {0, 0, 0};
            } else if (!play_host_interstitial) {
                weight_controls_ = {5, 5, 5};
            }
            weight_gauges_ready = !host_cue.has_value();
        }
        if (scene_ == kSlotsScene) {
            LoadSlotBitmaps();
        }

        // Every recovered ambient start passes sndPlaySound flags 9
        // (SND_ASYNC | SND_LOOP). Cryo starts its loop before scheduling C11;
        // the three judging dialogs start theirs only after C21/C31/C41 exits.
        const bool start_ambient_before_host = scene_ == kCryoScene;
        if (selected.sound != nullptr &&
            (!host_cue || start_ambient_before_host) &&
            !loading_judging_category) {
            const auto ambient = media_root / selected.sound;
            PlaySoundW(
                ambient.c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_LOOP
            );
        }
        if (scene_ == kCryoScene) {
            if (host_cue) {
                ScheduleHostEntry(*host_cue, charge_performance);
            }
            return;
        }

        std::filesystem::path video_path;
        if (const auto category = JudgingCategoryForScene(scene_)) {
            media_root = JudgingAssetsRoot(*category);
            if (*category != metaverse::JudgingCategory::looks) {
                // OnInit sets +0x64 along with the initial base/cash dirties.
                // With no active contestant the first paint only consumes it.
                wide_selection_frame_dirty_ = true;
                wide_rating_visible_ = true;
            }
            LoadJudgingBackground(media_root, selected.bitmap);
            LoadJudgingActionBitmaps(*category);
            if (*category == metaverse::JudgingCategory::looks) {
                LoadLooksConstructionHiddenBitmaps();
            }
            if (host_cue) {
                // fcn.00412944 does not construct ROTATE.AVI or put a
                // magnifier bitmap in the presentation rectangle until after
                // the one-shot C41 host callback returns.  Keep that area as
                // the LOOKS.BMP background while the entry clip is active.
                if (*category != metaverse::JudgingCategory::looks) {
                    LoadJudgingRatingBitmap(*category);
                }
                LoadJudgingPortraits(*category);
                ScheduleHostEntry(*host_cue, charge_performance);
                return;
            }
            if (delay_consumed_judging_entry) {
                // Resource construction precedes SetTimer in all three dialog
                // OnInit bodies. A consumed document one-shot skips only the
                // mode-6 host call; it does not skip the 500 ms callback or its
                // post-host continuation.
                if (*category != metaverse::JudgingCategory::looks) {
                    LoadJudgingRatingBitmap(*category);
                }
                LoadJudgingPortraits(*category);
                pending_judging_entry_continuation_ =
                    PendingJudgingEntryContinuation{
                        *category,
                        std::chrono::steady_clock::now() +
                            metaverse::PlanJudgingEntryTimer(
                                *category, false
                            ).delay,
                    };
                status_ = *category == metaverse::JudgingCategory::looks
                    ? L"Preparing the Looks pavilion."
                    : L"Select any unfinished contestant portrait.";
                return;
            }
            if (*category == metaverse::JudgingCategory::looks &&
                !judging_contestant_active_) {
                // After C41 (or its suppressed re-entry callback), the Looks
                // dialog's fcn.004140bf selects the first pending portrait and
                // seeks ROTATE.AVI to that contestant automatically. Talent
                // and Brains instead remain at an empty chooser.
                if (const auto first = NextEligibleJudgingSlot(*category, 0)) {
                    judging_contestant_ = *first;
                    judging_contestant_active_ = true;
                    current_rating_ = 0;
                    looks_selection_frame_dirty_ = true;
                }
            }
            if (*category == metaverse::JudgingCategory::looks) {
                // Unlike Talent and Brains, Looks never charges or checks a
                // performance surcharge, and its recovered mouse handler does
                // not gate ACCEPT/PENALTY/ROTATE on an active contestant ID.
                performance_access_granted_ = true;
            }
            if (!judging_contestant_active_) {
                // With no pending Looks contestant, fcn.004140bf leaves its
                // current contestant ID at the entry caller's zero. The timer
                // has nevertheless constructed ROTATE.AVI at frame zero and
                // hides that retained child. A later WM_ACTIVATEAPP can show
                // it again; there is no MAG/111.BMP idle substitute.
                if (*category == metaverse::JudgingCategory::looks) {
                    LoadLooksRotateFrame(0, false);
                }
                LoadJudgingRatingBitmap(*category);
                LoadJudgingPortraits(*category);
                const auto ambient = PavilionAmbientSound(*category);
                PlaySoundW(
                    ambient.c_str(), nullptr,
                    SND_FILENAME | SND_ASYNC | SND_LOOP
                );
                status_ = L"Select any unfinished contestant portrait.";
                return;
            }
            if (*category == metaverse::JudgingCategory::talent &&
                !HasSecondaryAssets()) {
                performance_access_granted_ = true;
                status_ = L"Supplemental assets were not found. Manual Talent rating "
                          L"remains available; pass --assets2 to restore original media.";
            } else if (!charge_performance && replay_access) {
                performance_access_granted_ = true;
                video_path = JudgingVideoPath(*category);
            } else {
                const std::int32_t contestant_id =
                    round_.contestants[judging_contestant_].contestant_id;
                float surcharge = 0.0f;
                std::string error;
                if (metaverse::ChargePerformanceSurcharge(
                        round_, contestant_id, surcharge, error
                    )) {
                    performance_access_granted_ = true;
                    video_path = JudgingVideoPath(*category);
                    wchar_t amount[32] = {};
                    swprintf_s(
                        amount, L"%.2f", static_cast<double>(surcharge)
                    );
                    status_ = L"Performance surcharge: $" +
                              std::wstring(amount) + L".";
                } else {
                    status_ = std::wstring(error.begin(), error.end()) + L".";
                    // 0x004073f2 / 0x0040962c clear the active-contestant
                    // field and return to the chooser after NOCASH2.  Leaving
                    // it set prevents both choosing another portrait and using
                    // any judging action, trapping the round permanently.
                    judging_contestant_active_ = false;
                    current_rating_ = 0;
                    const auto no_cash = media_root / L"WAV" / L"ANNOUNCE" /
                        L"NOCASH2.WAV";
                    PlaySoundW(
                        no_cash.c_str(), nullptr,
                        SND_FILENAME | SND_SYNC | SND_NODEFAULT
                    );
                }
            }
            if (*category == metaverse::JudgingCategory::looks) {
                LoadLooksContestantFrame();
                if (!delay_consumed_judging_entry) {
                    const auto ambient = PavilionAmbientSound(*category);
                    PlaySoundW(
                        ambient.c_str(), nullptr,
                        SND_FILENAME | SND_ASYNC | SND_LOOP
                    );
                }
            }
            LoadJudgingRatingBitmap(*category);
            LoadJudgingPortraits(*category);
        } else if (selected.data != nullptr) {
            metaverse::SceneDefinition definition;
            std::string error;
            if (metaverse::LoadSceneData(
                    media_root / selected.data, definition, error
                )) {
                video_path = definition.background_video;
                scene_data_ = std::move(definition);
                navigator_.emplace();
                const std::size_t variant = navigation_hub_active_
                    ? navigation_variant_
                    : selected.variant;
                if (!navigator_->Reset(*scene_data_, variant, error)) {
                    status_ = L"Scene navigation failed: " +
                              std::wstring(error.begin(), error.end());
                    navigator_.reset();
                } else {
                    navigation_arrival_pending_ = true;
                    navigation_initial_entry_pending_ = true;
                }
            } else {
                status_ = L"Scene data failed: " + std::wstring(error.begin(), error.end());
            }
        } else if (selected.video != nullptr) {
            video_path = selected.video;
        }

        if (!video_path.empty() && scene_ == kIntroScene) {
            // fcn.0040133c paints and centers resource 158, then timer ID 1
            // waits 100 ms before 0x004013e0 creates and starts MMINTRO.AVI.
            // Winner and credit movies enter through this same resource-158
            // class and use ScheduleCenteredOneShotStart at their call sites.
            ScheduleCenteredOneShotStart(
                CenteredOneShotKind::intro, assets_, video_path
            );
            return;
        }

        if (!video_path.empty()) {
            std::string error;
            if (const auto category = JudgingCategoryForScene(scene_);
                category &&
                (*category == metaverse::JudgingCategory::brains ||
                 *category == metaverse::JudgingCategory::talent)) {
                // The original stores its newly allocated wrapper at +0x17c
                // before 0x0040802f tries MCIWndCreate/Open, and ignores that
                // helper's return. Preserve child/input ownership on failure.
                active_performance_category_ = *category;
            }
            if (video_decoder_.Open(media_root / video_path, error)) {
                metaverse::AudioTrack audio;
                bool has_audio = false;
                if (!metaverse::DecodeAudioTrack(
                        media_root / video_path, audio, has_audio, error
                    )) {
                    status_ = L"Audio decode failed: " +
                              std::wstring(error.begin(), error.end());
                } else if (has_audio) {
                    if (selected.data != nullptr) {
                        // Navigation MCI children are loaded and paused at an
                        // entry frame. Embedded audio begins only when a DAT
                        // branch issues a bounded PlayTo, not at scene load.
                        navigation_media_audio_ = std::move(audio);
                    } else if (!audio_player_.Play(audio, false, error)) {
                        status_ = L"Audio playback failed: " +
                                  std::wstring(error.begin(), error.end());
                    } else {
                        // MCIWnd uses its interleaved media clock for these
                        // one-shot AVIs. Drive native frame selection from the
                        // same embedded-audio cursor so a busy message pump
                        // skips to the authored frame instead of drifting.
                        video_audio_clocked_ = true;
                        video_audio_sample_rate_ =
                            static_cast<std::uint32_t>(audio.sample_rate);
                        video_audio_clock_start_frame_ = 0;
                    }
                }
                video_looping_ = false;
                if (selected.data != nullptr) {
                    metaverse::VideoFrame first_frame;
                    bool ended = false;
                    if (!video_decoder_.DecodeNext(first_frame, ended, error) || ended) {
                        status_ = L"Initial video frame failed: " +
                                  std::wstring(error.begin(), error.end());
                    } else {
                        UploadVideoFrame(first_frame);
                    }
                    video_paused_ = true;
                } else {
                    next_frame_due_ = std::chrono::steady_clock::now();
                }
                return;
            }
            status_ = L"Video open failed: " + std::wstring(error.begin(), error.end());
        }
        if (selected.bitmap == nullptr) {
            return;
        }
        if (JudgingCategoryForScene(scene_)) {
            return;
        }
        const auto path = media_root / selected.bitmap;
        bitmap_ = LoadBitmapWithLegacyWarning(path);
        BITMAP details = {};
        if (bitmap_ != nullptr && GetObjectW(bitmap_, sizeof(details), &details)) {
            bitmap_width_ = details.bmWidth;
            bitmap_height_ = details.bmHeight;
        } else {
            bitmap_width_ = 0;
            bitmap_height_ = 0;
        }
        if (construct_weight_resources) {
            // 0x004106e6 loads WEIGHT.BMP before OK, hidden TH*_1 backings,
            // and the six arrows. Keep its warning and ownership order exact.
            LoadWeightBitmaps(weight_gauges_ready);
            weight_painter_.base_dirty = true;
        }
        if (host_cue) {
            ScheduleHostEntry(*host_cue, charge_performance);
        }
    }

    std::filesystem::path assets_;
    std::optional<std::filesystem::path> assets2_;
    std::filesystem::path profile_path_;
    HWND owner_window_ = nullptr;
    std::vector<metaverse::PlayerProfile> profiles_;
    metaverse::PlayerProfile player_;
    metaverse::GameRoundState round_;
    metaverse::PavilionRotationState pavilion_rotation_;
    metaverse::HostReactionState host_reaction_state_;
    bool profile_selection_pending_ = false;
    bool profile_selection_in_progress_ = false;
    metaverse::LegacyRandom random_;
    std::array<std::int32_t, metaverse::kContestantsPerRound> brain_variants_{};
    std::array<std::int32_t, metaverse::kContestantsPerRound> talent_variants_{};
    bool pavilion_variants_ready_ = false;
    metaverse::PavilionVisitState judging_visit_{};
    std::size_t judging_contestant_ = 0;
    bool judging_contestant_active_ = false;
    int current_rating_ = 0;
    bool suppress_judging_rating_bitmap_once_ = false;
    bool round_tallied_ = false;
    std::optional<metaverse::TallyResult> tally_result_;
    std::optional<metaverse::SlotOutcome> slot_outcome_;
    bool slot_spin_active_ = false;
    bool slot_result_visible_ = false;
    bool slot_no_credit_visible_ = false;
    std::chrono::steady_clock::time_point slot_spin_due_{};
    metaverse::CommentOrderState comment_order_;
    std::array<std::int32_t, metaverse::kJudgingCategoryCount> weight_controls_{5, 5, 5};
    int selected_weight_ = 0;
    int browsed_contestant_ = 0;
    std::vector<std::int32_t> selected_contestants_;
    int scene_ = 0;
    std::optional<metaverse::SceneDefinition> scene_data_;
    std::optional<metaverse::SceneNavigator> navigator_;
    std::vector<metaverse::NavigationEntry> navigation_entries_;
    std::size_t navigation_entry_index_ = 0;
    std::size_t navigation_variant_ = 1;
    bool navigation_hub_active_ = false;
    bool pending_navigation_resolution_ = false;
    bool navigation_arrival_pending_ = false;
    bool navigation_initial_entry_pending_ = false;
    bool navigation_loader_seed_pending_ = false;
    metaverse::NavigationPopupOneShotState navigation_popup_one_shot_state_;
    metaverse::NavigationPopupIdleLatch navigation_popup_idle_latch_;
    std::array<
        bool, metaverse::kNavigationIdleAnimationObjects.size()
    > navigation_idle_animation_used_{};
    bool navigation_idle_animation_active_ = false;
    std::chrono::steady_clock::time_point navigation_idle_due_{};
    std::array<bool, 256> navigation_encountered_nodes_{};
    std::optional<std::int32_t> pending_navigation_encounter_node_;
    std::chrono::steady_clock::time_point navigation_encounter_due_{};
    static constexpr auto kNavigationFadeDuration =
        std::chrono::milliseconds(260);
    std::optional<std::size_t> pending_navigation_fade_target_;
    std::chrono::steady_clock::time_point navigation_fade_started_{};
    HCURSOR navigation_up_cursor_ = nullptr;
    HCURSOR navigation_neutral_cursor_a_ = nullptr;
    HCURSOR generic_target_cursor_ = nullptr;
    HCURSOR navigation_right_cursor_ = nullptr;
    HCURSOR navigation_left_cursor_ = nullptr;
    HCURSOR navigation_neutral_cursor_b_ = nullptr;
    HCURSOR default_hand_cursor_ = nullptr;
    HCURSOR pavilion_exit_cursor_ = nullptr;
    HCURSOR looks_magnifier_cursor_ = nullptr;
    static constexpr auto kNavigationCursorInterval =
        std::chrono::milliseconds(
            metaverse::kNavigationTimerIntervalMs
        );
    std::chrono::steady_clock::time_point navigation_cursor_due_{};
    bool navigation_neutral_cursor_active_ = false;
    bool navigation_neutral_cursor_frame_ = false;
    HBITMAP navigation_overlay_bitmap_ = nullptr;
    int navigation_overlay_width_ = 0;
    int navigation_overlay_height_ = 0;
    int navigation_overlay_x_ = 0;
    int navigation_overlay_y_ = 0;
    std::int32_t navigation_overlay_object_ = -1;
    bool navigation_overlay_visible_ = false;
    bool navigation_overlay_next_visible_ = false;
    bool pending_tally_finale_ = false;
    bool pending_exit_after_credit_ = false;
    std::optional<PendingCenteredOneShotStart>
        pending_centered_one_shot_start_;
    bool centered_one_shot_child_created_ = false;
    std::array<bool, metaverse::kHostPointCount> host_entry_played_{};
    std::optional<HostCue> pending_host_entry_cue_;
    bool pending_host_entry_charge_ = true;
    std::chrono::steady_clock::time_point pending_host_entry_due_{};
    std::optional<bool> pending_host_scene_load_charge_;
    std::optional<PendingHostJudgingAdvance> pending_host_judging_advance_;
    std::optional<metaverse::JudgingCategory> pending_host_pavilion_close_;
    bool pending_host_resume_cryo_ambient_ = false;
    std::optional<PendingJudgingEntryContinuation>
        pending_judging_entry_continuation_;
    std::array<bool, metaverse::kJudgingCategoryCount>
        first_gong_host_played_{};
    std::array<bool, metaverse::kJudgingCategoryCount>
        first_penalty_host_played_{};
    bool cryo_first_sponsor_host_played_ = false;
    bool host_interstitial_active_ = false;
    bool host_motion_active_ = false;
    bool host_visual_complete_ = false;
    bool host_ouch_pending_ = false;
    std::int64_t host_audio_clocked_frame_ = -1;
    metaverse::MotionPlayer host_motion_player_;
    metaverse::VideoDecoder host_video_decoder_;
    metaverse::AudioPlayer host_audio_player_;
    HBITMAP host_bitmap_ = nullptr;
    int host_bitmap_width_ = 0;
    int host_bitmap_height_ = 0;
    int host_bitmap_x_ = 0;
    int host_bitmap_y_ = 0;
    std::chrono::steady_clock::time_point host_next_frame_due_{};
    std::optional<std::chrono::steady_clock::time_point>
        host_motion_sync_anchor_;
    metaverse::VideoDecoder video_decoder_;
    metaverse::VideoDecoder sprite_decoder_;
    metaverse::VideoDecoder cryo_rotate_decoder_;
    metaverse::VideoDecoder cryo_info_decoder_;
    metaverse::VideoDecoder looks_rotate_decoder_;
    metaverse::MotionPlayer motion_player_;
    metaverse::AudioPlayer audio_player_;
    metaverse::AudioPlayer navigation_effect_player_;
    metaverse::AudioPlayer sprite_audio_player_;
    std::optional<metaverse::AudioTrack> navigation_departure_audio_;
    std::optional<metaverse::AudioTrack> navigation_ricochet_audio_;
    std::optional<metaverse::AudioTrack> cryo_info_audio_;
    std::optional<metaverse::AudioTrack> navigation_media_audio_;
    std::chrono::steady_clock::time_point next_frame_due_{};
    std::chrono::steady_clock::time_point next_sprite_due_{};
    std::int32_t simm_motion_pacing_factor_ = 0;
    std::optional<float> simm_pending_award_;
    std::chrono::steady_clock::time_point cryo_rotate_due_{};
    std::chrono::steady_clock::time_point cryo_info_due_{};
    std::chrono::steady_clock::time_point looks_rotate_due_{};
    std::optional<std::pair<std::int64_t, std::int64_t>> playback_range_;
    bool video_looping_ = false;
    bool video_paused_ = false;
    bool video_audio_clocked_ = false;
    std::uint32_t video_audio_sample_rate_ = 0;
    std::int64_t video_audio_clocked_frame_ = -1;
    std::int64_t video_audio_clock_start_frame_ = 0;
    bool performance_access_granted_ = false;
    std::optional<PendingPenaltyAdvance> pending_penalty_advance_;
    std::optional<metaverse::JudgingCategory> active_performance_category_;
    std::wstring status_;
    HBITMAP bitmap_ = nullptr;
    HBITMAP background_bitmap_ = nullptr;
    HBITMAP looks_bitmap_ = nullptr;
    HBITMAP looks_rotate_bitmap_ = nullptr;
    HBITMAP judging_rating_bitmap_ = nullptr;
    std::array<HBITMAP, metaverse::kContestantsPerRound>
        judging_portrait_bitmaps_{};
    std::array<HBITMAP, 4> judging_action_bitmaps_{};
    HBITMAP sprite_bitmap_ = nullptr;
    HBITMAP cryo_controls_bitmap_ = nullptr;
    HBITMAP cryo_money_bitmap_ = nullptr;
    HBITMAP cryo_info_background_bitmap_ = nullptr;
    std::array<HBITMAP, metaverse::kContestantsPerRound>
        cryo_portrait_bitmaps_{};
    std::array<HBITMAP, 5> cryo_control_highlight_bitmaps_{};
    std::array<HBITMAP, 6> cryo_info_highlight_bitmaps_{};
    HBITMAP cryo_rotate_bitmap_ = nullptr;
    HBITMAP cryo_info_bitmap_ = nullptr;
    std::array<HBITMAP, 3> slot_bitmaps_{};
    std::array<HBITMAP, metaverse::kCommentCount> comment_phrase_bitmaps_{};
    HBITMAP comment_accept_bitmap_ = nullptr;
    HBITMAP comment_hand_bitmap_ = nullptr;
    HBITMAP comment_thermometer_bitmap_ = nullptr;
    bool comment_accept_pressed_ = false;
    bool comment_hand_visible_ = false;
    std::int32_t comment_hand_row_ = 0;
    metaverse::CommentPainterState comment_painter_{};
    std::array<HBITMAP, metaverse::kJudgingCategoryCount>
        weight_gauge_bitmaps_{};
    std::array<bool, metaverse::kJudgingCategoryCount>
        weight_gauge_visible_{};
    metaverse::CategoryWeightPainterState weight_painter_{};
    std::array<HBITMAP, metaverse::kCategoryWeightControlCount>
        weight_control_bitmaps_{};
    HBITMAP weight_ok_bitmap_ = nullptr;
    int bitmap_width_ = 0;
    int bitmap_height_ = 0;
    int background_width_ = 0;
    int background_height_ = 0;
    int looks_width_ = 0;
    int looks_height_ = 0;
    int looks_rotate_width_ = 0;
    int looks_rotate_height_ = 0;
    std::int32_t looks_current_contestant_id_ = 0;
    std::array<std::int32_t, 4> looks_magnification_levels_{
        metaverse::kLooksInitialMagnificationLevel,
        metaverse::kLooksInitialMagnificationLevel,
        metaverse::kLooksInitialMagnificationLevel,
        metaverse::kLooksInitialMagnificationLevel,
    };
    int judging_rating_width_ = 0;
    int judging_rating_height_ = 0;
    std::array<std::array<bool, metaverse::kContestantsPerRound>,
               metaverse::kJudgingCategoryCount> gonged_{};
    int cryo_rotate_width_ = 0;
    int cryo_rotate_height_ = 0;
    int cryo_info_width_ = 0;
    int cryo_info_height_ = 0;
    std::int64_t cryo_rotate_last_frame_ = 0;
    std::int64_t cryo_rotation_next_frame_ = 0;
    std::int64_t cryo_info_last_frame_ = 0;
    std::int64_t looks_rotate_last_frame_ = 0;
    bool cryo_rotate_playing_ = false;
    bool cryo_info_visible_ = false;
    bool cryo_info_playing_ = false;
    bool cryo_info_audio_clocked_ = false;
    bool cryo_media_children_constructed_ = false;
    bool cryo_base_only_exposure_ = false;
    int cryo_info_audio_sample_rate_ = 0;
    std::int64_t cryo_info_first_frame_ = 0;
    std::int64_t cryo_info_clocked_frame_ = -1;
    int cryo_pressed_action_ = 0;
    int weight_pressed_control_ = -1;
    bool weight_ok_pressed_ = false;
    JudgingAction judging_pressed_action_ = JudgingAction::none;
    JudgingAction judging_pressed_visual_ = JudgingAction::none;
    std::optional<std::size_t> judging_hovered_portrait_;
    bool judging_comment_previewed_ = false;
    bool looks_rotate_playing_ = false;
    bool looks_rotate_visible_ = false;
    bool looks_rotate_child_constructed_ = false;
    bool looks_magnifier_visible_ = false;
    bool looks_rating_visible_ = false;
    bool looks_front_view_ = true;
    bool looks_selection_frame_dirty_ = false;
    bool wide_rating_visible_ = true;
    bool wide_selection_frame_dirty_ = false;
    int sprite_width_ = 0;
    int sprite_height_ = 0;
    int sprite_number_ = -1;
    std::uint64_t last_sprite_sound_entry_ = 0;
    metaverse::SimmSelectionTable simm_selection_table_{};
    bool fullscreen_active_ = false;
    RECT windowed_bounds_{};
};

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    switch (message) {
        case WM_CREATE:
            if (application != nullptr) {
                application->InitializeWindow(window);
            }
            SetTimer(window, 1, 10, nullptr);
            return 0;
        case WM_TIMER:
            if (application != nullptr && wparam == 1) {
                application->Tick(window);
            }
            return 0;
        case WM_SETCURSOR:
            if (application != nullptr &&
                application->HandleSetCursor(window)) {
                return TRUE;
            }
            break;
        case WM_ACTIVATEAPP:
            if (application != nullptr &&
                metaverse::GenericMediaActivationChangeEndsModal(
                    wparam != FALSE
                ) &&
                application->HandleApplicationActivationChanged(
                    window, wparam != FALSE
                )) {
                return 0;
            }
            break;
        case WM_KEYDOWN: {
            const bool alt_down =
                (GetKeyState(VK_MENU) & 0x8000) != 0 ||
                (lparam & 0x20000000) != 0;
            const bool control_down =
                (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const auto shortcut = metaverse::NativeShortcutForKey(
                static_cast<std::uint32_t>(wparam), alt_down, control_down
            );
            if (application != nullptr &&
                shortcut != metaverse::NativeShortcutAction::none) {
                if ((lparam & 0x40000000) == 0) {
                    if (shortcut ==
                        metaverse::NativeShortcutAction::toggle_fullscreen) {
                        application->ToggleFullscreen(window);
                    } else {
                        application->ApplyCreditCheat(window);
                    }
                }
                return 0;
            }
            if (application != nullptr && wparam == VK_ESCAPE) {
                application->HandleDialogCancel(window);
            } else if (application != nullptr && wparam == VK_RETURN &&
                       application->HandleDialogAccept(window)) {
                return 0;
            }
            return 0;
        }
        case WM_SYSKEYDOWN: {
            const bool alt_down =
                (GetKeyState(VK_MENU) & 0x8000) != 0 ||
                (lparam & 0x20000000) != 0;
            const bool control_down =
                (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const auto shortcut = metaverse::NativeShortcutForKey(
                static_cast<std::uint32_t>(wparam), alt_down, control_down
            );
            if (application != nullptr &&
                shortcut != metaverse::NativeShortcutAction::none) {
                // Suppress repeats while the key is held. Ordinary Enter keeps
                // the recovered dialog-default behavior in WM_KEYDOWN.
                if ((lparam & 0x40000000) == 0) {
                    if (shortcut ==
                        metaverse::NativeShortcutAction::toggle_fullscreen) {
                        application->ToggleFullscreen(window);
                    } else {
                        application->ApplyCreditCheat(window);
                    }
                }
                return 0;
            }
            break;
        }
        case WM_SYSCHAR:
            if (wparam == VK_RETURN || wparam == VK_F1) {
                // Prevent the default menu-key beep after handled shortcuts.
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (application != nullptr && application->HandleMouseDown(
                    GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), wparam, window
                )) {
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (application != nullptr && application->HandleMouseUp(
                    GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), window
                )) {
                return 0;
            }
            break;
        case WM_RBUTTONDOWN:
            if (application != nullptr &&
                application->ShowNavigationContextMenu(
                    GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), window
                )) {
                return 0;
            }
            break;
        case WM_RBUTTONDBLCLK:
            if (application != nullptr &&
                application->HandleRightButtonDoubleClick(window)) {
                return 0;
            }
            break;
        case WM_COMMAND:
            if (application != nullptr && HIWORD(wparam) == 0 &&
                application->HandleNavigationMenuCommand(
                    LOWORD(wparam), window
                )) {
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (application != nullptr && application->HandleMouseMove(
                    GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), window
                )) {
                return 0;
            }
            break;
        case WM_PAINT:
            if (application != nullptr) {
                application->Paint(window);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            // All painting is done in WM_PAINT with an explicit full-frame clear.
            // Suppressing erase prevents black background flashes during frequent
            // invalidations and scene transitions.
            return TRUE;
        case WM_CLOSE:
            if (application != nullptr) {
                application->HandleDialogCancel(window);
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(window, 1);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    SetProcessDPIAware();
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const auto assets = FindAssets(argc, argv);
    if (!assets) {
        LocalFree(argv);
        MessageBoxW(
            nullptr,
            L"Could not locate extracted assets. Use --assets <directory> or set "
            L"MS_METAVERSE_ASSETS.",
            L"Ms. Metaverse Native",
            MB_OK | MB_ICONERROR
        );
        return 2;
    }
    const auto assets2 = FindAssets2(argc, argv);
    const std::filesystem::path profile_path = FindProfileDataPath(argc, argv);
    const bool guest_mode = HasArgument(argc, argv, L"--guest");
    LocalFree(argv);

    std::vector<metaverse::PlayerProfile> profiles;
    std::string profile_error;
    if (!metaverse::LoadLegacyProfiles(profile_path, profiles, profile_error)) {
        const std::wstring message(profile_error.begin(), profile_error.end());
        MessageBoxW(
            nullptr, message.c_str(), L"Ms. Metaverse Native — Profile Error",
            MB_OK | MB_ICONERROR
        );
        return 3;
    }
    metaverse::PavilionRotationState pavilion_rotation;
    // NRFPAV.DAT is neither read nor validated at process startup in MM.EXE.
    // Its ten round-preparation objects perform best-effort selected-line I/O
    // later; malformed or unavailable state must not gate login or the intro.
    metaverse::HostReactionState host_reaction_state;
    const std::filesystem::path host_reaction_path =
        profile_path.parent_path() / L"hnrd.dat";
    // HNRD.DAT is not a startup gate in MM.EXE. Its selection helper performs
    // a zero-filled best-effort raw read only when an X reaction is requested.
    metaverse::LoadLegacyHostReactionStateForSelection(
        host_reaction_path, host_reaction_state
    );
    metaverse::PlayerProfile player;
    if (guest_mode) {
        player = metaverse::MakeGuestProfile();
    } else {
        // The owning controller authenticates before fcn.00401b03 plays
        // MMINTRO.AVI. This placeholder is replaced from WM_CREATE before the
        // introduction is opened.
        player = metaverse::MakeGuestProfile();
    }
    Application application(
        *assets,
        assets2,
        profile_path,
        std::move(profiles),
        std::move(player),
        std::move(pavilion_rotation),
        std::move(host_reaction_state),
        !guest_mode
    );

    constexpr wchar_t class_name[] = L"MsMetaverseNativeWindow";
    WNDCLASSW window_class = {};
    // The built-in dialog class used by resource 158 emits double-click
    // messages. ORDER relies on WM_RBUTTONDBLCLK for its result-zero escape
    // route, so the native host class must request those messages as well.
    window_class.style = CS_DBLCLKS;
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    // The original PE's resource 178 contains both of its application-icon
    // images. The reconstructed group is linked into this EXE, so Windows does
    // not need an adjacent ICO or any original installation file.
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(178));
    // Resource 166 is MM.EXE's normal game-screen hand cursor. It is embedded
    // in this executable; no external .cur file is needed at runtime.
    window_class.hCursor = LoadCursorW(
        instance,
        MAKEINTRESOURCEW(metaverse::kCenteredOneShotCursorResourceId)
    );
    if (window_class.hCursor == nullptr) {
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    }
    window_class.hbrBackground = nullptr;
    if (!RegisterClassW(&window_class)) {
        return 1;
    }

    // Shared dialog resource 158 is WS_POPUP | WS_VISIBLE | DS_SETFONT and
    // every recovered game screen uses its 640x480 outer window. The intro
    // setup at 0x0040137d-0x004013ac centers that exact outer rectangle on the
    // desktop; keep the native host window identical rather than adding a
    // resizable caption/frame around the original canvas.
    constexpr int game_width = metaverse::kCenteredOneShotWidth;
    constexpr int game_height = metaverse::kCenteredOneShotHeight;
    const int game_x = (GetSystemMetrics(SM_CXSCREEN) - game_width) / 2;
    const int game_y = (GetSystemMetrics(SM_CYSCREEN) - game_height) / 2;
    HWND window = CreateWindowExW(
        0,
        class_name,
        metaverse::kCenteredOneShotCaption.data(),
        WS_POPUP,
        game_x,
        game_y,
        game_width,
        game_height,
        nullptr,
        nullptr,
        instance,
        &application
    );
    if (window == nullptr) {
        return 1;
    }
    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    // fcn.00402edd returns silently when its CFile open/update fails, and the
    // orchestrator at 0x004026b8 ignores the routine's outcome. Preserve that
    // original exit behavior instead of displaying a port-only save dialog.
    application.Persist(profile_error);
    return static_cast<int>(message.wParam);
}
