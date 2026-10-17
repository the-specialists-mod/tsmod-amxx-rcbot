// rcbot/think.inc: CBot::Think, SpawnInit, the TS loadout choice, task selection and the task
// runner.
// Owner: think. Ports bot/think.rs (Bot::think, spawn_init, select_loadout, learn_items,
// switch_weapon, drop_roaming_for_sound, can_pickup, facing, objective_tasks, objective_role,
// defend_spot, ctb_tasks, look_for_new_tasks, do_tasks, task_find_path, path_deadline,
// push_current_on_path, nearest_to_origin, task_normal_attack, task_use, task_listen,
// is_grenade_rocket, trace_toss) and the mod.rs helpers they use (resolve, distance_from,
// add_task / add_priority_task, the conditions). Bot::frame / teleported / command_ran are in
// manager.inc; item_taken is items.inc's, hear_gunfire hearing.inc's.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported (docs/design.md §3 think.inc). learn_items doesn't learn the items in sight
// that the map's list lacks (items.inc has no ItemMemory::learn yet).

#if defined _rcbot_think_included
	#endinput
#endif
#define _rcbot_think_included

// think.rs HUNT_TIME: seconds a hunt or investigation may take to reach its place.
#define THINK_HUNT_TIME			15.0
// think.rs PICKUP_TRIES: seconds of pressing use on an item before the bot gives it up.
#define THINK_PICKUP_TRIES		1.5
// think.rs PATH_TIMEOUT: seconds to reach the first waypoint of a path found.
#define THINK_PATH_TIMEOUT		30.0
// think.rs SWITCH_WAIT: seconds a weapon switch waits for the game before it may be asked again.
#define THINK_SWITCH_WAIT		1.0
// nav MAX_JUMP_HEIGHT.
#define THINK_MAX_JUMP_HEIGHT	45.0
// think.rs ObjectiveRole.
#define THINK_ROLE_ATTACK		0
#define THINK_ROLE_DEFEND		1
// The most objectives, CTB entities and failed goals read at once.
#define THINK_MAX_OBJECTIVES	16
#define THINK_MAX_CTB			32
#define THINK_MAX_FAILED		64

// ---- mod.rs helpers -----------------------------------------------------------------------------

// Entity::handle's serial for an entity index: a player's user id, an item's TSITM_SERIAL (the
// frame's item list), else 0 (state.inc EntRef).
stock think_handle_serial(ent)
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		return world_userid(ent);
	}
	new n = world_items();
	for (new k = 0; k < n; k++)
	{
		if (world_item_field(k, TSITM_ENT) == ent)
		{
			return world_item_field(k, TSITM_SERIAL);
		}
	}
	return 0;
}

// EntRef equality: the index, and for a player the user id (Pawn has no edict serials; other
// entities' handles are their index, design §2).
stock bool:think_same_handle(a, a_serial, b, b_serial)
{
	return a == b && (a > MAX_PLAYERS || a_serial == b_serial);
}

// Bot::resolve (MyEHandle::Get): the entity behind a handle is still there.
stock bool:think_resolve(ent, serial)
{
	return ent > 0 && entref_alive(ent, serial);
}

// Entity::is_alive (EntityIsAlive).
stock bool:think_entity_alive(ent)
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		return bot_alive(ent);
	}
	if (!pev_valid(ent))
	{
		return false;
	}
	new Float:health;
	pev(ent, pev_health, health);
	return pev(ent, pev_deadflag) == RC_DEAD_NO && health > 0.0;
}

// The entity's health (players from the frame's snapshot).
stock Float:think_entity_health(ent)
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		return world_health(ent);
	}
	new Float:health;
	pev(ent, pev_health, health);
	return health;
}

// Entity::is_moving (BotFunc_EntityIsMoving): any linear or angular velocity.
stock bool:think_entity_moving(ent)
{
	new Float:v[3];
	pev(ent, pev_velocity, v);
	if (!vec_is_zero(v))
	{
		return true;
	}
	pev(ent, pev_avelocity, v);
	return !vec_is_zero(v);
}

// mod.rs Bot::distance_from (DistanceFrom): from the gun position.
stock Float:think_distance_from(id, const Float:v[3], bool:two_d)
{
	new Float:eyes[3];
	world_eyes(id, eyes);
	return two_d ? vec_distance_2d(v, eyes) : vec_distance(v, eyes);
}

// The bot's maximum health (pev->max_health).
stock Float:think_max_health(id)
{
	new Float:m;
	pev(id, pev_max_health, m);
	return m;
}

stock think_classname(ent, out[], len)
{
	out[0] = 0;
	if (pev_valid(ent))
	{
		pev(ent, pev_classname, out, len);
	}
}

// navigate.rs Bot::nearest_waypoint (WaypointLocations.NearestWaypoint with upstream's
// arguments, no failed goals); navigate_nearest_waypoint lacks ignore / unreachable / is_bot.
stock think_nearest_waypoint(const Float:origin[3], Float:range, ignore, bool:visible, bool:unreachable, bool:is_bot)
{
	new flags = (visible ? NAV_NEAREST_VISIBLE : 0) | (unreachable ? NAV_NEAREST_UNREACHABLE : 0) | (is_bot ? NAV_NEAREST_BOT : 0);
	return nav_nearest(origin, range, ignore, flags);
}

// The bot's failed goals (paths.failed_goals) into out: how many.
stock think_failed_goals(id, out[], len)
{
	if (!g_nv_i[id][NV_PATH])
	{
		return 0;
	}
	return min(nav_path_failed_goals(RcPath:g_nv_i[id][NV_PATH], out, len), len);
}

// nav flags::team_allows.
stock bool:think_team_allows(flags, team)
{
	return !(team != -1 && (flags & NAV_FL_TEAM_SPECIFIC) && (flags & NAV_FL_TEAM) != team);
}

// mod.rs Bot::add_task with Task::with_timeout: the slot, -1 when the queue was full.
stock think_add_timed(id, kind, sched, edict, serial, int_info, Float:float_info, const Float:vec[3], Float:secs)
{
	new k = tasks_add(id, kind, sched, edict, serial, int_info, float_info, vec);
	tasks_set_time_to_complete(id, k, secs);
	return k;
}

// ---- think.rs ----------------------------------------------------------------------------------

// think.rs is_grenade_rocket (UTIL_IsGrenadeRocket).
stock bool:think_is_grenade_rocket(ent)
{
	new cls[32];
	think_classname(ent, cls, charsmax(cls));
	return contain(cls, "grenade") != -1 || contain(cls, "rpg_rocket") != -1;
}

// think.rs trace_toss (BotFunc_TraceToss rewritten, design §4): where a thrown entity lands,
// flown in 0.1 s steps under TS's grenade gravity (0.75 sv_gravity) for the 3 s fuse.
stock think_trace_toss(ent, Float:out[3])
{
	new Float:g = world_cvar_gravity() * 0.75;
	new Float:origin[3], Float:velocity[3], Float:next[3], Float:end[3], hit;
	pev(ent, pev_origin, origin);
	pev(ent, pev_velocity, velocity);
	for (new k = 0; k < 30; k++)
	{
		velocity[2] -= g * 0.1;
		vec_mad(origin, velocity, 0.1, next);
		if (world_trace_line(origin, next, false, false, ent, end, hit) < 1.0)
		{
			vec_copy(end, out);
			return;
		}
		vec_copy(next, origin);
	}
	vec_copy(origin, out);
}

// think.rs Bot::think (CBot::Think).
stock think_bot(id)
{
	new Float:now = g_now;
	new Float:v[3], Float:bv[3];
	world_velocity(id, v);
	pev(id, pev_basevelocity, bv);
	vec_add(v, bv, v);
	g_th_f[id][TH_VELOCITY_3D] = vec_length(v);
	g_th_f[id][TH_VELOCITY_2D] = vec_length_2d(v);
	g_th_i[id][TH_LOOK_FOR_NEW_TASKS] = 1;
	g_mv_f[id][MV_MOVE_SPEED] = 0.0;
	if (g_mv_f[id][MV_UP_TIME] < now)
	{
		g_mv_f[id][MV_UP_SPEED] = 0.0;
	}
	new last_buttons = g_co_i[id][CO_BUTTON];
	// NotStartedGame is false in TS; the port's rules team (from 1) is RCBot's team from 0.
	new team = world_team(id);
	g_th_i[id][TH_TEAM] = world_teamplay() && team > 0 ? team - 1 : -1;

	// The failed goals' expiry and the back-offs (paths.failed_goals, expire_backed_off_goals).
	navigate_think(id);
	squad_think(id);
	if (!(g_th_i[id][TH_CONDITIONS] & COND_WANT_TO_LEAVE_GAME) && config_is_on(CFG_BOTS_LEAVE_AND_JOIN) && events_want_to_leave_game(id))
	{
		g_th_i[id][TH_CONDITIONS] |= COND_WANT_TO_LEAVE_GAME;
	}

	if (!bot_alive(id))
	{
		think_dead(id);
		return;
	}

	if (g_th_f[id][TH_SPAWN_TIME] == 0.0)
	{
		g_th_f[id][TH_SPAWN_TIME] = now;
		g_th_i[id][TH_FRAGS_SINCE_DEATH] = world_frags(id);
		g_th_f[id][TH_PREV_HEALTH] = world_health(id);
		world_origin(id, g_th_v[id][TH_SPAWN_POSITION]);
	}
	g_th_i[id][TH_NEED_TO_INIT] = 1;

	remember_tick(id);
	hearing_tick(id);
	look_work_view_angles(id);
	movement_work_move_direction(id);
	movement_check_stuck(id);
	movement_dive_landing(id);

	if (g_th_f[id][TH_LAST_THINK_TIME] >= now)
	{
		return;
	}
	g_th_f[id][TH_LAST_THINK_TIME] = now + g_th_f[id][TH_THINK_TIME];

	// A bot the port handed over alive (adopt_bot) picks here, as upstream.
	think_select_loadout(id);
	think_learn_items(id);

	g_mv_f[id][MV_IDEAL_MOVE_SPEED] = g_mv_f[id][MV_MAX_SPEED];
	g_th_i[id][TH_WANT_TO_FIND_ENEMY] = !config_is_on(CFG_DONT_SHOOT);
	combat_update_conditions(id);

	if (g_cb_f[id][CB_UPDATE_VISIBLES_TIME] < now)
	{
		g_cb_f[id][CB_UPDATE_VISIBLES_TIME] = now + g_pr_f[id][PR_VISION_TIME];
		if (!combat_update_visibles(id) && g_cb_f[id][CB_NEXT_WORK_RANGE_COSTS] < now)
		{
			g_cb_f[id][CB_NEXT_WORK_RANGE_COSTS] = now + 1.0;
		}
	}

	if (last_buttons == g_co_i[id][CO_BUTTON])
	{
		g_co_i[id][CO_BUTTON] = 0;
	}

	// TS: run from a grenade that is coming (design §4: any within 400 units).
	combat_flee_grenades(id);
	think_weapon_upkeep(id);
	think_see_waypoint(id);

	if (g_tk_count[id] > 0)
	{
		new t = g_tk_i[id][0][TK_EDICT];
		new Float:o[3], Float:eyes[3], Float:end[3], hit;
		if (think_resolve(t, g_tk_i[id][0][TK_EDICT_SERIAL]) && world_entity_origin(t, o))
		{
			world_origin(id, eyes);
			if (world_trace_line(eyes, o, true, true, id, end, hit) >= 1.0)
			{
				g_th_i[id][TH_CONDITIONS] |= COND_SEE_TASK_EDICT;
			}
			else
			{
				g_th_i[id][TH_CONDITIONS] &= ~COND_SEE_TASK_EDICT;
			}
		}
	}

	// Upstream waited half a second whenever there was no next waypoint; only a bot without any
	// waypoint near waits now (update_waypoint, design §4).
	if (!squad_follow_formation(id) && g_nv_f[id][NV_UPDATE_WPT_TIME] < now)
	{
		navigate_update_waypoint(id);
	}

	if (!g_cb_i[id][CB_ENEMY])
	{
		if (movement_climb_type(id) == CLIMB_NONE && !special_player_standing_on_me(id) && !special_standing_on_player(id)
			&& g_th_f[id][TH_LISTEN_TO_SOUND_TIME] < now)
		{
			g_th_f[id][TH_LISTEN_TO_SOUND_TIME] = now + g_th_f[id][TH_REACTION_TIME] + random_float(0.3, 0.75);
			// Gunfire heard is glanced at (upstream: a listen task, then a path to the shooter;
			// design §4).
			new h = hearing_newest_new(id);
			if (h != -1 && now - g_hr_f[id][h][HR_TIME] < 1.0)
			{
				preaim_watch(id, g_hr_origin[id][h], 0.8);
				think_drop_roaming_for_sound(id);
			}
		}
		else if (g_th_f[id][TH_CHAT_MESSAGE_TIME] < now)
		{
			if (!tasks_has(id, TASK_TYPE_MESSAGE))
			{
				g_th_f[id][TH_CHAT_MESSAGE_TIME] = now + random_float(10.0, 20.0);
				if (random_num(0, 100) < g_cfg_i[CFG_CHAT_PERCENT])
				{
					chat_bot_chat(id, RcChat_Idle, 0, false);
					events_getting_bored(id);
				}
			}
		}
		else if (g_cb_f[id][CB_LAST_SEE_ENEMY_TIME] > 0.0 && g_cb_f[id][CB_LAST_SEE_ENEMY_TIME] + 5.0 < now)
		{
			new cur = g_bw_i[id][BW_CURRENT];
			if (cur >= 0 && cur < RC_MAX_WEAPONS && botweapons_can_reload(id, cur) && botweapons_out_of_ammo(id, cur))
			{
				tasks_add_priority(id, TASK_RELOAD, 0, 0, 0, 0, 0.0, VEC_ZERO);
			}
			g_cb_f[id][CB_LAST_SEE_ENEMY_TIME] = 0.0;
		}
	}
	else
	{
		g_th_f[id][TH_CHAT_MESSAGE_TIME] = now + 5.0;
		g_cb_f[id][CB_LAST_SEE_ENEMY_TIME] = now;
	}

	squad_modes(id);
	g_th_i[id][TH_CONDITIONS] &= ~COND_CANT_SHOOT;
	g_cb_i[id][CB_ENEMY] = 0;
	g_cb_i[id][CB_ENEMY_SERIAL] = 0;
	think_do_tasks(id);
	learning_think(id);
	g_th_i[id][TH_CONDITIONS] &= ~COND_TASK_EDICT_PAIN;
	if (g_th_i[id][TH_LOOK_FOR_NEW_TASKS])
	{
		think_look_for_new_tasks(id);
	}
	preaim_update(id);
	// WantToFindEnemy, TS: unless dont_shoot is on. combat_find_enemy answers the index; the
	// handle's serial is taken here.
	new enemy = g_th_i[id][TH_WANT_TO_FIND_ENEMY] ? combat_find_enemy(id) : 0;
	g_cb_i[id][CB_ENEMY] = enemy;
	g_cb_i[id][CB_ENEMY_SERIAL] = enemy ? think_handle_serial(enemy) : 0;
	navigate_share_with_team(id);
}

// think.rs Bot::think's reload or switch of an empty gun: a gun with nothing in the clip or the
// reserve is dropped for the next weapon with rounds, else kung fu (design §4); an empty clip near
// an enemy goes for the next loaded weapon when the reload is slow; under half a clip with no
// enemy, reload now.
static stock think_weapon_upkeep(id)
{
	new cur = g_bw_i[id][BW_CURRENT];
	if (cur < 0 || cur >= RC_MAX_WEAPONS || botweapons_is_melee(id, cur))
	{
		return;
	}
	new enemy = g_cb_i[id][CB_ENEMY];
	if (botweapons_out_of_ammo(id, cur) && g_bw_weapon[id][cur][BW_MAX_CLIP] > 0)
	{
		if (!tasks_has(id, TASK_DROP_WEAPON))
		{
			tasks_add_priority(id, TASK_DROP_WEAPON, 0, 0, 0, 0, 0.0, VEC_ZERO);
		}
	}
	else if (botweapons_clip_is_empty(id, cur))
	{
		new Float:eo[3];
		if (think_resolve(enemy, g_cb_i[id][CB_ENEMY_SERIAL]) && world_entity_origin(enemy, eo))
		{
			// Within 600 units a reload over a second is worse than the next loaded weapon
			// (design §4).
			new Float:dist = think_distance_from(id, eo, false);
			new id_w = g_bw_weapon[id][cur][BW_ID];
			new bool:slow = id_w < 0 || id_w >= RC_MAX_WEAPONS || !g_gd_i[id_w][GD_KNOWN] || g_gd_f[id_w][GD_RELOAD] > 1.0;
			new Float:score;
			new best = fire_best_scored_weapon(id, dist, id_w, score);
			new bool:can = botweapons_can_reload(id, cur);
			if (best != -1 && ((dist < 600.0 && slow) || !can))
			{
				think_switch_weapon(id, best);
			}
			else if (can)
			{
				botweapons_reload(id);
			}
		}
		else if (botweapons_can_reload(id, cur) && !botweapons_out_of_ammo(id, cur))
		{
			botweapons_reload(id);
		}
	}
	else if (!enemy && botweapons_low_on_ammo(id, cur) && botweapons_can_reload(id, cur) && fire_safe_to_reload(id))
	{
		botweapons_reload(id);
	}
}

// think.rs Bot::think: can the bot see where it is going?
static stock think_see_waypoint(id)
{
	new Float:now = g_now;
	if (g_nv_i[id][NV_CURRENT_WPT] == -1 || !(g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_LADDER))
	{
		new Float:origin[3], Float:end[3], hit;
		world_origin(id, origin);
		new bool:see = world_trace_line(origin, g_mv_v[id][MV_MOVE_TO], true, true, id, end, hit) >= 1.0;
		// A door or something moving: walk through. Index 0 is the worldspawn.
		if (!see && hit > 0 && pev_valid(hit))
		{
			new Float:hv[3];
			pev(hit, pev_velocity, hv);
			if (!vec_is_zero(hv))
			{
				see = true;
			}
			else
			{
				new cls[32];
				think_classname(hit, cls, charsmax(cls));
				see = equal(cls, "func_door", 9) != 0;
			}
		}
		if (see)
		{
			g_th_i[id][TH_CONDITIONS] |= COND_SEE_NEXT_WAYPOINT;
			g_nv_f[id][NV_LAST_SEE_WPT] = 0.0;
		}
		else
		{
			if (g_th_i[id][TH_CONDITIONS] & COND_SEE_NEXT_WAYPOINT)
			{
				g_nv_f[id][NV_LAST_SEE_WPT] = now;
			}
			g_th_i[id][TH_CONDITIONS] &= ~COND_SEE_NEXT_WAYPOINT;
		}
	}
	else if (g_mv_i[id][MV_MOVE_TO_VALID])
	{
		g_nv_f[id][NV_LAST_SEE_WPT] = 0.0;
	}
}

// think.rs Bot::think's dead branch: the death's learning (fitness, breeding), SpawnInit, the
// next life's loadout (bought while dead: death clears the port's loadout), the respawn
// command every 5 s, and attack pressed at random as upstream.
stock think_dead(id)
{
	new Float:now = g_now;
	if (g_th_i[id][TH_NEED_TO_INIT])
	{
		g_th_i[id][TH_NUM_DEATHS]++;
		learning_on_death(id);
		think_spawn_init(id);
		g_th_f[id][TH_SPAWN_TIME] = 0.0;
	}
	think_select_loadout(id);
	if (g_th_f[id][TH_RESPAWN_TIME] < now)
	{
		bot_action_command(id, "respawn");
		g_th_f[id][TH_RESPAWN_TIME] = now + 5.0;
	}
	g_co_i[id][CO_BUTTON] = random_num(0, 1) ? IN_ATTACK : 0;
}

// think.rs Bot::spawn_init (CBot::SpawnInit(false)): forget this life and pick new genes.
stock think_spawn_init(id)
{
	g_th_f[id][TH_RESPAWN_TIME] = 0.0;
	tasks_flush(id);
	learning_on_spawn(id);
	g_cb_i[id][CB_ZOOM] = 0;
	g_th_i[id][TH_USED_MELEE] = 0;
	g_cb_f[id][CB_NEXT_GET_ENEMY_TIME] = 0.0;
	g_th_i[id][TH_PICKUP_ENT] = 0;
	g_th_i[id][TH_PICKUP_SERIAL] = 0;
	g_th_f[id][TH_LISTEN_TO_SOUND_TIME] = 0.0;
	g_th_f[id][TH_LAST_UPDATE_CONDITIONS] = 0.0;
	g_th_f[id][TH_LAST_THINK_TIME] = 0.0;
	g_co_i[id][CO_KILL] = 0;
	g_th_i[id][TH_CONDITIONS] = 0;
	g_th_i[id][TH_NEED_TO_INIT] = 0;
	movement_spawn_init(id);
	navigate_spawn_init(id);
	look_spawn_init(id);
	combat_spawn_init(id);
	botweapons_spawn_init(id);
	kungfu_spawn_init(id);
	fire_spawn_init(id);
}

// think.rs Bot::select_loadout: Think's TS weapon choice (COND_SELECTED_GUN), bought unless
// BOT_CONFIG_TS_KUNGFU. The GA still picks its genes (they feed the stunt perceptron and breed
// as upstream); the loadout bought is the bandit's (design §4).
stock think_select_loadout(id)
{
	if (g_th_i[id][TH_CONDITIONS] & COND_SELECTED_GUN)
	{
		return;
	}
	new RcLearn:h = RcLearn:g_ln_i[id][LN_HANDLE];
	if (h)
	{
		new ga[3];
		rl_bot_select_ts_weapons(h, config_is_on(CFG_DISABLE_WEAPON_LEARN), ga);
	}
	new ids[RC_MAX_LOADOUT];
	new n = learning_choose_loadout(id, ids, RC_MAX_LOADOUT);
	if (config_is_on(CFG_TS_KUNGFU))
	{
		g_ln_i[id][LN_PENDING_LOADOUT] = -1;
	}
	else
	{
		bot_action_loadout(id, ids, n);
	}
	g_th_i[id][TH_CONDITIONS] |= COND_SELECTED_GUN;
}

// think.rs Bot::learn_items: the map's items (the list now and then) and whether the ones in view
// are there.
// TODO(items): the items in sight the map's list doesn't name (a weapon a player dropped) are
// learned from the visibles in the Rust bot (ItemMemory::learn); items.inc has no such stock yet.
stock think_learn_items(id)
{
	items_scan(id);
	items_look(id);
}

// think.rs Bot::switch_weapon (CBot::SwitchWeapon, TS): the port's weapon switch (kung fu is the
// game's 0). The current weapon changes when the game reports it; the switch isn't asked again
// while it is pending.
stock bool:think_switch_weapon(id, weapon)
{
	if (g_bw_i[id][BW_CURRENT] == weapon || g_bw_i[id][BW_SWITCH_PENDING] == weapon)
	{
		return true;
	}
	bot_action_select_weapon(id, ts_weapon_to_game_id(weapon));
	g_bw_i[id][BW_SWITCH_PENDING] = weapon;
	g_bw_f[id][BW_SWITCH_UNTIL] = g_now + THINK_SWITCH_WAIT;
	g_cb_i[id][CB_ZOOM] = 0;
	return true;
}

// think.rs Bot::drop_roaming_for_sound: a bot only roaming drops that to choose again (the sound
// is among its goals); anything else it was doing goes on.
static stock think_drop_roaming_for_sound(id)
{
	if (g_tk_count[id] == 0)
	{
		return;
	}
	new sched = g_tk_i[id][0][TK_SCHEDULE_ID];
	if (g_tk_i[id][0][TK_KIND] != TASK_FIND_PATH || g_tk_f[id][0][TK_FLOAT] != -1.0 || g_tk_i[id][0][TK_EDICT] != 0)
	{
		return;
	}
	new same = 0;
	for (new k = 0; k < g_tk_count[id]; k++)
	{
		if (g_tk_i[id][k][TK_SCHEDULE_ID] == sched)
		{
			same++;
		}
	}
	if (same != 1)
	{
		return;
	}
	tasks_finish_schedule(id, sched);
	if (sched == 0)
	{
		tasks_finished_current(id, false);
	}
}

// think.rs Bot::can_pickup (CBot::CanPickup, TS): power-ups and ground weapons in reach, that are
// there, that the bot can take and wants (goals.inc item values), and that it didn't just fail
// to take (design §4). The classname test (ts_powerup*, ts_groundweapon) is tsx's entity kind
// (TSENT_POWERUP, TSENT_GROUND_WEAPON), which names the same entities without a string compare.
stock bool:think_can_pickup(id, ent)
{
	if (!pev_valid(ent) || g_th_f[id][TH_PICKUP_ITEM_TIME] > g_now || (pev(ent, pev_effects) & RC_EF_NODRAW))
	{
		return false;
	}
	new Float:o[3], Float:me[3];
	pev(ent, pev_origin, o);
	world_origin(id, me);
	if (o[2] > me[2] + THINK_MAX_JUMP_HEIGHT)
	{
		return false;
	}
	new info[TSEI_SIZE];
	if (ts_get_entity_info(ent, info) != TSBOT_OK)
	{
		return false;
	}
	new kind = info[TSEI_KIND];
	if ((kind != TSENT_POWERUP && kind != TSENT_GROUND_WEAPON) || items_ignored(id, ent, think_handle_serial(ent)))
	{
		return false;
	}
	if (!info[TSEI_AVAILABLE])
	{
		return false;
	}
	if (kind == TSENT_GROUND_WEAPON)
	{
		return goals_weapon_value(id, info[TSEI_WEAPON]) > 0.0;
	}
	return goals_powerup_value(id, info[TSEI_POWERUP]) > 0.0;
}

// think.rs Bot::facing (UTIL_IsFacingEntity): the view line at the entity's distance ends inside
// its box.
stock bool:think_facing(id, ent)
{
	new Float:fwd[3], Float:right[3], Float:src[3], Float:eo[3], Float:dst[3], Float:mins[3], Float:maxs[3];
	if (!world_entity_origin(ent, eo))
	{
		return false;
	}
	make_vectors(g_co_v[id][CO_VANGLE], fwd, right);
	world_eyes(id, src);
	vec_mad(src, fwd, vec_distance(eo, src), dst);
	pev(ent, pev_absmin, mins);
	pev(ent, pev_absmax, maxs);
	return dst[0] <= maxs[0] && dst[1] <= maxs[1] && dst[2] <= maxs[2] && dst[0] >= mins[0] && dst[1] >= mins[1] && dst[2] >= mins[2];
}

// ---- objectives (think.rs objective_tasks, objective_role, defend_spot, ctb_tasks) -------------

// think.rs Bot::objective_tasks: LookForNewTasks' TS objectives, by what each asks of the bot's
// team (design §4): the briefcase first on a CTB map; then an objective the bot can complete is
// gone for and used (a ts_hack / ts_bomb within 100 units of its waypoint, for 9 s), one of the
// other team's is defended from a waypoint that sees it. Sometimes the bot roams instead (1 in 8
// attacking, 1 in 4 defending). False when there is nothing to do. sched: the schedule id.
stock bool:think_objective_tasks(id, sched)
{
	new Float:now = g_now;
	if (g_game_ctb && think_ctb_tasks(id, sched))
	{
		return true;
	}
	// The objectives that can still be completed (host world.rs ts_objectives: active only).
	static data[TSOBJ_SIZE * THINK_MAX_OBJECTIVES];
	static list[THINK_MAX_OBJECTIVES];
	new total = min(ts_get_objectives(id, data, THINK_MAX_OBJECTIVES), THINK_MAX_OBJECTIVES), n = 0;
	for (new k = 0; k < total; k++)
	{
		if (data[k * TSOBJ_SIZE + TSOBJ_ACTIVE])
		{
			list[n++] = k;
		}
	}
	if (n == 0)
	{
		return false;
	}
	// An attempt that left the objective as it was: this team can't complete it.
	if (g_th_i[id][TH_OBJ_ATTEMPT] != -1 && g_th_f[id][TH_OBJ_ATTEMPT_UNTIL] < now)
	{
		new oid = g_th_i[id][TH_OBJ_ATTEMPT];
		g_th_i[id][TH_OBJ_ATTEMPT] = -1;
		for (new j = 0; j < n; j++)
		{
			if (data[list[j] * TSOBJ_SIZE + TSOBJ_ID] == oid)
			{
				think_obj_failed_remove(id, oid);
				new c = g_th_i[id][TH_OBJ_FAILED_COUNT];
				if (c < RC_MAX_OBJ_FAILED)
				{
					g_th_obj_failed_id[id][c] = oid;
					g_th_obj_failed_until[id][c] = now + 60.0;
					g_th_i[id][TH_OBJ_FAILED_COUNT] = c + 1;
				}
				break;
			}
		}
	}
	// objective_failed.retain(until > now).
	new kept = 0;
	for (new j = 0; j < g_th_i[id][TH_OBJ_FAILED_COUNT]; j++)
	{
		if (g_th_obj_failed_until[id][j] > now)
		{
			g_th_obj_failed_id[id][kept] = g_th_obj_failed_id[id][j];
			g_th_obj_failed_until[id][kept] = g_th_obj_failed_until[id][j];
			kept++;
		}
	}
	g_th_i[id][TH_OBJ_FAILED_COUNT] = kept;

	static attack[THINK_MAX_OBJECTIVES];
	new na = 0;
	for (new j = 0; j < n; j++)
	{
		if (think_objective_role(id, data, list[j]) == THINK_ROLE_ATTACK)
		{
			attack[na++] = list[j];
		}
	}
	new obj, role;
	if (na > 0)
	{
		obj = attack[random_num(0, na - 1)];
		role = THINK_ROLE_ATTACK;
	}
	else
	{
		obj = list[random_num(0, n - 1)];
		role = THINK_ROLE_DEFEND;
	}
	new bool:roam = role == THINK_ROLE_ATTACK ? random_num(0, 7) == 0 : random_num(0, 3) == 0;
	new Float:obj_origin[3];
	for (new c = 0; c < 3; c++)
	{
		obj_origin[c] = Float:data[obj * TSOBJ_SIZE + TSOBJ_ORIGIN + c];
	}
	new wpt = think_nearest_waypoint(obj_origin, NAV_REACHABLE_RANGE, g_nv_i[id][NV_LAST_FAILED_WPT], false, false, true);
	if (roam || wpt == -1)
	{
		return false;
	}
	if (role == THINK_ROLE_ATTACK)
	{
		new Float:at[3];
		nav_origin(wpt, at);
		new target = 0;
		static near[64];
		new m = world_entities_in_sphere(at, 100.0, near, sizeof(near));
		new cls[32];
		for (new j = 0; j < m; j++)
		{
			think_classname(near[j], cls, charsmax(cls));
			if (equal(cls, "ts_hack") || equal(cls, "ts_bomb"))
			{
				target = near[j];
				break;
			}
		}
		tasks_add(id, TASK_FIND_PATH, sched, 0, 0, wpt, -1.0, VEC_ZERO);
		if (target)
		{
			tasks_add(id, TASK_USE, sched, target, think_handle_serial(target), 0, 9.0, VEC_ZERO);
			g_th_i[id][TH_OBJ_ATTEMPT] = data[obj * TSOBJ_SIZE + TSOBJ_ID];
			g_th_f[id][TH_OBJ_ATTEMPT_UNTIL] = now + 30.0;
		}
		else
		{
			tasks_add(id, TASK_DEFEND, sched, 0, 0, 0, 0.0, obj_origin);
		}
	}
	else
	{
		new spot = think_defend_spot(id, wpt);
		tasks_add(id, TASK_FIND_PATH, sched, 0, 0, spot, -1.0, VEC_ZERO);
		tasks_add(id, TASK_DEFEND, sched, 0, 0, 0, 0.0, obj_origin);
	}
	return true;
}

// objective_failed.retain(id != oid).
static stock think_obj_failed_remove(id, oid)
{
	new kept = 0;
	for (new j = 0; j < g_th_i[id][TH_OBJ_FAILED_COUNT]; j++)
	{
		if (g_th_obj_failed_id[id][j] != oid)
		{
			g_th_obj_failed_id[id][kept] = g_th_obj_failed_id[id][j];
			g_th_obj_failed_until[id][kept] = g_th_obj_failed_until[id][j];
			kept++;
		}
	}
	g_th_i[id][TH_OBJ_FAILED_COUNT] = kept;
}

// think.rs Bot::objective_role: the team the port gives the objective's entity, if any; else an
// objective whose use failed lately is defended, and so is one whose text for the bot's team is
// empty or says to defend it. Reads the objective's text (only when the bot picks a new goal).
static stock think_objective_role(id, const data[], k)
{
	new ent = data[k * TSOBJ_SIZE + TSOBJ_ENT];
	if (ent > 0)
	{
		new info[TSEI_SIZE];
		if (ts_get_entity_info(ent, info) == TSBOT_OK && info[TSEI_TEAM] != 0)
		{
			return info[TSEI_TEAM] == world_team(id) ? THINK_ROLE_ATTACK : THINK_ROLE_DEFEND;
		}
	}
	new oid = data[k * TSOBJ_SIZE + TSOBJ_ID];
	for (new j = 0; j < g_th_i[id][TH_OBJ_FAILED_COUNT]; j++)
	{
		if (g_th_obj_failed_id[id][j] == oid)
		{
			return THINK_ROLE_DEFEND;
		}
	}
	new text[TSOBJ_NAME_LEN];
	if (ts_get_objective_name(id, k, text, charsmax(text)) <= 0 || text[0] == 0)
	{
		return THINK_ROLE_DEFEND;
	}
	static const words[][] = { "defend", "protect", "guard", "prevent", "stop", "hold" };
	for (new j = 0; j < sizeof(words); j++)
	{
		if (containi(text, words[j]) != -1)
		{
			return THINK_ROLE_DEFEND;
		}
	}
	return THINK_ROLE_ATTACK;
}

// think.rs Bot::defend_spot: a waypoint to defend target (a waypoint) from: 200-900 units from it,
// seeing it, not the one the bot is at (defenders rotate); target itself if none.
static stock think_defend_spot(id, target)
{
	if (target < 0)
	{
		return target;
	}
	static spots[RC_MAX_WAYPOINTS];
	new Float:to[3], Float:at[3];
	nav_origin(target, to);
	new n = 0, count = min(nav_count(), RC_MAX_WAYPOINTS), cur = g_nv_i[id][NV_CURRENT_WPT], team = g_th_i[id][TH_TEAM];
	for (new i = 0; i < count; i++)
	{
		if (i == target || i == cur)
		{
			continue;
		}
		new f = nav_flags(i);
		if ((f & (NAV_FL_DELETED | NAV_FL_AIMING | NAV_FL_UNREACHABLE)) || !think_team_allows(f, team))
		{
			continue;
		}
		nav_origin(i, at);
		new Float:d = vec_distance(at, to);
		if (d < 200.0 || d >= 900.0 || !nav_visible(i, target) || navigate_claim_factor(id, i) < 1.0)
		{
			continue;
		}
		spots[n++] = i;
	}
	return n == 0 ? target : spots[random_num(0, n - 1)];
}

// The CTB entities of a class and tsx kind: up to max into out.
static stock think_ctb_list(const classname[], kind, out[], max)
{
	new n = 0, ent = 0, info[TSEI_SIZE];
	while (n < max && (ent = find_ent_by_class(ent, classname)) > 0)
	{
		if (ts_get_entity_info(ent, info) == TSBOT_OK && info[TSEI_KIND] == kind)
		{
			out[n++] = ent;
		}
	}
	return n;
}

// The nearest base team t may capture at (its team or any), 0 none.
static stock think_ctb_nearest_base(const bases[], n, const Float:from[3], t)
{
	new best = 0, Float:best_d = 0.0, Float:o[3], info[TSEI_SIZE];
	for (new k = 0; k < n; k++)
	{
		if (ts_get_entity_info(bases[k], info) != TSBOT_OK || (info[TSEI_TEAM] != t && info[TSEI_TEAM] != 0) || !world_entity_origin(bases[k], o))
		{
			continue;
		}
		new Float:d = vec_distance(o, from);
		if (!best || d < best_d)
		{
			best = bases[k];
			best_d = d;
		}
	}
	return best;
}

// think.rs Bot::ctb_tasks: capture the briefcase (ts_flag cases, ts_briefcase capture points;
// design §4): a carrier takes it to a point its team may capture at, a teammate's carrier is
// escorted, an enemy carrier is cut off at the point it must reach, and otherwise the bot goes
// for a case its team may take. False on a map without them.
static stock bool:think_ctb_tasks(id, sched)
{
	static cases[THINK_MAX_CTB], bases[THINK_MAX_CTB];
	new nc = think_ctb_list("ts_flag", TSENT_CTB_FLAG, cases, THINK_MAX_CTB);
	new nb = think_ctb_list("ts_briefcase", TSENT_CTB_BASE, bases, THINK_MAX_CTB);
	new team = world_team(id);
	new Float:me[3], Float:o[3];
	world_origin(id, me);
	if (world_flags(id) & TSPF_HAS_FLAG)
	{
		new base = think_ctb_nearest_base(bases, nb, me, team);
		if (base)
		{
			new s = think_handle_serial(base);
			think_add_timed(id, TASK_FIND_PATH, sched, base, s, 0, 0.0, VEC_ZERO, 60.0);
			tasks_add(id, TASK_MOVE_TO_VECTOR, sched, base, s, 0, 0.0, VEC_ZERO);
			return true;
		}
	}
	new carrier = 0;
	for (new p = 1; p <= g_maxclients; p++)
	{
		if (p != id && bot_alive(p) && (world_flags(p) & TSPF_HAS_FLAG))
		{
			carrier = p;
			break;
		}
	}
	if (carrier)
	{
		new their = world_team(carrier);
		new Float:co[3];
		world_origin(carrier, co);
		if (their == team && team != 0)
		{
			// Escort: keep near the carrier.
			if (think_distance_from(id, co, false) > 300.0)
			{
				think_add_timed(id, TASK_FIND_PATH, sched, carrier, world_userid(carrier), 0, 0.0, VEC_ZERO, 8.0);
			}
			else
			{
				tasks_add(id, TASK_DEFEND, sched, 0, 0, 0, 4.0, co);
			}
			return true;
		}
		new base = think_ctb_nearest_base(bases, nb, co, their);
		if (base)
		{
			think_add_timed(id, TASK_FIND_PATH, sched, base, think_handle_serial(base), 0, 0.0, VEC_ZERO, 30.0);
			world_entity_origin(base, o);
			tasks_add(id, TASK_DEFEND, sched, 0, 0, 0, 0.0, o);
			return true;
		}
	}
	new best = 0, Float:best_d = 0.0, info[TSEI_SIZE];
	for (new k = 0; k < nc; k++)
	{
		new c = cases[k];
		if ((pev(c, pev_effects) & RC_EF_NODRAW) || ts_get_entity_info(c, info) != TSBOT_OK || !info[TSEI_AVAILABLE]
			|| (info[TSEI_TEAM] != 0 && info[TSEI_TEAM] == team) || !world_entity_origin(c, o))
		{
			continue;
		}
		new Float:d = vec_distance(o, me);
		if (!best || d < best_d)
		{
			best = c;
			best_d = d;
		}
	}
	if (best)
	{
		new s = think_handle_serial(best);
		think_add_timed(id, TASK_FIND_PATH, sched, best, s, 0, 0.0, VEC_ZERO, 60.0);
		tasks_add(id, TASK_MOVE_TO_VECTOR, sched, best, s, 0, 0.0, VEC_ZERO);
		return true;
	}
	return false;
}

// ---- think.rs Bot::look_for_new_tasks ------------------------------------------------------------

// think.rs Bot::look_for_new_tasks (CBot::LookForNewTasks, TS). The port scores its goals
// (goals_choose, design §4): an item worth the way, an enemy seen lately, a sound heard, a spot to
// hold, a teammate to regroup with; then the objectives, the end-level waypoints in teamplay, the
// squad leader, and roaming.
stock think_look_for_new_tasks(id)
{
	new Float:now = g_now;
	// Upstream stopped here, so a bot stood still for a moment at every goal it reached; the
	// tasks below give it its next move (design §4).
	new last = g_cb_i[id][CB_LAST_ENEMY];
	g_lk_i[id][LK_LOOK_TASK] = think_resolve(last, g_cb_i[id][CB_LAST_ENEMY_SERIAL]) && think_entity_alive(last) ? LOOK_SEARCH_FOR_LAST_ENEMY : LOOK_PRE_AIM;
	new sched = tasks_new_schedule_id(id);
	g_cb_i[id][CB_LOOKING_FOR_ENEMY] = 0;
	// A new goal's path may be searched at once.
	g_nv_f[id][NV_FIND_PATH_TIME] = 0.0;
	new arg, Float:vec[3], Float:score, Float:when;
	new goal = goals_choose(id, arg, vec, score, when);
	switch (goal)
	{
		case GOAL_ITEM:
		{
			new serial = think_handle_serial(arg);
			if (!config_is_on(CFG_TS_DONT_PICKUP_WEAPONS) && think_resolve(arg, serial) && think_can_pickup(id, arg))
			{
				tasks_add(id, TASK_FIND_PATH, sched, arg, serial, 0, 0.0, VEC_ZERO);
				tasks_add(id, TASK_PICKUP_ITEM, sched, arg, serial, 0, 0.0, VEC_ZERO);
				tasks_add(id, TASK_USE, sched, arg, serial, 0, 0.0, VEC_ZERO);
				navigate_claim(id, -1, arg, serial);
				return;
			}
		}
		case GOAL_HUNT:
		{
			remember_hunted(id, vec);
			new wpt = goals_predict_enemy(id, vec, now - when);
			if (wpt != -1 && !think_goal_ignored(id, wpt))
			{
				think_add_timed(id, TASK_FIND_PATH, sched, 0, 0, wpt, -1.0, VEC_ZERO, THINK_HUNT_TIME);
				new Float:at[3];
				nav_origin(wpt, at);
				tasks_add(id, TASK_SEARCH_FOR_ENEMY, sched, 0, 0, 0, random_float(3.0, 6.0), at);
				g_cb_i[id][CB_LOOKING_FOR_ENEMY] = 1;
				navigate_claim(id, wpt, 0, 0);
				return;
			}
		}
		case GOAL_INVESTIGATE:
		{
			hearing_investigate(id, vec);
			think_add_timed(id, TASK_FIND_PATH, sched, 0, 0, 0, 0.0, vec, THINK_HUNT_TIME);
			tasks_add(id, TASK_SEARCH_FOR_ENEMY, sched, 0, 0, 0, random_float(3.0, 5.0), vec);
			g_cb_i[id][CB_LOOKING_FOR_ENEMY] = 1;
			navigate_claim(id, think_nearest_waypoint(vec, 400.0, -1, false, false, false), 0, 0);
			return;
		}
		case GOAL_HOLD:
		{
			g_th_f[id][TH_HOLD_AGAIN] = now + random_float(15.0, 30.0);
			think_add_timed(id, TASK_FIND_PATH, sched, 0, 0, arg, -1.0, VEC_ZERO, THINK_HUNT_TIME);
			new Float:at[3];
			nav_origin(arg, at);
			tasks_add(id, TASK_DEFEND, sched, 0, 0, 0, 0.0, at);
			navigate_claim(id, arg, 0, 0);
			return;
		}
		case GOAL_REGROUP:
		{
			think_add_timed(id, TASK_FIND_PATH, sched, 0, 0, 0, 0.0, vec, THINK_HUNT_TIME);
			return;
		}
	}
	#pragma unused score
	if (world_teamplay() && think_objective_tasks(id, sched))
	{
		return;
	}
	if (world_teamplay())
	{
		static failed[THINK_MAX_FAILED];
		new nf = think_failed_goals(id, failed, THINK_MAX_FAILED);
		new Float:origin[3];
		world_origin(id, origin);
		new wpt = nav_random_flag_near(origin, 4096.0, NAV_FL_ENDLEVEL, -1, failed, nf);
		if (g_nv_i[id][NV_CURRENT_WPT] != wpt && wpt != -1)
		{
			tasks_add(id, TASK_FIND_PATH, 0, 0, 0, wpt, -1.0, VEC_ZERO);
			return;
		}
	}
	// In a squad (one a player made: TS bots never assemble their own), follow the leader.
	if (g_sq_i[id][SQ_LEADER])
	{
		tasks_add(id, TASK_FOLLOW_LEADER, sched, 0, 0, 0, 0.0, VEC_ZERO);
		return;
	}
	if (g_tk_count[id] == 0)
	{
		tasks_add_priority(id, TASK_FIND_PATH, sched, 0, 0, -1, -1.0, VEC_ZERO);
	}
}

// paths.ignored_goals(now).contains(wpt): the failed and backed-off goals.
static stock bool:think_goal_ignored(id, wpt)
{
	if (!g_nv_i[id][NV_PATH])
	{
		return false;
	}
	static ignored[THINK_MAX_FAILED];
	new n = min(nav_path_ignored_goals(RcPath:g_nv_i[id][NV_PATH], ignored, THINK_MAX_FAILED), THINK_MAX_FAILED);
	for (new k = 0; k < n; k++)
	{
		if (ignored[k] == wpt)
		{
			return true;
		}
	}
	return false;
}

// ---- think.rs Bot::do_tasks ----------------------------------------------------------------------

// think.rs Bot::do_tasks (CBot::DoTasks): runs the current task (slot 0), then finishes or fails
// it.
stock think_do_tasks(id)
{
	new Float:now = g_now;
	if (g_tk_count[id] > 0)
	{
		if (tasks_timed_out(id, 0))
		{
			tasks_finish_schedule(id, g_tk_i[id][0][TK_SCHEDULE_ID]);
		}
		else
		{
			tasks_remove_timed_out_schedules(id);
		}
	}
	if (g_tk_count[id] == 0)
	{
		return;
	}
	new kind = g_tk_i[id][0][TK_KIND];
	new t_int = g_tk_i[id][0][TK_INT];
	new Float:t_float = g_tk_f[id][0][TK_FLOAT];
	new Float:t_vec[3];
	vec_copy(g_tk_vec[id][0], t_vec);
	new bool:done = false, bool:failed = false, bool:path_found = false;
	// to_add: the listen task's path to a shooter it can't see (a priority task after the run).
	new add_edict = -1, add_serial = 0, add_sched = 0;
	g_th_i[id][TH_LOOK_FOR_NEW_TASKS] = 0;

	switch (kind)
	{
		case TASK_FIND_PATH:
		{
			think_task_find_path(id, done, failed, path_found);
		}
		case TASK_NORMAL_ATTACK:
		{
			think_task_normal_attack(id, done, failed);
		}
		case TASK_SEARCH_FOR_ENEMY:
		{
			if (g_cb_i[id][CB_ENEMY] || (g_th_i[id][TH_CONDITIONS] & COND_SEE_ENEMY))
			{
				failed = true;
			}
			else
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_SEARCH_FOR_ENEMY;
				if (vec_is_zero(t_vec))
				{
					movement_stop_moving(id);
				}
				else
				{
					// Searching around a place, on the move (design §4; upstream stood still).
					navigate_search_move(id, t_vec);
				}
				if (t_int == 0)
				{
					g_tk_f[id][0][TK_FLOAT] += now;
					g_tk_i[id][0][TK_INT] = 1;
					g_lk_f[id][LK_SEARCH_ENEMY_TIME] = g_tk_f[id][0][TK_FLOAT];
				}
				else if (t_float <= now)
				{
					done = true;
				}
			}
		}
		case TASK_RELOAD:
		{
			g_th_i[id][TH_CONDITIONS] |= COND_CANT_SHOOT;
			new cur = g_bw_i[id][BW_CURRENT];
			if (cur < 0 || cur >= RC_MAX_WEAPONS)
			{
				failed = true;
			}
			else if (!botweapons_has(id, cur))
			{
				failed = true;
				g_bw_i[id][BW_CURRENT] = -1;
			}
			else if (!(botweapons_need_to_reload(id, cur)
				|| (g_bw_weapon[id][cur][BW_RESERVE] > 0 && g_bw_weapon[id][cur][BW_CLIP] < g_bw_weapon[id][cur][BW_MAX_CLIP])))
			{
				// Nothing to reload (a full clip, no reserve, kung fu or a blade, whose reload key
				// blocks): done at once instead of holding the key (design §4).
				done = true;
			}
			else
			{
				if (g_cb_i[id][CB_LAST_ENEMY])
				{
					g_lk_i[id][LK_LOOK_TASK] = LOOK_SEARCH_FOR_LAST_ENEMY;
					g_lk_f[id][LK_SEARCH_ENEMY_TIME] = now + 0.5;
				}
				if (random_num(0, 100) > 0)
				{
					botweapons_reload(id);
				}
				if (t_int == 0)
				{
					g_tk_f[id][0][TK_FLOAT] = now + random_float(0.5, 1.5);
					g_tk_i[id][0][TK_INT] = 1;
				}
				else if (t_float < now)
				{
					done = true;
				}
			}
		}
		case TASK_WAIT_AND_FACE_VECTOR, TASK_FACE_VECTOR:
		{
			if (kind == TASK_WAIT_AND_FACE_VECTOR && (g_cb_i[id][CB_ENEMY] || (g_th_i[id][TH_CONDITIONS] & COND_SEE_ENEMY)))
			{
				failed = true;
			}
			else
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_VECTOR;
				if (kind == TASK_WAIT_AND_FACE_VECTOR)
				{
					movement_stop_moving(id);
				}
				if (t_int == 1)
				{
					if (t_float < now)
					{
						done = true;
					}
				}
				else
				{
					g_tk_f[id][0][TK_FLOAT] += now;
					g_tk_i[id][0][TK_INT] = 1;
				}
			}
		}
		case TASK_MOVE_TO_VECTOR:
		{
			think_task_move_to_vector(id, done, failed);
		}
		case TASK_PICKUP_ITEM:
		{
			if (t_int == 0)
			{
				g_th_i[id][TH_PICKUP_ENT] = g_tk_i[id][0][TK_EDICT];
				g_th_i[id][TH_PICKUP_SERIAL] = g_tk_i[id][0][TK_EDICT_SERIAL];
				g_tk_i[id][0][TK_INT] = 1;
				tasks_set_time_to_complete(id, 0, 10.0);
			}
			new p = g_th_i[id][TH_PICKUP_ENT], ps = g_th_i[id][TH_PICKUP_SERIAL];
			new Float:po[3];
			if (think_resolve(p, ps) && think_can_pickup(id, p) && world_entity_origin(p, po))
			{
				movement_set_move_vector(id, po);
				if (think_distance_from(id, po, false) < 64.0)
				{
					movement_use(id);
					// Pressing use on it for a while and it's still there: the game won't give it
					// (full, or a power-up held). Leave it alone (design §4).
					if (g_tk_f[id][0][TK_FLOAT] == 0.0)
					{
						g_tk_f[id][0][TK_FLOAT] = now;
					}
					else if (now - g_tk_f[id][0][TK_FLOAT] > THINK_PICKUP_TRIES)
					{
						items_untakeable(id, p, ps);
						failed = true;
					}
				}
			}
			else
			{
				failed = true;
			}
		}
		case TASK_USE:
		{
			think_task_use(id, done, failed);
		}
		case TASK_WAIT:
		{
			if (t_int == 0)
			{
				g_tk_f[id][0][TK_FLOAT] += now;
				g_tk_i[id][0][TK_INT] = 1;
			}
			if (g_tk_f[id][0][TK_FLOAT] < now)
			{
				done = true;
			}
			else
			{
				movement_stop_moving(id);
			}
		}
		case TASK_DEFEND:
		{
			// Held for 10-20 s (a set time when given), then the bot moves to another spot,
			// watching the approaches (design §4).
			if (t_int == 0)
			{
				g_tk_f[id][0][TK_FLOAT] = now + (t_float > 0.0 ? t_float : random_float(10.0, 20.0));
				g_tk_i[id][0][TK_INT] = 1;
			}
			if (g_tk_f[id][0][TK_FLOAT] < now)
			{
				done = true;
			}
			movement_stop_moving(id);
			if (!g_cb_i[id][CB_ENEMY])
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_PRE_AIM;
			}
		}
		case TASK_LISTEN_TO_SOUND:
		{
			think_task_listen(id, done, failed, add_edict, add_serial, add_sched);
		}
		case TASK_CHANGE_WEAPON:
		{
			g_th_i[id][TH_CONDITIONS] |= COND_CANT_SHOOT;
			if (!think_switch_weapon(id, t_int))
			{
				failed = true;
			}
			done = true;
		}
		case TASK_DROP_WEAPON:
		{
			// The port's drop drops the gun in hand; forget it so nothing picks it again. TS then
			// takes out the next weapon the player owns: only a gun the bot knows has rounds is
			// chosen here; otherwise the game's choice stands.
			new cur = g_bw_i[id][BW_CURRENT];
			if (cur >= 0 && cur != RC_KUNGFU)
			{
				bot_action_command(id, "drop");
				botweapons_remove(id, cur);
				g_bw_i[id][BW_CURRENT] = -1;
				g_bw_f[id][BW_NEXT_INVENTORY_SYNC] = 0.0;
				new next = botweapons_best_loaded_gun(id);
				if (next != -1)
				{
					think_switch_weapon(id, next);
				}
			}
			done = true;
		}
		case TASK_PUSH_PUSHABLE:
		{
			special_task_push_pushable(id, done, failed);
		}
		case TASK_WAIT_FOR_LIFT:
		{
			special_task_wait_for_lift(id, done);
		}
		case TASK_WAIT_FOR_ENTITY:
		{
			special_task_wait_for_entity(id, done, failed);
		}
		case TASK_HUMAN_TOWER:
		{
			special_task_human_tower(id, done);
		}
		case TASK_THROW_GRENADE:
		{
			special_task_throw_grenade(id, done, failed);
		}
		case TASK_WAIT_FOR_BOT_AT_WPT:
		{
			special_task_wait_for_bot_at_wpt(id, done, failed);
		}
		case TASK_FACE_EDICT:
		{
			special_task_face_edict(id, done, failed);
		}
		case TASK_FOLLOW_LEADER:
		{
			squad_task_follow_leader(id, failed);
		}
		case TASK_TYPE_MESSAGE:
		{
			events_task_type_message(id, done, failed);
		}
		case TASK_CROUCH:
		{
			movement_duck(id);
			if (t_int == 0)
			{
				g_tk_f[id][0][TK_FLOAT] += now;
				g_tk_i[id][0][TK_INT] = 1;
			}
			else if (t_float < now)
			{
				done = true;
			}
		}
	}

	if (g_tk_count[id] > 0)
	{
		new cur_kind = g_tk_i[id][0][TK_KIND];
		if (failed)
		{
			if (cur_kind == TASK_FIND_PATH)
			{
				g_nv_i[id][NV_LAST_FAILED_WPT] = -1;
			}
			done = true;
		}
		if (cur_kind == TASK_FIND_PATH && !g_tk_i[id][0][TK_FOUND_PATH])
		{
			if (path_found)
			{
				g_mv_i[id][MV_MOVE_TO_VALID] = 1;
			}
			g_tk_i[id][0][TK_FOUND_PATH] = path_found;
		}
		if (done)
		{
			tasks_finished_current(id, failed);
			if (cur_kind == TASK_NORMAL_ATTACK)
			{
				g_cb_i[id][CB_ENEMY] = 0;
				g_cb_i[id][CB_ENEMY_SERIAL] = 0;
			}
		}
	}
	if (add_edict != -1)
	{
		tasks_add_priority(id, TASK_FIND_PATH, add_sched, add_edict, add_serial, -1, -2.0, VEC_ZERO);
	}
}

// think.rs path_deadline: a path just found must be walked within PATH_TIMEOUT unless the task
// has a time already (design §4).
static stock think_path_deadline(id)
{
	if (g_tk_count[id] > 0 && g_tk_i[id][0][TK_KIND] == TASK_FIND_PATH && g_tk_f[id][0][TK_TIME_TO_COMPLETE] == -1.0)
	{
		tasks_set_time_to_complete(id, 0, THINK_PATH_TIMEOUT);
	}
}

// think.rs push_current_on_path: if (m_stBotPaths.IsEmpty() || top != current) push(current).
static stock think_push_current_on_path(id)
{
	if (g_nv_i[id][NV_PATH])
	{
		nav_path_push(RcPath:g_nv_i[id][NV_PATH], g_nv_i[id][NV_CURRENT_WPT], true);
	}
}

// think.rs nearest_to_origin: NearestWaypointToOrigin(v, m_iLastFailedWaypoint, &m_FailedGoals).
static stock think_nearest_to_origin(id, const Float:v[3])
{
	static failed[THINK_MAX_FAILED];
	new nf = think_failed_goals(id, failed, THINK_MAX_FAILED);
	new ignore = g_nv_i[id][NV_LAST_FAILED_WPT];
	return nav_nearest(v, NAV_REACHABLE_RANGE, ignore, 0, failed, nf);
}

// think.rs task_find_path (BOT_TASK_FIND_PATH): int = goal waypoint (-1 random), float = mode
// (-1 random goal, -2 find the path and finish, -3 search pending, 0 go to the vector).
static stock think_task_find_path(id, &bool:done, &bool:failed, &bool:path_found)
{
	new Float:now = g_now;
	new t_int = g_tk_i[id][0][TK_INT];
	new Float:t_float = g_tk_f[id][0][TK_FLOAT];
	new Float:t_vec[3];
	vec_copy(g_tk_vec[id][0], t_vec);
	new target = g_tk_i[id][0][TK_EDICT];
	if (!think_resolve(target, g_tk_i[id][0][TK_EDICT_SERIAL]))
	{
		target = 0;
	}
	new RcPath:path = RcPath:g_nv_i[id][NV_PATH];
	new Float:origin[3];
	world_origin(id, origin);
	g_mv_f[id][MV_IDEAL_MOVE_SPEED] = g_mv_f[id][MV_MAX_SPEED];
	if (!g_tk_i[id][0][TK_FOUND_PATH])
	{
		if ((!path || nav_path_length(path) == 0) && !g_mv_i[id][MV_NOT_FOLLOWING_WAYPOINT])
		{
			movement_stop_moving(id);
		}
		if (t_float == -3.0)
		{
			if (g_nv_i[id][NV_CURRENT_WPT] == -1)
			{
				g_nv_i[id][NV_CURRENT_WPT] = think_nearest_waypoint(origin, NAV_REACHABLE_RANGE, g_nv_i[id][NV_LAST_FAILED_WPT], true, false, true);
			}
			new NavPathStatus:r = navigate_search_path(id, g_nv_i[id][NV_CURRENT_WPT], g_nv_i[id][NV_GOAL_WPT], true);
			if (r == NAV_PATH_FAILED)
			{
				failed = true;
				return;
			}
			if (r != NAV_PATH_FOUND)
			{
				return;
			}
			think_push_current_on_path(id);
			g_mv_i[id][MV_MOVE_TO_VALID] = 1;
			path_found = true;
			think_path_deadline(id);
			g_tk_i[id][0][TK_FOUND_PATH] = 1;
			if (path)
			{
				nav_path_clear_failed_goals(path);
			}
			if (g_tk_f[id][0][TK_FLOAT] == -2.0)
			{
				done = true;
			}
			g_tk_f[id][0][TK_FLOAT] = 1.0;
		}
		else if (t_float == -2.0 || g_nv_f[id][NV_FIND_PATH_TIME] < now)
		{
			g_nv_f[id][NV_FIND_PATH_TIME] = now + 0.5;
			new Float:o[3];
			if (t_float == -1.0 && t_int == -1)
			{
				new goal = navigate_roam_goal(id);
				g_nv_i[id][NV_GOAL_WPT] = goal;
				g_tk_i[id][0][TK_INT] = goal;
				if (goal == -1)
				{
					failed = true;
					return;
				}
				nav_origin(goal, g_tk_vec[id][0]);
			}
			else if (target && world_entity_origin(target, o))
			{
				vec_copy(o, g_tk_vec[id][0]);
				g_nv_i[id][NV_GOAL_WPT] = think_nearest_to_origin(id, o);
			}
			else if (t_float == 0.0)
			{
				g_tk_f[id][0][TK_FLOAT] = 1.0;
				g_nv_i[id][NV_GOAL_WPT] = think_nearest_to_origin(id, t_vec);
			}
			else
			{
				g_nv_i[id][NV_GOAL_WPT] = t_int;
			}
			new goal = g_nv_i[id][NV_GOAL_WPT];
			if (goal == -1)
			{
				failed = true;
				return;
			}
			g_nv_i[id][NV_CURRENT_WPT] = think_nearest_waypoint(origin, NAV_REACHABLE_RANGE, g_nv_i[id][NV_LAST_FAILED_WPT], true, false, false);
			g_tk_i[id][0][TK_INT] = goal;
			if (vec_is_zero(g_tk_vec[id][0]))
			{
				nav_origin(goal, g_tk_vec[id][0]);
			}
			new cur = g_nv_i[id][NV_CURRENT_WPT];
			if (cur == -1)
			{
				failed = true;
				return;
			}
			vec_copy(g_tk_vec[id][0], g_nv_v[id][NV_GOAL_ORIGIN]);
			g_nv_i[id][NV_GOAL_ORIGIN_SET] = 1;
			if (goal == cur)
			{
				if (path)
				{
					nav_path_push(path, cur, false);
				}
				g_mv_i[id][MV_MOVE_TO_VALID] = 1;
				path_found = true;
				return;
			}
			new NavPathStatus:r = navigate_search_path(id, cur, goal, false);
			if (r == NAV_PATH_FAILED)
			{
				failed = true;
				return;
			}
			if (r != NAV_PATH_FOUND)
			{
				g_tk_f[id][0][TK_FLOAT] = -3.0;
				return;
			}
			think_push_current_on_path(id);
			g_mv_i[id][MV_MOVE_TO_VALID] = 1;
			path_found = true;
			think_path_deadline(id);
			if (t_float == -2.0)
			{
				done = true;
			}
		}
	}
	else
	{
		new last = g_cb_i[id][CB_LAST_ENEMY];
		if (g_cb_i[id][CB_NEAREST_REMEMBER_VISIBLE] && !g_cb_i[id][CB_ENEMY] && g_cb_i[id][CB_LOOKING_FOR_ENEMY])
		{
			g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_NEAREST_REMEMBER_POS;
		}
		else if (think_resolve(last, g_cb_i[id][CB_LAST_ENEMY_SERIAL]) && think_entity_alive(last) && g_cb_f[id][CB_LAST_SEE_ENEMY_TIME] + 6.0 > now)
		{
			g_lk_i[id][LK_LOOK_TASK] = LOOK_SEARCH_FOR_LAST_ENEMY;
			g_lk_f[id][LK_SEARCH_ENEMY_TIME] = now + 0.5;
		}
		else
		{
			// Upstream looked where it walked; the port checks where enemies may appear.
			g_lk_i[id][LK_LOOK_TASK] = LOOK_PRE_AIM;
		}
		new cur = g_nv_i[id][NV_CURRENT_WPT], goal = g_nv_i[id][NV_GOAL_WPT];
		if (cur == -1 || goal != t_int || t_int == -1)
		{
			g_tk_i[id][0][TK_FOUND_PATH] = 0;
			return;
		}
		new Float:eo[3], Float:eyes[3];
		if (target && goal == cur && world_entity_origin(target, eo))
		{
			world_eyes(id, eyes);
			if (think_distance_from(id, eo, false) < nav_distance(goal, eyes))
			{
				done = true;
			}
		}
		g_mv_i[id][MV_MOVE_TO_VALID] = 1;
	}
}

// think.rs do_tasks' BOT_TASK_MOVE_TO_VECTOR: edict or vector, float = how near.
static stock think_task_move_to_vector(id, &bool:done, &bool:failed)
{
	g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_VECTOR;
	new target = g_tk_i[id][0][TK_EDICT];
	new Float:me[3], Float:origin[3], Float:o[3];
	world_origin(id, me);
	if (think_resolve(target, g_tk_i[id][0][TK_EDICT_SERIAL]) && world_entity_origin(target, o))
	{
		new Float:size[3];
		pev(target, pev_size, size);
		new Float:d = vec_distance_2d(o, me);
		if (d <= vec_length_2d(size) || d < think_distance_from(id, g_tk_vec[id][0], false))
		{
			done = true;
		}
		vec_copy(o, origin);
	}
	else
	{
		if (think_distance_from(id, g_tk_vec[id][0], false) < g_tk_f[id][0][TK_FLOAT])
		{
			done = true;
		}
		vec_copy(g_tk_vec[id][0], origin);
	}
	if (done)
	{
		return;
	}
	if (vec_distance_2d(origin, me) <= 100.0)
	{
		movement_stop_moving(id);
		done = true;
	}
	else if (g_th_i[id][TH_CONDITIONS] & COND_SEE_NEXT_WAYPOINT)
	{
		if (origin[2] > me[2] + THINK_MAX_JUMP_HEIGHT)
		{
			failed = true;
		}
		movement_set_move_vector(id, origin);
	}
	else
	{
		failed = true;
	}
}

// think.rs task_normal_attack (BOT_TASK_NORMAL_ATTACK, TS).
static stock think_task_normal_attack(id, &bool:done, &bool:failed)
{
	new Float:now = g_now;
	new enemy = g_tk_i[id][0][TK_EDICT], serial = g_tk_i[id][0][TK_EDICT_SERIAL];
	new Float:t_float = g_tk_f[id][0][TK_FLOAT];
	g_cb_i[id][CB_ENEMY] = enemy;
	g_cb_i[id][CB_ENEMY_SERIAL] = serial;
	new Float:eo[3];
	if (!think_resolve(enemy, serial) || !world_entity_origin(enemy, eo))
	{
		g_cb_i[id][CB_LAST_ENEMY] = 0;
		g_cb_i[id][CB_LAST_ENEMY_SERIAL] = 0;
		done = true;
		return;
	}
	if (!combat_is_enemy(id, enemy))
	{
		g_cb_i[id][CB_LAST_ENEMY] = 0;
		g_cb_i[id][CB_LAST_ENEMY_SERIAL] = 0;
		g_cb_i[id][CB_ENEMY] = 0;
		g_cb_i[id][CB_ENEMY_SERIAL] = 0;
		done = true;
		return;
	}
	if (t_float != 0.0)
	{
		movement_stop_moving(id);
		if (t_float < now)
		{
			done = true;
			return;
		}
	}
	// SEE_ENEMY can be stale: the port asks for a clear line to the enemy itself every time
	// (design §4); combat_fvisible clears the condition when blocked.
	new Float:face[3];
	if (g_th_i[id][TH_CONDITIONS] & COND_SEE_ENEMY)
	{
		combat_fvisible(id, enemy);
	}
	else if (kungfu_face(id, face) && combat_fvisible(id, enemy))
	{
		// A kung fu wall jump turns the bot's back to the opponent for a moment: it still knows
		// where the opponent is.
		g_th_i[id][TH_CONDITIONS] |= COND_SEE_ENEMY;
	}
	if (g_th_i[id][TH_CONDITIONS] & COND_SEE_ENEMY)
	{
		new Float:eyes[3];
		world_eyes(id, eyes);
		g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_ENEMY;
		new Float:dist = vec_distance(eo, eyes);
		g_cb_i[id][CB_LAST_ENEMY] = enemy;
		g_cb_i[id][CB_LAST_ENEMY_SERIAL] = serial;
		vec_copy(eo, g_cb_v[id][CB_LAST_SEE_ENEMY_POS]);
		fire_saw_enemy(id, enemy);
		fire_use_powerup(id, dist);
		new cur = g_bw_i[id][BW_CURRENT];
		new bool:has_cur = cur >= 0 && cur < RC_MAX_WEAPONS;
		if (has_cur && botweapons_is_melee(id, cur) && pev(id, pev_movetype) == RC_MOVETYPE_FLY && random_num(0, 1) != 0)
		{
			movement_jump(id);
		}
		new weap = has_cur ? g_bw_weapon[id][cur][BW_ID] : 0;
		// TS's melee weapons: combat knife (25), katana (34), seal knife (35) and kung fu (design §5).
		new bool:melee = !has_cur || weap == 25 || weap == 34 || weap == 35 || weap == RC_KUNGFU;
		if (weap == RC_KUNGFU)
		{
			// The port's kung fu fighter (design §4).
			kungfu_fight(id, enemy, dist);
		}
		else if (melee)
		{
			movement_set_move_vector(id, eo);
			if (dist < 256.0)
			{
				combat_decide_jump_duck_strafe(id, dist, eo);
			}
			// TS 3.0's +attack2 throws a knife and the katana: slash with +attack, and throw only a
			// spare knife at a target out of reach (design §4).
			if (dist < 72.0)
			{
				bot_primary_attack(id);
				if (!config_is_on(CFG_TS_DONT_STEAL_WEAPONS))
				{
					movement_use(id);
				}
			}
			else if ((weap == 25 || weap == 35) && dist >= 150.0 && dist < 500.0 && has_cur && g_bw_weapon[id][cur][BW_CLIP] > 1
				&& fire_in_aim(id, enemy, dist))
			{
				bot_secondary_attack(id);
			}
		}
		else
		{
			new Float:mv[3], Float:ev[3], Float:inputs[5];
			world_velocity(id, mv);
			if (enemy <= MAX_PLAYERS)
			{
				world_velocity(enemy, ev);
			}
			else
			{
				pev(enemy, pev_velocity, ev);
			}
			new Float:enemy_max;
			pev(enemy, pev_maxspeed, enemy_max);
			inputs[0] = float(world_field(id, TSPS_STANCE)) / 2.0;
			inputs[1] = vec_length(mv) / g_mv_f[id][MV_MAX_SPEED];
			inputs[2] = vec_length(ev) / enemy_max;
			inputs[3] = look_yaw_angle_to(id, eo) / 180.0;
			inputs[4] = float(weap) / float(RC_MAX_WEAPONS);
			// The perceptron no longer presses +alt1 every think it fires: it is one way a dodge
			// dive starts (design §4).
			new RcLearn:h = RcLearn:g_ln_i[id][LN_HANDLE];
			new bool:stunt = h ? rl_bot_decide(h, RlDecide_Stunt, inputs, 5) : false;
			fire_dodge(id, enemy, stunt);
			// Losing the trade (under 38% health, the enemy healthier): break off to cover.
			new Float:health = world_health(id);
			if (health < think_max_health(id) * 0.38 && think_entity_health(enemy) > health)
			{
				combat_run_for_cover(id, eo, false);
			}
			fire_choose_weapon(id, dist);
			g_fi_f[id][FI_SPREAD_LIMIT] = fire_spread_limit(id, dist);
			g_fi_f[id][FI_SPREAD_LIMIT_UNTIL] = now + 0.1;
			if (has_cur && botweapons_need_to_reload(id, cur))
			{
				combat_run_for_cover(id, eo, false);
				botweapons_reload(id);
			}
			else if (g_mv_f[id][MV_HOLD_ATTACK_TIME] > now || fire_ready_to_fire(id, enemy, dist))
			{
				bot_primary_attack(id);
			}
			fire_combat_move(id, eo, dist);
		}
	}
	else if (think_same_handle(g_cb_i[id][CB_LAST_ENEMY], g_cb_i[id][CB_LAST_ENEMY_SERIAL], enemy, serial))
	{
		// HasSeenEnemy.
		new Float:last[3];
		vec_copy(g_cb_v[id][CB_LAST_SEE_ENEMY_POS], last);
		g_lk_f[id][LK_SEARCH_ENEMY_TIME] = now + random_float(5.0, 10.0);
		combat_remember_lost_enemy(id, last, enemy);
		fire_grenade_behind_cover(id);
		if (fire_hold_angle(id))
		{
			// Hold the angle where it went, aiming where it will show (design §4).
			movement_stop_moving(id);
			g_mv_f[id][MV_STRAFE_TIME] = 0.0;
			g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_ENEMY;
		}
		else if (combat_fvisible_point(id, last) && (g_fi_f[id][FI_LAST_SIGHT] == 0.0 || g_fi_f[id][FI_LAST_SIGHT] + 1.0 > now))
		{
			// The bot goes after the enemy a second after losing it (design §4).
			g_th_i[id][TH_CONDITIONS] |= COND_SEE_ENEMY;
			g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_ENEMY;
		}
		else if (!combat_want_to_follow_enemy(id))
		{
			failed = true;
		}
		else if (think_distance_from(id, last, false) > 100.0)
		{
			tasks_finished_current(id, false);
			new sched = tasks_new_schedule_id(id);
			if (!tasks_has(id, TASK_SEARCH_FOR_ENEMY))
			{
				tasks_add_priority(id, TASK_SEARCH_FOR_ENEMY, sched, 0, 0, 0, random_float(3.0, 6.0), VEC_ZERO);
				new Float:secs = random_float(10.0, 20.0);
				tasks_set_time_to_complete(id, tasks_add_priority(id, TASK_FIND_PATH, sched, 0, 0, 0, 0.0, last), secs);
			}
		}
		else
		{
			g_lk_i[id][LK_LOOK_TASK] = LOOK_SEARCH_FOR_ENEMY;
			failed = true;
		}
	}
	else
	{
		g_lk_i[id][LK_LOOK_TASK] = LOOK_SEARCH_FOR_LAST_ENEMY;
		g_cb_i[id][CB_ENEMY] = 0;
		g_cb_i[id][CB_ENEMY_SERIAL] = 0;
		g_cb_i[id][CB_LAST_ENEMY] = 0;
		g_cb_i[id][CB_LAST_ENEMY_SERIAL] = 0;
		done = true;
	}
}

// think.rs task_use (BOT_TASK_USE): edict = what to use, int = state (-2/-1 no path needed,
// 1 pressing), float = how long; a momentary button is held until what it targets stops moving.
static stock think_task_use(id, &bool:done, &bool:failed)
{
	new Float:now = g_now;
	new t_int = g_tk_i[id][0][TK_INT];
	new e = g_tk_i[id][0][TK_EDICT];
	if (t_int != -2 && t_int != -1 && !g_tk_i[id][0][TK_FOUND_PATH])
	{
		failed = true;
		return;
	}
	new Float:o[3];
	if (!think_resolve(e, g_tk_i[id][0][TK_EDICT_SERIAL]) || !world_entity_origin(e, o))
	{
		failed = true;
		return;
	}
	g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_EDICT;
	movement_stop_moving(id);
	if (t_int == 1)
	{
		new Float:until = g_tk_vec[id][0][0];
		if (until == 0.0)
		{
			until = now + g_tk_f[id][0][TK_FLOAT];
			vec_set(g_tk_vec[id][0], until, 0.0, 0.0);
		}
		movement_use(id);
		if (until > now)
		{
			new cls[32];
			think_classname(e, cls, charsmax(cls));
			if (equal(cls, "momentary", 9))
			{
				done = true;
				new target[64];
				pev(e, pev_target, target, charsmax(target));
				if (target[0])
				{
					new d = 0;
					while ((d = find_ent_by_tname(d, target)) > 0)
					{
						if (think_entity_moving(d))
						{
							done = false;
							break;
						}
					}
				}
			}
		}
		else
		{
			done = until <= now;
		}
		return;
	}
	new Float:me[3];
	world_origin(id, me);
	if (vec_distance_2d(o, me) < 24.0 && floatabs(o[2] - me[2]) > THINK_MAX_JUMP_HEIGHT)
	{
		failed = true;
		return;
	}
	if (t_int == -1 || think_distance_from(id, o, false) < 72.0)
	{
		if (think_facing(id, e))
		{
			if (t_int == -1)
			{
				if (random_num(0, 1) != 0)
				{
					movement_use(id);
					done = true;
				}
				return;
			}
			g_tk_i[id][0][TK_INT] = 1;
			movement_use(id);
		}
	}
	else
	{
		movement_set_move_vector(id, o);
	}
}

// think.rs task_listen (BOT_TASK_LISTEN_TO_SOUND): edict = who made it, float = how long. A
// shooter it can't see gets a path to it (add_* out: the to_add task, add_edict -1 for none).
static stock think_task_listen(id, &bool:done, &bool:failed, &add_edict, &add_serial, &add_sched)
{
	new Float:now = g_now;
	if ((g_cb_i[id][CB_ENEMY] && (g_th_i[id][TH_CONDITIONS] & COND_SEE_ENEMY)) || pev(id, pev_movetype) == RC_MOVETYPE_FLY)
	{
		failed = true;
		return;
	}
	new sound = g_tk_i[id][0][TK_EDICT], serial = g_tk_i[id][0][TK_EDICT_SERIAL];
	if (!think_resolve(sound, serial))
	{
		failed = true;
		return;
	}
	new state_ = g_tk_i[id][0][TK_INT];
	new Float:until = g_tk_f[id][0][TK_FLOAT];
	new Float:v[3];
	vec_copy(g_tk_vec[id][0], v);
	if (state_ == 0)
	{
		until = now + g_tk_f[id][0][TK_FLOAT];
		state_ = 1;
		pev(sound, pev_origin, v);
	}
	else if (state_ == 1)
	{
		pev(sound, pev_origin, v);
		if (pev(sound, pev_button) & IN_ATTACK)
		{
			if (combat_is_visible(id, sound))
			{
				if (!combat_is_enemy(id, sound))
				{
					state_ = 2;
				}
			}
			else if (g_th_f[id][TH_INVESTIGATE_SOUND_TIME] < now)
			{
				g_th_f[id][TH_INVESTIGATE_SOUND_TIME] = now + random_float(8.0, 16.0);
				add_edict = sound;
				add_serial = serial;
				add_sched = g_tk_i[id][0][TK_SCHEDULE_ID];
			}
		}
	}
	else if (state_ == 2)
	{
		new Float:ang[3], Float:fwd[3], Float:right[3], Float:ofs[3];
		pev(sound, pev_v_angle, ang);
		make_vectors(ang, fwd, right);
		pev(sound, pev_origin, v);
		pev(sound, pev_view_ofs, ofs);
		vec_add(v, ofs, v);
		vec_mad(v, fwd, 8192.0, v);
		state_ = 3;
	}
	g_tk_f[id][0][TK_FLOAT] = until;
	g_tk_i[id][0][TK_INT] = state_;
	vec_copy(v, g_tk_vec[id][0]);
	g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_VECTOR;
	if (until <= now)
	{
		done = true;
		g_th_f[id][TH_LISTEN_TO_SOUND_TIME] = !tasks_has(id, TASK_USE) ? now + g_th_f[id][TH_REACTION_TIME] * 4.0 : now + random_float(4.5, 10.0);
	}
}
