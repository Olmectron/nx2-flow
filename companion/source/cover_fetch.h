// SPDX-FileCopyrightText: Azahar Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

// Cover art from GameTDB, over this console's own internet connection.
//
// The way toronja fetches covers (src/citra_switch/covers.cpp), for the part of the site
// that holds this family of consoles: a picture is found by the code GameTDB files the game
// under, and by nothing else. For each code the whole wrap is asked for and then the front,
// in the regions the code's last letter makes likely.
namespace Companion::CoverFetch {

struct Outcome {
    unsigned fetched{};   // Codes that got a picture now.
    unsigned not_there{}; // Codes the site has no picture for.
    unsigned failed{};    // Codes that could not be asked about (no connection, no room...).
    bool stopped{};       // Stopped by the user, or given up: the site cannot be reached.
    std::string words;    // How it went, as a sentence for the user.
};

/// Where a code's pictures are kept on this card.
std::string FrontPath(const std::string& code);
std::string WrapPath(const std::string& code);

/// Fetches the pictures of each code (letters and digits only). Runs until done, stopped
/// (Companion::Cancelled) or failed.
Outcome Fetch(const std::vector<std::string>& codes);

/// Lets go of the system's TLS service, if it was used.
void Close();

} // namespace Companion::CoverFetch
