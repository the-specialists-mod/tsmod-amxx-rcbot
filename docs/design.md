# Design: the Pawn bot

How `scripting/rcbot_amxx.sma` maps onto tsmod-rcbot-rs's bot (`../tsmod-rcbot-rs`, the
standalone `tsmod_rcbot`), and where it differs. Conventions and file ownership:
[pawn.md](pawn.md).

## 1. Architecture

| tsmod-rcbot-rs | Here |
|---|---|
| `crates/host` (plugin API v7: `register_bot_backend`, `start_frame`, callbacks) | `rcbot_amxx.sma` + `backend.inc`, `manager.inc`, `world.inc`, `files.inc` over tsx's `tsbot.inc` (tsmod-amxx-rs v0.3.0) and AMX Mod X's engine/fakemeta |
| `rcbot::bot` (`Bot`, `BotManager`) | `scripting/rcbot/<module>.inc`, one per `bot/*.rs`; `struct Bot` is `state.inc` |
| `rcbot::config`, `rcbot::weapons` | `config.inc`, `commands.inc`, `menu.inc`, `profile.inc`, `rep.inc`, `users.inc`, `client.inc`; `weapons.inc`, `tsweapons.inc` |
| `rcbot::chat` | `rcbot_megahal` natives (`chat.inc` is the glue) |
| `rcbot::learn` | `rcbot_learn` natives (`learning.inc`) |
| `rcbot::nav` (waypoints, A*, heatmap, editor) | `rcbot_nav` natives (`navigate.inc`); each bot's A* state is an `RcPath` |

### The frame (host lib.rs `start_frame`, lib.rs:489-578)

`ts_frame` → `manager_frame`:

1. `world_refresh_frame`: frame info, team scores, every slot's snapshot (once), the masters
   (first frame of a map).
2. `navigate_editor_frame`, `commands_force_grip_frame`, `config_map_config_frame`.
3. Not the active backend: stop.
4. `backend_rejoin_frame` (min_bots / max_bots / bots_leave_and_join).
5. `manager_think_all` (`BotManager::frame`): per bot in index order, squad orders, the camera's
   other-has-enemy, leaving, then `manager_bot_frame` (`Bot::frame`: snapshot → view / teleport
   check, weapon sync, kick / kill, `think_bot`, `movement_run_player_move`). Then squad orders
   and headings.
6. `squad_flush_messages`, `botcam_frame`.
7. `manager_act_all`: per bot, its actions in order (command, select weapon with
   `weapon_select_failed` on `TSBOT_INVALID`, loadout = clear + buy ids 1..37 except 29 with 4
   ammo, kill, kick), then `ts_bot_runcmd` (`TSBOT_SETVIEW | TSBOT_AUTOMSEC`) and
   `bot_command_ran` (fixangle view, teleport).
8. `report_frame` (e2e only).

Every bot thinks before any acts, so all read the same frame.

### Events

| Host callback | Here |
|---|---|
| `client_put_in_server` / `client_disconnect` | `client_putinserver` / `client_disconnected` → `backend_put_in_server` / `backend_disconnect` |
| `player_killed` | `ts_player_killed` → `manager_death` (`BotManager::death`) |
| `damage` | `ts_damage` → `manager_damage` (hurt bot turns; learners) |
| `kill_scored` | none in tsx: `learning_kill_event` builds the kill from `ts_player_killed` and the victim's last `ts_damage` (TODO(learning)) |
| `sound` | `ts_sound` → `manager_sound` |
| `entity_touch` | `ts_bot_touch` → `manager_touch` |
| `weapon_fired` | `ts_weapon_fired` → `manager_weapon_fired` |
| `item_pickup` | `ts_item_pickup` → `manager_item_pickup` |
| `player_stunt` | tsfun's `client_stunt` → `learning_player_stunt` |
| `client_command` say / say_team | `say` / `say_team` client commands → `chat_say_command` |
| `add_bot` / `activate` / `deactivate` / `adopt_bot` | `ts_bot_add` / `ts_bot_activate` / `ts_bot_deactivate` / `ts_bot_adopt` |
| `server_deactivate` / `shutdown` | `plugin_end` (`backend_save_all`, heatmap, squads, camera) |

## 2. Differences from tsmod-rcbot-rs (the plugin host)

* **Data folder**: RCBot's files are read and written in `tsmod/rcbot/` (the modules' and the
  standalone plugin's folder, byte-compatible). `ts_bot_data_dir()` (`tsmod/rcbot_amxx/`) is
  only a fallback for reading the ini files.
* **Profiles across a map change** (`previous_profiles`): AMX Mod X reloads the plugin, so they
  pass through the `rcbot_amxx_prof` localinfo key (`backend_store_prev_profiles`).
* **Random numbers**: AMX Mod X's `random_num` / `random_float`, not the engine's `RANDOM_LONG`
  sequence (the modules have their own seeded generators too).
* **Kill events**: tsx has no `kill_scored` forward (weapon, headshot and kung fu tags of a kill);
  they come from `ts_player_killed` and the last `ts_damage`.
* **EntRef serials**: players' are user ids (as the host's); items' are `TSITM_SERIAL`; other
  entities have none (index only), since Pawn can't read an edict's serial number.
* **Placeholder**: until think/combat/look/movement are ported, `minimal.inc` drives living bots
  (wander, jump, shoot the nearest visible enemy) and buys an AK-47 and a Glock 18.

## 3. Rust → Pawn map

Generated from the stubs' comments when the skeleton was made; owners keep their rows current
(status `ported`, `partial`, `stub`, `placeholder`). Rust paths are relative to
`../tsmod-rcbot-rs/crates/rcbot/src/bot/` unless they name `host`, `config/` or `weapons/`.

### think.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| think.rs Bot::think (CBot::Think): the living branch calls the subsystems in think.rs's order; `combat_find_enemy`'s index gets its serial here | `think.inc` `think_bot` | ported |
| think.rs Bot::think's weapon upkeep (drop an empty gun, switch or reload an empty clip, reload when low) | `think.inc` `think_weapon_upkeep` | ported |
| think.rs Bot::think's can-see-the-next-waypoint check | `think.inc` `think_see_waypoint` | ported |
| think.rs Bot::think's dead branch | `think.inc` `think_dead` | ported |
| think.rs Bot::spawn_init (CBot::SpawnInit(false)) | `think.inc` `think_spawn_init` | ported |
| think.rs Bot::select_loadout (rl_bot_select_ts_weapons, then learning's bandit loadout) | `think.inc` `think_select_loadout` | ported |
| think.rs Bot::look_for_new_tasks | `think.inc` `think_look_for_new_tasks` | ported |
| think.rs Bot::do_tasks (the task runner) | `think.inc` `think_do_tasks` | ported |
| think.rs task_find_path / path_deadline / push_current_on_path / nearest_to_origin | `think.inc` `think_task_find_path` (static) | ported |
| think.rs task_normal_attack (the stunt perceptron is rl_bot_decide RlDecide_Stunt) | `think.inc` `think_task_normal_attack` (static) | ported |
| think.rs task_use / task_listen; do_tasks' MOVE_TO_VECTOR | `think.inc` `think_task_use`, `think_task_listen`, `think_task_move_to_vector` (static) | ported |
| think.rs Bot::learn_items (items_scan, items_learn for combat's visibles, items_look) | `think.inc` `think_learn_items` | ported |
| think.rs Bot::switch_weapon | `think.inc` `think_switch_weapon` | ported |
| think.rs Bot::drop_roaming_for_sound | `think.inc` `think_drop_roaming_for_sound` (static) | ported |
| think.rs Bot::objective_tasks / objective_role / defend_spot / ctb_tasks (second argument: the schedule id) | `think.inc` `think_objective_tasks` | ported |
| think.rs Bot::can_pickup (the classname test is tsx's entity kind, TSENT_POWERUP / TSENT_GROUND_WEAPON) | `think.inc` `think_can_pickup` | ported |
| think.rs Bot::facing | `think.inc` `think_facing` | ported |
| think.rs is_grenade_rocket | `think.inc` `think_is_grenade_rocket` | ported |
| think.rs trace_toss | `think.inc` `think_trace_toss` | ported |
| mod.rs Entity::handle's serial (players' user ids, items' TSITM_SERIAL), EntRef equality, Bot::resolve, Entity::is_alive / is_moving, Bot::distance_from, nearest_waypoint, failed goals, flags::team_allows, add_task with a timeout | `think.inc` `think_handle_serial`, `think_same_handle`, `think_resolve`, `think_entity_alive`, `think_entity_moving`, `think_distance_from`, `think_nearest_waypoint`, `think_failed_goals`, `think_team_allows`, `think_add_timed` | ported |

### tasks.inc

The queue holds `RC_MAX_TASKS` (16): a task added to the end of a full queue is dropped, one added
to the front drops the last task instead (the Rust queue is unbounded).

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| Tasks::flush. | `tasks.inc` `tasks_flush` | ported |
| Tasks::is_empty / len. | `tasks.inc` `tasks_count` | ported |
| Tasks::current's kind, TASK_NONE for none. | `tasks.inc` `tasks_current_kind` | ported |
| Task::new + Tasks::add_to_end (AddTask) | `tasks.inc` `tasks_add` | ported |
| Tasks::add_priority (AddPriorityTask). | `tasks.inc` `tasks_add_priority` | ported |
| Tasks::add_to_front (with remove_same_task, Task::same). | `tasks.inc` `tasks_add_to_front` | ported |
| Tasks::has_task. | `tasks.inc` `tasks_has` | ported |
| Tasks::get_task | `tasks.inc` `tasks_find` | ported |
| Tasks::has_schedule. | `tasks.inc` `tasks_has_schedule` | ported |
| Tasks::new_schedule_id. | `tasks.inc` `tasks_new_schedule_id` | ported |
| Tasks::finish_schedule. | `tasks.inc` `tasks_finish_schedule` | ported |
| Tasks::finish_schedule_desc. | `tasks.inc` `tasks_finish_schedule_desc` | ported |
| Tasks::give_schedule_description. | `tasks.inc` `tasks_give_schedule_description` | ported |
| Tasks::set_time_to_complete_schedule (secs from now). | `tasks.inc` `tasks_set_time_to_complete_schedule` | ported |
| Tasks::remove_timed_out_schedules. | `tasks.inc` `tasks_remove_timed_out_schedules` | ported |
| Tasks::remove_similar_task. | `tasks.inc` `tasks_remove_similar` | ported |
| Tasks::remove_paths. | `tasks.inc` `tasks_remove_paths` | ported |
| Tasks::finished_current (done or failed; find-path hands its path on). | `tasks.inc` `tasks_finished_current` | ported |
| Tasks::forget_edict. | `tasks.inc` `tasks_forget_edict` | ported |
| Task::timed_out. | `tasks.inc` `tasks_timed_out` | ported |
| Task::set_time_to_complete / with_timeout (a slot, -1 ignored). | `tasks.inc` `tasks_set_time_to_complete` | ported |
| TaskKind::description (CBotTask::_getTaskDescription) into out. | `tasks.inc` `tasks_description` | ported |

### goals.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| goals.rs score | `goals.inc` `goals_score` | ported |
| goals.rs Bot::weapon_value (priorities: weapons.inc's WP_PRIORITY) | `goals.inc` `goals_weapon_value` | ported |
| goals.rs Bot::powerup_value. | `goals.inc` `goals_powerup_value` | ported |
| goals.rs Bot::item_value (a KnownItem, items.inc slot). | `goals.inc` `goals_item_value` | ported |
| goals.rs Bot::danger_near (nav_heatmap_danger). | `goals.inc` `goals_danger_near` | ported |
| goals.rs Bot::choose_goal: GOAL_*, arg, vec, score, and a hunt's sighting time (`when`) | `goals.inc` `goals_choose` | ported |
| goals.rs Bot::hold_spot: the waypoint (-1 none) and its value | `goals.inc` `goals_hold_spot` | ported |
| goals.rs Bot::claim_near. | `goals.inc` `goals_claim_near` (static) | ported |
| goals.rs Bot::fight_readiness. | `goals.inc` `goals_fight_readiness` | ported |
| goals.rs Bot::predict_enemy(origin, secs): the waypoint, -1 none | `goals.inc` `goals_predict_enemy` | ported |
| goals.rs Bot::claim_item_factor (board_item_claimed). | `goals.inc` `goals_claim_item_factor` | ported |
| goals.rs Bot::regroup_point (board_sighting, board_mates). | `goals.inc` `goals_regroup_point` | ported |

### special.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| special.rs Bot::touch (DispatchTouch for a living bot) | `special.inc` `special_touch` | ported |
| special.rs Bot::player_standing_on_me | `special.inc` `special_player_standing_on_me` | ported |
| special.rs Bot::standing_on_player | `special.inc` `special_standing_on_player` | ported |
| special.rs Bot::check_lift. | `special.inc` `special_check_lift` | ported |
| special.rs Bot::touched_pushable_waypoint. | `special.inc` `special_touched_pushable_waypoint` | ported |
| special.rs Bot::next_waypoint_tasks. | `special.inc` `special_next_waypoint_tasks` | ported |
| special.rs Bot::touched_human_tower. | `special.inc` `special_touched_human_tower` | ported |
| special.rs find_nearest_button / master_find_button | `special.inc` `special_find_nearest_button` | ported |
| special.rs find_nearest_entity (by classname list), 0 none. | `special.inc` `special_find_nearest_entity` | ported |
| special.rs player_near_vector, 0 none. | `special.inc` `special_player_near_vector` | ported |
| special.rs is_button, best_pushable_distance, desired_pushable_vector, acceptable_pushable_vector | `special.inc` (static) | ported |
| special.rs Bot::task_push_pushable (task slot 0). | `special.inc` `special_task_push_pushable` | ported |
| special.rs Bot::task_wait_for_lift. | `special.inc` `special_task_wait_for_lift` | ported |
| special.rs Bot::task_wait_for_entity. | `special.inc` `special_task_wait_for_entity` | ported |
| special.rs Bot::task_human_tower. | `special.inc` `special_task_human_tower` | ported |
| special.rs Bot::task_wait_for_bot_at_wpt (OtherBot: one of our bots, by user id). | `special.inc` `special_task_wait_for_bot_at_wpt` | ported |
| special.rs Bot::task_face_edict. | `special.inc` `special_task_face_edict` | ported |
| special.rs Bot::task_throw_grenade | `special.inc` `special_task_throw_grenade` | partial: look_turn_to has no turn-time argument (Rust turn_to(aim, now, 0.04)) |

### look.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| think.rs Bot::teleported, look's part | `look.inc` `look_teleported` | ported |
| think.rs Bot::spawn_init, look's part | `look.inc` `look_spawn_init` | ported |
| look.rs Bot::work_view_angles (CBot::WorkViewAngles) | `look.inc` `look_work_view_angles` | ported |
| look.rs Bot::set_view_angles (CBot::SetViewAngles) | `look.inc` `look_set_view_angles` | ported |
| look.rs Bot::yaw_angle_to | `look.inc` `look_yaw_angle_to` | ported |
| look.rs Bot::dot_from_origin | `look.inc` `look_dot_from_origin` | ported |
| look.rs Bot::in_view_cone | `look.inc` `look_in_view_cone` | ported |
| look.rs Bot::aim_point (design §4: chest, head for skilled bots with precise guns) | `look.inc` `look_aim_point` | ported |
| look.rs Bot::aim_vector (GetAimVector rewritten: lead, angular error, settling) | `look.inc` `look_aim_vector` | ported |
| look.rs Bot::turn_to (the aimed turn: time constant, feed-forward, max rate) | `look.inc` `look_turn_to` | ported |
| math.rs Vector::normalize (zero becomes (0, 0, 1), unlike math.inc's vec_normalize) | `look.inc` `look_normalize` | ported |

### preaim.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| preaim.rs PreAim::default (a new bot) | `preaim.inc` `preaim_bot_init` | ported |
| preaim.rs Bot::watch | `preaim.inc` `preaim_watch` | ported |
| preaim.rs Bot::pre_aim_target (nav_visible, nav_heatmap_danger) | `preaim.inc` `preaim_target` | ported |
| preaim.rs Bot::update_pre_aim | `preaim.inc` `preaim_update` | ported |
| preaim.rs Bot::pre_aim_look | `preaim.inc` `preaim_look` | ported |

### combat.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| think.rs Bot::spawn_init, combat's part | `combat.inc` `combat_spawn_init` | ported |
| mod.rs Bot::new's Visibles and range costs (a new bot) | `combat.inc` `combat_bot_init` | ported |
| combat.rs Visibles::contains (IsInVisibleList) | `combat.inc` `combat_is_visible` | ported |
| combat.rs Visibles::set (AddVisible / RemoveVisible; a full list drops the newest) | `combat.inc` `combat_set_visible` | ported |
| combat.rs Bot::fvisible (FVisible: middle, then eyes; see body / head) | `combat.inc` `combat_fvisible` | ported |
| combat.rs Bot::fvisible_point | `combat.inc` `combat_fvisible_point` | ported |
| combat.rs box_in_sphere | `combat.inc` `combat_box_in_sphere` | ported |
| combat.rs Bot::is_enemy (players, tm_ model teams by model index, breakables) | `combat.inc` `combat_is_enemy` | ported |
| combat.rs Bot::breakable_is_enemy (BotFunc_BreakableIsEnemy) | `combat.inc` `combat_breakable_is_enemy` | ported |
| combat.rs Bot::can_avoid (CanAvoid, TS) | `combat.inc` `combat_can_avoid` | ported |
| combat.rs Bot::update_visibles (the vision pass, vision revs per call) | `combat.inc` `combat_update_visibles` | partial: no PVS pre-test until world.inc's world_in_pvs compiles (the clear line decides alone) |
| combat.rs Bot::is_inanimate | `combat.inc` `combat_is_inanimate` | ported |
| combat.rs Bot::work_enemy_costs (survival gene via rl_bot_gene) | `combat.inc` `combat_work_enemy_costs` | ported |
| combat.rs Bot::clear_enemy_costs | `combat.inc` `combat_clear_enemy_costs` | ported |
| events.rs Bot::rep_of (Option: client.inc's rep id, the profile's reps) | `combat.inc` `combat_rep_of` | ported |
| combat.rs Bot::find_enemy (FindEnemy rewritten: threat scores, 25% to switch) | `combat.inc` `combat_find_enemy` | ported |
| combat.rs Bot::enemy_found | `combat.inc` `combat_enemy_found` | partial: the bot camera hand-off (BotCam::set_current_bot) waits for a botcam.inc stock |
| combat.rs Bot::enemy_down | `combat.inc` `combat_enemy_down` | ported |
| combat.rs Bot::decide_jump_duck_strafe (rl_bot_decide) | `combat.inc` `combat_decide_jump_duck_strafe` | ported |
| combat.rs Bot::want_to_follow_enemy | `combat.inc` `combat_want_to_follow_enemy` | ported |
| combat.rs Bot::run_for_cover (nav_cover with the heatmap, nav_nearest) | `combat.inc` `combat_run_for_cover` | ported |
| think.rs trace_toss | `combat.inc` `combat_trace_toss` | ported (local copy until think.inc exposes one) |
| combat.rs Bot::flee_grenades (design §4: any within 400 units) | `combat.inc` `combat_flee_grenades` | ported |
| combat.rs Bot::remember_lost_enemy | `combat.inc` `combat_remember_lost_enemy` | ported |
| combat.rs Bot::update_conditions | `combat.inc` `combat_update_conditions` | ported |
| combat.rs Bot::best_weapon (GetBestWeaponId; need_weapons) | `combat.inc` `combat_best_weapon` | ported |
| mod.rs Bot::hurt_by (BotEvent HURT for a damage report) | `combat.inc` `combat_hurt_by` | ported |
| mod.rs Bot::hurt | `combat.inc` `combat_hurt` | ported |
| Entity fields (exists, serial, is_client, is_alive, origin, view_ofs, velocity, v_angle, boxes, size, health, effects) | `combat.inc` `combat_ent_*` | ported |

### fire.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| think.rs Bot::spawn_init (fight = Fight::default()) | `fire.inc` `fire_spawn_init` | ported |
| fire.rs Bot::gun_in_hand | `fire.inc` `fire_gun_in_hand` | ported |
| fire.rs Bot::double_fire_rate | `fire.inc` `fire_double_fire_rate` | ported |
| fire.rs Bot::skill_share | `fire.inc` `fire_skill_share` | ported |
| fire.rs Bot::track_shots | `fire.inc` `fire_track_shots` | ported |
| fire.rs Bot::fire_buttons (fresh presses, akimbo, spread limit, dive and roll) | `fire.inc` `fire_buttons` | ported |
| fire.rs Bot::safe_to_reload | `fire.inc` `fire_safe_to_reload` | ported |
| fire.rs Bot::in_aim | `fire.inc` `fire_in_aim` | ported |
| fire.rs Bot::capped_speeds | `fire.inc` `fire_capped_speeds` | ported |
| fire.rs Bot::spread_limit | `fire.inc` `fire_spread_limit` | ported |
| fire.rs Bot::spread_ok | `fire.inc` `fire_spread_ok` | ported |
| fire.rs Bot::ready_to_fire | `fire.inc` `fire_ready_to_fire` | ported |
| fire.rs Bot::combat_move | `fire.inc` `fire_combat_move` | ported |
| fire.rs Bot::dodge | `fire.inc` `fire_dodge` | ported |
| fire.rs Bot::saw_enemy | `fire.inc` `fire_saw_enemy` | ported |
| fire.rs Bot::enemy_out_of_sight | `fire.inc` `fire_enemy_out_of_sight` | ported |
| fire.rs Bot::lost_aim_point | `fire.inc` `fire_lost_aim_point` | ported |
| fire.rs Bot::hold_angle | `fire.inc` `fire_hold_angle` | ported |
| fire.rs Bot::threat (with a known rep) | `fire.inc` `fire_threat_rep` | ported |
| fire.rs Bot::threat | `fire.inc` `fire_threat` | ported |
| fire.rs Bot::weapon_score | `fire.inc` `fire_weapon_score` | ported |
| fire.rs Bot::learned_score (learning_weapon_score) | `fire.inc` `fire_learned_score` | ported |
| fire.rs Bot::best_scored_weapon | `fire.inc` `fire_best_scored_weapon` | ported |
| fire.rs Bot::choose_weapon | `fire.inc` `fire_choose_weapon` | ported |
| fire.rs Bot::use_powerup | `fire.inc` `fire_use_powerup` | ported |
| fire.rs Bot::grenade_behind_cover | `fire.inc` `fire_grenade_behind_cover` | ported |
| fire.rs throw_pitch: (pitch, miss) | `fire.inc` `fire_throw_pitch` | ported |

### botweapons.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| mod.rs Bot::weapon_select_failed | `botweapons.inc` `botweapons_select_failed` | ported |
| think.rs Bot::frame: TS bots always have kung fu | `botweapons.inc` `botweapons_ensure_kungfu` | ported |
| think.rs Bot::spawn_init's weapon part | `botweapons.inc` `botweapons_spawn_init` | ported |
| bot/weapons.rs BotWeapon::set_weapon (SetWeapon) | `botweapons.inc` `botweapons_set_weapon` | ported |
| BotWeapons::add (AddWeapon) | `botweapons.inc` `botweapons_add` | ported |
| BotWeapons::remove | `botweapons.inc` `botweapons_remove` | ported |
| BotWeapons::has | `botweapons.inc` `botweapons_has` | ported |
| mod.rs Bot::new's BotWeapons::default (a new bot) | `botweapons.inc` `botweapons_bot_init` | ported |
| think.rs Bot::frame's weapon block (BotClient_TS_WeaponInfo, inventory sync, drop on empty) | `botweapons.inc` `botweapons_frame_sync` | ported |
| bot/weapons.rs BotWeapons::best_loaded_gun | `botweapons.inc` `botweapons_best_loaded_gun` | ported |
| bot/weapons.rs BotWeapons::record | `botweapons.inc` `botweapons_record` | ported |
| bot/weapons.rs BotWeapons::weapon_info | `botweapons.inc` `botweapons_weapon_info` | ported |
| bot/weapons.rs BotWeapons::sync_inventory (world_inventory) | `botweapons.inc` `botweapons_sync_inventory` | ported |
| bot/weapons.rs BotWeapons::best_weapon_id (enemy_dist < 0: none; need_weapons) | `botweapons.inc` `botweapons_best_weapon_id` | ported |
| bot/weapons.rs BotWeapon::is_melee | `botweapons.inc` `botweapons_is_melee` | ported |
| bot/weapons.rs BotWeapon::clip_is_empty | `botweapons.inc` `botweapons_clip_is_empty` | ported |
| bot/weapons.rs BotWeapon::out_of_ammo | `botweapons.inc` `botweapons_out_of_ammo` | ported |
| bot/weapons.rs BotWeapon::low_on_ammo | `botweapons.inc` `botweapons_low_on_ammo` | ported |
| bot/weapons.rs BotWeapon::can_reload | `botweapons.inc` `botweapons_can_reload` | ported |
| bot/weapons.rs BotWeapon::need_to_reload | `botweapons.inc` `botweapons_need_to_reload` | ported |
| bot/weapons.rs BotWeapon::can_be_used_underwater | `botweapons.inc` `botweapons_can_be_used_underwater` | ported |
| bot/weapons.rs BotWeapon::priority | `botweapons.inc` `botweapons_priority` | ported |
| bot/weapons.rs BotWeapon::can_shoot_primary | `botweapons.inc` `botweapons_can_shoot_primary` | ported |
| mod.rs Bot::reload (design §4: once per RELOAD_RETRY, never spammed) | `botweapons.inc` `botweapons_reload` | ported |

### weapons.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| host lib.rs game_init: WeaponPresets::parse(bot_weapons.ini, MOD_TS) and its warnings | `weapons.inc` `weapons_read_presets` | ported |
| stoi.rs stoi (std::stoi) | `weapons.inc` `weapons_stoi` | ported |
| weapons/mod.rs Weapons::clear | `weapons.inc` `weapons_clear` | ported |
| weapons/mod.rs Weapons::add (record's registration, with MOD_TS's preset) | `weapons.inc` `weapons_add` | ported |
| weapons/mod.rs Weapons::get(w).is_some() | `weapons.inc` `weapons_registered` | ported |
| weapons/mod.rs WeaponInfo::can_be_used_underwater / can_use_primary / can_use_secondary / priority / is_melee | `weapons.inc` `weapons_can_be_used_underwater` | ported |
|  | `weapons.inc` `weapons_can_use_primary` | ported |
|  | `weapons.inc` `weapons_can_use_secondary` | ported |
|  | `weapons.inc` `weapons_priority` | ported |
|  | `weapons.inc` `weapons_is_melee` | ported |
| weapons/mod.rs WeaponInfo::primary_in_range | `weapons.inc` `weapons_primary_in_range` | ported |
| weapons/mod.rs WeaponInfo::secondary_in_range | `weapons.inc` `weapons_secondary_in_range` | ported |
| weapons/mod.rs WeaponInfo::prim_max_range / prim_min_range | `weapons.inc` `weapons_prim_max_range` | ported |
|  | `weapons.inc` `weapons_prim_min_range` | ported |

### tsweapons.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| TsWeapon::from_game_id | `tsweapons.inc` `ts_weapon_from_game_id` | ported |
| The game's id of an RCBot id (ts_bot_select_weapon, ts_bot_buy) | `tsweapons.inc` `ts_weapon_to_game_id` | ported |
| ts_data.rs ROWS / AMMO / gun_data (once at plugin_init) | `tsweapons.inc` `tsweapons_init` | ported |
| ts_data.rs gun_data(id).is_some() | `tsweapons.inc` `tsweapons_known` | ported |
| ts_data.rs GunData::is_melee_or_thrown | `tsweapons.inc` `tsweapons_is_melee_or_thrown` | ported |
| ts_data.rs GunData::needs_fresh_press | `tsweapons.inc` `tsweapons_needs_fresh_press` | ported |
| ts_data.rs GunData::press_interval | `tsweapons.inc` `tsweapons_press_interval` | ported |
| ts_data.rs GunData::range_multiplier | `tsweapons.inc` `tsweapons_range_multiplier` | ported |
| ts_data.rs GunData::grow_penalty | `tsweapons.inc` `tsweapons_grow_penalty` | ported |
| ts_data.rs decay_penalty | `tsweapons.inc` `tsweapons_decay_penalty` | ported |

### movement.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| movement.rs Bot::run_player_move (CBot::RunPlayerMove) | `movement.inc` `movement_run_player_move` | ported |
| mod.rs Bot::jump / duck / use_button / alt_button. | `movement.inc` `movement_jump` | ported |
|  | `movement.inc` `movement_duck` | ported |
|  | `movement.inc` `movement_use` | ported |
|  | `movement.inc` `movement_alt` | ported |
| mod.rs Bot::primary_attack | `movement.inc` `bot_primary_attack` | ported |
| mod.rs Bot::secondary_attack. | `movement.inc` `bot_secondary_attack` | ported |
| mod.rs Bot::stop_moving. | `movement.inc` `movement_stop_moving` | ported |
| mod.rs Bot::set_move_vector. | `movement.inc` `movement_set_move_vector` | ported |
| mod.rs Bot::duck_and_jump. | `movement.inc` `movement_duck_and_jump` | ported |
| mod.rs Bot::jump_and_duck. | `movement.inc` `movement_jump_and_duck` | ported |
| think.rs Bot::teleported, movement's part | `movement.inc` `movement_teleported` | ported |
|  | `movement.inc` `movement_reset_unstick` | ported |
| think.rs Bot::spawn_init, movement's part. | `movement.inc` `movement_spawn_init` | ported |
| mod.rs Bot::distance_from (DistanceFrom, from the gun position) | `movement.inc` `movement_distance_from` | ported |
| Entity::size for the bot (snapshot absmax - absmin) | `movement.inc` `movement_size` | ported |
| movement.rs Bot::work_move_direction (+ avoid_angle, speed_limit) | `movement.inc` `movement_work_move_direction` | partial: the stealthy squad's half speed needs squad.inc's `squad_is_stealth` (requested); the stay-near half speed is in |
| movement.rs Bot::check_stuck (+ react_to_stuck, give_up_stuck) | `movement.inc` `movement_check_stuck` | ported |
| movement.rs Bot::stunt_ready (TSPS_STANCE, TSPF_STUNTING, TSCOMBAT_NEXT_STUNT) | `movement.inc` `movement_stunt_ready` | ported |
| movement.rs Bot::dive (TS's +alt1 stunt on its own) | `movement.inc` `movement_dive` | ported |
| movement.rs Bot::dive_landing (TSPF_PRONE, TSCOMBAT_PRONE_LOCK) | `movement.inc` `movement_dive_landing` | ported |
| mod.rs Bot::climb_type (GetClimbType, TS's cases). | `movement.inc` `movement_climb_type` | ported |
| mod.rs Bot::ladder_dir (1 up, -1 down, 0 stay) | `movement.inc` `movement_ladder_dir` | ported |

### navigate.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| mod.rs Bot::new's AStar / AStarBot / Explore | `navigate.inc` `navigate_bot_init` | ported |
| The bot left | `navigate.inc` `navigate_bot_free` | ported |
| think.rs Bot::teleported, navigation's part | `navigate.inc` `navigate_teleported` | ported |
| think.rs Bot::spawn_init, navigation's part (next_clear_failed_goals 0: the failed goals cleared, as the next think would) | `navigate.inc` `navigate_spawn_init` | ported |
| mod.rs Bot::next_waypoint (GetNextWaypoint) | `navigate.inc` `navigate_next_waypoint` | ported |
| host lib.rs map precache (m_iWaypointTexture, sprites/lgtning.spr) | `navigate.inc` `navigate_precache` | ported (core calls it from plugin_precache) |
| host commands.rs load_waypoints (nav_load) and bots.rs load_heatmap (nav_heatmap_load). From plugin_cfg. | `navigate.inc` `navigate_load_map` | ported |
| bots.rs save_globals' heatmap (nav_heatmap_save). From plugin_end. | `navigate.inc` `navigate_save_map` | ported |
| host editor.rs frame (nav_draw_around once a second, nav_auto_waypoint / nav_auto_reset) | `navigate.inc` `navigate_editor_frame` | ported |
| host editor.rs send (TE_BEAMPOINTS, play, console message) | `navigate.inc` `navigate_send_events` | ported |
| config/client.rs Client waypoint_on / waypoint_paths_on / auto_waypoint / debug_auto_waypoint / current_waypoint | `navigate.inc` `navigate_editor_set`, `navigate_editor_current_waypoint`, `navigate_editor_player_left` | ported (commands.inc sets them) |
| think.rs Think's failed-goal upkeep (nav_path_think), each think. | `navigate.inc` `navigate_think` | ported |
| navigate.rs Bot::update_waypoint (touch distance by flags, ladders, human towers, walking past, 2 s unseen give-up, goal reached, jump/crouch-jump/stunt waypoints, bots wait for bots) | `navigate.inc` `navigate_update_waypoint` | ported |
| navigate.rs Bot::find_next_waypoint (nav_path_pop / nav_path_next, missed waypoints) | `navigate.inc` `navigate_find_next_waypoint` | ported |
| navigate.rs Bot::nearest_waypoint (ignore, visible, unreachable, is_bot) | `navigate.inc` `navigate_nearest_waypoint` | ported |
| navigate.rs Bot::roam_goal (nav_path_roam_goal with the team's claims) | `navigate.inc` `navigate_roam_goal` | ported |
| navigate.rs Bot::claim_factor | `navigate.inc` `navigate_claim_factor` | ported |
| navigate.rs Bot::search_path (+ think.rs push_current_on_path on a found path) | `navigate.inc` `navigate_search_path` | ported |
| navigate.rs Bot::threat_waypoints → nav_path_add_threat | `navigate.inc` `navigate_update_threats` | ported |
| navigate.rs Bot::crowded_waypoints → nav_path_crowd_from (teammates' live paths, not the posted copies) | `navigate.inc` `navigate_update_crowding` | ported |
| navigate.rs Bot::claim (board.inc claims) | `navigate.inc` `navigate_claim` | ported |
| navigate.rs Bot::share_with_team (board.inc posts) | `navigate.inc` `navigate_share_with_team` | ported |
| navigate.rs Bot::search_move | `navigate.inc` `navigate_search_move` | ported |
| navigate.rs Bot::fail_path (nav_path_fail) | `navigate.inc` `navigate_fail_path` | ported |
| navigate.rs Bot::scan_fov | `navigate.inc` `navigate_scan_fov` | ported |
| navigate.rs Bot::wall_on_side | `navigate.inc` `navigate_wall_on_side` | ported |
| navigate.rs Bot::got_stuck | `navigate.inc` `navigate_got_stuck` | ported |
| mod.rs Bot::go_to_waypoint (util bot_goto: flush, FIND_PATH -2) | `navigate.inc` `navigate_go_to_waypoint` | ported |
| manager.rs player_waypoint / update_heatmap (damage) | `navigate.inc` `navigate_heatmap_damage` | ported (core calls it from manager_damage) |
| manager.rs player_waypoint / update_heatmap (kill) | `navigate.inc` `navigate_heatmap_kill` | ported (core calls it from manager_death) |

### remember.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| RememberPositions::default (a new bot). | `remember.inc` `remember_bot_init` | ported |
| RememberPositions::forget (manager.rs death) | `remember.inc` `remember_forget` | ported |
| RememberPositions::add (replaces by entity / place, keeps 8). | `remember.inc` `remember_add` | ported |
| RememberPositions::tick. | `remember.inc` `remember_tick` | ported |
| RememberPositions::hunted. | `remember.inc` `remember_hunted` | ported |
| RememberPositions::any | `remember.inc` `remember_any` | ported |
| RememberPositions::newest | `remember.inc` `remember_newest` | ported |
| RememberPositions::nearest | `remember.inc` `remember_nearest` | ported |
| RememberPositions::clear. | `remember.inc` `remember_clear` | ported |

### hearing.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| hearing.rs Hearing::default (a new bot). | `hearing.inc` `hearing_bot_init` | ported |
| events.rs SoundKind::of_sample filtered by is_heard | `hearing.inc` `hearing_sound_kind` | ported |
| events.rs Bot::hear_sound. | `hearing.inc` `hearing_hear_sound` | ported |
| think.rs Bot::hear_gunfire (the port's weapon_fired). | `hearing.inc` `hearing_hear_gunfire` | ported |
| hearing.rs Hearing::add (merges a sound within SAME_SOUND). | `hearing.inc` `hearing_add` | ported |
| hearing.rs Hearing::tick (drops sounds older than HEARD_FOR). | `hearing.inc` `hearing_tick` | ported |
| hearing.rs Hearing::investigate. | `hearing.inc` `hearing_investigate` | ported |
| hearing.rs Hearing::newest_new | `hearing.inc` `hearing_newest_new` | ported |

### items.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| ItemMemory::default (a new bot). | `items.inc` `items_bot_init` | ported |
| think.rs Bot::item_taken + ItemMemory::taken (the port's item_pickup) | `items.inc` `items_taken` | ported |
| ItemMemory::scan (ts_get_items every 5 s) | `items.inc` `items_scan` | ported |
| ItemMemory::learn (an entity on the game's list) | `items.inc` `items_learn` | ported |
| ItemMemory::look + seen (with think.rs learn_items' view cone) | `items.inc` `items_look` | ported |
| ItemMemory::get | `items.inc` `items_find` | ported |
| KnownItem::there_at | `items.inc` `items_there_at` | ported |
| ItemMemory::untakeable. | `items.inc` `items_untakeable` | ported |
| ItemMemory::ignored. | `items.inc` `items_ignored` | ported |
| ItemMemory::touched. | `items.inc` `items_touched` | ported |

### board.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| TeamBoards::died | `board.inc` `board_died` | ported |
| TeamBoards::clear (map change). | `board.inc` `board_clear` | ported |
| TeamBoards::get(team).is_some() | `board.inc` `board_exists` | ported |
| TeamBoard::sight. | `board.inc` `board_sight` | ported |
| TeamBoard::hear. | `board.inc` `board_hear` | ported |
| TeamBoard::claim. | `board.inc` `board_claim` | ported |
| TeamBoard::post (position; the path is the bot's live RcPath) | `board.inc` `board_post` | ported |
| TeamBoard::leave. | `board.inc` `board_leave` | ported |
| TeamBoard::expire. | `board.inc` `board_expire` | ported |
| TeamBoard sightings / sounds (readers for goals.inc) | `board.inc` `board_sighting_count`, `board_sighting`, `board_sound_count`, `board_sound` | ported |
| goals.rs item claim check (board.claims) | `board.inc` `board_item_claimed` | ported |
| TeamBoard::mates | `board.inc` `board_mates` | ported |
| TeamBoard::paths_of_others' sources | `board.inc` `board_mate_paths` | ported |
| navigate.rs threat_waypoints' team part | `board.inc` `board_add_threats` | ported |
| roam_goal / claim_factor's claimed waypoints | `board.inc` `board_claimed_waypoints` | ported |

### kungfu.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| kungfu.rs KungFu::new (a new bot | `kungfu.inc` `kungfu_bot_init` | stub |
| think.rs spawn_init's kungfu part | `kungfu.inc` `kungfu_spawn_init` | stub |
| host bots.rs new_bot | `kungfu.inc` `kungfu_load` | stub |
| host bots.rs save_bot | `kungfu.inc` `kungfu_save` | stub |
| kungfu.rs Bot::kungfu_fight | `kungfu.inc` `kungfu_fight` | stub |
| kungfu.rs Bot::kf_settle | `kungfu.inc` `kungfu_settle` | stub |
| kungfu.rs Bot::kungfu_face | `kungfu.inc` `kungfu_face` | stub |
| kungfu.rs KungFu::fighting. | `kungfu.inc` `kungfu_fighting` | stub |
| kungfu.rs KungFu::opponent | `kungfu.inc` `kungfu_opponent` | stub |
| kungfu.rs is_ranged (a gun, not a blade or kung fu). | `kungfu.inc` `kungfu_is_ranged` | stub |

### learning.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| host lib.rs game_init | `learning.inc` `learning_load_globals` | stub |
| host bots.rs save_globals | `learning.inc` `learning_save_globals` | stub |
| host bots.rs new_bot | `learning.inc` `learning_bot_load` | stub |
| host bots.rs save_bot | `learning.inc` `learning_bot_save` | stub |
| free the handle (rl_bot_free) when the bot leaves. | `learning.inc` `learning_bot_free` | stub |
| think.rs Think's death | `learning.inc` `learning_on_death` | stub |
| think.rs spawn_init | `learning.inc` `learning_on_spawn` | stub |
| learning.rs Bot::learn_think (exposure, shots, ledger tick). | `learning.inc` `learning_think` | stub |
| learning.rs Bot::choose_loadout (the bandit's pick, rl_loadout_choose) | `learning.inc` `learning_choose_loadout` | stub |
| manager.rs combat_event for a damage | `learning.inc` `learning_damage_event` | stub |
| manager.rs combat_event for a kill (KillEvent | `learning.inc` `learning_kill_event` | stub |
| manager.rs player_stunt (rl_opp_stunt; tsfun's client_stunt). | `learning.inc` `learning_player_stunt` | stub |
| learning.rs Bot::learned_threat (rl_opp_threat). | `learning.inc` `learning_learned_threat` | stub |
| learning.rs Bot::waypoint_danger (the heatmap, rcbot_nav). | `learning.inc` `learning_waypoint_danger` | stub |
| learning.rs Bot::learned_weapon_score (rl_ws_score) | `learning.inc` `learning_weapon_score` | stub |

### squad.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| squad.rs Squads::clear (FreeMemory) | `squad.inc` `squad_clear` | ported |
| Squads::get | `squad.inc` `squad_get` | ported |
| Squads::find_by_leader (FindSquadByLeader) | `squad.inc` `squad_find_by_leader` | ported |
| Squads::add_member (AddSquadMember) | `squad.inc` `squad_add_member` | ported (at most 16 squads of 8 members; the rest dropped with a debug line) |
| Squads::remove (RemoveSquad) | `squad.inc` `squad_remove` | ported |
| Squads::change_leader + Squad::change_leader | `squad.inc` `squad_change_leader_of` | ported |
| Squads::return_all_to_formation | `squad.inc` `squad_return_all_to_formation` | ported |
| Squads::update_angles | `squad.inc` `squad_update_angles` | ported |
| manager.rs apply_squad_orders + Bot::squad_order | `squad.inc` `squad_apply_orders` | ported |
| host bots.rs flush_squad_messages | `squad.inc` `squad_flush_messages` | ported |
| host bots.rs disconnect's change_leader | `squad.inc` `squad_player_left` | ported |
| Squad::change_formation / spread / change_spread | `squad.inc` `squad_change_formation`, `squad_spread`, `squad_change_spread` | ported |
| Squad::toggle_fire_mode (Effect::SquadToggleFireMode) | `squad.inc` `squad_toggle_fire_mode` | ported |
| Squad::set_combat_type (Effect::SquadToggleCombatType) | `squad.inc` `squad_toggle_combat_type` | ported |
| Squad::formation_position / formation_vector | `squad.inc` `squad_formation_vector` | ported (upstream's blocked-trace quirk kept) |
| Squad::is_stealth / can_shoot of the bot's squad | `squad.inc` `squad_is_stealth`, `squad_can_shoot` | ported |
| Bot::clear_squad / set_squad / is_squad_leader | `squad.inc` `squad_clear_bot`, `squad_set_squad`, `squad_is_leader` | ported |
| Bot::can_add_to_squad / add_to_squad / remove_my_squad | `squad.inc` `squad_can_add`, `squad_add`, `squad_remove_mine` | ported |
| host bots.rs remove_all_bot_squads | `squad.inc` `squad_remove_all_bot_squads` | ported |
| Bot::squad_think | `squad.inc` `squad_think` | ported |
| Bot::update_squad_conditions | `squad.inc` `squad_update_conditions` | ported |
| Bot::follow_formation (+ squad_leader_think) | `squad.inc` `squad_follow_formation` | ported |
| Bot::squad_modes | `squad.inc` `squad_modes` | ported (holding fire never works, as upstream) |
| Bot::squad_enemy_found | `squad.inc` `squad_enemy_found` | ported |
| Bot::see_leader_again | `squad.inc` `squad_see_leader_again` | ported |
| Bot::task_follow_leader (+ say_in_position) | `squad.inc` `squad_task_follow_leader` | ported |

### botcam.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| BotCam::clear | `botcam.inc` `botcam_clear` | ported |
| BotCam::is_working | `botcam.inc` `botcam_is_working` | ported |
| BotCam::set_current_bot | `botcam.inc` `botcam_set_current_bot` | ported |
| combat.rs enemy_found's camera part (BotHasEnemy) | `botcam.inc` `botcam_enemy_found` | ported (combat calls it) |
| manager.rs frame's other_has_enemy | `botcam.inc` `botcam_set_other_has_enemy` | ported |
| host bots.rs botcam_frame + spawn_camera, BotCam::think / move_camera | `botcam.inc` `botcam_frame` | ported (the camera is made only if `clients_precache` ran in plugin_precache) |
| host bots.rs botcam_tune_in (TuneIn, tune_in_error) | `botcam.inc` `botcam_tune_in` | ported |

### events.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| events.rs Bot::forget_greeting | `events.inc` `events_forget_greeting` | ported |
| Bot::team_number | `events.inc` `events_team_number` | ported |
| Bot::having_fun / getting_bored / boredom_text | `events.inc` `events_having_fun`, `events_getting_bored`, `events_boredom_text` | ported |
| Bot::want_to_leave_game | `events.inc` `events_want_to_leave_game` | ported |
| BotEvent::Leaving | `events.inc` `events_leaving` | ported |
| BotEvent::KillSelf | `events.inc` `events_kill_self` | ported |
| BotEvent::Kill (event_kill) | `events.inc` `events_kill` | ported |
| BotEvent::Died (event_died) | `events.inc` `events_died` | ported |
| BotEvent::SeeTeammateDie (see_teammate_die) | `events.inc` `events_see_teammate_die` | ported (a world killer: nothing) |
| BotEvent::SeePlayerDie (see_player_die) | `events.inc` `events_see_player_die` | ported |
| BotEvent::SeeTeammateKill | `events.inc` `events_see_teammate_kill` | ported (does nothing) |
| BotEvent::SeeEnemyKill (see_enemy_kill) | `events.inc` `events_see_enemy_kill` | ported |
| BotEvent::HearTeammateDie | `events.inc` `events_hear_teammate_die` | ported |
| Bot::task_type_message (BOT_TASK_TYPE_MESSAGE) | `events.inc` `events_task_type_message` | ported |

### chat.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| globals.cpp SetupBotChat | `chat.inc` `chat_init` | ported |
| host bots.rs wake_hal | `chat.inc` `chat_wake_hal` | ported (`hal_load` with the profile's hal_*_file overrides) |
| host bots.rs save_bot's brain | `chat.inc` `chat_save_hal` | ported |
| the brain freed with the bot | `chat.inc` `chat_free_hal` | ported |
| host bots.rs greet | `chat.inc` `chat_greet` | ported |
| events.rs Bot::bot_chat (+ chat_players) | `chat.inc` `chat_bot_chat` | ported (to: player, 0 nobody, -1 a non-client) |
| events.rs Bot::reply_to_message | `chat.inc` `chat_reply_to_message` | ported |
| host bots.rs say_command | `chat.inc` `chat_say_command` | ported |

### config.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| config/mod.rs Config::default | `config.inc` `config_defaults` | ported |
| Config::is_on (IsConfigSettingOn) | `config.inc` `config_is_on` | ported |
| Config::change_setting (ChangeConfigSetting) | `config.inc` `config_change_setting` | ported |
| Config::is_debug_on (IsDebugLevelOn) | `config.inc` `config_is_debug_on` | ported |
| cstr.rs fgets_chunks + text_mode (the line readers) | `config.inc` `cfg_open`, `cfg_fgets`, `cfg_close`, `cfg_strip_line`, `cfg_is_space` | ported |
| host bots.rs State::write (atomic writes) | `config.inc` `cfg_write_begin`, `cfg_write_end`, `cfg_write_text`, `cfg_make_dirs` | ported (Windows: the old file is deleted before the rename) |
| file.rs read_config + host commands.rs read_config | `config.inc` `config_read`, `config_tokenize` | ported (at most 64 command lines) |
| host lib.rs server_activate's map config read | `config.inc` `config_read_map_config` | ported (at most 128 lines of 191 bytes) |
| host lib.rs map_config_frame | `config.inc` `config_map_config_frame` | ported |
| file.rs read_bot_folder (rcbot_folder.ini) | none | not ported: the folder is `tsmod/rcbot/` |

### commands.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| lib.rs register_command(BOT_COMMAND_ACCESS) | `commands.inc` `commands_init` | ported (`rcbot` server and client commands, `menuselect`, `+/-rcbot_force_grip`) |
| commands.rs server_command | `commands.inc` `commands_server_rcbot` | ported |
| commands.rs client_command | `commands.inc` `commands_client_rcbot`, `commands_client_menuselect`, `commands_client_force_grip` | ported (the dead "addbot" tip left out) |
| commands.rs run_config | `commands.inc` `commands_run_config_line` | ported |
| commands.rs run (Run::action) | `commands.inc` `commands_run` | ported (pending access rechecks run right after; the host never runs them) |
| CommandId lookup / needs_access / help, command_list | `commands.inc` `commands_lookup`, `commands_needs_access`, `commands_help`, `commands_print_list` | ported |
| Host::print | `commands.inc` `commands_print`, `commands_print_all` | ported |
| Run::add_bot + Host::add_bot | `commands.inc` `commands_add_bot` | ported |
| commands.rs ts_team_choice | `commands.inc` `commands_ts_team_choice` | ported |
| Run::menu_select / menu_action (BotMenu_Func_*) | `commands.inc` `commands_menu_select`, `commands_menu_action` | ported |
| Run::set_pass / waypoint_menu / util / users / debug_ent / debug_bot / debug / config / squad / path_waypoint / auto_waypoint / botcam / help_command / waypoint / waypoint_flag | `commands.inc` (one static each) | ported; `util givetsweapon` is tsfun's `ts_giveweapon` (200 extra rounds), `util explo`'s damage is the engine's `fakedamage` |
| Host::find_player / player_by_user_id / facing_entity / nearest_waypoint / toggles, view_trace, explosion_create, radius_damage | `commands.inc` statics, `commands_find_player`, `commands_nearest_waypoint` | ported |
| lib.rs map_config_frame's rejoin hold | `commands.inc` `commands_hold_rejoin` | ported |
| host bots.rs force_grip_frame | `commands.inc` `commands_force_grip_frame` | ported |
| host bots.rs disconnect's grips / release_force_grip | `commands.inc` `commands_force_grip_release_player` | ported |

### menu.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| Menu::get | `menu.inc` `menu_caption`, `menu_item` | ported (TS's menus) |
| Menu::render | `menu.inc` `menu_render` | ported |
| Menu::show + host api.rs show_menu (ShowMenu message) | `menu.inc` `menu_show` | ported |
| menu.rs activate | `menu.inc` `menu_select` | ported (actions run `commands_menu_action`) |

### profile.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| BotProfile::init (BotFunc_InitProfile) | `profile.inc` `profile_init` | ported |
| host bots.rs profile_for + choose_profile | `profile.inc` `profile_for` | ported |
| A bot in the game (other than slot except) uses profile pid. | `profile.inc` `profile_in_use` | ported |
| parse_profile_list | `profile.inc` `profile_parse_list` | ported |
| BotProfile::read | `profile.inc` `profile_read` | ported (favmap, spray and the NS percentages read but not kept) |
| BotProfile::write | none | not ported (nothing calls it) |

### rep.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| events.rs Bot::client_rep_id (State::rep_ids) | `rep.inc` `rep_client_id` | ported |
| Reputations::get / add_rep | `rep.inc` `rep_find`, `rep_add_rep` | ported (at most 64 reputations a bot) |
| Reputations::client_rep | `rep.inc` `rep_client_rep` | ported |
| events.rs Bot::client_rep | `rep.inc` `rep_of` | ported |
| events.rs Bot::rep_of | `rep.inc` `rep_lookup` | ported |
| events.rs increase_rep_of / decrease_rep_of (Reputations::increase_rep / decrease_rep) | `rep.inc` `rep_increase`, `rep_decrease` | ported |
| Reputations::average_rep_on_server | `rep.inc` `rep_average_on_server` | ported |
| Reputations::random_client | `rep.inc` `rep_random_client` | ported |
| Reputation::describe, Reputations::print_reps | `rep.inc` `rep_describe`, `rep_print_reps` | ported |
| decrease_rep_roll / increase_rep_roll | `rep.inc` `rep_decrease_roll`, `rep_increase_roll` | ported |
| parse_rep_file, write_rep, add_load_rep, remove_save_rep (.rcr) | `rep.inc` statics | ported (byte-compatible records, atomic rewrite) |
| host bots.rs new_bot's reputations | `rep.inc` `rep_load_all` | ported |
| host bots.rs put_in_server's reputations | `rep.inc` `rep_add_load` | ported |
| host bots.rs disconnect's reputations | `rep.inc` `rep_remove_save` | ported |
| Reputations::save_all_rep, new_rep, get_create_rep | none | not ported (nothing calls them) |

### users.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| AllowedPlayers::clear | `users.inc` `users_clear` | ported |
| AllowedPlayers::add_player / remove_player (AllowedPlayer::matches) | `users.inc` `users_add_player`, `users_remove_player` | ported (at most 64 users) |
| AllowedPlayers::get_player (AllowedPlayer::is_for_client) | `users.inc` `users_get_player`, `users_access` | ported |
| AllowedPlayers::show_users | `users.inc` `users_show` | ported |
| AllowedPlayers::read_users | `users.inc` `users_read_file` | ported |
| host commands.rs read_users | `users.inc` `users_read` | ported |

### client.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| host lib.rs precache (camera model, fireball) | `client.inc` `clients_precache` | ported (needs a plugin_precache call) |
| Clients::connect + host bots.rs put_in_server | `client.inc` `clients_connect` | ported (the Steam ID is the auth id, as upstream; the host passes "") |
| Clients::recheck_auth | `client.inc` `clients_recheck_auth` | ported |
| Clients::disconnect | `client.inc` `clients_disconnect` | ported |
| Client::set_pass / has_cmd / toggle_cmd / set_auto_waypoint / has_tooltip_sent | `client.inc` `clients_set_pass`, `clients_has_cmd`, `clients_toggle_cmd`, `clients_set_auto_waypoint`, `clients_has_tooltip_sent` | ported |
| Client::current_waypoint (the editor's and the menu's) | `client.inc` `clients_current_waypoint` | ported |
| the client's editor switches to navigate.inc | `client.inc` `clients_editor_sync` | ported |
| rep.rs player_rep_id (bot_playerids.ini) | `client.inc` `clients_player_rep_id` | ported (atomic rewrite on append) |
| Engine::beam (util explo's beam) | `client.inc` `clients_beam` | ported |

### manager.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| ts_frame | `manager.inc` `manager_frame` | ported |
| BotManager::frame | `manager.inc` `manager_think_all` | ported |
| Bot::is_alive | `manager.inc` `bot_alive` | ported |
| think.rs Bot::frame | `manager.inc` `manager_bot_frame` | ported |
| The rest of lib.rs start_frame | `manager.inc` `manager_act_all` | ported |
|  | `manager.inc` `manager_run_action` | ported |
| Action::Command | `manager.inc` `bot_action_command` | ported |
| Action::SelectWeapon | `manager.inc` `bot_action_select_weapon` | ported |
| Action::Loadout | `manager.inc` `bot_action_loadout` | ported |
| Action::Kill (ClientKill). | `manager.inc` `bot_action_kill` | ported |
| Action::Kick. | `manager.inc` `bot_action_kick` | ported |
| Bot::command_ran | `manager.inc` `bot_command_ran` | ported |
| Bot::teleported | `manager.inc` `bot_teleported` | ported |
| BotManager::death (BotClient_Generic_DeathMessage) | `manager.inc` `manager_death` | ported |
| BotManager::combat_event for a damage (the port's damage event, design §4) | `manager.inc` `manager_damage` | ported |
| BotManager::sound (pfnEmitSound's loop) | `manager.inc` `manager_sound` | ported |
| BotManager::touch (DispatchTouch) | `manager.inc` `manager_touch` | ported |
| BotManager::item_pickup | `manager.inc` `manager_item_pickup` | ported |
| BotManager::weapon_fired | `manager.inc` `manager_weapon_fired` | ported |
| BotManager::make_squad (BotFunc_MakeSquad) | `manager.inc` `manager_make_squad` | ported |
| BotManager::say (ClientCommand's say/say_team) | `manager.inc` `manager_say` | ported |
| Every subsystem's CBot::Init part for a new bot (after state_clear_bot and the profile). | `manager.inc` `manager_bot_init` | ported |

### backend.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| plugin_init | `backend.inc` `backend_register` | ported |
|  | `backend.inc` `ts_bot_activate` | ported |
|  | `backend.inc` `ts_bot_deactivate` | ported |
| addbot, addcustombot, the quota. | `backend.inc` `ts_bot_add` | ported |
| A bot the engine kept across the level change | `backend.inc` `ts_bot_adopt` | ported |
| lib.rs create | `backend.inc` `backend_create` | ported |
|  | `backend.inc` `backend_copy_profile` | ported |
| bots.rs new_bot | `backend.inc` `backend_new_bot` | ported |
| bots.rs save_bot | `backend.inc` `backend_save_bot` | ported |
| The slot's bot is gone | `backend.inc` `backend_remove_bot` | ported |
| lib.rs save_all | `backend.inc` `backend_save_all` | ported |
| previous_profiles across the plugin reload | `backend.inc` `backend_store_prev_profiles` | ported |
|  | `backend.inc` `backend_load_prev_profiles` | ported |
| commands.rs Host::add_bot (rcbot addbot, the rejoin code) | `backend.inc` `backend_add_bot_now` | ported |
| lib.rs rejoin_frame (StartFrame's rejoin part) | `backend.inc` `backend_rejoin_frame` | ported |
| bots.rs put_in_server (CClients::ClientConnected) | `backend.inc` `backend_put_in_server` | ported |
| bots.rs disconnect (ClientDisconnect, CClients::ClientDisconnected) | `backend.inc` `backend_disconnect` | ported |

### world.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| plugin_init | `world.inc` `world_init` | ported |
|  | `world.inc` `world_end` | ported |
| world.rs refresh_frame | `world.inc` `world_refresh_frame` | ported |
| lib.rs read_masters | `world.inc` `world_read_masters` | ported |
|  | `world.inc` `world_userid` | ported |
|  | `world.inc` `world_flags` | ported |
| PlayerState::alive | `world.inc` `world_alive` | ported |
| The rules team, 1.. in teamplay, 0 none. | `world.inc` `world_team` | ported |
|  | `world.inc` `world_origin` | ported |
|  | `world.inc` `world_velocity` | ported |
|  | `world.inc` `world_vangle` | ported |
| GetGunPosition | `world.inc` `world_eyes` | ported |
| Entity::entity_origin for a player | `world.inc` `world_player_center` | ported |
|  | `world.inc` `world_health` | ported |
|  | `world.inc` `world_armor` | ported |
| The weapon in hand, the game's id (0 kung fu). | `world.inc` `world_weapon` | ported |
|  | `world.inc` `world_clip` | ported |
|  | `world.inc` `world_ammo` | ported |
|  | `world.inc` `world_frags` | ported |
|  | `world.inc` `world_field` | ported |
|  | `world.inc` `world_combat` | ported |
|  | `world.inc` `world_is_bot` | ported |
| BotWorld::teamplay | `world.inc` `world_teamplay` | ported |
| BotWorld::model_teams | `world.inc` `world_model_teams` | ported |
| ts_get_gamestate, once per frame. | `world.inc` `world_gamestate` | ported |
|  | `world.inc` `world_cvar_gravity` | ported |
| BotWorld::map_items | `world.inc` `world_items` | ported |
|  | `world.inc` `world_item_field` | ported |
|  | `world.inc` `world_item_origin` | ported |
| BotWorld::inventory | `world.inc` `world_inventory` | ported |
|  | `world.inc` `world_inventory_field` | ported |
|  | `world.inc` `world_owns` | ported |
| Entity::entity_origin (BotWorld::entity_glance), cached for the frame | `world.inc` `world_entity_origin` | ported |
| nav World::trace_line (UTIL_TraceLine) | `world.inc` `world_trace_line` | ported |
| BotWorld::trace_point_hull (pfnTraceHull, point hull 0). | `world.inc` `world_trace_hull` | ported |
| nav World::point_contents (CONTENTS_*). | `world.inc` `world_point_contents` | ported |
| BotWorld::in_pvs | `world.inc` `world_in_pvs` | ported |
| BotWorld::entities_in_sphere (UTIL_FindEntityInSphere, every match) | `world.inc` `world_entities_in_sphere` | ported |

### files.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| plugin_init, after ts_bot_register_backend (tsx makes the backend folder then). | `files.inc` `files_init` | ported |
| <rcbot dir><rel> into out. | `files.inc` `files_path` | ported |
| The file to read for rel | `files.inc` `files_find` | ported |
| Reads a whole small text file (an ini) into out; returns its length, -1 if missing. | `files.inc` `files_read_text` | ported |

### math.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
|  | `math.inc` `vec_set` | ported |
|  | `math.inc` `vec_copy` | ported |
|  | `math.inc` `vec_zero` | ported |
|  | `math.inc` `vec_is_zero` | ported |
| out = a - b | `math.inc` `vec_sub` | ported |
| out = a + b | `math.inc` `vec_add` | ported |
| out = a + b * s | `math.inc` `vec_mad` | ported |
|  | `math.inc` `vec_scale` | ported |
| math.rs Vector::length | `math.inc` `vec_length` | ported |
|  | `math.inc` `vec_length_2d` | ported |
|  | `math.inc` `vec_distance` | ported |
|  | `math.inc` `vec_distance_2d` | ported |
| math.rs Vector::normalize | `math.inc` `vec_normalize` | ported |
| look.rs dot | `math.inc` `vec_dot` | ported |
| look.rs fix_float_angle | `math.inc` `fix_float_angle` | ported |
| look.rs fix_angles | `math.inc` `fix_angles` | ported |
| look.rs angle_diff | `math.inc` `angle_diff` | ported |
| look.rs vec_to_angles | `math.inc` `vec_to_angles` | ported |
| look.rs make_vectors | `math.inc` `make_vectors` | ported |
| look.rs change_angles | `math.inc` `change_angles` | ported |
|  | `math.inc` `fclamp` | ported |
|  | `math.inc` `fmin` | ported |
|  | `math.inc` `fmax` | ported |

### report.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
|  | `report.inc` `report_init` | ported |
| plugin_cfg and every check | `report.inc` `report_refresh_mode` | ported |
|  | `report.inc` `report_check_pass` | ported |
| ts_bot_activate came. | `report.inc` `report_backend_active` | ported |
|  | `report.inc` `report_bot_added` | ported |
|  | `report.inc` `report_spawned` | ported |
|  | `report.inc` `report_fired` | ported |
|  | `report.inc` `report_runcmd` | ported |
| lib.rs report | `report.inc` `report_frame` | ported |

### minimal.inc

| Rust (tsmod-rcbot-rs) | Pawn | Status |
|---|---|---|
| The loadout bought for the next life | `minimal.inc` `minimal_loadout` | placeholder |
|  | `minimal.inc` `minimal_think_alive` | placeholder |
