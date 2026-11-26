#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The menu's file receiver (source/transfer.cpp) with a NUP file too large for one file of the card, tried on a PC.

The receiver runs as it does on the console (preview/transfer_test), with a folder for a card, and this speaks to it over the
local network as the program that sends does (Toronja Transfer's protocol, "TTX1"): the file goes in parts, and is read back,
hashed and deleted as the one file it is. The receiver is told beforehand the most one file may hold, so that a small file is
taken in parts as a game's is on a FAT32 card.

    python tools/test_transfer_parts.py <transfer_test program> <a folder for a card>       (from menu/: make partstest)
"""
import os
import random
import shutil
import socket
import struct
import subprocess
import sys
import time
import zlib

MAGIC = b'TTX1'
PING, LIST, PUT, GET, DELETE, HASH, MKDIR = range(7)
PORT = 28302
PART = 1 << 20  # what the receiver is told: parts of 1 MB
MOST = PART + PART // 2  # ... and the most one file may hold: 1.5 MB
GAMES = 'nx2-flow/games'


class Sender:
    """What the program that sends does, over one connection."""

    def __init__(self):
        self.sock = socket.create_connection(('127.0.0.1', PORT), timeout=30)

    def close(self):
        self.sock.close()

    def took(self, n):
        data = b''
        while len(data) < n:
            got = self.sock.recv(n - len(data))
            if not got:
                raise ConnectionError('the receiver hung up')
            data += got
        return data

    def ask(self, op, path=''):
        path = path.encode('utf-8')
        self.sock.sendall(MAGIC + bytes([op]) + struct.pack('<H', len(path)) + path)

    def status(self):
        """0, or the receiver's reason as text."""
        kind = self.took(1)[0]
        if kind == 0:
            return 0, ''
        return kind, self.took(struct.unpack('<H', self.took(2))[0]).decode('utf-8', 'replace')

    def put(self, path, data, until=None):
        """Sends a file: (status, reason, the sum the receiver made of it). With `until`, the connection is closed after that
        many bytes, as a sender that was stopped would close it."""
        self.ask(PUT, path)
        self.sock.sendall(struct.pack('<Q', len(data)))  # (the receiver reads the least significant byte first)
        kind, text = self.status()
        if kind:
            return kind, text, None
        self.sock.sendall(data if until is None else data[:until])
        if until is not None:
            self.close()
            return 0, '', None
        kind, text = self.status()
        return kind, text, None if kind else struct.unpack('<I', self.took(4))[0]

    def got(self, op, path):
        """HASH (the sum of a file) or GET (its bytes): (status, reason, bytes, the sum)."""
        self.ask(op, path)
        kind, text = self.status()
        if kind:
            return kind, text, None, None
        size = struct.unpack('<Q', self.took(8))[0]
        data = self.took(size) if op == GET else None
        return 0, '', data, struct.unpack('<I', self.took(4))[0]

    def delete(self, path):
        self.ask(DELETE, path)
        return self.status()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    program, card = sys.argv[1], sys.argv[2]
    if os.path.isdir(card):
        shutil.rmtree(card)
    os.makedirs(os.path.join(card, GAMES))
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    def there(name):
        return os.path.join(card, GAMES, name)

    def files():
        return sorted(os.listdir(os.path.join(card, GAMES)))

    def of(name):
        """The bytes on the card of the file of that name, in parts or one."""
        data = b''
        for found in files():
            if found == name or (found.startswith(name + '.') and found[len(name) + 1:].isdigit()):
                with open(there(found), 'rb') as f:
                    data += f.read()
        return data

    rng = random.Random(7)

    def container(size):
        """A NUP file's first bytes, as far as the receiver and the menu look into them: what it is, and how long the whole
        file is. The rest is anything."""
        data = bytearray(rng.randbytes(size))
        head = b'NUP' + bytes([1]) + bytes(4) + struct.pack('<QQ', 0x80, 0) + bytes(32) + struct.pack('<QQI', size, size, 1)
        data[:len(head)] = head
        return bytes(data)

    with open(os.path.join(card, 'receiver.log'), 'w') as log:
        # (a block of 64 KB, a queue of 4, no cap, a socket buffer of 256 KB: parts of 1 MB, and 1.5 MB the most one file)
        receiver = subprocess.Popen([program, card, '10', '64', '4', '0', '256', str(PART >> 20), str(MOST)], stdout=log, stderr=subprocess.STDOUT)
    said = ''
    for _ in range(150):  # (until it listens: said in its output)
        time.sleep(0.1)
        with open(os.path.join(card, 'receiver.log')) as f:
            said = f.read()
        if 'Listening on port' in said:
            break
        if receiver.poll() is not None:
            break
    check('Listening on port' in said, 'the receiver listens (it says: %s)' % said.strip()[-300:])

    if 'Listening on port' in said:
        # A NUP file too large for one file: it goes in parts, each as long as the others but the last.
        game = container(2 * PART + 12345)  # (a third part, 12345 bytes long, after two of 1 MB)
        name = 'Parts Game [0400B00000000001].nup'
        sender = Sender()
        kind, text, crc = sender.put('/' + GAMES + '/' + name, game)
        check(kind == 0 and crc == zlib.crc32(game), 'a NUP file too large for one file is taken, in parts (%s %s)' % (kind, text))
        check(files() == [name + '.001', name + '.002', name + '.003'], 'it is on the card in three parts, numbered from 1, with nothing left over (%s)' % files())
        check([os.path.getsize(there(name + s)) for s in ('.001', '.002', '.003')] == [PART, PART, len(game) - 2 * PART], 'the first two of one size, the last shorter')
        check(of(name) == game, 'and the parts in order are the file, byte for byte')
        # ... and it is the one file it is, to whoever asks for it by the file's name.
        kind, text, data, crc = sender.got(HASH, '/' + GAMES + '/' + name)
        check(kind == 0 and crc == zlib.crc32(game), 'hashed as one file (%s %s)' % (kind, text))
        kind, text, data, crc = sender.got(GET, '/' + GAMES + '/' + name)
        check(kind == 0 and data == game and crc == zlib.crc32(game), 'sent back as one file (%s %s)' % (kind, text))

        # A NUP file that fits in one file still goes in one.
        small = container(MOST - 1000)
        kind, text, crc = sender.put('/' + GAMES + '/Small Game [0400B00000000002].nup', small)
        check(kind == 0 and crc == zlib.crc32(small) and of('Small Game [0400B00000000002].nup') == small, 'a NUP file that fits in one file goes in one')

        # Anything else too large for one file isn't taken in parts: the game loader reads none so.
        kind, text, crc = sender.put('/nx2-flow/other/big.bin', container(2 * PART))
        check(kind != 0 and 'reserve' in text, "what isn't a NUP file, too large for one file, is refused as before (%s %s)" % (kind, text))
        sender.close()
        check(not os.path.exists(os.path.join(card, 'nx2-flow', 'other', 'big.bin.ttpart')), 'and nothing of it is left on the card')

        # A sender stopped halfway: nothing of the file stays.
        sender = Sender()
        sender.put('/' + GAMES + '/Interrupted Game [0400B00000000003].nup', container(2 * PART + 100), until=PART // 2)
        time.sleep(1.0)
        check([f for f in files() if f.startswith('Interrupted')] == [], "a sender stopped halfway leaves nothing on the card (%s)" % files())

        # What was there of the file from before goes: the file as one, parts of another copy, a part still being received.
        stale = 'Stale Game [0400B00000000004].nup'
        with open(there(stale), 'wb') as f:
            f.write(b'an old copy, in one file')
        with open(there(stale + '.005'), 'wb') as f:
            f.write(b'a part of a larger copy')
        with open(there(stale + '.003.ttpart'), 'wb') as f:
            f.write(b'never finished')
        new = container(2 * PART - 5)
        sender = Sender()
        kind, text, crc = sender.put('/' + GAMES + '/' + stale, new)
        check(kind == 0 and of(stale) == new, 'a file in parts replaces what was there of it (%s %s)' % (kind, text))
        check(not os.path.exists(there(stale)) and not os.path.exists(there(stale + '.005')) and not os.path.exists(there(stale + '.003.ttpart')),
              'the file as one, the parts of another copy and the part never finished are gone (%s)' % files())

        # A set that isn't whole on the card: not hashed as a file.
        half = container(2 * PART)
        with open(there('Half Game [0400B00000000005].nup.001'), 'wb') as f:
            f.write(half[:PART])
        kind, text, data, crc = sender.got(HASH, '/' + GAMES + '/Half Game [0400B00000000005].nup')
        check(kind != 0, "a set that isn't whole isn't hashed as a file (%s)" % text)
        kind, text, data, crc = sender.got(HASH, '/' + GAMES + '/No Such Game.nup')
        check(kind != 0, 'nor what is not there at all')

        # Deleted by the file's name: its parts go.
        kind, text = sender.delete('/' + GAMES + '/' + name)
        check(kind == 0 and not os.path.exists(there(name + '.001')), 'deleted by the file\'s name: its parts go (%s %s)' % (kind, text))
        kind, text = sender.delete('/' + GAMES + '/' + name)
        check(kind != 0, 'and once gone, it is said not to be there')
        sender.close()

    try:
        receiver.wait(timeout=40)
    except subprocess.TimeoutExpired:
        receiver.kill()
        check(False, 'the receiver ends when its time is up')
    with open(os.path.join(card, 'receiver.log')) as f:
        said = f.read()
    for what in failed:
        print('failed:', what)
    if failed:
        print('the receiver said:')
        for line in said.splitlines():
            print('   ', line)
    print('test_transfer_parts: %s' % ('%d checks failed' % len(failed) if failed else 'every check passed'))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
