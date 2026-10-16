// The C API the Flutter launcher calls (platform/launcher_api.hpp): problem
// codes for files that are not the game, and whether a data folder is set up.
#include "platform/launcher_api.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
int g_failures = 0;
void check(bool condition, const char *what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++g_failures;
}
void write(const fs::path &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary);
    out << bytes;
}
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "ninja-impact-launcher-api-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    char message[512];

    check(ni_check_image((dir / "missing.iso").string().c_str(), message, sizeof message) == NI_IO,
        "a missing file is an I/O problem");
    check(std::strlen(message) > 0, "a problem comes with a message");

    write(dir / "game.cso", std::string("CISO") + std::string(60, '\0'));
    check(ni_check_image((dir / "game.cso").string().c_str(), message, sizeof message) == NI_COMPRESSED,
        "a CSO image is compressed");

    write(dir / "notes.iso", std::string(4096, 'x'));
    check(ni_check_image((dir / "notes.iso").string().c_str(), message, sizeof message) == NI_NOT_PSP,
        "a file that is no disc image is not a PSP image");

    check(ni_check_image((dir / "notes.iso").string().c_str(), message, 1) == NI_NOT_PSP && message[0] == '\0',
        "a one-byte buffer still gets a terminated message");

    write(dir / "keys.txt", "");
    check(ni_check_keys((dir / "notes.iso").string().c_str(), (dir / "keys.txt").string().c_str()) == NI_NOT_PSP,
        "keys are not checked against a file that is no disc image");
    check(ni_check_keys((dir / "notes.iso").string().c_str(), (dir / "none.txt").string().c_str()) == NI_IO,
        "a missing keys file is an I/O problem");

    check(ni_install((dir / "notes.iso").string().c_str(), (dir / "data").string().c_str(), nullptr, message,
              sizeof message) == NI_NOT_PSP,
        "installing from a file that is no disc image fails before writing");
    check(!fs::exists(dir / "data" / "EBOOT.ELF"), "a failed install leaves no executable");

    check(ni_is_installed(dir.string().c_str()) == 0, "an empty folder is not set up");
    write(dir / "EBOOT.ELF", "elf");
    write(dir / "disc.iso", "iso");
    write(dir / "settings.ini", "disc_image=disc.iso\n");
    check(ni_is_installed(dir.string().c_str()) == 1, "executable, image and settings make a set-up folder");
    fs::remove(dir / "disc.iso");
    check(ni_is_installed(dir.string().c_str()) == 0, "without the image it is not set up");

    fs::remove_all(dir);
    if (g_failures != 0) std::printf("%d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
