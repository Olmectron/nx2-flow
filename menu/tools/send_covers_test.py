#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A sender of covers, on a computer: what the companion program does on the wire, in a few lines.

For trying the menu's receiver without a second console, and as a worked example of docs/layout.md,
"Covers by local wireless". It finds the receiver (the broadcast, or --address), asks which covers are
missing, hangs up, comes back and sends one small picture for each game it was told of: named by the
game's title id, a front and a wrap alternately. With --damage the last one goes with a wrong checksum,
which the receiver must refuse.

    python send_covers_test.py                      look for a receiver on this network
    python send_covers_test.py --address 127.0.0.1  one that is known
    python send_covers_test.py --no-ask             as toronja's sender: send without asking (two made-up files)
    python send_covers_test.py --same-connection    ask, and go on with the files on the same connection
"""
import argparse
import io
import socket
import struct
import sys
import time

PORT = 28301
HELLO, SEEK, HERE, ASK = b'TCOV1\0', b'TCOV?\0', b'TCOV!\0', b'TCOVQ\0'
FRONT, WRAP, END = 0, 1, 0xFF


def fnv1a(data):
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def picture(text, wrap):
    """A small JPEG with the text on it (Pillow), or a few bytes that say they are one when Pillow isn't here."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return b'\xff\xd8' + text.encode() * 200 + b'\xff\xd9'
    image = Image.new('RGB', (1036, 656) if wrap else (400, 650), (30 + sum(text.encode()) % 150, 80, 140))
    ImageDraw.Draw(image).text((20, 20), text, fill=(255, 255, 255))
    out = io.BytesIO()
    image.save(out, 'JPEG', quality=80)
    return out.getvalue()


def find(seconds):
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    udp.settimeout(0.5)
    for _ in range(seconds * 2):
        for to in ('255.255.255.255', '127.0.0.1'):
            try:
                udp.sendto(SEEK, (to, PORT))
            except OSError:
                pass
        try:
            answer, sender = udp.recvfrom(16)
            if answer[:6] == HERE:
                return sender[0]
        except (socket.timeout, OSError):
            pass
    return None


def read(sock, n):
    data = b''
    while len(data) < n:
        more = sock.recv(n - len(data))
        if not more:
            raise ConnectionError('the receiver hung up')
        data += more
    return data


def read_text(sock):
    return read(sock, read(sock, 1)[0]).decode('utf-8', 'replace')


def ask(sock):
    sock.sendall(ASK)
    count = struct.unpack('>I', read(sock, 4))[0]
    return [(read_text(sock), read_text(sock), read_text(sock)) for _ in range(count)]


def send(sock, files, damage):
    sock.sendall(HELLO + struct.pack('>I', len(files)))
    stored = 0
    for k, (kind, name, data) in enumerate(files):
        checksum = fnv1a(data) ^ (1 if damage and k == len(files) - 1 else 0)
        sock.sendall(bytes([kind, len(name)]) + name.encode() + struct.pack('>II', len(data), checksum) + data)
        answer = read(sock, 1)
        print(f'  {name} ({"wrap" if kind == WRAP else "front"}, {len(data)} bytes): {"stored" if answer == b"K" else "refused"}')
        stored += answer == b'K'
    sock.sendall(bytes([END, 0]))
    return stored


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--address')
    ap.add_argument('--no-ask', action='store_true')
    ap.add_argument('--same-connection', action='store_true')
    ap.add_argument('--damage', action='store_true')
    ap.add_argument('--wait', type=float, default=1.0, help='seconds away between asking and coming back')
    args = ap.parse_args()
    host = args.address or find(6)
    if not host:
        sys.exit('no receiver answered on this network')
    print(f'receiver at {host}')
    sock = socket.create_connection((host, PORT), timeout=15)
    if args.no_ask:
        files = [(FRONT, 'ZZ01A.jpg', picture('ZZ01A', False)), (WRAP, 'ZZ01A.jpg', picture('ZZ01A', True))]
    else:
        wanted = ask(sock)
        print(f'{len(wanted)} games without a cover there:')
        for title_id, code, title in wanted:
            print(f'  {title_id}  {code or "-":8}  {title}')
        files = [(WRAP if k % 2 else FRONT, title_id + '.jpg', picture(title, k % 2 == 1)) for k, (title_id, _, title) in enumerate(wanted)]
        if not args.same_connection:
            sock.close()
            time.sleep(args.wait)  # (where the companion goes to the internet for the pictures)
            sock = socket.create_connection((host, PORT), timeout=15)
    stored = send(sock, files, args.damage)
    sock.close()
    print(f'{stored} of {len(files)} stored')


if __name__ == '__main__':
    main()
