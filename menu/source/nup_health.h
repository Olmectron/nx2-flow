// SPDX-License-Identifier: GPL-2.0-or-later

// Pre-launch NUP container health inspection: verifies superblock, TOC layout, split-part
// consistency, full data reach on disk, and Zstandard-compressed entry sizes using bounded
// streaming reads so large containers can be checked safely with minimal RAM.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nup.h"

namespace Nup {

/// Single Zstandard-compressed entries larger than this risk exhausting the loader's contiguous
/// unpack buffer (loader/runtime/game.c allocates the full uncompressed size in nx2_big).
inline constexpr std::uint64_t kOversizedZstdBytes = 256ull << 20; // 256 MiB
/// Maximum TOC size accepted by loader/runtime/game.c.
inline constexpr std::uint64_t kMaxTocBytes = 512ull << 20; // 512 MiB

enum class HealthStatus {
    Ok,      // Structure, data reach, and compressed entry sizes look healthy.
    Warning, // Container is readable, but has risk factors (oversized zstd, mismatched split parts).
    Error,   // Container is truncated, corrupt, missing parts, or unsupported.
};

struct HealthReport {
    HealthStatus status{HealthStatus::Ok};

    // Container and split-part geometry.
    std::uint64_t total_size{};
    std::uint64_t toc_at{};
    std::uint64_t toc_size{};
    std::uint64_t furthest_data{}; // Furthest byte referenced by TOC, metadata, or file entries.
    std::uint64_t missing_bytes{}; // Non-zero when container data extends past total_size.

    int parts{};
    int mismatched_part{}; // 1-based index of first intermediate part whose size != part 1; 0 if none.

    // TOC counts.
    std::uint32_t packages{};
    std::uint64_t dirs{};
    std::uint64_t files{};
    bool has_program{}; // At least one game/update package references an ExeFS directory.

    // Compression and memory footprint diagnostics.
    std::uint64_t raw_files{};
    std::uint64_t zstd_files{};
    std::uint64_t unknown_codec_files{};
    std::uint64_t zstd_packed_bytes{};
    std::uint64_t zstd_unpacked_bytes{};
    std::uint64_t oversized_zstd_files{}; // codec == 1 && unpacked size > zstd_warn_bytes.
    std::uint64_t largest_zstd_unpacked{};
    std::uint64_t largest_zstd_packed{};
    std::string largest_zstd_name; // Sanitized entry name from the TOC string pool.

    // Human-readable findings (errors first, then warnings).
    std::vector<std::string> issues;
    std::string summary; // Short single-line headline for notices and dialog titles.
    std::string advice;  // Actionable guidance when status != Ok.
};

/// Inspects a NUP container (whole file or split parts) without loading the entire TOC into RAM.
HealthReport CheckHealth(const std::vector<Part>& parts,
                         std::uint64_t zstd_warn_bytes = kOversizedZstdBytes);

/// Convenience overload for a single-file container.
HealthReport CheckHealth(const std::string& path,
                         std::uint64_t zstd_warn_bytes = kOversizedZstdBytes);

/// Formats a byte count cleanly (B, KB, MB, GB) for diagnostics output.
std::string FormatBytes(std::uint64_t bytes);

} // namespace Nup
