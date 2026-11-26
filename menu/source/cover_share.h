// SPDX-FileCopyrightText: Azahar Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Covers from another console, for one that is kept off the internet: the receiving half of
// toronja's cover share (src/citra_switch/cover_share.cpp), with nothing changed on the wire.
//
// This console waits; the other (the companion program, on a console that has internet
// access) looks for it. They meet on the network both are already on when there is one (the
// sender's broadcast finds this console), and otherwise over a direct wireless link this
// console opens just for the transfer (LDN). The files then travel over one TCP connection,
// each with a checksum and an answer.
//
// One thing is added (docs/layout.md, "Covers by local wireless"): the sender may first ask
// which covers are missing here, and gets the list of the games that have none.

#pragma once

#include <string>
#include <vector>

namespace CoverShare {

/// A game that has no cover yet: what the sender is told when it asks.
struct Wanted {
    std::string id;    // The title id, 16 hex digits, lower case.
    std::string code;  // The code GameTDB files it under; empty when unknown.
    std::string title;
};

/// Starts waiting for a sender, on a thread of its own. `folder` is where covers go, as the C
/// library takes it (fronts in it, whole wraps in its "full" folder). Nothing happens if a
/// wait is already under way.
void StartReceive(std::string folder, std::vector<Wanted> wanted);
/// Stops and waits for the thread.
void Stop();
bool IsActive();

/// A few words on where it is ("Waiting", "3 / 12", "12 received"), and a sentence.
std::string Brief();
std::string Detail();
/// The sentences so far, the last few.
std::vector<std::string> Lines();
/// Goes up each time there is something to tell the user (a step, the ending).
int Serial();
/// Whether what Serial last announced is a failure.
bool Failed();
/// Covers stored since StartReceive.
unsigned Stored();
/// True once after covers were stored: the shelf should look again.
bool TakeArrived();

} // namespace CoverShare
