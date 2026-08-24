#include "profile_store.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

namespace metaverse {
namespace {

constexpr std::size_t kLegacyRecordSize = 28;
constexpr std::size_t kLegacyPasswordOffset = 11;
constexpr std::size_t kLegacyCreditsOffset = 24;

std::string ReadCredential(
    const std::uint8_t* data,
    std::size_t capacity
) {
    const auto* begin = reinterpret_cast<const char*>(data);
    const auto* end = std::find(begin, begin + capacity, '\0');
    return std::string(begin, end);
}

void WriteCredential(
    std::array<std::uint8_t, kLegacyRecordSize>& record,
    std::size_t offset,
    std::string_view value
) {
    const std::size_t count = std::min(value.size(), kLegacyCredentialCharacters);
    std::memcpy(record.data() + offset, value.data(), count);
}

std::uint32_t ReadU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

void WriteU32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value & 0xff);
    data[1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    data[2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    data[3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

bool EncodeLegacyProfile(
    const PlayerProfile& profile,
    std::array<std::uint8_t, kLegacyRecordSize>& record,
    std::string& error
) {
    if (!IsPersistentProfile(profile) || profile.name.empty() ||
        profile.password.empty()) {
        error = "cannot serialize invalid profile: " + profile.name;
        return false;
    }
    record.fill(0);
    WriteCredential(record, 0, profile.name);
    WriteCredential(record, kLegacyPasswordOffset, profile.password);
    WriteU32(
        record.data() + kLegacyCreditsOffset,
        std::bit_cast<std::uint32_t>(profile.credits)
    );
    return true;
}

}  // namespace

PlayerProfile MakeGuestProfile() {
    return {"GUEST", "", kNewProfileCredits, true};
}

ProfileLoginDecision EvaluateProfileLogin(
    const std::vector<PlayerProfile>& profiles,
    std::string_view name,
    std::string_view password
) {
    // This is the exact reachable offline branch order at 0x00402b10-
    // 0x00402da0. In particular, DEMO/BUYVV is checked before the ordinary
    // empty-password rule but a valid DEMO still scans MM.DAT.
    if (name.empty()) {
        return {ProfileLoginDisposition::invalid_name};
    }
    if (name == "GUEST") {
        return {
            password.empty()
                ? ProfileLoginDisposition::guest
                : ProfileLoginDisposition::guest_password_not_allowed
        };
    }
    if (name == "DEMO" && password != "BUYVV") {
        return {ProfileLoginDisposition::demo_password_wrong};
    }
    if (password.empty()) {
        return {ProfileLoginDisposition::password_required};
    }
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        // The offline scan compares each fixed ten-byte record field against
        // CString::Left(10), not against the full edit text. Resource 167 does
        // not impose an edit limit.
        if (profiles[index].name !=
            name.substr(0, kLegacyCredentialCharacters)) {
            continue;
        }
        return {
            profiles[index].password ==
                    password.substr(0, kLegacyCredentialCharacters)
                ? ProfileLoginDisposition::existing_profile
                : ProfileLoginDisposition::existing_password_wrong,
            index
        };
    }
    if (name == "DEMO") {
        return {ProfileLoginDisposition::new_demo};
    }
    return {ProfileLoginDisposition::new_profile};
}

bool ValidateNewProfile(
    std::string_view name,
    std::string_view password,
    std::string& error
) {
    error.clear();
    if (name.empty()) {
        error = "profile name must not be empty";
        return false;
    }
    if (name == "GUEST") {
        error = "GUEST is reserved for the non-persistent guest profile";
        return false;
    }
    if (password.empty()) {
        error = "password must not be empty";
        return false;
    }
    if (name.find('\0') != std::string_view::npos ||
        password.find('\0') != std::string_view::npos) {
        error = "credentials cannot contain a null character";
        return false;
    }
    return true;
}

const PlayerProfile* FindProfile(
    const std::vector<PlayerProfile>& profiles,
    std::string_view name
) {
    const auto found = std::find_if(
        profiles.begin(), profiles.end(),
        [name](const PlayerProfile& profile) { return profile.name == name; }
    );
    return found == profiles.end() ? nullptr : &*found;
}

PlayerProfile* FindProfile(
    std::vector<PlayerProfile>& profiles,
    std::string_view name
) {
    const auto found = std::find_if(
        profiles.begin(), profiles.end(),
        [name](const PlayerProfile& profile) { return profile.name == name; }
    );
    return found == profiles.end() ? nullptr : &*found;
}

bool AuthenticateProfile(const PlayerProfile& profile, std::string_view password) {
    return profile.guest ? password.empty() : profile.password == password;
}

PlayerProfile ActivateReturningProfile(const PlayerProfile& stored) {
    PlayerProfile selected = stored;
    // 0x00402d84 copies the record's four credit bytes verbatim.  The next
    // instruction is a signed integer comparison against float bits
    // 0x43c80000, not an x87/SSE floating-point comparison.  This distinction
    // is observable for negative NaNs and every other sign-bit-set payload.
    const std::int32_t stored_bits = std::bit_cast<std::int32_t>(stored.credits);
    if (stored_bits < static_cast<std::int32_t>(0x43c80000)) {
        selected.credits = kReturningProfileMinimumCredits;
    }
    return selected;
}

PlayerProfile ActivateReturningProfile(
    const PlayerProfile& stored,
    std::string_view submitted_name,
    std::string_view submitted_password
) {
    PlayerProfile selected = ActivateReturningProfile(stored);
    // After authenticating against CString::Left(10), 0x00402da0 copies the
    // complete edit strings into the document. The later save routine applies
    // Left(10) again when it searches and writes MM.DAT.
    selected.name = submitted_name;
    selected.password = submitted_password;
    selected.guest = false;
    return selected;
}

bool IsPersistentProfile(const PlayerProfile& profile) {
    // The final-save guards at 0x00402627-0x004026b6 and
    // 0x00402750-0x00402797 exclude both special login names.
    return !profile.guest && profile.name != "GUEST" && profile.name != "DEMO";
}

bool ParseLegacyProfiles(
    std::istream& input,
    std::vector<PlayerProfile>& profiles,
    std::string& error
) {
    profiles.clear();
    error.clear();
    std::vector<char> raw_bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    if (!input.eof() && input.fail()) {
        error = "failed while reading legacy profile database";
        return false;
    }
    // fcn.0040291f does no database-wide validation. It repeatedly asks CFile
    // for one 28-byte record and treats only a zero-byte read as EOF. Preserve
    // all complete records without rejecting unrelated empty credentials,
    // missing terminators, or non-finite credit bit patterns. A damaged final
    // fragment cannot be represented safely as MM.EXE's uninitialized stack
    // tail, so ignore it as the closest deterministic form of the next EOF.
    const std::size_t complete_bytes =
        raw_bytes.size() - (raw_bytes.size() % kLegacyRecordSize);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(raw_bytes.data());
    for (std::size_t offset = 0; offset < complete_bytes; offset += kLegacyRecordSize) {
        PlayerProfile profile;
        profile.name = ReadCredential(bytes + offset, 11);
        profile.password = ReadCredential(bytes + offset + kLegacyPasswordOffset, 11);
        profile.credits = std::bit_cast<float>(
            ReadU32(bytes + offset + kLegacyCreditsOffset)
        );
        profile.guest = false;
        profiles.push_back(std::move(profile));
    }
    return true;
}

bool WriteLegacyProfiles(
    std::ostream& output,
    const std::vector<PlayerProfile>& profiles,
    std::string& error
) {
    error.clear();
    for (const PlayerProfile& profile : profiles) {
        if (!IsPersistentProfile(profile)) {
            continue;
        }
        std::array<std::uint8_t, kLegacyRecordSize> record{};
        if (!EncodeLegacyProfile(profile, record, error)) {
            return false;
        }
        output.write(
            reinterpret_cast<const char*>(record.data()),
            static_cast<std::streamsize>(record.size())
        );
        if (!output) {
            error = "failed while writing legacy profile database";
            return false;
        }
    }
    return true;
}

bool LoadLegacyProfiles(
    const std::filesystem::path& path,
    std::vector<PlayerProfile>& profiles,
    std::string& error
) {
    if (!std::filesystem::exists(path)) {
        // fcn.0040291f first attempts to open MM.DAT for reading, then retries
        // with CFile::modeCreate when it is absent. This leaves an empty
        // database behind even if the player subsequently chooses GUEST.
        if (!path.parent_path().empty()) {
            std::error_code directory_error;
            std::filesystem::create_directories(path.parent_path(), directory_error);
            if (directory_error) {
                error = "cannot create profile directory: " +
                    directory_error.message();
                return false;
            }
        }
        std::ofstream output(path, std::ios::binary);
        if (!output) {
            error = "cannot create legacy profile database: " + path.string();
            return false;
        }
        profiles.clear();
        error.clear();
        return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open legacy profile database: " + path.string();
        return false;
    }
    return ParseLegacyProfiles(input, profiles, error);
}

bool SaveLegacyProfiles(
    const std::filesystem::path& path,
    const std::vector<PlayerProfile>& profiles,
    std::string& error
) {
    if (!path.parent_path().empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(path.parent_path(), directory_error);
        if (directory_error) {
            error = "cannot create profile directory: " + directory_error.message();
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot create legacy profile database: " + path.string();
        return false;
    }
    return WriteLegacyProfiles(output, profiles, error);
}

bool SaveLegacyProfile(
    const std::filesystem::path& path,
    const PlayerProfile& profile,
    std::string& error
) {
    error.clear();
    std::array<std::uint8_t, kLegacyRecordSize> replacement{};
    if (!EncodeLegacyProfile(profile, replacement, error)) {
        return false;
    }
    if (!path.parent_path().empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(path.parent_path(), directory_error);
        if (directory_error) {
            error = "cannot create profile directory: " + directory_error.message();
            return false;
        }
    }

    if (!std::filesystem::exists(path)) {
        std::ofstream output(path, std::ios::binary);
        if (!output) {
            error = "cannot create legacy profile database: " + path.string();
            return false;
        }
        output.write(
            reinterpret_cast<const char*>(replacement.data()),
            static_cast<std::streamsize>(replacement.size())
        );
        if (!output) {
            error = "failed while appending legacy profile database";
            return false;
        }
        return true;
    }

    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        error = "cannot update legacy profile database: " + path.string();
        return false;
    }
    std::array<std::uint8_t, kLegacyRecordSize> current{};
    std::streamoff replacement_offset = -1;
    std::streamoff offset = 0;
    const std::string match_name =
        profile.name.substr(0, kLegacyCredentialCharacters);
    const std::string match_password =
        profile.password.substr(0, kLegacyCredentialCharacters);
    while (file.read(
        reinterpret_cast<char*>(current.data()),
        static_cast<std::streamsize>(current.size())
    )) {
        // 0x00402f08/0x00402f37 materialize CString::Left(10) before the
        // fixed-record scan. Long resource-edit values therefore still find
        // and replace their truncated legacy record.
        if (ReadCredential(current.data(), 11) == match_name &&
            ReadCredential(current.data() + kLegacyPasswordOffset, 11) ==
                match_password) {
            replacement_offset = offset;
            break;
        }
        offset += static_cast<std::streamoff>(current.size());
    }
    // CFile::Read treats any nonzero short read as another record. Its unused
    // stack tail is undefined, but the ordinary nonmatching outcome is a
    // subsequent zero-byte read followed by append-at-EOF. Preserve the
    // damaged bytes and use that deterministic, memory-safe outcome.
    file.clear();
    if (replacement_offset >= 0) {
        file.seekp(replacement_offset, std::ios::beg);
    } else {
        file.seekp(0, std::ios::end);
    }
    file.write(
        reinterpret_cast<const char*>(replacement.data()),
        static_cast<std::streamsize>(replacement.size())
    );
    if (!file) {
        error = "failed while updating legacy profile database";
        return false;
    }
    return true;
}

}  // namespace metaverse
