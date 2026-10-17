// rcbot/think.inc: CBot::Think, SpawnInit, the TS loadout choice, task selection.
// Owner: think. Ports bot/think.rs (Bot::think, spawn_init, select_loadout, learn_items,
// item_taken, switch_weapon, hear_gunfire, drop_roaming_for_sound, can_pickup, facing,
// objective_tasks, objective_role, defend_spot, ctb_tasks, look_for_new_tasks, do_tasks,
// task_find_path, path_deadline, push_current_on_path, nearest_to_origin, task_normal_attack,
// task_use, task_listen, is_grenade_rocket, trace_toss). Bot::frame / teleported / command_ran
// are in manager.inc.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: the dead branch of Think and SpawnInit are ported; the living branch is
// minimal.inc's placeholder until TODO(think) ports think.rs:330-494.

#if defined _rcbot_think_included
	#endinput
#endif
#define _rcbot_think_included

// think.rs Bot::think (CBot::Think).
stock think_bot(id)
{
	new Float:now = g_now;
	new Float:v[3];
	world_velocity(id, v);
	g_th_f[id][TH_VELOCITY_3D] = vec_length(v);
	g_th_f[id][TH_VELOCITY_2D] = vec_length_2d(v);
	g_th_i[id][TH_LOOK_FOR_NEW_TASKS] = 1;
	g_mv_f[id][MV_MOVE_SPEED] = 0.0;
	if (g_mv_f[id][MV_UP_TIME] < now)
	{
		g_mv_f[id][MV_UP_SPEED] = 0.0;
	}
	// NotStartedGame is false in TS; the port's rules team (from 1) is RCBot's team from 0.
	new team = world_team(id);
	g_th_i[id][TH_TEAM] = world_teamplay() && team > 0 ? team - 1 : -1;

	// TODO(navigate): failed goals expiry (paths.failed_goals, expire_backed_off_goals).
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

	// TODO(think): think.rs:338-494 (remembered/hearing ticks, work_view_angles,
	// work_move_direction, check_stuck, dive_landing, the think-time gate, select_loadout for an
	// adopted bot, learn_items, update_conditions, update_visibles, grenades, reload/switch,
	// can-see-waypoint, task edict, follow_formation / update_waypoint, sounds and idle chat,
	// squad_modes, do_tasks, learn_think, look_for_new_tasks, update_pre_aim, find_enemy,
	// share_with_team). Until then the placeholder wanders and shoots.
	minimal_think_alive(id);
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
// BOT_CONFIG_TS_KUNGFU. The GA still picks its genes; the loadout bought is the bandit's.
stock think_select_loadout(id)
{
	if (g_th_i[id][TH_CONDITIONS] & COND_SELECTED_GUN)
	{
		return;
	}
	// TODO(think): learned.select_ts_weapons (rl_bot_select_ts_weapons) and the bandit's
	// choose_loadout (learning_choose_loadout); minimal_loadout until then.
	new ids[RC_MAX_LOADOUT], n = minimal_loadout(id, ids);
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

// TODO(think): think.rs Bot::look_for_new_tasks.
stock think_look_for_new_tasks(id)
{
	#pragma unused id
}

// TODO(think): think.rs Bot::do_tasks (the task runner; task_* functions).
stock think_do_tasks(id)
{
	#pragma unused id
}

// TODO(think): think.rs Bot::learn_items.
stock think_learn_items(id)
{
	#pragma unused id
}

// TODO(think): think.rs Bot::switch_weapon: asks for weapon id (RCBot id) unless pending.
stock bool:think_switch_weapon(id, weapon)
{
	#pragma unused id, weapon
	return false;
}

// TODO(think): think.rs Bot::objective_tasks / objective_role / defend_spot / ctb_tasks.
stock bool:think_objective_tasks(id, objective)
{
	#pragma unused id, objective
	return false;
}

// TODO(think): think.rs Bot::can_pickup.
stock bool:think_can_pickup(id, ent)
{
	#pragma unused id, ent
	return false;
}

// TODO(think): think.rs Bot::facing.
stock bool:think_facing(id, ent)
{
	#pragma unused id, ent
	return false;
}

// TODO(think): think.rs is_grenade_rocket / trace_toss.
stock bool:think_is_grenade_rocket(ent)
{
	#pragma unused ent
	return false;
}
