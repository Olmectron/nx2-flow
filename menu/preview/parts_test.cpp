// SPDX-License-Identifier: GPL-2.0-or-later

// Games whose NUP files are kept in parts (a card that takes no file of 4 GB or more), checked
// on a PC: the library's list of a pretend card that has the same container whole and cut in
// every way, and what is read of each.
//
//   parts_test <card folder>          (the card: tools/make_parts_card.py; tools/test_parts.sh does both)
//
// The card comes with what to expect of it (expect.txt, icons/): the games the library is to
// list and no others, each with its path (what the game loader is given), its parts, its title
// id, its title, its size, and the icon its container has. Then games are deleted, and what is
// left on the card is looked at. It says what it checked and ends with 0 when all of it held.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "library.h"
#include "nup.h"

namespace {

int g_failed = 0, g_checked = 0;

void Check(bool held, const std::string& what) {
    ++g_checked;
    if (!held) {
        ++g_failed;
        std::printf("FAILED: %s\n", what.c_str());
    }
}

std::vector<std::uint8_t> FileBytes(const std::string& path) {
    std::vector<std::uint8_t> bytes;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        std::uint8_t block[4096];
        for (std::size_t got; (got = std::fread(block, 1, sizeof block, f)) != 0;) {
            bytes.insert(bytes.end(), block, block + got);
        }
        std::fclose(f);
    }
    return bytes;
}

struct Expected {
    std::string path, id, title;
    std::size_t parts{};
    std::uint64_t size{};
};

std::vector<std::string> Fields(const std::string& line) {
    std::vector<std::string> out{""};
    for (const char c : line) {
        if (c == '\t') {
            out.emplace_back();
        } else if (c != '\n' && c != '\r') {
            out.back() += c;
        }
    }
    return out;
}

/// The library's list against what is expected: the same games, and of each the same facts.
void Compare(const std::vector<Flow::Game>& games, const std::vector<Expected>& expected, const char* when) {
    const std::string at = std::string{" ("} + when + ")";
    Check(games.size() == expected.size(), std::to_string(games.size()) + " games listed, " + std::to_string(expected.size()) + " expected" + at);
    std::map<std::string, const Flow::Game*> by_path;
    for (const Flow::Game& game : games) {
        Check(by_path.emplace(game.path, &game).second, game.path + " listed once" + at);
    }
    for (const Expected& e : expected) {
        const auto it = by_path.find(e.path);
        Check(it != by_path.end(), e.path + " is listed" + at);
        if (it == by_path.end()) {
            continue;
        }
        const Flow::Game& game = *it->second;
        by_path.erase(it);
        Check(game.parts.size() == e.parts, e.path + " in " + std::to_string(e.parts) + " parts: " + std::to_string(game.parts.size()) + at);
        Check(game.id == e.id, e.path + " title id " + e.id + ": " + game.id + at);
        Check(game.title_id == std::strtoull(e.id.c_str(), nullptr, 16), e.path + " title id as a number" + at);
        Check(game.title == e.title, e.path + " title \"" + e.title + "\": \"" + game.title + "\"" + at);
        Check(game.size == e.size, e.path + " size " + std::to_string(e.size) + ": " + std::to_string(game.size) + at);
        Check(game.error.empty() == !e.id.empty(), e.path + " can be started, or not: \"" + game.error + "\"" + at);
        std::uint64_t sum = 0;
        for (const Flow::Game::Part& part : game.parts) {
            sum += part.size;
            Check(Flow::Exists(part.path) && !Flow::IsFolder(part.path), part.path + " is a file of the card" + at);
        }
        Check(sum == e.size, e.path + " parts add up to its size" + at);
    }
    for (const auto& [path, game] : by_path) {
        Check(false, path + " is listed and isn't expected" + at);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("parts_test <card folder>\n");
        return 1;
    }
    const std::string card = argv[1];
    Flow::SetCardRoot(card);

    std::vector<Expected> expected;
    int here = -1;
    if (std::FILE* f = std::fopen((card + "/expect.txt").c_str(), "rb")) {
        char line[2048];
        while (std::fgets(line, sizeof line, f)) {
            const std::vector<std::string> fields = Fields(line);
            if (fields.size() == 2 && fields[0] == "here") {
                here = std::atoi(fields[1].c_str());
            } else if (fields.size() == 5) {
                expected.push_back({fields[0], fields[2], fields[3], static_cast<std::size_t>(std::atoi(fields[1].c_str())),
                                    std::strtoull(fields[4].c_str(), nullptr, 10)});
            }
        }
        std::fclose(f);
    }
    if (expected.empty() || here < 0) {
        std::printf("no expect.txt in %s: make the card with tools/make_parts_card.py\n", card.c_str());
        return 1;
    }

    // The list, read from the containers; then again, from what the first time kept.
    std::vector<Flow::Game> games = Flow::ScanGames(Flow::kDefaultGames);
    Compare(games, expected, "first look");
    Compare(Flow::ScanGames(Flow::kDefaultGames), expected, "from the cache");
    // (that it is what was kept that the second list comes from: a title changed there shows)
    const std::string cache = Flow::OnCard(std::string{Flow::kCache} + "/library.txt");
    std::vector<std::uint8_t> kept = FileBytes(cache);
    const std::string was = "Deep Down", now = "Kept Then";
    const auto found = std::search(kept.begin(), kept.end(), was.begin(), was.end());
    Check(found != kept.end(), "what was read of the games is kept");
    if (found != kept.end()) {
        std::copy(now.begin(), now.end(), found);
        if (std::FILE* f = std::fopen(cache.c_str(), "wb")) {
            std::fwrite(kept.data(), 1, kept.size(), f);
            std::fclose(f);
        }
        bool from_kept = false;
        for (const Flow::Game& game : Flow::ScanGames(Flow::kDefaultGames)) {
            from_kept = from_kept || game.title == now;
        }
        Check(from_kept, "a game that hasn't changed is listed from what was kept, not read again");
        std::remove(cache.c_str());
    }
    Check(Flow::CountGames(Flow::kDefaultGames) == here, "the folder picker counts " + std::to_string(here) + " games in the games folder: " +
                                                             std::to_string(Flow::CountGames(Flow::kDefaultGames)));

    // What is read of each: its icon, byte for byte the one its container has, through
    // whichever parts it is in; and the same kept for the shelf.
    for (const Flow::Game& game : games) {
        if (game.id.empty()) {
            continue;
        }
        const std::vector<std::uint8_t> icon = FileBytes(card + "/icons/" + game.id + ".jpg");
        std::vector<Nup::Part> parts;
        for (const Flow::Game::Part& part : game.parts) {
            parts.push_back({Flow::OnCard(part.path), part.size});
        }
        Nup::Info info;
        std::string error;
        Check(Nup::Read(parts, info, error), game.path + " is read: " + error);
        Check(!icon.empty() && info.icon == icon, game.path + " icon is its container's (" + std::to_string(info.icon.size()) + " bytes)");
        Check(info.title_id == game.title_id && info.updates == 1, game.path + " packages");
        Check(FileBytes(Flow::OnCard(std::string{Flow::kCache} + "/" + game.id + ".jpg")) == icon, game.path + " icon kept for the shelf");
    }

    // Deleting a game takes all its parts, and its folder when that was the game and nothing
    // else is in it.
    const auto game_at = [&](const std::string& name) -> const Flow::Game* {
        for (const Flow::Game& game : games) {
            if (game.path == std::string{Flow::kDefaultGames} + "/" + name) {
                return &game;
            }
        }
        Check(false, name + " is there to delete");
        return nullptr;
    };
    const auto remove = [&](const std::string& name) {
        if (const Flow::Game* game = game_at(name)) {
            Check(Flow::RemoveGame(*game), name + " deleted");
            for (const Flow::Game::Part& part : game->parts) {
                Check(!Flow::Exists(part.path), part.path + " is gone");
            }
            for (std::size_t k = 0; k < expected.size(); ++k) {
                if (expected[k].path == game->path) {
                    expected.erase(expected.begin() + static_cast<std::ptrdiff_t>(k));
                    break;
                }
            }
        }
    };
    const std::string in_games = std::string{Flow::kDefaultGames} + "/";
    remove("X.nup.001");
    Check(Flow::Exists(in_games + "Q.nup.002"), "another game's lone part stays");
    remove("Z");
    Check(!Flow::Exists(in_games + "Z"), "a folder of nameless parts goes with them");
    remove("W.nup");
    Check(Flow::Exists(in_games + "W.nup/note.txt"), "a folder that holds something else stays, with it");
    remove("Solo/S.nup.001");
    Check(!Flow::Exists(in_games + "Solo"), "a folder that held one game's named parts goes with them");
    remove("Two/First.nup");
    Check(Flow::Exists(in_games + "Two/Second.nup"), "a folder of games stays when one is deleted");
    remove("Whole.nup");
    games = Flow::ScanGames(Flow::kDefaultGames);
    Compare(games, expected, "after deleting");

    std::printf("%d checks, %d failed\n", g_checked, g_failed);
    return g_failed == 0 ? 0 : 1;
}
