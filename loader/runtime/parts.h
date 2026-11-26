// A file the card keeps in parts: where each of its bytes is.
// A FAT32 card keeps no file of 4 GiB or more (one there is 4 GiB less one byte at most), and a game's NUP file is far larger.
// One is copied onto the card as <name>.nup.001, .002, ..., each part as long as the others but the last, and the parts in order
// are the file: nothing in the file says so, and nothing of its format changes. (nx2-relink keeps a game's files that way too,
// <name>.romfs.00, .01, ...: so parts numbered from 0 and from 1 are both read, of any width.)
// Nothing here is the console's own: the same functions are tried on a computer (tests/parts_test.c, tools/test_parts.py).
#pragma once
#include "nx2.h"

#define NX2_PARTS 64  // the most parts a file is read in (255 GiB, in parts of 4095 MB)

typedef struct {
    u64 at;    // where the part begins in the file
    s64 size;  // how many bytes it holds
} Nx2Part;

typedef struct {
    Nx2Part part[NX2_PARTS];
    u32 count;  // how many there are (0: none yet)
    u64 size;   // the whole file's size
} Nx2Parts;

static void nx2_parts_reset(Nx2Parts *p) { p->count = 0, p->size = 0; }

// A part after the last. 0: it is in. -1: it holds nothing, or there is no place for it.
static int nx2_parts_add(Nx2Parts *p, s64 size) {
    if (size <= 0 || p->count >= NX2_PARTS) return -1;
    p->part[p->count].at = p->size, p->part[p->count].size = size;
    p->count++, p->size += (u64)size;
    return 0;
}

// The part that holds the byte at `at` (which is in the file): the last that begins at or before it.
static u32 nx2_parts_of(const Nx2Parts *p, u64 at) {
    u32 lo = 0, hi = p->count;
    while (lo + 1 < hi) {
        const u32 mid = lo + (hi - lo) / 2;
        if (p->part[mid].at <= at) lo = mid;
        else hi = mid;
    }
    return lo;
}

// How many of `want` bytes from `at` lie in part k: 0 when k holds none of them (so a read never leaves its part).
static u64 nx2_parts_room(const Nx2Parts *p, u32 k, u64 at, u64 want) {
    const Nx2Part *part = &p->part[k];
    if (at < part->at || at - part->at >= (u64)part->size) return 0;
    const u64 left = (u64)part->size - (at - part->at);
    return want < left ? want : left;
}

// Do parts of these sizes, by their numbers, make one whole file of `size` bytes (0: not known)? 0: they do.
// 1: the numbers don't follow one another from 0 or 1 (a part missing, or two of one number). 2: a part but the last isn't as
// long as the first (or holds nothing). 3: they hold other than `size` bytes: a copy that was cut short, or a part of another
// copy left over.
static int nx2_parts_whole(const u64 *numbers, const s64 *sizes, u32 count, u64 size) {
    if (!count || numbers[0] > 1) return 1;
    u64 total = 0;
    for (u32 k = 0; k < count; k++) {
        if (k && numbers[k] != numbers[k - 1] + 1) return 1;
        if (sizes[k] <= 0 || (k + 1 < count && sizes[k] != sizes[0])) return 2;
        total += (u64)sizes[k];
    }
    return size && size != total ? 3 : 0;
}
