// SPDX-License-Identifier: GPL-2.0-or-later

// Host unit tests for pre-launch NUP container health inspection (source/nup_health.{h,cpp}).
// Builds and runs on a PC with ASan + UBSan against real temporary NUP files (single and split).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "nup_health.h"

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

void Wr32(std::vector<std::uint8_t>& buf, std::size_t off, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        buf[off + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

void Wr64(std::vector<std::uint8_t>& buf, std::size_t off, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf[off + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

struct FakePkg {
    std::uint64_t title_id{0x0400F00000001000ull};
    std::uint32_t root{0};
    std::uint32_t exefs{0};
    std::uint32_t romfs{0};
    std::uint32_t version{0};
    std::uint8_t kind{0}; // 0=base, 1=update, 2=addon
    std::uint64_t meta_at{0};
    std::uint32_t meta_size{0};
};

struct FakeFile {
    std::string name;
    std::uint64_t unpacked_size{};
    std::uint64_t at{};
    std::uint64_t kept_size{};
    std::uint8_t codec{}; // 0=raw, 1=zstd
};

/// Builds a synthetic NUP v1 image in memory matching loader/runtime/game.c's exact binary layout.
std::vector<std::uint8_t> BuildNup(const std::vector<FakePkg>& pkgs,
                                   const std::vector<FakeFile>& files,
                                   std::uint64_t pad_to_size = 0) {
    const std::uint32_t npackages = static_cast<std::uint32_t>(pkgs.size());
    const std::uint64_t ndirs = std::max<std::uint64_t>(1, npackages);
    const std::uint64_t nfiles = files.size();

    std::vector<std::uint8_t> pool;
    std::vector<std::uint32_t> name_offs;
    name_offs.reserve(files.size());
    for (const FakeFile& f : files) {
        name_offs.push_back(static_cast<std::uint32_t>(pool.size()));
        pool.insert(pool.end(), f.name.begin(), f.name.end());
    }

    const std::uint64_t pkg_off = 0x30;
    const std::uint64_t dirs_off = pkg_off + 0x40ull * npackages;
    const std::uint64_t files_off = dirs_off + 0x18ull * ndirs;
    const std::uint64_t children_off = files_off + 0x50ull * nfiles;
    const std::uint64_t pool_at = children_off;
    const std::uint64_t toc_size = pool_at + pool.size();
    const std::uint64_t toc_at = 0x80;

    std::uint64_t data_end = toc_at + toc_size;
    for (const FakePkg& p : pkgs) {
        if (p.meta_size > 0 && p.meta_at + p.meta_size > data_end && p.meta_at < (1ull << 40)) {
            data_end = p.meta_at + p.meta_size;
        }
    }
    for (const FakeFile& f : files) {
        if (f.kept_size > 0 && f.at < (1ull << 40) && f.at + f.kept_size > data_end) {
            data_end = f.at + f.kept_size;
        }
    }
    const std::uint64_t total_size = pad_to_size > 0 ? pad_to_size : data_end;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(total_size), 0);

    // Superblock (0x80 bytes)
    if (img.size() >= 0x80) {
        std::memcpy(img.data(), "NUP\x01", 4);
        Wr64(img, 0x08, toc_at);
        Wr64(img, 0x10, toc_size);
        Wr64(img, 0x38, data_end);
        Wr32(img, 0x48, npackages);
    }

    // TOC header + tables (only written within img.size())
    std::vector<std::uint8_t> toc(static_cast<std::size_t>(toc_size), 0);
    std::memcpy(toc.data(), "TOC\x01", 4);
    Wr32(toc, 0x04, npackages);
    Wr64(toc, 0x08, ndirs);
    Wr64(toc, 0x10, nfiles);
    Wr64(toc, 0x18, pool_at);
    Wr64(toc, 0x20, pool.size());

    for (std::uint32_t k = 0; k < npackages; ++k) {
        const std::size_t p = static_cast<std::size_t>(pkg_off + 0x40ull * k);
        Wr64(toc, p + 0x00, pkgs[k].title_id);
        Wr32(toc, p + 0x08, pkgs[k].root);
        Wr32(toc, p + 0x0C, pkgs[k].exefs);
        Wr32(toc, p + 0x10, pkgs[k].romfs);
        Wr32(toc, p + 0x14, pkgs[k].version);
        toc[p + 0x18] = pkgs[k].kind;
        Wr64(toc, p + 0x20, pkgs[k].meta_at);
        Wr32(toc, p + 0x28, pkgs[k].meta_size);
    }

    for (std::size_t k = 0; k < files.size(); ++k) {
        const std::size_t e = static_cast<std::size_t>(files_off + 0x50ull * k);
        Wr32(toc, e + 0x00, name_offs[k]);
        Wr32(toc, e + 0x04, static_cast<std::uint32_t>(files[k].name.size()));
        Wr64(toc, e + 0x08, files[k].unpacked_size);
        Wr64(toc, e + 0x10, files[k].at);
        Wr64(toc, e + 0x18, files[k].kept_size);
        toc[e + 0x40] = files[k].codec;
    }

    if (!pool.empty()) {
        std::memcpy(toc.data() + pool_at, pool.data(), pool.size());
    }

    if (img.size() > toc_at) {
        const std::size_t copy_n = std::min<std::size_t>(toc.size(), img.size() - static_cast<std::size_t>(toc_at));
        std::memcpy(img.data() + toc_at, toc.data(), copy_n);
    }
    return img;
}

bool WriteBytes(const std::filesystem::path& p, const std::uint8_t* data, std::size_t n) {
    std::FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool ok = n == 0 || std::fwrite(data, 1, n, f) == n;
    std::fclose(f);
    return ok;
}

bool WriteBytes(const std::filesystem::path& p, const std::vector<std::uint8_t>& v) {
    return WriteBytes(p, v.data(), v.size());
}

void TestFormatBytes() {
    Check(Nup::FormatBytes(0) == "0 B", "FormatBytes(0)");
    Check(Nup::FormatBytes(512) == "512 B", "FormatBytes(512)");
    Check(Nup::FormatBytes(1024) == "1 KB", "FormatBytes(1024)");
    Check(Nup::FormatBytes(1536) == "2 KB", "FormatBytes(1536) rounds up KB");
    Check(Nup::FormatBytes(1ull << 20) == "1 MB", "FormatBytes(1 MiB)");
    Check(Nup::FormatBytes(256ull << 20) == "256 MB", "FormatBytes(256 MiB)");
    Check(Nup::FormatBytes((3ull << 30) / 2) == "1.5 GB", "FormatBytes(1.5 GiB)");
}

void TestEmptyAndMissing(const std::filesystem::path& dir) {
    {
        const Nup::HealthReport r = Nup::CheckHealth(std::vector<Nup::Part>{});
        Check(r.status == Nup::HealthStatus::Error, "empty parts list is Error");
        Check(!r.issues.empty(), "empty parts has issue message");
    }
    {
        const Nup::HealthReport r = Nup::CheckHealth((dir / "does-not-exist.nup").string());
        Check(r.status == Nup::HealthStatus::Error, "missing single file is Error");
        Check(!r.issues.empty(), "missing file has issue message");
    }
    {
        const auto p1 = dir / "missing_part.nup.001";
        WriteBytes(p1, std::vector<std::uint8_t>(256, 0));
        const Nup::HealthReport r = Nup::CheckHealth({{p1.string(), 256}, {(dir / "missing_part.nup.002").string(), 256}});
        Check(r.status == Nup::HealthStatus::Error, "missing split part is Error");
        Check(!r.summary.empty(), "missing split part has summary");
    }
}

void TestBadMagicAndTruncatedHeader(const std::filesystem::path& dir) {
    {
        const auto p = dir / "short.nup";
        WriteBytes(p, std::vector<std::uint8_t>{'N', 'U', 'P', 1, 0, 0});
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "short header is Error");
    }
    {
        const auto p = dir / "not_nup.nup";
        std::vector<std::uint8_t> data(256, 'A');
        WriteBytes(p, data);
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "bad NUP magic is Error");
    }
    {
        const auto p = dir / "bad_toc_magic.nup";
        auto img = BuildNup({FakePkg{}}, {});
        img[0x80] = 'X';
        WriteBytes(p, img);
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "bad TOC magic is Error");
    }
}

void TestTruncatedToc(const std::filesystem::path& dir) {
    const auto p = dir / "trunc_toc.nup";
    auto img = BuildNup({FakePkg{}}, {{"main", 1024, 4096, 1024, 0}});
    // Keep superblock intact, cut off half the TOC.
    img.resize(0xA0);
    WriteBytes(p, img);
    const Nup::HealthReport r = Nup::CheckHealth(p.string());
    Check(r.status == Nup::HealthStatus::Error, "truncated TOC is Error");
    Check(r.missing_bytes > 0, "truncated TOC reports missing_bytes > 0");
    Check(!r.advice.empty(), "truncated TOC includes advice");
}

void TestCorruptTocBounds(const std::filesystem::path& dir) {
    // 1. toc_size > kMaxTocBytes
    {
        const auto p = dir / "huge_toc.nup";
        auto img = BuildNup({FakePkg{}}, {});
        Wr64(img, 0x10, Nup::kMaxTocBytes + 1);
        WriteBytes(p, img);
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "oversized TOC > 512 MiB is Error");
    }
    // 2. npackages mismatch between superblock and TOC
    {
        const auto p = dir / "pkg_mismatch.nup";
        auto img = BuildNup({FakePkg{}}, {});
        Wr32(img, 0x80 + 0x04, 2);
        WriteBytes(p, img);
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "npackages mismatch is Error");
    }
    // 3. nfiles overflow
    {
        const auto p = dir / "nfiles_overflow.nup";
        auto img = BuildNup({FakePkg{}}, {});
        Wr64(img, 0x80 + 0x10, 0xFFFFFFFFFFFFFF00ull);
        WriteBytes(p, img);
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "huge nfiles overflow is Error");
    }
    // 4. pool_at + pool_size > toc_size
    {
        const auto p = dir / "bad_pool.nup";
        auto img = BuildNup({FakePkg{}}, {});
        Wr64(img, 0x80 + 0x20, 999999);
        WriteBytes(p, img);
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "out-of-bounds pool is Error");
    }
}

void TestHealthySingleFile(const std::filesystem::path& dir) {
    const auto p = dir / "healthy.nup";
    std::vector<FakePkg> pkgs{
        FakePkg{0x0400F00000001000ull, 0, 0, 0, 0, 0, 0, 0},
        FakePkg{0x0400F00000001800ull, 1, 1, 1, 0x10000, 1, 0, 0},
        FakePkg{0x0400F00000002001ull, 2, 0xFFFFFFFFu, 2, 0, 2, 0, 0},
    };
    std::vector<FakeFile> files{
        {"main", 4096, 4096, 4096, 0},
        {"Data/Level1.pack", 64ull << 20, 8192, 2048, 1},
        {"Data/Music.bfstm", 16384, 10240, 16384, 0},
    };
    auto img = BuildNup(pkgs, files);
    WriteBytes(p, img);

    const Nup::HealthReport r = Nup::CheckHealth(p.string());
    Check(r.status == Nup::HealthStatus::Ok, "healthy single NUP is Ok");
    Check(r.parts == 1, "single NUP reports 1 part");
    Check(r.packages == 3, "healthy NUP reports 3 packages");
    Check(r.files == 3, "healthy NUP reports 3 files");
    Check(r.raw_files == 2, "healthy NUP reports 2 raw files");
    Check(r.zstd_files == 1, "healthy NUP reports 1 zstd file");
    Check(r.zstd_packed_bytes == 2048, "healthy NUP tracks zstd packed bytes");
    Check(r.zstd_unpacked_bytes == (64ull << 20), "healthy NUP tracks zstd unpacked bytes");
    Check(r.oversized_zstd_files == 0, "64 MiB zstd is not oversized");
    Check(r.largest_zstd_unpacked == (64ull << 20), "largest zstd unpacked tracked");
    Check(r.largest_zstd_packed == 2048, "largest zstd packed tracked");
    Check(r.largest_zstd_name == "Data/Level1.pack", "largest zstd name extracted: " + r.largest_zstd_name);
    Check(r.has_program, "healthy NUP detects program exefs");
    Check(r.missing_bytes == 0, "healthy NUP has 0 missing bytes");
    Check(r.furthest_data == 10240 + 16384, "healthy NUP tracks exact furthest_data");
    Check(r.issues.empty(), "healthy NUP has no issues");
}

void TestHealthyZeroFileFakeCardNup(const std::filesystem::path& dir) {
    // Matches make_fake_card.py: 0 files, exefs=0xFFFFFFFF, valid superblock+TOC+metadata.
    const auto p = dir / "fake_card.nup";
    FakePkg pkg;
    pkg.exefs = 0xFFFFFFFFu;
    pkg.romfs = 0xFFFFFFFFu;
    pkg.meta_at = 0x80 + 0x30 + 0x40 + 0x18;
    pkg.meta_size = 64;
    auto img = BuildNup({pkg}, {});
    WriteBytes(p, img);
    const Nup::HealthReport r = Nup::CheckHealth(p.string());
    Check(r.status == Nup::HealthStatus::Ok, "0-file preview NUP is Ok");
    Check(r.files == 0, "0-file preview NUP reports 0 files");
}

void TestTruncatedDataAndMetadata(const std::filesystem::path& dir) {
    // 1. File entry reaches past EOF.
    {
        const auto p = dir / "trunc_data.nup";
        std::vector<FakeFile> files{
            {"main", 4096, 4096, 4096, 0},
            {"Movie/Intro.mp4", 65536, 8192, 65536, 0},
        };
        auto img = BuildNup({FakePkg{}}, files, 16384); // File claims up to 73728, disk has 16384.
        WriteBytes(p, img);
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "truncated file data is Error");
        Check(r.furthest_data == 8192 + 65536, "truncated data reports exact furthest_data");
        Check(r.missing_bytes == (8192 + 65536) - 16384, "truncated data reports exact missing_bytes");
        Check(!r.issues.empty(), "truncated data reports issue");
    }
    // 2. Package metadata block reaches past EOF.
    {
        const auto p = dir / "trunc_meta.nup";
        FakePkg pkg;
        pkg.meta_at = 4096;
        pkg.meta_size = 8192;
        auto img = BuildNup({pkg}, {}, 4096 + 1024);
        WriteBytes(p, img);
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "truncated package metadata is Error");
        Check(r.missing_bytes == 7168, "truncated metadata computes exact missing_bytes");
    }
    // 3. File entry offset+size uint64 overflow.
    {
        const auto p = dir / "offset_overflow.nup";
        std::vector<FakeFile> files{
            {"overflow.bin", 100, std::numeric_limits<std::uint64_t>::max() - 10, 100, 0},
        };
        auto img = BuildNup({FakePkg{}}, files, 4096);
        WriteBytes(p, img);
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "file entry offset overflow is Error");
    }
}

void TestOversizedZstdAndBoundary(const std::filesystem::path& dir) {
    // 1. Exact threshold (256 MiB) -> Ok.
    {
        const auto p = dir / "zstd_exact.nup";
        std::vector<FakeFile> files{
            {"exact.pak", Nup::kOversizedZstdBytes, 4096, 1024, 1},
        };
        WriteBytes(p, BuildNup({FakePkg{}}, files));
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Ok, "exact 256 MiB zstd is Ok");
        Check(r.oversized_zstd_files == 0, "exact 256 MiB zstd count is 0");
    }
    // 2. Threshold + 1 -> Warning.
    {
        const auto p = dir / "zstd_over1.nup";
        std::vector<FakeFile> files{
            {"over1.pak", Nup::kOversizedZstdBytes + 1, 4096, 1024, 1},
        };
        WriteBytes(p, BuildNup({FakePkg{}}, files));
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Warning, "256 MiB + 1 zstd triggers Warning");
        Check(r.oversized_zstd_files == 1, "oversized zstd count is 1");
    }
    // 3. Multiple zstd entries, picks largest and sanitizes control bytes in filename.
    {
        const auto p = dir / "zstd_multi.nup";
        std::vector<FakeFile> files{
            {"small.pak", 300ull << 20, 4096, 1000, 1},
            {"World\tData\nHuge.pak", 420ull << 20, 6000, 2000, 1},
            {"raw_huge.bin", 1024ull << 20, 8000, 1000, 0}, // Raw file > 256 MiB must NOT warn!
        };
        WriteBytes(p, BuildNup({FakePkg{}}, files));
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Warning, "oversized zstd multi is Warning");
        Check(r.oversized_zstd_files == 2, "two oversized zstd files counted (raw ignored)");
        Check(r.largest_zstd_unpacked == (420ull << 20), "largest zstd unpacked is 420 MiB");
        Check(r.largest_zstd_name == "World Data Huge.pak", "control characters sanitized in largest_zstd_name: " + r.largest_zstd_name);
        Check(r.advice.find("Room") != std::string::npos || r.advice.find("room") != std::string::npos,
              "oversized zstd advice mentions Room setting");
    }
}

void TestCodecsAndPackages(const std::filesystem::path& dir) {
    // 1. Unknown codec (codec == 2) -> Error.
    {
        const auto p = dir / "bad_codec.nup";
        WriteBytes(p, BuildNup({FakePkg{}}, {{"mystery.dat", 1024, 4096, 512, 2}}));
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "unknown codec is Error");
        Check(r.unknown_codec_files == 1, "unknown codec count is 1");
    }
    // 2. DLC-only container -> Error.
    {
        const auto p = dir / "dlc_only.nup";
        FakePkg dlc;
        dlc.kind = 2;
        dlc.exefs = 0xFFFFFFFFu;
        WriteBytes(p, BuildNup({dlc}, {{"dlc.dat", 256, 4096, 256, 0}}));
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "DLC-only container is Error");
    }
    // 3. Non-empty game container without ExeFS -> Error.
    {
        const auto p = dir / "no_exefs.nup";
        FakePkg base;
        base.kind = 0;
        base.exefs = 0xFFFFFFFFu;
        WriteBytes(p, BuildNup({base}, {{"rom.dat", 256, 4096, 256, 0}}));
        const Nup::HealthReport r = Nup::CheckHealth(p.string());
        Check(r.status == Nup::HealthStatus::Error, "non-empty container without ExeFS is Error");
        Check(!r.has_program, "has_program is false");
    }
}

void TestMultiPartAndStreaming(const std::filesystem::path& dir) {
    // Build a container with 2500 files (exercises multi-chunk streaming > 512 entries/chunk)
    // and split it into 3 parts so TOC, file entries, and string pool cross part boundaries.
    std::vector<FakeFile> files;
    files.reserve(2500);
    std::uint64_t off = 260000;
    for (int i = 0; i < 2500; ++i) {
        FakeFile f;
        f.name = "asset_" + std::to_string(i) + ".bin";
        f.at = off;
        f.kept_size = 16;
        if (i == 2499) {
            f.name = "final_zstd_entry.pack";
            f.unpacked_size = 32ull << 20;
            f.codec = 1;
        } else {
            f.unpacked_size = 16;
            f.codec = 0;
        }
        off += 16;
        files.push_back(std::move(f));
    }
    const auto img = BuildNup({FakePkg{}}, files);
    const std::size_t part_sz = 120000; // 3 parts: 120000, 120000, remainder
    const auto p1 = dir / "split.nup.001";
    const auto p2 = dir / "split.nup.002";
    const auto p3 = dir / "split.nup.003";
    WriteBytes(p1, img.data(), part_sz);
    WriteBytes(p2, img.data() + part_sz, part_sz);
    WriteBytes(p3, img.data() + 2 * part_sz, img.size() - 2 * part_sz);

    // 1. Healthy 3-part NUP.
    {
        const Nup::HealthReport r = Nup::CheckHealth({
            {p1.string(), part_sz},
            {p2.string(), part_sz},
            {p3.string(), img.size() - 2 * part_sz},
        });
        Check(r.status == Nup::HealthStatus::Ok, "healthy 3-part 2500-file NUP is Ok");
        Check(r.parts == 3, "reports 3 parts");
        Check(r.mismatched_part == 0, "no part size mismatch");
        Check(r.files == 2500, "all 2500 entries streamed accurately");
        Check(r.raw_files == 2499, "2499 raw files counted");
        Check(r.zstd_files == 1, "1 zstd file counted");
        Check(r.largest_zstd_name == "final_zstd_entry.pack", "string pool read across part boundary: " + r.largest_zstd_name);
        Check(r.missing_bytes == 0, "0 missing bytes on complete 3-part NUP");
    }

    // 2. Missing final part (only 2 parts present) -> Error with exact missing_bytes.
    {
        const Nup::HealthReport r = Nup::CheckHealth({
            {p1.string(), part_sz},
            {p2.string(), part_sz},
        });
        Check(r.status == Nup::HealthStatus::Error, "missing final split part is Error");
        Check(r.missing_bytes == img.size() - 2 * part_sz, "missing final split part computes exact missing_bytes");
    }

    // 3. Mismatched intermediate part size -> Warning with mismatched_part == 2.
    {
        const auto m1 = dir / "mismatch.nup.001";
        const auto m2 = dir / "mismatch.nup.002";
        const auto m3 = dir / "mismatch.nup.003";
        const std::size_t s1 = 100000;
        const std::size_t s2 = 120000; // Different from part 1!
        WriteBytes(m1, img.data(), s1);
        WriteBytes(m2, img.data() + s1, s2);
        WriteBytes(m3, img.data() + s1 + s2, img.size() - s1 - s2);
        const Nup::HealthReport r = Nup::CheckHealth({
            {m1.string(), s1},
            {m2.string(), s2},
            {m3.string(), img.size() - s1 - s2},
        });
        Check(r.status == Nup::HealthStatus::Warning, "mismatched intermediate part size triggers Warning");
        Check(r.mismatched_part == 2, "mismatched_part reports 2");
        Check(r.missing_bytes == 0, "total bytes still complete");
    }
}

} // namespace

int main() {
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "nx2_flow_health_test";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);

    TestFormatBytes();
    TestEmptyAndMissing(tmp);
    TestBadMagicAndTruncatedHeader(tmp);
    TestTruncatedToc(tmp);
    TestCorruptTocBounds(tmp);
    TestHealthySingleFile(tmp);
    TestHealthyZeroFileFakeCardNup(tmp);
    TestTruncatedDataAndMetadata(tmp);
    TestOversizedZstdAndBoundary(tmp);
    TestCodecsAndPackages(tmp);
    TestMultiPartAndStreaming(tmp);

    std::filesystem::remove_all(tmp, ec);
    std::printf("health_test: %d checks, %d failed\n", g_checked, g_failed);
    return g_failed == 0 ? 0 : 1;
}
