// SPDX-License-Identifier: GPL-2.0-or-later

// Host unit tests for Nup::FromNacp and Nup::Read NACP handling:
//   - format 0 (uncompressed NacpLanguageEntry[16]), including legacy short blobs;
//   - format 1 (Horizon OS 21.0.0+ raw-DEFLATE compressed NacpLanguageEntry[32] at offset 0x3215);
//   - malformed, truncated, empty, oversized, and overflow compressed streams.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "nup.h"

namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

int g_checked = 0;
int g_failed = 0;

void Check(bool held, const std::string& what) {
    ++g_checked;
    if (!held) {
        ++g_failed;
        std::printf("FAILED: %s\n", what.c_str());
    }
}

void Wr16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>((v >> 8) & 0xFF);
}

void Wr32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>((v >> 8) & 0xFF);
    p[2] = static_cast<u8>((v >> 16) & 0xFF);
    p[3] = static_cast<u8>((v >> 24) & 0xFF);
}

void Wr64(u8* p, u64 v) {
    Wr32(p, static_cast<u32>(v & 0xFFFFFFFFu));
    Wr32(p + 4, static_cast<u32>(v >> 32));
}

// Raw-DEFLATE (wbits = -15) test vectors generated with Python zlib.compressobj:
// 1. 32 * 0x300 = 0x6000 bytes; entry 0 has name "Horizon Echo", author "Studio North".
static constexpr u8 kDeflateEntry0[] = {
    0xed, 0xd0, 0xb1, 0x0d, 0x80, 0x20, 0x00, 0x04, 0x40, 0x47, 0x61, 0x19, 0x13, 0x2b, 0x1b,
    0x47, 0x90, 0x02, 0x1a, 0x3f, 0x21, 0xd8, 0x38, 0xbd, 0x3b, 0xd8, 0x7a, 0x37, 0xc2, 0x6d,
    0x19, 0xfd, 0xc9, 0x55, 0xd6, 0xb3, 0x65, 0xe1, 0x77, 0x8e, 0x79, 0xd7, 0x9e, 0xb2, 0x67,
    0xcc, 0x66, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xbb, 0x17,
};

// 2. 32 * 0x300 = 0x6000 bytes; entries 0..19 empty, entry 20 has name "Extended Slot Title",
//    author "Extended Publisher" (proves entries beyond legacy index 15 are read).
static constexpr u8 kDeflateEntry20[] = {
    0xed, 0xd1, 0xc1, 0x09, 0x80, 0x30, 0x10, 0x00, 0xc1, 0x94, 0x92, 0x66, 0xfc, 0x0b, 0x5a,
    0x81, 0xe4, 0xc0, 0x40, 0x50, 0xd0, 0x08, 0x96, 0x6f, 0x1b, 0x8a, 0x33, 0x25, 0xec, 0xa6,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xbc, 0xc5, 0x70, 0xf7, 0xd8, 0x4a, 0x94, 0x3c, 0xb5, 0xbd, 0xe7, 0xb9, 0xf6, 0x16, 0xa2,
    0xfc, 0xf1, 0xff, 0x78, 0x2d, 0xad, 0x9e, 0x6b, 0x1c, 0x9a, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7c, 0xc9, 0x03,
};

// 3. 33 * 0x300 = 0x6300 bytes (> 0x6000 max output buffer): must fail cleanly without overflow.
static constexpr u8 kDeflateOversized[] = {
    0xed, 0xc1, 0x31, 0x11, 0x00, 0x00, 0x08, 0x04, 0x20, 0xb3, 0x19, 0xc5, 0xfd, 0xcf, 0xfe,
    0x9b, 0x1d, 0x9c, 0x81, 0xde, 0x4c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xc0, 0xd3, 0x01,
};

std::vector<u8> MakeCompressedNacp(const u8* payload, std::size_t payload_len, u16 declared_size,
                                   const char* display_version) {
    std::vector<u8> nacp(0x4000, 0);
    Wr16(nacp.data(), declared_size);
    if (payload && payload_len > 0) {
        std::memcpy(nacp.data() + 2, payload, payload_len);
    }
    if (display_version) {
        std::strncpy(reinterpret_cast<char*>(nacp.data() + 0x3060), display_version, 0x10);
    }
    nacp[0x3215] = 1; // NacpStruct::titles_data_format
    return nacp;
}

void TestFormat0Uncompressed() {
    std::vector<u8> nacp(0x4000, 0);
    // Leave entry 0 empty, populate entry 2.
    std::memcpy(nacp.data() + 2 * 0x300, "Plain Game", 10);
    std::memcpy(nacp.data() + 2 * 0x300 + 0x200, "Plain Dev", 9);
    std::memcpy(nacp.data() + 0x3060, "1.0.2", 5);
    nacp[0x3215] = 0;

    std::string title, publisher, version;
    Nup::FromNacp(nacp, title, publisher, version);
    Check(title == "Plain Game", "format 0 title extracted");
    Check(publisher == "Plain Dev", "format 0 publisher extracted");
    Check(version == "1.0.2", "format 0 version extracted");
}

void TestFormat0LegacyShortBlob() {
    std::vector<u8> nacp(0x3070, 0);
    std::memcpy(nacp.data(), "Short Blob Game", 15);
    std::memcpy(nacp.data() + 0x200, "Short Dev", 9);
    std::memcpy(nacp.data() + 0x3060, "0.9", 3);

    std::string title, publisher, version;
    Nup::FromNacp(nacp, title, publisher, version);
    Check(title == "Short Blob Game", "short blob format 0 title extracted");
    Check(publisher == "Short Dev", "short blob format 0 publisher extracted");
    Check(version == "0.9", "short blob format 0 version extracted");
}

void TestTooSmallBlob() {
    std::vector<u8> nacp(0x306F, 0x41);
    std::string title, publisher, version;
    Nup::FromNacp(nacp, title, publisher, version);
    Check(title.empty() && publisher.empty() && version.empty(), "blob < 0x3070 ignored");
}

void TestFormat1CompressedEntry0() {
    const auto nacp = MakeCompressedNacp(kDeflateEntry0, sizeof(kDeflateEntry0),
                                         static_cast<u16>(sizeof(kDeflateEntry0)), "2.1.0");
    std::string title, publisher, version;
    Nup::FromNacp(nacp, title, publisher, version);
    Check(title == "Horizon Echo", "format 1 entry 0 title: got \"" + title + "\"");
    Check(publisher == "Studio North", "format 1 entry 0 publisher: got \"" + publisher + "\"");
    Check(version == "2.1.0", "format 1 entry 0 version: got \"" + version + "\"");
}

void TestFormat1CompressedEntry20() {
    const auto nacp = MakeCompressedNacp(kDeflateEntry20, sizeof(kDeflateEntry20),
                                         static_cast<u16>(sizeof(kDeflateEntry20)), "3.4.5");
    std::string title, publisher, version;
    Nup::FromNacp(nacp, title, publisher, version);
    Check(title == "Extended Slot Title", "format 1 entry 20 title: got \"" + title + "\"");
    Check(publisher == "Extended Publisher", "format 1 entry 20 publisher: got \"" + publisher + "\"");
    Check(version == "3.4.5", "format 1 entry 20 version: got \"" + version + "\"");
}

void TestFormat1PreservesExistingTitle() {
    const auto nacp = MakeCompressedNacp(kDeflateEntry0, sizeof(kDeflateEntry0),
                                         static_cast<u16>(sizeof(kDeflateEntry0)), "2.1.0");
    std::string title = "Already Known", publisher = "Known Pub", version;
    Nup::FromNacp(nacp, title, publisher, version);
    Check(title == "Already Known", "existing title preserved");
    Check(publisher == "Known Pub", "existing publisher preserved");
    Check(version == "2.1.0", "empty version filled when title already known");
}

void TestFormat1ZeroLengthRejected() {
    const auto nacp = MakeCompressedNacp(kDeflateEntry0, sizeof(kDeflateEntry0), 0, "1.1.0");
    std::string title, publisher, version;
    Nup::FromNacp(nacp, title, publisher, version);
    Check(title.empty(), "format 1 zero compressed size yields empty title");
    Check(publisher.empty(), "format 1 zero compressed size yields empty publisher");
    Check(version == "1.1.0", "format 1 zero compressed size still extracts display version");
}

void TestFormat1OversizedLengthRejected() {
    for (const u16 bad_size : {static_cast<u16>(0x2FFF), static_cast<u16>(0xFFFF)}) {
        const auto nacp = MakeCompressedNacp(kDeflateEntry0, sizeof(kDeflateEntry0), bad_size, "1.2.0");
        std::string title, publisher, version;
        Nup::FromNacp(nacp, title, publisher, version);
        Check(title.empty(), "format 1 oversized size rejected (title empty)");
        Check(publisher.empty(), "format 1 oversized size rejected (publisher empty)");
        Check(version == "1.2.0", "format 1 oversized size still extracts display version");
    }
}

void TestFormat1TruncatedAndCorruptStreams() {
    // Truncated stream (only first 12 bytes of a 73-byte DEFLATE stream).
    {
        const auto nacp = MakeCompressedNacp(kDeflateEntry0, 12, 12, "1.3.0");
        std::string title, publisher, version;
        Nup::FromNacp(nacp, title, publisher, version);
        Check(title.empty(), "format 1 truncated stream leaves title empty");
        Check(publisher.empty(), "format 1 truncated stream leaves publisher empty");
        Check(version == "1.3.0", "format 1 truncated stream still extracts display version");
    }
    // Corrupt stream (invalid DEFLATE block type 3).
    {
        const u8 corrupt[] = {0xFF, 0xFF, 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
        const auto nacp = MakeCompressedNacp(corrupt, sizeof(corrupt),
                                             static_cast<u16>(sizeof(corrupt)), "1.4.0");
        std::string title, publisher, version;
        Nup::FromNacp(nacp, title, publisher, version);
        Check(title.empty(), "format 1 corrupt stream leaves title empty");
        Check(publisher.empty(), "format 1 corrupt stream leaves publisher empty");
        Check(version == "1.4.0", "format 1 corrupt stream still extracts display version");
    }
    // Valid DEFLATE stream whose decompressed output exceeds 0x6000 bytes.
    {
        const auto nacp = MakeCompressedNacp(kDeflateOversized, sizeof(kDeflateOversized),
                                             static_cast<u16>(sizeof(kDeflateOversized)), "1.5.0");
        std::string title, publisher, version;
        Nup::FromNacp(nacp, title, publisher, version);
        Check(title.empty(), "format 1 decompressed > 0x6000 rejected (title empty)");
        Check(publisher.empty(), "format 1 decompressed > 0x6000 rejected (publisher empty)");
        Check(version == "1.5.0", "format 1 decompressed > 0x6000 still extracts display version");
    }
}

void TestUnknownFormatIgnored() {
    std::vector<u8> nacp(0x4000, 0);
    std::memcpy(nacp.data(), "GarbageIfParsed", 15);
    std::memcpy(nacp.data() + 0x3060, "9.9.9", 5);
    nacp[0x3215] = 2;

    std::string title, publisher, version;
    Nup::FromNacp(nacp, title, publisher, version);
    Check(title.empty(), "unknown titles_data_format does not read raw bytes as title");
    Check(publisher.empty(), "unknown titles_data_format does not read raw bytes as publisher");
    Check(version == "9.9.9", "unknown titles_data_format still extracts display version");
}

void TestEndToEndNupReadWithCompressedNacp() {
    const auto nacp = MakeCompressedNacp(kDeflateEntry20, sizeof(kDeflateEntry20),
                                         static_cast<u16>(sizeof(kDeflateEntry20)), "4.0.0");

    // Build a minimal single-package NUP container with only kNpdm ("META") and kNacp facts.
    const u64 toc_at = 0x80;
    const u64 toc_size = 0x30 + 0x40;
    const u64 meta_at = toc_at + toc_size;
    std::vector<u8> meta;
    // Fact 0: kNpdm ("META")
    meta.resize(8 + 4, 0);
    Wr16(meta.data(), 0);
    Wr32(meta.data() + 4, 4);
    std::memcpy(meta.data() + 8, "META", 4);
    // Fact 1: kNacp
    const std::size_t nacp_fact_off = meta.size();
    meta.resize(nacp_fact_off + 8 + nacp.size(), 0);
    Wr16(meta.data() + nacp_fact_off, 1);
    Wr32(meta.data() + nacp_fact_off + 4, static_cast<u32>(nacp.size()));
    std::memcpy(meta.data() + nacp_fact_off + 8, nacp.data(), nacp.size());

    std::vector<u8> file(meta_at + meta.size(), 0);
    // Superblock
    std::memcpy(file.data(), "NUP\x01", 4);
    Wr64(file.data() + 8, toc_at);
    Wr64(file.data() + 16, toc_size);
    Wr32(file.data() + 0x48, 1); // package count
    // TOC
    std::memcpy(file.data() + toc_at, "TOC\x01", 4);
    u8* pk = file.data() + toc_at + 0x30;
    Wr64(pk, 0x0100AABBCCDD0000ull);
    Wr32(pk + 0x14, 0x10000);
    pk[0x18] = 0; // base game
    Wr64(pk + 0x20, meta_at);
    Wr32(pk + 0x28, static_cast<u32>(meta.size()));
    // Meta block
    std::memcpy(file.data() + meta_at, meta.data(), meta.size());

    const std::string path = "/tmp/nx2_flow_nacp_test.nup";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    Check(f != nullptr, "temp NUP created");
    if (!f) {
        return;
    }
    const bool wrote = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    std::fclose(f);
    Check(wrote, "temp NUP written");

    Nup::Info info;
    std::string error;
    const bool ok = Nup::Read(path, info, error);
    std::remove(path.c_str());

    Check(ok, "Nup::Read succeeded: " + error);
    Check(info.title_id == 0x0100AABBCCDD0000ull, "Nup::Read title_id");
    Check(info.title == "Extended Slot Title", "Nup::Read compressed NACP title: \"" + info.title + "\"");
    Check(info.publisher == "Extended Publisher", "Nup::Read compressed NACP publisher: \"" + info.publisher + "\"");
    Check(info.display_version == "4.0.0", "Nup::Read compressed NACP display_version: \"" + info.display_version + "\"");
}

} // namespace

int main() {
    TestFormat0Uncompressed();
    TestFormat0LegacyShortBlob();
    TestTooSmallBlob();
    TestFormat1CompressedEntry0();
    TestFormat1CompressedEntry20();
    TestFormat1PreservesExistingTitle();
    TestFormat1ZeroLengthRejected();
    TestFormat1OversizedLengthRejected();
    TestFormat1TruncatedAndCorruptStreams();
    TestUnknownFormatIgnored();
    TestEndToEndNupReadWithCompressedNacp();

    std::printf("%d checks, %d failed\n", g_checked, g_failed);
    return g_failed == 0 ? 0 : 1;
}
