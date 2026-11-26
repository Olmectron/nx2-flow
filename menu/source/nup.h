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
// A container is one file as a rule, "Game.nup". A FAT32 card keeps no file of 4 GiB or more,
// though, and games are larger: one is copied onto the card in parts, "Game.nup.001", ".002",
// ..., each as long as the others but the last, which in order are the file (nothing in the file
// changes: the parts are only where its bytes are). The menu shows the parts as the one game they
// are, and the game loader (nx2-game.nro) reads them so too. nx2-relink numbers the parts of its
// own files from 0 (".00", ".01"): both ways are read, of any width.

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
    int parts{};                   // The files the container is in on the card (1: one file).
};

/// What a file's name says it is: no container, a container in one file ("Game.nup"), or a part
/// of one ("Game.nup.003"). For a part, `of` is given the container's name ("Game.nup") and
/// `number` its number.
enum class Name { Other, One, Part };
Name Named(const std::string& file, std::string* of = nullptr, std::uint64_t* number = nullptr);

inline constexpr int kMostParts = 64; // The most parts a container is read in: the game loader's own number.

/// A container as the card has it: one file, or the parts of one, in order.
struct Kept {
    std::string file;                  // Its name, as of one file: "Game.nup".
    std::vector<std::string> parts;    // Its files, as the C library takes their paths: one, or the parts in order.
    std::vector<std::uint64_t> held{}; // How many bytes each of them holds.
    std::uint64_t size{};              // How many they hold in all.
    std::int64_t mtime{};              // The newest of their times.
    bool whole{true};                  // False when the parts don't make one whole container.
    std::string error;                 // Why they don't, in words.
};

/// The containers among the files of a folder (a path as the C library takes it), by name. A file
/// that is a whole container is one, as its name says; the parts of one are one container, by the
/// file's name with the number taken off. Where the file itself is, with parts beside it, the file
/// stands alone: the parts beside it are not looked at.
std::vector<Kept> Find(const std::string& folder);

/// The container named by the last part of a path (".../Game.nup"), as the card has it. False
/// when there is neither a file nor a part of that name there.
bool Locate(const std::string& path, Kept& out);

/// Reads what the menu shows of a container. False, with `error` in words, when the file isn't
/// one or can't be read. A container in parts is read from them even when they aren't a whole one
/// (`Kept::whole` says so, and why): its name and icon are shown all the same.
bool Read(const Kept& kept, Info& out, std::string& error);
bool Read(const std::string& path, Info& out, std::string& error);

} // namespace Nup
