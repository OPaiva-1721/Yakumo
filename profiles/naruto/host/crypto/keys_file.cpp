#include "crypto/keys_file.hpp"

#include "psprecomp/sha256.hpp"

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

Fingerprints &host_fingerprints() {
    static Fingerprints fingerprints = console_fingerprints();
    return fingerprints;
}

} // namespace

const Fingerprints &console_fingerprints() {
    static const Fingerprints fingerprints{
        {"kirk.aes.38", "b5e2a84dbaaf99015874182a8609494d3c8e38f1837755ee29858c78b986ad17"},
        {"kirk.aes.39", "be3798d2e2a46c0018a4f7e41f5557e6cc2aeb64b0b15ae6b7063a8fa41c8559"},
        {"kirk.aes.63", "16df5079b5e31b07045b9d117c9db8bf9c45e606dbe4a9c71bb763f12d61be83"},
        {"amctrl.1cd4", "dbe5dd77c4a5f3b930b30f520ae71064a4b48161ba43d79252351effc0ecb403"},
        {"amctrl.1ce4", "4aae0892755232421ac3077d844110b0c0334eeadc9cef6093e55a3fb8e56ee4"},
        {"amctrl.1cf4", "e0942366cf4a2142ac1369df3d404198ac8d9bf9df73ccde6e540f2554dbcf4c"},
    };
    return fingerprints;
}

std::string fingerprint(const Key16 &key) {
    return psprecomp::sha256_bytes(std::vector<std::uint8_t>(key.begin(), key.end()));
}

KeysFile KeysFile::parse(std::string_view text, const Fingerprints &expected) {
    KeysFile file;
    file.found_ = true;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string name = lower(trim(std::string_view(line).substr(0, equals)));
        const auto key = parse_key(trim(std::string_view(line).substr(equals + 1)));
        if (!key) {
            if (expected.count(name) != 0u) file.problems_.push_back(name + " is not 32 hex digits");
            continue;
        }
        if (const auto wanted = expected.find(name); wanted != expected.end() && fingerprint(*key) != wanted->second) {
            file.problems_.push_back(name + " is not the right key: its fingerprint does not match");
            continue;
        }
        file.keys_[name] = *key;
    }
    return file;
}

KeysFile KeysFile::load(const std::filesystem::path &path, const Fingerprints &expected) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return parse(text.str(), expected);
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

void set_host_keys_fingerprints(Fingerprints fingerprints) {
    host_fingerprints() = std::move(fingerprints);
}

const KeysFile &host_keys() {
    static const KeysFile keys = [] {
        std::filesystem::path path = host_keys_path();
        if (const char *env = std::getenv("MHP3RD_KEYS_FILE"); env != nullptr && *env != '\0') path = env;
        KeysFile file = path.empty() ? KeysFile{} : KeysFile::load(path, host_fingerprints());
        std::cerr << "[keys] " << (file.found() ? std::to_string(file.count()) + " keys from " : "no keys file at ")
                  << path.string() << "\n";
        for (const std::string &problem : file.problems()) std::cerr << "[keys] " << problem << "\n";
        for (const auto &[name, sha256] : host_fingerprints())
            if (file.found() && file.find(name) == nullptr) std::cerr << "[keys] missing " << name << "\n";
        return file;
    }();
    return keys;
}

} // namespace mhp3rd::pgd
