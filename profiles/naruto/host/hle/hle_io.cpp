// IoFileMgrForUser and sceUmdUser: UMD access straight from the disc image
// (including raw "sce_lbn" sector files) and a host directory for ms0:.
#include "hle_common.hpp"
#include "crypto/keys_file.hpp"
#include "crypto/pgd.hpp"
#include "kernel/fast_loading.hpp"
#include "kernel/load_trace.hpp"
#include "kernel/iso_image.hpp"
#include "platform/utf8_path.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

namespace mhp3rd {
namespace {

namespace io_error {
constexpr std::uint32_t kFileNotFound = 0x80010002u;
constexpr std::uint32_t kBadFileDescriptor = 0x80010009u;
constexpr std::uint32_t kDeviceNotFound = 0x80010013u;
constexpr std::uint32_t kInvalidArgument = 0x80010016u;
constexpr std::uint32_t kReadOnly = 0x8001001Eu;
// No asynchronous operation is pending on the descriptor.
constexpr std::uint32_t kNoAsync = 0x80020329u;
} // namespace io_error

// A 32-bit PSP error code as the signed 64-bit result some calls return.
constexpr std::int64_t wide_error(std::uint32_t code) {
    return static_cast<std::int64_t>(static_cast<std::int32_t>(code));
}

constexpr std::uint32_t kOpenWrite = 0x0002u;
constexpr std::uint32_t kOpenAppend = 0x0100u;
constexpr std::uint32_t kOpenCreate = 0x0200u;
constexpr std::uint32_t kOpenTruncate = 0x0400u;

enum class Device { Disc, MemoryStick, Unknown };

struct OpenFile {
    enum class Kind { Disc, Host, Directory } kind{};
    std::string path;
    std::uint64_t disc_offset{}; // absolute image offset of byte 0
    std::uint64_t size{};
    std::uint64_t position{};
    std::unique_ptr<std::fstream> host;
    // The *Async calls finish their work at once and leave the result here for
    // sceIoWaitAsync/sceIoPollAsync; a closed descriptor lingers until then.
    std::optional<std::int64_t> async_result;
    bool close_pending{};
    // The UMD device opened itself ("umd0:"): reads and seeks count 2048-byte
    // sectors, not bytes. `position` and `size` stay in bytes.
    bool block_device{};
    // Set by the PGD key ioctl: reads return decrypted data, and `position`
    // and `size` count decrypted bytes (crypto/pgd.hpp).
    std::shared_ptr<pgd::PgdFile> pgd;
};

struct IoState {
    std::unique_ptr<IsoImage> disc;
    std::filesystem::path memory_stick;
    std::map<std::uint32_t, OpenFile> files;
    std::uint32_t next_fd{3u};
};

IoState &io() {
    static IoState state;
    return state;
}

struct SplitPath {
    Device device{Device::Unknown};
    std::string path; // without device, '/' separated, no leading '/'
};

SplitPath split_path(const std::string &full) {
    SplitPath result;
    const auto colon = full.find(':');
    std::string device = colon == std::string::npos ? "disc0" : full.substr(0, colon);
    std::transform(device.begin(), device.end(), device.begin(), [](unsigned char c) { return std::tolower(c); });
    if (device == "disc0" || device == "umd0" || device == "umd1" || device == "isofs0")
        result.device = Device::Disc;
    else if (device == "ms0" || device == "fatms0")
        result.device = Device::MemoryStick;

    std::string rest = colon == std::string::npos ? full : full.substr(colon + 1u);
    std::replace(rest.begin(), rest.end(), '\\', '/');
    std::vector<std::string> parts;
    std::size_t start = 0u;
    while (start <= rest.size()) {
        const auto end = rest.find('/', start);
        std::string part = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (end == std::string::npos) break;
        start = end + 1u;
    }
    for (std::size_t i = 0; i < parts.size(); ++i) result.path += (i == 0 ? "" : "/") + parts[i];
    return result;
}

// "sce_lbn0x5e0_size0x1000" -> {lba, size}
std::optional<std::pair<std::uint64_t, std::uint64_t>> parse_lbn_path(const std::string &path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto lbn = lower.find("sce_lbn");
    const auto size = lower.find("_size");
    if (lbn != 0u || size == std::string::npos) return std::nullopt;
    const auto parse = [](const std::string &text) -> std::optional<std::uint64_t> {
        std::string digits = text;
        int base = 10;
        if (digits.starts_with("0x")) {
            digits = digits.substr(2);
            base = 16;
        }
        std::uint64_t value{};
        const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
        if (ec != std::errc{} || ptr != digits.data() + digits.size()) return std::nullopt;
        return value;
    };
    const auto lba = parse(lower.substr(7, size - 7));
    const auto length = parse(lower.substr(size + 5));
    if (!lba || !length) return std::nullopt;
    return std::make_pair(*lba, *length);
}

std::filesystem::path host_path(const std::string &path) {
    return io().memory_stick / path;
}

// The disc folder a Memory Stick folder is linked to, if any (kDiscLinkFile).
std::optional<std::string> disc_link(const std::filesystem::path &folder) {
    std::ifstream in(folder / kDiscLinkFile);
    std::string target;
    if (!in || !std::getline(in, target)) return std::nullopt;
    while (!target.empty() && (target.back() == '\r' || target.back() == ' ')) target.pop_back();
    if (target.empty()) return std::nullopt;
    return target;
}

// A file of a linked Memory Stick folder that is not there, found on the disc.
std::optional<IsoImage::Entry> linked_entry(const std::string &path) {
    if (!io().disc) return std::nullopt;
    const auto slash = path.rfind('/');
    if (slash == std::string::npos) return std::nullopt;
    const std::optional<std::string> target = disc_link(host_path(path.substr(0, slash)));
    if (!target) return std::nullopt;
    std::optional<IsoImage::Entry> entry = io().disc->find(*target + "/" + path.substr(slash + 1u));
    if (!entry || entry->directory) return std::nullopt;
    return entry;
}

bool trace_io() {
    static const bool enabled = std::getenv("MHP3RD_TRACE_IO") != nullptr;
    return enabled;
}

std::int64_t open_file(const std::string &full_path, std::uint32_t flags) {
    const SplitPath split = split_path(full_path);
    OpenFile file;
    file.path = full_path;
    if (split.device == Device::Disc) {
        if (!io().disc) return static_cast<std::int32_t>(io_error::kDeviceNotFound);
        if ((flags & kOpenWrite) != 0u) return static_cast<std::int32_t>(io_error::kReadOnly);
        if (const auto lbn = parse_lbn_path(split.path)) {
            file.kind = OpenFile::Kind::Disc;
            file.disc_offset = lbn->first * IsoImage::kSectorSize;
            file.size = lbn->second;
        } else if (split.path.empty()) {
            // Opening the device itself gives raw sector access. The ISO root
            // also has an entry, so resolve the device before that directory.
            file.kind = OpenFile::Kind::Disc;
            file.size = io().disc->size_bytes();
            // umd0:/umd1:/umd: are block devices counted in sectors; disc0:
            // (the ISO file system) keeps byte offsets.
            std::string device = full_path.substr(0, 3);
            std::transform(device.begin(), device.end(), device.begin(), [](unsigned char c) { return std::tolower(c); });
            file.block_device = device == "umd";
        } else if (const auto entry = io().disc->find(split.path)) {
            file.kind = entry->directory ? OpenFile::Kind::Directory : OpenFile::Kind::Disc;
            file.disc_offset = static_cast<std::uint64_t>(entry->lba) * IsoImage::kSectorSize;
            file.size = entry->size;
        } else {
            return static_cast<std::int32_t>(io_error::kFileNotFound);
        }
    } else if (split.device == Device::MemoryStick) {
        const auto path = host_path(split.path);
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        // Install data left on the disc, read from there.
        if (const auto linked = !exists && (flags & kOpenWrite) == 0u ? linked_entry(split.path) : std::nullopt) {
            file.kind = OpenFile::Kind::Disc;
            file.disc_offset = static_cast<std::uint64_t>(linked->lba) * IsoImage::kSectorSize;
            file.size = linked->size;
            const std::uint32_t fd = io().next_fd++;
            io().files.emplace(fd, std::move(file));
            return fd;
        }
        if (!exists && (flags & kOpenCreate) == 0u) return static_cast<std::int32_t>(io_error::kFileNotFound);
        std::ios::openmode mode = std::ios::binary | std::ios::in;
        if ((flags & kOpenWrite) != 0u) {
            std::filesystem::create_directories(path.parent_path(), ec);
            mode |= std::ios::out;
            if (!exists || (flags & kOpenTruncate) != 0u) mode |= std::ios::trunc;
            if ((flags & kOpenAppend) != 0u) mode |= std::ios::app;
        }
        file.kind = OpenFile::Kind::Host;
        file.host = std::make_unique<std::fstream>(path, mode);
        if (!*file.host) return static_cast<std::int32_t>(io_error::kFileNotFound);
        file.size = std::filesystem::exists(path, ec) ? std::filesystem::file_size(path, ec) : 0u;
    } else {
        return static_cast<std::int32_t>(io_error::kDeviceNotFound);
    }
    const std::uint32_t fd = io().next_fd++;
    io().files.emplace(fd, std::move(file));
    return fd;
}

void write_date_time(psprecomp::GuestMemory &memory, std::uint32_t address, std::time_t time) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    memory.store16(address, static_cast<std::uint16_t>(tm.tm_year + 1900));
    memory.store16(address + 2u, static_cast<std::uint16_t>(tm.tm_mon + 1));
    memory.store16(address + 4u, static_cast<std::uint16_t>(tm.tm_mday));
    memory.store16(address + 6u, static_cast<std::uint16_t>(tm.tm_hour));
    memory.store16(address + 8u, static_cast<std::uint16_t>(tm.tm_min));
    memory.store16(address + 10u, static_cast<std::uint16_t>(tm.tm_sec));
    memory.store32(address + 12u, 0u);
}

// SceIoStat: mode, attr, size(64), ctime, atime, mtime, private[6].
void write_stat(
    psprecomp::GuestMemory &memory, std::uint32_t address, bool directory, std::uint64_t size, std::uint32_t lba) {
    for (std::uint32_t i = 0; i < 88u; ++i) memory.store8(address + i, 0u);
    memory.store32(address, (directory ? 0x1000u : 0x2000u) | 0x1FFu);
    memory.store32(address + 4u, (directory ? 0x10u : 0x20u) | 0x7u);
    store64(memory, address + 8u, size);
    const std::time_t now = std::time(nullptr);
    write_date_time(memory, address + 16u, now);
    write_date_time(memory, address + 32u, now);
    write_date_time(memory, address + 48u, now);
    memory.store32(address + 64u, lba);
}

// Reads a disc file at `offset` into it.
std::size_t read_disc(const OpenFile &file, std::uint64_t offset, std::span<std::uint8_t> out) {
    return io().disc->read(file.disc_offset + offset, out);
}

// sceIoRead and sceIoReadAsync: bytes read into guest memory, or an error.
std::uint32_t read_file(Runtime &rt, std::uint32_t fd, std::uint32_t address, std::uint32_t requested) {
    auto found = io().files.find(fd);
    if (found == io().files.end() || found->second.kind == OpenFile::Kind::Directory || found->second.close_pending)
        return io_error::kBadFileDescriptor;
    OpenFile &file = found->second;
    const std::uint32_t unit = file.block_device ? IsoImage::kSectorSize : 1u;
    requested = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{requested} * unit, 0xFFFFFFFFu));
    std::vector<std::uint8_t> buffer;
    if (file.pgd) {
        const std::uint64_t available = file.position < file.size ? file.size - file.position : 0u;
        buffer.resize(static_cast<std::size_t>(std::min<std::uint64_t>(requested, available)));
        const std::size_t count = file.pgd->read(file.position, buffer,
            [&file](std::uint64_t offset, std::span<std::uint8_t> out) { return read_disc(file, offset, out); });
        buffer.resize(count);
    } else if (file.kind == OpenFile::Kind::Disc) {
        const std::uint64_t available = file.position < file.size ? file.size - file.position : 0u;
        requested = static_cast<std::uint32_t>(std::min<std::uint64_t>(requested, available));
        buffer.resize(requested);
        const std::size_t count = read_disc(file, file.position, buffer);
        buffer.resize(count);
    } else {
        buffer.resize(requested);
        file.host->clear();
        file.host->seekg(static_cast<std::streamoff>(file.position));
        file.host->read(reinterpret_cast<char *>(buffer.data()), requested);
        buffer.resize(static_cast<std::size_t>(file.host->gcount()));
    }
    rt.memory().copy_in(address, buffer);
    // A disc read is what tells a load from play (kernel/fast_loading.hpp).
    if (file.kind == OpenFile::Kind::Disc) {
        load_trace::note_disc_read(buffer.size());
        fast_loading::note_disc_read();
    } else {
        load_trace::note_memory_stick_read(buffer.size());
    }
    if (trace_io())
        std::cerr << "[io] read fd=" << fd << " " << file.path << " offset=" << file.position
                  << " size=" << buffer.size() << " -> " << psprecomp::hex32(address) << "\n";
    file.position += buffer.size();
    return static_cast<std::uint32_t>(buffer.size() / unit);
}

// sceIoLseek and sceIoLseekAsync: the new position, or a widened error.
std::int64_t seek_file(std::uint32_t fd, std::int64_t offset, std::uint32_t whence) {
    auto found = io().files.find(fd);
    if (found == io().files.end() || found->second.close_pending) return wide_error(io_error::kBadFileDescriptor);
    OpenFile &file = found->second;
    const std::int64_t unit = file.block_device ? IsoImage::kSectorSize : 1;
    offset *= unit;
    std::int64_t base = 0;
    switch (whence) {
    case 0u:
        base = 0;
        break;
    case 1u:
        base = static_cast<std::int64_t>(file.position);
        break;
    case 2u:
        base = static_cast<std::int64_t>(file.size);
        break;
    default:
        return wide_error(io_error::kInvalidArgument);
    }
    const std::int64_t target = base + offset;
    if (target < 0) return wide_error(io_error::kInvalidArgument);
    file.position = static_cast<std::uint64_t>(target);
    if (trace_io()) std::cerr << "[io] lseek fd=" << fd << " " << file.path << " -> " << file.position << "\n";
    return target / unit;
}

// The PGD key ioctl: reads the file's PGD header with the 16-byte version key
// at `key_address` and, if it opens, makes the file serve decrypted data.
std::int64_t set_pgd_key(Runtime &rt, OpenFile &file, std::uint32_t key_address, std::uint32_t key_length) {
    constexpr std::uint32_t kIoError = 0x80010005u;
    if (file.kind != OpenFile::Kind::Disc || key_length < 16u) return wide_error(io_error::kInvalidArgument);
    pgd::Block vkey{};
    for (std::size_t i = 0; i < vkey.size(); ++i)
        vkey[i] = rt.memory().load8(key_address + static_cast<std::uint32_t>(i));
    std::array<std::uint8_t, pgd::kHeaderSize> header{};
    if (read_disc(file, 0u, header) != header.size()) return wide_error(kIoError);
    std::string error;
    auto opened = pgd::PgdFile::open(header, vkey, file.size, pgd::host_keys().source(), error);
    if (!opened) {
        std::cerr << "[pgd] " << file.path << ": " << error << "\n";
        return wide_error(kIoError);
    }
    if (trace_io())
        std::cerr << "[pgd] " << file.path << ": data size " << opened->size() << ", blocks of "
                  << opened->desc().block_size << "\n";
    file.size = opened->size();
    file.position = 0u;
    file.pgd = std::make_shared<pgd::PgdFile>(std::move(*opened));
    return 0;
}

// Records the result of an *Async call that already did its work; v0 of the
// call itself is 0, or the error when the descriptor is not open.
void finish_async(AllegrexContext &ctx, std::uint32_t fd, std::int64_t result) {
    auto found = io().files.find(fd);
    if (found == io().files.end()) {
        kernel().finish(ctx, io_error::kBadFileDescriptor);
        return;
    }
    found->second.async_result = result;
    kernel().finish(ctx, 0u);
}

} // namespace

bool link_disc_folder(const std::filesystem::path &folder, const std::string &disc_folder) {
    if (!io().disc) return false;
    const std::optional<IsoImage::Entry> source = io().disc->find(disc_folder);
    if (!source || !source->directory) return false;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) return false;
    // The icons and other small files are copied; the data stays on the disc.
    constexpr std::uint64_t kCopiedAtMost = 1u << 20u;
    for (const std::string &name : io().disc->list(disc_folder)) {
        const std::optional<IsoImage::Entry> entry = io().disc->find(disc_folder + "/" + name);
        if (!entry || entry->directory || entry->size > kCopiedAtMost) continue;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(entry->size));
        if (io().disc->read(static_cast<std::uint64_t>(entry->lba) * IsoImage::kSectorSize, bytes) != bytes.size())
            return false;
        std::ofstream out(folder / path_from_utf8(name), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    std::ofstream link(folder / kDiscLinkFile, std::ios::trunc);
    link << disc_folder << "\n";
    return static_cast<bool>(link);
}

std::vector<std::pair<std::string, std::uint64_t>> linked_disc_files(const std::filesystem::path &folder) {
    std::vector<std::pair<std::string, std::uint64_t>> files;
    const std::optional<std::string> target = disc_link(folder);
    if (!target || !io().disc) return files;
    std::error_code ec;
    for (const std::string &name : io().disc->list(*target)) {
        const std::optional<IsoImage::Entry> entry = io().disc->find(*target + "/" + name);
        if (!entry || entry->directory || std::filesystem::exists(folder / path_from_utf8(name), ec)) continue;
        files.emplace_back(name, entry->size);
    }
    return files;
}

std::size_t read_open_file(std::uint32_t fd, std::uint64_t offset, std::uint8_t *output, std::size_t size) {
    const auto found = io().files.find(fd);
    if (found == io().files.end()) return 0u;
    OpenFile &file = found->second;
    const std::span<std::uint8_t> out(output, size);
    if (file.kind == OpenFile::Kind::Disc) {
        if (!io().disc || offset >= file.size) return 0u;
        return read_disc(file, offset, out.first(std::min<std::uint64_t>(size, file.size - offset)));
    }
    if (file.kind == OpenFile::Kind::Host) {
        file.host->clear();
        file.host->seekg(static_cast<std::streamoff>(offset));
        file.host->read(reinterpret_cast<char *>(output), static_cast<std::streamsize>(size));
        const auto count = static_cast<std::size_t>(file.host->gcount());
        file.host->clear();
        return count;
    }
    return 0u;
}

void register_io(
    HleRegistrar &hle, const std::filesystem::path &disc_image, const std::filesystem::path &memory_stick) {
    if (!disc_image.empty()) io().disc = std::make_unique<IsoImage>(disc_image);
    io().memory_stick = memory_stick;
    // The player's keys (crypto/keys_file.hpp) sit in the game directory.
    pgd::set_host_keys_path(memory_stick.parent_path() / "keys.txt");

    hle.add("IoFileMgrForUser", "sceIoOpen", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string path = read_cstring(rt.memory(), arg(ctx, 0), 256u);
        const std::int64_t fd = open_file(path, arg(ctx, 1));
        if (trace_io() || fd < 0)
            std::cerr << "[io] open " << path << " flags=" << psprecomp::hex32(arg(ctx, 1)) << " -> "
                      << psprecomp::hex32(static_cast<std::uint32_t>(fd)) << "\n";
        kernel().finish(ctx, static_cast<std::uint32_t>(fd));
    });
    hle.add("IoFileMgrForUser", "sceIoClose", [](Runtime &, AllegrexContext &ctx) {
        kernel().finish(ctx, io().files.erase(arg(ctx, 0)) != 0u ? 0u : io_error::kBadFileDescriptor);
    });
    hle.add("IoFileMgrForUser", "sceIoRead", [](Runtime &rt, AllegrexContext &ctx) {
        kernel().finish(ctx, read_file(rt, arg(ctx, 0), arg(ctx, 1), arg(ctx, 2)));
    });
    hle.add("IoFileMgrForUser", "sceIoWrite", [](Runtime &rt, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        const std::uint32_t address = arg(ctx, 1);
        const std::uint32_t size = arg(ctx, 2);
        if (fd == 1u || fd == 2u) {
            std::string text(size, '\0');
            for (std::uint32_t i = 0; i < size; ++i) text[i] = static_cast<char>(rt.memory().load8(address + i));
            std::cerr << "[guest] " << text;
            kernel().finish(ctx, size);
            return;
        }
        auto found = io().files.find(fd);
        if (found == io().files.end() || found->second.kind != OpenFile::Kind::Host) {
            kernel().finish(ctx, io_error::kBadFileDescriptor);
            return;
        }
        std::vector<char> data(size);
        for (std::uint32_t i = 0; i < size; ++i) data[i] = static_cast<char>(rt.memory().load8(address + i));
        OpenFile &file = found->second;
        file.host->clear();
        file.host->seekp(static_cast<std::streamoff>(file.position));
        file.host->write(data.data(), size);
        file.host->flush();
        file.position += size;
        file.size = std::max(file.size, file.position);
        kernel().finish(ctx, size);
    });
    hle.add("IoFileMgrForUser", "sceIoLseek", [](Runtime &, AllegrexContext &ctx) {
        kernel().finish64(ctx, seek_file(arg(ctx, 0), static_cast<std::int64_t>(arg64(ctx, 2)), arg(ctx, 4)));
    });

    // Asynchronous I/O. Each *Async call does its work at once and keeps the
    // result for sceIoWaitAsync(CB)/sceIoPollAsync, which hand it over once.
    hle.add("IoFileMgrForUser", "sceIoOpenAsync", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string path = read_cstring(rt.memory(), arg(ctx, 0), 256u);
        const std::int64_t fd = open_file(path, arg(ctx, 1));
        if (trace_io() || fd < 0)
            std::cerr << "[io] open-async " << path << " flags=" << psprecomp::hex32(arg(ctx, 1)) << " -> "
                      << psprecomp::hex32(static_cast<std::uint32_t>(fd)) << "\n";
        if (fd < 0) {
            kernel().finish(ctx, static_cast<std::uint32_t>(fd));
            return;
        }
        io().files.at(static_cast<std::uint32_t>(fd)).async_result = fd;
        kernel().finish(ctx, static_cast<std::uint32_t>(fd));
    });
    hle.add("IoFileMgrForUser", "sceIoReadAsync", [](Runtime &rt, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        const std::uint32_t result = read_file(rt, fd, arg(ctx, 1), arg(ctx, 2));
        if (result == io_error::kBadFileDescriptor) {
            kernel().finish(ctx, result);
            return;
        }
        finish_async(ctx, fd, wide_error(result));
    });
    hle.add("IoFileMgrForUser", "sceIoLseekAsync", [](Runtime &, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        const std::int64_t result = seek_file(fd, static_cast<std::int64_t>(arg64(ctx, 2)), arg(ctx, 4));
        if (result == wide_error(io_error::kBadFileDescriptor)) {
            kernel().finish(ctx, io_error::kBadFileDescriptor);
            return;
        }
        finish_async(ctx, fd, result);
    });
    // 0x04100001 gives a PGD file its key (crypto/pgd.hpp); no other command
    // changes what this host serves (UMD seek/cache hints), so each completes
    // with 0.
    hle.add("IoFileMgrForUser", "sceIoIoctlAsync", [](Runtime &rt, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        const auto found = io().files.find(fd);
        if (found == io().files.end() || found->second.close_pending) {
            kernel().finish(ctx, io_error::kBadFileDescriptor);
            return;
        }
        const std::uint32_t command = arg(ctx, 1);
        log_once("ioctl:" + psprecomp::hex32(command), "[io] ioctl-async command " + psprecomp::hex32(command));
        const std::int64_t result =
            command == pgd::kIoctlSetKey ? set_pgd_key(rt, found->second, arg(ctx, 2), arg(ctx, 3)) : 0;
        finish_async(ctx, fd, result);
    });
    hle.add("IoFileMgrForUser", "sceIoCloseAsync", [](Runtime &, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        const auto found = io().files.find(fd);
        if (found == io().files.end() || found->second.close_pending) {
            kernel().finish(ctx, io_error::kBadFileDescriptor);
            return;
        }
        found->second.close_pending = true;
        finish_async(ctx, fd, 0);
    });
    // Hands over the pending result as a 64-bit value; a closed descriptor
    // goes away with it.
    const auto take_async_result = [](Runtime &rt, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        const auto found = io().files.find(fd);
        if (found == io().files.end()) {
            kernel().finish(ctx, io_error::kBadFileDescriptor);
            return;
        }
        if (!found->second.async_result) {
            kernel().finish(ctx, io_error::kNoAsync);
            return;
        }
        store64(rt.memory(), arg(ctx, 1), static_cast<std::uint64_t>(*found->second.async_result));
        found->second.async_result.reset();
        if (found->second.close_pending) io().files.erase(found);
        kernel().finish(ctx, 0u);
    };
    hle.add("IoFileMgrForUser", "sceIoWaitAsync", take_async_result);
    hle.add("IoFileMgrForUser", "sceIoPollAsync", take_async_result);
    hle.add("IoFileMgrForUser", "sceIoWaitAsyncCB", [take_async_result](Runtime &rt, AllegrexContext &ctx) {
        (void)kernel().deliver_callbacks();
        take_async_result(rt, ctx);
    });
    hle.add("IoFileMgrForUser", "sceIoGetstat", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string path = read_cstring(rt.memory(), arg(ctx, 0), 256u);
        const SplitPath split = split_path(path);
        std::uint32_t result = io_error::kFileNotFound;
        if (split.device == Device::Disc && io().disc) {
            if (const auto entry = io().disc->find(split.path)) {
                write_stat(rt.memory(), arg(ctx, 1), entry->directory, entry->size, entry->lba);
                result = 0u;
            }
        } else if (split.device == Device::MemoryStick) {
            std::error_code ec;
            const auto host = host_path(split.path);
            if (std::filesystem::exists(host, ec)) {
                const bool directory = std::filesystem::is_directory(host, ec);
                write_stat(
                    rt.memory(), arg(ctx, 1), directory, directory ? 0u : std::filesystem::file_size(host, ec), 0u);
                result = 0u;
            } else if (const auto linked = linked_entry(split.path)) {
                write_stat(rt.memory(), arg(ctx, 1), false, linked->size, linked->lba);
                result = 0u;
            }
        }
        if (trace_io() || result != 0u)
            std::cerr << "[io] getstat " << path << " -> " << psprecomp::hex32(result) << "\n";
        kernel().finish(ctx, result);
    });
    hle.add("IoFileMgrForUser", "sceIoDopen", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string path = read_cstring(rt.memory(), arg(ctx, 0), 256u);
        const SplitPath split = split_path(path);
        bool exists = false;
        if (split.device == Device::Disc && io().disc) {
            const auto entry = io().disc->find(split.path);
            exists = split.path.empty() || (entry && entry->directory);
        } else if (split.device == Device::MemoryStick) {
            std::error_code ec;
            exists = std::filesystem::is_directory(host_path(split.path), ec);
        }
        if (!exists) {
            std::cerr << "[io] dopen " << path << " -> not found\n";
            kernel().finish(ctx, io_error::kFileNotFound);
            return;
        }
        OpenFile file;
        file.kind = OpenFile::Kind::Directory;
        file.path = path;
        const std::uint32_t fd = io().next_fd++;
        io().files.emplace(fd, std::move(file));
        kernel().finish(ctx, fd);
    });
    hle.add("IoFileMgrForUser", "sceIoDclose", [](Runtime &, AllegrexContext &ctx) {
        kernel().finish(ctx, io().files.erase(arg(ctx, 0)) != 0u ? 0u : io_error::kBadFileDescriptor);
    });
    hle.add("IoFileMgrForUser", "sceIoRename", [](Runtime &rt, AllegrexContext &ctx) {
        const SplitPath from = split_path(read_cstring(rt.memory(), arg(ctx, 0), 256u));
        const SplitPath to = split_path(read_cstring(rt.memory(), arg(ctx, 1), 256u));
        if (from.device != Device::MemoryStick || to.device != Device::MemoryStick) {
            kernel().finish(ctx, io_error::kReadOnly);
            return;
        }
        std::error_code ec;
        std::filesystem::rename(host_path(from.path), host_path(to.path), ec);
        kernel().finish(ctx, ec ? io_error::kFileNotFound : 0u);
    });
    hle.add("IoFileMgrForUser", "sceIoDevctl", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string device = read_cstring(rt.memory(), arg(ctx, 0), 64u);
        const std::uint32_t command = arg(ctx, 1);
        const std::uint32_t input = arg(ctx, 2);
        const std::uint32_t output = arg(ctx, 4);
        const std::uint32_t output_length = arg(ctx, 5);
        auto &memory = rt.memory();
        switch (command) {
        case 0x02015804u:   // register memory stick insert/eject callback (ms0:)
        case 0x02415821u: { // the same for fatms0:
            // The game waits for the first notification before it continues, and
            // refuses to save while it has not been told a card is inserted, so
            // report the card as present right away.
            const std::uint32_t callback = input != 0u ? memory.load32(input) : 0u;
            if (callback != 0u) kernel().notify_callback(static_cast<SceUID>(callback), 1u);
            break;
        }
        case 0x02015805u: // unregister
        case 0x02415822u:
            break;
        case 0x02025801u: // memory stick state: 4 = inserted and ready
        case 0x02025806u: // memory stick inserted
            if (output != 0u && output_length >= 4u) memory.store32(output, command == 0x02025801u ? 4u : 1u);
            break;
        case 0x02425823u: // fatms inserted
            if (output != 0u && output_length >= 4u) memory.store32(output, 1u);
            break;
        case 0x02425818u: { // free space: input holds a pointer to the info block
            const std::uint32_t info = input != 0u ? memory.load32(input) : 0u;
            if (info != 0u) {
                constexpr std::uint32_t kSectorSize = 0x200u;
                constexpr std::uint32_t kSectorsPerCluster = 0x20u;
                constexpr std::uint32_t kFreeClusters = 0x100000u / 0x10u; // ~1 GiB free
                memory.store32(info, kFreeClusters * 2u);
                memory.store32(info + 4u, kFreeClusters);
                memory.store32(info + 8u, kFreeClusters);
                memory.store32(info + 12u, kSectorSize);
                memory.store32(info + 16u, kSectorsPerCluster);
            }
            break;
        }
        default:
            log_once("devctl:" + device + psprecomp::hex32(command),
                "[io] devctl " + device + " cmd=" + psprecomp::hex32(command) + " (unhandled, returning 0)");
            break;
        }
        kernel().finish(ctx, 0u);
    });

    hle.add("sceUmdUser", "sceUmdActivate", [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); });
    hle.add("sceUmdUser", "sceUmdGetDriveStat", [](Runtime &, AllegrexContext &ctx) {
        constexpr std::uint32_t kPresent = 0x02u;
        constexpr std::uint32_t kReady = 0x10u;
        constexpr std::uint32_t kReadable = 0x20u;
        kernel().finish(ctx, io().disc ? (kPresent | kReady | kReadable) : 0x01u);
    });
    hle.add("sceUmdUser", "sceUmdGetErrorStat", [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); });
    hle.add("sceUmdUser", "sceUmdCheckMedium",
        [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, io().disc ? 1u : 0u); });
    // The disc never changes while the game runs, so the callback is accepted
    // and never notified.
    hle.add(
        "sceUmdUser", "sceUmdRegisterUMDCallBack", [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); });
    // A present disc is already ready and readable; any state a game waits for
    // (0x10 ready, 0x20 readable, 0x02 present) holds at once.
    const auto wait_drive_stat = [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); };
    hle.add("sceUmdUser", "sceUmdWaitDriveStat", wait_drive_stat);
    hle.add("sceUmdUser", "sceUmdWaitDriveStatCB", [wait_drive_stat](Runtime &rt, AllegrexContext &ctx) {
        (void)kernel().deliver_callbacks();
        wait_drive_stat(rt, ctx);
    });
}

} // namespace mhp3rd
