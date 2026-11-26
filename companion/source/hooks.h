// SPDX-FileCopyrightText: Azahar Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// What the two workers (cover_fetch.cpp, cover_send.cpp) need from the program around them:
// main.cpp implements these. There are no threads here: a worker runs until it is done, and
// asks Cancelled() often enough that the screen keeps up and a press of B is seen.
namespace Companion {

/// The folder the covers live in on this console's card (fronts in it, whole wraps in its
/// "full" folder).
std::string CoverFolder();
/// A line for the screen's log.
void Log(const std::string& text);
/// What the worker is at, in a few words and a sentence: shown above the log.
void Status(const std::string& brief, const std::string& detail);
/// The console at the other end, as the screen names it; empty when there is none.
void Peer(const std::string& text);
/// The user asked to stop (B), or the system asked the program to close. Also where the
/// screen is drawn and the pad is read while a worker runs.
bool Cancelled();

} // namespace Companion
