# tsmod-amxx-rcbot

[RCBot1](https://github.com/APGRoboCop/rcbotold) (Cheeseh's Half-Life bot) hosted by **AMX Mod X**
for **The Specialists 3.0** on its
[Rust port](https://github.com/the-specialists-mod/the-specialistsmod-rs): reusable Rust AMX Mod X
modules for the bot's MegaHAL, learning and (later) navigation, with the bot logic as a Pawn
plugin that registers as the port's bot backend `rcbot_amxx`.

The modules are thin native layers over the `rcbot` library of
[tsmod-rcbot-rs](https://github.com/the-specialists-mod/tsmod-rcbot-rs) (the standalone
`tsmod_rcbot` bot), and share its data files byte for byte in `<gamedir>/tsmod/rcbot/`: a brain
or learned file written by one is read by the other.

## Status

| Part | State |
|---|---|
| Workspace, licences, xtask (build, dist, lint, test, pawn), release workflow | done |
| `rcbot_megahal`: MegaHAL brains (`botprofiles/<id>_hal.brn`, keyword lists, pretraining), `bot_chat.ini` lines filled from a plugin-supplied context, BotChatReply, typos | done |
| `rcbot_learn`: global learned files, per-bot learned data (`.rld`/`.rwr`/`.rlo`), spawn/death breeding, decision perceptrons, weapons by range, loadout bandit, opponent model, heatmap, combat ledger, generic GA/perceptron/NN/SOM | done |
| `rcbot_nav`: waypoints, visibility, A* | later (needs new engine wrappers in tsmod-amxx-rs) |
| `scripting/rcbot_amxx.sma`: the bot logic in Pawn, backend `rcbot_amxx` | later |
| End-to-end tests on the amxx-testenv roots | later |

## Install

From a release zip (`tsmod-amxx-rcbot-<tag>-<platform>.zip`): copy `addons/` over the server's
`ts/addons/`. The modules load on demand (`#pragma reqlib`), or list `rcbot_megahal` and
`rcbot_learn` in `configs/modules.ini`. They are pure AMX Mod X modules (no metamod plugin),
built on the module SDK tsmod-amxx-rs's modules use (AMX Mod X 1.8.2 and later); they have not
been run on a server yet (no e2e). Plugins include `rcbot_megahal.inc` / `rcbot_learn.inc` from
`scripting/include/`.

## Natives

Each native is documented in its include. Handles (`RcHal`, `RcChat`, `RcLearn`, `RcGa`,
`RcPerceptron`, `RcNn`, `RcSom`) are never 0, die at map change (plugins reload), and an invalid
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
| `rl_load_globals()`, `rl_save_globals()` | `team0-3.rld`, `tsweaps.rld`, `weaponstats.rwr`, `loadouts.rlo`, `botprofiles/players.rop` (+ the heatmap on save) |
| `RcLearn:rl_bot_load(profile_id, const name[])`, `bool:rl_bot_save(RcLearn:bot)`, `bool:rl_bot_free(&RcLearn:bot, bool:save = false)`, `rl_bot_key(RcLearn:bot, out[], len)` | a bot's `botprofiles/<key>.rld/.rwr/.rlo` |
| `rl_bot_on_spawn(RcLearn:bot, team)`, `rl_bot_on_death(RcLearn:bot, team, Float:fitness, bool:used_melee)`, `Float:rl_death_fitness(Float:survival_time, frags_since_death, team_score_since_death)` | SpawnInit / death breeding |
| `rl_bot_select_ts_weapons(RcLearn:bot, bool:disable_learn, ids[3])` | the loadout GA's pick |
| `bool:rl_bot_decide(RcLearn:bot, RlDecision:which, const Float:inputs[], num, &Float:output = 0.0)` | jump/duck/strafe/stunt/electrified perceptrons, weights from the genes |
| `Float:rl_bot_gene(RcLearn:bot, RlGenes:set, index)`, `rl_bot_genes(RcLearn:bot, RlGenes:set, Float:out[], len)` | read genes |
| `rl_ws_add_exposure/add_hit/add_shots(RcLearn:bot, weapon, Float:dist, Float:value)`, `bool:rl_ws_score(RcLearn:bot, weapon, Float:dist, &Float:score)`, `Float:rl_ws_samples(...)`, `bool:rl_ws_accuracy(..., &Float:accuracy)` | weapon effectiveness by range (bot 0: the prior) |
| `rl_loadout_count()`, `rl_loadout_weapons(arm, ids[], len)`, `rl_loadout_slots(arm)`, `rl_weapon_slots(weapon)`, `rl_loadout_choose(RcLearn:bot, capacity, bool:random)`, `rl_loadout_reward(RcLearn:bot, arm, Float:reward)`, `Float:rl_life_reward(kills, deaths, Float:dealt, Float:seconds)`, `rl_credited_loadout(bought, const owned[], num)` | the loadout bandit |
| `rl_opp_set_key(player, RlKeyKind:kind, id)`, `rl_opp_hit_on_bot`, `rl_opp_kill_of_bot`, `rl_opp_death_to_bot`, `rl_opp_exposure`, `rl_opp_stunt`, `rl_opp_kungfu`, `Float:rl_opp_threat(player)`, `rl_opp_profile(player, Float:out[], len)`, `rl_opp_new_map()` | the opponent model |
| `bool:rl_heatmap_load(const map[], num_waypoints)`, `bool:rl_heatmap_save()`, `rl_heatmap_add_death/add_kill(waypoint, rules_team)`, `rl_heatmap_add_damage(waypoint, rules_team, Float:amount)`, `Float:rl_heatmap_danger/cover_penalty(waypoint, bot_team)`, `Float:rl_heatmap_ambush(waypoint)` | `waypoints/ts/<map>.rhm` |
| `rl_ledger_record`, `rl_ledger_engage`, `rl_ledger_tick`, `rl_ledger_close`, `Float:rl_ledger_credit`, `Float:rl_ledger_dealt_since`, `Float:rl_ledger_taken_since`, `Float:rl_ledger_reward_since`, `rl_ledger_kills_since`, `rl_ledger_clear`, `rl_range_bin(Float:dist)` | the combat ledger |
| `rl_ga_create/free/add/can_pick/pick/best/generations/population/save/load` | generic GA (`CGA` file layout) |
| `rl_perceptron_create/free/execute/fired/output/train/weights/set_weights/randomize/save/load` | generic perceptron |
| `rl_nn_create/free/execute/train/weights/set_weights/randomize/save/load` | generic neural network |
| `rl_som_create/free/input/bmu/weights/epochs` | generic self-organising map (no file) |

## Differences from the standalone bot

- **Random numbers** are the module's seeded xorshift (`hal_seed`, `rl_seed`), not the engine's
  `RANDOM_LONG` sequence: a pure AMX Mod X module has no engine table.
- **Strings** pass through AMX Mod X as UTF-8: bytes of names that aren't UTF-8 are replaced.
- The kung fu fighter's `<key>.rkf` isn't handled by the modules (Pawn kung fu phase).
- The global data is loaded and saved when the plugin asks (`rl_load_globals` in `plugin_init`,
  `rl_save_globals` in `plugin_end`), not by the module on its own.

## Building

See [AGENTS.md](AGENTS.md): `cargo xtask test | build | lint | dist | pawn`, with
`../xash3d-fwgs` (or `FWGS_DIR`) for the FWGS headers.

## Licence

GPL-3.0-or-later with RCBot's and AMX Mod X's linking exceptions: [LICENSE](LICENSE),
[GPL-EXCEPTION.txt](GPL-EXCEPTION.txt).
