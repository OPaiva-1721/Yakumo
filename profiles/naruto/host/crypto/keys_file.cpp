#include "crypto/keys_file.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace mhp3rd::pgd {
namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char &c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// 32 hex digits, spaces between them allowed.
std::optional<Key16> parse_key(std::string_view text) {
    std::string digits;
    for (const char c : text) {
        if (c == ' ' || c == '\t') continue;
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
        digits.push_back(c);
    }
    if (digits.size() != 32u) return std::nullopt;
    Key16 key{};
    for (std::size_t i = 0; i < key.size(); ++i)
        key[i] = static_cast<std::uint8_t>(std::strtoul(digits.substr(2 * i, 2).c_str(), nullptr, 16));
    return key;
}

const char *fixed_key_name(int index) {
    switch (index) {
    case 1:
        return "amctrl.1cd4";
    case 2:
        return "amctrl.1ce4";
    case 3:
        return "amctrl.1cf4";
    default:
        return nullptr;
    }
}

std::filesystem::path &host_keys_path() {
    static std::filesystem::path path;
    return path;
}

} // namespace

KeysFile KeysFile::parse(std::string_view text) {
    KeysFile file;
    file.found_ = true;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string name = lower(trim(std::string_view(line).substr(0, equals)));
        if (const auto key = parse_key(trim(std::string_view(line).substr(equals + 1)))) file.keys_[name] = *key;
    }
    return file;
}

KeysFile KeysFile::load(const std::filesystem::path &path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return parse(text.str());
}

const Key16 *KeysFile::find(std::string_view name) const {
    const auto found = keys_.find(lower(name));
    return found != keys_.end() ? &found->second : nullptr;
}

KeySource KeysFile::source() const {
    return KeySource{[this](std::uint8_t slot) -> const Key16 * {
                         char name[16];
                         std::snprintf(name, sizeof(name), "kirk.aes.%02x", slot);
                         return find(name);
                     },
        [this](int index) -> const Key16 * {
            const char *name = fixed_key_name(index);
            return name != nullptr ? find(name) : nullptr;
        }};
}

void set_host_keys_path(const std::filesystem::path &path) {
    host_keys_path() = path;
}

const KeysFile &host_keys() {
    static const KeysFile keys = [] {
        std::filesystem::path path = host_keys_path();
        if (const char *env = std::getenv("MHP3RD_KEYS_FILE"); env != nullptr && *env != '\0') path = env;
        KeysFile file = path.empty() ? KeysFile{} : KeysFile::load(path);
        std::cerr << "[keys] " << (file.found() ? std::to_string(file.count()) + " keys from " : "no keys file at ")
                  << path.string() << "\n";
        return file;
    }();
    return keys;
}

} // namespace mhp3rd::pgd
