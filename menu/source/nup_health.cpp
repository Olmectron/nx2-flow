// SPDX-License-Identifier: GPL-2.0-or-later

#include "nup_health.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sys/stat.h>

namespace Nup {
namespace {

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr std::size_t kSuperblockSize = 0x80;
constexpr std::size_t kTocHeaderSize = 0x30;
constexpr std::size_t kPackageRowSize = 0x40;
constexpr std::size_t kDirRowSize = 0x18;
constexpr std::size_t kFileRowSize = 0x50;
constexpr u32 kMaxPackages = 64;
constexpr u32 kNoneIndex = 0xFFFFFFFFu;
constexpr std::size_t kChunkEntries = 512;
constexpr std::size_t kMaxNameDisplay = 128;

u32 Rd32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

u64 Rd64(const u8* p) {
    return static_cast<u64>(Rd32(p)) | (static_cast<u64>(Rd32(p + 4)) << 32);
}

u64 SaturatingAdd(u64 a, u64 b) {
    const u64 max_u64 = std::numeric_limits<u64>::max();
    return a > max_u64 - b ? max_u64 : a + b;
}

bool SeekAndRead(std::FILE* f, u64 offset, void* dst, std::size_t size) {
    if (size == 0) {
        return true;
    }
#if defined(_WIN32) && !defined(__CYGWIN__)
    if (_fseeki64(f, static_cast<long long>(offset), SEEK_SET) != 0) {
#else
    if (fseeko(f, static_cast<off_t>(offset), SEEK_SET) != 0) {
#endif
        return false;
    }
    return std::fread(dst, 1, size, f) == size;
}

std::string Plural(u64 n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string SanitizeName(const char* raw, std::size_t len) {
    std::string out;
    out.reserve(len);
    for (std::size_t i = 0; i < len; ++i) {
        const unsigned char ch = static_cast<unsigned char>(raw[i]);
        if (ch == 0) {
            break;
        }
        if (ch < 0x20 || ch == 0x7F) {
            out.push_back(' ');
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    // Trim trailing invalid partial UTF-8 bytes if we truncated mid-codepoint.
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
        out.pop_back();
    }
    if (!out.empty()) {
        const unsigned char last = static_cast<unsigned char>(out.back());
        if ((last & 0xE0) == 0xC0 || (last & 0xF0) == 0xE0 || (last & 0xF8) == 0xF0) {
            out.pop_back();
        }
    }
    return out;
}

struct LivePart {
    std::string path;
    u64 offset{};
    u64 size{};
};

/// Reads arbitrary byte ranges across a whole or multi-part NUP container while keeping
/// at most one FILE handle open at a time.
class PartReader {
public:
    explicit PartReader(const std::vector<LivePart>& parts) : parts_{parts} {}
    ~PartReader() {
        Close();
    }
    PartReader(const PartReader&) = delete;
    PartReader& operator=(const PartReader&) = delete;

    bool ReadAt(u64 at, void* dst, std::size_t size) {
        if (size == 0) {
            return true;
        }
        if (parts_.empty()) {
            return false;
        }
        u8* out = static_cast<u8*>(dst);
        std::size_t idx = FindPart(at);
        while (size > 0) {
            if (idx >= parts_.size()) {
                return false;
            }
            const LivePart& p = parts_[idx];
            if (at < p.offset || at >= p.offset + p.size) {
                ++idx;
                continue;
            }
            if (!OpenPart(idx)) {
                return false;
            }
            const u64 in_part = at - p.offset;
            const u64 avail = p.size - in_part;
            const std::size_t take = static_cast<std::size_t>(std::min<u64>(size, avail));
            if (!SeekAndRead(file_, in_part, out, take)) {
                return false;
            }
            out += take;
            at += take;
            size -= take;
            if (in_part + take == p.size) {
                ++idx;
            }
        }
        return true;
    }

private:
    std::size_t FindPart(u64 at) const {
        if (open_idx_ < parts_.size()) {
            const LivePart& cur = parts_[open_idx_];
            if (at >= cur.offset && at < cur.offset + cur.size) {
                return open_idx_;
            }
        }
        std::size_t lo = 0;
        std::size_t hi = parts_.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (at < parts_[mid].offset + parts_[mid].size) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    bool OpenPart(std::size_t idx) {
        if (idx == open_idx_ && file_) {
            return true;
        }
        Close();
        file_ = std::fopen(parts_[idx].path.c_str(), "rb");
        if (!file_) {
            return false;
        }
        open_idx_ = idx;
        return true;
    }

    void Close() {
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
        open_idx_ = std::numeric_limits<std::size_t>::max();
    }

    const std::vector<LivePart>& parts_;
    std::FILE* file_{nullptr};
    std::size_t open_idx_{std::numeric_limits<std::size_t>::max()};
};

void MarkError(HealthReport& r, std::string summary, std::string issue, std::string advice) {
    r.status = HealthStatus::Error;
    if (r.summary.empty()) {
        r.summary = std::move(summary);
    }
    if (!issue.empty()) {
        r.issues.push_back(std::move(issue));
    }
    if (r.advice.empty() && !advice.empty()) {
        r.advice = std::move(advice);
    }
}

} // namespace

std::string FormatBytes(std::uint64_t bytes) {
    char text[32];
    if (bytes >= (1ull << 30)) {
        std::snprintf(text, sizeof text, "%.1f GB", static_cast<double>(bytes) / static_cast<double>(1ull << 30));
    } else if (bytes >= (1ull << 20)) {
        std::snprintf(text, sizeof text, "%llu MB", static_cast<unsigned long long>(bytes >> 20));
    } else if (bytes >= 1024ull) {
        std::snprintf(text, sizeof text, "%llu KB", static_cast<unsigned long long>((bytes + 1023ull) >> 10));
    } else {
        std::snprintf(text, sizeof text, "%llu B", static_cast<unsigned long long>(bytes));
    }
    return text;
}

HealthReport CheckHealth(const std::vector<Part>& parts, std::uint64_t zstd_warn_bytes) {
    HealthReport r;
    if (parts.empty()) {
        MarkError(r, "No container files", "No container files were given to inspect.",
                  "Rescan the games folder from Settings.");
        return r;
    }

    // 1. Stat every part live on disk so truncated or removed parts are detected immediately.
    std::vector<LivePart> live;
    live.reserve(parts.size());
    u64 offset = 0;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        struct stat st{};
        if (parts[k].path.empty() || stat(parts[k].path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            const std::string label = parts.size() > 1 ? "Part " + std::to_string(k + 1) : "The NUP file";
            MarkError(r, label + " cannot be opened",
                      label + " is missing or unreadable on the card.",
                      "Check that all files of this container are present on the card, or transfer it again.");
            return r;
        }
        const u64 sz = st.st_size > 0 ? static_cast<u64>(st.st_size) : 0ull;
        if (offset > std::numeric_limits<u64>::max() - sz) {
            MarkError(r, "Container size overflows", "Combined part sizes exceed 64-bit range.",
                      "Verify the container files on the card.");
            return r;
        }
        live.push_back({parts[k].path, offset, sz});
        offset += sz;
    }

    r.parts = static_cast<int>(live.size());
    r.total_size = offset;

    if (live.size() > 1) {
        if (live[0].size == 0) {
            MarkError(r, "Part 1 is empty", "The first split part is 0 bytes.",
                      "Re-split or re-transfer the container.");
            return r;
        }
        for (std::size_t k = 1; k + 1 < live.size(); ++k) {
            if (live[k].size != live[0].size) {
                r.mismatched_part = static_cast<int>(k + 1);
                break;
            }
        }
    }

    PartReader reader{live};

    // 2. Superblock (0x80 bytes at offset 0).
    u8 head[kSuperblockSize];
    if (r.total_size < kSuperblockSize || !reader.ReadAt(0, head, sizeof head)) {
        MarkError(r, "Container is too short",
                  "The file is smaller than a NUP header (" + FormatBytes(r.total_size) + ").",
                  "The copy was likely cut short; transfer the NUP file again.");
        return r;
    }
    if (std::memcmp(head, "NUP\x01", 4) != 0) {
        MarkError(r, "Not a NUP container",
                  "The file does not begin with a NUP v1 header.",
                  "Only NUP containers can be started by nx2-flow.");
        return r;
    }

    r.toc_at = Rd64(head + 0x08);
    r.toc_size = Rd64(head + 0x10);
    const u32 head_packages = Rd32(head + 0x48);

    if (r.toc_size < kTocHeaderSize || r.toc_size > kMaxTocBytes) {
        MarkError(r, "Invalid table of contents size",
                  "Table of contents size (" + FormatBytes(r.toc_size) + ") is outside valid bounds.",
                  "The container header looks damaged; rebuild or re-transfer the NUP file.");
        return r;
    }

    const u64 toc_end = SaturatingAdd(r.toc_at, r.toc_size);
    r.furthest_data = std::max(r.furthest_data, toc_end);
    if (r.toc_at > r.total_size || r.toc_size > r.total_size - r.toc_at) {
        r.missing_bytes = toc_end > r.total_size ? toc_end - r.total_size : 1;
        const std::string part_hint =
            r.parts > 1 ? "A split part is missing or was not copied whole."
                        : "The copy was cut short, or this is part 1 of a split container whose other parts are missing.";
        MarkError(r, "Container is incomplete",
                  "Table of contents ends at " + FormatBytes(toc_end) + " but only " + FormatBytes(r.total_size) +
                      " is on the card (" + FormatBytes(r.missing_bytes) + " missing).",
                  part_hint + " Transfer or copy the container again.");
        return r;
    }

    // 3. TOC header (0x30 bytes at toc_at).
    u8 toc_hdr[kTocHeaderSize];
    if (!reader.ReadAt(r.toc_at, toc_hdr, sizeof toc_hdr) || std::memcmp(toc_hdr, "TOC\x01", 4) != 0) {
        MarkError(r, "Damaged table of contents",
                  "Table of contents does not begin with a valid TOC v1 header.",
                  "Rebuild or re-transfer the NUP file.");
        return r;
    }

    const u32 toc_packages = Rd32(toc_hdr + 0x04);
    const u64 ndirs = Rd64(toc_hdr + 0x08);
    const u64 nfiles = Rd64(toc_hdr + 0x10);
    const u64 pool_at = Rd64(toc_hdr + 0x18);
    const u64 pool_size = Rd64(toc_hdr + 0x20);

    if (toc_packages == 0 || toc_packages > kMaxPackages || toc_packages != head_packages) {
        MarkError(r, "Corrupt package count",
                  "Package count mismatch between superblock (" + std::to_string(head_packages) + ") and TOC (" +
                      std::to_string(toc_packages) + ").",
                  "Rebuild or re-transfer the NUP file.");
        return r;
    }

    // Overflow-safe validation of every TOC section inside toc_size.
    const u64 pkg_bytes = static_cast<u64>(toc_packages) * kPackageRowSize;
    const u64 after_pkg = kTocHeaderSize + pkg_bytes;
    if (after_pkg > r.toc_size || ndirs > (r.toc_size - after_pkg) / kDirRowSize) {
        MarkError(r, "Corrupt TOC layout", "Directory table extends past the end of the table of contents.",
                  "Rebuild or re-transfer the NUP file.");
        return r;
    }
    const u64 after_dirs = after_pkg + ndirs * kDirRowSize;
    if (nfiles > (r.toc_size - after_dirs) / kFileRowSize) {
        MarkError(r, "Corrupt TOC layout", "File table extends past the end of the table of contents.",
                  "Rebuild or re-transfer the NUP file.");
        return r;
    }
    const u64 files_off = after_dirs;
    const u64 children_at = files_off + nfiles * kFileRowSize;
    if (children_at > r.toc_size || pool_at > r.toc_size || pool_size > r.toc_size - pool_at) {
        MarkError(r, "Corrupt TOC layout", "String pool or children table extends past the table of contents.",
                  "Rebuild or re-transfer the NUP file.");
        return r;
    }

    r.packages = toc_packages;
    r.dirs = ndirs;
    r.files = nfiles;

    // 4. Package table scan (<= 4 KB).
    std::vector<u8> pkg_buf(static_cast<std::size_t>(pkg_bytes));
    if (!reader.ReadAt(r.toc_at + kTocHeaderSize, pkg_buf.data(), pkg_buf.size())) {
        MarkError(r, "Unreadable package table", "Could not read package entries from the container.",
                  "Verify the SD card and re-transfer the NUP file.");
        return r;
    }

    u32 game_or_update_pkgs = 0;
    bool offset_overflow = false;
    for (u32 k = 0; k < toc_packages; ++k) {
        const u8* p = pkg_buf.data() + static_cast<std::size_t>(k) * kPackageRowSize;
        const u32 exefs = Rd32(p + 0x0C);
        const u8 kind = p[0x18];
        const u64 meta_at = Rd64(p + 0x20);
        const u32 meta_size = Rd32(p + 0x28);
        if (kind <= 1) {
            ++game_or_update_pkgs;
            if (exefs != kNoneIndex) {
                r.has_program = true;
            }
        }
        if (meta_size > 0) {
            if (meta_at > std::numeric_limits<u64>::max() - meta_size) {
                offset_overflow = true;
            } else {
                r.furthest_data = std::max(r.furthest_data, meta_at + meta_size);
            }
        }
    }

    // 5. Stream file entries in bounded chunks (40 KB) to check data reach and zstd sizes.
    std::vector<u8> chunk(kChunkEntries * kFileRowSize);
    bool have_largest_zstd = false;
    u32 largest_name_off = 0;
    u32 largest_name_len = 0;

    u64 remaining = nfiles;
    u64 cursor = r.toc_at + files_off;
    while (remaining > 0) {
        const std::size_t batch = static_cast<std::size_t>(std::min<u64>(remaining, kChunkEntries));
        const std::size_t bytes = batch * kFileRowSize;
        if (!reader.ReadAt(cursor, chunk.data(), bytes)) {
            MarkError(r, "Unreadable file table", "Could not read file entries from the table of contents.",
                      "Verify the SD card and re-transfer the NUP file.");
            return r;
        }
        for (std::size_t i = 0; i < batch; ++i) {
            const u8* e = chunk.data() + i * kFileRowSize;
            const u32 name_off = Rd32(e + 0x00);
            const u32 name_len = Rd32(e + 0x04);
            const u64 unpacked = Rd64(e + 0x08);
            const u64 at = Rd64(e + 0x10);
            const u64 kept = Rd64(e + 0x18);
            const u8 codec = e[0x40];

            if (kept > 0) {
                if (at > std::numeric_limits<u64>::max() - kept) {
                    offset_overflow = true;
                } else {
                    r.furthest_data = std::max(r.furthest_data, at + kept);
                }
            }

            if (codec == 0) {
                ++r.raw_files;
            } else if (codec == 1) {
                ++r.zstd_files;
                r.zstd_packed_bytes = SaturatingAdd(r.zstd_packed_bytes, kept);
                r.zstd_unpacked_bytes = SaturatingAdd(r.zstd_unpacked_bytes, unpacked);
                if (unpacked > zstd_warn_bytes) {
                    ++r.oversized_zstd_files;
                }
                if (!have_largest_zstd || unpacked > r.largest_zstd_unpacked) {
                    have_largest_zstd = true;
                    r.largest_zstd_unpacked = unpacked;
                    r.largest_zstd_packed = kept;
                    largest_name_off = name_off;
                    largest_name_len = name_len;
                }
            } else {
                ++r.unknown_codec_files;
            }
        }
        cursor += bytes;
        remaining -= batch;
    }

    // Resolve only the single largest zstd entry's name from the TOC string pool.
    if (have_largest_zstd && largest_name_len > 0 && largest_name_off <= pool_size &&
        largest_name_len <= pool_size - largest_name_off) {
        const std::size_t take = static_cast<std::size_t>(std::min<u32>(largest_name_len, kMaxNameDisplay));
        std::string raw(take, '\0');
        if (reader.ReadAt(r.toc_at + pool_at + largest_name_off, raw.data(), take)) {
            r.largest_zstd_name = SanitizeName(raw.data(), raw.size());
        }
    }

    // 6. Classify findings: fatal integrity errors first, then non-fatal warnings.
    if (offset_overflow) {
        MarkError(r, "Corrupt file offset",
                  "At least one entry has a file offset + size that overflows 64-bit bounds.",
                  "The container table looks damaged; rebuild or re-transfer the NUP file.");
    }
    if (r.furthest_data > r.total_size) {
        r.missing_bytes = r.furthest_data - r.total_size;
        const std::string part_hint =
            r.parts > 1 ? "A split part is missing or was not copied whole."
                        : "The copy was cut short, or other split parts are missing beside this file.";
        MarkError(r, "Container is incomplete",
                  "File data reaches " + FormatBytes(r.furthest_data) + " but only " + FormatBytes(r.total_size) +
                      " is on the card (" + FormatBytes(r.missing_bytes) + " missing).",
                  part_hint + " Transfer or copy the container again.");
    }
    if (r.unknown_codec_files > 0) {
        MarkError(r, "Unsupported compression codec",
                  "Contains " + Plural(r.unknown_codec_files, "file", "files") +
                      " using an unknown compression codec.",
                  "Repack the NUP container with standard uncompressed or Zstandard entries.");
    }
    if (game_or_update_pkgs == 0) {
        MarkError(r, "No game in container",
                  "This container holds only add-on content and no base game or update package.",
                  "Include the base game package in the NUP container.");
    } else if (r.files > 0 && !r.has_program) {
        MarkError(r, "No program folder in container",
                  "No base game or update package references an ExeFS program folder.",
                  "Rebuild the NUP container with the game's ExeFS included.");
    }

    // Warnings (still added to issues even if an error is present, so full diagnostics show).
    if (r.oversized_zstd_files > 0) {
        const std::string who = r.largest_zstd_name.empty() ? "largest entry" : r.largest_zstd_name;
        r.issues.push_back("Found " + Plural(r.oversized_zstd_files, "Zstandard file", "Zstandard files") +
                           " larger than " + FormatBytes(zstd_warn_bytes) + " unpacked (largest: " + who + ", " +
                           FormatBytes(r.largest_zstd_unpacked) + ").");
        if (r.status == HealthStatus::Ok) {
            r.status = HealthStatus::Warning;
            r.summary = "Large compressed file (" + FormatBytes(r.largest_zstd_unpacked) + ")";
            r.advice = "The loader unpacks Zstandard files whole into memory. Entries over " +
                       FormatBytes(zstd_warn_bytes) +
                       " can run out of memory at runtime; repack uncompressed or raise Room in Advanced options.";
        }
    }
    if (r.mismatched_part > 0) {
        r.issues.push_back("Split part " + std::to_string(r.mismatched_part) + " has a different size (" +
                           FormatBytes(live[static_cast<std::size_t>(r.mismatched_part - 1)].size) + ") from part 1 (" +
                           FormatBytes(live[0].size) + ").");
        if (r.status == HealthStatus::Ok) {
            r.status = HealthStatus::Warning;
            r.summary = "Split part size mismatch";
            r.advice = "Split parts of one cut normally share the same size except the last. Verify your copy if "
                       "the game fails to load.";
        }
    }

    if (r.status == HealthStatus::Ok) {
        r.summary = "Container looks healthy";
    }
    return r;
}

HealthReport CheckHealth(const std::string& path, std::uint64_t zstd_warn_bytes) {
    return CheckHealth(std::vector<Part>{{path, 0}}, zstd_warn_bytes);
}

} // namespace Nup
