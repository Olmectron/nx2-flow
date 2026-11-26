// SPDX-License-Identifier: GPL-2.0-or-later

// nx2-flow covers: the companion of nx2-flow, for a Switch that is on the internet.
//
// It does two things and nothing else: it downloads the covers that the file ids.txt names
// from GameTDB, and it hands the covers it has to nx2-flow's menu on another console, over
// the network both are on or over a direct wireless link. It is made to be started from the
// Album (applet mode): a text screen, one thread, nothing kept in memory but the file that
// is on its way.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include <switch.h>

#include "cover_fetch.h"
#include "cover_send.h"
#include "hooks.h"

#ifndef VERSION
#define VERSION "dev"
#endif

namespace {

constexpr char FOLDER[] = "sdmc:/nx2-flow-covers";
constexpr std::size_t WIDTH = 78;    // Characters of a line of the screen that are used.
constexpr std::size_t LOG_ROWS = 29; // Lines of the log that fit under the head.
constexpr std::size_t LOG_KEPT = 200;

/// A cover that ids.txt asks for: the code GameTDB files it under, and the title ids the
/// file says the code belongs to (none, when the line was the code alone).
struct Wanted {
    std::string code;
    std::vector<std::string> title_ids;
};

PadState s_pad;
bool s_sockets = false, s_nifm = false;
bool s_working = false, s_cancel = false, s_quit = false, s_said_stopping = false;
bool s_dirty = true;
u64 s_last_pump = 0, s_last_draw = 0, s_last_active = 0;

std::string s_brief = "Ready", s_detail, s_peer;
std::deque<std::string> s_log;

std::vector<Wanted> s_wanted;
bool s_ids_file = false;
unsigned s_bare_title_ids = 0, s_unread_lines = 0;
unsigned s_fronts = 0, s_wraps = 0;
unsigned s_without = 0; // Codes of ids.txt that have no picture on this card.

u64 MsSince(u64 tick) {
    return armTicksToNs(armGetSystemTick() - tick) / 1'000'000ull;
}

// -------------------------------------------------------------------------------- screen

bool Online() {
    NifmInternetConnectionType type{};
    u32 strength = 0;
    NifmInternetConnectionStatus status{};
    return s_nifm && R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) &&
           status == NifmInternetConnectionStatus_Connected;
}

void Line(const std::string& text) {
    std::printf(" %.*s\n", static_cast<int>(WIDTH), text.c_str());
}

void Rule() {
    Line(std::string(WIDTH, '-'));
}

void Draw() {
    s_dirty = false;
    s_last_draw = armGetSystemTick();
    std::printf("\x1b[2J\x1b[1;1H");
    Line("nx2-flow covers " VERSION);
    Line("Downloads covers from GameTDB and hands them to nx2-flow on another console.");
    Rule();
    Line(appletGetAppletType() == AppletType_LibraryApplet
             ? "Started   : from the Album (applet mode)"
             : "Started   : over a game. If the link is refused, start it from the Album.");
    Line("Covers    : " + std::to_string(s_fronts) + (s_fronts == 1 ? " front, " : " fronts, ") +
         std::to_string(s_wraps) + (s_wraps == 1 ? " whole wrap" : " whole wraps") + ", in " + FOLDER);
    if (!s_ids_file) {
        Line("ids.txt   : not there");
    } else {
        Line("ids.txt   : " + std::to_string(s_wanted.size()) + (s_wanted.size() == 1 ? " code, " : " codes, ") +
             std::to_string(s_without) + " without a cover yet" +
             (s_bare_title_ids > 0 ? "; " + std::to_string(s_bare_title_ids) + " title ids with no code" : ""));
    }
    Line(std::string("Internet  : ") + (Online() ? "connected" : "not connected"));
    Line("Connected : " + (s_peer.empty() ? std::string("nobody") : s_peer));
    Line("");
    Line("Now       : " + s_brief + (s_detail.empty() ? "" : "  " + s_detail));
    Line("");
    Line("A  Download the covers ids.txt names     X  Send the covers to nx2-flow");
    Line("B  Stop what is running, or exit         +  Exit");
    Rule();
    const std::size_t from = s_log.size() > LOG_ROWS ? s_log.size() - LOG_ROWS : 0;
    for (std::size_t i = from; i < s_log.size(); ++i) {
        Line(s_log[i]);
    }
    consoleUpdate(nullptr);
}

/// Reads the pad, keeps the system informed and the screen drawn. The buttons pressed since
/// the last time are returned. `always`: even when it was done a moment ago.
u64 Pump(bool always) {
    if (!always && MsSince(s_last_pump) < 30) {
        return 0;
    }
    s_last_pump = armGetSystemTick();
    if (!appletMainLoop()) {
        s_cancel = s_quit = true;
        return 0;
    }
    padUpdate(&s_pad);
    const u64 down = padGetButtonsDown(&s_pad);
    if (s_working) {
        // (held, not pressed: a wait of a second in the system must not swallow the press)
        if ((padGetButtons(&s_pad) & HidNpadButton_B) != 0) {
            s_cancel = true;
        }
        if ((down & HidNpadButton_Plus) != 0) {
            s_cancel = s_quit = true;
        }
        if (s_cancel && !s_said_stopping) {
            s_said_stopping = true;
            Companion::Log("Stopping...");
        }
        if (MsSince(s_last_active) > 10'000) {
            s_last_active = armGetSystemTick();
            appletReportUserIsActive(); // Or the console dims and sleeps in the middle of it.
        }
    }
    if (s_dirty || MsSince(s_last_draw) > 500) {
        Draw();
    }
    return down;
}

// --------------------------------------------------------------------------------- files

std::string Upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return text;
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool IsTitleId(const std::string& text) {
    return text.size() == 16 &&
           std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

/// What a GameTDB code may look like: a few letters and digits. Which code a game has is
/// not something this program knows or works out.
bool IsCode(const std::string& text) {
    return text.size() >= 3 && text.size() <= 12 &&
           std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isalnum(ch) != 0; });
}

bool Exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool HasCover(const std::string& code) {
    return Exists(Companion::CoverFetch::FrontPath(code)) || Exists(Companion::CoverFetch::WrapPath(code));
}

/// The pictures of a folder that the other console would take.
std::vector<std::string> Pictures(const std::string& folder) {
    std::vector<std::string> names;
    DIR* dir = opendir(folder.c_str());
    if (dir == nullptr) {
        return names;
    }
    while (const dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (Companion::CoverSend::GoodName(name) && Exists(folder + "/" + name)) {
            names.push_back(name);
        }
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    return names;
}

void CountCovers() {
    s_fronts = static_cast<unsigned>(Pictures(FOLDER).size());
    s_wraps = static_cast<unsigned>(Pictures(std::string(FOLDER) + "/full").size());
    s_without = 0;
    for (const Wanted& wanted : s_wanted) {
        s_without += HasCover(wanted.code) ? 0 : 1;
    }
    s_dirty = true;
}

void Want(const std::string& code, const std::string& title_id) {
    auto at = std::find_if(s_wanted.begin(), s_wanted.end(),
                           [&](const Wanted& wanted) { return wanted.code == code; });
    if (at == s_wanted.end()) {
        if (s_wanted.size() >= 4000) {
            return;
        }
        s_wanted.push_back({code, {}});
        at = s_wanted.end() - 1;
    }
    if (!title_id.empty() &&
        std::find(at->title_ids.begin(), at->title_ids.end(), title_id) == at->title_ids.end()) {
        at->title_ids.push_back(title_id);
    }
}

/// ids.txt: a line is a GameTDB code, or "<title id>=<code>" (what nx2-flow's own ids.txt
/// holds, so that file can be copied here as it is). "#" starts a comment.
void ReadIds() {
    s_wanted.clear();
    s_bare_title_ids = s_unread_lines = 0;
    std::FILE* file = std::fopen((std::string(FOLDER) + "/ids.txt").c_str(), "r");
    s_ids_file = file != nullptr;
    s_dirty = true;
    if (file == nullptr) {
        return;
    }
    char line[256];
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        std::string text = line;
        if (text.rfind("\xEF\xBB\xBF", 0) == 0) {
            text.erase(0, 3);
        }
        text = text.substr(0, text.find('#'));
        std::vector<std::string> words;
        std::string word;
        for (const char ch : text + " ") {
            if (ch == '=' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
                if (!word.empty()) {
                    words.push_back(word);
                    word.clear();
                }
            } else {
                word += ch;
            }
        }
        if (words.empty()) {
            continue;
        }
        if (words.size() == 1 && IsTitleId(words[0])) {
            ++s_bare_title_ids; // No code beside it, and none is guessed.
        } else if (words.size() == 1 && IsCode(words[0])) {
            Want(Upper(words[0]), {});
        } else if (words.size() == 2 && IsTitleId(words[0]) && IsCode(words[1])) {
            Want(Upper(words[1]), Lower(words[0]));
        } else {
            ++s_unread_lines;
        }
    }
    std::fclose(file);
}

// ---------------------------------------------------------------------------------- jobs

struct Working {
    bool before;
    Working() : before(s_working) {
        if (!before) {
            s_cancel = s_said_stopping = false;
            padUpdate(&s_pad);
        }
        s_working = true;
        s_last_active = armGetSystemTick();
    }
    ~Working() {
        s_working = before;
        if (!before) {
            Companion::Status("Ready", {});
            Companion::Peer({});
        }
    }
};

/// Downloads the covers ids.txt names that this card has none of yet.
void RunDownload() {
    ReadIds();
    CountCovers();
    if (!s_ids_file) {
        Companion::Log(std::string("There is no ") + FOLDER + "/ids.txt: write one, a GameTDB code or "
                       "<title id>=<code> on each line, to say which covers to download.");
        return;
    }
    if (s_bare_title_ids > 0) {
        Companion::Log(std::to_string(s_bare_title_ids) + " lines of ids.txt are a title id alone. GameTDB "
                       "files covers under its own codes, not title ids: write <title id>=<code>.");
    }
    if (s_unread_lines > 0) {
        Companion::Log(std::to_string(s_unread_lines) + " lines of ids.txt were not understood.");
    }
    std::vector<std::string> codes;
    for (const Wanted& wanted : s_wanted) {
        if (!HasCover(wanted.code)) {
            codes.push_back(wanted.code);
        }
    }
    if (codes.empty()) {
        Companion::Log(s_wanted.empty() ? "ids.txt names no codes." : "Every code of ids.txt has its cover already.");
        return;
    }
    if (!s_sockets || !Online()) {
        Companion::Log(std::to_string(codes.size()) + (codes.size() == 1 ? " cover is" : " covers are") +
                       " still to download, and this console is not connected to the internet.");
        return;
    }
    Working working;
    Companion::Log("Downloading " + std::to_string(codes.size()) + (codes.size() == 1 ? " cover" : " covers") +
                   " from GameTDB...");
    const Companion::CoverFetch::Outcome outcome = Companion::CoverFetch::Fetch(codes);
    Companion::Log(outcome.words);
    CountCovers();
}

/// The files to send: the whole wraps and then the fronts. A cover that ids.txt ties to
/// title ids goes under each title id, which is a name nx2-flow finds without a list of
/// codes of its own; any other goes under the name it has here.
std::vector<Companion::CoverSend::Item> ToSend() {
    std::vector<Companion::CoverSend::Item> items;
    for (const bool wrap : {true, false}) {
        const std::string folder = wrap ? std::string(FOLDER) + "/full" : std::string(FOLDER);
        for (const std::string& name : Pictures(folder)) {
            const std::size_t dot = name.rfind('.');
            const std::string code = Upper(name.substr(0, dot));
            const auto at = std::find_if(s_wanted.begin(), s_wanted.end(),
                                         [&](const Wanted& wanted) { return wanted.code == code; });
            if (at == s_wanted.end() || at->title_ids.empty()) {
                items.push_back({folder + "/" + name, name, wrap});
                continue;
            }
            for (const std::string& title_id : at->title_ids) {
                items.push_back({folder + "/" + name, title_id + Lower(name.substr(dot)), wrap});
            }
        }
    }
    return items;
}

void RunSend() {
    if (!s_sockets) {
        Companion::Log("The network could not be started: nothing can be sent.");
        return;
    }
    Working working;
    // What is asked for and not here yet is fetched first, while the internet is still there:
    // the direct wireless link takes the console off its network for as long as it lasts.
    ReadIds();
    CountCovers();
    if (s_without > 0 && Online()) {
        RunDownload();
    }
    if (s_cancel) {
        return;
    }
    const std::vector<Companion::CoverSend::Item> items = ToSend();
    if (items.empty()) {
        Companion::Log("There are no covers on this console to send.");
        return;
    }
    Companion::Log("Sending " + std::to_string(items.size()) + (items.size() == 1 ? " file" : " files") +
                   ". On the other console, open nx2-flow and choose to receive covers.");
    const Companion::CoverSend::Outcome outcome = Companion::CoverSend::Send(items);
    Companion::Log(outcome.words);
}

} // namespace

// ------------------------------------------------------------- what the workers ask of us

namespace Companion {

std::string CoverFolder() {
    return FOLDER;
}

void Log(const std::string& text) {
    // In lines the screen can hold, broken at a space where there is one.
    std::string rest = text;
    bool first = true;
    while (!rest.empty()) {
        const std::size_t room = first ? WIDTH : WIDTH - 2;
        std::size_t cut = rest.size();
        if (cut > room) {
            cut = rest.rfind(' ', room);
            if (cut == std::string::npos || cut < room / 2) {
                cut = room;
            }
        }
        s_log.push_back((first ? "" : "  ") + rest.substr(0, cut));
        rest.erase(0, cut);
        rest.erase(0, rest.find_first_not_of(' '));
        first = false;
    }
    while (s_log.size() > LOG_KEPT) {
        s_log.pop_front();
    }
    s_dirty = true;
}

void Status(const std::string& brief, const std::string& detail) {
    s_brief = brief;
    s_detail = detail;
    s_dirty = true;
}

void Peer(const std::string& text) {
    if (s_peer != text) {
        if (!text.empty()) {
            Log("Connected to " + text + ".");
        }
        s_peer = text;
        s_dirty = true;
    }
}

bool Cancelled() {
    if (!s_cancel) {
        Pump(false);
    }
    return s_cancel;
}

} // namespace Companion

int main(int, char**) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&s_pad);
    s_sockets = R_SUCCEEDED(socketInitializeDefault());
    s_nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));

    mkdir(FOLDER, 0777);
    mkdir((std::string(FOLDER) + "/full").c_str(), 0777);

    Companion::Log(std::string("Covers are kept in ") + FOLDER + ", whole wraps in its folder \"full\".");
    if (!s_sockets) {
        Companion::Log("The network could not be started: covers can be neither downloaded nor sent.");
    }
    // What ids.txt names and this card lacks is fetched without being asked.
    RunDownload();

    while (!s_quit) {
        const u64 down = Pump(true);
        if (s_quit || (down & (HidNpadButton_B | HidNpadButton_Plus)) != 0) {
            break;
        }
        if ((down & HidNpadButton_A) != 0) {
            RunDownload();
        } else if ((down & HidNpadButton_X) != 0) {
            RunSend();
        }
        svcSleepThread(16'000'000);
    }

    Companion::CoverFetch::Close();
    if (s_nifm) {
        nifmExit();
    }
    if (s_sockets) {
        socketExit();
    }
    consoleExit(nullptr);
    return 0;
}
