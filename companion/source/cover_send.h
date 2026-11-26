// SPDX-FileCopyrightText: Azahar Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

// Covers from this console to another, for a console that is kept off the internet.
//
// This is the sending half of toronja's cover share (src/citra_switch/cover_share.cpp), with
// nothing changed on the wire: the receiving console waits; this one looks for it. They meet
// on the network both are already on when there is one (a broadcast finds the receiver), and
// otherwise over a direct wireless link the receiver opens just for the transfer (LDN). The
// files then travel over one TCP connection, each with a checksum and an answer.
namespace Companion::CoverSend {

/// One file to send: where it is on this card, and the name the receiver stores it under
/// (letters and digits, then .jpg, .jpeg or .png: the receiver refuses anything else).
struct Item {
    std::string path;
    std::string name;
    bool wrap{}; // The whole wrap (the receiver's "full" folder), not the front alone.
};

struct Outcome {
    bool reached{};  // The other console was found and answered.
    bool finished{}; // The stream ended as it should.
    unsigned stored{}, refused{}, total{};
    std::string words; // How it went, as a sentence for the user.
};

/// Sends the files. Runs until done, stopped (Companion::Cancelled) or failed.
Outcome Send(const std::vector<Item>& items);

/// Whether a name is one the receiver takes.
bool GoodName(const std::string& name);

} // namespace Companion::CoverSend
