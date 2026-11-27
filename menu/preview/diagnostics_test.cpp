// SPDX-License-Identifier: GPL-2.0-or-later

// Host unit tests for active host diagnostics and safe head+tail log previewing.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "diagnostics.h"

namespace {

int g_checked = 0;
int g_failed = 0;

void Check(bool cond, const std::string& what) {
    ++g_checked;
    if (!cond) {
        ++g_failed;
        std::printf("FAILED: %s\n", what.c_str());
    }
}

bool IsValidUtf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c0 = static_cast<unsigned char>(s[i]);
        if (c0 < 0x80) {
            ++i;
            continue;
        }
        std::size_t len = 0;
        if ((c0 & 0xE0) == 0xC0 && c0 >= 0xC2) {
            len = 2;
        } else if ((c0 & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c0 & 0xF8) == 0xF0 && c0 <= 0xF4) {
            len = 4;
        } else {
            return false;
        }
        if (i + len > s.size()) {
            return false;
        }
        for (std::size_t k = 1; k < len; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += len;
    }
    return true;
}

bool WriteFile(const std::string& path, const std::string& content) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool ok = content.empty() || std::fwrite(content.data(), 1, content.size(), f) == content.size();
    std::fclose(f);
    return ok;
}

void TestHostClassification() {
    // 1. Unavailable host (e.g. desktop / unqueried).
    {
        Flow::HostInfo info;
        Check(Flow::ClassifyHost(info) == Flow::HostKind::Unknown, "unavailable host is Unknown");
        Check(!Flow::HostNeedsWarning(info), "unavailable host needs no warning");
        Check(Flow::FormatHostSummary(info).empty(), "unavailable host has empty summary");
    }

    // 2. Healthy Switch 2 title (0400..., 9120 MB RAM, 192 MB GPU, 64 MB SysRes).
    {
        Flow::HostInfo info;
        info.available = true;
        info.program_id = 0x0400F00000001000ull;
        info.total_memory = 9120ull << 20;
        info.remote_gpu_memory = 192ull << 20;
        info.has_remote_gpu = true;
        info.sysres_memory = 64ull << 20;
        info.has_sysres = true;
        Check(Flow::ClassifyHost(info) == Flow::HostKind::Switch2Title, "0400 title with 9120 MB is Switch2Title");
        Check(!Flow::HostNeedsWarning(info), "healthy Switch 2 title needs no warning");
        const std::string summary = Flow::FormatHostSummary(info);
        Check(summary.find("0400f00000001000") != std::string::npos, "summary includes program id: " + summary);
        Check(summary.find("9120 MB") != std::string::npos, "summary includes total memory: " + summary);
        Check(summary.find("192 MB GPU") != std::string::npos, "summary includes GPU memory: " + summary);
        Check(summary.find("64 MB SysRes") != std::string::npos, "summary includes SysRes: " + summary);
    }

    // 3. Switch 2 title reporting 0 MB SysRes.
    {
        Flow::HostInfo info;
        info.available = true;
        info.program_id = 0x0400123400000000ull;
        info.total_memory = 9000ull << 20;
        info.sysres_memory = 0;
        info.has_sysres = true;
        Check(Flow::ClassifyHost(info) == Flow::HostKind::Switch2Title, "0400 title with 0 SysRes is Switch2Title");
        Check(Flow::FormatHostSummary(info).find("0 MB SysRes") != std::string::npos, "summary displays 0 MB SysRes");
    }

    // 4. Switch 1 title (0100..., ~6900 MB RAM).
    {
        Flow::HostInfo info;
        info.available = true;
        info.program_id = 0x0100AABB00001000ull;
        info.total_memory = 6900ull << 20;
        info.has_remote_gpu = true;
        info.remote_gpu_memory = 96ull << 20;
        info.has_sysres = true;
        info.sysres_memory = 0;
        Check(Flow::ClassifyHost(info) == Flow::HostKind::Switch1Title, "0100 title is Switch1Title");
        Check(Flow::HostNeedsWarning(info), "Switch 1 title triggers warning");
        Check(Flow::HostWarningTitle(info).find("Switch 1") != std::string::npos, "Switch 1 warning title mentions Switch 1");
        Check(Flow::HostWarningBody(info).find("0400...") != std::string::npos, "Switch 1 warning body recommends 0400... host");
    }

    // 5. Applet host (e.g. Album 010000000000100D, is_applet = true).
    {
        Flow::HostInfo info;
        info.available = true;
        info.program_id = 0x010000000000100Dull;
        info.total_memory = 480ull << 20;
        info.is_applet = true;
        Check(Flow::ClassifyHost(info) == Flow::HostKind::Applet, "applet flag classifies as Applet");
        Check(Flow::HostNeedsWarning(info), "applet host triggers warning");
        Check(Flow::FormatHostSummary(info).find("Applet") != std::string::npos, "applet summary says Applet");
        Check(Flow::HostWarningBody(info).find("0400...") != std::string::npos, "applet warning recommends 0400... host");
    }

    // 6. Low-memory non-0100, non-applet host.
    {
        Flow::HostInfo info;
        info.available = true;
        info.program_id = 0x0400999900001000ull;
        info.total_memory = 3200ull << 20;
        Check(Flow::ClassifyHost(info) == Flow::HostKind::LowMemory, "non-0100 host under 8000 MB is LowMemory");
        Check(Flow::HostNeedsWarning(info), "low-memory host triggers warning");
        Check(Flow::HostWarningBody(info).find("3200 MB") != std::string::npos, "low-memory warning shows detected RAM");
    }
}

void TestLogTailExtraction(const std::filesystem::path& dir) {
    // 1. Missing file.
    {
        const Flow::LogPreview p = Flow::ReadLogPreview((dir / "does-not-exist.txt").string());
        Check(!p.ok, "missing file returns !ok");
        Check(!p.error.empty(), "missing file sets error message");
        Check(p.lines.empty(), "missing file returns 0 lines");
    }

    // 2. Empty file.
    {
        const std::string path = (dir / "empty.txt").string();
        Check(WriteFile(path, ""), "write empty file");
        const Flow::LogPreview p = Flow::ReadLogPreview(path);
        Check(p.ok && p.empty && !p.truncated && p.file_size == 0 && p.lines.empty(), "empty file handled cleanly");
    }

    // 3. Short file with CRLF, lone CR, tabs, UTF-8, and control chars.
    {
        const std::string path = (dir / "short.txt").string();
        std::string content;
        content += "nx2-game 0.1.6 crash report\r\n";
        content += "ended:\tpage fault at 0x1234\r";
        content += "caf\xC3\xA9 \xE2\x80\x94 ok\x07!\n";
        content += "last line without newline";
        Check(WriteFile(path, content), "write short file");
        const Flow::LogPreview p = Flow::ReadLogPreview(path);
        Check(p.ok && !p.empty && !p.truncated, "short file read completely");
        Check(p.lines.size() == 4, "short file has 4 lines, got " + std::to_string(p.lines.size()));
        if (p.lines.size() == 4) {
            Check(p.lines[0] == "nx2-game 0.1.6 crash report", "CRLF stripped cleanly");
            Check(p.lines[1] == "ended:    page fault at 0x1234", "tab expanded and lone CR split");
            Check(p.lines[2] == "caf\xC3\xA9 \xE2\x80\x94 ok !", "UTF-8 preserved and BEL replaced with space");
            Check(p.lines[3] == "last line without newline", "trailing line without newline preserved");
        }
    }

    // 4. Medium file (<36 KB) with >136 lines -> head + divider + tail.
    {
        const std::string path = (dir / "many_short_lines.txt").string();
        std::string content;
        for (int i = 1; i <= 250; ++i) {
            content += "line " + std::to_string(i) + "\n";
        }
        Check(WriteFile(path, content), "write 250-line file");
        const Flow::LogPreview p = Flow::ReadLogPreview(path, 16, 120);
        Check(p.ok && p.truncated, "250-line file is truncated");
        Check(p.lines.size() == 16 + 1 + 120, "250-line preview has 16 + 1 + 120 lines");
        if (p.lines.size() == 137) {
            Check(p.lines.front() == "line 1", "head starts at line 1");
            Check(p.lines[15] == "line 16", "head ends at line 16");
            Check(p.lines[16].find("114 lines omitted") != std::string::npos, "divider reports omitted line count");
            Check(p.lines[17] == "line 131", "tail starts at line 131");
            Check(p.lines.back() == "line 250", "tail ends at line 250");
        }
    }

    // 5. Large >100 KB file (~220 KB, 2500 lines) -> fast seek head + tail.
    {
        const std::string path = (dir / "large.txt").string();
        std::string content;
        content.reserve(240000);
        content += "nx2-game 0.1.6 crash report\r\n";
        content += "console:  Switch 2, system 20.0.0\r\n";
        content += "ended:    abort in nn::os\r\n";
        for (int i = 1; i <= 2500; ++i) {
            content += "[nx2] event #" + std::to_string(i) + " payload=0123456789abcdef0123456789abcdef0123456789abcdef\r\n";
        }
        Check(content.size() > 150000, "large test file is >150 KB");
        Check(WriteFile(path, content), "write large file");
        const Flow::LogPreview p = Flow::ReadLogPreview(path, 16, 120);
        Check(p.ok && p.truncated && p.file_size == content.size(), "large file preview ok & truncated");
        Check(p.lines.size() == 16 + 1 + 120, "large file returns 16 head + 1 divider + 120 tail lines, got " + std::to_string(p.lines.size()));
        if (p.lines.size() == 137) {
            Check(p.lines[0] == "nx2-game 0.1.6 crash report", "large file preserves crash header line 0");
            Check(p.lines[2] == "ended:    abort in nn::os", "large file preserves crash reason");
            Check(p.lines[16].find("middle omitted") != std::string::npos, "large file has middle omitted divider");
            Check(p.lines[17].find("event #2381 ") != std::string::npos, "large file tail starts at event #2381");
            Check(p.lines.back().find("event #2500 ") != std::string::npos, "large file tail ends at event #2500");
        }
    }

    // 6. UTF-8 safe truncation: both per-line truncation and >100 KB single-line multi-byte file.
    {
        // 4-byte UTF-8 sequence repeated past max_bytes boundary.
        std::string emojis;
        for (int i = 0; i < 40; ++i) {
            emojis += "\xF0\x9F\x9A\x80"; // U+1F680 (4 bytes)
        }
        const std::string cut = Flow::SanitizeLogLine(emojis, 19);
        Check(cut.size() <= 19, "truncated UTF-8 line fits within max_bytes");
        Check(IsValidUtf8(cut), "truncated UTF-8 line remains valid UTF-8");
        Check(cut.size() >= 3 && cut.substr(cut.size() - 3) == "...", "truncated line ends with ellipsis");

        // Invalid UTF-8 bytes replaced safely.
        const std::string bad = std::string{"ok "} + "\xFF\xFE\xC0\xAF" + " end";
        const std::string cleaned = Flow::SanitizeLogLine(bad, 64);
        Check(IsValidUtf8(cleaned), "invalid UTF-8 bytes sanitized to valid UTF-8");
        Check(cleaned.find("ok ") == 0 && cleaned.find(" end") != std::string::npos, "valid surrounding ASCII kept");

        // >100 KB single line of 3-byte UTF-8 characters (no newlines anywhere) -> tail seek must align cleanly.
        const std::string path = (dir / "huge_utf8_single_line.txt").string();
        std::string blob;
        blob.reserve(120000);
        for (int i = 0; i < 40000; ++i) {
            blob += "\xE2\x98\x85"; // U+2605 (3 bytes)
        }
        Check(WriteFile(path, blob), "write >100 KB single-line UTF-8 file");
        const Flow::LogPreview p = Flow::ReadLogPreview(path);
        Check(p.ok && p.truncated && !p.lines.empty(), "huge single-line UTF-8 file readable");
        for (const std::string& line : p.lines) {
            Check(IsValidUtf8(line), "every output line from huge UTF-8 file is valid UTF-8");
        }
    }
}

} // namespace

int main() {
    TestHostClassification();

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "nx2_flow_diag_test";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    TestLogTailExtraction(tmp);
    std::filesystem::remove_all(tmp, ec);

    std::printf("%d checks, %d failed\n", g_checked, g_failed);
    return g_failed == 0 ? 0 : 1;
}
