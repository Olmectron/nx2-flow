#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# The PC tests of games kept in parts, step by step (as render_previews.sh: on Windows the PC compiler is in devkitPro's own
# MSYS2 shell, and the Python that has Pillow may be another): run from menu/, in any shell that has python.
#
#   sh tools/test_parts.sh
#
# 1. the library's list of a pretend card with the same game whole and cut in parts (preview/parts_test.cpp);
# 2. the file receiver writing what it is sent in parts, menu.cfg's split=<MB>, and the library listing what arrived
#    (preview/transfer_test.cpp, against itself: it opens the receiver's port on this PC for a moment).
# Ends with 0 when both held.
set -e
DKP_SHELL=${DKP_SHELL:-C:/devkitPro/msys2/usr/bin}
HERE=$(pwd)
if [ -x "$DKP_SHELL/bash.exe" ]; then
    "$DKP_SHELL/bash.exe" -lc "cd '$(cygpath -u "$HERE" 2>/dev/null || echo "$HERE")' && make partstest nettest"
    PATH="$(cygpath -u "$DKP_SHELL" 2>/dev/null || echo "$DKP_SHELL"):$PATH"
else
    make partstest nettest
fi
rm -rf out/preview/parts_card out/preview/split_card
python tools/make_parts_card.py out/preview/parts_card
out/preview/parts_test out/preview/parts_card
python tools/make_parts_card.py out/preview/split_card
out/preview/transfer_test out/preview/split_card split
