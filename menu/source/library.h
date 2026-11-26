// SPDX-License-Identifier: GPL-2.0-or-later

// The card as the menu sees it: where things are (docs/layout.md), the two kinds of settings
// file, and the list of games: one for each NUP container in the games folder and in the
// folders inside it, whether the container is one file or was cut in parts (nup.h).

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ui_views.h"

namespace Flow {

// What is where, as paths of the card ("/nx2-flow/..."). The card itself is CardRoot():
// "sdmc:" on the console, a folder on a PC (the previews).
inline constexpr const char* kName = "nx2-flow";
inline constexpr const char* kHome = "/nx2-flow";
inline constexpr const char* kDefaultGames = "/nx2-flow/games";
inline constexpr const char* kCovers = "/nx2-flow/covers";
inline constexpr const char* kCache = "/nx2-flow/cache";
inline constexpr const char* kMenuCfg = "/nx2-flow/menu.cfg";
inline constexpr const char* kMenuNro = "/switch/nx2-flow/nx2-flow.nro";
inline constexpr const char* kGameNro = "/switch/nx2-flow/nx2-game.nro";

const std::string& CardRoot();
void SetCardRoot(std::string root);
/// A path of the card as the C library takes it.
std::string OnCard(const std::string& card_path);

bool Exists(const std::string& card_path);
bool IsFolder(const std::string& card_path);
void MakeFolders(const std::string& card_path); // With every folder on the way.
/// The folders in a folder, by name; `files` too when it is given.
std::vector<std::string> ListFolders(const std::string& card_path,
                                     std::vector<std::string>* files = nullptr);

/// A settings file: lines of key=value. Reading a file that isn't there gives nothing.
using Settings = std::map<std::string, std::string>;
Settings ReadSettings(const std::string& card_path);
bool WriteSettings(const std::string& card_path, const Settings& values);

struct Game {
    /// One of the files the container is in.
    struct Part {
        std::string path; // As a path of the card.
        std::uint64_t size{};
    };

    // The container, as a path of the card: what the game loader is given to run it, and what
    // the game is known by here. A whole file; of a container cut in parts, the first part, or
    // the folder when the parts have no name of their own ("00", "01": the folder is the game).
    std::string path;
    std::string file;      // Its name alone.
    std::vector<Part> parts; // In their order; one for a whole file.
    std::string own_folder; // The folder that was listed as this game (it holds its parts and no other game); else empty.
    std::uint64_t size{};  // Of all the parts.
    std::int64_t mtime{};  // Of the part written last.
    std::uint64_t title_id{};
    std::string id;        // The title id, 16 hex digits, lower case: the game's folder's name.
    std::string title;
    std::string publisher;
    std::string version;
    std::string note;      // "1 update, 2 added contents" and the like.
    std::string error;     // Why it can't be started; empty when it can.
    std::string slot;      // The slot of saved data in use (game.cfg's "save"); empty: "default".
    // The container's own icon: decoded when first shown.
    std::vector<SwitchFrontend::Ui::u32> icon;
    int icon_size{};
    bool icon_tried{};

    std::string Folder() const {
        return std::string{kHome} + "/" + id;
    }
};

/// The games of a folder and of the folders in it (a few levels down), by title. What was read
/// of each container is kept in a small file under kCache, by the container's name, size and
/// time: a library opens without reading the containers again.
std::vector<Game> ScanGames(const std::string& games_folder);
/// How many games are in a folder itself, not looking into the folders in it that aren't one.
int CountGames(const std::string& card_path);

/// A game's container deleted from the card: every part of it, and the folder that was the
/// game if nothing else is left in it. Its saved data and settings stay. False when a file
/// couldn't be deleted.
bool RemoveGame(const Game& game);

/// Decodes a game's icon if it hasn't been yet (from kCache/<id>.jpg). At most `budget` a call.
void LoadIcon(Game& game, int& budget);

/// The sets of mods a game has: the folders of <game folder>/modsets, by name.
std::vector<std::string> ModSets(const Game& game);

/// The slots of saved data a game has besides "default": the folders of <game folder>/saves.
std::vector<std::string> SaveSlots(const Game& game);
/// Where a slot's data is, as a path of the card. "" and "default" are <game folder>/save.
std::string SlotFolder(const Game& game, const std::string& slot);
/// A name as a slot may have it: letters, digits, spaces, "-", "_" and ".", at most 32 of
/// them, and not "default". Empty when nothing usable is left of what was typed.
std::string CleanSlotName(const std::string& typed);

/// A folder and everything in it, copied file-by-file via a `.tmp` staging file that is
/// flushed, closed and renamed into place so an interrupted or failed write never leaves a
/// half-written destination file. False, with why, when something couldn't be read or written.
bool CopyFolder(const std::string& from_card_path, const std::string& to_card_path, std::string& error);
/// A folder and everything in it, deleted.
bool RemoveFolder(const std::string& card_path);

std::string HumanSize(std::uint64_t bytes);

} // namespace Flow
