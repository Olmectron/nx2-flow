// SPDX-License-Identifier: GPL-2.0-or-later

#include "nup.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// The rules for a container kept in parts: the game loader's own header, so that both know the
// same game by the same rules. It is C, every function static, and it keeps large tables of its
// own: this is the one file that takes it in, and one thread at a time is let into it. Not all
// of what it has is used here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
extern "C" {
#include "nupparts.h"
}
#pragma GCC diagnostic pop

namespace Nup {

const int kMostParts = NUP_PARTS_MAX;

namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

std::mutex g_parts_mutex;
NupPart g_parts[NUP_PARTS_MAX]; // (what the header fills: too much for a stack)

u16 Rd16(const u8* p) {
    return static_cast<u16>(p[0] | p[1] << 8);
}
u32 Rd32(const u8* p) {
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}
u64 Rd64(const u8* p) {
    return static_cast<u64>(Rd32(p)) | static_cast<u64>(Rd32(p + 4)) << 32;
}

bool ReadFileAt(std::FILE* f, u64 at, void* to, std::size_t size) {
#if defined(_WIN32) && !defined(__CYGWIN__)
    if (_fseeki64(f, static_cast<long long>(at), SEEK_SET) != 0) {
#else
    if (fseeko(f, static_cast<off_t>(at), SEEK_SET) != 0) {
#endif
        return false;
    }
    return std::fread(to, 1, size, f) == size;
}

/// A container's bytes, whichever files they are in. A whole file is read as it always was;
/// with parts, a place in the container is a place in one of them, and what is read may begin
/// in one part and end in another.
class Bytes {
public:
    explicit Bytes(const std::vector<Part>& from) : parts{from} {
        if (parts.size() > 1) {
            // (the header's own table, for its sizes: the files are opened by the paths we were given)
            table.resize(parts.size());
            for (std::size_t k = 0; k < parts.size(); ++k) {
                table[k].path[0] = 0;
                table[k].size = parts[k].size;
            }
        }
    }
    ~Bytes() {
        if (f) {
            std::fclose(f);
        }
    }
    Bytes(const Bytes&) = delete;
    Bytes& operator=(const Bytes&) = delete;

    /// The part that has the superblock, opened.
    bool Open() {
        return !parts.empty() && Open(0);
    }

    bool ReadAt(u64 at, void* to, std::size_t size) {
        if (table.empty()) {
            return ReadFileAt(f, at, to, size);
        }
        u8* p = static_cast<u8*>(to);
        while (size != 0) {
            uint64_t in_part = 0, left = 0;
            const int k = nup_parts_at(table.data(), static_cast<int>(table.size()), at, &in_part, &left);
            if (k < 0 || !Open(k)) {
                return false;
            }
            const std::size_t take = static_cast<std::size_t>(std::min<u64>(size, left));
            if (!ReadFileAt(f, in_part, p, take)) {
                return false;
            }
            p += take;
            at += take;
            size -= take;
        }
        return true;
    }

private:
    bool Open(int k) {
        if (k == open_part) {
            return true;
        }
        if (f) {
            std::fclose(f);
        }
        f = std::fopen(parts[static_cast<std::size_t>(k)].path.c_str(), "rb");
        open_part = f ? k : -1;
        return f != nullptr;
    }

    const std::vector<Part>& parts;
    std::vector<NupPart> table; // Empty for a whole file.
    std::FILE* f{};
    int open_part{-1};
};

// The facts of a package, in their order. A container numbers them from 0 or from 1 (two
// versions of the tool that makes them): which, is told by where an NPDM is.
enum Fact { kNpdm, kNacp, kIcon, kDisplayVersion, kTitle, kPublisher };

struct Package {
    u64 title{};
    u32 version{};
    int kind{};
    u64 meta_at{};
    u32 meta_size{};
    std::map<int, std::vector<u8>> meta; // By the number the container gives each.
};

std::string Text(const std::vector<u8>& v) {
    std::size_t n = 0;
    while (n < v.size() && v[n] != 0) {
        ++n;
    }
    return std::string{reinterpret_cast<const char*>(v.data()), n};
}

/// A NACP's own title and publisher (the first language that has one) and display version:
/// for a container that gives the NACP and not the texts.
void FromNacp(const std::vector<u8>& nacp, std::string& title, std::string& publisher,
              std::string& version) {
    if (nacp.size() < 0x3070) {
        return;
    }
    for (int lang = 0; lang < 16 && title.empty(); ++lang) {
        const u8* entry = nacp.data() + lang * 0x300;
        const std::size_t tl = strnlen(reinterpret_cast<const char*>(entry), 0x200);
        if (tl != 0) {
            title.assign(reinterpret_cast<const char*>(entry), tl);
            const std::size_t pl = strnlen(reinterpret_cast<const char*>(entry + 0x200), 0x100);
            publisher.assign(reinterpret_cast<const char*>(entry + 0x200), pl);
        }
    }
    if (version.empty()) {
        const std::size_t vl = strnlen(reinterpret_cast<const char*>(nacp.data() + 0x3060), 0x10);
        version.assign(reinterpret_cast<const char*>(nacp.data() + 0x3060), vl);
    }
}

} // namespace

int Entry(const std::string& dir, const std::string& name, bool is_folder, Found& out) {
    out = Found{};
    std::lock_guard lock{g_parts_mutex};
    const int n = nup_parts_entry(dir.c_str(), name.c_str(), is_folder ? 1 : 0, g_parts, NUP_PARTS_MAX);
    if (n <= 0) {
        return n;
    }
    for (int k = 0; k < n; ++k) {
        out.parts.push_back({g_parts[k].path, g_parts[k].size});
    }
    out.size = nup_parts_size(g_parts, n);
    // (a part's path is the header's own making: a folder, a slash, a name)
    const std::string& first = out.parts[0].path;
    const std::size_t slash = first.rfind('/');
    const std::string folder = first.substr(0, slash), file = first.substr(slash + 1);
    NupPartName as;
    const bool part = nup_part_name(file.c_str(), &as) != 0;
    if (part && as.kind == NUP_PART_BARE) {
        out.nameless = true;
        out.given = folder;
        out.name = folder.substr(folder.find_last_of("/:") + 1);
    } else {
        out.given = first;
        // (a whole file's name may look like a part's, "Some.Name.8.nup": what it has in common
        // with other parts is only asked of a file that has them)
        out.name = n > 1 && part ? as.stem : file;
    }
    out.called_nup = nup_name_is_nup(out.name.c_str()) || (!out.nameless && nup_name_is_nup(file.c_str()));
    if (nup_name_is_nup(out.name.c_str())) {
        out.name.resize(out.name.size() - 4);
    }
    return n;
}

bool NamelessPart(const std::string& name) {
    NupPartName as;
    return nup_part_name(name.c_str(), &as) != 0 && as.kind == NUP_PART_BARE;
}

bool Read(const std::string& path, Info& out, std::string& error) {
    return Read(std::vector<Part>{{path, 0}}, out, error);
}

bool Read(const std::vector<Part>& parts, Info& out, std::string& error) {
    out = Info{};
    Bytes f{parts};
    if (!f.Open()) {
        error = "the file can't be opened";
        return false;
    }

    u8 head[0x80];
    if (!f.ReadAt(0, head, sizeof head) || std::memcmp(head, "NUP\x01", 4) != 0) {
        error = "not a NUP container";
        return false;
    }
    out.container = true;
    const u64 toc_at = Rd64(head + 8), toc_size = Rd64(head + 16);
    const u32 count = Rd32(head + 0x48);
    if (count == 0 || count > 64 || toc_size < 0x30 + count * 0x40ull) {
        error = "its table of contents isn't one this reads";
        return false;
    }
    std::vector<u8> toc(0x30 + count * 0x40);
    if (!f.ReadAt(toc_at, toc.data(), toc.size()) || std::memcmp(toc.data(), "TOC\x01", 4) != 0) {
        error = "its table of contents isn't one this reads";
        return false;
    }
    std::vector<Package> packages(count);
    int first_fact = -1;
    for (u32 k = 0; k < count; ++k) {
        const u8* p = toc.data() + 0x30 + k * 0x40;
        Package& pk = packages[k];
        pk.title = Rd64(p);
        pk.version = Rd32(p + 0x14);
        pk.kind = p[0x18];
        pk.meta_at = Rd64(p + 0x20);
        pk.meta_size = Rd32(p + 0x28);
        if (pk.meta_size == 0 || pk.meta_size > (8u << 20)) {
            continue; // (no facts, or a block too large to be one)
        }
        std::vector<u8> block(pk.meta_size);
        if (!f.ReadAt(pk.meta_at, block.data(), block.size())) {
            continue;
        }
        for (std::size_t at = 0; at + 8 <= block.size();) {
            const int kind = Rd16(block.data() + at);
            const u32 size = Rd32(block.data() + at + 4);
            if (at + 8 + size > block.size()) {
                break;
            }
            pk.meta[kind].assign(block.begin() + at + 8, block.begin() + at + 8 + size);
            if (first_fact < 0 && size >= 4 && std::memcmp(block.data() + at + 8, "META", 4) == 0) {
                first_fact = kind;
            }
            at += 8 + size;
        }
    }
    if (first_fact < 0) {
        first_fact = 0;
    }
    const auto fact = [&](const Package& pk, Fact which) -> const std::vector<u8>* {
        const auto it = pk.meta.find(first_fact + which);
        return it == pk.meta.end() || it->second.empty() ? nullptr : &it->second;
    };

    const Package* game = nullptr;
    const Package* update = nullptr;
    out.packages = static_cast<int>(count);
    for (const Package& pk : packages) {
        if (pk.kind == 0 && !game) {
            game = &pk;
        } else if (pk.kind == 1) {
            ++out.updates;
            if (!update || pk.version > update->version) {
                update = &pk;
            }
        } else if (pk.kind == 2) {
            ++out.addons;
        }
    }
    if (!game && !update) {
        error = "it has no game in it (only added content)";
        return false;
    }
    const Package* newest = update ? update : game;
    const Package* base = game ? game : update;
    // (an update's title id is the game's with 0x800 added: the game's is what a game is known by)
    out.title_id = game ? game->title : update->title & ~0xFFFull;
    out.version = newest->version;
    for (const Package* pk : {newest, base}) {
        if (out.display_version.empty()) {
            if (const auto* v = fact(*pk, kDisplayVersion)) {
                out.display_version = Text(*v);
            }
        }
        if (out.title.empty()) {
            if (const auto* v = fact(*pk, kTitle)) {
                out.title = Text(*v);
            }
        }
        if (out.publisher.empty()) {
            if (const auto* v = fact(*pk, kPublisher)) {
                out.publisher = Text(*v);
            }
        }
        if (out.icon.empty()) {
            if (const auto* v = fact(*pk, kIcon); v && v->size() > 4 && (*v)[0] == 0xFF && (*v)[1] == 0xD8) {
                out.icon = *v;
            }
        }
        if (const auto* nacp = fact(*pk, kNacp)) {
            FromNacp(*nacp, out.title, out.publisher, out.display_version);
        }
    }
    return true;
}

} // namespace Nup
