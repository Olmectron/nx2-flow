#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A NUP file kept in parts on a card (a FAT32 card holds no file of 4 GiB or more), checked against the file it was copied
from. The parts are "<name>.nup.001", ".002", ..., each as long as the others but the last (docs/layout.md).

  python tools/nup_parts.py check  <the game on the card>
  python tools/nup_parts.py verify <the file it was copied from.nup>  <the game on the card>

<the game on the card> is its name as one file, "F:/nx2-flow/games/Game.nup" (the parts' name without the number), or any
one of its parts, "F:/nx2-flow/games/Game.nup.003".

check looks at the card alone: whether the parts make one whole file, by the rules the menu and the game loader judge them
by, and what game it is. verify reads the original and the parts in turn and says whether they are the same bytes: a few
minutes for each, on a card reader. A set that isn't whole is compared as far as it goes.

Exit: 0 all is well; 1 the parts aren't whole, or aren't the original's bytes; 2 a path or the file system is at fault.
"""
import hashlib
import os
import struct
import sys
import time

MB = 1 << 20
CHUNK = 16 * MB
MOST_PARTS = 64  # the most parts the game loader reads a NUP file in
FACT_NPDM, FACT_NACP, FACT_VERSION, FACT_TITLE = 1, 2, 4, 5  # a package's facts, by their number


def how_big(n):
    if n >= 1 << 30:
        return '%.2f GB' % (n / (1 << 30))
    if n >= MB:
        return '%.1f MB' % (n / MB)
    return '%.1f KB' % (n / 1024)


def text(data):
    return data.split(b'\0')[0].decode('utf-8', 'replace') if data else ''


class Kept:
    """A NUP file as the card keeps it: one file, or its parts in the order of their numbers."""

    def __init__(self, path):
        folder, name = os.path.split(os.path.abspath(path))
        base = name
        stem, dot, tail = name.rpartition('.')
        if dot and stem.lower().endswith('.nup') and tail.isascii() and tail.isdigit():
            base = stem  # (given one of its parts: the file's name is that without the number)
        self.file = os.path.join(folder, base)
        self.numbers, self.digits, self.parts = [], [], []
        if os.path.isfile(self.file):
            self.parts = [self.file]
            return
        found = []
        for entry in os.listdir(folder):
            tail = entry[len(base) + 1:]
            if entry[:len(base)].lower() == base.lower() and entry[len(base):len(base) + 1] == '.' and tail.isascii() and tail.isdigit() and len(tail) <= 18:
                found.append((int(tail), tail, os.path.join(folder, entry)))
        found.sort()
        for number, digits, part in found:
            self.numbers.append(number), self.digits.append(digits), self.parts.append(part)

    @property
    def one_file(self):
        return self.parts == [self.file]

    def sizes(self):
        return [os.path.getsize(part) for part in self.parts]

    def judged(self):
        """Whether the parts make one whole file, by the menu's and the game loader's own rules: (so or not, why not, how
        many bytes the file says it has)."""
        sizes, total = self.sizes(), sum(self.sizes())
        if not sizes:
            return False, 'there is no file of that name on the card, and no part of one', None
        if self.one_file:
            return True, None, None
        if len(sizes) > MOST_PARTS:
            return False, 'it is in %d parts, and at most %d are read' % (len(sizes), MOST_PARTS), None
        if self.numbers[0] > 1:
            return False, 'its first part is %s: the ones before it are missing' % self.digits[0], None
        for k in range(len(sizes) - 1):
            if self.numbers[k + 1] != self.numbers[k] + 1:
                return False, 'a part is missing, or two have one number (its parts are %s)' % ', '.join(self.digits[:9]), None
        for k in range(len(sizes) - 1):
            if sizes[k] != sizes[0]:
                return False, 'its parts are not of one size: %s holds %s and %s holds %s' % (
                    self.digits[0], how_big(sizes[0]), self.digits[k], how_big(sizes[k])), None
        if sizes[-1] == 0:
            return False, 'its last part, %s, is empty' % self.digits[-1], None
        head = self.read(0, 0x80)
        says = struct.unpack_from('<Q', head, 0x40)[0] if head and head[:4] == b'NUP\x01' else None
        if says and says != total:
            if says > total:
                return False, 'its parts hold %s of its %s: a copy that was cut short' % (how_big(total), how_big(says)), says
            return False, 'its parts hold %s, and by its own first bytes it is %s: a part is left over from another copy' % (
                how_big(total), how_big(says)), says
        return True, None, says

    def read(self, at, n):
        """n bytes from `at` in the file, from the part or parts that hold them: None when they can't be had."""
        out, begins = [], 0
        for part, size in zip(self.parts, self.sizes()):
            if at >= begins + size:
                begins += size
                continue
            take = min(n, begins + size - at)
            with open(part, 'rb') as f:
                f.seek(at - begins)
                block = f.read(take)
            if len(block) != take:
                return None
            out.append(block)
            at, n = at + take, n - take
            begins += size
            if not n:
                return b''.join(out)
        return None

    def says(self):
        """What the file says of itself, as the menu reads it: (the game's title id, its version, its display version, its
        title, how many updates, how many added contents). None when it can't be read."""
        head = self.read(0, 0x80)
        if not head or head[:4] != b'NUP\x01':
            return None
        toc_at = struct.unpack_from('<Q', head, 8)[0]
        count = struct.unpack_from('<I', head, 0x48)[0]
        toc = self.read(toc_at, 0x30 + 0x40 * count) if 0 < count <= 64 else None
        if not toc or toc[:4] != b'TOC\x01':
            return None
        game = update = None
        updates = addons = 0
        for k in range(count):
            title, _, _, _, version, kind = struct.unpack_from('<QIIIIB', toc, 0x30 + 0x40 * k)
            meta_at, meta_size = struct.unpack_from('<QI', toc, 0x30 + 0x40 * k + 0x20)
            facts = {}
            if 0 < meta_size <= 8 * MB:
                block = self.read(meta_at, meta_size)
                at = 0
                while block and at + 8 <= len(block):
                    number, _, size = struct.unpack_from('<HHI', block, at)
                    if at + 8 + size > len(block):
                        break
                    facts.setdefault(number, block[at + 8:at + 8 + size])
                    at += 8 + size
            if kind == 0 and game is None:
                game = title, version, facts
            elif kind == 1:
                updates += 1
                if update is None or version > update[1]:
                    update = title, version, facts
            elif kind == 2:
                addons += 1
        if game is None and update is None:
            return None
        newest = update or game
        facts = [newest[2], game[2]]  # (the newest's first, then the game's: the first that has each thing)
        nacp = next((f[FACT_NACP] for f in facts if FACT_NACP in f and len(f[FACT_NACP]) >= 0x3070), None)
        title = next((text(f[FACT_TITLE]) for f in facts if FACT_TITLE in f), '') or (text(nacp[:0x200]) if nacp else '')
        shown = next((text(f[FACT_VERSION]) for f in facts if FACT_VERSION in f), '') or (text(nacp[0x3060:0x3070]) if nacp else '')
        return (game[0] if game else update[0] & ~0xFFF), newest[1], shown, title, updates, addons


def shown(kept):
    """Says what is on the card, and whether it is whole."""
    sizes = kept.sizes()
    if kept.one_file:
        print('%s: one file, %s' % (kept.file, how_big(sizes[0])))
    elif sizes:
        print('%s: in %d parts' % (kept.file, len(sizes)))
        for digits, size in sorted(zip(kept.digits, sizes)):
            print('  .%s  %10s' % (digits, how_big(size)))
        print('  %s in all' % how_big(sum(sizes)))
    else:
        print(kept.file)
    said = kept.says()
    if said:
        title_id, version, display, title, updates, addons = said
        what = 'the game %016x, version %s' % (title_id, display or '%d.%d.%d' % (version >> 16, version >> 8 & 255, version & 255))
        more = ['%d %s' % (n, word if n > 1 else word[:-1] if word.endswith('s') else word)
                for n, word in ((updates, 'updates'), (addons, 'added contents')) if n]
        print('  %s%s%s' % (what, ' (%s)' % ', '.join(more) if more else '', ': ' + title if title else ''))
    whole, why, _ = kept.judged()
    if whole:
        print('  whole: the menu shows it as a game, and the game loader reads it')
    else:
        print("  NOT WHOLE: %s. The menu shows it, and says it can't be started." % why)
    return whole


class Stream:
    """Several files' bytes in order, read a piece at a time, as one file's: a piece may lie in two of them."""

    def __init__(self, paths):
        self.left = list(paths)
        self.file = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def read(self, n):
        """Up to n bytes: fewer only at the end of them all."""
        out = bytearray()
        while len(out) < n:
            if self.file is None:
                if not self.left:
                    break
                self.file = open(self.left.pop(0), 'rb')
            block = self.file.read(n - len(out))
            if not block: # (the end of this one: the next)
                self.close()
                continue
            out += block
        return bytes(out)

    def close(self):
        if self.file:
            self.file.close()
            self.file = None


def digested(paths, limit, label):
    """The SHA-256 of the files' first `limit` bytes (all: None), read a piece at a time, with how far it has got said."""
    total = limit if limit is not None else sum(os.path.getsize(path) for path in paths)
    digest = hashlib.sha256()
    done, began, last = 0, time.time(), 0.0
    with Stream(paths) as stream:
        while True:
            block = stream.read(CHUNK if limit is None else min(CHUNK, limit - done))
            if not block:
                break
            digest.update(block)
            done += len(block)
            if time.time() - last > 0.25:
                last = went = time.time() - began
                print('\r  %s: %5.1f%% of %s, %s a second' % (label, 100.0 * done / max(total, 1), how_big(total),
                                                             how_big(done / went) if went >= 0.5 else '...'), end='', flush=True)
    went = time.time() - began
    print('\r  %s: %s read in %d:%02d, %s a second        ' % (label, how_big(done), int(went) // 60, int(went) % 60, how_big(done / went) if went else ''))
    return digest.hexdigest()


def first_difference(original, kept):
    """The first byte at which the original and what is on the card are not the same: a second reading of both."""
    at = 0
    with open(original, 'rb') as f, Stream(kept.parts) as card:
        while True:
            a, b = f.read(CHUNK), card.read(CHUNK)
            if a != b:
                for k in range(min(len(a), len(b))):
                    if a[k] != b[k]:
                        return at + k
                return at + min(len(a), len(b)) # (the same as far as they both go: one ends here)
            if not a:
                return None
            at += len(a)


def compared(original, kept):
    """Whether the original and what is on the card are the same bytes. 0: they are."""
    theirs, ours = os.path.getsize(original), sum(kept.sizes())
    if theirs == ours:
        print('reading the original: %s' % original)
        one = digested([original], None, 'the original')
        print('reading the card')
        other = digested(kept.parts, None, 'the card')
        if one == other:
            print('THE SAME: every byte of the original is on the card (SHA-256 %s for both).' % one)
            return 0
        print('NOT THE SAME: as long as the original, but other bytes.')
    else:
        print("NOT THE SAME LENGTH: the original is %s, and the card holds %s." % (how_big(theirs), how_big(ours)))
        far = min(theirs, ours)
        print('comparing the first %s of each' % how_big(far))
        one = digested([original], far, 'the original')
        other = digested(kept.parts, far, 'the card')
        if one == other:
            print("THE SAME AS FAR AS THEY GO: what is on the card is the original's first %s, byte for byte%s." % (
                how_big(far), '' if ours < theirs else ', with more after it'))
            return 1
        print('NOT THE SAME.')
    at = first_difference(original, kept)
    if at is None:
        return 1
    begins = 0
    for digits, part, size in zip(kept.digits, kept.parts, kept.sizes()):
        if at < begins + size:
            print('  they first differ %s into part .%s (byte %d of the file).' % (how_big(at - begins), digits, at))
            break
        begins += size
    else:
        print('  they first differ at byte %d of the file.' % at)
    return 1


def main(argv):
    if len(argv) < 3 or argv[1] not in ('verify', 'check') or len(argv) != (4 if argv[1] == 'verify' else 3):
        print(__doc__)
        return 2
    try:
        kept = Kept(argv[3] if argv[1] == 'verify' else argv[2])
        whole = shown(kept)
        if not whole and argv[1] == 'check':
            return 1
        return compared(argv[2], kept) if argv[1] == 'verify' else 0
    except OSError as error:
        print('nup_parts: %s' % error)
        return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
