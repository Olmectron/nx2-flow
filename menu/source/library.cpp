// SPDX-License-Identifier: GPL-2.0-or-later

#include "library.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <set>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "images.h"
#include "nup.h"

namespace Flow {
namespace {

std::string g_root = "sdmc:";

std::string Lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) {
        ++a;
    }
    return s.substr(a);
}

std::vector<std::string> Split(const std::string& line, char by) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t next = line.find(by, at);
        out.push_back(line.substr(at, next == std::string::npos ? next : next - at));
        if (next == std::string::npos) {
            break;
        }
        at = next + 1;
    }
    return out;
}

/// A line's text with nothing in it that would end the line or a field.
std::string Flat(std::string s) {
    for (char& c : s) {
        if (c == '\t' || c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return s;
}

std::string Hex16(std::uint64_t v) {
    char text[20];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(v));
    return text;
}

} // namespace

const std::string& CardRoot() {
    return g_root;
}

void SetCardRoot(std::string root) {
    g_root = std::move(root);
}

std::string OnCard(const std::string& card_path) {
    return g_root + card_path;
}

bool Exists(const std::string& card_path) {
    struct stat st;
    return stat(OnCard(card_path).c_str(), &st) == 0;
}

bool IsFolder(const std::string& card_path) {
    struct stat st;
    return stat(OnCard(card_path.empty() ? "/" : card_path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void MakeFolders(const std::string& card_path) {
    for (std::size_t at = 1; at <= card_path.size(); ++at) {
        if (at == card_path.size() || card_path[at] == '/') {
            mkdir(OnCard(card_path.substr(0, at)).c_str(), 0777);
        }
    }
}

std::vector<std::string> ListFolders(const std::string& card_path, std::vector<std::string>* files) {
    std::vector<std::string> folders;
    const std::string base = OnCard(card_path.empty() ? "/" : card_path);
    DIR* dir = opendir(base.c_str());
    if (!dir) {
        return folders;
    }
    while (const dirent* e = readdir(dir)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        struct stat st;
        const std::string full = base + (base.back() == '/' ? "" : "/") + name;
        if (stat(full.c_str(), &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            folders.push_back(name);
        } else if (files) {
            files->push_back(name);
        }
    }
    closedir(dir);
    const auto by_name = [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); };
    std::sort(folders.begin(), folders.end(), by_name);
    if (files) {
        std::sort(files->begin(), files->end(), by_name);
    }
    return folders;
}

Settings ReadSettings(const std::string& card_path) {
    Settings out;
    std::FILE* f = std::fopen(OnCard(card_path).c_str(), "rb");
    if (!f) {
        return out;
    }
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        const std::string text = Trim(line);
        const std::size_t eq = text.find('=');
        if (text.empty() || text[0] == '#' || eq == std::string::npos) {
            continue;
        }
        out[Trim(text.substr(0, eq))] = Trim(text.substr(eq + 1));
    }
    std::fclose(f);
    return out;
}

bool WriteSettings(const std::string& card_path, const Settings& values) {
    const std::size_t slash = card_path.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        MakeFolders(card_path.substr(0, slash));
    }
    std::FILE* f = std::fopen(OnCard(card_path).c_str(), "wb");
    if (!f) {
        return false;
    }
    for (const auto& [key, value] : values) {
        if (!value.empty()) {
            std::fprintf(f, "%s=%s\n", key.c_str(), value.c_str());
        }
    }
    return std::fclose(f) == 0;
}

std::string HumanSize(std::uint64_t bytes) {
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

namespace {

// How far below the games folder a game is looked for: a folder of games in it, a folder in
// that one. Not further: the folder chosen may be a large part of the card.
constexpr int kDeepest = 3;

/// A container as it was found, with nothing of it read yet.
struct Find {
    Nup::Found found;
    std::string own_folder; // The folder that was listed as this game, as a path of the card.
};

/// The containers in a folder and, `deeper` levels down, in the folders in it that aren't one
/// themselves. What a container is, whole or in parts, is for Nup::Entry to say.
/// taken: the files of the containers found until now (a part is listed once, with its first).
void FindGames(const std::string& folder, int deeper, bool nameless_asked, std::vector<Find>& out, std::set<std::string>& taken) {
    std::vector<std::string> files;
    const std::vector<std::string> folders = ListFolders(folder, &files);
    const std::string dir = OnCard(folder);
    const auto take = [&](Find& one) {
        if (taken.count(one.found.parts[0].path) != 0) {
            return;
        }
        for (const Nup::Part& part : one.found.parts) {
            taken.insert(part.path);
        }
        out.push_back(std::move(one));
    };
    for (const std::string& file : files) {
        // Every nameless file of a folder ("00", "01") is the same question, whether this folder
        // is a game's parts: asked once, and not at all in a folder that was found not to be
        // one on the way here. Such a folder may hold thousands of them.
        const bool nameless = Nup::NamelessPart(file);
        if ((nameless && nameless_asked) || taken.count(dir + "/" + file) != 0) {
            continue;
        }
        nameless_asked = nameless_asked || nameless;
        Find one;
        if (Nup::Entry(dir, file, false, one.found) > 0) {
            take(one);
        }
    }
    for (const std::string& name : folders) {
        const std::string inside = (folder == "/" ? std::string{} : folder) + "/" + name;
        if (name[0] == '.' || inside == kCovers || inside == kCache) {
            continue; // (hidden, or the menu's own pictures: no game is there)
        }
        Find one;
        const int n = Nup::Entry(dir, name, true, one.found);
        if (n > 0) {
            one.own_folder = inside;
            take(one);
        } else if (n < 0 && deeper > 0) {
            FindGames(inside, deeper - 1, true, out, taken);
        }
    }
}

/// A path as the C library takes it, as a path of the card.
std::string CardPath(const std::string& path) {
    return path.substr(g_root.size());
}

} // namespace

int CountGames(const std::string& card_path) {
    std::vector<Find> finds;
    std::set<std::string> taken;
    FindGames(card_path.empty() ? "/" : card_path, 0, false, finds, taken);
    int count = 0;
    for (const Find& find : finds) {
        // (as ScanGames has it: files that only look like parts count when they are a container)
        Nup::Info info;
        std::string error;
        count += find.found.called_nup || (Nup::Read(find.found.parts, info, error), info.container);
    }
    return count;
}

bool RemoveGame(const Game& game) {
    bool ok = true;
    for (const Game::Part& part : game.parts) {
        ok = std::remove(OnCard(part.path).c_str()) == 0 && ok;
    }
    if (ok && !game.own_folder.empty()) {
        rmdir(OnCard(game.own_folder).c_str()); // (refused when anything else is in it: then it stays)
    }
    return ok;
}

std::vector<Game> ScanGames(const std::string& games_folder) {
    std::vector<Game> games;
    std::vector<Find> finds;
    std::set<std::string> taken;
    FindGames(games_folder, kDeepest, false, finds, taken);

    // What was read before: a line for each container (name, size, time, then what it said).
    const std::string cache_path = std::string{kCache} + "/library.txt";
    std::map<std::string, std::vector<std::string>> known;
    if (std::FILE* f = std::fopen(OnCard(cache_path).c_str(), "rb")) {
        char line[2048];
        while (std::fgets(line, sizeof line, f)) {
            // (the line's end only is taken off: its last fields are empty for a game that can
            // be started, and the tabs between them are what makes them nine)
            std::string text = line;
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                text.pop_back();
            }
            std::vector<std::string> fields = Split(text, '\t');
            if (fields.size() >= 9) {
                known[fields[0]] = std::move(fields);
            }
        }
        std::fclose(f);
    }

    bool changed = false;
    std::vector<std::vector<std::string>> lines;
    for (Find& find : finds) {
        Game game;
        game.path = CardPath(find.found.given);
        game.file = game.path.substr(game.path.rfind('/') + 1);
        game.own_folder = std::move(find.own_folder);
        game.size = find.found.size;
        bool there = false;
        for (const Nup::Part& part : find.found.parts) {
            game.parts.push_back({CardPath(part.path), part.size});
            struct stat st;
            if (stat(part.path.c_str(), &st) == 0) {
                there = true;
                game.mtime = std::max(game.mtime, static_cast<std::int64_t>(st.st_mtime));
            }
        }
        if (!there) {
            continue;
        }
        const std::string size_text = std::to_string(game.size), time_text = std::to_string(game.mtime);
        const auto it = known.find(game.path);
        if (it != known.end() && it->second[1] == size_text && it->second[2] == time_text) {
            const std::vector<std::string>& k = it->second;
            game.id = k[3];
            game.title_id = std::strtoull(k[3].c_str(), nullptr, 16);
            game.title = k[4];
            game.publisher = k[5];
            game.version = k[6];
            game.note = k[7];
            game.error = k[8];
        } else {
            Nup::Info info;
            std::string error;
            const bool read = Nup::Read(find.found.parts, info, error);
            if (!info.container && !find.found.called_nup) {
                // Files that look like the parts of something ("x.001", "x.002"; "0", "1" in a
                // folder) and aren't called a container's: an archive in volumes, a program's
                // own data. Only a container among those is a game.
                continue;
            }
            changed = true;
            if (read) {
                game.title_id = info.title_id;
                game.id = Hex16(info.title_id);
                game.title = Flat(info.title);
                game.publisher = Flat(info.publisher);
                game.version = Flat(info.display_version);
                if (info.updates != 0) {
                    game.note = std::to_string(info.updates) + (info.updates == 1 ? " update" : " updates");
                }
                if (info.addons != 0) {
                    game.note += (game.note.empty() ? "" : ", ") + std::to_string(info.addons) +
                                 (info.addons == 1 ? " added content" : " added contents");
                }
                if (!info.icon.empty()) {
                    MakeFolders(kCache);
                    if (std::FILE* out = std::fopen(OnCard(std::string{kCache} + "/" + game.id + ".jpg").c_str(), "wb")) {
                        std::fwrite(info.icon.data(), 1, info.icon.size(), out);
                        std::fclose(out);
                    }
                }
            } else {
                game.error = error;
            }
        }
        if (game.title.empty()) {
            // (the file's own name without ".nup"; of parts, what their names have in common, or their folder's name)
            game.title = find.found.name.empty() ? game.file : find.found.name;
        }
        lines.push_back({game.path, size_text, time_text, game.id, game.title, game.publisher, game.version, game.note, game.error});
        games.push_back(std::move(game));
    }
    if (changed || lines.size() != known.size()) {
        MakeFolders(kCache);
        if (std::FILE* f = std::fopen(OnCard(cache_path).c_str(), "wb")) {
            for (const auto& fields : lines) {
                for (std::size_t k = 0; k < fields.size(); ++k) {
                    std::fprintf(f, "%s%s", k ? "\t" : "", Flat(fields[k]).c_str());
                }
                std::fputc('\n', f);
            }
            std::fclose(f);
        }
    }
    std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
        const std::string x = Lower(a.title), y = Lower(b.title);
        return x != y ? x < y : a.file < b.file;
    });
    return games;
}

void LoadIcon(Game& game, int& budget) {
    if (game.icon_tried || game.id.empty() || budget <= 0) {
        return;
    }
    game.icon_tried = true;
    --budget;
    SwitchFrontend::Ui::Image image;
    if (Images::LoadFile(OnCard(std::string{kCache} + "/" + game.id + ".jpg"), image, 256) && image.w == image.h) {
        game.icon_size = image.w;
        game.icon = std::move(image.px);
    }
}

std::vector<std::string> ModSets(const Game& game) {
    return ListFolders(game.Folder() + "/modsets");
}

std::vector<std::string> SaveSlots(const Game& game) {
    return ListFolders(game.Folder() + "/saves");
}

std::string SlotFolder(const Game& game, const std::string& slot) {
    return slot.empty() || slot == "default" ? game.Folder() + "/save" : game.Folder() + "/saves/" + slot;
}

std::string CleanSlotName(const std::string& typed) {
    std::string name;
    for (const char ch : typed) {
        const bool fine = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' ||
                          ch == '_' || ch == '.';
        if (!fine || name.size() >= 32 || (ch == ' ' && !name.empty() && name.back() == ' ')) {
            continue; // (what is left out may leave two spaces together: one is kept)
        }
        name += ch;
    }
    // (a folder's name neither begins nor ends with a space or a dot on this kind of card)
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    std::size_t first = 0;
    while (first < name.size() && (name[first] == ' ' || name[first] == '.')) {
        ++first;
    }
    name = name.substr(first);
    return Lower(name) == "default" ? std::string{} : name;
}

namespace {

bool CopyFileAtomic(const std::string& src_path, const std::string& dst_path,
                    const std::string& name, std::vector<char>& block, std::string& error) {
    std::FILE* in = std::fopen(src_path.c_str(), "rb");
    if (!in) {
        error = name + " couldn't be read";
        return false;
    }
    const std::string tmp_path = dst_path + ".tmp";
    std::FILE* out = std::fopen(tmp_path.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        error = name + " couldn't be written";
        return false;
    }
    bool ok = true;
    for (;;) {
        const std::size_t got = std::fread(block.data(), 1, block.size(), in);
        if (got == 0) {
            ok = std::ferror(in) == 0;
            break;
        }
        if (std::fwrite(block.data(), 1, got, out) != got) {
            ok = false;
            break;
        }
    }
    std::fclose(in);
    if (ok && (std::fflush(out) != 0 || std::ferror(out) != 0)) {
        ok = false;
    }
    if (std::fclose(out) != 0) {
        ok = false;
    }
    if (!ok) {
        std::remove(tmp_path.c_str());
        error = name + " couldn't be copied (is the card full?)";
        return false;
    }
    if (std::rename(tmp_path.c_str(), dst_path.c_str()) != 0) {
        const int err = errno;
        bool renamed = false, removed = false;
        // Horizon OS (fsFsRenameFile) and Windows refuse rename() onto an existing file.
        if (err == EEXIST
#ifdef _WIN32
            || err == EACCES
#endif
        ) {
            if (std::remove(dst_path.c_str()) == 0) {
                removed = true;
                renamed = std::rename(tmp_path.c_str(), dst_path.c_str()) == 0;
            }
        }
        if (!renamed) {
            // (With the old file already gone, the whole new copy is all there is of it: kept, under its ".tmp" name.)
            if (!removed) {
                std::remove(tmp_path.c_str());
            }
            error = removed ? name + " was copied but couldn't be given its name: it is kept as " + name + ".tmp"
                            : name + " couldn't be copied (is the card full?)";
            return false;
        }
    }
    return true;
}

} // namespace

bool CopyFolder(const std::string& from_card_path, const std::string& to_card_path, std::string& error) {
    MakeFolders(to_card_path);
    std::vector<std::string> files;
    const std::vector<std::string> folders = ListFolders(from_card_path, &files);
    std::vector<char> block(1 << 20);
    for (const std::string& name : files) {
        if (!CopyFileAtomic(OnCard(from_card_path + "/" + name), OnCard(to_card_path + "/" + name),
                            name, block, error)) {
            return false;
        }
    }
    for (const std::string& name : folders) {
        if (!CopyFolder(from_card_path + "/" + name, to_card_path + "/" + name, error)) {
            return false;
        }
    }
    return true;
}

bool RemoveFolder(const std::string& card_path) {
    std::vector<std::string> files;
    const std::vector<std::string> folders = ListFolders(card_path, &files);
    bool ok = true;
    for (const std::string& name : files) {
        ok = std::remove(OnCard(card_path + "/" + name).c_str()) == 0 && ok;
    }
    for (const std::string& name : folders) {
        ok = RemoveFolder(card_path + "/" + name) && ok;
    }
    return rmdir(OnCard(card_path).c_str()) == 0 && ok;
}

} // namespace Flow
