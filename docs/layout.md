# What is where, and how the programs talk

Two homebrew programs for a Switch 2, both in one folder of the card so that the homebrew menu shows one entry:

```
/switch/nx2-flow/nx2-flow.nro      the menu: lists the games, starts one, receives files from a computer
/switch/nx2-flow/nx2-game.nro      the game loader: runs the game of one NUP file. Started by the menu, not by hand
```

## On the card

```
/nx2-flow/menu.cfg                 the menu's settings, lines of key=value
/nx2-flow/games/                   the NUP files (the default; menu.cfg's "games" names another folder)
/nx2-flow/covers/<ID>.jpg|png      a game's front cover;  covers/full/<ID>.jpg|png  the whole wrap
/nx2-flow/covers/ids.txt           lines of <title id>=<GameTDB code>: which code a game's cover is filed under
/nx2-flow/cache/                   the menu's own: what it read of each NUP file (library.txt) and the games' icons
/nx2-flow/transfer.log             the menu's own: the figures of every large file received from a computer
/nx2-flow/<title id>/              one game's folder, named by its title id (16 hex digits, lower case):
    save/                            its saved data: the slot named "default"
    saves/<slot>/                    its saved data, other slots: one folder each, named by the user
    cache/<index>/                   its "cache storage", what a game keeps that it can make again: one folder for each
                                     index (four hex digits), apart from the saved data and the same for every slot
    log.txt  log.prev.txt  crash.txt the loader's log of the last run, of the one before, and a run that ended badly
    mods/                            files laid over the game's own (the default set)
    modsets/<name>/                  other sets of such files, one folder each
    game.cfg                         the game's settings, lines of key=value
    debug                            an empty file: the detailed log is on
    nvnnative                        an empty file: the game's calls to the graphics driver are not counted by the loader
                                     (the log then has no line of how fast the game runs). For a game that needs it only
```

`<ID>` is the title id, or the code GameTDB names the game's cover by.

## menu.cfg

| key | meaning |
|---|---|
| `games` | the folder the NUP files are in (`/nx2-flow/games` when absent) |
| `style` | how the games stand on the shelf, by name (`Showroom` when absent) |
| `covers` | `hidden`: every game shows its own icon; anything else: covers are shown |
| `cases` | `off`: covers are flat pictures; anything else: they stand as cases |
| `theme` | `light`, or dark |
| `transfer_kbps` | the most the menu's file receiver takes from the network, in KB a second; 0 or absent: no limit |
| `transfer_block_kb` | the receiver's one write to the card, in KB (64 to 4096; 1024 when absent) |
| `transfer_queue` | how many such blocks may wait for the card before the network is held back (2 to 64; 8 when absent) |
| `transfer_rcvbuf_kb` | the receiving socket's own buffer, in KB (64 to 2048; 256 when absent) |
| `split` | MB: a file the receiver is sent that is larger than this is written in parts of this size (`Name.001`, `Name.002`, ...). Absent: a file is written whole, and in parts of just under 4 GB only where the card can't take it whole |

The loader reads none of these: they are the menu's.

## game.cfg

Written by the menu, read by the loader. A key that is absent or empty means the default.

| key | meaning | default |
|---|---|---|
| `modset` | the folder under `modsets/` to lay over the game | `mods/` |
| `save` | the slot of saved data to use: the name of a folder under `saves/` | `save/` (the slot "default") |
| `memory` | MB the game is told of (2048 to 8992). **Advanced** | decided by the loader |
| `room` | MB the loader keeps for itself while it opens the game (128 to 2048); what the game turns out not to need of it is given back to the game. **Advanced** | 1024 |
| `modules` | `whole`: no module is merged with another; `merge`: all that may be are. **Advanced**, by hand: the menu has no row for it | merged only when the game needs the places |
| `dlc` | `off`: the add-on content the NUP file holds is not given to the game. By hand: the menu has no row for it | given |

The advanced ones are shown behind a warning: a wrong value keeps a game from starting. Removing the key puts it right again.
The detailed log is not a key: it is the file `debug` in the game's folder.

A slot's name, as the menu makes them: letters, digits, spaces, `-`, `_` and `.`, at most 32 characters, never `default` (that
is `save/`). A `save` that names a folder that isn't there is the loader's to make.

## Starting a game

The menu asks the homebrew loader to start `nx2-game.nro` next (`envSetNextLoad`) and exits. The arguments:

```
argv[0]   sdmc:/switch/nx2-flow/nx2-game.nro
argv[1]   the NUP file, as a path of the card: sdmc:/nx2-flow/games/Some Game.nup
argv[2..] menu=sdmc:/switch/nx2-flow/nx2-flow.nro      (optional: what to start again when the game is closed)
          room=<MB>                                    (optional: game.cfg's "room", which the loader must know before it can read files)
```

Everything else the loader needs it reads from the NUP file and from the game's folder.

## A NUP file in parts

For a card that takes no file of 4 GB (FAT32) a NUP file may be kept cut in parts, made with any tool that cuts a file in
pieces. `loader/runtime/nupparts.h` is the one place that says which names are one game's parts; the loader and the menu both
use it.

```
Game.nup.001  Game.nup.002           a number as the last ending (from 0 or 1; any number of digits up to four)
Game.nup.part1  Game.nup.part2       "part" and a number
Game.nup.aa  Game.nup.ab             letters, after ".nup" only
Game.nu0  Game.nu1                   the ending's last letter made a number
Game.part1.nup  Game.001.nup         the number before the ending
<folder>/00  <folder>/01             nameless parts: the folder is the game, and may hold other files beside them
```

- One game's parts share the name and the way they are numbered, follow one another from 0 or 1 with none missing, and are at
  least two (a folder of nameless parts may hold one). A name that looks like a part and has no such company is a file of its
  own if it ends `.nup`, and nothing otherwise.
- The parts are the whole file's bytes in order; they may be of any sizes.
- `argv[1]` for a game in parts is its first part (`sdmc:/nx2-flow/games/Game.nup.001`) or, for nameless parts, the folder.
  The loader finds the rest beside it.
- The menu looks into the folders of the games folder: a folder that holds one game's parts and no other game's file is that
  game; any other folder's files are listed as the games they are.
- A folder that the console itself takes for one file (the archive attribute set, as its own tools make them for a file of
  4 GB on such a card) is opened as that file: it needs none of this.

## Receiving files from a computer

The menu has a screen that does what Toronja Transfer does (toronja's `tools/transfer-nx`): it listens on TCP port 28302 and speaks
the same protocol ("TTX1": PING, LIST, PUT, GET, DELETE, HASH, MKDIR), so the same program on the computer sends to either. It
listens while that screen is open, and looks for games again when it is closed.

Nothing is different on the wire. What is different is how a file is received (`menu/source/transfer.h`): the network fills
blocks of one size (`transfer_block_kb`), a second thread writes each to the card with a single write, into a file given its whole
size first, and at most `transfer_queue` blocks wait between the two: when the card falls behind, the menu stops reading the
socket, whose buffer is small (`transfer_rcvbuf_kb`), so the sender is held back by the connection itself. `transfer_kbps` caps the
pace from the first byte. Every file of 8 MB or more leaves three lines in `/nx2-flow/transfer.log`: seconds, MB a second, the
longest time with nothing written, how full the queue got, how long the writes took, and where the time went.
