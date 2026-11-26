// SPDX-License-Identifier: GPL-2.0-or-later

// The menu's cover receiver (source/cover_share.cpp) on a PC, with a folder for the covers: to
// try the exchange against a sender with no console (tools/send_covers_test.py is one). The
// direct wireless link is the console's, so here the two meet on the network they share.
//
//   cover_share_test <covers folder> <seconds to run> [title id=code=title ...]
//
// What follows the seconds is the list of games without a cover, as the menu would give it.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "cover_share.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("cover_share_test <covers folder> <seconds> [title id=code=title ...]\n");
        return 1;
    }
    std::vector<CoverShare::Wanted> wanted;
    for (int k = 3; k < argc; ++k) {
        const std::string text = argv[k];
        const std::size_t a = text.find('='), b = text.find('=', a + 1);
        if (a == std::string::npos || b == std::string::npos) {
            std::printf("%s: write it as <title id>=<code>=<title>\n", argv[k]);
            return 1;
        }
        wanted.push_back({text.substr(0, a), text.substr(a + 1, b - a - 1), text.substr(b + 1)});
    }
    CoverShare::StartReceive(argv[1], wanted);
    int serial = -1;
    std::string brief;
    const int ticks = std::atoi(argv[2]) * 10;
    for (int k = 0; k < ticks; ++k) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (CoverShare::Serial() != serial || CoverShare::Brief() != brief) {
            serial = CoverShare::Serial();
            brief = CoverShare::Brief();
            std::printf("[%s] %s%s\n", brief.c_str(), CoverShare::Detail().c_str(), CoverShare::Failed() ? "  (failed)" : "");
            std::fflush(stdout);
        }
        if (!CoverShare::IsActive()) {
            break;
        }
    }
    CoverShare::Stop();
    std::printf("%u stored\n", CoverShare::Stored());
    return 0;
}
