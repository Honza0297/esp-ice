# Getting started with ice

This guide walks through your first project with `ice`: getting an
ESP-IDF source tree, binding a project to a chip, and building +
flashing + monitoring the bundled `hello_world` example.

If you haven't installed `ice` yet, see [Install](../README.md#install).
The rest of this guide assumes `ice` is on your `PATH` (`ice --version`
should print a version).

## Before you start

You'll need:

- An ESP32-family board connected over USB, once you're ready to
  flash.  The serial port shows up as `/dev/ttyUSB0` (Linux),
  `/dev/cu.usbserial-*` (macOS), or `COM3` (Windows) -- ice tries to
  detect it automatically.
- Roughly 2 GB of free disk space for the ESP-IDF clone, plus another
  ~500 MB for the toolchain.  Both are downloaded once per IDF version
  and cached under `~/.ice/`.

Tooling that ice can provide or fall back on:

- **git** -- only required if you want ice to fetch and manage ESP-IDF
  for you (Options A and C in Step 1).  If you already have an
  ESP-IDF checkout and will point ice at it (Option B), you don't
  need `git` at all.
- **cmake and ninja** -- ice can install both via `ice tools` so you
  don't have to.  If the versions on your `PATH` satisfy ESP-IDF's
  minimum requirements, ice will happily use them instead.
- **A C/C++ cross-compiler** -- always installed by ice on `ice init`
  (downloaded into `~/.ice/tools/` and reused across projects).
- **Python** -- still used internally at build time for a few IDF
  tools not yet reimplemented in C.  ice manages the Python side
  itself, including a small internal environment; you don't source
  `export.sh` or run `pip install` yourself.

You do **not** need to install ESP-IDF, source `export.sh`, or set up
a Python virtual environment yourself.  ice handles all of that.

## First -- enable tab completion

Do this before anything else.  `ice` ships rich tab-completion for
subcommands, flags, chip targets, config keys, aliases, and profile
names -- every `TAB` makes the next step of this guide easier.

```bash
eval "$(ice completion bash)"   # or zsh, fish, powershell
```

Add the same line to your shell rc file (`~/.bashrc`, `~/.zshrc`,
`~/.config/fish/config.fish`, or your PowerShell `$PROFILE`) so it
persists across sessions.  Completion stays in sync with the binary
automatically -- there's nothing to regenerate after an upgrade.

## Step 1 -- provide an ESP-IDF source tree

Every project is bound to an ESP-IDF version.  There are three ways
to make one available; pick whichever fits your situation.

### Option A -- let ice manage everything (recommended)

If you don't yet have ESP-IDF on disk, this is the simplest path.
ice clones a single reference repo into `~/.ice/esp-idf/` and creates
cheap working checkouts under `~/.ice/checkouts/<name>/` that share
git objects with the reference.

```bash
ice repo clone                # one-time: clones into ~/.ice/esp-idf/
ice repo checkout v5.4        # creates ~/.ice/checkouts/v5.4/
```

A bare `<name>` argument to `ice repo checkout` lands at
`~/.ice/checkouts/<name>/`.  Pass an explicit path to drop the
checkout anywhere you like instead:

```bash
ice repo checkout v5.4 ~/work/esp-idf-5.4   # absolute path
ice repo checkout release/v5.2 ./idf-v5.2   # relative path
```

`ice repo list` shows the available branches and tags.  Create as
many checkouts as you need (`ice repo checkout v5.5`,
`ice repo checkout master`, ...) -- each one is cheap because objects
are shared with the reference.

### Option B -- point ice at an existing ESP-IDF checkout

If you already have an ESP-IDF clone you want to keep working in,
just pass its path to `ice init` later (Step 2):

```bash
ice init esp32 ~/work/esp-idf
```

Anything that looks like a path (contains a `/` or starts with `~`)
is used verbatim instead of being resolved under `~/.ice/checkouts/`.
Nothing is copied or moved -- ice reads from your existing tree
directly.

### Option C -- borrow from an existing clone, then let ice manage it

You have an ESP-IDF clone, but you want ice to manage version
checkouts going forward without re-downloading every git object.
Use `--reference` on `ice repo clone` to borrow objects from your
existing clone:

```bash
ice repo clone --reference ~/work/esp-idf
```

The new reference at `~/.ice/esp-idf/` borrows objects from
`~/work/esp-idf` instead of re-fetching them.  Add `--dissociate` to
copy the borrowed objects locally afterwards, so the ice-managed
reference no longer depends on your original clone:

```bash
ice repo clone --reference ~/work/esp-idf --dissociate
```

After cloning the reference, create checkouts the same way as
Option A (`ice repo checkout v5.4`).

## Step 2 -- set up your project

`ice init` binds the *current directory* to a chip target and an
ESP-IDF version.  Run it from the root of an ESP-IDF project (any
directory whose top-level `CMakeLists.txt` declares an IDF app).

For your first run, use the `hello_world` example that ESP-IDF
ships:

```bash
cd ~/.ice/checkouts/v5.4/examples/get-started/hello_world
ice init esp32 v5.4
```

Replace `esp32` with your chip target (`esp32s3`, `esp32c6`,
`esp32h2`, ...).  Replace `v5.4` with the IDF version you checked
out -- or with a path if you took Option B.

`ice init` will:

1. Wipe any previous build directory in this project.
2. Install the toolchain for the target chip (downloaded into
   `~/.ice/tools/` and reused across projects).
3. Run cmake to configure the build.
4. Persist the configuration in `.ice/config` so subsequent commands
   know which chip and IDF to use.

Run `ice status` afterwards to confirm what's bound:

```bash
ice status
```

## Step 3 -- build, flash, monitor

```bash
ice build       # compile
ice flash       # upload over the detected serial port
ice monitor     # tail serial output (Ctrl-] to exit)
```

`ice flash` will rebuild the project first if anything changed --
this matches `idf.py flash` behaviour and is controlled by the
`core.build-always` config key (default `true`).  The explicit `ice
build` above is shown for clarity but isn't strictly required.  Set
`core.build-always = false` if you'd rather have `ice flash` refuse
to act on a stale or unbuilt project.

If the serial port can't be auto-detected, set it explicitly:

```bash
ice config serial.port /dev/ttyUSB0   # write
ice config serial.port                # read back
```

`hello_world` prints chip info, free heap, and a 10-second countdown
to a restart.  If you see that on the monitor, your toolchain,
flashing, and serial wiring all work end-to-end.

## Where ice keeps things

| Path | What lives there |
|------|------------------|
| `~/.ice/esp-idf/` | Managed ESP-IDF reference clone.  Don't work in it directly. |
| `~/.ice/checkouts/<name>/` | Per-version working checkouts.  Borrow git objects from the reference. |
| `~/.ice/tools/` | Downloaded toolchains, shared across all projects. |
| `<project>/.ice/config` | Per-project configuration: chip, IDF path, sdkconfig, profiles. |
| `<project>/build/` | Build artefacts.  `ice clean` clears this. |

## Common next steps

- **Multiple chips in one project.**  Use named profiles to keep
  build artefacts from different chips apart:

  ```bash
  ice init esp32   v5.4 dev
  ice init esp32s3 v5.4 prod
  ice --profile=prod build
  ```

  See `ice help init` for the full list of per-profile knobs.

- **Inspect or change configuration.**  `ice menuconfig` opens the
  sdkconfig UI; `ice config --list` dumps every effective setting
  with its source scope; `ice status` shows the effective state for
  the active profile.

- **See firmware size.**  `ice size` summarises memory usage; pass
  `--archives` or `--files` to drill in.

## Troubleshooting

**`ice: command not found`** -- the install directory isn't on your
`PATH`.  See the install script's output for the exact `export PATH`
line, or follow the [Install](../README.md#install) section again.

**`failed to detect serial port`** -- pass it explicitly:
`ice flash --port /dev/ttyUSB0`, or persist it with `ice config
serial.port /dev/ttyUSB0`.  On Linux, you may need to add yourself
to the `dialout` group (`sudo usermod -aG dialout $USER`, then log
out and back in).

**`monitor` won't exit** -- press `Ctrl-]` (Control + right square
bracket).  This matches `idf.py monitor`'s convention.

**Something else** -- run with `-v` for full command output, or
`ice log` to inspect the captured logs of the last few tool runs.

## See also

- `ice` (no args) -- prints the same getting-started block from
  inside the binary itself.
- `ice help <command>` -- full help for any subcommand.
- [README.md](../README.md) -- install, build-from-source, shell
  completion.
- [CONTRIBUTING.md](../CONTRIBUTING.md) -- development setup,
  repository layout, and contribution conventions.
