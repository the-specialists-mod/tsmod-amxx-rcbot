# The Pawn bot: layout, conventions, ownership

`scripting/rcbot_amxx.sma` is RCBot for TS 3.0 as an AMX Mod X plugin: the port's bot backend
`rcbot_amxx`. Its logic is ported function by function from tsmod-rcbot-rs
(`../tsmod-rcbot-rs/crates/rcbot/src/{bot,config,weapons}` and the host's duties in
`crates/host/src`), with the same behaviour, the documented gameplay deviations included (the
kung fu fighter, no wall shots, no reload spam: tsmod-rcbot-rs `docs/design.md` §4). MegaHAL,
learning and navigation stay in Rust, behind the `rcbot_megahal`, `rcbot_learn` and `rcbot_nav`
natives. The function-by-function map is [design.md](design.md).

Read this before touching `scripting/`. The rules exist because several agents port subsystems
at once, and because Pawn runs every bot every server frame.

## 1. Files

```
scripting/rcbot_amxx.sma     entry: plugin_init/cfg/end, client hooks, tsx forwards -> rcbot/*; no logic
scripting/rcbot/
  consts.inc                 shared constants: COND_*, LOOK_*, TASK_*, ACT_*, CFG_*, DBG_*, SOUND_*, sizes
  math.inc                   Float:[3] vectors, angles (look.rs helpers, math.rs)
  config.inc                 Config (g_cfg_*), bot_config.ini, map configs
  state.inc                  every per-bot field (struct Bot), one section per subsystem; g_active
  util.inc                   rc_log, rc_debug, is_rcbot
  world.inc                  the frame's snapshots and caches, traces, PVS, contents (host world.rs)
  files.inc                  tsmod/rcbot/ paths, the map name
  report.inc                 e2e CHECK lines, status lines
  backend.inc                ts_bot_* forwards, making/saving/dropping bots, rejoin (host lib.rs, bots.rs)
  manager.inc                the frame loop, actions, runcmd, event fan-out (lib.rs start_frame, manager.rs)
  <subsystem>.inc            one per Rust module (section 6)
  minimal.inc                PLACEHOLDER behaviour, deleted once think/combat/look/movement are ported
```

Include order matters (section 3): `rcbot_amxx.sma` includes the core files first, then the
subsystems, `minimal.inc` last. A new file is added to that list by its owner (one line, in its
group) and nowhere else.

## 2. Data layout

**Per-bot state is enum-indexed 2D arrays**, indexed by player index (1..32; slot 0 is scratch,
used by `backend_create` to choose a profile before the bot has a slot). Each subsystem owns a
section of `state.inc`:

```pawn
enum Mv_I { MV_MOVE_TO_VALID, MV_AVOID_ENT, ... }        new g_mv_i[RC_SLOTS][Mv_I];
enum Mv_F { Float:MV_MOVE_SPEED, Float:MV_STUCK_TIME, ... } new Float:g_mv_f[RC_SLOTS][Mv_F];
enum Mv_V { MV_MOVE_TO, MV_AVOID_MOVE_TO, ... }           new Float:g_mv_v[RC_SLOTS][Mv_V][3];
```

* Read `g_mv_f[id][MV_MOVE_SPEED]`; pass a vector as `g_mv_v[id][MV_MOVE_TO]` (a `Float:[3]`
  row).
* **Add fields at the end of your own section's enums only**, so merges of different subsystems
  never touch the same lines. Reset them in your `<subsystem>_spawn_init` / `_bot_init`, not in
  someone else's.
* Lists (Rust `Vec`) are fixed arrays plus a count: `g_tk_i[RC_SLOTS][RC_MAX_TASKS][Tk_I]`,
  `g_tk_count[id]`. Stacks keep their top at the end, as the Rust port does (upstream's
  `dataStack`). When full, drop the newest (log at `rc_debug`), never index past the size.
* Server-wide tables (squads, boards, the camera, the weapon registry, client rep ids) live in
  the owning subsystem's file, not in `state.inc`.
* Arrays nobody reads yet are declared `stock` (they take no memory and raise no "never used"
  warning until code uses them). Drop `stock` when you start using one, if you like.
* Pawn allows three dimensions at most; a list of vectors is `Float:x[RC_SLOTS][N][3]`, with its
  scalar fields in separate `[RC_SLOTS][N][Enum]` arrays.

## 3. Pawn rules that bite

* **Variables, constants and enums must be declared before use** (functions may be used before
  their definition). A constant or global another file reads goes in `consts.inc` or
  `state.inc`; otherwise keep it in your file. A use before the declaration gives a cascade of
  errors and amxxpc 1.10 crashes with `std::bad_alloc` instead of printing them: compile with
  `-e<file>` to see the errors.
* `forward`, `state`, `native`, `public`, `stock`, `static`, `const`, `sizeof`, `tagof`,
  `defined`, `goto`, `sleep`, `exit` are keywords: not parameter or variable names.
* **Warnings are errors** (`cargo xtask pawn`). Unused parameters: `#pragma unused a, b` on its
  own line (on the line of the `{` it crashes amxxpc). Helpers only your file uses: `static`
  functions (file scope, no name clashes). Everything else: `stock`, named
  `<subsystem>_<rust name>`.
* AMX Mod X's `floatatan2(a, b)` is C's `atan2(a, b)`: the first argument is y.
* View pitch is the negated `vec_to_angles` pitch (look.rs `set_view_angles`: `-angles.x`).
* Floats are `Float:` tagged cells; tsbot.inc's data arrays hold floats as raw cells: read them
  as `Float:g_ws[id][TSPS_HEALTH]`. Store a tagged handle in an int field with `_:` and read it
  back with its tag: `g_nv_i[id][NV_PATH] = _:nav_path_create();`,
  `nav_path_next(RcPath:g_nv_i[id][NV_PATH])`.
* Bits: cells are 32 bits. `COND_*`, `CFG_*`, `IN_*`, `TSPF_*` fit; a bit per entity is
  `g_cb_vis_bits[id][ent >> 5] & (1 << (ent & 31))`.

## 4. Performance

Every bot thinks every frame (`ts_frame`, 100+ times a second with 32 slots). Per frame:

* **No string work.** No `format`, `copy`, `equal`, `log_*`, `get_user_name`, file access or
  `ts_bot_command` lines built per frame. Strings happen on events (a bot added, a chat line, a
  command) or on timers of seconds. `ts_sound`'s sample is matched once per sound
  (`hearing_sound_kind`, prefix checks), and most samples stop there.
* **Read the world through `world.inc`.** `world_refresh_frame` reads every player's snapshot
  once per frame (`g_ws`, the host's `refresh_frame`); `world_items()`, `world_inventory(id)`,
  `world_gamestate()` read their lists once per frame on first use; `world_entity_origin` caches
  entities per frame. Don't call `ts_get_player_*`, `get_user_origin`, `pev(player, ...)` or
  `is_user_alive` per bot per frame: `world_alive`, `world_origin`, `world_eyes`,
  `world_health`, `world_weapon`, ... Entvars the snapshot lacks (`pev_waterlevel`,
  `pev_movetype`) are a cheap `pev` each.
* **Budget traces.** `world_trace_line` is an engine trace: gate vision and searches on the
  think time (`TH_THINK_TIME`, `CB_UPDATE_VISIBLES_TIME`, the profile's `PR_VISION_REVS`) as the
  Rust bot does, never all bots x all entities each frame.
* **Locals**: big local arrays go `static` (locals live on the heap/stack, 32768 cells:
  `#pragma dynamic 32768`); small ones are fine.
* **Natives**: module natives are cheap calls, but A* is incremental (`nav_path_find` with
  `max_loops` = the profile's path revs, resumed next frame while `NAV_PATH_SEARCHING`).

## 5. Rust to Pawn

| Rust | Pawn |
|---|---|
| `struct Bot` field `self.x` | `g_<sec>_{i,f,v}[id][<SEC>_X]` (state.inc section of the owning module) |
| `impl Bot { fn f(&mut self, ...) }` | `stock <subsystem>_f(id, ...)` |
| `Vector` | `Float:v[3]`, passed by reference; `math.inc` (`vec_sub`, `vec_length` as `pow(..., 0.5)`, ...) |
| `Option<i32>` (index, id) | `-1` (or `0` for entity indexes) |
| `Option<f32>` / `Option<Vector>` | a `*_SET` int field beside the value |
| `Option<EntRef>` / `EntRef` | two int fields: index (0 none) and serial (user id for players, `TSITM_SERIAL` for items, 0 else); `entref_alive(index, serial)` |
| `Vec<T>` | fixed array + count (stacks: top at the end) |
| `enum` without data | `enum { A = 0, B, ... }` constants (consts.inc when shared) |
| `enum` with data (`Goal`, `BotEvent`) | a kind constant plus out parameters / one function per variant |
| `bool` | `bool:` returns; 0/1 in int fields |
| `f32` | `Float:` (tag every float literal: `0.0`) |
| `u32` bitflags | cells (32 bits) |
| `String` | fixed `[N]` char arrays (bytes, not UTF-8); only off the frame path |
| returns of tuples | the first value returned, the rest by reference (`&Float:score`) |
| `rng.random_long(lo, hi)` / `random_float` | `random_num(lo, hi)` / `random_float(lo, hi)` (AMX Mod X's generator, not the engine's `RANDOM_LONG` sequence) |
| `BotWorld` / `World` (trait) | world.inc (`world_*`) |
| `Action::Command/SelectWeapon/Loadout/Kill/Kick` | `bot_action_command/select_weapon/loadout/kill/kick` (manager.inc), run after every bot thought |
| `Frame::cmd` (`BotCmd`) | `movement_run_player_move` fills `CO_CMD_*`; manager runs `ts_bot_runcmd` |
| `CmdOutcome` | `bot_command_ran(id, events, vang)` |
| `BotGlobals` | the globals of their owners (g_cfg_*, weapons.inc, squad.inc, board.inc, ...) |
| `rcbot::chat` / `rcbot::learn` / `rcbot::nav` | `hal_*`/`chat_*`, `rl_*`, `nav_*` natives (handles in state fields, freed when the bot leaves) |
| `TsWeapon` ids (38 = kung fu) | RCBot ids in the bot (`RC_KUNGFU` 38); `ts_weapon_to_game_id` (0) for tsbot natives |

Cite the Rust above each function: `// think.rs Bot::spawn_init (CBot::SpawnInit(false)): ...`.
A deliberate difference from the Rust port is a comment saying why, and a line in design.md.

## 6. File ownership

One owner per file; others change it only through its owner. `state.inc` is shared: each
section's owner is named in its header comment. TODO tags in stubs are `TODO(<file stem>)`.

| Owner (agent) | Files | Rust sources |
|---|---|---|
| core | `rcbot_amxx.sma`, `consts.inc`, `math.inc`, `util.inc`, `state.inc` (core section, layout), `world.inc`, `files.inc`, `report.inc`, `backend.inc`, `manager.inc`, `minimal.inc` (think deletes it), `xtask/`, docs | host `lib.rs`, `bots.rs`, `world.rs`; `bot/manager.rs`; `bot/mod.rs` (types); `math.rs` |
| think | `think.inc`, `tasks.inc`, `goals.inc`, `special.inc`; state: think, tasks | `bot/think.rs`, `tasks.rs`, `goals.rs`, `special.rs` |
| combat | `combat.inc`, `fire.inc`, `look.inc`, `preaim.inc`, `botweapons.inc`, `weapons.inc`, `tsweapons.inc`; state: look, preaim, combat, fire, botweapons | `bot/combat.rs`, `fire.rs`, `look.rs`, `preaim.rs`, `weapons.rs`; `weapons/` |
| movement | `movement.inc`, `navigate.inc`, `remember.inc`, `hearing.inc`, `items.inc`, `board.inc`; state: movement, navigate, remember, hearing, items | `bot/movement.rs`, `navigate.rs`, `remember.rs`, `hearing.rs`, `items.rs`, `board.rs`; host `editor.rs`, `commands.rs` `load_waypoints` |
| kungfu | `kungfu.inc`, `learning.inc`; state: kungfu, learning | `bot/kungfu.rs`, `learning.rs`; host `bots.rs` learned files |
| social | `events.inc`, `chat.inc`, `squad.inc`, `botcam.inc`, `config.inc`, `commands.inc`, `menu.inc`, `profile.inc`, `rep.inc`, `users.inc`, `client.inc`; state: profile, squad, events | `bot/events.rs`, `squad.rs`, `botcam.rs`; `config/`; host `commands.rs`, `bots.rs` chat/rep |

Cross-subsystem calls go through the owner's `stock` functions (their names and signatures are
fixed now; change one only with its owner and every caller in one commit). Reading another
section's state fields is fine; writing them is the owner's job, except where the Rust code
writes them from that module (then say so in a comment).

## 7. Testing

* `cargo xtask pawn`: compiles every `.sma` with amxxpc 1.10 against `include/`,
  tsmod-amxx-rs's includes (`TSMOD_AMXX_INCLUDE`, default `~/dev/hl/tsmod-amxx-rs/include`) and
  AMX Mod X's; any warning fails.
* `cargo xtask e2e [linux] [win]` (xtask/src/e2e.rs): the plugin and the five modules on the
  amxx-testenv roots, `ts_bot_backend rcbot_amxx`, `ts_bot_quota 0`, two `addbot` and two
  `rcbot addbot`, then `report.inc`'s checks (`backend`, `add_quota`, `add_rcbot`, `spawned`,
  `runcmd`, `moved`, `fired`) in `logs/rcbot_amxx_e2e.log`, and no runtime errors. The roots are
  shared: run it only when nothing else uses them. Add a check for your subsystem in
  `report.inc` (core reviews) when its behaviour can be seen from the log.
