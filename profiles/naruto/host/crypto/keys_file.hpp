#pragma once

// The player's keys file: the console keys this port does not ship, which the
// PGD decryption needs (crypto/pgd.hpp). One key per line, `name = hex`, with
// `#` starting a comment; names are case-insensitive:
//
//   kirk.aes.38 = ...   kirk.aes.39 = ...   kirk.aes.63 = ...
//   amctrl.1CD4 = ...   amctrl.1CE4 = ...   amctrl.1CF4 = ...
//
// Lines that are not a 16-byte key are ignored. The host reads the file named
// by MHP3RD_KEYS_FILE, else keys.txt in the game directory (next to ms0).

#include "crypto/pgd.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace mhp3rd::pgd {

class KeysFile {
public:
    [[nodiscard]] static KeysFile parse(std::string_view text);
    // Empty (found() false) when the file does not exist.
    [[nodiscard]] static KeysFile load(const std::filesystem::path &path);

    [[nodiscard]] bool found() const noexcept { return found_; }
    [[nodiscard]] std::size_t count() const noexcept { return keys_.size(); }
    [[nodiscard]] const Key16 *find(std::string_view name) const;
    // The keys PGD asks for. Refers to this object, which must outlive it.
    [[nodiscard]] KeySource source() const;

private:
    std::map<std::string, Key16> keys_;
    bool found_{};
};

// The keys file the host's I/O uses, read once on first use.
void set_host_keys_path(const std::filesystem::path &path);
[[nodiscard]] const KeysFile &host_keys();

} // namespace mhp3rd::pgd
