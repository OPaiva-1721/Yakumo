// Adapted from TeamGDB/PortableKit tests/pgd_tests.cpp (MIT, PR #37), plus
// the keys file reader (crypto/keys_file.hpp).
// PGD containers (crypto/pgd.hpp) with dummy keys made up here: files built
// by the implementation itself decrypt back, random access into them works,
// and a wrong MAC, a wrong version key, another kind of PGD and a missing
// key are refused. No
// console key and no game data.

#include "crypto/keys_file.hpp"
#include "crypto/pgd.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char *what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++g_failures;
}

mhp3rd::pgd::Key16 dummy(std::uint8_t seed) {
    mhp3rd::pgd::Key16 key{};
    for (std::size_t i = 0; i < key.size(); ++i) key[i] = static_cast<std::uint8_t>(seed * 31u + i * 7u + 1u);
    return key;
}

} // namespace

int main() {
    using namespace mhp3rd::pgd;
    static std::map<std::uint8_t, mhp3rd::pgd::Key16> kirk{{0x38, dummy(1)}, {0x39, dummy(2)}, {0x63, dummy(4)}};
    static std::map<int, mhp3rd::pgd::Key16> fixed{{1, dummy(5)}, {2, dummy(6)}, {3, dummy(7)}};
    const KeySource keys{[](std::uint8_t slot) -> const mhp3rd::pgd::Key16 * {
                             const auto it = kirk.find(slot);
                             return it != kirk.end() ? &it->second : nullptr;
                         },
                         [](int index) -> const mhp3rd::pgd::Key16 * {
                             const auto it = fixed.find(index);
                             return it != fixed.end() ? &it->second : nullptr;
                         }};

    std::vector<std::uint8_t> plain(3000);
    for (std::size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<std::uint8_t>(i * 13u + (i >> 8u));
    const Block desc_key = dummy(20), data_key = dummy(21), vkey = dummy(22);

    const auto file = PgdFile::build(plain, desc_key, data_key, vkey, keys);
    check(file.size() == 0x90u + 3u * 0x400u, "three 0x400-byte blocks after the 0x90-byte header");
    const auto raw_reader = [&file](std::uint64_t offset, std::span<std::uint8_t> out) -> std::size_t {
        if (offset >= file.size()) return 0u;
        const std::size_t n = std::min<std::size_t>(out.size(), file.size() - offset);
        std::copy_n(file.begin() + static_cast<std::ptrdiff_t>(offset), n, out.begin());
        return n;
    };
    std::string error;
    auto opened = PgdFile::open(std::span(file.data(), 0x90), vkey, file.size(), keys, error);
    check(opened.has_value(), "a PGD built here opens with its version key and a matching MAC");
    if (opened) {
        check(opened->size() == plain.size() && opened->desc().block_size == 0x400u && opened->desc().data_offset == 0x90u,
              "its description decrypts to the size, block size and data offset");
        std::vector<std::uint8_t> all(plain.size());
        check(opened->read(0, all, raw_reader) == plain.size() && all == plain, "the whole file decrypts back");
        std::vector<std::uint8_t> middle(1500);
        const std::size_t got = opened->read(1000, middle, raw_reader);
        check(got == 1500u && std::equal(middle.begin(), middle.end(), plain.begin() + 1000),
              "a read across a block boundary at a random offset decrypts back");
        std::vector<std::uint8_t> late(40);
        check(opened->read(2100, late, raw_reader) == 40u && std::equal(late.begin(), late.end(), plain.begin() + 2100),
              "the third block alone decrypts back (the stream runs on through the file)");
        std::vector<std::uint8_t> tail(100);
        check(opened->read(2950, tail, raw_reader) == 50u, "a read past the end stops at the data size");
    }

    // The key stream is its own inverse and continuous: two runs of 16 bytes
    // from positions 0 and 1 are the same as one run of 32 from 0.
    std::vector<std::uint8_t> one(32, 0u), two(32, 0u);
    (void)bb_cipher(keys, data_key, vkey, 0u, one);
    (void)bb_cipher(keys, data_key, vkey, 0u, std::span(two.data(), 16));
    (void)bb_cipher(keys, data_key, vkey, 1u, std::span(two.data() + 16, 16));
    check(one == two, "the key stream from position 1 continues the one from 0");

    auto tampered = file;
    tampered[0x44] ^= 1u;
    check(!PgdFile::open(std::span(tampered.data(), 0x90), vkey, tampered.size(), keys, error) &&
              error.find("MAC") != std::string::npos,
          "a changed header fails the MAC check");
    auto bad_mac = file;
    bad_mac[0x7F] ^= 0x80u;
    check(!PgdFile::open(std::span(bad_mac.data(), 0x90), vkey, bad_mac.size(), keys, error) &&
              error.find("MAC") != std::string::npos,
          "a changed MAC is refused");
    check(!PgdFile::open(std::span(file.data(), 0x90), dummy(99), file.size(), keys, error) &&
              error.find("MAC") != std::string::npos,
          "another version key fails the MAC check");
    auto other_kind = file;
    other_kind[0x08] = 2u;
    check(!PgdFile::open(std::span(other_kind.data(), 0x90), vkey, other_kind.size(), keys, error) &&
              error.find("not supported") != std::string::npos,
          "another DRM type is refused as not supported");

    const KeySource missing{[](std::uint8_t) -> const mhp3rd::pgd::Key16 * { return nullptr; },
                            [](int) -> const mhp3rd::pgd::Key16 * { return nullptr; }};
    std::vector<std::uint8_t> header(0x90, 0u);
    header[1] = 'P';
    header[2] = 'G';
    header[3] = 'D';
    header[4] = 1u;
    header[8] = 1u;
    check(!PgdFile::open(header, vkey, 0x10000, missing, error) &&
              error.find("kirk.aes.") != std::string::npos,
          "a missing key is named");
    header[1] = 'X';
    check(!PgdFile::open(header, vkey, 0x10000, keys, error) && error == "not a PGD file",
          "a file without the magic is not PGD");

    // The keys file: names in any case, comments, spaces inside the hex, and
    // lines that are not a 16-byte key ignored; it feeds the same KeySource.
    const auto keys_file = mhp3rd::pgd::KeysFile::parse(
        "# dummy keys\n"
        "KIRK.AES.38 = 20 27 2e 35 3c 43 4a 51 58 5f 66 6d 74 7b 82 89\n"
        "kirk.aes.39=3f464d545b626970777e858c939aa1a8 # trailing comment\n"
        "kirk.aes.63 = 7d848b9299a0a7aeb5bcc3cad1d8dfe6\n"
        "amctrl.1CD4 = 9ca3aab1b8bfc6cdd4dbe2e9f0f7fe05\n"
        "amctrl.1ce4 = bbc2c9d0d7dee5ecf3fa01080f161d24\n"
        "amctrl.1cf4 = dae1e8eff6fd040b121920272e353c43\n"
        "amctrl.short = 0011\n"
        "not a key line\n");
    check(keys_file.found() && keys_file.count() == 6u, "the keys file keeps the six 16-byte keys only");
    check(keys_file.find("kirk.aes.38") != nullptr && *keys_file.find("kirk.aes.38") == dummy(1) &&
              keys_file.find("KIRK.AES.63") != nullptr && *keys_file.find("kirk.aes.63") == dummy(4),
          "keys are found by name in any case");
    const auto from_file = keys_file.source();
    check(from_file.kirk(0x39u) != nullptr && *from_file.kirk(0x39u) == dummy(2) && from_file.kirk(0x40u) == nullptr &&
              from_file.fixed(1) != nullptr && *from_file.fixed(3) == dummy(7) && from_file.fixed(4) == nullptr,
          "the keys file serves KIRK slots and the amctrl keys");
    {
        auto reopened = PgdFile::open(std::span(file.data(), 0x90), vkey, file.size(), from_file, error);
        std::vector<std::uint8_t> all(plain.size());
        check(reopened && reopened->read(0, all, raw_reader) == plain.size() && all == plain,
              "a PGD decrypts with keys read from a keys file");
    }
    check(!mhp3rd::pgd::KeysFile::load("no-such-keys-file.txt").found(), "a missing keys file is reported as absent");

    // Fingerprints: a key whose SHA-256 differs from the expected one is
    // refused and named; the rest load.
    {
        const mhp3rd::pgd::Fingerprints expected{
            {"kirk.aes.38", mhp3rd::pgd::fingerprint(dummy(1))}, {"kirk.aes.39", mhp3rd::pgd::fingerprint(dummy(99))}};
        const auto checked = mhp3rd::pgd::KeysFile::parse("kirk.aes.38 = 20272e353c434a51585f666d747b8289\n"
                                                          "kirk.aes.39 = 3f464d545b626970777e858c939aa1a8\n"
                                                          "kirk.aes.63 = 7d848b9299a0a7aeb5bcc3cad1d8dfe6\n",
            expected);
        check(checked.find("kirk.aes.38") != nullptr && checked.find("kirk.aes.63") != nullptr,
              "keys that match their fingerprint, or have none, load");
        check(checked.find("kirk.aes.39") == nullptr && checked.problems().size() == 1u &&
                  checked.problems()[0].find("kirk.aes.39") != std::string::npos,
              "a key that does not match its fingerprint is refused and named");
    }
    check(mhp3rd::pgd::console_fingerprints().size() == 6u &&
              mhp3rd::pgd::console_fingerprints().count("amctrl.1cf4") == 1u,
          "the console fingerprints cover the six PGD keys");

    if (g_failures != 0) std::printf("%d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
