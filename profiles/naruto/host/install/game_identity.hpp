#pragma once

// Identity of the one release this profile supports: the European UMD of
// Naruto Shippuden: Ultimate Ninja Impact, disc version 1.01.
namespace mhp3rd::install {

// DISC_ID in PSP_GAME/PARAM.SFO.
inline constexpr const char *kDiscId = "ULES01537";
inline constexpr const char *kDiscIdDisplay = "ULES-01537";
inline constexpr const char *kGameTitle = "Naruto Shippuden: Ultimate Ninja Impact";

inline constexpr const char *kExecutablePathOnDisc = "PSP_GAME/SYSDIR/EBOOT.BIN";
inline constexpr const char *kParamSfoPathOnDisc = "PSP_GAME/PARAM.SFO";

// SHA-256 of PSP_GAME/SYSDIR/EBOOT.BIN as it is on the disc.
inline constexpr const char *kEncryptedExecutableSha256 =
    "6f1b38f6ee9593ffc1f6db3b45417318048783c655f5c9b7bb33ef07e3e1414b";
// SHA-256 of the decrypted executable the recompiled code was generated from
// (PPSSPP's "dump decrypted EBOOT.BIN" output; the built-in installer cannot
// decrypt this title's tag yet).
inline constexpr const char *kExecutableSha256 = "8edc4c36331eeb1b2ee1d796ac2b3d77fbfdc87f570cae8b121a8cec449f5ede";

} // namespace mhp3rd::install
