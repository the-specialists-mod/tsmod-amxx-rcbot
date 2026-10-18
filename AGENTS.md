# AGENTS.md

Guidance for coding agents (and people) working in this repository: **RCBot hosted by AMX Mod X**
for the Rust port of The Specialists 3.0
([the-specialistsmod-rs](https://github.com/the-specialists-mod/the-specialistsmod-rs), checked out
at `../rust-hlmod-the-specialists`). Start with [README.md](README.md).

## Design

The standalone bot, [tsmod-rcbot-rs](https://github.com/the-specialists-mod/tsmod-rcbot-rs)
(`../tsmod-rcbot-rs`), is a port plugin (`tsmod_rcbot`, `ts_bot_backend rcbot`): its `rcbot`
library crate is the whole bot AI in pure Rust (no FFI, no file I/O) and its `host` crate glues it
to the port's plugin API. This repository splits the same bot differently, so server operators
and plugin authors can change the bot's behaviour in Pawn:

* **Reusable Rust AMX Mod X modules** for what is heavy, stateful or file-backed, each a thin
  native layer over `rcbot`:
  * `rcbot_megahal` (`crates/megahal`, `include/rcbot_megahal.inc`): MegaHAL brains and
    `bot_chat.ini` (`rcbot::chat`). Done.
  * `rcbot_learn` (`crates/learn`, `include/rcbot_learn.inc`): the global and per-bot learned
    data and their files, the opponent model, the combat ledger, and generic GAs,
    perceptrons, NNs and SOMs (`rcbot::learn`). Done.
  * `rcbot_nav` (`crates/nav`, `include/rcbot_nav.inc`): waypoints (`.rcw`), visibility
    (`.rcv`), the queries, per-bot A* path objects (`rcbot::nav`), the map's heatmap (`.rhm`, the
    A* danger source, so it lives here rather than in `rcbot_learn`), the editor and
    auto-waypointer (`rcbot::nav::editor` over an engine-backed `BotWorld`, `src/world.rs`). Also
    a metamod plugin, for the engine table (traces, point contents, entities) and `StartFrame`
    (missing visibility rows, traced a few per frame). Done.
* **The bot logic as a Pawn plugin**, `scripting/rcbot_amxx.sma` + `scripting/rcbot/*.inc`
  (one file per Rust module, all ported; docs/design.md lists each function): think, tasks,
  combat, kung fu, squads, chat decisions; it registers with the port as bot backend
  `rcbot_amxx` (`ts_bot_backend rcbot_amxx`) through tsx's `tsbot.inc` (tsmod-amxx-rs v0.3.0).
  It calls the modules' natives where the Rust bot calls `rcbot::chat` / `rcbot::learn` /
  `rcbot::nav` (see `crates/learn/src/natives.rs` for the mapping from `bot/learning.rs`,
  `think.rs`, `combat.rs`, `manager.rs`). **Read [docs/pawn.md](docs/pawn.md) before touching
  `scripting/`** (data layout, performance rules, file ownership); the function map is
  [docs/design.md](docs/design.md).
* `crates/common` (`rcbot-amxx-common`, an rlib): handle tables, the data directory with atomic
  writes, the module's random numbers, cell/float/string helpers.

**Data files are byte-compatible with the standalone `tsmod_rcbot`** and live in the same
folder, `<gamedir>/tsmod/rcbot/` (AMX Mod X's `BuildPathname("tsmod/rcbot")`), with the same
names and key scheme (`botprofiles/<id>_hal.brn`, `botprofiles/<key>.rld/.rwr/.rlo` with
`rcbot::learn::store::profile_key`, `team<n>.rld`, `tsweaps.rld`, `weaponstats.rwr`,
`loadouts.rlo`, `botprofiles/players.rop`, `waypoints/ts/<map>.rcw/.rcv/.rhm`). Never invent a format: the
bytes come from `rcbot`'s own `save`/`load`, and the tests compare files with what `rcbot` writes.
Writes are atomic (temporary file renamed over the old one), as the host's.

The kung fu fighter's `<key>.rkf` belongs to the Pawn kung fu phase: `rcbot::bot::kungfu` is
bot logic, not a learner, and isn't exposed (it could be passed through as an opaque blob later
if the Pawn fighter keeps its format).

## Sources of truth

| Question | Where to look |
|---|---|
| What the bot does, file formats, upstream behaviour | `../tsmod-rcbot-rs/crates/rcbot` (pinned by tag in `Cargo.toml`) and its `docs/design.md`; RCBot upstream is APGRoboCop/rcbotold |
| How files are found, loaded and saved | `../tsmod-rcbot-rs/crates/host/src/bots.rs` (`wake_hal`, `new_bot`, `save_bot`, `save_globals`, `load_heatmap`), `lib.rs` (global load), `commands.rs` (`load_waypoints`, `waypoint_save`) |
| What the bot does around A* (path stack, failed goals, tactics, roaming) | `../tsmod-rcbot-rs/crates/rcbot/src/bot/navigate.rs`, `think.rs` (`task_find_path`), `goals.rs` (`Explore`); `crates/nav/src/paths.rs` cites each |
| The module SDK (`export_module!`, `natives!`, `Args`, `FakeArgs`, core functions) | `../tsmod-amxx-rs/crates/amxx` (pinned by tag) and its AGENTS.md |
| AMX Mod X's module ABI and Pawn | `../ref/amxmodx` |
| The port's plugin API (bot backends) | `../rust-hlmod-the-specialists/crates/plugin-api` |

## Layout and the `unsafe` boundary

```
crates/common/   rcbot-amxx-common: handles, DataDir, ModuleRandom, helpers (no unsafe)
crates/megahal/  rcbot_megahal module: src/natives.rs, src/state.rs, src/tests.rs, src/ffi.rs
crates/learn/    rcbot_learn module: src/natives.rs, src/objects.rs, src/state.rs, src/tests.rs, src/ffi.rs
crates/nav/      rcbot_nav module + metamod plugin: src/natives.rs, src/paths.rs (RcPath), src/state.rs,
                 src/world.rs (EngineWorld: rcbot's World/BotWorld on amxx::engine), src/tests.rs, src/ffi.rs
include/         rcbot_megahal.inc, rcbot_learn.inc, rcbot_nav.inc (shipped in dist)
scripting/       Pawn plugins; scripting/test/rcbot_modules_smoke.sma calls every native
scripting/rcbot_amxx.sma, scripting/rcbot/   the Pawn bot (docs/pawn.md)
docs/            pawn.md (Pawn conventions, ownership), design.md (Rust -> Pawn map)
xtask/           cargo xtask build | dist | lint | test | pawn | e2e (src/e2e.rs) | compat (src/compat.rs) | perf (src/perf.rs) | perf-compare
```

1. **No `unsafe` in this repository** except what `amxx::export_module!`, `amxx::export_metamod!`
   and `amxx::natives!` expand to, in each module's `src/ffi.rs` (`#[allow(unsafe_code)]` there only; the crates are
   `#![deny(unsafe_code)]`). Engine or AMX Mod X access that isn't in `amxx` goes into
   tsmod-amxx-rs as a safe wrapper first, then gets used here after a release.
2. **Natives are `fn(&mut dyn Args) -> Cell`** in `natives.rs`/`objects.rs`, tested with
   `amxx::native::FakeArgs` in `tests.rs`; each test runs on its own thread, so the thread-local
   state is per test. `ffi.rs` only lists the table and the `ModuleDef`.
3. **No native calls back into a plugin**, so the state's `RefCell` is never borrowed twice. If a
   native ever fires a forward, release the state first (as tsx does).
4. **rcbot_megahal and rcbot_learn are pure AMX Mod X modules**: no metamod exports (AMX Mod X
   would treat a library exporting `Meta_Attach` as a metamod plugin). **rcbot_nav is also a
   metamod plugin** (`export_metamod!`, as tsx): its hooks are safe `extern "C" fn`s in `ffi.rs`
   (`StartFrame` only) that call into `state` and set `meta::ignored()`. `xtask dist` checks both
   ways (`MODULES`' metamod column). Engine access goes through `world::with_world` (tests
   install a fake world there). Randomness is `ModuleRandom` everywhere (seeded xorshift,
   `hal_seed`/`rl_seed`/`nav_seed`), not `RANDOM_LONG`: amxx v0.3.0 has no `pfnRandomLong`
   wrapper; rcbot_nav switches with its `NavRandom` type once it has.
5. **Handles** are positive cells from `common::Handles`; `*_free(&h)` zeroes the variable. They
   are cleared in `AMXX_PluginsUnloaded` (plugins reload every map); `rcbot_learn`'s global data
   stays across maps, like the standalone plugin's.
6. **Panics abort the server** (`panic = "abort"`): never index with plugin values; check sizes
   (`natives::MAX_ARRAY`, `objects::MAX_SIZE`) and return a native error instead.

## Build and test

Requirements: Rust stable with `i686-unknown-linux-gnu`, `i686-pc-windows-gnu` and
`x86_64-pc-windows-gnu` (the x86_64 builds are for goldsrc-net's 64-bit AMX Mod X fork), clang ≥ 18,
32-bit glibc/libgcc, MinGW-w64, and `../xash3d-fwgs` (with `3rdparty/library_suffix`) or
`FWGS_DIR`: `amxx` pulls in the port's `goldsrc-sys`, which generates bindings from the FWGS
headers. `.cargo/config.toml` sets `FWGS_DIR` to `../xash3d-fwgs` unless the environment has it;
xtask looks for `xash3d-fwgs` next to the checkout or any ancestor (worktrees work).

```sh
cargo xtask test                 # unit tests, x86_64 and i686, then `pawn` if amxxpc is there
cargo xtask build [--release]    # all four targets; must build without warnings
cargo xtask lint                 # cargo fmt --check, clippy --all-targets -D warnings, every target
cargo xtask dist [--debug-info]  # dist/<platform>/addons/amxmodx/{modules,scripting/include}/ with
                                 # AMX Mod X's names (rcbot_megahal_amxx_i386.so, _amd64.so, rcbot_nav_amxx.dll, ...);
                                 # checks the AMXX_* exports, the metamod exports (rcbot_nav only, with an
                                 # undecorated GiveFnptrsToDll), no MinGW runtime imports
cargo xtask pawn                 # amxxpc on scripting/**/*.sma (include/ first; warnings fail)
```

`AMXX_SCRIPTING` names amxxpc's directory (default
`~/dev/hl/ref/extracted/amxmodx-1.10.0-git5487-linux/addons/amxmodx/scripting`). amxxpc needs its
own directory on `LD_LIBRARY_PATH` (xtask sets it).

`TSMOD_AMXX_INCLUDE` names tsmod-amxx-rs's include directory (tsbot.inc; default
`~/dev/hl/tsmod-amxx-rs/include`; CI unpacks the v0.3.0 release's), searched after `include/` and
before AMX Mod X's.

`cargo xtask e2e [linux] [win]` (`xtask/src/e2e.rs`) runs rcbot_amxx on the amxx-testenv roots
(`~/games/amxx_rehlds_{linux,win}`, built by `../ref/amxx-testenv/setup.sh`): dist, the plugin,
tsx/tsfun from `../tsmod-amxx-rs/dist` (its `cargo xtask dist` when missing), ts_dojo with
`ts_bot_backend rcbot_amxx` and `ts_bot_quota 0`, four test profiles with chat on, 2 `addbot` +
2 `rcbot addbot`, the plugin's phase 1 `CHECK` lines (`logs/rcbot_amxx_e2e.log`: waypoints, A*,
weapon switch, turning, a kill, chat and a MegaHAL reply, ...), the `rcbot` command over rcon,
`changelevel ts_lobby` and the phase 2 checks (bots back, kung fu), the data files saved during
the run, and the error logs; it restores the configs and files it changed. The `~/games` roots
are shared: never start servers there outside an e2e run (and run e2e only when nothing else uses
them), and stop them with the testenv's `stop.sh`. `linux64` (in e2e, compat, perf and
perf-compare) is the 64-bit root `~/games/amxx_rehlds_linux64` (`setup.sh linux64`: goldsrc-net's
ReHLDS, Metamod-R and AMX Mod X fork) with the x86_64 modules and tsx/tsfun from tsmod-amxx-rs's
`dist/linux-x86_64`.

`cargo xtask compat [linux] [win]` (`xtask/src/compat.rs`) shows the data files are shared with
the standalone tsmod_rcbot on one root: rcbot_amxx plays and writes every learned file, then
tsmod_rcbot (`../tsmod-rcbot-rs/dist`, loaded by the port from `ts/dlls/`) loads, plays and
rewrites them, then rcbot_amxx loads what it wrote (report.inc's `file` lines for the Pawn
readers, `.rkf` and `.rcr`); `ts/tsmod/rcbot/` is restored afterwards. crates/learn's
`compat_tests` load a checked-in run's files (`crates/learn/tests/compat/pawn/`) with `rcbot`'s
loaders and save them back byte for byte. `cargo xtask perf [linux] [win]` times `ts_frame`
(`rcbot_profile`, plugin without `debug`) with 4, 16 and 31 bots and prints the server's fps.
`cargo xtask perf-compare [linux] [win]` (`xtask/src/perf_compare.rs`) compares the backends on
one root: `native`, the standalone `rcbot` and `rcbot_amxx`, on ts_dojo and ts_lobby with 0, 4, 16
and 31 quota bots, three repetitions in a seeded shuffled order, the server pinned with taskset;
the process' CPU time from `/proc`, the frames and alive bots from
`scripting/test/perf_alive.sma`, `stats`, rcbot_amxx's ts_frame, and 31 bots at `sys_ticrate
1000` (the `PC_*` variables in its header). CSV and markdown in `target/perf-compare/`; it
restores what it changes.

Before calling a change done: `cargo fmt`, `cargo xtask test`, `cargo xtask lint`, and
`cargo xtask dist` when exports or the build change.

## Conventions

- **Keep `rcbot`'s behaviour.** Natives wrap `rcbot` functions; they don't reimplement them.
  Where the host did something around a call (the order of brain load, save and pretrain; which
  files are loaded for a bot), port it from `../tsmod-rcbot-rs/crates/host` and cite it.
- **Native names** use `hal_`/`chat_` (megahal), `rl_` (learn) and `nav_` (nav). Every native is in its
  `.inc` with a doc comment, in the README's table, and called by
  `scripting/test/rcbot_modules_smoke.sma`. Adding a native means all four plus a test.
- **Strings** are bytes in `rcbot`. `Args::string` is UTF-8 (lossy) and `set_string` takes `&str`,
  so non-UTF-8 bytes in names are replaced; keep conversions in `common` (`set_bytes`, `set_out`).
- **Formatting** is rustfmt's (`rustfmt.toml`: 140 columns, `use_small_heuristics = "Max"`).
  Comments say why; doc comments on public items; file headers credit RCBot (Paul "Cheeseh"
  Murphy) and say GPL-3.0-or-later with the AMX Mod X and RCBot exceptions.

## Pinned dependencies

`amxx` (tsmod-amxx-rs) and `rcbot` (tsmod-rcbot-rs) are git dependencies pinned by tag in the root
`Cargo.toml`'s `[workspace.dependencies]`, the only place they are named. `amxx` v0.3.0 pins the
port's v3.0.8 (plugin API 7); `rcbot` (v0.3.0) has no port dependency. When bumping, check
`Cargo.lock` has one `goldsrc-sys` and one `ts-plugin-api`.

## Git

- Work on `main`. Commit in logical steps; push only when asked.
- Releases: push a `v*` tag; `.github/workflows/release.yml` builds both platforms (with the
  Pawn compile check) and publishes `tsmod-amxx-rcbot-<tag>-<platform>.zip`. It pins the FWGS
  commit the port's release workflow uses; bump them together.
