// SPDX-License-Identifier: GPL-2.0-or-later

// The menu itself: the shelf of games, a game's options, the menu's settings. It draws on a
// plain canvas and is told what was pressed, so the same code runs on the console and renders
// the previews on a PC.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "library.h"

namespace Flow {

inline constexpr const char* kVersion = "0.1.0";

/// What was pressed since the last frame (a direction held repeats: the caller sees to it).
struct Input {
    bool a{}, b{}, x{}, y{};
    bool l{}, r{};
    bool plus{}, minus{};
    bool up{}, down{}, left{}, right{};
};

/// What the menu asks of its caller after a frame.
struct Request {
    enum class Kind { None, Exit, Launch } kind{Kind::None};
    std::string nup; // Launch: the container, as a path of the card.
};

class App {
public:
    void Start();
    Request Update(const Input& in, float dt);
    void Draw(SwitchFrontend::Ui::Canvas& c);
    /// The settings as they stand, written if they changed (the caller is about to leave).
    void Finish();

private:
    enum class Mode { Shelf, Sheet, Settings, Picker };

    struct Row {
        std::string label;
        std::string value;
        std::string help;
        bool arrows{}; // Left and right change it.
    };

    void LoadSettings();
    void SaveSettings();
    void Rescan();
    void Notice(std::string text, bool error = false);
    Game* Selected();

    Request UpdateShelf(const Input& in);
    void UpdateSheet(const Input& in);
    void UpdateSettings(const Input& in);
    void UpdatePicker(const Input& in);
    void OpenSheet();
    void OpenPicker();
    void PickerLook();
    std::vector<Row> SheetRows();
    std::vector<Row> SettingsRows();
    void ChangeSheet(int row, int dir);
    void ChangeSetting(int row, int dir, bool press);

    void DrawShelfScreen(SwitchFrontend::Ui::Canvas& c);
    void DrawSheet(SwitchFrontend::Ui::Canvas& c);
    void DrawSettings(SwitchFrontend::Ui::Canvas& c);
    void DrawPicker(SwitchFrontend::Ui::Canvas& c);
    void DrawRows(SwitchFrontend::Ui::Canvas& c, int x, int y, int w, const std::vector<Row>& rows, int cursor);
    void DrawHints(SwitchFrontend::Ui::Canvas& c, const std::vector<std::pair<const char*, const char*>>& hints);
    void DrawNotice(SwitchFrontend::Ui::Canvas& c);
    const SwitchFrontend::Ui::Image* MadeCover(Game& game);

    SwitchFrontend::Ui::Font font;
    Mode mode{Mode::Shelf};
    std::vector<Game> games;
    // A front made here for a game that has no cover (or when covers are hidden): its icon and
    // its name on a case's shape, by the container's path.
    std::unordered_map<std::string, SwitchFrontend::Ui::Image> made;
    float time{};

    // The settings (menu.cfg).
    std::string games_folder{kDefaultGames};
    int style{};
    bool covers_shown{true};
    bool cases{true};
    bool light{};
    bool settings_changed{};

    // The shelf.
    int selected{};
    float position{};
    bool flipped{};
    float flip{};

    // A game's options.
    int sheet_cursor{};
    Settings game_cfg;
    std::vector<std::string> mod_sets;

    // The settings page and its folder picker.
    int settings_cursor{};
    std::string picker_path;
    std::vector<std::string> picker_folders;
    int picker_nups{};
    int picker_cursor{};

    std::string notice;
    bool notice_error{};
    float notice_left{};
};

} // namespace Flow
