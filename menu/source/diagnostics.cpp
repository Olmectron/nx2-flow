// SPDX-License-Identifier: GPL-2.0-or-later

#include "diagnostics.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace Flow {
namespace {

constexpr const char* kDotSep = " \xC2\xB7 "; // UTF-8 middle dot (present in font_baked.h).

std::string FormatMiB(std::uint64_t bytes) {
    return std::to_string(bytes >> 20) + " MB";
}

std::string FormatBytesCompact(std::uint64_t bytes) {
    char buf[32];
    if (bytes >= (1ull << 20)) {
        std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / 1048576.0);
    } else if (bytes >= 1024ull) {
        std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buf, sizeof buf, "%" PRIu64 " B", bytes);
    }
    return buf;
}

/// Returns the valid UTF-8 sequence length starting at `s[i]`, or 0 if invalid/truncated.
std::size_t ValidUtf8Len(std::string_view s, std::size_t i) {
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0) {
        if (i + 1 >= s.size()) {
            return 0;
        }
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        if ((c1 & 0xC0) != 0x80 || c0 < 0xC2) { // Reject overlong encodings.
            return 0;
        }
        return 2;
    }
    if ((c0 & 0xF0) == 0xE0) {
        if (i + 2 >= s.size()) {
            return 0;
        }
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
            return 0;
        }
        if ((c0 == 0xE0 && c1 < 0xA0) || (c0 == 0xED && c1 >= 0xA0)) { // Overlong or UTF-16 surrogate.
            return 0;
        }
        return 3;
    }
    if ((c0 & 0xF8) == 0xF0) {
        if (i + 3 >= s.size()) {
            return 0;
        }
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        const unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
            return 0;
        }
        if ((c0 == 0xF0 && c1 < 0x90) || c0 > 0xF4 || (c0 == 0xF4 && c1 >= 0x90)) {
            return 0;
        }
        return 4;
    }
    return 0;
}

/// Drops any incomplete multi-byte UTF-8 sequence at the end of a raw byte buffer.
std::string_view TrimTrailingIncompleteUtf8(std::string_view s) {
    std::size_t n = s.size();
    // Walk back over up to 3 trailing continuation bytes (10xxxxxx) to find a possible lead byte.
    std::size_t back = 0;
    while (back < 3 && back < n && (static_cast<unsigned char>(s[n - 1 - back]) & 0xC0) == 0x80) {
        ++back;
    }
    if (back < n) {
        const std::size_t lead = n - 1 - back;
        const unsigned char c = static_cast<unsigned char>(s[lead]);
        std::size_t expected = 1;
        if ((c & 0xE0) == 0xC0) {
            expected = 2;
        } else if ((c & 0xF0) == 0xE0) {
            expected = 3;
        } else if ((c & 0xF8) == 0xF0) {
            expected = 4;
        }
        if (expected > 1 && lead + expected > n) {
            return s.substr(0, lead);
        }
    }
    return s;
}

/// Skips any leading UTF-8 continuation bytes after seeking into the middle of a file.
std::string_view SkipLeadingUtf8Continuations(std::string_view s) {
    std::size_t start = 0;
    while (start < s.size() && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) {
        ++start;
    }
    return s.substr(start);
}

/// Splits raw text into sanitized lines, normalizing CRLF and lone CR.
std::vector<std::string> SplitLines(std::string_view data, bool drop_incomplete_last) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < data.size()) {
        const std::size_t start = i;
        while (i < data.size() && data[i] != '\n' && data[i] != '\r') {
            ++i;
        }
        const bool had_break = i < data.size();
        const std::string_view segment = data.substr(start, i - start);
        if (had_break) {
            if (data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n') {
                i += 2;
            } else {
                ++i;
            }
            out.push_back(SanitizeLogLine(segment));
        } else if (!drop_incomplete_last || out.empty()) {
            out.push_back(SanitizeLogLine(segment));
        }
    }
    return out;
}

bool SeekTo(std::FILE* f, std::uint64_t offset) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<long long>(offset), SEEK_SET) == 0;
#else
    return std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0;
#endif
}

bool FileSize(std::FILE* f, std::uint64_t& size_out) {
#if defined(_WIN32)
    if (_fseeki64(f, 0, SEEK_END) != 0) {
        return false;
    }
    const long long end = _ftelli64(f);
#else
    if (std::fseek(f, 0, SEEK_END) != 0) {
        return false;
    }
    const long end = std::ftell(f);
#endif
    if (end < 0) {
        return false;
    }
    size_out = static_cast<std::uint64_t>(end);
    return SeekTo(f, 0);
}

} // namespace

HostKind ClassifyHost(const HostInfo& info) {
    if (!info.available) {
        return HostKind::Unknown;
    }
    if (info.is_applet) {
        return HostKind::Applet;
    }
    const std::uint16_t prefix = static_cast<std::uint16_t>(info.program_id >> 48);
    if (prefix == 0x0100) {
        return HostKind::Switch1Title;
    }
    if (info.total_memory > 0 && info.total_memory < kSwitch2MemoryThreshold) {
        return HostKind::LowMemory;
    }
    if (prefix == 0x0400 || info.total_memory >= kSwitch2MemoryThreshold) {
        return HostKind::Switch2Title;
    }
    return HostKind::Unknown;
}

bool HostNeedsWarning(const HostInfo& info) {
    const HostKind kind = ClassifyHost(info);
    return kind == HostKind::Applet || kind == HostKind::Switch1Title || kind == HostKind::LowMemory;
}

std::string FormatProgramId(std::uint64_t program_id) {
    if (program_id == 0) {
        return {};
    }
    char buf[20];
    std::snprintf(buf, sizeof buf, "%016" PRIx64, program_id);
    return buf;
}

std::string FormatHostSummary(const HostInfo& info) {
    if (!info.available) {
        return {};
    }
    std::string out = "Host";
    const HostKind kind = ClassifyHost(info);
    const std::string id = FormatProgramId(info.program_id);
    if (kind == HostKind::Applet) {
        out += " Applet";
        if (!id.empty()) {
            out += " (" + id + ")";
        }
    } else if (!id.empty()) {
        out += " " + id;
    }
    if (info.total_memory > 0) {
        out += kDotSep + FormatMiB(info.total_memory);
    }
    if (info.has_remote_gpu) {
        out += kDotSep + FormatMiB(info.remote_gpu_memory) + " GPU";
    }
    if (info.has_sysres) {
        out += kDotSep + FormatMiB(info.sysres_memory) + " SysRes";
    }
    return out;
}

std::string HostWarningTitle(const HostInfo& info) {
    switch (ClassifyHost(info)) {
    case HostKind::Applet:
        return "Running in applet mode";
    case HostKind::Switch1Title:
        return "Started over a Switch 1 title";
    case HostKind::LowMemory:
        return "Low-memory host title";
    default:
        return {};
    }
}

std::string HostWarningBody(const HostInfo& info) {
    const std::string ram = info.total_memory > 0 ? FormatMiB(info.total_memory) : std::string{"limited"};
    const std::string id = FormatProgramId(info.program_id);
    switch (ClassifyHost(info)) {
    case HostKind::Applet:
        return "Applet hosts only provide " + ram +
               " of memory. Hold R while starting a Switch 2 game (0400...) to launch over a full title.";
    case HostKind::Switch1Title:
        return "Host " + (id.empty() ? std::string{"0100..."} : id) + " has " + ram +
               " of memory. Switch 2 games need a Switch 2 title (0400...) as host for full RAM, GPU memory and system resources.";
    case HostKind::LowMemory:
        return "This host has " + ram +
               " of memory (Switch 2 titles provide ~9100 MB). Start the homebrew menu over a Switch 2 title (0400...) if the game crashes.";
    default:
        return {};
    }
}

std::string SanitizeLogLine(std::string_view raw, std::size_t max_bytes) {
    std::string out;
    out.reserve(std::min(raw.size(), max_bytes));
    std::size_t i = 0;
    bool truncated = false;
    while (i < raw.size()) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c == '\r' || c == '\n' || c == '\0') {
            ++i;
            continue;
        }
        if (c == '\t') {
            constexpr std::size_t kTab = 4;
            if (out.size() + kTab > max_bytes) {
                truncated = true;
                break;
            }
            out.append(kTab, ' ');
            ++i;
            continue;
        }
        if (c < 0x20 || c == 0x7F) {
            if (out.size() + 1 > max_bytes) {
                truncated = true;
                break;
            }
            out.push_back(' ');
            ++i;
            continue;
        }
        const std::size_t len = ValidUtf8Len(raw, i);
        if (len == 0) {
            if (out.size() + 1 > max_bytes) {
                truncated = true;
                break;
            }
            out.push_back('?');
            ++i;
            continue;
        }
        if (out.size() + len > max_bytes) {
            truncated = true;
            break;
        }
        out.append(raw.data() + i, len);
        i += len;
    }
    if (truncated && max_bytes >= 3) {
        // Replace trailing bytes cleanly on a UTF-8 boundary to append "...".
        while (!out.empty() && out.size() + 3 > max_bytes) {
            while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
                out.pop_back();
            }
            if (!out.empty()) {
                out.pop_back();
            }
        }
        out.append("...");
    }
    return out;
}

LogPreview ReadLogPreview(const std::string& fs_path,
                          std::size_t max_head_lines,
                          std::size_t max_tail_lines,
                          std::size_t max_head_bytes,
                          std::size_t max_tail_bytes) {
    LogPreview res;
    std::FILE* f = std::fopen(fs_path.c_str(), "rb");
    if (!f) {
        res.error = "The file could not be opened.";
        return res;
    }
    if (!FileSize(f, res.file_size)) {
        std::fclose(f);
        res.error = "Could not determine file size.";
        return res;
    }
    res.ok = true;
    if (res.file_size == 0) {
        std::fclose(f);
        res.empty = true;
        return res;
    }

    const std::uint64_t combined_limit = static_cast<std::uint64_t>(max_head_bytes) + static_cast<std::uint64_t>(max_tail_bytes);
    if (res.file_size <= combined_limit) {
        std::string all(static_cast<std::size_t>(res.file_size), '\0');
        const std::size_t got = std::fread(all.data(), 1, all.size(), f);
        std::fclose(f);
        all.resize(got);
        std::vector<std::string> lines = SplitLines(all, false);
        if (lines.empty()) {
            res.empty = true;
            return res;
        }
        if (lines.size() <= max_head_lines + max_tail_lines) {
            res.lines = std::move(lines);
            return res;
        }
        res.truncated = true;
        const std::size_t omitted = lines.size() - max_head_lines - max_tail_lines;
        res.lines.reserve(max_head_lines + max_tail_lines + 1);
        for (std::size_t k = 0; k < max_head_lines; ++k) {
            res.lines.push_back(std::move(lines[k]));
        }
        res.lines.push_back("--- " + std::to_string(omitted) + " lines omitted (" + FormatBytesCompact(res.file_size) + " total) ---");
        for (std::size_t k = lines.size() - max_tail_lines; k < lines.size(); ++k) {
            res.lines.push_back(std::move(lines[k]));
        }
        return res;
    }

    // Large file: read bounded header from start and bounded tail from end.
    std::string head_buf(max_head_bytes, '\0');
    const std::size_t head_got = std::fread(head_buf.data(), 1, head_buf.size(), f);
    head_buf.resize(head_got);
    const std::string_view head_view = TrimTrailingIncompleteUtf8(head_buf);
    std::vector<std::string> head_lines = SplitLines(head_view, true);
    if (head_lines.size() > max_head_lines) {
        head_lines.resize(max_head_lines);
    }

    const std::uint64_t tail_offset = res.file_size > max_tail_bytes ? res.file_size - max_tail_bytes : 0;
    std::string tail_buf(max_tail_bytes, '\0');
    std::size_t tail_got = 0;
    if (SeekTo(f, tail_offset)) {
        tail_got = std::fread(tail_buf.data(), 1, tail_buf.size(), f);
    }
    std::fclose(f);
    tail_buf.resize(tail_got);

    std::string_view tail_view = tail_buf;
    if (tail_offset > 0) {
        const std::size_t nl = tail_view.find('\n');
        if (nl != std::string_view::npos && nl + 1 < tail_view.size()) {
            tail_view = tail_view.substr(nl + 1);
        } else {
            // Single giant line with no newline in the tail window: align on a UTF-8 boundary.
            tail_view = SkipLeadingUtf8Continuations(tail_view);
        }
    }
    std::vector<std::string> tail_lines = SplitLines(tail_view, false);
    if (tail_lines.size() > max_tail_lines) {
        tail_lines.erase(tail_lines.begin(), tail_lines.end() - static_cast<std::ptrdiff_t>(max_tail_lines));
    }

    res.truncated = true;
    res.lines.reserve(head_lines.size() + tail_lines.size() + 1);
    for (std::string& line : head_lines) {
        res.lines.push_back(std::move(line));
    }
    res.lines.push_back("--- middle omitted (" + FormatBytesCompact(res.file_size) + " total) ---");
    for (std::string& line : tail_lines) {
        res.lines.push_back(std::move(line));
    }
    return res;
}

} // namespace Flow
