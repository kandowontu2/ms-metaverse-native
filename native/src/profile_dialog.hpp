#pragma once

#include "profile_store.hpp"

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace metaverse {

// Shows the recovered 640x480 artwork-backed login/create-profile window.
// Existing profiles are authenticated; an unknown valid name creates a new
// legacy profile. Closing the window returns false with an empty error.
bool ChoosePlayerProfile(
    HINSTANCE instance,
    HWND owner,
    const std::filesystem::path& assets,
    std::vector<PlayerProfile>& profiles,
    PlayerProfile& selected,
    std::string& error
);

}  // namespace metaverse
