#pragma once

// The C API the Flutter launcher calls through dart:ffi (launcher/lib/native/
// ffi_game_bridge.dart in naruto-impact-recomp). Plain C types only; one call
// at a time, from any thread. Paths are UTF-8.

#include <cstdint>

#if defined(_WIN32)
#define NI_EXPORT extern "C"
#else
#define NI_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Results. The problem codes match install::Problem.
#define NI_OK 0
#define NI_OTHER 1
#define NI_WRONG_GAME 2
#define NI_COMPRESSED 3
#define NI_NOT_PSP 4
#define NI_MISSING_TAG_KEY 5
#define NI_NO_SPACE 6
#define NI_IO 7

// Installation stages reported to the progress callback.
#define NI_STAGE_CHECK 1
#define NI_STAGE_PREPARE 2

using ni_progress_fn = void (*)(int stage, std::uint64_t done, std::uint64_t total);

// Checks that the file is the supported disc image. On a problem, writes a
// message (English, for the log) into `message`, always NUL-terminated.
NI_EXPORT int ni_check_image(const char *iso_path, char *message, int message_size);
// NI_OK when the keys file unlocks the image's executable, NI_MISSING_TAG_KEY
// when it does not, another code when the image or keys cannot be read.
NI_EXPORT int ni_check_keys(const char *iso_path, const char *keys_path);
// Prepares the game from the image (copied into `data_dir` when it is not
// there yet) with the keys.txt or EBOOT.ELF already in `data_dir`, and writes
// settings.ini. `progress` may be null.
NI_EXPORT int ni_install(const char *iso_path, const char *data_dir, ni_progress_fn progress, char *message,
    int message_size);
// 1 when `data_dir` holds a prepared game (executable, image and settings).
NI_EXPORT int ni_is_installed(const char *data_dir);
