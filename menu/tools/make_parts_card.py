#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Makes a pretend card of games whose NUP files are kept in parts, for preview/parts_test.cpp: the same tiny container whole
and cut in every way the menu is to take for one game, and beside them the files it is to leave out. Nothing of a real game is
in it.

    python tools/make_parts_card.py <folder>          (needs Pillow)

The cuts are where they hurt: inside the superblock, inside the table of contents, inside the icon. Written beside the card's
folders, for the test to check the library against:
    expect.txt       a line "here <n>": the games in the games folder itself; then a line for each game the library is to list:
                     its path, how many parts, its title id (empty: the file can't be read), its title, its size
    icons/<id>.jpg   the icon each container has
"""
import os
import sys

from make_fake_card import FACT_ICON, FACT_NPDM, FACT_PUBLISHER, FACT_TITLE, FACT_VERSION, jpeg, nup, picture

GAMES = '/nx2-flow/games'


def container(number, title):
    """A container of a game and one update, with a title or without. Its title id, its icon, its bytes."""
    title_id = 0x0400F00000201000 + (number << 16)
    icon = jpeg(picture(100 + number, (256, 256)))
    facts = {FACT_NPDM: b'META' + b'\0' * 0x7C, FACT_ICON: icon, FACT_VERSION: b'1.0.0', FACT_PUBLISHER: b'Nobody at All'}
    if title:
        facts[FACT_TITLE] = title.encode('utf-8')
    return title_id, icon, nup([(title_id, 0, 0, facts), (title_id | 0x800, 1 << 16, 1, {FACT_VERSION: b'1.0.1'})])


def cut(data, places):
    """The bytes in pieces, cut at the places."""
    edges = [0] + sorted(places) + [len(data)]
    return [data[a:b] for a, b in zip(edges, edges[1:])]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    card = sys.argv[1]
    expect = []

    def put(name, data):
        path = os.path.join(card, *(GAMES + '/' + name).split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(data)

    def listed(path, parts, title_id, title, size):
        expect.append(f'{GAMES}/{path}\t{parts}\t{title_id:016x}\t{title}\t{size}' if title_id else f'{GAMES}/{path}\t{parts}\t\t{title}\t{size}')

    def icon_of(title_id, icon):
        os.makedirs(os.path.join(card, 'icons'), exist_ok=True)
        open(os.path.join(card, 'icons', f'{title_id:016x}.jpg'), 'wb').write(icon)

    os.makedirs(os.path.join(card, 'nx2-flow'), exist_ok=True)
    title = 'Game in Parts'
    title_id, icon, whole = container(0, title)
    icon_of(title_id, icon)
    toc, icon_at = 0x80, whole.index(icon)
    rows = toc + 0x30  # (the packages, 0x40 bytes each; where a package's facts are is at 0x20 of its row)
    assert toc < rows + 0x64 < icon_at < icon_at + len(icon) <= len(whole)

    # The game whole, and the same bytes cut.
    put('Whole.nup', whole)
    listed('Whole.nup', 1, title_id, title, len(whole))
    for k, piece in enumerate(cut(whole, [rows + 0x10, icon_at + len(icon) // 2])):  # (the table of contents, and the icon)
        put(f'X.nup.{k + 1:03d}', piece)
    listed('X.nup.001', 3, title_id, title, len(whole))
    for k, piece in enumerate(cut(whole, [rows + 0x64])):  # (through the number that says where the update's facts are)
        put(f'Y.nup.a{"ab"[k]}', piece)
    listed('Y.nup.aa', 2, title_id, title, len(whole))
    for k, piece in enumerate(cut(whole, [icon_at + 100])):
        put(f'Z/{k:02d}', piece)
    listed('Z', 2, title_id, title, len(whole))
    put('W.nup/00', whole)
    put('W.nup/note.txt', b'a folder of nameless parts may hold other things\n')
    listed('W.nup', 1, title_id, title, len(whole))
    for k, piece in enumerate(cut(whole, [0x40, toc + 0x04, rows + 0x20, icon_at + len(icon) // 3])):  # (the superblock too)
        put(f'P.nup.part{k + 1}', piece)
    listed('P.nup.part1', 5, title_id, title, len(whole))
    for k, piece in enumerate(cut(whole, [icon_at + 1])):  # (named parts with a folder to themselves: listed by the first)
        put(f'Solo/S.nup.{k + 1:03d}', piece)
    listed('Solo/S.nup.001', 2, title_id, title, len(whole))

    # A folder that holds two whole games is not a game: each of its files is.
    for number, name in ((1, 'First'), (2, 'Second')):
        two_id, two_icon, two = container(number, f'{name} of Two')
        icon_of(two_id, two_icon)
        put(f'Two/{name}.nup', two)
        listed(f'Two/{name}.nup', 1, two_id, f'{name} of Two', len(two))

    # Containers that give no title: the name comes from the file's, the parts' or the folder's.
    plain_id, plain_icon, plain = container(3, None)
    icon_of(plain_id, plain_icon)
    put('Some.Name.8.nup', plain)  # (a whole game: its name looks like a part's, and no other part is there)
    listed('Some.Name.8.nup', 1, plain_id, 'Some.Name.8', len(plain))
    for k, piece in enumerate(cut(plain, [rows + 0x30])):
        put(f'Plain.nup.{k + 1:03d}', piece)
    listed('Plain.nup.001', 2, plain_id, 'Plain', len(plain))
    for k, piece in enumerate(cut(plain, [toc + 0x10, icon_at + 7])):
        put(f'Bare Folder.nup/{k}', piece)
    listed('Bare Folder.nup', 3, plain_id, 'Bare Folder', len(plain))

    # As deep as folders are looked into, and one deeper.
    deep_id, deep_icon, deep = container(4, 'Deep Down')
    icon_of(deep_id, deep_icon)
    put('Deep/A/B/In.nup', deep)
    listed('Deep/A/B/In.nup', 1, deep_id, 'Deep Down', len(deep))
    put('Deep/A/B/C/Far.nup', deep)

    # Called a container's parts and not one: listed, as a file that can't be started.
    put('Broken.nup.001', b'this is not a NUP container' * 8)
    put('Broken.nup.002', b'nor is this' * 8)
    listed('Broken.nup.001', 2, 0, 'Broken', 27 * 8 + 11 * 8)

    # Not listed: a later part with no first, files that look like parts of something and aren't a container's, what is hidden.
    put('Q.nup.002', whole)
    put('notes.001', b'an archive in volumes, or anything else\n' * 4)
    put('notes.002', b'and the rest of it\n' * 4)
    put('junk/0', b'some program\'s own data\n' * 4)
    put('junk/1', b'and more of it\n' * 4)
    put('.hidden/H.nup', whole)
    put('readme.txt', b'not a game\n')

    here = sum(1 for line in expect if line.split('\t')[0].count('/') == 3 or line.split('\t')[0].endswith('/Solo/S.nup.001'))
    open(os.path.join(card, 'expect.txt'), 'w', newline='\n').write(f'here\t{here}\n' + '\n'.join(expect) + '\n')
    print(f'{len(expect)} games to be listed, {here} of them in the games folder itself')


if __name__ == '__main__':
    main()
