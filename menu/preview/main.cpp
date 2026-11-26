// SPDX-License-Identifier: GPL-2.0-or-later

// Renders the menu on a PC, to pictures. The menu draws on a plain canvas and is told what was
// pressed (source/app.h), so what comes out here is what the console shows: handy when nobody
// is in front of one. After toronja's tools/ui-preview.
//
//   make preview                       (from menu/: builds this and a pretend card, and runs it)
//   ./preview <card folder> <output folder>
//
// Output: one binary PPM per scene (tools/ppm_to_png.py makes PNGs of them).
//
// The receiver of files from a computer is a stand-in here: it says what the scene needs it to
// say, and opens no port on the PC. The receiver itself runs on a PC too, for trying it against
// a sender: transfer_test.cpp.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "app.h"
#include "platform.h"
#include "transfer.h"

namespace {
std::string g_address = "192.168.0.50";
std::string g_typed; // What the keyboard "gives"; empty: no name given.
Transfer::State g_transfer;
} // namespace

namespace Platform {
bool HasNetwork() {
    return false;
}
bool Download(const std::string&, const std::string&, std::string&) {
    return false;
}
bool StartNetwork() {
    return true;
}
std::string Address() {
    return g_address;
}
void KeepAwake(bool) {}
bool AskText(const std::string&, const std::string&, std::string& out) {
    out = g_typed;
    return !g_typed.empty();
}
} // namespace Platform

namespace Transfer {
void Start(const Tuning& tuning) {
    g_transfer.running = g_transfer.listening = true;
    g_transfer.queue_size = tuning.queue;
    g_transfer.lines = {"Listening on port 28302 (blocks of " + std::to_string(tuning.block_kb) + " KB, a queue of " +
                        std::to_string(tuning.queue) + ", no cap)"};
}
void Stop() {
    g_transfer.running = g_transfer.listening = g_transfer.connected = g_transfer.busy = false;
}
State Look() {
    return g_transfer;
}
} // namespace Transfer

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
    Flow::Request last;

    /// A press, then time for what it set moving to come to rest (and for covers to be decoded).
    void Press(Flow::Input in, int frames = 90) {
        last = app.Update(in, 1.0f / 60);
        app.Draw(canvas);
        for (int k = 1; k < frames; ++k) {
            app.Update({}, 1.0f / 60);
            app.Draw(canvas);
        }
    }

    /// A few of the same press.
    void Press(Flow::Input in, int times, int frames) {
        for (int k = 0; k < times; ++k) {
            Press(in, frames);
        }
    }

    /// Time alone: for a note to go.
    void Wait() {
        Press({}, 400);
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
    case 'z': in.zl = true; break;
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
    run.Press(Key('R'), 2, 90);
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

    // a game's options: the second game of the pretend card has two sets of mods and three slots of saved data
    run.Press(Key('L'), 8, 30);
    run.Press(Key('R'), 60);
    run.Shot("04-shelf-slot-in-use");
    run.Press(Key('y'));
    run.Shot("05-options");
    run.Press(Key('a'));
    run.Press(Key('D'));
    run.Press(Key('R'));
    run.Shot("06-options-changed");

    // its slots of saved data
    run.Press(Key('D'));
    run.Shot("07-options-saved-data");
    run.Press(Key('a'));
    run.Shot("08-slots");
    run.Press(Key('D'), 1, 20); // to "New empty slot"
    g_typed = "Hard mode / try 2"; // (the slash can't be in a folder's name: it is left out)
    run.Press(Key('a'), 30);
    run.Shot("09-slots-new-named");
    run.Wait();
    run.Press(Key('D'), 8, 20); // to "Copy ... into a new slot"
    g_typed.clear();            // no name comes back from the keyboard
    run.Press(Key('a'), 30);
    run.Shot("10-slots-no-name-given");
    run.Press(Key('a'), 1); // yes: one frame, the one that says it is copying
    run.Shot("11-slots-copying");
    run.Press({}, 30);
    run.Shot("12-slots-copied");
    run.Wait();
    run.Press(Key('x'), 30);
    run.Shot("13-slots-delete-question");
    run.Press(Key('a'), 30);
    run.Shot("14-slots-deleted");
    run.Wait();
    run.Press(Key('U'), 8, 20);
    run.Press(Key('x'), 30);
    run.Shot("15-slots-default-stays");
    run.Wait();
    run.Press(Key('D'), 2, 20);
    run.Press(Key('a'), 30); // another slot is put in use
    run.Shot("16-slots-picked");
    run.Wait();
    run.Press(Key('b'));

    // the advanced options, behind their warning
    run.Press(Key('D'));
    run.Press(Key('a'), 30);
    run.Shot("17-advanced-warning");
    run.Press(Key('a'));
    run.Shot("18-advanced");
    run.Press(Key('L'), 6, 10); // memory: from automatic down from the top
    run.Press(Key('D'));
    run.Press(Key('R'), 9, 10); // room: from automatic up from the bottom
    run.Shot("19-advanced-set");
    run.Press(Key('b'));
    run.Shot("20-options-advanced-set");
    run.Press(Key('b'));
    // what starting the game would hand over
    run.Press(Key('a'), 1);
    std::printf("launch: kind %d, nup \"%s\", room \"%s\"\n", static_cast<int>(run.last.kind), run.last.nup.c_str(), run.last.room.c_str());
    // and back to automatic, with X
    run.Press(Key('y'));
    run.Press(Key('D'), 3, 10);
    run.Press(Key('a'), 30);
    run.Press(Key('a'));
    run.Press(Key('x'), 30);
    run.Shot("21-advanced-reset");
    run.Press(Key('D'));
    run.Press(Key('x'), 30);
    run.Wait();
    run.Press(Key('b'));
    run.Press(Key('b'));
    run.Press(Key('a'), 1);
    std::printf("launch: kind %d, nup \"%s\", room \"%s\"\n", static_cast<int>(run.last.kind), run.last.nup.c_str(), run.last.room.c_str());

    // the file that isn't a container
    run.Press(Key('R'), 2, 60);
    run.Press(Key('a'), 30);
    run.Shot("22-not-a-container");
    run.Press(Key('R'), 4, 60);
    run.Shot("23-no-cover");
    run.Wait();

    // the menu's own settings, and the folder picker
    run.Press(Key('-'));
    run.Shot("24-settings");
    run.Press(Key('D'), 5, 20);
    run.Press(Key('a'), 30);
    run.Shot("25-settings-download");
    run.Wait();
    run.Press(Key('D'), 3, 20);
    run.Press(Key('R'), 4, 20);
    run.Shot("26-settings-receiver-pace");
    run.Press(Key('L'), 4, 20);
    run.Press(Key('U'), 8, 20);
    run.Press(Key('a'));
    run.Shot("27-picker");
    run.Press(Key('b'));
    run.Shot("28-picker-up");
    run.Press(Key('b'));
    run.Shot("29-picker-root");
    run.Press(Key('b'));

    // files from a computer
    run.Press(Key('D'), 6, 20);
    run.Press(Key('a'), 30);
    run.Shot("35-transfer-waiting");
    g_transfer.connected = g_transfer.busy = true;
    g_transfer.current = "Receiving /nx2-flow/games/Pretend Game Seven [0400F00000071000].nup";
    g_transfer.total = 6200ull << 20;
    g_transfer.queue_now = 1;
    g_transfer.files = 2;
    g_transfer.bytes = 181ull << 20;
    g_transfer.lines.push_back("192.168.0.20 connected");
    g_transfer.lines.push_back("PUT /nx2-flow/covers/ids.txt: 212 bytes, 0.1 MB/s");
    g_transfer.lines.push_back("PUT /nx2-flow/games/Another Pretend Game.nup: 180.6 MiB in 6.9 s, 26.2 MB/s; longest pause 0.21 s; queue at most 3 of 8");
    for (int k = 1; k <= 40; ++k) { // (some seconds of a steady pace, for the figure to settle)
        g_transfer.done = static_cast<std::uint64_t>(k) * (27ull << 20) / 4 + (2600ull << 20);
        run.Press({}, 15);
    }
    run.Shot("36-transfer-receiving");
    run.Press(Key('b'), 30);
    run.Shot("37-transfer-leave-question");
    run.Press(Key('b'), 30);
    g_transfer.busy = false;
    g_transfer.files = 3;
    g_transfer.bytes = 6381ull << 20;
    g_transfer.lines.push_back("PUT /nx2-flow/games/Pretend Game Seven [0400F00000071000].nup: 6200.0 MiB in 238.4 s, 26.0 MB/s; longest pause 0.34 s; queue at most 4 of 8");
    g_address.clear();
    run.Press({}, 200);
    run.Shot("38-transfer-no-network");
    g_address = "192.168.0.50";
    g_transfer.files = 0;
    run.Press(Key('b'));
    run.Wait();

    // the light theme
    run.Press(Key('U'), 3, 20);
    run.Press(Key('R'));
    run.Shot("39-settings-light");
    run.Press(Key('b'));
    run.Press(Key('L'), 8, 30);
    run.Press(Key('R'), 90);
    run.Shot("40-shelf-light");
    run.Press(Key('y'));
    run.Shot("41-options-light");
    run.Press(Key('D'), 2, 10);
    run.Press(Key('a'));
    run.Shot("42-slots-light");
    run.Press(Key('b'));
    run.Press(Key('D'));
    run.Press(Key('a'), 30);
    run.Shot("43-advanced-warning-light");
    run.Press(Key('b'));
    run.Press(Key('b'));
    run.Press(Key('z'));
    run.Shot("44-transfer-light");
    run.Press(Key('b'));

    // a card with no games
    Flow::SetCardRoot(std::string{argv[1]} + "/empty");
    Run none;
    none.out = argv[2];
    none.app.Start();
    none.Press({}, 30);
    none.Shot("45-no-games");

    // container health check: healthy game (via X on the game sheet)
    Flow::SetCardRoot(argv[1]);
    run.Press(Key('y'));
    run.Press(Key('x'), 30);
    run.Shot("46-health-ok");
    run.Press(Key('b'));
    run.Press(Key('b'));

    // container health check: oversized zstd warning + pre-launch confirmation dialog
    const std::string warn_card = std::string{argv[1]} + "/health_warn";
    std::error_code ec;
    std::filesystem::create_directories(warn_card + "/nx2-flow/games", ec);
    std::filesystem::create_directories(warn_card + "/switch/nx2-flow", ec);
    Flow::SetCardRoot(warn_card);
    if (std::FILE* f = std::fopen(Flow::OnCard("/switch/nx2-flow/nx2-game.nro").c_str(), "wb")) {
        std::fputs("preview stub\n", f);
        std::fclose(f);
    }
    // Minimal NUP with a 320 MiB Zstandard entry ("Data/World.pack") to preview the warning UI.
    {
        auto wr32 = [](std::vector<unsigned char>& b, std::size_t o, std::uint32_t v) {
            for (int i = 0; i < 4; ++i) b[o + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
        };
        auto wr64 = [](std::vector<unsigned char>& b, std::size_t o, std::uint64_t v) {
            for (int i = 0; i < 8; ++i) b[o + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
        };
        const std::string fname = "Data/World.pack";
        const std::string title = "Big Pack Adventure";
        const std::string pub = "Preview Studio";
        const std::string ver = "1.0.0";
        std::vector<unsigned char> meta;
        auto add_fact = [&](std::uint16_t k, const std::string& v) {
            const std::size_t at = meta.size();
            meta.resize(at + 8 + v.size(), 0);
            meta[at] = static_cast<unsigned char>(k & 0xFF);
            meta[at + 1] = static_cast<unsigned char>(k >> 8);
            wr32(meta, at + 4, static_cast<std::uint32_t>(v.size()));
            std::memcpy(meta.data() + at + 8, v.data(), v.size());
        };
        add_fact(1, std::string{"META"} + std::string(0x7C, '\0'));
        add_fact(4, ver);
        add_fact(5, title);
        add_fact(6, pub);

        const std::uint64_t pool_at = 0x30 + 0x40 + 0x18 + 0x50;
        const std::uint64_t toc_size = pool_at + fname.size();
        const std::uint64_t meta_at = 0x80 + toc_size;
        const std::uint64_t file_at = meta_at + meta.size();
        const std::uint64_t kept = 4096;
        std::vector<unsigned char> nup(static_cast<std::size_t>(file_at + kept), 0);
        std::memcpy(nup.data(), "NUP\x01", 4);
        wr64(nup, 0x08, 0x80);
        wr64(nup, 0x10, toc_size);
        wr64(nup, 0x38, file_at + kept);
        wr32(nup, 0x48, 1);
        std::memcpy(nup.data() + 0x80, "TOC\x01", 4);
        wr32(nup, 0x80 + 0x04, 1);
        wr64(nup, 0x80 + 0x08, 1);
        wr64(nup, 0x80 + 0x10, 1);
        wr64(nup, 0x80 + 0x18, pool_at);
        wr64(nup, 0x80 + 0x20, fname.size());
        const std::size_t pkg = 0x80 + 0x30;
        wr64(nup, pkg + 0x00, 0x0400F00000009000ull);
        wr32(nup, pkg + 0x0C, 0);
        wr64(nup, pkg + 0x20, meta_at);
        wr32(nup, pkg + 0x28, static_cast<std::uint32_t>(meta.size()));
        const std::size_t fe = 0x80 + 0x30 + 0x40 + 0x18;
        wr32(nup, fe + 0x00, 0);
        wr32(nup, fe + 0x04, static_cast<std::uint32_t>(fname.size()));
        wr64(nup, fe + 0x08, 320ull << 20);
        wr64(nup, fe + 0x10, file_at);
        wr64(nup, fe + 0x18, kept);
        nup[fe + 0x40] = 1; // zstd
        std::memcpy(nup.data() + 0x80 + pool_at, fname.data(), fname.size());
        std::memcpy(nup.data() + meta_at, meta.data(), meta.size());
        if (std::FILE* f = std::fopen(Flow::OnCard("/nx2-flow/games/Big Pack Adventure.nup").c_str(), "wb")) {
            std::fwrite(nup.data(), 1, nup.size(), f);
            std::fclose(f);
        }
    }
    Run warn;
    warn.out = argv[2];
    warn.app.Start();
    warn.Press({}, 30);
    warn.Press(Key('y'));
    warn.Press(Key('x'), 30);
    warn.Shot("47-health-zstd-warning");
    warn.Press(Key('b'));
    warn.Press(Key('b'));
    warn.Press(Key('a'), 30);
    warn.Shot("48-launch-warning");
    return 0;
}
