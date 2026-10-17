# tsmod-amxx-rcbot

[RCBot1](https://github.com/APGRoboCop/rcbotold) (Cheeseh's Half-Life bot) hosted by **AMX Mod X**
for **The Specialists 3.0** on its
[Rust port](https://github.com/the-specialists-mod/the-specialistsmod-rs): reusable Rust AMX Mod X
modules for the bot's MegaHAL, learning and navigation, with the bot logic as a Pawn
plugin that registers as the port's bot backend `rcbot_amxx`.

The modules are thin native layers over the `rcbot` library of
[tsmod-rcbot-rs](https://github.com/the-specialists-mod/tsmod-rcbot-rs) (the standalone
`tsmod_rcbot` bot), and share its data files byte for byte in `<gamedir>/tsmod/rcbot/`: a brain,
learned file or waypoint file written by one is read by the other.

## Status

| Part | State |
|---|---|
| Workspace, licences, xtask (build, dist, lint, test, pawn), release workflow | done |
| `rcbot_megahal`: MegaHAL brains (`botprofiles/<id>_hal.brn`, keyword lists, pretraining), `bot_chat.ini` lines filled from a plugin-supplied context, BotChatReply, typos | done |
| `rcbot_learn`: global learned files, per-bot learned data (`.rld`/`.rwr`/`.rlo`), spawn/death breeding, decision perceptrons, weapons by range, loadout bandit, opponent model, combat ledger, generic GA/perceptron/NN/SOM | done |
| `rcbot_nav`: waypoints (`.rcw`), visibility (`.rcv`, missing rows traced over frames), queries (nearest, goals, cover, paths, buckets), per-bot incremental A* with tactics, the heatmap (`.rhm`), editor operations, auto-waypointing and draw data | done (no server run yet) |
| `scripting/rcbot_amxx.sma`: the bot logic in Pawn, backend `rcbot_amxx` | skeleton: backend, frame loop, state, every subsystem's stubs, placeholder behaviour ([docs/pawn.md](docs/pawn.md), [docs/design.md](docs/design.md)) |
| End-to-end tests on the amxx-testenv roots (`cargo xtask e2e`) | written, not run yet |

## Install

From a release zip (`tsmod-amxx-rcbot-<tag>-<platform>.zip`): copy `addons/` over the server's
`ts/addons/`. The modules load on demand (`#pragma reqlib`), or list `rcbot_megahal`,
`rcbot_learn` and `rcbot_nav` in `configs/modules.ini`. `rcbot_megahal` and `rcbot_learn` are
pure AMX Mod X modules; `rcbot_nav` is also a metamod plugin (it needs the engine's traces),
which AMX Mod X attaches as one when it loads the module, as with tsx. They are built on the
module SDK tsmod-amxx-rs's modules use (AMX Mod X 1.8.2 and later); they have not been run on a
server yet (no e2e). Plugins include `rcbot_megahal.inc` / `rcbot_learn.inc` / `rcbot_nav.inc`
from `scripting/include/`.

## Natives

Each native is documented in its include. Handles (`RcHal`, `RcChat`, `RcLearn`, `RcGa`,
`RcPerceptron`, `RcNn`, `RcSom`, `RcPath`) are never 0, die at map change (plugins reload), and an invalid
one is a native error. File names are relative to `tsmod/rcbot/`; paths leaving it are refused.

### rcbot_megahal (`#include <rcbot_megahal>`)

| Native | What |
|---|---|
| `RcHal:hal_load(profile_id, const pretrain[] = "", const aux[] = "", const ban[] = "", const swap[] = "")` | wake a profile's brain as tsmod_rcbot does (new brain saved, then pretrained, when missing or damaged; profile 0 in memory only) |
| `bool:hal_save(RcHal:hal)` | write `botprofiles/<id>_hal.brn` |
| `bool:hal_free(&RcHal:hal, bool:save = false)` | free (and zero the variable) |
| `hal_learn(RcHal:hal, const text[])` | learn a line |
| `hal_reply(RcHal:hal, const text[], out[], len)` | MegaHAL's reply, learning nothing |
| `hal_bot_reply(RcHal:hal, message[], const bot_name[], const sender_name[], flags, sender_rep, num_clients, reply_percent, out[], len)` | BotChatReply (`HAL_*` flags); changes `message` in place as upstream |
| `hal_words(RcHal:hal)`, `hal_profile(RcHal:hal)` | dictionary size, profile id |
| `hal_seed(seed)` | seed the module's random numbers |
| `RcChat:chat_load(const file[] = "bot_chat.ini")`, `bool:chat_free(&RcChat:chat)` | chat lines |
| `chat_count(RcChat:chat, RcChatKind:kind, array_num)`, `chat_get(RcChat:chat, RcChatKind:kind, array_num, index, out[], len)` | read the lists |
| `chat_context(const chat_to[] = "", const map[] = "")`, `chat_context_player(const name[], rep, bool:enemy)` | the context `%n %r %g %b %e %m` are filled from |
| `chat_pick(RcChat:chat, RcChatKind:kind, array_num, out[], len, &bool:team_only)` | CBot::BotChat's pick and fill |
| `chat_rep_array(rep)`, `chat_humanize(text[], len)`, `chat_remove_name_tags(const name[], out[], len)`, `Float:chat_typing_time(const text[])` | helpers |

### rcbot_learn (`#include <rcbot_learn>`)

| Native | What |
|---|---|
| `rl_seed(seed)` | seed the module's random numbers |
| `rl_load_globals()`, `rl_save_globals()` | `team0-3.rld`, `tsweaps.rld`, `weaponstats.rwr`, `loadouts.rlo`, `botprofiles/players.rop` (the heatmap is `nav_heatmap_save`) |
| `RcLearn:rl_bot_load(profile_id, const name[])`, `bool:rl_bot_save(RcLearn:bot)`, `bool:rl_bot_free(&RcLearn:bot, bool:save = false)`, `rl_bot_key(RcLearn:bot, out[], len)` | a bot's `botprofiles/<key>.rld/.rwr/.rlo` |
| `rl_bot_on_spawn(RcLearn:bot, team)`, `rl_bot_on_death(RcLearn:bot, team, Float:fitness, bool:used_melee)`, `Float:rl_death_fitness(Float:survival_time, frags_since_death, team_score_since_death)` | SpawnInit / death breeding |
| `rl_bot_select_ts_weapons(RcLearn:bot, bool:disable_learn, ids[3])` | the loadout GA's pick |
| `bool:rl_bot_decide(RcLearn:bot, RlDecision:which, const Float:inputs[], num, &Float:output = 0.0)` | jump/duck/strafe/stunt/electrified perceptrons, weights from the genes |
| `Float:rl_bot_gene(RcLearn:bot, RlGenes:set, index)`, `rl_bot_genes(RcLearn:bot, RlGenes:set, Float:out[], len)` | read genes |
| `rl_ws_add_exposure/add_hit/add_shots(RcLearn:bot, weapon, Float:dist, Float:value)`, `bool:rl_ws_score(RcLearn:bot, weapon, Float:dist, &Float:score)`, `Float:rl_ws_samples(...)`, `bool:rl_ws_accuracy(..., &Float:accuracy)` | weapon effectiveness by range (bot 0: the prior) |
| `rl_loadout_count()`, `rl_loadout_weapons(arm, ids[], len)`, `rl_loadout_slots(arm)`, `rl_weapon_slots(weapon)`, `rl_loadout_choose(RcLearn:bot, capacity, bool:random)`, `rl_loadout_reward(RcLearn:bot, arm, Float:reward)`, `Float:rl_life_reward(kills, deaths, Float:dealt, Float:seconds)`, `rl_credited_loadout(bought, const owned[], num)` | the loadout bandit |
| `rl_opp_set_key(player, RlKeyKind:kind, id)`, `rl_opp_hit_on_bot`, `rl_opp_kill_of_bot`, `rl_opp_death_to_bot`, `rl_opp_exposure`, `rl_opp_stunt`, `rl_opp_kungfu`, `Float:rl_opp_threat(player)`, `rl_opp_profile(player, Float:out[], len)`, `rl_opp_new_map()` | the opponent model |
| `rl_ledger_record`, `rl_ledger_engage`, `rl_ledger_tick`, `rl_ledger_close`, `Float:rl_ledger_credit`, `Float:rl_ledger_dealt_since`, `Float:rl_ledger_taken_since`, `Float:rl_ledger_reward_since`, `rl_ledger_kills_since`, `rl_ledger_clear`, `rl_range_bin(Float:dist)` | the combat ledger |
| `rl_ga_create/free/add/can_pick/pick/best/generations/population/save/load` | generic GA (`CGA` file layout) |
| `rl_perceptron_create/free/execute/fired/output/train/weights/set_weights/randomize/save/load` | generic perceptron |
| `rl_nn_create/free/execute/train/weights/set_weights/randomize/save/load` | generic neural network |
| `rl_som_create/free/input/bmu/weights/epochs` | generic self-organising map (no file) |

### rcbot_nav (`#include <rcbot_nav>`)

One set of waypoints per server, the map's: `waypoints/ts/<map>.rcw` / `.rcv` / `.rhm`. Waypoint
arguments are indexes (an invalid one answers -1 / 0 / false, as RCBot's lookups do); teams are
RCBot's (rules team - 1, -1 none). Flags are `NAV_FL_*` (`W_FL_*`).

| Native | What |
|---|---|
| `nav_seed(seed)` | seed the module's random numbers |
| `bool:nav_load(const map[] = "")`, `bool:nav_save()`, `nav_count()`, `nav_clear()`, `nav_map(out[], len)` | `WaypointLoad` / `WaypointSave` (`""`: the current map, from the port); missing `.rcv` rows are traced a few per frame |
| `bool:nav_valid(wp)`, `bool:nav_origin(wp, Float:o[3])`, `nav_flags(wp)`, `Float:nav_distance(wp, const Float:from[3])` | a waypoint |
| `nav_nearest(origin, range, ignore, NAV_NEAREST_* flags, failed[], num)` | `CWaypointLocations::NearestWaypoint` (location buckets) |
| `nav_nearest_scan`, `nav_nearest_flag`, `nav_random_flag`, `nav_random_flag_near`, `nav_nearest_aiming`, `nav_find_reachable`, `bool:nav_reachable` | `WaypointFindNearest`, `...NearestGoal`, `...RandomGoal` (both), `...NearestAiming`, `...FindReachable`, `WaypointReachable` |
| `nav_paths_out(wp, out[], len, team)`, `nav_paths_in(wp, out[], len)`, `bool:nav_has_path(from, to, team)` | paths |
| `nav_cover(player[3], from[3], ignore[], num, team, bool:heatmap)` | `GetCoverWaypoint`, with the heatmap's cover penalty |
| `nav_buckets_around(origin, out[], len)`, `nav_nearby(origin, out[], len)` | location buckets |
| `bool:nav_visible(from, to)`, `nav_vis_pending()`, `nav_vis_budget(traces)`, `nav_vis_rebuild(bool:now)`, `nav_vis_finish()` | visibility table |
| `bool:nav_heatmap_load()`, `bool:nav_heatmap_save()`, `nav_heatmap_add_death/add_kill(wp, rules_team)`, `nav_heatmap_add_damage(wp, rules_team, Float:amount)`, `Float:nav_heatmap_danger/cover_penalty(wp, bot_team)`, `Float:nav_heatmap_ambush(wp)` | `waypoints/ts/<map>.rhm` |
| `RcPath:nav_path_create()`, `bool:nav_path_free(&RcPath:path)` | a bot's A* state |
| `NavPathStatus:nav_path_find(path, from, to, const Float:gun_pos[3], team, max_loops, NAV_FIND_* flags, bool:resume)`, `nav_path_continue(path)`, `nav_path_status(path)` | `BotNavigate_AStarAlgo`, incremental across frames (`NAV_PATH_SEARCHING`) |
| `nav_path_next`, `nav_path_pop`, `nav_path_push`, `nav_path_length`, `nav_path_get`, `nav_path_contains`, `nav_path_after`, `nav_path_clear`, `nav_path_reset` | the path stack, `GetNextWaypoint` |
| `nav_path_fail`, `nav_path_failed_count`, `nav_path_clear_failed_paths`, `nav_path_goal_reached`, `nav_path_failed_goals`, `nav_path_ignored_goals`, `nav_path_clear_failed_goals`, `nav_path_think` | failed paths (by `PATH` node) and goals, back-offs |
| `nav_path_set_weights`, `nav_path_set_threats`, `nav_path_add_threat`, `nav_path_threats`, `nav_path_clear_threats`, `nav_path_set_crowded`, `nav_path_crowd_from` | `NAV_FIND_TACTICS` inputs |
| `nav_path_touch`, `Float:nav_path_since_visit`, `nav_path_roam_goal(path, team, origin[3], claims[], num)` | exploration and roaming goals |
| `nav_add`, `nav_add_aiming`, `bool:nav_delete`, `nav_delete_near`, `nav_add_path`, `nav_remove_path`, `nav_remove_paths_to`, `nav_set_origin`, `nav_set_flags/add_flags/clear_flags` | editing (`WaypointAddOrigin` with auto flags and auto paths, ...) |
| `nav_edit_create_path/remove_path(player, origin[3], cmd)`, `bool:nav_auto_waypoint(player, ...)`, `nav_auto_reset(player)`, `bool:nav_can_stand` | the editor's per-player commands, auto-waypointing |
| `nav_draw_index(wp)`, `nav_draw_around(viewer, origin[3], bool:paths)`, `nav_draw_path(path, r, g, b)`, `nav_event_count()`, `NavEvent:nav_event_pop(start[3], end[3], params[7], text[], len)`, `nav_event_clear()` | draw data: beams, sounds, messages queued for the plugin to send |

## Differences from the standalone bot

- **Random numbers** are the module's seeded xorshift (`hal_seed`, `rl_seed`, `nav_seed`), not
  the engine's `RANDOM_LONG` sequence: the pure AMX Mod X modules have no engine table, and
  tsmod-amxx-rs v0.3.0 has no `pfnRandomLong`/`pfnRandomFloat` wrapper yet for rcbot_nav (one
  type, `state::NavRandom`, switches it when it does).
- **Visibility rows** a `.rcv` doesn't hold are traced over the following frames
  (`nav_vis_budget` traces each) rather than all at load; until then those rows read "not
  visible". `nav_save` finishes them first.
- **Strings** pass through AMX Mod X as UTF-8: bytes of names that aren't UTF-8 are replaced.
- The kung fu fighter's `<key>.rkf` isn't handled by the modules (Pawn kung fu phase).
- The global data, the waypoints and the heatmap are loaded and saved when the plugin asks
  (`rl_load_globals`, `nav_load`, `nav_heatmap_load` in `plugin_init`; `rl_save_globals`,
  `nav_heatmap_save` in `plugin_end`), not by the modules on their own.
- Waypoints have no radius in RCBot's format; touch distances are the Pawn bot's (by flag, as
  `bot/navigate.rs` `update_waypoint`).

## Building

See [AGENTS.md](AGENTS.md): `cargo xtask test | build | lint | dist | pawn`, with
`../xash3d-fwgs` (or `FWGS_DIR`) for the FWGS headers.

## Licence

GPL-3.0-or-later with RCBot's and AMX Mod X's linking exceptions: [LICENSE](LICENSE),
[GPL-EXCEPTION.txt](GPL-EXCEPTION.txt).
