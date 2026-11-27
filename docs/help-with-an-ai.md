# Helping nx2-flow, with an AI assistant at your side

Many people would like to help nx2-flow and don't know where to start. This page is for them. It has two parts:

1. **For the person**: what nx2-flow is, what you need, and every step from an empty card to a good report.
2. **For your AI assistant**: a block of instructions to paste into the assistant you use (a chat with a large language
   model). It then walks you through the steps one at a time, asks for what it needs to see, and reads your logs with you.

You can give this whole page to the assistant as it is: it is written for both of you.

What this page says is true of nx2-flow **0.2.6** (menu 0.2.6, loader 0.1.7). A line marked **(coming)** is about work that
is not in a release yet: it may change, or not come at all.

> [!IMPORTANT]
> **Only games you own, dumped by you, from your own console.** No game, key, firmware or console file is part of nx2-flow,
> none will be given, and none may be asked for or linked to, here or in an issue.

---

## Part 1: for the person

### What nx2-flow is

nx2-flow is two homebrew programs for a Switch 2:

| program | what it does |
|---|---|
| `nx2-flow.nro` (the menu) | A shelf of your games, kept as **NUP files** on the card. Each game has its options: detailed log, mods, slots of saved data, advanced settings. |
| `nx2-game.nro` (the loader) | Runs the game you picked, straight from its NUP file. The menu starts it; you don't start it by hand. |

A **NUP file** is one file that holds a game: its own files, and, when they were dumped with it, its update and its add-on
content. KaiDump, the dumper named below, can write one.

Homebrew runs on a Switch 2 through **kaiHEN**, a "homebrew enabler". kaiHEN is not part of nx2-flow: it has its own
project, its own pages and its own rules. nx2-flow only runs once kaiHEN runs.

> [!WARNING]
> This is experimental. kaiHEN itself warns that homebrew has full access to the system, and that a bad program could make a
> console unusable for good. Nothing here comes with a warranty.

### What you need

- A Switch 2, on the system version that kaiHEN's own page asks for.
- One of the starting points that kaiHEN supports (the next section).
- A card for homebrew, with room for your games (a game can be several GB). kaiHEN's page lists a microSD or a microSD
  Express card, and recommends microSD Express. This card is for homebrew files: it is not where the console keeps its own
  games.
- A computer, to copy files onto the card.
- The games you want to run, bought by you and on your console.

### Step 1: get kaiHEN running

kaiHEN is started from a game or an app that can be made to start it, called an "entry point". Which one you use depends
on what you have. **Follow kaiHEN's own instructions, not this page**: they change, and only kaiHEN's pages are kept up to
date. nx2-flow does not copy them, and hosts no file of theirs.

- kaiHEN's site: <https://ouncebrew.github.io/kaihensite/>
- kaiHEN's source and releases: <https://github.com/ouncebrew/kaihen> and <https://github.com/ouncebrew/kaihen/releases>

The entry points kaiHEN's site names:

#### If you start from SmileBASIC 4

kaiHEN's site has a section for it, with the versions it works with, the one-time setup and how to start kaiHEN again
later. Read "How do I use KaiHEN with SmileBASIC 4" and "Re-running KaiHEN with SmileBASIC 4" there.

#### If you start from Sid Meier's Civilization VII

kaiHEN's site names it as the other way in, and says which version of it (the Switch 1 version, 1.0.0, only). Read its list
of what you need before you start.

#### Other entry points

kaiHEN's site says, in its questions and answers, that some other games can be used too, in theory, and that they are more
useful as a second way in once kaiHEN has been set up. This guide gives no steps for them: read kaiHEN's own pages, and
use only what they document.

When kaiHEN has started, its tool on the computer says so. Then go on to step 2. Step 1 ends here: everything after it is
about nx2-flow, once kaiHEN runs.

### Step 2: open the homebrew menu over a game

With kaiHEN running, **hold R while you start a game** from the console's home screen.

- The first time you do it with a game, the game starts as usual: kaiHEN keeps a note of that game on the card.
- From the second time on, holding R opens the **homebrew menu** (hbmenu) in place of the game.

The game you hold R on is called the **host**. It matters a great deal, because nx2-flow runs as that game, with what that
game is allowed: its memory, its memory for graphics, its "system resource" (memory the system keeps for the program's own
use), and the services it may open.

| host | does it open the homebrew menu? |
|---|---|
| a Switch 2 game | yes |
| a Switch 2 Edition of a Switch game | yes |
| a Switch game with no Switch 2 Edition | no, not at this time |

The surest host for a game is the same game. A user's table of what each host has is in
[issue 5](https://github.com/Olmectron/nx2-flow/issues/5).

### Step 3: put nx2-flow on the card

1. Download the zip of the [latest release](https://github.com/Olmectron/nx2-flow/releases/latest).
2. Unzip it onto the card's root. You then have:

```
/switch/nx2-flow/nx2-flow.nro      the menu (what the homebrew menu shows)
/switch/nx2-flow/nx2-game.nro      the game loader
```

Your games go in `/nx2-flow/games/`. `docs/layout.md` says where everything else is.

### Step 4: dump your own games with KaiDump

KaiDump (<https://github.com/kraibse/KaiDump>) is a homebrew program that copies a game that is on your console. Its own
README is the one to follow; in short:

1. Copy `KaiDump.nro` from its [latest release](https://github.com/kraibse/KaiDump/releases) to
   `/switch/KaiDump/KaiDump.nro`.
2. Open the homebrew menu (step 2) and start KaiDump.
3. Pick the game, then what to take: the game, its update, its add-on content.
4. Pick the form:

| form | what it is | for nx2-flow 0.2.6 |
|---|---|---|
| **NUP** | one `.nup` file: what nx2-flow reads | **use this one** |
| TAR | one archive file holding the game's files (like a `.zip` with no packing), the quickest to make | not read by nx2-flow 0.2.6 |
| Extracted | the game's files, copied out into folders | not read by nx2-flow 0.2.6 (coming: games kept as folders) |

5. Pick where it goes: **SD card** (`sdmc:/switch/KaiDump/dumps/Game title [Title ID]/`), **FTP** (a way to copy files over
   your home network, with an FTP program on the computer), or **USB** to a computer. With FTP, set your FTP program to
   **binary** transfers: KaiDump refuses the others.

Things worth knowing, from KaiDump's own pages (they are KaiDump's to change):

- **If a game fails to dump**, KaiDump's README says to try starting kaiHEN through that game, then start KaiDump from that
  same homebrew menu and try again. In practice: open the homebrew menu by holding R on that game. This only works for a game
  that can open the homebrew menu at all (step 2).
- **When a game's update can't be opened**, KaiDump 1.0.8 dumps the game's first version instead (the program and the game's
  files of that one version, so that they match) and says so with a warning. That changes what is in the dump; whether
  nx2-flow then runs it is another question.
- **A Switch 2 Edition** in NUP form holds only the Switch program of it with KaiDump 1.0.8, not the Switch 2 one.
  nx2-flow 0.2.6 is made for Switch 2 programs, so such a NUP file is not one it is made to run (coming: the Switch 2
  program in the NUP file, and Switch programs in nx2-flow).
- KaiDump writes `sdmc:/switch/KaiDump/KaiDump.log`: send it with a report about a dump.

### Step 5: put the game on the card

Copy the `.nup` file to `/nx2-flow/games/` (or to a folder inside it). Three ways:

- **Whole**, on a card formatted as exFAT.
- **In parts**, on a card formatted as FAT32, which takes no file of 4 GB or more. Cut the file on the computer and copy the
  parts: the menu shows them as one game and the loader reads them as one file. The README's section
  "A FAT32 card: a game in parts" has the tools and the names that work, for example:

```
split -b 4000m Game.nup Game.nup.        (Linux, macOS, Git Bash)    makes Game.nup.aa, Game.nup.ab, ...
7-Zip: File > Split file..., 4000M       (Windows)                   makes Game.nup.001, Game.nup.002, ...
```

- **In a folder of its own**: a folder inside the games folder that holds one game's parts is that game, and a folder of
  nameless parts (`00`, `01`, ...) is one too. A game kept as its unpacked files (the program's folder `exefs/` and the
  game files' folder `romfs/`, as KaiDump's Extracted form writes them) is not read by 0.2.6 (coming).

To copy, take the card out and use a card reader, or use a file transfer program over the network. The menu's own receiver
(its Transfer screen, `ZL`) listens on port 28302 and speaks the protocol of Toronja Transfer; `menu/README.md` says more.

### Step 6: start the game

1. Hold R on the host game, so that the homebrew menu opens.
2. Start **nx2-flow**.
3. Look at the top of the screen: it says which game the menu was started over (its title id, which begins `0400` for a
   Switch 2 program and `0100` for a Switch one) and what it has: memory, memory for graphics, system resource. A host that is
   a Switch game, or has little memory, is shown with a warning.
4. Pick your game. Before you start it, `Y` opens its options, and there `X` checks the NUP file: that it is all there and
   what it holds.
5. Press `A` to start it.

### Step 7: read what happened

Every game has a folder of its own, named by its title id (16 hex digits):

```
/nx2-flow/<title id>/log.txt        the loader's log of the last run
/nx2-flow/<title id>/log.prev.txt   the log of the run before
/nx2-flow/<title id>/crash.txt      a run that ended badly: the file to send with a report
/nx2-flow/<title id>/debug          an empty file: when it is there, the log has more detail
```

- In the menu, `Y` on a game shows its last log and its crash report on the console itself (the row "View last log" or
  "View crash log"), so you don't need to take the card out.
- "Detailed log" in the same options turns on more detail (it makes the file `debug`). Turn it on before you run a game
  again for a report.
- `crash.txt` begins with a few lines that say what ended the run (the line `ended:`) and when the file was made
  (`written:`, which is not always when the game ended), then the whole log of that run. It is written as the run ends when
  the loader knows why. When a run ends with no word to the program, `crash.txt` is written the **next** time that game is
  started: when the console recorded a fault, or when the console's records could not be asked (then it may also be a game
  closed from the HOME menu, and the file says it can't tell). A game closed from the HOME menu with nothing recorded leaves
  none. So check that `crash.txt` is of the run you mean. When there is none, `log.txt` (the last run) or `log.prev.txt`
  (the one before) is what to send.
- **(coming)** the log will begin with a table, every line starting `[preflight]`, that compares what the host has with what
  the game asks for, and a line `[preflight] VERDICT ...` that sums it up. Part 2 says how to read it.

### Step 8: send a good report

Open an issue at <https://github.com/Olmectron/nx2-flow/issues>, after looking at the open ones: someone may already have the
same game or the same error.

A good report has:

- the game, by its **title id** (and its version, if you know it);
- the **host**: its title id, as the top of the menu shows it;
- **docked or handheld**;
- the nx2-flow version (0.2.6) and the kaiHEN version;
- what you did, and what you saw;
- `crash.txt` of that run, or else the lines of `log.txt` (or `log.prev.txt`) that matter, ideally with the detailed log on.

And none of these: any game file, key or console file, any link to one, your console's serial number, your IP address,
your name or your account. Before you share a log or a picture, with anyone (your AI assistant too), read it and take out
anything of that kind; crop a photo of the screen to the screen. The loader's log is meant to hold no personal data, but
what you add around it may.

### Step 9: for developers

Both programs are this repository's own source. Pull requests are welcome.

**What you need**: devkitPro (devkitA64) and ounce-devkit (libounce), from <https://github.com/ouncebrew/ounce-devkit>, and
Python 3.10 or newer. The easiest is ounce-devkit's Docker image:

```sh
git clone https://github.com/ouncebrew/ounce-devkit
cd ounce-devkit && docker build -t ounce-devkit .
```

**Build**, from the root of this repository:

```sh
docker run --rm -v "$PWD:/work" -w /work/loader ounce-devkit python3 tools/build_loader.py    # loader/out/nx2-game.nro
docker run --rm -v "$PWD:/work" -w /work/menu   ounce-devkit make                             # menu/out/nx2-flow.nro
```

**Tests that run on a computer** (in `menu/`, with a PC compiler; the image has one):

```sh
make nacptest copytest diagtest healthtest
out/preview/nacp_test && out/preview/copy_test && out/preview/diagnostics_test && out/preview/health_test
sh tools/test_parts.sh        # games in parts and the file receiver (needs Python with Pillow)
```

`make preview` is not a test: it draws the menu's screens on a computer, as pictures in `out/preview/` (it needs Python with
Pillow), so that a change to the menu can be looked at without a console.

**A pull request**:

1. Fork the repository, and make a branch from `main` for one change: one pull request, one purpose.
2. Write the commit message as the repository does: the part it is for, then what it does
   (`loader: ...`, `menu: ...`, `loader, menu: ...`).
3. Say in the pull request what it was tried with (the game by its title id, the host, docked or handheld), and attach the
   lines of `log.txt` or `crash.txt` from before and after.
4. Say what it does to a game that doesn't need it: a change for one game runs in every other one too.
5. Say plainly what was tried on a console and what only on a computer.
6. Name no game in the source's comments, and never log which user is playing.
7. No game's file, key or console file in a pull request or an issue, and no link to one.

The loader's runtime (`loader/runtime`) is shared with [nx2-relink](https://github.com/Olmectron/nx2-relink): a fix may need to
go there too. The menu is GPL-2.0-or-later and the loader MIT: write your own code, and copy none from a project under
another license.

---

## Part 2: for your AI assistant

Copy everything in the block below into your assistant, then tell it where you are ("kaiHEN isn't set up yet",
"kaiHEN runs, my game crashes", "I want to send a pull request").

Before you show the assistant a log or a picture, read Step 8's last paragraph: take out anything personal first.

```text
You are helping a beginner use and contribute to nx2-flow, a homebrew launcher and game loader for the Nintendo Switch 2
(https://github.com/Olmectron/nx2-flow). The guide you were given with these instructions (docs/help-with-an-ai.md) is your
reference; the project's README.md, docs/layout.md, menu/README.md and loader/README.md are the authority when they say more.
It is about nx2-flow 0.2.6. A feature the guide marks "(coming)" is not in that release: never present it as available.

RULES THAT ARE NOT NEGOTIABLE
1. Only games the person owns, dumped by them from their own console. If they mention a game they don't own, a download,
   or a dump from someone else, stop helping with that game and say why, once, plainly.
2. Never give, ask for, describe how to find, or link to: games, dumps, keys (prod.keys, title keys), firmware files,
   console files, or sites that share them.
3. Never help to get around copy protection, encryption or DRM, and never help to take apart a commercial game's program.
   If an error is about a missing key or encryption, say it is out of scope and stop there.
4. kaiHEN and its entry points belong to their own project. For setting kaiHEN up, only send the person to kaiHEN's own
   pages (https://ouncebrew.github.io/kaihensite/ and https://github.com/ouncebrew/kaihen). Do not write out, sum up, fill
   in or troubleshoot an entry point's steps, and do not point to other projects' ways in. Your help starts once kaiHEN runs.
5. Nothing personal, to you or in a report: no serial number, no IP address, no name, no account, no user id, no photo of
   a face or room. Before the person shares any log or picture, even with you, ask them to read it and take such things
   out; ask for a cropped photo of the screen, or the lines that matter, or a description in words if they prefer.
6. Do not make things up. If you don't know what a log line means, say so, and suggest sending it in an issue.
7. Say the console's risks once, at the start: this is experimental, and kaiHEN gives homebrew full access to the system.

HOW TO GUIDE
- First ask: which console (Switch 2), whether kaiHEN already runs (if not: send them to kaiHEN's pages, rule 4, and wait
  until it runs), which card format (exFAT or FAT32), and what they want to do.
- One step at a time. Give the step, then wait. After every step, ask for proof before the next one: a cropped photo of the
  screen, the lines of the log that matter, or what the screen says in words. Do not guess what the screen shows.
- Use plain words. When you must use a term (host, NUP, title id, system resource), explain it the first time.
- The order of the steps: kaiHEN runs; hold R on a game, twice the first time, to open the homebrew menu; put nx2-flow on
  the card; dump the game with KaiDump in NUP form; copy the .nup to /nx2-flow/games/ (whole, or cut in parts on FAT32);
  start nx2-flow from the homebrew menu; check the file (Y, then X); start the game (A); read the log; report.
- Hosts: the homebrew menu opens over a Switch 2 game or a Switch 2 Edition; not over a Switch game that has no Switch 2
  Edition, at this time. The surest host for a game is that same game. A Switch 2 game needs a Switch 2 host.
- KaiDump: NUP is the form nx2-flow 0.2.6 reads (TAR and Extracted are not). FTP must be in binary mode. If a game fails to
  dump, KaiDump's README says to open the homebrew menu through that same game (hold R on it) and try again from there; only
  a game that can be a host can be used so. KaiDump 1.0.8 dumps the first version of a game, with a warning, when the
  update can't be opened: that changes what is dumped, not whether it runs. A Switch 2 Edition in NUP form holds only its
  Switch program with KaiDump 1.0.8, which nx2-flow 0.2.6 is not made to run. Its log is sdmc:/switch/KaiDump/KaiDump.log.

HOW TO READ THE FILES
Files are in /nx2-flow/<title id>/ : log.txt (last run), log.prev.txt (the run before), crash.txt (a run that ended badly).
Ask for crash.txt first if there is one and it is of the run in question: its line "ended:" says what ended the run, and
the whole log follows. "written:" is when the file was made, not always when the game ended: when a run ends with no word
to the program, crash.txt is written at the next start of that game, if the console recorded a fault or if its records
could not be asked (then a close from the HOME menu can leave one too, and the file says it can't tell). A game closed
from the HOME menu with nothing recorded leaves none. With no crash.txt of that run, ask for log.txt, or log.prev.txt when
a game was started again since. Ask the
person to turn on "Detailed log" in the game's options (Y) and run again when the log says too little. The menu shows
the last log or crash report on the console itself (Y, "View last log" / "View crash log").

Lines are tagged by who wrote them: [nx2] the loader's start and memory, [game] reading the NUP file and the game's modules,
[load] linking the modules, [ipc] services the game asked for, [svc] system calls, including the game's end ("Break").
Read from the top: the first lines say the host's memory and what the game is told of.

KNOWN ERRORS (the words are those of the log or of crash.txt)
| what you see | what it means | what to do |
| "[nx2] THE HOMEBREW MENU WAS STARTED OVER A TITLE THAT HAS ... MB OF MEMORY" | the host is a Switch game, or a title with too little memory | start the homebrew menu over a Switch 2 game |
| "[ipc] sm: the service "..." isn't given to the title this was started over (result 1015, not allowed)" | the host may not open that service. Often harmless: many are asked for and never needed, and some are stood in for | only if the game then ends on it: change host |
| crash.txt "ended:" says the console refuses the service "..." to the game the homebrew menu was started over | the game needed a service the host may not have | start the homebrew menu over another game; the surest is the game itself |
| "this game is of the older kind ... nvdrv" | it draws as a Switch game does | the host must be a game of the same kind; the surest is the same game |
| "remote memory" ... "THAT IS LESS" | the host has less memory for graphics than the game was made for | a host with more (see issue 5) |
| "system resource" ... "THERE IS NONE" | the host has no system resource and the game maps memory as it runs | a host that has some (see issue 5) |
| "[game] ... isn't a NUP container" | the file is not a NUP file (a TAR, or something else) | dump again in NUP form |
| "[game] ... is not all there" | the file is cut short, or a part is missing | copy it again; check every part is there |
| "no module defines" / "the game called a function that none of its modules has" | the game needs something the loader doesn't give | report it; it may need a port made with nx2-relink |
| crash.txt says the game found its files damaged ("data is corrupted" kind) | the game's SDK read data it took for damaged; the cause is not known from this alone | copy the game again and check the card has room; if it stays, report it |
| crash.txt says the console still had memory the game wasn't told of | it may have run out of memory | in the game's options, Advanced, set Memory to 8992, try again; take it back to automatic after (X) |
| crash.txt says the run ended with no word to the program, and the console's records couldn't be asked | it may have been closed from the HOME menu, the console turned off, or a fault: the loader can't tell | report it with this file and say what you did |

(coming, not in 0.2.6) A pending change adds a table at the top of the log, lines starting "[preflight]", ending with
"[preflight] VERDICT STACK=... VAM=... SYSRES=... COMPAT_PACK=... REMOTE=..." and "advice:" lines. Use this only if the
person's log has these lines, and trust the log's own "advice:" lines over this summary, which may change:
- STACK=MISMATCH: the host and the game are not of one kind (0100... is a Switch program, 0400... a Switch 2 one). A Switch 2
  game needs a Switch 2 host. Change host.
- VAM=WARN: the game says it maps memory as it runs, and the host has no system resource to let it. SYSRES=WARN: the host
  has less system resource than the game declares. For either, a host that has as much.
- REMOTE=WARN: the host has less memory for graphics than the game declares; the game may end when it maps its graphics.
- COMPAT_PACK=ABSENT: the system's compatibility layer for Switch programs was not found in the host. It would matter only
  for running a Switch game, which is itself coming; for a Switch 2 game it says nothing.
- UNKNOWN: it could not be read. Not an error by itself.

THE ISSUE (https://github.com/Olmectron/nx2-flow/issues). Help the person fill in, then show them the text to check:
  Title: <title id> <short description of what happens>
  - Game: <title id>, version <x.y.z if known>, NUP made with KaiDump <version>
  - Host: <host title id, as the top of the menu shows it>
  - Docked or handheld: <...>
  - nx2-flow 0.2.6, kaiHEN <version>
  - What I did: <steps>
  - What happened: <what the screen showed>
  - Detailed log on: <yes/no>
  - Attached: crash.txt (or the lines of log.txt that matter)
Before they post: no game file, key or console file, no link to one, nothing personal. Look at the open issues first.

THE PULL REQUEST (for a person who writes code)
- One branch from main, one purpose. Commit messages as the repository writes them: "loader: ...", "menu: ...".
- Build in ounce-devkit's Docker image: in loader/, "python3 tools/build_loader.py"; in menu/, "make".
- Run the computer tests in menu/: "make nacptest copytest diagtest healthtest", then run out/preview/nacp_test,
  out/preview/copy_test, out/preview/diagnostics_test and out/preview/health_test; and "sh tools/test_parts.sh" for games
  in parts and the file receiver. Add a test for what the change does.
- The description says: what it fixes or adds, what it was tried with (game by title id, host, docked or handheld), the lines
  of log.txt or crash.txt before and after, what it does to a game that doesn't need it, and what was tried on a console
  and what only on a computer. Never claim it was tried on a console when it was not.
- No game named in the source's comments, no user id in the log, no code copied from a project under another license
  (the menu is GPL-2.0-or-later, the loader MIT).
- No game's file, key or console file in the pull request, and no link to one.
```

---

## Where things are said

| what | where |
|---|---|
| nx2-flow, in short | [`README.md`](../README.md) |
| what is where on the card, the settings files | [`docs/layout.md`](layout.md) |
| the menu: buttons, covers, the file receiver, its tests | [`menu/README.md`](../menu/README.md) |
| the loader: what it does as it starts, its limits | [`loader/README.md`](../loader/README.md) |
| what each host has | [issue 5](https://github.com/Olmectron/nx2-flow/issues/5) |
| kaiHEN | <https://ouncebrew.github.io/kaihensite/>, <https://github.com/ouncebrew/kaihen> |
| KaiDump | <https://github.com/kraibse/KaiDump> |
| ounce-devkit | <https://github.com/ouncebrew/ounce-devkit> |
