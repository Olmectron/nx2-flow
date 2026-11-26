// SPDX-License-Identifier: GPL-2.0-or-later

// The menu's reading of a NUP container kept in parts, tried on a PC: the parts are one game on
// the shelf, its name is read through them, and a set that isn't whole says why it can't be
// started, by the game loader's own rules.
//
//   make partstest             (from menu/: builds this and runs it)
//   ./parts_test <a folder for a pretend card>      (made and filled here: nothing of a game is in it)

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "library.h"
#include "nup.h"

namespace {

using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

int failed;
void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failed;
        std::printf("failed: %s\n", what.c_str());
    }
}

std::string Bytes(const void* from, std::size_t size) {
    return std::string{static_cast<const char*>(from), size};
}
std::string U16(u16 v) {
    return Bytes(&v, 2);
}
std::string U32(u32 v) {
    return Bytes(&v, 4);
}
std::string U64(u64 v) {
    return Bytes(&v, 8);
}

/// A NUP container with what the menu reads of one and nothing else: its superblock, which says
/// how long the whole file is; its table of contents with the packages (no files); each package's
/// block of facts. (The same as tools/make_fake_card.py makes.)
struct Package {
    u64 title;
    u32 version;
    char kind;
    std::map<int, std::string> facts; // By their number: 4 the display version, 5 the title.
};

/// `pad` bytes follow the block of facts, as a game's files do in a real one: they are nothing, but the file is that long.
std::string Container(const std::vector<Package>& packages, std::size_t pad = 0) {
    const std::size_t n = packages.size();
    const u64 toc_size = 0x30 + 0x40 * n + 0x18 * n;
    const u64 meta_at = 0x80 + toc_size;
    std::string rows, blocks;
    for (std::size_t k = 0; k < n; ++k) {
        const Package& p = packages[k];
        std::string block;
        for (const auto& [number, data] : p.facts) {
            block += U16(static_cast<u16>(number)) + U16(0) + U32(static_cast<u32>(data.size())) + data;
        }
        rows += (U64(p.title) + U32(static_cast<u32>(k)) + U32(0xFFFFFFFFu) + U32(0xFFFFFFFFu) + U32(p.version) + std::string(1, p.kind) +
                 std::string(7, '\0')); // (its root folder; no exefs or romfs ones; then the kind, and what is nothing)
        rows += (U64(meta_at + blocks.size()) + U32(static_cast<u32>(block.size()))).append(0x20 - 0x0C, '\0');
        blocks += block;
    }
    std::string toc = (std::string("TOC\x01", 4) + U32(static_cast<u32>(n)) + U64(n) + U64(0) + U64(toc_size) + U64(0)).append(0x30 - 0x28, '\0');
    toc += rows + std::string(0x18 * n, '\0');
    const std::string size = U64(0x80 + toc_size + blocks.size() + pad);
    std::string head = (std::string("NUP\x01", 4) + std::string(4, '\0') + U64(0x80) + U64(toc_size) + std::string(32, '\0') + size + size +
                        U32(static_cast<u32>(n))).append(0x80 - 0x4C, '\0');
    return head + toc + blocks + std::string(pad, 'f');
}

/// A package as a real one is: its NPDM among its facts (a container numbers its facts from 0 or from 1, and the menu tells
/// which by where an NPDM is: a package without one isn't read aright).
Package Pack(u64 title, u32 version, char kind, std::map<int, std::string> facts) {
    facts.emplace(1, std::string("META", 4) + std::string(0x7C, char(0)));
    return Package{title, version, kind, facts};
}

void Put(const std::string& path, const std::string& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    Check(f != nullptr, "the test's file can be written: " + path);
    if (f) {
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }
}

/// A container in parts, "<path>.001", ".002", ...: each `size` bytes but the last. Numbered from `first`.
void Cut(const std::string& path, const std::string& data, std::size_t size, int first = 1) {
    for (std::size_t k = 0; k * size < data.size(); ++k) {
        char digits[16];
        std::snprintf(digits, sizeof digits, "%03d", static_cast<int>(k) + first);
        Put(path + "." + digits, data.substr(k * size, size));
    }
}

const Flow::Game* GameNamed(const std::vector<Flow::Game>& games, const std::string& file) {
    for (const Flow::Game& game : games) {
        if (game.file == file) {
            return &game;
        }
    }
    return nullptr;
}

/// Empty when `text` has `part` in it: what to say of it otherwise.
std::string Has(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos ? "" : "it says \"" + text + "\" instead";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("parts_test <a folder for a pretend card>\n");
        return 1;
    }
    const std::string card = argv[1];
    mkdir(card.c_str(), 0777); // (the pretend card's own folder, which the rest is made in)
    Flow::SetCardRoot(card);
    const std::string folder = "/nx2-flow/games";
    Flow::MakeFolders(folder);
    std::remove(Flow::OnCard("/nx2-flow/cache/library.txt").c_str());
    const std::string there = Flow::OnCard(folder) + "/";
    for (const std::string& old : Flow::ListFolders(folder, nullptr)) {
        std::remove((there + old).c_str());
    }

    // What a file's name says it is.
    std::string of;
    u64 number = 0;
    Check(Nup::Named("Game.nup") == Nup::Name::One, "Game.nup is a container in one file");
    Check(Nup::Named("Game.NUP") == Nup::Name::One, "letters are not told apart");
    Check(Nup::Named("Game.nup.003", &of, &number) == Nup::Name::Part && of == "Game.nup" && number == 3, "Game.nup.003 is part 3 of Game.nup");
    Check(Nup::Named("Game.nup.00", &of, &number) == Nup::Name::Part && number == 0, "parts may be numbered from 0");
    Check(Nup::Named("Game.nup.7", &of, &number) == Nup::Name::Part && number == 7, "and of any width");
    Check(Nup::Named("Game [v0].nup.012", &of, &number) == Nup::Name::Part && of == "Game [v0].nup" && number == 12, "a number in the game's own name");
    Check(Nup::Named("Game.nup.003.ttpart") == Nup::Name::Other, "a part still being received is none yet");
    Check(Nup::Named("Game.zip.001") == Nup::Name::Other, "the parts of anything else are not a game's");
    Check(Nup::Named("Game.nup.003.nup") == Nup::Name::One, "a name that ends in .nup is a container's, whatever is before");
    Check(Nup::Named("Game.nup.001.002") == Nup::Name::Other, "a number after a part's number is not a part");
    of = "set before";
    Check(Nup::Named("Game.nup.", &of) == Nup::Name::Other && of == "set before", "a dot and no number");
    Check(Nup::Named("Game.nup.9999999999999999999") == Nup::Name::Other, "more digits than a number holds");
    Check(Nup::Named("nup") == Nup::Name::Other && Nup::Named("Game.bin") == Nup::Name::Other, "no container at all");

    // The shelf: each is one game, whatever files it is in. (A container made here is 264 bytes of superblock and table of
    // contents, then its block of facts.)
    // (each title is other than its file's name, so that a title read is known from one fallen back to)
    const std::string whole = Container({Pack(0x0400A00000000001ull, 0x10000, 0, {{4, "1.0.0"}, {5, "One Whole"}})});
    const std::string oddly = Container({Pack(0x0400A00000000002ull, 0, 0, {{5, "Cut Into Eight"}})});
    const std::string shortly = Container({Pack(0x0400A00000000003ull, 0, 0, {{5, "Short By One Part"}, {6, std::string(500, 'p')}})}, 300);
    const std::string gap = Container({Pack(0x0400A00000000004ull, 0, 0, {{5, "With A Gap"}})});
    const std::string uneven = Container({Pack(0x0400A00000000005ull, 0, 0, {{5, "Uneven"}})});
    const std::string both = Container({Pack(0x0400A00000000006ull, 0, 0, {{5, "The File Itself"}})});
    const std::string beside = Container({Pack(0x0400A00000000006ull, 0, 0, {{5, "The Part Beside"}})});
    const std::string zero = Container({Pack(0x0400A00000000007ull, 0, 0, {{5, "Numbered From Zero"}})});
    const std::string coming = Container({Pack(0x0400A00000000008ull, 0, 0, {{5, "Still Coming"}})});
    const std::string small = Container({Pack(0x0400A00000000009ull, 0x20000, 1, {{4, "2.0.0"}})});
    const std::string many = Container({Pack(0x0400A0000000000Aull, 0, 0, {{5, "Too Many"}})});
    Put(there + "Whole One.nup", whole);
    Cut(there + "Cut Finely.nup", oddly, 40); // (40 bytes each: every part begins and ends inside something)
    Cut(there + "Cut Short.nup", shortly, 200);
    std::remove((there + "Cut Short.nup.007").c_str()); // (its last part never came)
    Cut(there + "With A Gap.nup", gap, 140);
    std::remove((there + "With A Gap.nup.002").c_str());
    Put(there + "Uneven.nup.001", uneven.substr(0, 100));
    Put(there + "Uneven.nup.002", uneven.substr(100, 50)); // (a part but the last, shorter than the first)
    Put(there + "Uneven.nup.003", uneven.substr(150));
    Put(there + "Both.nup", both);
    Put(there + "Both.nup.001", beside); // (the file itself is there: it stands alone)
    Cut(there + "From Zero.nup", zero, 213, 0);
    Cut(there + "Still Coming.nup", coming, 100);
    std::rename((there + "Still Coming.nup.005").c_str(), (there + "Still Coming.nup.005.ttpart").c_str()); // (still on its way from a computer)
    Cut(there + "whole one.nup", whole, 90); // (parts beside a whole file: not looked at)
    Cut(there + "Small Update [0400A00000000009].nup", small, 100);
    Put(there + "Archive.zip.001", "a");
    Put(there + "not a container.nup", "this is not a NUP container");
    Cut(there + "Too Many.nup", many, 4); // (seventy parts of 4 bytes: more than the game loader reads)

    for (int pass = 0; pass < 2; ++pass) { // (the second pass: what was read is kept in the cache)
        const std::vector<Flow::Game> games = Flow::ScanGames(folder);
        Check(games.size() == 11, "eleven games on the shelf, of eleven containers");
        if (const Flow::Game* g = GameNamed(games, "Whole One.nup")) {
            Check(g->parts == 1 && g->error.empty() && g->size == whole.size(), "a whole file: one file, no fault");
            Check(g->title_id == 0x0400A00000000001ull && g->version == "1.0.0" && g->title == "One Whole", "and what it says of itself is read");
        } else {
            Check(false, "Whole One.nup is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "Cut Finely.nup")) {
            Check(g->parts == 11 && g->error.empty() && g->size == oddly.size(), "a file in eleven parts of 40 bytes: one game, no fault (" + std::to_string(g->parts) + ")");
            Check(g->title_id == 0x0400A00000000002ull && g->title == "Cut Into Eight" && g->path == "/nx2-flow/games/Cut Finely.nup",
                  "its name is read through the cuts, and it is known by the file's name, as one file");
        } else {
            Check(false, "Cut Finely.nup is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "Cut Short.nup")) {
            Check(g->parts == 6 && Has(g->error, "cut short").empty(), "a copy cut short says so (" + std::to_string(g->parts) + " parts; " + Has(g->error, "cut short") + ")");
            Check(g->title == "Short By One Part", "and still says what game it is (" + g->title + ")");
        } else {
            Check(false, "Cut Short.nup is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "With A Gap.nup")) {
            Check(g->parts == 2 && Has(g->error, "missing").empty(), "a part missing says so " + Has(g->error, "missing"));
        } else {
            Check(false, "With A Gap.nup is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "Uneven.nup")) {
            Check(g->parts == 3 && Has(g->error, "of one size").empty(), "parts not of one size say so " + Has(g->error, "of one size"));
        } else {
            Check(false, "Uneven.nup is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "Both.nup")) {
            Check(g->parts == 1 && g->error.empty() && g->title == "The File Itself", "where the file itself is, it stands alone");
        } else {
            Check(false, "Both.nup is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "From Zero.nup")) {
            Check(g->parts == 2 && g->error.empty() && g->title == "Numbered From Zero", "parts numbered from 0 are read (" + g->title + ")");
        } else {
            Check(false, "From Zero.nup is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "Still Coming.nup")) {
            Check(g->parts == 4 && Has(g->error, "cut short").empty(), "a part still being received is not counted as there (" + std::to_string(g->parts) +
                                                                    " parts; " + g->error + ")");
        } else {
            Check(false, "Still Coming.nup is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "Small Update [0400A00000000009].nup")) {
            Check(g->parts == 5 && g->error.empty() && g->version == "2.0.0" && g->note == "1 update", "an update's facts are read through parts too");
        } else {
            Check(false, "the ninth is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "not a container.nup")) {
            Check(g->parts == 1 && Has(g->error, "not a NUP container").empty(), "what isn't a container says so, as before");
        } else {
            Check(false, "not a container.nup is on the shelf");
        }
        if (const Flow::Game* g = GameNamed(games, "Too Many.nup")) {
            Check(Has(g->error, "at most 64").empty(), "more parts than the game loader reads say so " + Has(g->error, "at most 64"));
        } else {
            Check(false, "Too Many.nup is on the shelf");
        }
        Check(GameNamed(games, "Archive.zip") == nullptr && GameNamed(games, "Cut Short.nup.004") == nullptr, "nothing else is a game");
    }

    // The container a path names, as the card has it: what is asked of a file when one is sent, hashed or deleted.
    Nup::Kept kept;
    Check(Nup::Locate(Flow::OnCard("/nx2-flow/games/Cut Finely.nup"), kept) && kept.whole && kept.parts.size() == 11 && kept.file == "Cut Finely.nup" &&
              kept.size == oddly.size(),
          "located by the file's name: its eleven parts, in order");
    Check(Nup::Locate(Flow::OnCard("/nx2-flow/games/cut finely.nup"), kept) && kept.parts.size() == 11, "letters are not told apart in a path");
    Check(Nup::Locate(Flow::OnCard("/nx2-flow/games/Whole One.nup"), kept) && kept.parts.size() == 1 && kept.parts[0] == Flow::OnCard("/nx2-flow/games/Whole One.nup"),
          "a whole file is located as itself");
    Check(!Nup::Locate(Flow::OnCard("/nx2-flow/games/No Such.nup"), kept) && kept.parts.empty(), "what isn't there is not located");
    Check(Nup::Locate(Flow::OnCard("/nx2-flow/games/With A Gap.nup"), kept) && kept.parts.size() == 2 && !kept.whole,
          "a set with a gap is located, as the two parts there are, and said not to be whole");

    // What it says of itself, read straight from the parts.
    Nup::Info info;
    std::string error;
    Check(Nup::Read(Flow::OnCard("/nx2-flow/games/Cut Finely.nup"), info, error) && info.parts == 11 && info.title == "Cut Into Eight", "read by a path");
    Check(!Nup::Read(Flow::OnCard("/nx2-flow/games/No Such.nup"), info, error) && error == "the file can't be opened", "or said not to be there");

    std::printf(failed ? "parts_test: %d checks failed\n" : "parts_test: every check passed\n", failed);
    return failed ? 1 : 0;
}
