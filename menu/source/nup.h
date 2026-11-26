// SPDX-License-Identifier: GPL-2.0-or-later

// A NUP container, read for the menu: which game it is. Only its superblock, the head of its
// table of contents (the packages) and the packages' blocks of facts are read: never a file's
// bytes, so a container of tens of gigabytes costs a few kilobytes of reading.
//
// The layout (all little-endian) is the one nx2-relink's tools/nup.py reads: a superblock of
// 0x80 bytes ("NUP\1": where the table of contents is and how long, how many packages); the
// table of contents ("TOC\1": the packages first, 0x40 bytes each); a package has its title's
// number, a version, a kind (0 the game, 1 an update, 2 added content) and a block of facts:
// its NPDM, its NACP, its icon (a JPEG), its display version, its title, its publisher.
//
// A container may be on the card in parts: a card of the FAT32 kind takes no file of 4 GB or
// more. Which files are the parts of one container is told by rules the game loader has too
// (loader/runtime/nupparts.h, the one place they are written), and the parts are read here as
// the one file they were.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Nup {

struct Info {
    std::uint64_t title_id{};      // Of the game's own package (kind 0); 0 when there is none.
    std::uint32_t version{};       // Of the newest package that has the game's program.
    int packages{};
    int updates{};                 // Packages of kind 1.
    int addons{};                  // Packages of kind 2.
    std::string title;             // Empty when the container gives none.
    std::string publisher;
    std::string display_version;   // Of the newest update, or the game's.
    std::vector<std::uint8_t> icon; // A JPEG; empty when there is none.
    bool container{};              // It begins as a container does (whatever else is wrong with it).
};

/// One of the files a container is in. A whole container is one part.
struct Part {
    std::string path; // As the C library takes it.
    std::uint64_t size{};
};

/// The most parts a container is read in (the loader's own limit).
extern const int kMostParts;

/// A container found on the card.
struct Found {
    std::vector<Part> parts; // In their order: the first has the superblock.
    std::uint64_t size{};    // Of them all.
    std::string given;       // What the game loader is given to run it: the whole file, the first of
                             // parts that have names, or the folder of parts that have none.
    std::string name;        // What to call it when the container gives no title: the file's name
                             // without its endings, or the folder's.
    bool nameless{};         // The parts have no name of their own ("00", "01"): their folder is the game.
    bool called_nup{};       // ".nup" is in its name (the file's, or the folder's of nameless parts). Files
                             // that only look like parts ("x.001", "x.002") may be anything at all.
};

/// What an entry of a folder is, for a list of games (`dir` as the C library takes it):
///   0   nothing to list: not a container, or a later part of one that is listed by its first;
///   n   a container in n parts (1: a whole file, or a folder with one nameless part): `out` is it;
///  -1   a folder that is not one container's parts: its own entries are to be looked at.
int Entry(const std::string& dir, const std::string& name, bool is_folder, Found& out);

/// Is this the name of a part that has no name of its own ("00", "7")? Every such file of a
/// folder gets the same answer from Entry, so one of them is enough to ask about.
bool NamelessPart(const std::string& name);

/// Reads what the menu shows of a container. False, with `error` in words, when the file
/// isn't one or can't be read.
bool Read(const std::string& path, Info& out, std::string& error);
/// The same of a container in parts.
bool Read(const std::vector<Part>& parts, Info& out, std::string& error);

} // namespace Nup
