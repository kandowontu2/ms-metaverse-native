#include "legacy_dialog.hpp"

namespace metaverse {

std::wstring LegacyBitmapReadFailureMessage(std::wstring_view filename) {
    if (filename.empty()) {
        return kLegacyBitmapReadFailure;
    }
    return L"Can't read bitmap file '" + std::wstring(filename) + L"'!";
}

}  // namespace metaverse
