// SPDX-License-Identifier: GPL-2.0-or-later

// Host unit tests for Flow::CopyFolder atomic file staging:
//   - normal recursive folder copy (including empty files and multi-block files);
//   - overwriting pre-existing destination files;
//   - simulated mid-write failure (RLIMIT_FSIZE -> EFBIG) on both a pre-existing
//     destination file (must stay intact) and a fresh destination (no partial file),
//     with zero leftover .tmp files in every case.

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "library.h"

namespace {

int g_checked = 0;
int g_failed = 0;

void Check(bool held, const std::string& what) {
    ++g_checked;
    if (!held) {
        ++g_failed;
        std::printf("FAILED: %s\n", what.c_str());
    }
}

bool WriteRawFile(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

bool WriteTextFile(const std::string& path, const std::string& text) {
    std::vector<std::uint8_t> data(text.begin(), text.end());
    return WriteRawFile(path, data);
}

std::vector<std::uint8_t> ReadRawFile(const std::string& path, bool* exists = nullptr) {
    std::vector<std::uint8_t> out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (exists) {
            *exists = false;
        }
        return out;
    }
    if (exists) {
        *exists = true;
    }
    std::uint8_t buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) {
        out.insert(out.end(), buf, buf + n);
    }
    std::fclose(f);
    return out;
}

std::string ReadTextFile(const std::string& path, bool* exists = nullptr) {
    const auto raw = ReadRawFile(path, exists);
    return std::string(raw.begin(), raw.end());
}

bool PathExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::vector<std::uint8_t> MakePattern(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> out(size);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = static_cast<std::uint8_t>((i * 31u + seed) & 0xFFu);
    }
    return out;
}

void TestNormalRecursiveCopy() {
    Flow::MakeFolders("/src_normal/sub/deep");
    const auto large = MakePattern(128u * 1024u, 0x17);
    const auto nested = MakePattern(4096u, 0x42);

    Check(WriteTextFile(Flow::OnCard("/src_normal/root.txt"), "root-save-payload"),
          "create root.txt");
    Check(WriteRawFile(Flow::OnCard("/src_normal/empty.bin"), {}), "create empty.bin");
    Check(WriteRawFile(Flow::OnCard("/src_normal/sub/nested.dat"), nested), "create nested.dat");
    Check(WriteRawFile(Flow::OnCard("/src_normal/sub/deep/large.bin"), large), "create large.bin");

    std::string error;
    const bool ok = Flow::CopyFolder("/src_normal", "/dst_normal", error);
    Check(ok, "normal CopyFolder succeeded: " + error);
    Check(ReadTextFile(Flow::OnCard("/dst_normal/root.txt")) == "root-save-payload",
          "root.txt copied verbatim");
    bool empty_exists = false;
    const auto empty_bytes = ReadRawFile(Flow::OnCard("/dst_normal/empty.bin"), &empty_exists);
    Check(empty_exists && empty_bytes.empty(), "empty.bin copied as 0-byte file");
    Check(ReadRawFile(Flow::OnCard("/dst_normal/sub/nested.dat")) == nested,
          "nested.dat copied verbatim");
    Check(ReadRawFile(Flow::OnCard("/dst_normal/sub/deep/large.bin")) == large,
          "large.bin copied verbatim");

    Check(!PathExists(Flow::OnCard("/dst_normal/root.txt.tmp")), "no root.txt.tmp leftover");
    Check(!PathExists(Flow::OnCard("/dst_normal/empty.bin.tmp")), "no empty.bin.tmp leftover");
    Check(!PathExists(Flow::OnCard("/dst_normal/sub/nested.dat.tmp")),
          "no nested.dat.tmp leftover");
    Check(!PathExists(Flow::OnCard("/dst_normal/sub/deep/large.bin.tmp")),
          "no large.bin.tmp leftover");
}

void TestOverwriteExistingDestination() {
    Flow::MakeFolders("/src_over/sub");
    Flow::MakeFolders("/dst_over/sub");

    Check(WriteTextFile(Flow::OnCard("/dst_over/save.dat"), "OLD_SAVE_CONTENT_v1"),
          "seed existing save.dat");
    Check(WriteTextFile(Flow::OnCard("/dst_over/sub/meta.bin"), "OLD_META"),
          "seed existing meta.bin");

    const auto new_save = MakePattern(64u * 1024u, 0x99);
    Check(WriteRawFile(Flow::OnCard("/src_over/save.dat"), new_save), "create new save.dat");
    Check(WriteTextFile(Flow::OnCard("/src_over/sub/meta.bin"), "NEW_META_v2"),
          "create new meta.bin");

    std::string error;
    const bool ok = Flow::CopyFolder("/src_over", "/dst_over", error);
    Check(ok, "overwrite CopyFolder succeeded: " + error);
    Check(ReadRawFile(Flow::OnCard("/dst_over/save.dat")) == new_save,
          "existing save.dat replaced atomically");
    Check(ReadTextFile(Flow::OnCard("/dst_over/sub/meta.bin")) == "NEW_META_v2",
          "existing meta.bin replaced atomically");
    Check(!PathExists(Flow::OnCard("/dst_over/save.dat.tmp")), "no save.dat.tmp after overwrite");
    Check(!PathExists(Flow::OnCard("/dst_over/sub/meta.bin.tmp")),
          "no meta.bin.tmp after overwrite");
}

void TestSimulatedWriteErrorPreservesOriginalAndCleansTmp() {
    Flow::MakeFolders("/src_fail");
    Flow::MakeFolders("/dst_fail_existing");

    const std::string original = "ORIGINAL_SAVE_DATA_MUST_SURVIVE";
    const auto big_new = MakePattern(128u * 1024u, 0xAB);

    Check(WriteRawFile(Flow::OnCard("/src_fail/save.bin"), big_new),
          "create 128 KiB source save.bin");
    Check(WriteTextFile(Flow::OnCard("/dst_fail_existing/save.bin"), original),
          "seed existing destination save.bin");

    // Ignore SIGXFSZ and lower soft RLIMIT_FSIZE to 4096 bytes so fwrite/fflush/fclose fails
    // with EFBIG mid-copy inside the real CopyFolder implementation.
    struct rlimit orig_limit {};
    Check(getrlimit(RLIMIT_FSIZE, &orig_limit) == 0, "getrlimit(RLIMIT_FSIZE) succeeded");

    auto* prev_sig = std::signal(SIGXFSZ, SIG_IGN);
    struct rlimit small_limit = orig_limit;
    small_limit.rlim_cur = 4096;
    const bool limited = setrlimit(RLIMIT_FSIZE, &small_limit) == 0;
    Check(limited, "setrlimit(RLIMIT_FSIZE, 4096) succeeded");

    if (limited) {
        // Case 1: Destination already has a valid save file -> must stay intact, no .tmp left.
        std::string err1;
        const bool ok1 = Flow::CopyFolder("/src_fail", "/dst_fail_existing", err1);

        // Case 2: Fresh destination -> must not leave a truncated partial file or .tmp.
        std::string err2;
        const bool ok2 = Flow::CopyFolder("/src_fail", "/dst_fail_fresh", err2);

        // Restore limit before running assertions / cleanup.
        setrlimit(RLIMIT_FSIZE, &orig_limit);
        std::signal(SIGXFSZ, prev_sig);

        Check(!ok1 && !err1.empty(), "CopyFolder fails on EFBIG over existing file");
        bool exists1 = false;
        const std::string after1 = ReadTextFile(Flow::OnCard("/dst_fail_existing/save.bin"), &exists1);
        Check(exists1 && after1 == original,
              "original destination save.bin intact after write error (got " +
                  std::to_string(after1.size()) + " bytes)");
        Check(!PathExists(Flow::OnCard("/dst_fail_existing/save.bin.tmp")),
              "no save.bin.tmp leftover after overwrite write error");

        Check(!ok2 && !err2.empty(), "CopyFolder fails on EFBIG into fresh destination");
        Check(!PathExists(Flow::OnCard("/dst_fail_fresh/save.bin")),
              "no truncated save.bin left in fresh destination after write error");
        Check(!PathExists(Flow::OnCard("/dst_fail_fresh/save.bin.tmp")),
              "no save.bin.tmp leftover in fresh destination after write error");
    } else {
        std::signal(SIGXFSZ, prev_sig);
    }
}

} // namespace

int main() {
    char tmpl[] = "/tmp/nx2_flow_copy_test_XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) {
        std::printf("FAILED: mkdtemp\n");
        return 1;
    }
    Flow::SetCardRoot(dir);

    TestNormalRecursiveCopy();
    TestOverwriteExistingDestination();
    TestSimulatedWriteErrorPreservesOriginalAndCleansTmp();

    Flow::RemoveFolder("");
    rmdir(dir);

    std::printf("%d checks, %d failed\n", g_checked, g_failed);
    return g_failed == 0 ? 0 : 1;
}
