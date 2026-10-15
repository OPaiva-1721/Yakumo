#pragma once

// The player's keys file: the console keys this port does not ship, which the
// PGD decryption needs (crypto/pgd.hpp). One key per line, `name = hex`, with
// `#` starting a comment; names are case-insensitive:
//
//   kirk.aes.38 = ...   kirk.aes.39 = ...   kirk.aes.63 = ...
//   amctrl.1CD4 = ...   amctrl.1CE4 = ...   amctrl.1CF4 = ...
//
// Lines that are not a 16-byte key are ignored. The host reads the file named
// by MHP3RD_KEYS_FILE, else keys.txt in the game directory (next to ms0), and
// checks each key against its SHA-256 fingerprint, which identifies the right
// value without containing it.

#include "crypto/pgd.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mhp3rd::pgd {

// Expected SHA-256 (lower-case hex) of each key's 16 bytes, by lower-case name.
using Fingerprints = std::map<std::string, std::string>;

// The fingerprints of the six console keys PGD uses, as TeamGDB/PortableKit
// publishes them (apps/portablekit/src/keys_file.cpp).
[[nodiscard]] const Fingerprints &console_fingerprints();
[[nodiscard]] std::string fingerprint(const Key16 &key);

class KeysFile {
public:
    // A key named in `expected` whose fingerprint differs is refused and
    // reported in problems(); keys not named there load unchecked.
    [[nodiscard]] static KeysFile parse(std::string_view text, const Fingerprints &expected = {});
    // Empty (found() false) when the file does not exist.
    [[nodiscard]] static KeysFile load(const std::filesystem::path &path, const Fingerprints &expected = {});

    [[nodiscard]] bool found() const noexcept { return found_; }
    [[nodiscard]] std::size_t count() const noexcept { return keys_.size(); }
    [[nodiscard]] const std::vector<std::string> &problems() const noexcept { return problems_; }
    [[nodiscard]] const Key16 *find(std::string_view name) const;
    // The keys PGD asks for. Refers to this object, which must outlive it.
    [[nodiscard]] KeySource source() const;

private:
    std::map<std::string, Key16> keys_;
    std::vector<std::string> problems_;
    bool found_{};
};

// The keys file the host's I/O uses, read once on first use and checked
// against console_fingerprints() unless a test replaces them first.
void set_host_keys_path(const std::filesystem::path &path);
void set_host_keys_fingerprints(Fingerprints fingerprints);
[[nodiscard]] const KeysFile &host_keys();

} // namespace mhp3rd::pgd
