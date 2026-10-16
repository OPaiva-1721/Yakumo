#include "platform/launcher_api.hpp"

#include "install/installer.hpp"
#include "install/user_data.hpp"
#include "platform/utf8_path.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

namespace install = mhp3rd::install;

void put_message(char *out, int size, const std::string &text) {
    if (out == nullptr || size <= 0) return;
    const std::size_t n = std::min(text.size(), static_cast<std::size_t>(size - 1));
    std::memcpy(out, text.data(), n);
    out[n] = '\0';
}

int code_of(const install::InstallError &e) {
    return static_cast<int>(e.problem());
}

} // namespace

NI_EXPORT int ni_check_image(const char *iso_path, char *message, int message_size) {
    put_message(message, message_size, "");
    try {
        (void)install::check_image(mhp3rd::path_from_utf8(iso_path));
        return NI_OK;
    } catch (const install::InstallError &e) {
        put_message(message, message_size, e.what());
        return code_of(e);
    } catch (const std::exception &e) {
        put_message(message, message_size, e.what());
        return NI_OTHER;
    }
}

NI_EXPORT int ni_check_keys(const char *iso_path, const char *keys_path) {
    try {
        std::ifstream in(mhp3rd::path_from_utf8(keys_path), std::ios::binary);
        if (!in) return NI_IO;
        std::ostringstream text;
        text << in.rdbuf();
        return install::keys_unlock_executable(mhp3rd::path_from_utf8(iso_path), text.str()) ? NI_OK
                                                                                            : NI_MISSING_TAG_KEY;
    } catch (const install::InstallError &e) {
        return code_of(e);
    } catch (const std::exception &) {
        return NI_OTHER;
    }
}

NI_EXPORT int ni_install(
    const char *iso_path, const char *data_dir, ni_progress_fn progress, char *message, int message_size) {
    put_message(message, message_size, "");
    try {
        const std::filesystem::path image = mhp3rd::path_from_utf8(iso_path);
        const std::filesystem::path dir = mhp3rd::path_from_utf8(data_dir);
        if (progress != nullptr) progress(NI_STAGE_CHECK, 0u, 1u);
        (void)install::check_image(image);
        if (progress != nullptr) progress(NI_STAGE_CHECK, 1u, 1u);
        install::install(image, install::ImageStorage::Copy, dir,
            [progress](const std::string &, std::uint64_t done, std::uint64_t total) {
                if (progress != nullptr) progress(NI_STAGE_PREPARE, done, total);
            });
        return NI_OK;
    } catch (const install::InstallError &e) {
        put_message(message, message_size, e.what());
        return code_of(e);
    } catch (const std::exception &e) {
        put_message(message, message_size, e.what());
        return NI_OTHER;
    }
}

NI_EXPORT int ni_is_installed(const char *data_dir) {
    std::error_code ec;
    const std::filesystem::path dir = mhp3rd::path_from_utf8(data_dir);
    return std::filesystem::is_regular_file(dir / install::kExecutableFile, ec) &&
            std::filesystem::is_regular_file(dir / install::kCopiedImageFile, ec) &&
            std::filesystem::is_regular_file(dir / install::kSettingsFile, ec)
        ? 1
        : 0;
}
