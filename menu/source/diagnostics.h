// SPDX-License-Identifier: GPL-2.0-or-later

// Active host diagnostics and safe head+tail log previewing for on-console troubleshooting.
// Kept free of console headers so the same logic runs on Switch 2 and in host unit tests.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Flow {

/// Memory threshold (8000 MiB) matching loader/runtime/main.c: a Switch 2 title provides ~9100 MB,
/// whereas a Switch 1 title provides ~6900 MB and an applet significantly less.
inline constexpr std::uint64_t kSwitch2MemoryThreshold = 8000ull << 20;

/// Snapshot of the host title that nx2-flow was launched over.
struct HostInfo {
    bool available{};
    std::uint64_t program_id{};        // InfoType_ProgramId (0 if unavailable).
    std::uint64_t total_memory{};      // InfoType_TotalMemorySize, bytes.
    std::uint64_t sysres_memory{};     // InfoType_SystemResourceSizeTotal, bytes.
    bool has_sysres{};                 // True when sysres was reported by the kernel.
    std::uint64_t remote_gpu_memory{}; // Remote GPU memory (InfoType 31), bytes.
    bool has_remote_gpu{};             // True when InfoType 31 succeeded.
    bool is_applet{};                  // True when running under an applet rather than a title.
};

enum class HostKind {
    Unknown,
    Switch2Title,
    Switch1Title,
    Applet,
    LowMemory,
};

HostKind ClassifyHost(const HostInfo& info);
bool HostNeedsWarning(const HostInfo& info);

/// Formats a 64-bit title/program id as 16 lowercase hex digits; empty when 0.
std::string FormatProgramId(std::uint64_t program_id);

/// Compact single-line summary for the header/settings footer, e.g.:
///   "Host 0400f00000001000 · 9120 MB · 192 MB GPU · 64 MB SysRes"
/// Empty when !info.available.
std::string FormatHostSummary(const HostInfo& info);

/// Non-blocking warning title/body for the Game Sheet when HostNeedsWarning(info) is true.
std::string HostWarningTitle(const HostInfo& info);
std::string HostWarningBody(const HostInfo& info);

/// Sanitized head + tail excerpt of a log or crash report file.
struct LogPreview {
    bool ok{};
    bool empty{};
    bool truncated{};
    std::uint64_t file_size{};
    std::vector<std::string> lines;
    std::string error;
};

/// Sanitizes a single line for display: strips CR, expands tabs, drops/replaces control bytes,
/// validates UTF-8, and truncates safely at a UTF-8 codepoint boundary within `max_bytes`.
std::string SanitizeLogLine(std::string_view raw, std::size_t max_bytes = 512);

/// Reads a bounded preview (header + tail) of a log/crash file from an OS/sdmc path without
/// loading multi-megabyte files into memory.
LogPreview ReadLogPreview(const std::string& fs_path,
                          std::size_t max_head_lines = 16,
                          std::size_t max_tail_lines = 120,
                          std::size_t max_head_bytes = 4096,
                          std::size_t max_tail_bytes = 32768);

} // namespace Flow
