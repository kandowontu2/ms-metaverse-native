#include "profile_dialog.hpp"
#include "legacy_dialog.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>
#include <windowsx.h>

namespace metaverse {
namespace {

// Password-dialog construction and input are split across the original class
// beginning at 0x0040815f. Its initialization path at 0x0040828c centers a
// 640x480 client, moves edit IDs 1002/1005 to these rectangles, and installs
// the painted OK hit target at 535,378 using the bitmap's native extent.
constexpr wchar_t kClassName[] = L"MsMetaverseNativeProfileDialog";
constexpr int kNameControl = 1002;
constexpr int kPasswordControl = 1005;
constexpr int kCanvasWidth = 640;
constexpr int kCanvasHeight = 480;
constexpr RECT kNameRect{205, 208, 413, 224};
constexpr RECT kPasswordRect{205, 295, 413, 311};
constexpr POINT kOkOrigin{535, 378};

struct DialogState {
    std::wstring name_text = L"GUEST";
    std::wstring password_text;
    HWND name = nullptr;
    HWND password = nullptr;
    HBITMAP background = nullptr;
    HBITMAP ok_pressed = nullptr;
    HFONT font = nullptr;
    // The packaged original is 53x52. Keep that deterministic hit area if a
    // failed legacy-style load leaves no bitmap dimensions to query.
    SIZE ok_size{53, 52};
    bool ok_held = false;

    ~DialogState() {
        if (background != nullptr) {
            DeleteObject(background);
        }
        if (ok_pressed != nullptr) {
            DeleteObject(ok_pressed);
        }
        if (font != nullptr) {
            DeleteObject(font);
        }
    }
};

void ApplyDialogFont(HWND control, HFONT font) {
    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(font),
        TRUE
    );
}

std::wstring ControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }
    std::vector<wchar_t> value(static_cast<std::size_t>(length) + 1);
    const LRESULT copied = SendMessageW(
        control,
        WM_GETTEXT,
        static_cast<WPARAM>(value.size()),
        reinterpret_cast<LPARAM>(value.data())
    );
    return std::wstring(
        value.data(), static_cast<std::size_t>(std::max<LRESULT>(copied, 0))
    );
}

bool ToLegacyCredential(
    std::wstring_view wide,
    std::string& legacy,
    std::string& error
) {
    legacy.clear();
    if (wide.empty()) {
        return true;
    }
    // MM.EXE is an ANSI application. Its edit controls and CString fields use
    // the active Windows code page, including its normal '?' substitution for
    // characters that the code page cannot represent; it does not impose the
    // port's former printable-ASCII-only validation branch.
    const int source_length = static_cast<int>(wide.size());
    const int required = WideCharToMultiByte(
        CP_ACP, 0, wide.data(), source_length, nullptr, 0, nullptr, nullptr
    );
    if (required <= 0) {
        error = "could not convert profile credentials to the Windows code page";
        return false;
    }
    legacy.resize(static_cast<std::size_t>(required));
    if (WideCharToMultiByte(
            CP_ACP,
            0,
            wide.data(),
            source_length,
            legacy.data(),
            required,
            nullptr,
            nullptr
        ) != required) {
        legacy.clear();
        error = "could not convert profile credentials to the Windows code page";
        return false;
    }
    return true;
}

HBITMAP LoadBitmapFile(const std::filesystem::path& path) {
    return static_cast<HBITMAP>(LoadImageW(
        nullptr,
        path.c_str(),
        IMAGE_BITMAP,
        0,
        0,
        LR_CREATEDIBSECTION | LR_LOADFROMFILE
    ));
}

bool BitmapSize(HBITMAP bitmap, SIZE& size) {
    BITMAP details{};
    if (bitmap == nullptr || GetObjectW(bitmap, sizeof(details), &details) == 0) {
        return false;
    }
    size.cx = details.bmWidth;
    size.cy = details.bmHeight;
    return size.cx > 0 && size.cy > 0;
}

RECT OkRect(const DialogState& state) {
    return {
        kOkOrigin.x,
        kOkOrigin.y,
        kOkOrigin.x + state.ok_size.cx,
        kOkOrigin.y + state.ok_size.cy,
    };
}

bool Inside(const RECT& rectangle, LPARAM lparam) {
    const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    return PtInRect(&rectangle, point) != FALSE;
}

void InvalidateOk(HWND window, const DialogState& state) {
    const RECT rectangle = OkRect(state);
    InvalidateRect(window, &rectangle, FALSE);
}

void PaintDialog(HWND window, const DialogState& state) {
    PAINTSTRUCT paint{};
    HDC destination = BeginPaint(window, &paint);
    HDC source = CreateCompatibleDC(destination);
    if (source != nullptr) {
        if (state.background != nullptr) {
            HGDIOBJ previous = SelectObject(source, state.background);
            BitBlt(
                destination,
                0,
                0,
                kCanvasWidth,
                kCanvasHeight,
                source,
                0,
                0,
                SRCCOPY
            );
            SelectObject(source, previous);
        }

        if (state.ok_held && state.ok_pressed != nullptr) {
            HGDIOBJ previous = SelectObject(source, state.ok_pressed);
            BitBlt(
                destination,
                kOkOrigin.x,
                kOkOrigin.y,
                state.ok_size.cx,
                state.ok_size.cy,
                source,
                0,
                0,
                SRCCOPY
            );
            SelectObject(source, previous);
        }
        DeleteDC(source);
    }
    EndPaint(window, &paint);
}

HWND AddEdit(
    HWND parent,
    const RECT& rectangle,
    int identifier,
    bool password,
    const std::wstring& initial,
    HFONT font
) {
    // Resource 167 uses 0x50010088 for name and 0x500100a8 for password:
    // both fields uppercase and auto-scroll; password adds ES_PASSWORD.
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                  ES_UPPERCASE | ES_AUTOHSCROLL;
    if (password) {
        style |= ES_PASSWORD;
    }
    HWND control = CreateWindowExW(
        0,
        L"EDIT",
        initial.c_str(),
        style,
        rectangle.left,
        rectangle.top,
        rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)),
        nullptr
    );
    if (control != nullptr) {
        ApplyDialogFont(control, font);
    }
    return control;
}

LRESULT CALLBACK ProfileProcedure(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    auto* state = reinterpret_cast<DialogState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
        case WM_CREATE:
            if (state == nullptr) {
                return -1;
            }
            state->name = AddEdit(
                window,
                kNameRect,
                kNameControl,
                false,
                state->name_text,
                state->font
            );
            state->password = AddEdit(
                window,
                kPasswordRect,
                kPasswordControl,
                true,
                state->password_text,
                state->font
            );
            if (state->name == nullptr || state->password == nullptr) {
                return -1;
            }
            SetFocus(state->name);
            SendMessageW(state->name, EM_SETSEL, 0, -1);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (state != nullptr) {
                PaintDialog(window, *state);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (state != nullptr && Inside(OkRect(*state), lparam)) {
                // 0x004084b9 sets the pressed flag only for a down inside the
                // painted rectangle, then invalidates and repaints it. It does
                // not call SetCapture despite the common button idiom.
                state->ok_held = true;
                InvalidateOk(window, *state);
                UpdateWindow(window);
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (state != nullptr && state->ok_held) {
                const bool accept = Inside(OkRect(*state), lparam);
                state->ok_held = false;
                InvalidateOk(window, *state);
                UpdateWindow(window);
                if (accept) {
                    // 0x004084f5 releases the art, retests the same rectangle,
                    // and closes only when the button-up also lands inside.
                    state->name_text = ControlText(state->name);
                    state->password_text = ControlText(state->password);
                    DestroyWindow(window);
                }
                return 0;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wparam) == IDCANCEL) {
                // The profile vtable at 0x00431198 overrides both OnOK and
                // OnCancel with the bare returns at 0x00408616/0x00408617.
                // IsDialogMessage turns Escape into IDCANCEL, but the original
                // painted dialog deliberately ignores that default command.
                static_assert(
                    LegacyDefaultRoutesFor(LegacyDialogRole::profile).on_cancel ==
                    LegacyDialogDefaultAction::no_op
                );
                return 0;
            }
            break;
        case WM_CLOSE:
            // Resource 167 has no caption close target. An external close also
            // reaches the same no-op OnCancel override instead of submitting
            // the initial GUEST fields or partially edited control text.
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool RegisterProfileClass(HINSTANCE instance, std::string& error) {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = ProfileProcedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = kClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (RegisterClassW(&window_class) != 0 ||
        GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        return true;
    }
    error = "could not register the profile dialog window class";
    return false;
}

bool RunProfileWindow(
    HINSTANCE instance,
    HWND owner,
    DialogState& state,
    std::string& error
) {
    // Resource 167 is a borderless DS_SETFONT popup. 0x004082b9 then calls
    // MoveWindow with an outer size of exactly 640x480.
    constexpr DWORD style = WS_POPUP;
    constexpr int width = kCanvasWidth;
    constexpr int height = kCanvasHeight;
    RECT desktop{
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)
    };
    GetWindowRect(GetDesktopWindow(), &desktop);
    // 0x004082ac uses RECT.right/bottom directly and does not subtract the
    // taskbar work area or add RECT.left/top.
    const int x = (desktop.right - width) / 2;
    const int y = (desktop.bottom - height) / 2;

    HWND window = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        kClassName,
        L"Ms. Metaverse",
        style,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        &state
    );
    if (window == nullptr) {
        error = "could not create the profile dialog";
        return false;
    }

    if (owner != nullptr) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    while (IsWindow(window)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) {
                PostQuitMessage(static_cast<int>(message.wParam));
            } else {
                error = "profile dialog message loop failed";
            }
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(window)) {
        DestroyWindow(window);
    }
    if (owner != nullptr) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return error.empty();
}

void OriginalWarning(HWND owner, const wchar_t* message) {
    // The login loop at 0x00402b10 uses AfxMessageBox with type 0x30.
    MessageBoxW(
        owner,
        message,
        kLegacyDialogCaption,
        static_cast<UINT>(kLegacyWarningMessageBoxStyle)
    );
}

}  // namespace

bool ChoosePlayerProfile(
    HINSTANCE instance,
    HWND owner,
    const std::filesystem::path& assets,
    std::vector<PlayerProfile>& profiles,
    PlayerProfile& selected,
    std::string& error
) {
    error.clear();
    if (!RegisterProfileClass(instance, error)) {
        return false;
    }

    DialogState state;
    state.font = CreateFontW(
        -11,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"MS Sans Serif"
    );
    state.background = LoadBitmapFile(
        assets / L"BMP" / L"PASSWORD" / L"PASSWORD.BMP"
    );
    state.ok_pressed = LoadBitmapFile(
        assets / L"BMP" / L"PASSWORD" / L"OK.BMP"
    );
    // The original profile constructor reports each failed CBitmap::Load call
    // with the same 0x30 warning, then continues constructing the dialog.
    if (state.background == nullptr) {
        OriginalWarning(owner, kLegacyBitmapReadFailure);
    }
    if (state.ok_pressed == nullptr) {
        OriginalWarning(owner, kLegacyBitmapReadFailure);
    }
    // MM.EXE reports either failed load but continues into the modal. It does
    // not validate the background dimensions or turn a missing image/font into
    // a second, port-only fatal path. A valid OK bitmap supplies its native hit
    // rectangle; the bundled original is always present in the release.
    BitmapSize(state.ok_pressed, state.ok_size);

    for (;;) {
        if (!RunProfileWindow(instance, owner, state, error)) {
            // fcn.0040291f calls DoModal at 0x00402b17 but never inspects its
            // result. A resource/window creation failure therefore falls
            // through with the dialog object's retained fields; on the first
            // attempt those are GUEST and blank. Keep registration/artwork
            // setup failures fatal, but preserve this post-construction path.
            error.clear();
        }
        // Normal Enter, Escape, and close never reach this boundary because
        // resource 167's OnOK/OnCancel vtable entries are bare returns.

        std::string name;
        std::string password;
        if (!ToLegacyCredential(state.name_text, name, error) ||
            !ToLegacyCredential(state.password_text, password, error)) {
            const std::wstring message(error.begin(), error.end());
            OriginalWarning(owner, message.c_str());
            error.clear();
            continue;
        }

        // This ordering and wording follow fcn.0040291f at 0x00402b10.
        const ProfileLoginDecision decision =
            EvaluateProfileLogin(profiles, name, password);
        switch (decision.disposition) {
            case ProfileLoginDisposition::invalid_name:
                OriginalWarning(owner, L"Invalid name!");
                state.name_text = L"GUEST";
                continue;
            case ProfileLoginDisposition::guest_password_not_allowed:
                OriginalWarning(owner, L"GUEST does't require password.");
                continue;
            case ProfileLoginDisposition::guest:
                selected = MakeGuestProfile();
                return true;
            case ProfileLoginDisposition::demo_password_wrong:
                OriginalWarning(owner, L"Wrong Password! ");
                continue;
            case ProfileLoginDisposition::password_required:
                OriginalWarning(owner, L"You must enter a password!");
                continue;
            case ProfileLoginDisposition::existing_password_wrong:
                OriginalWarning(owner, L"Wrong password! ");
                continue;
            case ProfileLoginDisposition::existing_profile:
                // 0x00402d84 copies only the authenticated record's balance
                // into the game document; 0x00402d92 then applies a signed
                // raw-bit $400 floor. Unselected records remain byte-stable.
                selected = ActivateReturningProfile(
                    profiles[decision.existing_profile_index], name, password
                );
                return true;
            case ProfileLoginDisposition::new_demo:
                // A valid unmatched DEMO retains the initial $500 and final
                // persistence continues to exclude it by name.
                selected = {"DEMO", "BUYVV", kNewProfileCredits, false};
                return true;
            case ProfileLoginDisposition::new_profile:
                if (!ValidateNewProfile(name, password, error)) {
                    const std::wstring message(error.begin(), error.end());
                    OriginalWarning(owner, message.c_str());
                    error.clear();
                    continue;
                }
                profiles.push_back({name, password, kNewProfileCredits, false});
                selected = profiles.back();
                return true;
        }
    }
}

}  // namespace metaverse
