// SPDX-License-Identifier: GPL-2.0-or-later

// What the menu asks of the machine it runs on: the console (main_switch.cpp) or a PC that
// renders previews (preview/main.cpp).

#pragma once

#include <string>

namespace Platform {

/// Whether the machine is on a network at all.
bool HasNetwork();

/// Fetches a URL into a file. False, with `error` in words when there is something to say,
/// when it couldn't (a 404 is no error to tell: the thing isn't there).
bool Download(const std::string& url, const std::string& to_file, std::string& error);

} // namespace Platform
