// SPDX-License-Identifier: GPL-2.0-or-later

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "covers.h"

namespace Flow {

using namespace SwitchFrontend::Ui;

namespace {

constexpr int kRowH = 52;
constexpr int kRowGap = 8;
constexpr const char* kMemoryChoices[] = {"", "7000", "7400", "7800", "8200", "8600", "8992"};
constexpr int kPickerRows = 8;

bool EndsWithNup(const std::string& name) {
    if (name.size() < 4) {
        return false;
    }
    std::string tail = name.substr(name.size() - 4);
    for (char& ch : tail) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return tail == ".nup";
}

void Dim(Canvas& c) {
    c.FillRect(0, 0, c.Width(), c.Height(), g_theme_light ? MakeColor(0xF2, 0xF2, 0xF2, 0xC8) : MakeColor(0x10, 0x11, 0x13, 0xC8));
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Settings and the library

void App::Start() {
    font.InitLibrary();
    LoadSettings();
    ApplyTheme(light);
    g_shelf_cases = cases;
    Covers::EnsureFolders();
    if (games_folder == kDefaultGames) {
        MakeFolders(kDefaultGames); // (so that there is somewhere to put the first game)
    }
    Rescan();
}

void App::LoadSettings() {
    const Settings cfg = ReadSettings(kMenuCfg);
    const auto get = [&](const char* key) {
        const auto it = cfg.find(key);
        return it == cfg.end() ? std::string{} : it->second;
    };
    if (const std::string v = get("games"); !v.empty()) {
        games_folder = v;
        while (games_folder.size() > 1 && games_folder.back() == '/') {
            games_folder.pop_back();
        }
    }
    const std::string name = get("style");
    for (int k = 0; k < static_cast<int>(ShelfStyle::Count); ++k) {
        if (name == ShelfStyleName(static_cast<ShelfStyle>(k))) {
            style = k;
        }
    }
    covers_shown = get("covers") != "hidden";
    cases = get("cases") != "off";
    light = get("theme") == "light";
}

void App::SaveSettings() {
    Settings cfg = ReadSettings(kMenuCfg); // (a key this version doesn't know is kept)
    cfg["games"] = games_folder;
    cfg["style"] = ShelfStyleName(static_cast<ShelfStyle>(style));
    cfg["covers"] = covers_shown ? "shown" : "hidden";
    cfg["cases"] = cases ? "on" : "off";
    cfg["theme"] = light ? "light" : "dark";
    if (!WriteSettings(kMenuCfg, cfg)) {
        Notice("The settings couldn't be written to the card", true);
    }
    settings_changed = false;
}

void App::Finish() {
    if (settings_changed) {
        SaveSettings();
    }
}

void App::Rescan() {
    const std::string keep = Selected() ? Selected()->path : std::string{};
    games = ScanGames(games_folder);
    Covers::Reset();
    made.clear();
    selected = 0;
    for (std::size_t k = 0; k < games.size(); ++k) {
        if (games[k].path == keep) {
            selected = static_cast<int>(k);
        }
    }
    position = static_cast<float>(selected);
    flipped = false;
    flip = 0.0f;
}

void App::Notice(std::string text, bool error) {
    notice = std::move(text);
    notice_error = error;
    notice_left = 5.0f;
}

Game* App::Selected() {
    return selected >= 0 && selected < static_cast<int>(games.size()) ? &games[selected] : nullptr;
}

// ---------------------------------------------------------------------------------------------
// What was pressed

Request App::Update(const Input& in, float dt) {
    time += dt;
    const float ease = std::min(1.0f, dt * 11.0f);
    position += (static_cast<float>(selected) - position) * ease;
    flip += ((flipped ? 1.0f : 0.0f) - flip) * std::min(1.0f, dt * 9.0f);
    if (notice_left > 0.0f) {
        notice_left -= dt;
    }
    if (Covers::TakeChanged()) {
        Covers::Reset();
        Notice(Covers::Status());
    }
    switch (mode) {
    case Mode::Shelf:
        return UpdateShelf(in);
    case Mode::Sheet:
        UpdateSheet(in);
        break;
    case Mode::Settings:
        UpdateSettings(in);
        break;
    case Mode::Picker:
        UpdatePicker(in);
        break;
    }
    return {};
}

Request App::UpdateShelf(const Input& in) {
    if (in.plus) {
        Finish();
        return {Request::Kind::Exit, {}};
    }
    if (in.minus) {
        mode = Mode::Settings;
        settings_cursor = 0;
        return {};
    }
    const int count = static_cast<int>(games.size());
    if (count == 0) {
        return {};
    }
    if (in.left && selected > 0) {
        --selected;
        flipped = false;
    }
    if (in.right && selected < count - 1) {
        ++selected;
        flipped = false;
    }
    if (in.l || in.r) {
        const int n = static_cast<int>(ShelfStyle::Count);
        style = (style + (in.r ? 1 : n - 1)) % n;
        settings_changed = true;
        Notice(std::string{"Shelf: "} + ShelfStyleName(static_cast<ShelfStyle>(style)));
    }
    if (in.x) {
        flipped = !flipped;
    }
    if (in.y) {
        OpenSheet();
    }
    if (in.a) {
        const Game& game = games[selected];
        if (!game.error.empty()) {
            Notice("This file can't be started: " + game.error, true);
        } else if (!Exists(kGameNro)) {
            Notice(std::string{"The game loader isn't on the card: "} + kGameNro, true);
        } else {
            Finish();
            return {Request::Kind::Launch, game.path};
        }
    }
    return {};
}

void App::OpenSheet() {
    Game* game = Selected();
    if (!game || game->id.empty()) {
        Notice("This file has no game this can read", true);
        return;
    }
    game_cfg = ReadSettings(game->Folder() + "/game.cfg");
    mod_sets = ModSets(*game);
    sheet_cursor = 0;
    mode = Mode::Sheet;
}

std::vector<App::Row> App::SheetRows() {
    std::vector<Row> rows;
    const Game* game = Selected();
    if (!game) {
        return rows;
    }
    rows.push_back({"Detailed log", Exists(game->Folder() + "/debug") ? "On" : "Off",
                    "Every call the game makes is written to its log. Slower: for a report of a game that doesn't run.", true});
    std::string set = game_cfg.count("modset") ? game_cfg["modset"] : std::string{};
    const bool there = set.empty() || std::find(mod_sets.begin(), mod_sets.end(), set) != mod_sets.end();
    rows.push_back({"Mod set", set.empty() ? "mods (default)" : there ? set : set + " (not there)",
                    "The folder of files laid over the game's own: mods/, or one of the folders of modsets/ in the game's folder.", true});
    const std::string memory = game_cfg.count("memory") ? game_cfg["memory"] : std::string{};
    rows.push_back({"Memory the game is told of", memory.empty() ? "Automatic" : memory + " MB",
                    "Automatic is right for nearly every game. A number is a try for one that doesn't start.", true});
    return rows;
}

void App::ChangeSheet(int row, int dir) {
    Game* game = Selected();
    if (!game) {
        return;
    }
    const std::string folder = game->Folder();
    if (row == 0) {
        const std::string file = folder + "/debug";
        if (Exists(file)) {
            std::remove(OnCard(file).c_str());
        } else {
            MakeFolders(folder);
            if (std::FILE* f = std::fopen(OnCard(file).c_str(), "wb")) {
                std::fclose(f);
            } else {
                Notice("The card couldn't be written to", true);
            }
        }
        return;
    }
    std::vector<std::string> choices;
    const char* key = row == 1 ? "modset" : "memory";
    if (row == 1) {
        choices.push_back("");
        choices.insert(choices.end(), mod_sets.begin(), mod_sets.end());
    } else {
        choices.assign(std::begin(kMemoryChoices), std::end(kMemoryChoices));
    }
    const std::string now = game_cfg.count(key) ? game_cfg[key] : std::string{};
    int at = 0;
    for (std::size_t k = 0; k < choices.size(); ++k) {
        if (choices[k] == now) {
            at = static_cast<int>(k);
        }
    }
    const int n = static_cast<int>(choices.size());
    game_cfg[key] = choices[(at + dir + n) % n];
    MakeFolders(folder);
    if (!WriteSettings(folder + "/game.cfg", game_cfg)) {
        Notice("The card couldn't be written to", true);
    }
}

void App::UpdateSheet(const Input& in) {
    const int count = static_cast<int>(SheetRows().size());
    if (in.b || in.y) {
        mode = Mode::Shelf;
        return;
    }
    if (in.up && sheet_cursor > 0) {
        --sheet_cursor;
    }
    if (in.down && sheet_cursor < count - 1) {
        ++sheet_cursor;
    }
    if (in.left) {
        ChangeSheet(sheet_cursor, -1);
    }
    if (in.right || in.a) {
        ChangeSheet(sheet_cursor, 1);
    }
}

std::vector<App::Row> App::SettingsRows() {
    std::vector<Row> rows;
    rows.push_back({"Games folder", games_folder, "The folder of the card the NUP files are in. A opens the card's folders to pick one.", false});
    rows.push_back({"Shelf", ShelfStyleName(static_cast<ShelfStyle>(style)), "How the games stand in the library. L and R change it from the library too.", true});
    rows.push_back({"Covers", covers_shown ? "Shown" : "Hidden", "Hidden, every game shows the icon its own file has.", true});
    rows.push_back({"Cases", cases ? "On" : "Off", "Covers stand as cases with a side to them, or as flat pictures.", true});
    rows.push_back({"Theme", light ? "Light" : "Dark", "", true});
    const std::string status = Covers::Status();
    rows.push_back({"Download covers", Covers::Downloading() ? "Working..." : "A",
                    status.empty() ? std::string{"From GameTDB, for the games that covers/ids.txt has a code for. Needs internet access."} : status, false});
    rows.push_back({"Look for games again", "A", std::to_string(games.size()) + (games.size() == 1 ? " game" : " games") + " found in the games folder.", false});
    return rows;
}

void App::ChangeSetting(int row, int dir, bool press) {
    switch (row) {
    case 0:
        if (press) {
            OpenPicker();
        }
        return;
    case 1: {
        const int n = static_cast<int>(ShelfStyle::Count);
        style = (style + dir + n) % n;
        break;
    }
    case 2:
        covers_shown = !covers_shown;
        break;
    case 3:
        cases = !cases;
        g_shelf_cases = cases;
        break;
    case 4:
        light = !light;
        ApplyTheme(light);
        break;
    case 5:
        if (press && !Covers::StartDownload(games)) {
            Notice(Covers::Status(), true);
        }
        return;
    case 6:
        if (press) {
            std::remove(OnCard(std::string{kCache} + "/library.txt").c_str());
            Rescan();
            Notice(std::to_string(games.size()) + (games.size() == 1 ? " game found" : " games found"));
        }
        return;
    default:
        return;
    }
    settings_changed = true;
}

void App::UpdateSettings(const Input& in) {
    const int count = static_cast<int>(SettingsRows().size());
    if (in.b || in.minus) {
        Finish();
        mode = Mode::Shelf;
        return;
    }
    if (in.up && settings_cursor > 0) {
        --settings_cursor;
    }
    if (in.down && settings_cursor < count - 1) {
        ++settings_cursor;
    }
    if (in.left) {
        ChangeSetting(settings_cursor, -1, false);
    }
    if (in.right) {
        ChangeSetting(settings_cursor, 1, false);
    }
    if (in.a) {
        ChangeSetting(settings_cursor, 1, true);
    }
}

void App::OpenPicker() {
    picker_path = IsFolder(games_folder) ? games_folder : std::string{};
    if (picker_path == "/") {
        picker_path.clear();
    }
    PickerLook();
    mode = Mode::Picker;
}

void App::PickerLook() {
    std::vector<std::string> files;
    picker_folders = ListFolders(picker_path, &files);
    picker_nups = static_cast<int>(std::count_if(files.begin(), files.end(), EndsWithNup));
    picker_cursor = 0;
}

void App::UpdatePicker(const Input& in) {
    const int count = static_cast<int>(picker_folders.size());
    if (in.up && picker_cursor > 0) {
        --picker_cursor;
    }
    if (in.down && picker_cursor < count - 1) {
        ++picker_cursor;
    }
    if (in.a && picker_cursor < count) {
        picker_path += "/" + picker_folders[picker_cursor];
        PickerLook();
        return;
    }
    if (in.b) {
        if (picker_path.empty()) {
            mode = Mode::Settings;
        } else {
            const std::size_t slash = picker_path.rfind('/');
            const std::string left = picker_path.substr(slash + 1);
            picker_path.resize(slash);
            PickerLook();
            for (std::size_t k = 0; k < picker_folders.size(); ++k) {
                if (picker_folders[k] == left) {
                    picker_cursor = static_cast<int>(k);
                }
            }
        }
        return;
    }
    if (in.x) {
        games_folder = picker_path.empty() ? "/" : picker_path;
        SaveSettings();
        Rescan();
        mode = Mode::Settings;
        Notice(std::to_string(games.size()) + (games.size() == 1 ? " game" : " games") + " found in " + games_folder);
    }
}

// ---------------------------------------------------------------------------------------------
// Drawing

void App::Draw(Canvas& c) {
    DrawShelfScreen(c);
    switch (mode) {
    case Mode::Shelf:
        break;
    case Mode::Sheet:
        DrawSheet(c);
        break;
    case Mode::Settings:
        DrawSettings(c);
        break;
    case Mode::Picker:
        DrawSettings(c);
        DrawPicker(c);
        break;
    }
    DrawNotice(c);
}

void App::DrawShelfScreen(Canvas& c) {
    const int W = c.Width(), H = c.Height();
    const int count = static_cast<int>(games.size());

    ShelfView shelf;
    shelf.x = 0;
    shelf.y = 58;
    shelf.w = W;
    shelf.h = H - 58 - 164;
    shelf.style = static_cast<ShelfStyle>(style);
    shelf.position = position;
    shelf.count = count;
    shelf.flip = flip;
    shelf.time = time;

    // Backdrop: a wall that brightens towards the horizon, then a floor that takes the
    // reflections and falls away into the bottom of the screen.
    const int floor = static_cast<int>(ShelfFloorY(shelf));
    FillGradientV(c, 0, 0, W, floor, kColShelfTop, kColShelfHorizon);
    FillGradientV(c, 0, floor, W, H - floor, kColFloorNear, kColFloorFar);

    // Top line: the menu on the left, where we are in the library on the right.
    font.Draw(c, 28, 40, kName, 24, kColText);
    if (count > 0) {
        const std::string counter = std::to_string(selected + 1) + " of " + std::to_string(count);
        font.Draw(c, W - 28 - font.Measure(counter, 17), 38, counter, 17, kColTextDim);
    }

    const int radius = 30;
    const int bar_y = H - 22 - radius;
    if (count == 0) {
        const char* lines[] = {"No games here yet", nullptr, nullptr};
        const std::string where = "Put NUP files in " + games_folder + " on the card,";
        const std::string or_else = "or pick another folder in Settings.";
        lines[1] = where.c_str();
        lines[2] = or_else.c_str();
        int y = H / 2 - 50;
        for (int k = 0; k < 3; ++k) {
            const int size = k == 0 ? 28 : 19;
            font.Draw(c, (W - font.Measure(lines[k], size)) / 2, y, lines[k], size, k == 0 ? kColText : kColTextDim);
            y += k == 0 ? 44 : 28;
        }
    } else {
        // What the shelf shows of each game: its cover when there is one and covers are shown,
        // the icon its file has otherwise. A few are decoded a frame, so a new library doesn't
        // stall the menu; what isn't ready yet shows as a case with the game's name.
        int budget = 2;
        std::vector<std::shared_ptr<const Covers::Art>> held; // (kept until the frame is drawn)
        DrawShelf(c, font, shelf, [&](int index) {
            ShelfItem item;
            Game& game = games[index];
            if (covers_shown) {
                if (auto art = Covers::Get(game, budget)) {
                    item.cover = &art->image;
                    item.full_wrap = art->full_wrap;
                    held.push_back(std::move(art));
                }
            }
            LoadIcon(game, budget);
            if (!game.icon.empty()) {
                item.icon = &game.icon;
                item.icon_size = game.icon_size;
            }
            if (!item.cover && (game.icon_tried || game.id.empty())) {
                item.cover = MadeCover(game); // (a case of its own, from its icon and its name)
            }
            item.title = game.title;
            item.publisher = game.publisher;
            item.badge = game.version;
            if (!game.version.empty()) {
                item.back_text.push_back("Version " + game.version);
            }
            if (!game.note.empty()) {
                item.back_text.push_back(game.note);
            }
            if (!game.id.empty()) {
                item.back_text.push_back("Title " + game.id);
            }
            item.back_text.push_back(game.file);
            item.back_text.push_back(HumanSize(game.size));
            if (!game.error.empty()) {
                item.back_text.push_back("Can't be started: " + game.error);
            }
            return item;
        });

        // Title block, centred under the shelf.
        const Game& game = games[selected];
        const int text_w = W - 2 * 160;
        const std::string title = font.Truncate(game.title, 28, text_w);
        font.Draw(c, (W - font.Measure(title, 28)) / 2, H - 126, title, 28, kColText);
        std::string under = game.publisher;
        if (!game.version.empty()) {
            under += (under.empty() ? "" : "   ") + std::string{"v"} + game.version;
        }
        if (!game.error.empty()) {
            under = "Can't be started: " + game.error;
        }
        under = font.Truncate(under, 17, text_w);
        font.Draw(c, (W - font.Measure(under, 17)) / 2, H - 100, under, 17, game.error.empty() ? kColTextDim : kColError);

        if (mode == Mode::Shelf) {
            struct Pill {
                const char* hint;
                const char* label;
            };
            const Pill pills[] = {{"A", "Play"}, {"X", "Flip"}, {"Y", "Options"}, {"L R", "Shelf"}};
            int total = 0;
            for (const Pill& p : pills) {
                total += PillWidth(font, p.hint, p.label) + 10;
            }
            int px = (W - (total - 10)) / 2;
            for (const Pill& p : pills) {
                px += DrawPill(c, font, px, bar_y - 17, p.hint, p.label, p.hint[0] == 'A') + 10;
            }
        }
    }
    // The bar's corners: the settings on the left, the way out on the right.
    DrawRoundButton(c, font, Icon::Gear, 26 + radius, bar_y, radius, "-", mode == Mode::Settings || mode == Mode::Picker);
    DrawRoundButton(c, font, Icon::Power, W - 26 - radius, bar_y, radius, "+", false);
}

const Image* App::MadeCover(Game& game) {
    if (const auto it = made.find(game.path); it != made.end()) {
        return &it->second;
    }
    constexpr int W = 320, H = 520; // (a case's own shape)
    Canvas canvas;
    canvas.Resize(W, H);
    // The case takes its colour from the icon: what the icon is, on the whole. With no icon,
    // from the name, so that two such games don't look alike.
    u32 sum[3] = {0x50, 0x58, 0x64};
    if (!game.icon.empty()) {
        u64 total[3] = {0, 0, 0};
        for (const u32 px : game.icon) {
            total[0] += px & 0xFF;
            total[1] += (px >> 8) & 0xFF;
            total[2] += (px >> 16) & 0xFF;
        }
        for (int k = 0; k < 3; ++k) {
            sum[k] = static_cast<u32>(total[k] / game.icon.size());
        }
    } else {
        u32 hash = 2166136261u;
        for (const char ch : game.title) {
            hash = (hash ^ static_cast<u8>(ch)) * 16777619u;
        }
        sum[0] = 0x40 + (hash & 0x7F);
        sum[1] = 0x40 + ((hash >> 8) & 0x7F);
        sum[2] = 0x40 + ((hash >> 16) & 0x7F);
    }
    const u32 base = MakeColor(static_cast<u8>(sum[0]), static_cast<u8>(sum[1]), static_cast<u8>(sum[2]));
    FillGradientV(canvas, 0, 0, W, H, MixColor(base, MakeColor(0, 0, 0), 0.30f), MixColor(base, MakeColor(0, 0, 0), 0.78f));
    constexpr int side = 256, top = 40;
    if (!game.icon.empty()) {
        canvas.BlitIcon(game.icon, game.icon_size, (W - side) / 2, top, side);
    } else {
        FillDisc(canvas, W / 2.0f, top + side / 2.0f, side * 0.42f, MixColor(base, MakeColor(255, 255, 255), 0.18f));
        const std::string initial = game.title.substr(0, game.title.empty() ? 0 : 1);
        font.Draw(canvas, (W - font.Measure(initial, 120)) / 2, top + side / 2 + 42, initial, 120, MakeColor(255, 255, 255));
    }
    std::vector<std::string> lines = WrapLines(font, game.title, 27, W - 40);
    if (lines.size() > 3) {
        lines.resize(3);
        lines.back() = font.Truncate(lines.back() + " ...", 27, W - 40);
    }
    int y = top + side + 54;
    for (const std::string& line : lines) {
        font.Draw(canvas, (W - font.Measure(line, 27)) / 2, y, line, 27, MakeColor(255, 255, 255));
        y += 34;
    }
    const std::string publisher = font.Truncate(game.publisher, 16, W - 40);
    font.Draw(canvas, (W - font.Measure(publisher, 16)) / 2, H - 26, publisher, 16, MakeColor(255, 255, 255, 0xB0));
    Image image;
    image.w = W;
    image.h = H;
    image.px.assign(canvas.Data(), canvas.Data() + static_cast<std::size_t>(W) * H);
    return &made.emplace(game.path, std::move(image)).first->second;
}

void App::DrawRows(Canvas& c, int x, int y, int w, const std::vector<Row>& rows, int cursor) {
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const Row& row = rows[k];
        const int ry = y + static_cast<int>(k) * (kRowH + kRowGap);
        const bool focused = static_cast<int>(k) == cursor;
        if (focused) {
            DrawFocusRing(c, x, ry, w, kRowH, 10, time, kColSurfaceHi);
        } else {
            c.FillRoundRect(x, ry, w, kRowH, 10, kColSurface);
        }
        font.Draw(c, x + 22, CenterBaseline(ry, kRowH, 19), row.label, 19, kColText);
        const int label_w = font.Measure(row.label, 19);
        const int room = w - 22 - label_w - 40 - (row.arrows ? 64 : 22);
        const std::string value = font.TruncateFront(row.value, 19, std::max(60, room));
        const int vw = font.Measure(value, 19);
        int vx = x + w - 22 - vw;
        if (row.arrows) {
            vx -= 30;
            const u32 arrow = focused ? kColText : kColTextDim;
            font.Draw(c, vx - 26, CenterBaseline(ry, kRowH, 19), "<", 19, arrow);
            font.Draw(c, x + w - 22 - font.Measure(">", 19), CenterBaseline(ry, kRowH, 19), ">", 19, arrow);
        }
        font.Draw(c, vx, CenterBaseline(ry, kRowH, 19), value, 19, kColAccent);
    }
}

void App::DrawHints(Canvas& c, const std::vector<std::pair<const char*, const char*>>& hints) {
    int total = 0;
    for (const auto& [hint, label] : hints) {
        total += PillWidth(font, hint, label) + 10;
    }
    int px = (c.Width() - (total - 10)) / 2;
    const int y = c.Height() - 22 - 30 - 17;
    for (const auto& [hint, label] : hints) {
        px += DrawPill(c, font, px, y, hint, label, false) + 10;
    }
}

void App::DrawSheet(Canvas& c) {
    const Game* game = Selected();
    if (!game) {
        return;
    }
    Dim(c);
    const int W = c.Width();
    const int w = 860, x = (W - w) / 2, y = 40, h = 590;
    c.FillRoundRect(x, y, w, h, 16, kColBg);
    c.FillRoundRect(x, y, w, 96, 16, kColRail);
    c.FillRect(x, y + 80, w, 16, kColRail);
    c.FillRect(x, y + 96, w, 1, kColLine);

    // The game: its icon, its name, and what the file says of it.
    int tx = x + 28;
    if (!game->icon.empty()) {
        c.BlitIcon(game->icon, game->icon_size, x + 24, y + 16, 64);
        tx = x + 24 + 64 + 18;
    }
    font.Draw(c, tx, y + 44, font.Truncate(game->title, 26, x + w - 28 - tx), 26, kColText);
    std::string under = game->publisher;
    for (const std::string& more : {game->version.empty() ? std::string{} : "v" + game->version, game->note}) {
        if (!more.empty()) {
            under += (under.empty() ? "" : "   ") + more;
        }
    }
    font.Draw(c, tx, y + 72, font.Truncate(under, 17, x + w - 28 - tx), 17, kColTextDim);

    const std::vector<Row> rows = SheetRows();
    const int rows_y = y + 96 + 22;
    DrawRows(c, x + 28, rows_y, w - 56, rows, sheet_cursor);
    int ty = rows_y + static_cast<int>(rows.size()) * (kRowH + kRowGap) + 14;
    if (sheet_cursor < static_cast<int>(rows.size())) {
        for (const std::string& line : WrapLines(font, rows[sheet_cursor].help, 16, w - 56 - 12)) {
            font.Draw(c, x + 34, ty, line, 16, kColTextDim);
            ty += 21;
        }
    }

    // Where the game's things are on the card.
    c.FillRect(x + 28, y + h - 172, w - 56, 1, kColLine);
    const std::string folder = game->Folder();
    const auto there = [&](const char* name, const char* yes, const char* no) {
        return folder + "/" + name + "   " + (Exists(folder + "/" + name) ? yes : no);
    };
    const std::pair<const char*, std::string> facts[] = {
        {"Folder", folder + "/"},
        {"Log", there("log.txt", "", "(none yet)")},
        {"Crash report", there("crash.txt", "(a run ended badly: the file to send with a report)", "(none)")},
        {"Saved data", there("save", "", "(none yet)")},
        {"File", game->path + "   " + HumanSize(game->size)},
    };
    int fy = y + h - 146;
    for (const auto& [label, value] : facts) {
        font.Draw(c, x + 34, fy, label, 16, kColTextDim);
        font.Draw(c, x + 34 + 130, fy, font.TruncateFront(value, 16, w - 68 - 130), 16, kColText);
        fy += 26;
    }
    DrawHints(c, {{"< >", "Change"}, {"B", "Back"}});
}

void App::DrawSettings(Canvas& c) {
    Dim(c);
    const int W = c.Width();
    const int w = 860, x = (W - w) / 2, y = 34, h = 590;
    c.FillRoundRect(x, y, w, h, 16, kColBg);
    font.Draw(c, x + 28, y + 46, "Settings", 26, kColText);
    const std::string version = std::string{kName} + " " + kVersion;
    font.Draw(c, x + w - 28 - font.Measure(version, 16), y + 44, version, 16, kColTextDim);
    c.FillRect(x + 28, y + 64, w - 56, 1, kColLine);

    const std::vector<Row> rows = SettingsRows();
    const int rows_y = y + 80;
    DrawRows(c, x + 28, rows_y, w - 56, rows, mode == Mode::Settings ? settings_cursor : -1);
    int ty = rows_y + static_cast<int>(rows.size()) * (kRowH + kRowGap) + 16;
    if (settings_cursor < static_cast<int>(rows.size())) {
        for (const std::string& line : WrapLines(font, rows[settings_cursor].help, 16, w - 56 - 12)) {
            font.Draw(c, x + 34, ty, line, 16, kColTextDim);
            ty += 21;
        }
    }
    const std::string loader = std::string{"Game loader   "} + kGameNro + (Exists(kGameNro) ? "" : "   (NOT THERE: games can't be started)");
    font.Draw(c, x + 34, y + h - 22, font.TruncateFront(loader, 16, w - 68), 16, Exists(kGameNro) ? kColTextDim : kColError);
    if (mode == Mode::Settings) {
        DrawHints(c, {{"< >", "Change"}, {"A", "Open"}, {"B", "Back"}});
    }
}

void App::DrawPicker(Canvas& c) {
    Dim(c);
    const int W = c.Width();
    const int w = 760, x = (W - w) / 2, y = 60, h = 540;
    c.FillRoundRect(x, y, w, h, 16, kColBg);
    font.Draw(c, x + 28, y + 44, "Games folder", 24, kColText);
    const std::string here = picker_path.empty() ? "/" : picker_path;
    font.Draw(c, x + 28, y + 74, font.TruncateFront(here, 18, w - 56), 18, kColAccent);
    const std::string found = std::to_string(picker_nups) + (picker_nups == 1 ? " NUP file here" : " NUP files here");
    font.Draw(c, x + w - 28 - font.Measure(found, 16), y + 44, found, 16, picker_nups ? kColText : kColTextDim);
    c.FillRect(x + 28, y + 90, w - 56, 1, kColLine);

    const int count = static_cast<int>(picker_folders.size());
    const int first = std::clamp(picker_cursor - kPickerRows / 2, 0, std::max(0, count - kPickerRows));
    const int row_h = 44;
    for (int k = 0; k < kPickerRows && first + k < count; ++k) {
        const int index = first + k;
        const int ry = y + 104 + k * (row_h + 6);
        if (index == picker_cursor) {
            DrawFocusRing(c, x + 28, ry, w - 56, row_h, 9, time, kColSurfaceHi);
        } else {
            c.FillRoundRect(x + 28, ry, w - 56, row_h, 9, kColSurface);
        }
        font.Draw(c, x + 50, CenterBaseline(ry, row_h, 18), font.Truncate(picker_folders[index], 18, w - 120), 18, kColText);
    }
    if (count == 0) {
        font.Draw(c, x + 34, y + 140, "No folders in this one.", 18, kColTextDim);
    } else if (count > kPickerRows) {
        const std::string more = std::to_string(picker_cursor + 1) + " of " + std::to_string(count);
        font.Draw(c, x + w - 28 - font.Measure(more, 15), y + h - 18, more, 15, kColTextDim);
    }
    DrawHints(c, {{"A", "Open"}, {"X", "Use this folder"}, {"B", picker_path.empty() ? "Cancel" : "Up"}});
}

void App::DrawNotice(Canvas& c) {
    if (notice_left <= 0.0f || notice.empty()) {
        return;
    }
    const int size = 18;
    const std::string text = font.Truncate(notice, size, c.Width() - 160);
    const int w = font.Measure(text, size) + 44, h = 44;
    // Under the top line on the shelf; over the bar of buttons when a panel is up (which has the top).
    const int x = (c.Width() - w) / 2, y = mode == Mode::Shelf ? 66 : c.Height() - 22 - 30 - 22;
    c.FillRoundRect(x, y, w, h, h / 2, notice_error ? kColWarnBg : kColSurfaceHi);
    font.Draw(c, x + 22, CenterBaseline(y, h, size), text, size, notice_error ? kColWarnText : kColText);
}

} // namespace Flow
