// SPDX-License-Identifier: GPL-2.0-or-later

#include "nup.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

#include <dirent.h>
#include <sys/stat.h>

namespace Nup {
namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

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

bool EndsWith(const std::string& s, const char* tail) {
    const std::size_t n = std::strlen(tail);
    if (s.size() < n) {
        return false;
    }
    std::string end = s.substr(s.size() - n);
    for (char& ch : end) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return end == tail;
}

std::string Lower(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

/// A file's place set to a byte of it: where the C library is to read from next.
bool Seek(std::FILE* f, u64 at) {
#if defined(_WIN32) && !defined(__CYGWIN__)
    return _fseeki64(f, static_cast<long long>(at), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(at), SEEK_SET) == 0;
#endif
}

/// The names of the files in a folder: none when it can't be read.
std::vector<std::string> Listing(const std::string& folder) {
    std::vector<std::string> names;
    DIR* dir = opendir(folder.c_str());
    if (!dir) {
        return names;
    }
    while (const dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            names.push_back(name);
        }
    }
    closedir(dir);
    return names;
}

std::string Join(const std::string& folder, const std::string& name) {
    return folder + (folder.empty() || folder.back() == '/' ? "" : "/") + name;
}

std::string Size(u64 bytes) {
    char text[32];
    if (bytes >= (1ull << 30)) {
        std::snprintf(text, sizeof text, "%.1f GB", static_cast<double>(bytes) / (1ull << 30));
    } else if (bytes >= (1ull << 20)) {
        std::snprintf(text, sizeof text, "%u MB", static_cast<unsigned>(bytes >> 20));
    } else {
        std::snprintf(text, sizeof text, "%u KB", static_cast<unsigned>((bytes + 1023) >> 10));
    }
    return text;
}

/// A part's number as the card spells it ("003" of "Game.nup.003").
std::string Number(const Kept& kept, std::size_t k) {
    const std::string name = kept.parts[k].substr(kept.parts[k].rfind('/') + 1);
    return name.size() > kept.file.size() ? name.substr(kept.file.size() + 1) : name;
}

/// The numbers of a container's parts, as a list for a message ("001, 002, 005").
std::string Numbers(const Kept& kept) {
    std::string text;
    for (std::size_t k = 0; k < kept.parts.size() && k < 9; ++k) {
        text += (k ? ", " : "") + Number(kept, k);
    }
    if (kept.parts.size() > 9) {
        text += ", ... (" + std::to_string(kept.parts.size()) + " in all)";
    }
    return text;
}

/// Whether the parts of a container make one whole file: said in the Kept. They are the game
/// loader's own rules (its runtime/parts.h), so that what the menu says can be started is what
/// the loader reads: numbered from 0 or from 1, each there in turn; all of one size but the last;
/// and as many bytes in all as the container says it has, by its own first bytes.
void Judge(Kept& kept, const std::vector<u64>& numbers) {
    const std::size_t count = kept.parts.size();
    kept.whole = false;
    if (count > static_cast<std::size_t>(kMostParts)) {
        kept.error = "it is in " + std::to_string(count) + " parts, and at most " + std::to_string(kMostParts) + " are read";
        return;
    }
    if (numbers.front() > 1) {
        kept.error = "its first part is " + Number(kept, 0) + ": the ones before it are missing";
        return;
    }
    for (std::size_t k = 0; k + 1 < count; ++k) {
        if (numbers[k + 1] != numbers[k] + 1) {
            kept.error = "a part is missing, or two have one number (its parts are " + Numbers(kept) + ")";
            return;
        }
    }
    for (std::size_t k = 0; k + 1 < count; ++k) {
        if (kept.held[k] != kept.held.front()) {
            kept.error = "its parts aren't of one size: " + Number(kept, 0) + " holds " + Size(kept.held[0]) + " and " + Number(kept, k) + " holds " +
                         Size(kept.held[k]);
            return;
        }
    }
    if (kept.held.back() == 0) {
        kept.error = "its last part, " + Number(kept, count - 1) + ", is empty";
        return;
    }
    // (the container's first bytes, from its parts: the first of them may hold fewer of them than there are)
    u8 head[0x80] = {};
    u64 got = 0;
    for (std::size_t k = 0; got < sizeof head && k < kept.parts.size(); ++k) {
        if (std::FILE* f = std::fopen(kept.parts[k].c_str(), "rb")) {
            got += std::fread(head + got, 1, static_cast<std::size_t>(std::min<u64>(sizeof head - got, kept.held[k])), f);
            std::fclose(f);
        }
    }
    const u64 says = got == sizeof head && std::memcmp(head, "NUP\x01", 4) == 0 ? Rd64(head + 0x40) : 0;
    {
        // (a copy that was cut short, or a part left over from another copy)
        if (says && says != kept.size) {
            kept.error = says > kept.size ? "its parts hold " + Size(kept.size) + " of its " + Size(says) + ": a copy that was cut short"
                                          : "its parts hold " + Size(kept.size) + ", and by its own first bytes it is " + Size(says) +
                                                ": a part is left over from another copy";
            return;
        }
    }
    kept.whole = true;
}

/// A container that is one file. Its `parts` is empty when the file isn't there to be read.
Kept OneFile(const std::string& folder, const std::string& name) {
    Kept kept;
    struct stat st {};
    if (stat(Join(folder, name).c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
        return kept;
    }
    kept.file = name, kept.parts.push_back(Join(folder, name)), kept.held.push_back(static_cast<u64>(st.st_size));
    kept.size = static_cast<u64>(st.st_size), kept.mtime = static_cast<std::int64_t>(st.st_mtime), kept.whole = true;
    return kept;
}

/// A container from its parts (each with its number), as the card has them: in the order of their
/// numbers, and judged. Its `parts` is empty when none of them is there to be read.
Kept InParts(const std::string& folder, std::vector<std::pair<u64, std::string>> parts) {
    Kept kept;
    std::sort(parts.begin(), parts.end()); // (by number, then by name: two of one number keep one order)
    std::vector<u64> numbers;
    for (const auto& [number, name] : parts) {
        struct stat st {};
        const std::string path = Join(folder, name);
        if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
            continue;
        }
        if (kept.file.empty()) {
            std::string of;
            Named(name, &of);
            kept.file = of; // (the container's name: the first part's with its number taken off)
        }
        kept.parts.push_back(path), kept.held.push_back(static_cast<u64>(st.st_size)), kept.size += static_cast<u64>(st.st_size);
        kept.mtime = std::max(kept.mtime, static_cast<std::int64_t>(st.st_mtime)), numbers.push_back(number);
    }
    if (!kept.parts.empty()) {
        Judge(kept, numbers);
    }
    return kept;
}

/// A container's bytes: from the file, or from its parts in order, each read within the part
/// that holds it.
class Bytes {
public:
    explicit Bytes(const Kept& kept) : kept_(kept) {
        u64 at = 0;
        for (const u64 held : kept.held) {
            at_.push_back(at), at += held;
        }
    }
    ~Bytes() {
        Shut();
    }
    Bytes(const Bytes&) = delete;
    Bytes& operator=(const Bytes&) = delete;

    /// `size` bytes from `at`, which may lie in two parts. False when they can't be had.
    bool Read(u64 at, void* to, std::size_t size) {
        u8* out = static_cast<u8*>(to);
        if (at_.empty() || at > kept_.size || size > kept_.size - at) {
            return false;
        }
        while (size) {
            std::size_t part = 0; // (the last that begins at or before `at`)
            for (std::size_t hi = at_.size(); part + 1 < hi;) {
                const std::size_t mid = part + (hi - part) / 2;
                if (at_[mid] <= at) {
                    part = mid;
                } else {
                    hi = mid;
                }
            }
            if (open_ != part) {
                Shut();
                file_ = std::fopen(kept_.parts[part].c_str(), "rb"), open_ = file_ ? part : kNone;
                if (!file_) {
                    return false;
                }
            }
            const std::size_t take = static_cast<std::size_t>(std::min<u64>(size, kept_.held[part] - (at - at_[part])));
            if (!Seek(file_, at - at_[part]) || std::fread(out, 1, take, file_) != take) {
                return false;
            }
            at += take, out += take, size -= take;
        }
        return true;
    }

private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    void Shut() {
        if (file_) {
            std::fclose(file_);
        }
        file_ = nullptr, open_ = kNone;
    }
    const Kept& kept_;
    std::vector<u64> at_; // where each part begins in the container
    std::FILE* file_{nullptr};
    std::size_t open_{kNone};
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

Name Named(const std::string& file, std::string* of, std::uint64_t* number) {
    const std::size_t dot = file.rfind('.');
    if (dot == std::string::npos) {
        return Name::Other;
    }
    const std::string digits = file.substr(dot + 1);
    if (digits.empty() || digits.size() > 18 || digits.find_first_not_of("0123456789") != std::string::npos) {
        return EndsWith(file, ".nup") ? Name::One : Name::Other;
    }
    // (a number after a name: a part of the container of that name, and not of anything else)
    if (!EndsWith(file.substr(0, dot), ".nup")) {
        return Name::Other;
    }
    u64 n = 0;
    for (const char ch : digits) {
        n = n * 10 + static_cast<u64>(ch - '0');
    }
    if (of) {
        *of = file.substr(0, dot);
    }
    if (number) {
        *number = n;
    }
    return Name::Part;
}

std::vector<Kept> Find(const std::string& folder) {
    // (the parts of one container, gathered by its name as a card tells names apart: not at all)
    std::map<std::string, std::vector<std::pair<u64, std::string>>> sets;
    std::map<std::string, std::string> ones;
    for (const std::string& name : Listing(folder)) {
        std::string of;
        u64 number = 0;
        const Name kind = Named(name, &of, &number);
        if (kind == Name::One) {
            ones[Lower(name)] = name;
        } else if (kind == Name::Part) {
            sets[Lower(of)].emplace_back(number, name);
        }
    }
    std::vector<Kept> kepts;
    for (const auto& [key, name] : ones) {
        if (Kept kept = OneFile(folder, name); !kept.parts.empty()) {
            kepts.push_back(std::move(kept));
        }
    }
    for (const auto& [key, parts] : sets) {
        if (ones.count(key)) {
            continue; // (the file itself is there: it stands alone)
        }
        if (Kept kept = InParts(folder, parts); !kept.parts.empty()) {
            kepts.push_back(std::move(kept));
        }
    }
    return kepts;
}

bool Locate(const std::string& path, Kept& out) {
    out = Kept{};
    const std::size_t slash = path.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (name.empty()) {
        return false;
    }
    struct stat st {};
    if (stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
        out = OneFile(folder, name);
        return true;
    }
    std::vector<std::pair<u64, std::string>> parts;
    for (const std::string& entry : Listing(folder)) {
        std::string of;
        u64 number = 0;
        if (Named(entry, &of, &number) == Name::Part && Lower(of) == Lower(name)) {
            parts.emplace_back(number, entry);
        }
    }
    out = InParts(folder, std::move(parts));
    return !out.parts.empty();
}

bool Read(const Kept& kept, Info& out, std::string& error) {
    out = Info{};
    Bytes bytes(kept);
    u8 head[0x80];
    if (!bytes.Read(0, head, sizeof head) || std::memcmp(head, "NUP\x01", 4) != 0) {
        error = kept.parts.size() > 1 && !kept.whole ? "not all of it is on the card (" + kept.error + ")" : "not a NUP container";
        return false;
    }
    const u64 toc_at = Rd64(head + 8), toc_size = Rd64(head + 16);
    const u32 count = Rd32(head + 0x48);
    if (count == 0 || count > 64 || toc_size < 0x30 + count * 0x40ull) {
        error = "its table of contents isn't one this reads";
        return false;
    }
    std::vector<u8> toc(0x30 + count * 0x40);
    if (!bytes.Read(toc_at, toc.data(), toc.size()) || std::memcmp(toc.data(), "TOC\x01", 4) != 0) {
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
        if (!bytes.Read(pk.meta_at, block.data(), block.size())) {
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
    out.parts = static_cast<int>(kept.parts.size());
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

bool Read(const std::string& path, Info& out, std::string& error) {
    Kept kept;
    if (!Locate(path, kept)) {
        error = "the file can't be opened";
        return false;
    }
    return Read(kept, out, error);
}

} // namespace Nup
