# nx2-flow covers: the companion

`nx2-flow-covers.nro`: a small homebrew program for a **Switch** (Atmosphère) that is on the internet. It does two things and
nothing else:

1. it **downloads cover art from GameTDB** to its own card, and
2. it **hands the covers to nx2-flow's menu** on another console, over the network both consoles are on or, when they share
   none, over a direct wireless link between the two (local wireless, LDN).

It is for a console that is kept off the internet and so cannot fetch its covers itself.

> **Experimental.** It builds; it has not been run on a console yet (see "Not tried yet" below). Use it at your own risk.

## What key a cover is found by

By **the code GameTDB files the game under**, and by nothing else: five letters and digits, the last part of a game's page on
the site (`www.gametdb.com/Switch/<code>`). The pictures are at

```
https://art.gametdb.com/switch/coverfull/<region>/<code>.jpg     the whole wrap
https://art.gametdb.com/switch/cover/<region>/<code>.jpg         the front
```

with `<region>` one of `US`, `EN`, `JA`, `KO`, tried in the order the code's last letter makes likely (`J`: JA, US, EN;
`K`: KO, US, EN; `P X Y Z D F S I`: EN, US, JA; any other: US, EN, JA).

What this means in practice:

- **A title id is not that code**, and this program has no table that turns one into the other: it does not guess. You look
  the game up on GameTDB and write its code down (see `ids.txt` below).
- **GameTDB has no section for the Switch 2** (looked for on the site on 2026-11-25: its sections are Wii, Wii U, PS3, DS,
  3DS and Switch). A game has a cover there only if it is filed in the Switch section.
- A code GameTDB has no picture for is reported on the screen and left alone.

## On this console's card

```
/nx2-flow-covers/ids.txt          which covers to download (you write it)
/nx2-flow-covers/<code>.jpg       a front cover
/nx2-flow-covers/full/<code>.jpg  a whole wrap
```

`ids.txt` is plain text, one cover on each line; `#` starts a comment:

```
# the code alone: the cover is kept, and sent, as <code>.jpg
ABCDE
# a title id and the code: the cover is sent as <title id>.jpg
0100000000000000=ABCDE
```

The second form is what nx2-flow's own `covers/ids.txt` holds, so that file can be copied here as it is. With it the cover
arrives on the other console named by the title id, which nx2-flow finds without any list of codes of its own. A line that is
a title id alone finds nothing (there is no code to ask GameTDB for) and is counted on the screen.

Pictures you put in these folders yourself (`.jpg`, `.jpeg` or `.png`, named with letters and digits only) are sent too, under
the name they have.

## Using it

Copy `nx2-flow-covers.nro` to `/switch/` on the card of the Switch.

**Start it from the Album**: open the Album to get the Homebrew Menu, and start it there. That is applet mode, and it is the
mode this program is made for: a text screen and very little memory. Started over a game instead (holding R while the game
starts) it works the same for downloading, but the system may refuse the direct wireless link (the screen says so, with the
answer `0xc2cb`).

- When it starts, it downloads every cover `ids.txt` names that the card has none of yet, if the console is connected to the
  internet. A code that has a front or a wrap already is not asked about again: delete the file to fetch it anew.
- **A** does that again (after you connect, or change `ids.txt`).
- **X** sends the covers. First open nx2-flow on the other console and choose to receive covers there; then press X here. This
  console looks for the other one on its network for a few seconds and then nearby for about a minute. Covers still missing
  are downloaded first, because the direct wireless link takes the console off its network for as long as it lasts; the
  console goes back to its network by itself afterwards.
- **B** stops what is running (hold it for a moment), or exits when nothing is. **+** exits.

The screen shows how many fronts and wraps are on the card, how many codes of `ids.txt` still have no cover, whether the
console is on the internet, the address of the console it is sending to, and a log of what was done.

## How it talks to nx2-flow

With toronja's cover share (toronja's `src/citra_switch/cover_share.cpp`), **unchanged on the wire**; this program is its
sending half. `source/cover_send.cpp` has the details.

| | |
|---|---|
| finding the receiver on a shared network | UDP port 28301: the sender broadcasts `TCOV?`, the receiver answers `TCOV!` (six bytes each: the word and a zero) |
| direct wireless (LDN) | the receiver opens a network, the sender scans for scene id `0x4356` (local communication id -1) and joins it with toronja's 16-byte passphrase, security mode "product", user name `ToronjaCovers`; the receiver is the network's first node |
| the stream | TCP port 28301: `TCOV1` and a zero, the number of files (32 bits, big-endian), then the files, then the two bytes `FF 00` |
| a file | one byte of kind (0 front, 1 whole wrap), one byte of name length, the name, the size and an FNV-1a checksum of the bytes (32 bits each, big-endian), the bytes; the receiver answers one byte, `K` when it stored the file |
| limits | a name of at most 40 characters, letters and digits and then `.jpg`, `.jpeg` or `.png`; a file of at most 6 MB |

The sender offers every file it has and the receiver stores the well-formed ones. There is no message by which the receiver
asks for particular covers: which covers there are to send is decided on this console, by `ids.txt` and by what is in the
folders.

## Building

Needs devkitPro with devkitA64 and libnx (`switch-dev`); nothing else: no curl, no other portlibs. Downloads go over a plain
socket and the console's own TLS service, the screen is libnx's text console.

```bash
make            # out/nx2-flow-covers.nro
```

## Not tried yet

The program compiles and links without warnings; none of the following has been seen working on a console:

- the download itself: the console's TLS service accepting GameTDB's certificate (when it does not, the program asks for the
  same picture over plain http and says so), and the plain connection;
- joining the other console's wireless network from applet mode, and the transfer to nx2-flow's menu;
- how the screen and the buttons behave, and that the memory of applet mode is enough.

Checked from a computer only: GameTDB's Switch list shows five-character codes, and the art server answers a request like
this program's with the picture and its length, or with "not found".

## License

GPL-2.0-or-later, like toronja, which the sending and downloading code is adapted from (see `LICENSE` and the headers of the
files in `source/`). `icon.jpg` is libnx's default icon for homebrew.
