# What is where, and how the programs talk

Two homebrew programs for a Switch 2, both in one folder of the card so that the homebrew menu shows one entry, and a third for a
Switch:

```
/switch/nx2-flow/nx2-flow.nro      the menu: lists the games, starts one, receives files from a computer
/switch/nx2-flow/nx2-game.nro      the game loader: runs the game of one NUP file. Started by the menu, not by hand
(on a Switch) nx2-flow-covers.nro  the companion: fetches covers from the internet and hands them over by local wireless
```

## On the card

```
/nx2-flow/menu.cfg                 the menu's settings, lines of key=value
/nx2-flow/games/                   the NUP files (the default; menu.cfg's "games" names another folder)
/nx2-flow/covers/<ID>.jpg|png      a game's front cover;  covers/full/<ID>.jpg|png  the whole wrap
/nx2-flow/covers/ids.txt           lines of <title id>=<GameTDB code>: which code a game's cover is filed under
/nx2-flow/cache/                   the menu's own: what it read of each NUP file (library.txt) and the games' icons
/nx2-flow/<title id>/              one game's folder, named by its title id (16 hex digits, lower case):
    save/                            its saved data: the slot named "default"
    saves/<slot>/                    its saved data, other slots: one folder each, named by the user
    log.txt  log.prev.txt  crash.txt the loader's log of the last run, of the one before, and a run that ended badly
    mods/                            files laid over the game's own (the default set)
    modsets/<name>/                  other sets of such files, one folder each
    game.cfg                         the game's settings, lines of key=value
    debug                            an empty file: the detailed log is on
```

`<ID>` is the title id, or the code GameTDB names the game's cover by.

## menu.cfg

| key | meaning |
|---|---|
| `games` | the folder the NUP files are in (`/nx2-flow/games` when absent) |
| `style` | how the games stand on the shelf, by name (`Coverflow` when absent) |
| `covers` | `hidden`: every game shows its own icon; anything else: covers are shown |
| `cases` | `off`: covers are flat pictures; anything else: they stand as cases |
| `theme` | `light`, or dark |
| `transfer_kbps` | the most the menu's file receiver takes from the network, in KB a second; 0 or absent: no limit |

The loader reads none of these: they are the menu's.

## game.cfg

Written by the menu, read by the loader. A key that is absent or empty means the default.

| key | meaning | default |
|---|---|---|
| `modset` | the folder under `modsets/` to lay over the game | `mods/` |
| `save` | the slot of saved data to use: a folder under `saves/` | `save/` (the slot "default") |
| `memory` | MB the game is told of (2048 to 8992). **Advanced** | decided by the loader |
| `room` | MB the loader keeps for itself (128 to 2048). **Advanced** | 384 |

The advanced ones are shown behind a warning: a wrong value keeps a game from starting. Removing the key puts it right again.
The detailed log is not a key: it is the file `debug` in the game's folder.

## Starting a game

The menu asks the homebrew loader to start `nx2-game.nro` next (`envSetNextLoad`) and exits. The arguments:

```
argv[0]   sdmc:/switch/nx2-flow/nx2-game.nro
argv[1]   the NUP file, as a path of the card: sdmc:/nx2-flow/games/Some Game.nup
argv[2..] menu=sdmc:/switch/nx2-flow/nx2-flow.nro      (optional: what to start again when the game is closed)
          room=<MB>                                    (optional: game.cfg's "room", which the loader must know before it can read files)
```

Everything else the loader needs it reads from the NUP file and from the game's folder.

## Receiving files from a computer

The menu has a screen that does what Toronja Transfer does (toronja's `tools/transfer-nx`): it listens on TCP port 28302 and speaks
the same protocol ("TTX1": PING, LIST, PUT, GET, DELETE, HASH, MKDIR), so the same program on the computer sends to either.

## Covers by local wireless

The companion for a Switch downloads covers and hands them to the menu by local wireless, with the protocol toronja uses between
two consoles for the same thing (toronja's `cover_share`), unchanged: the menu asks for the covers of its games by their codes, and
gets the files.

## The NUP container

The format is the one `tools/nup.py` of nx2-relink reads: a superblock, a table of contents (packages, folders, files, names),
then the files' bytes. A package has a title id, a kind (0 the game, 1 an update, 2 added content), a version, and a block of
facts: its NPDM, its title, its publisher, its display version, its icon (a JPEG).
