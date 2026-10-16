#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace mhp3rd::install {

// Turns PSP_GAME/SYSDIR/EBOOT.BIN of the supported disc into the executable
// the recompiled code was generated from.
//
// This is deliberately not a general tool: it accepts only the one file whose
// SHA-256 is kEncryptedExecutableSha256, handles only the header layout that
// file uses, and checks the result against kExecutableSha256. Anything else is
// refused with psprecomp::Error. Only the installer calls it, with a callback
// that receives the bytes decrypted so far and the total.
//
// The key the file's tag selects is not shipped. The caller passes every key
// in the player's keys file (pgd::all_keys_in); the one that decrypts the
// file is used, and MissingTagKey is thrown when none does.
struct MissingTagKey : std::runtime_error {
    MissingTagKey() : std::runtime_error("the keys file does not hold the key for this EBOOT.BIN (tag 0xD91613F0)") {}
};
[[nodiscard]] std::vector<std::uint8_t> prepare_executable(std::span<const std::uint8_t> eboot_bin,
    std::span<const std::array<std::uint8_t, 16>> tag_key_candidates,
    const std::function<void(std::uint64_t done, std::uint64_t total)> &progress = {});

} // namespace mhp3rd::install
