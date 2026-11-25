// SPDX-License-Identifier: GPL-2.0-or-later

// Renders the menu on a PC, to pictures. The menu draws on a plain canvas and is told what was
// pressed (source/app.h), so what comes out here is what the console shows: handy when nobody
// is in front of one. After toronja's tools/ui-preview.
//
//   make preview                       (from menu/: builds this and a pretend card, and runs it)
//   ./preview <card folder> <output folder>
//
// Output: one binary PPM per scene (tools/ppm_to_png.py makes PNGs of them).

#include <cstdio>
#include <string>

#include "app.h"
#include "platform.h"

namespace Platform {
bool HasNetwork() {
    return false;
}
bool Download(const std::string&, const std::string&, std::string&) {
    return false;
}
} // namespace Platform

namespace {

using namespace SwitchFrontend::Ui;

void Save(Canvas& c, const std::string& dir, const char* name) {
    const std::string path = dir + "/" + name + ".ppm";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::printf("can't write %s\n", path.c_str());
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", c.Width(), c.Height());
    const u32* px = c.Data();
    for (int i = 0; i < c.Width() * c.Height(); ++i) {
        const unsigned char rgb[3] = {static_cast<unsigned char>(px[i] & 0xFF), static_cast<unsigned char>((px[i] >> 8) & 0xFF),
                                      static_cast<unsigned char>((px[i] >> 16) & 0xFF)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    std::printf("%s\n", path.c_str());
}

struct Run {
    Flow::App app;
    Canvas canvas;
    std::string out;

    /// A press, then time for what it set moving to come to rest (and for covers to be decoded).
    void Press(Flow::Input in, int frames = 90) {
        app.Update(in, 1.0f / 60);
        app.Draw(canvas);
        for (int k = 1; k < frames; ++k) {
            app.Update({}, 1.0f / 60);
            app.Draw(canvas);
        }
    }

    void Shot(const char* name) {
        app.Draw(canvas);
        Save(canvas, out, name);
    }
};

Flow::Input Key(char which) {
    Flow::Input in;
    switch (which) {
    case 'a': in.a = true; break;
    case 'b': in.b = true; break;
    case 'x': in.x = true; break;
    case 'y': in.y = true; break;
    case 'l': in.l = true; break;
    case 'r': in.r = true; break;
    case '+': in.plus = true; break;
    case '-': in.minus = true; break;
    case 'U': in.up = true; break;
    case 'D': in.down = true; break;
    case 'L': in.left = true; break;
    case 'R': in.right = true; break;
    default: break;
    }
    return in;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::printf("preview <card folder> <output folder>\n");
        return 1;
    }
    Flow::SetCardRoot(argv[1]);
    Run run;
    run.out = argv[2];
    run.app.Start();
    run.Press({}, 240);
    run.Press(Key('R'));
    run.Press(Key('R'));
    run.Shot("01-shelf");
    run.Press(Key('x'));
    run.Shot("02-flipped");
    run.Press(Key('x'));
    // every way the games can stand
    for (int k = 1; k < static_cast<int>(ShelfStyle::Count); ++k) {
        run.Press(Key('r'), 400); // (long enough for the note that names it to go)
        char name[64];
        std::snprintf(name, sizeof name, "03-style-%d-%s", k, ShelfStyleName(static_cast<ShelfStyle>(k)));
        run.Shot(name);
    }
    run.Press(Key('r'), 400);
    // a game's options: the first game of the pretend card has two sets of mods
    for (int k = 0; k < 8; ++k) {
        run.Press(Key('L'), 30);
    }
    run.Press(Key('R'), 60);
    run.Press(Key('y'));
    run.Shot("04-options");
    run.Press(Key('a'));
    run.Press(Key('D'));
    run.Press(Key('R'));
    run.Press(Key('D'));
    run.Press(Key('R'));
    run.Press(Key('R'));
    run.Shot("05-options-changed");
    // put back, so that a second run of the preview starts from the same card
    run.Press(Key('L'));
    run.Press(Key('L'));
    run.Press(Key('U'));
    run.Press(Key('L'));
    run.Press(Key('U'));
    run.Press(Key('a'));
    run.Press(Key('b'));
    // the file that isn't a container
    for (int k = 0; k < 2; ++k) {
        run.Press(Key('R'), 60);
    }
    run.Press(Key('a'), 30);
    run.Shot("06-not-a-container");
    for (int k = 0; k < 4; ++k) {
        run.Press(Key('R'), 60);
    }
    run.Shot("06-no-cover");
    run.Press({}, 400);
    // the menu's own settings, and the folder picker
    run.Press(Key('-'));
    run.Shot("07-settings");
    for (int k = 0; k < 5; ++k) {
        run.Press(Key('D'), 20);
    }
    run.Press(Key('a'), 30);
    run.Shot("08-settings-download");
    run.Press({}, 400);
    for (int k = 0; k < 5; ++k) {
        run.Press(Key('U'), 20);
    }
    run.Press(Key('a'));
    run.Shot("09-picker");
    run.Press(Key('b'));
    run.Shot("10-picker-up");
    run.Press(Key('b'));
    run.Shot("11-picker-root");
    run.Press(Key('b'));
    // the light theme
    for (int k = 0; k < 4; ++k) {
        run.Press(Key('D'), 20);
    }
    run.Press(Key('R'));
    run.Shot("12-settings-light");
    run.Press(Key('b'));
    for (int k = 0; k < 8; ++k) {
        run.Press(Key('L'), 30);
    }
    run.Press(Key('R'), 90);
    run.Shot("13-shelf-light");
    run.Press(Key('y'));
    run.Shot("14-options-light");
    run.Press(Key('b'));
    run.Press(Key('-'));
    for (int k = 0; k < 4; ++k) {
        run.Press(Key('D'), 20);
    }
    run.Press(Key('R'));
    run.Press(Key('b'));
    // a card with no games
    Flow::SetCardRoot(std::string{argv[1]} + "/empty");
    Run none;
    none.out = argv[2];
    none.app.Start();
    none.Press({}, 30);
    none.Shot("15-no-games");
    return 0;
}
