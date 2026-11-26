// parts.h tried on a computer: where each byte of a file kept in parts is, and whether parts make one whole file. Nothing of a
// console's is needed, so tools/test_parts.py builds this with the computer's own C compiler and runs it:
//   cc -std=c11 -O2 -Wall -Wextra -I runtime tests/parts_test.c -o parts_test && ./parts_test
#include <stdio.h>
#include <string.h>

#include "parts.h"

static int failed;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            failed++;                                                   \
            printf("%s:%d: failed: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                               \
    } while (0)

static u64 state = 0x9E3779B97F4A7C15ull;
static u64 rnd(void) {  // (splitmix64: the same series each run, so that a failure is found again)
    state += 0x9E3779B97F4A7C15ull;
    u64 z = state;
    z = (z ^ z >> 30) * 0xBF58476D1CE4E5B9ull, z = (z ^ z >> 27) * 0x94D049BB133111EBull;
    return z ^ z >> 31;
}

// The part that holds the byte at `at`, told the slow way.
static u32 naive(const Nx2Parts *p, u64 at) {
    for (u32 k = 0; k < p->count; k++)
        if (at < p->part[k].at + (u64)p->part[k].size) return k;
    return p->count;
}

// A read made as the loader's nup_read makes it: in pieces of at most 4 MB that never leave the part they are in. 0: the pieces
// cover exactly `size` bytes from `at`, each from the part that holds it. With `seen`, each byte read is marked with the part
// it came from.
static int read_like_the_loader(const Nx2Parts *p, u64 at, u64 len, u8 *seen) {
    if (at > p->size || len > p->size - at) return -1;
    const u64 end = at + len;
    u32 pieces = 0;
    while (len) {
        const u32 k = nx2_parts_of(p, at);
        u64 take = len < (4u << 20) ? len : 4u << 20;
        const u64 room = nx2_parts_room(p, k, at, take);
        if (!room) return -2;
        take = room;
        if (p->part[k].at > at || at + take > p->part[k].at + (u64)p->part[k].size) return -3;
        if (seen)
            for (u64 b = at; b < at + take; b++) seen[b] = (u8)k;
        at += take, len -= take, pieces++;
    }
    // (a piece for every 4 MB, and one more each time the read enters a part: never more than those two together)
    return at == end && pieces <= (end >> 22) + 1 + p->count ? 0 : -4;
}

static void where_each_byte_is(void) {
    enum { ROUNDS = 6000, TINY = 128 };  // (tiny parts: every byte of such a file fits in a table)
    static u8 owner[NX2_PARTS * TINY], seen[NX2_PARTS * TINY];
    for (int round = 0; round < ROUNDS; round++) {
        Nx2Parts p;
        nx2_parts_reset(&p);
        const int tiny = round % 3 == 0;
        const int megabytes = round % 12 == 7;  // (parts of as many megabytes as a game's file has them: a few, or the walk is long)
        const u32 count = megabytes ? 1 + rnd() % 8 : 1 + rnd() % NX2_PARTS;
        // (most sets are as a card has them: of one size but the last. A set in seven is of sizes of any kind)
        const u64 one = tiny ? 1 + rnd() % TINY : 1 + rnd() % (megabytes ? 4095 : 5000);
        const u64 scale = megabytes ? 1u << 20 : 1;
        for (u32 k = 0; k < count; k++) {
            const u64 size = tiny ? 1 + rnd() % TINY : (k + 1 == count || round % 7 == 0 ? 1 + rnd() % one : one) * scale;
            CHECK(nx2_parts_add(&p, (s64)size) == 0);
        }
        CHECK(p.count == count);
        u64 total = 0;
        for (u32 k = 0; k < count; k++) {
            CHECK(p.part[k].at == total);
            if (tiny)
                for (u64 b = 0; b < (u64)p.part[k].size; b++) owner[total + b] = (u8)k;
            total += (u64)p.part[k].size;
        }
        CHECK(p.size == total);
        for (int t = 0; t < 30; t++) {
            const u64 at = rnd() % p.size;
            const u32 k = nx2_parts_of(&p, at);
            CHECK(k == naive(&p, at));
            CHECK(k < count && p.part[k].at <= at && at < p.part[k].at + (u64)p.part[k].size);
            const u64 end = p.part[k].at + (u64)p.part[k].size;
            const u64 wants[5] = {1, 2, 4096, 4u << 20, ~0ull};
            for (int w = 0; w < 5; w++) {
                CHECK(nx2_parts_room(&p, k, at, wants[w]) == (wants[w] < end - at ? wants[w] : end - at));
                for (u32 other = 0; other < count; other++)  // (no other part holds any of them)
                    if (other != k) CHECK(nx2_parts_room(&p, other, at, wants[w]) == 0);
            }
        }
        // Reads from where each part begins and ends (of a few parts, in the bigger sets), of lengths that stop short of the
        // part's end, reach just to it, and go over into the next. In the tiny sets every byte is checked against its part.
        const u32 step = count > 8 ? count / 8 : 1;
        for (u32 k = 0; k < count; k += step) {
            const u64 start = p.part[k].at, end = start + (u64)p.part[k].size;
            const u64 froms[5] = {start, end - 1, start + (end - start) / 2, end > 4096 ? end - 4096 : start, end > 999 ? end - 999 : start};
            for (int f = 0; f < 5; f++) {
                const u64 lengths[6] = {1, 2, 999, 4096, 4u << 20, rnd() % p.size + 1};
                for (int l = 0; l < 6; l++) {
                    const u64 len = lengths[l] < p.size - froms[f] ? lengths[l] : p.size - froms[f];
                    CHECK(read_like_the_loader(&p, froms[f], len, NULL) == 0);
                    if (tiny && lengths[l] != 4096) {
                        CHECK(read_like_the_loader(&p, froms[f], len, seen) == 0);
                        for (u64 b = froms[f]; b < froms[f] + len; b++) CHECK(seen[b] == owner[b]);
                    }
                }
            }
        }
        for (int t = 0; t < 5; t++) {  // (reads from anywhere, and ones that aren't in the file at all)
            const u64 at = rnd() % p.size, len = rnd() % (p.size - at) + 1;
            CHECK(read_like_the_loader(&p, at, len, NULL) == 0);
            if (tiny) {
                CHECK(read_like_the_loader(&p, at, len, seen) == 0);
                for (u64 b = at; b < at + len; b++) CHECK(seen[b] == owner[b]);
            }
        }
        CHECK(read_like_the_loader(&p, p.size, 0, NULL) == 0);
        CHECK(read_like_the_loader(&p, p.size, 1, NULL) == -1);
        CHECK(read_like_the_loader(&p, p.size - 1, 2, NULL) == -1);
        CHECK(read_like_the_loader(&p, ~0ull, 1, NULL) == -1);
        CHECK(read_like_the_loader(&p, 0, ~0ull, NULL) == -1);
    }
}

static void whole_or_not(void) {
    const u64 one_to_three[] = {1, 2, 3}, zero_to_one[] = {0, 1}, a_gap[] = {1, 2, 5}, twice[] = {1, 1, 2}, from_two[] = {2, 3};
    const u64 wrapping[] = {1, 2, ~0ull, 0};  // (numbers that would follow one another only if they wrapped: they don't)
    const s64 same_but_the_last[] = {4095, 4095, 100}, shorter[] = {4095, 4000, 100}, empty[] = {4095, 4095, 0}, four[] = {4095, 4095, 4095, 100};
    const s64 gigabytes[] = {4095ll << 20, 4095ll << 20, 100};  // (parts of 4095 MB: their sum is past what 32 bits hold)
    CHECK(nx2_parts_whole(one_to_three, same_but_the_last, 3, 8290) == 0);
    CHECK(nx2_parts_whole(one_to_three, same_but_the_last, 3, 0) == 0);  // (the file's own size not known)
    CHECK(nx2_parts_whole(one_to_three, same_but_the_last, 3, 8291) == 3);
    CHECK(nx2_parts_whole(one_to_three, same_but_the_last, 3, 8185) == 3);
    CHECK(nx2_parts_whole(zero_to_one, same_but_the_last, 2, 8190) == 0);
    CHECK(nx2_parts_whole(one_to_three, same_but_the_last, 1, 4095) == 0);
    CHECK(nx2_parts_whole(a_gap, same_but_the_last, 3, 0) == 1);
    CHECK(nx2_parts_whole(twice, same_but_the_last, 3, 0) == 1);
    CHECK(nx2_parts_whole(from_two, same_but_the_last, 2, 0) == 1);
    CHECK(nx2_parts_whole(one_to_three, same_but_the_last, 0, 0) == 1);
    CHECK(nx2_parts_whole(wrapping, four, 4, 0) == 1);
    CHECK(nx2_parts_whole(one_to_three, shorter, 3, 0) == 2);
    CHECK(nx2_parts_whole(one_to_three, empty, 3, 0) == 2);
    CHECK(nx2_parts_whole(one_to_three, shorter, 3, 8290) == 2);  // (the numbers are seen to first, then the sizes)
    CHECK(nx2_parts_whole(one_to_three, gigabytes, 3, (4095ull << 20) * 2 + 100) == 0);
    CHECK(nx2_parts_whole(one_to_three, gigabytes, 3, 0xFFFFFFFFull) == 3);
}

static void the_most_there_can_be(void) {
    Nx2Parts p;
    nx2_parts_reset(&p);
    CHECK(nx2_parts_add(&p, 0) == -1);
    CHECK(nx2_parts_add(&p, -5) == -1);
    CHECK(p.count == 0 && p.size == 0);
    for (u32 k = 0; k < NX2_PARTS; k++) CHECK(nx2_parts_add(&p, 4095) == 0);
    CHECK(nx2_parts_add(&p, 1) == -1);
    CHECK(p.count == NX2_PARTS && p.size == (u64)NX2_PARTS * 4095);
    nx2_parts_reset(&p);
    CHECK(p.count == 0 && p.size == 0);
}

int main(void) {
    where_each_byte_is();
    whole_or_not();
    the_most_there_can_be();
    printf(failed ? "parts_test: %d checks failed\n" : "parts_test: every check passed\n", failed);
    return failed ? 1 : 0;
}
