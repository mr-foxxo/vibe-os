# Apps, Modules, and Executable Catalog

This page describes the executable surface of the repository as it exists today.

## 1. Execution tiers

There are four different categories of "apps" in this tree, and mixing them up makes the system look more complete or more inconsistent than it really is.

### Built-in bootstrap code

These pieces are linked into the kernel image and exist to get the machine to a usable shell or desktop path:

- built-in `init` service
- built-in bootstrap shell fallback
- userland bootstrap runtime allocator
- service host entrypoints

### External AppFS apps

These are the real steady-state runnable applications exposed to the shell and desktop. They are loaded one at a time through the AppFS runtime.

### Language/runtime apps

These are launchable runtimes or tooling layers packaged as apps:

- `hello`
- `js`
- `ruby`
- `python`
- `java`
- `javac`
- `lua`
- `sectorc`

### Compatibility ports

These are imported or adapted utilities and BSD games that are surfaced through the same shell-visible catalog even though their source origin differs from the native Vibe OS apps.

## 2. Authoritative app catalog

The shell-visible command list is generated into:

- [`build/generated/app_catalog.h`](../../build/generated/app_catalog.h)
- from [`config/app_catalog.tsv`](../../config/app_catalog.tsv)

That generated catalog is the best single reference for:

- command names
- whether they are shell-visible
- whether the shell should prefer an external implementation
- stub paths synthesized into the userland FS view

## 3. Boot-critical external apps

These matter most to the normal boot path:

| App | Role |
| --- | --- |
| `userland` | default external boot app; starts shell and optionally auto-runs desktop boot |
| `startx` | external graphical launcher for the desktop/runtime path |
| `edit` | external launcher into editor flow |
| `nano` | external launcher into nano-style editor mode |

`init` tries `userland` first and only falls back from there.

## 4. Native desktop/windowed apps

Main desktop applications under [`userland/applications/`](../../userland/applications):

| App | Source | Purpose |
| --- | --- | --- |
| Terminal | `terminal.c` | interactive terminal window backed by shell command execution |
| Clock | `clock.c` | GUI clock widget/window |
| File Manager | `filemanager.c` | file browser and launcher UI |
| Editor | `editor.c` | text editor window |
| Task Manager | `taskmgr.c` | process/task snapshot viewer and termination UI |
| Calculator | `calculator.c` | calculator UI and input handling |
| Image Viewer | `imageviewer.c` | opens image nodes from the userland FS layer |
| Sketchpad | `sketchpad.c` | drawing app with bitmap export flow |
| Desktop shell | `desktop.c` | window manager, taskbar, start menu, launcher, dialogs, theming, resolution switching |

Important detail:

- `desktop.c` is not just wallpaper and a taskbar
- it is the main window orchestration layer that owns app instance pools, launch requests, dialogs, trash handling, and start-menu routing

## 5. Desktop-integrated games

Games under [`userland/applications/games/`](../../userland/applications/games):

| App | Role |
| --- | --- |
| `snake` | native Snake |
| `tetris` | native Tetris |
| `pacman` | native Pac-Man-style game |
| `space_invaders` | native Space Invaders-style game |
| `pong` | native Pong |
| `donkey_kong` | native Donkey Kong-style game |
| `brick_race` | native driving/avoidance game |
| `flap_birb` | native Flappy-Bird-style game |
| `doom` | DOOM-style runtime integration |
| `craft` | voxel/crafting runtime integration |
| `personalize` | desktop personalization utility |

`desktop.c` also contains explicit start-menu entries and launch routing for these apps.

## 6. Language/runtime utilities

These are packaged as external apps and exposed to the shell:

| Command | Role |
| --- | --- |
| `hello` | sample/minimal app |
| `js` | JavaScript runtime entry |
| `ruby` | mruby runtime entry |
| `python` | MicroPython runtime entry |
| `java` | Java runtime entry |
| `javac` | Java compiler/tool entry |
| `lua` | Lua runtime and REPL path |
| `sectorc` | custom language/toolchain runtime |

These live across `lang/`, `userland/lua/`, and `userland/sectorc/`.

## 7. POSIX-style and compatibility utilities

Shell-visible utilities currently listed in the catalog include:

- `echo`
- `cat`
- `wc`
- `pwd`
- `head`
- `sleep`
- `rmdir`
- `mkdir`
- `tail`
- `grep`
- `sed`
- `loadkeys`
- `true`
- `false`
- `printf`

Some of these prefer external AppFS implementations first, while others can be serviced through compatibility stubs or built-in fallbacks.

## 8. BSD games and imported programs

The generated catalog also exposes a long compatibility/game list sourced from the compatibility tree, including:

- `adventure`
- `arithmetic`
- `atc`
- `backgammon`
- `banner`
- `bcd`
- `battlestar`
- `boggle`
- `bs`
- `caesar`
- `canfield`
- `cribbage`
- `factor`
- `fish`
- `fortune`
- `gomoku`
- `grdc`
- `hack`
- `hangman`
- `mille`
- `monop`
- `morse`
- `number`
- `phantasia`
- `pig`
- `pom`
- `ppt`
- `primes`
- `quiz`
- `rain`
- `random`
- `robots`
- `sail`
- `snake-bsd`
- `teachgammon`
- `tetris-bsd`
- `trek`
- `wargames`
- `worm`
- `worms`
- `wump`

These commands are part of the exposed app surface even though they are not all native Vibe OS GUI apps.

## 9. Core userland modules that make apps possible

Important module files under [`userland/modules/`](../../userland/modules):

| Module | Purpose |
| --- | --- |
| `console.c` | userland console abstraction |
| `shell.c` | command line, history, parsing, execution |
| `fs.c` | userland filesystem and persistence layer |
| `lang_loader.c` | AppFS directory and app image loader |
| `syscalls.c` | thin syscall wrappers |
| `ui.c` / `ui_*` | desktop UI primitives |
| `image.c` / `bmp.c` | image decoding/helpers |
| `dirty_rects.c` | redraw optimization support |
| `busybox.c` | built-in command multiplexer/fallbacks |

Without these modules, the desktop and shell apps would have no runtime substrate.

## 10. Practical interpretation

When someone asks "what apps does Vibe OS have?", the accurate answer is:

- a small built-in bootstrap path
- a larger external AppFS catalog
- a desktop/windowing environment implemented as modular apps
- language runtime apps
- a broad compatibility catalog of utilities and BSD games

When someone asks "which apps are actually part of the boot-to-usable-system path?", the short list is:

- `init`
- `userland`
- `startx`
- the shell
- the desktop
