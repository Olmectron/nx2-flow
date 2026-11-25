# nx2-flow, the menu

A shelf of the games found as NUP files on the card: pick one, and the game loader (`nx2-game.nro`) is started with it.
What is where on the card, and how the two programs talk, is in [`../docs/layout.md`](../docs/layout.md).

The interface is toronja's (its software canvas, its shelf and cases, its covers), with everything of the emulator left
out. It is under the **GNU General Public License, version 2 or later**, as toronja's is: see [`LICENSE`](LICENSE).

## What it does

- **Lists the games**: every `*.nup` in the games folder (`/nx2-flow/games`, or the folder chosen in Settings). Of each file it
  reads only the superblock, the packages and their facts (title id, title, publisher, version, icon): a few kilobytes,
  whatever the file's size. What it read is kept in `/nx2-flow/cache/`, by the file's name, size and time, so a library opens
  without reading the files again.
- **Shows them as cases on a shelf**, in nine ways (`L` / `R`): a game's cover from `/nx2-flow/covers` when there is one, and
  otherwise a case made of the icon the NUP file has and the game's name.
- **A game's options** (`Y`): the detailed log on or off (the empty file `debug` in the game's folder), which set of mods is
  laid over the game (`mods/`, or a folder of `modsets/`; written to `game.cfg`), the memory the game is told of
  (`game.cfg`), and where the game's folder, log, crash report and saved data are.
- **Settings** (`-`): the games folder (picked among the card's folders), the shelf, covers shown or hidden, cases or flat
  pictures, the theme, downloading covers, looking for games again.
- **Starts a game** (`A`): asks the homebrew loader to start `/switch/nx2-flow/nx2-game.nro` next, with the NUP file and this
  menu's own path as its arguments, and exits. If `nx2-game.nro` isn't there, it says so.

| Button | On the shelf | In a panel |
|---|---|---|
| Left / Right | the game before, the next | change the focused line |
| `A` | start the game | change it, or open it |
| `X` | turn the case round | (folder picker) use this folder |
| `Y` | the game's options | |
| `L` / `R` | how the games stand | |
| `-` | Settings | |
| `B` | | back |
| `+` | leave the menu | |

## Building

For the console (ounce-devkit, in devkitPro's shell):

```
make                    out/nx2-flow.nro
```

Nothing else is needed: the text is drawn from glyph pictures made beforehand (`source/font_baked.h`, from
`tools/bake_font.py`), and pictures are decoded by `stb_image.h`. Copy `out/nx2-flow.nro` to `/switch/nx2-flow/` on the card,
beside `nx2-game.nro`.

(toronja's own FreeType text is still in `ui_canvas.h`, behind `UI_FONT_FREETYPE`, for a later build that loads the console's
font: it would draw every script. Nothing builds it yet.)

The previews, on a PC (a PC `g++`, Python and Pillow):

```
make preview            or, on Windows:   sh tools/render_previews.sh
```

That builds `preview/main.cpp` with the menu's own code, makes a pretend card (`tools/make_fake_card.py`: tiny NUP files of
games that don't exist, with made-up names, icons and covers), plays the menu through its screens and writes them to
`out/preview/*.png`. What the previews show is what the console's screen shows: the menu draws on a plain canvas either way.

## Covers

```
/nx2-flow/covers/<ID>.jpg|png         the front
/nx2-flow/covers/full/<ID>.jpg|png    the whole wrap: back, spine and front (the shelf can turn it)
/nx2-flow/covers/ids.txt              <title id>=<GameTDB code>, one line a game
```

`<ID>` is the game's title id (16 hex digits) or the code GameTDB files it under.

**GameTDB and title ids.** GameTDB names a game's art by a short code of its own, five letters and digits for this family of
consoles (`https://art.gametdb.com/switch/cover/<region>/<code>.jpg`, and `coverfull`, `coverHQ`, `coverfullHQ`: checked
against the site, by asking for headers only). **A NUP file has the game's title id and not that code**, and no public table
that ties the two was found. So nothing is guessed: a game gets covers from GameTDB once `ids.txt` says which code it has.

- `tools/fetch_covers.py`, on a computer: reads a folder of NUP files, takes codes from `--id <title id>=<code>` or an
  existing `ids.txt`, downloads the covers and writes them, with `ids.txt`, as the menu looks for them (`--card E:\` or
  `--out <folder>`). With `--match-titles` it looks a game's code up by its exact title in GameTDB's own list of games; that
  part could not be tried here (the site refused this computer's automated request for the list), and two games can share a
  name, so it is off unless asked for.
- On the console: Settings, Download covers. For the games `ids.txt` has a code for, when the console has internet access.
- **Games of the newer console**: whether GameTDB files them under `switch/` too, or anywhere yet, isn't known. A game with no
  cover shows a case made of its own icon and name.

## What is where

| | |
|---|---|
| `source/app.cpp`, `app.h` | the menu: the shelf, a game's options, the settings, the folder picker |
| `source/library.cpp`, `library.h` | the card's layout, the two settings files, the list of games and its cache |
| `source/nup.cpp`, `nup.h` | what the menu reads of a NUP file |
| `source/covers.cpp`, `covers.h` | cover files, `ids.txt`, the download |
| `source/images.cpp`, `stb_image.h` | JPEG and PNG, decoded and made smaller |
| `source/ui_canvas.h`, `ui_views.h` | toronja's canvas, text, shelf, cases and buttons, as changed by `tools/adapt_ui.py` |
| `source/font_baked.h` | the font, as glyph pictures (Aileron Regular, CC0) |
| `source/main_switch.cpp` | the console: the framebuffer, the pad, the network, handing over to the game loader |
| `preview/main.cpp` | the same menu on a PC, rendered to pictures |
| `tools/` | the font, the pretend card, the previews, the covers script |

## Not tried on a console

This was written and looked at through the previews only. Still to be seen on the console itself:

- **Presenting**: the framebuffer on the console's window (as toronja's menu makes it), and how fast the shelf redraws, in
  software, at 1280x720.
- **The pad**: buttons and the repeat of a held direction.
- **Starting the game loader**: `envSetNextLoad` with the arguments in quotes (a NUP file's name has spaces), and coming back
  to the menu after a game.
- **The download**: the console's sockets, and GameTDB reached plainly (`http://`); a secure connection through the system's
  own TLS is there for a redirect to `https://` and is the least sure part.
- **Text**: the font has the Latin letters without accents (an accented letter is drawn as its plain one); a title in another
  script shows no text of its own, only its icon.
