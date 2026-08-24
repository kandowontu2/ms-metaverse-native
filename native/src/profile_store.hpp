#pragma once

#include <filesystem>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace metaverse {

constexpr float kNewProfileCredits = 500.0f;
constexpr float kReturningProfileMinimumCredits = 400.0f;
constexpr std::size_t kLegacyCredentialCharacters = 10;

struct PlayerProfile {
    std::string name;
    std::string password;
    float credits = kNewProfileCredits;
    bool guest = false;
};

enum class ProfileLoginDisposition {
    invalid_name,
    guest_password_not_allowed,
    guest,
    demo_password_wrong,
    password_required,
    existing_password_wrong,
    existing_profile,
    new_demo,
    new_profile,
};

inline constexpr std::size_t kNoLegacyProfileIndex =
    std::numeric_limits<std::size_t>::max();

struct ProfileLoginDecision {
    ProfileLoginDisposition disposition = ProfileLoginDisposition::invalid_name;
    std::size_t existing_profile_index = kNoLegacyProfileIndex;
};

PlayerProfile MakeGuestProfile();
ProfileLoginDecision EvaluateProfileLogin(
    const std::vector<PlayerProfile>& profiles,
    std::string_view name,
    std::string_view password
);
bool ValidateNewProfile(
    std::string_view name,
    std::string_view password,
    std::string& error
);
const PlayerProfile* FindProfile(
    const std::vector<PlayerProfile>& profiles,
    std::string_view name
);
PlayerProfile* FindProfile(
    std::vector<PlayerProfile>& profiles,
    std::string_view name
);
bool AuthenticateProfile(const PlayerProfile& profile, std::string_view password);
PlayerProfile ActivateReturningProfile(const PlayerProfile& stored);
PlayerProfile ActivateReturningProfile(
    const PlayerProfile& stored,
    std::string_view submitted_name,
    std::string_view submitted_password
);
bool IsPersistentProfile(const PlayerProfile& profile);

bool ParseLegacyProfiles(
    std::istream& input,
    std::vector<PlayerProfile>& profiles,
    std::string& error
);
bool WriteLegacyProfiles(
    std::ostream& output,
    const std::vector<PlayerProfile>& profiles,
    std::string& error
);
bool LoadLegacyProfiles(
    const std::filesystem::path& path,
    std::vector<PlayerProfile>& profiles,
    std::string& error
);
bool SaveLegacyProfiles(
    const std::filesystem::path& path,
    const std::vector<PlayerProfile>& profiles,
    std::string& error
);
bool SaveLegacyProfile(
    const std::filesystem::path& path,
    const PlayerProfile& profile,
    std::string& error
);

}  // namespace metaverse
