// rcbot/movement.inc: how the bot moves: speeds, avoidance, stuck handling, dives, the usercmd.
// Owner: movement. Ports bot/movement.rs (Unstick, avoid_angle, speed_limit,
// work_move_direction, react_to_stuck, give_up_stuck, stunt_ready, dive, dive_landing,
// check_stuck, run_player_move) and bot/mod.rs's movement helpers (jump, duck, use_button,
// alt_button, stop_moving, set_move_vector, duck_and_jump, jump_and_duck, ladder_dir,
// climb_type) and the movement part of think.rs teleported / spawn_init.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: run_player_move and the small helpers are ported; the rest is TODO(movement).

#if defined _rcbot_movement_included
	#endinput
#endif
#define _rcbot_movement_included

// movement.rs STUCK_SUICIDE and the unstick / dive timings.
#define STUCK_SUICIDE			20.0
#define UNSTICK_SERIES_TIME		4.0
#define UNSTICK_SERIES_RANGE	128.0
#define WALL_JUMP_RANGE			48.0
#define DIVE_DUCK_FROM			0.15
#define DIVE_DUCK_UNTIL			1.5
#define DIVE_GET_UP_AFTER		0.6

// movement.rs Bot::run_player_move (CBot::RunPlayerMove): the usercmd into g_co_* CO_CMD_*. Its
// msec is the server's (TSBOT_AUTOMSEC).
stock movement_run_player_move(id)
{
	new Float:now = g_now;
	fix_angles(g_co_v[id][CO_VANGLE]);
	// The stay-near speed is applied where the move is worked out (speed_limit).
	if ((g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_CROUCH) || (g_mv_f[id][MV_START_DUCK_TIME] <= now && g_mv_f[id][MV_END_DUCK_TIME] > now))
	{
		movement_duck(id);
	}
	if (g_mv_f[id][MV_START_JUMP_TIME] <= now && g_mv_f[id][MV_END_JUMP_TIME] > now)
	{
		movement_jump(id);
	}
	if (g_mv_f[id][MV_HOLD_ATTACK_TIME] > now)
	{
		bot_primary_attack(id);
	}
	g_co_i[id][CO_CMD_BUTTONS] = fire_buttons(id, g_co_i[id][CO_BUTTON]);
	new Float:forward_speed, Float:side_speed;
	fire_capped_speeds(id, forward_speed, side_speed);
	vec_copy(g_co_v[id][CO_VANGLE], g_co_v[id][CO_CMD_ANGLES]);
	g_co_f[id][CO_CMD_FWD] = forward_speed;
	g_co_f[id][CO_CMD_SIDE] = side_speed;
	g_co_f[id][CO_CMD_UP] = g_mv_f[id][MV_UP_SPEED];
	g_co_i[id][CO_CMD_IMPULSE] = 0;
}

// mod.rs Bot::jump / duck / use_button / alt_button.
stock movement_jump(id)
{
	g_co_i[id][CO_BUTTON] |= IN_JUMP;
}

stock movement_duck(id)
{
	g_co_i[id][CO_BUTTON] |= IN_DUCK;
}

stock movement_use(id)
{
	g_co_i[id][CO_BUTTON] |= IN_USE;
}

stock movement_alt(id)
{
	g_co_i[id][CO_BUTTON] |= IN_ALT1;
}

// mod.rs Bot::primary_attack: nothing while reloading.
stock bot_primary_attack(id)
{
	if (!(g_co_i[id][CO_BUTTON] & IN_RELOAD))
	{
		g_co_i[id][CO_BUTTON] |= IN_ATTACK;
	}
}

// mod.rs Bot::secondary_attack.
stock bot_secondary_attack(id)
{
	g_co_i[id][CO_BUTTON] |= IN_ATTACK2;
}

// mod.rs Bot::stop_moving.
stock movement_stop_moving(id)
{
	g_mv_i[id][MV_MOVE_TO_VALID] = 0;
}

// mod.rs Bot::set_move_vector.
stock movement_set_move_vector(id, const Float:v[3])
{
	vec_copy(v, g_mv_v[id][MV_MOVE_TO]);
	g_mv_i[id][MV_MOVE_TO_VALID] = 1;
}

// mod.rs Bot::duck_and_jump.
stock movement_duck_and_jump(id)
{
	new Float:now = g_now;
	g_mv_f[id][MV_START_JUMP_TIME] = now + 0.1;
	g_mv_f[id][MV_END_JUMP_TIME] = now + 0.2;
	g_mv_f[id][MV_START_DUCK_TIME] = now;
	g_mv_f[id][MV_END_DUCK_TIME] = now + 0.2;
}

// mod.rs Bot::jump_and_duck.
stock movement_jump_and_duck(id)
{
	new Float:now = g_now;
	if (g_mv_f[id][MV_END_JUMP_TIME] < now)
	{
		g_mv_f[id][MV_START_JUMP_TIME] = now;
		g_mv_f[id][MV_END_JUMP_TIME] = now + 0.1;
	}
	if (g_mv_f[id][MV_END_DUCK_TIME] < now)
	{
		g_mv_f[id][MV_START_DUCK_TIME] = now + 0.1;
		g_mv_f[id][MV_END_DUCK_TIME] = now + 0.7;
	}
}

// think.rs Bot::teleported, movement's part: stuck timers and the unstick series.
stock movement_teleported(id)
{
	g_mv_f[id][MV_STUCK_TIME] = 0.0;
	g_mv_f[id][MV_LAST_NO_MOVE] = 0.0;
	movement_reset_unstick(id);
}

stock movement_reset_unstick(id)
{
	g_mv_i[id][MV_UNSTICK_LEVEL] = 0;
	g_mv_i[id][MV_UNSTICK_EDGE_PREV] = 0;
	g_mv_i[id][MV_UNSTICK_EDGE_CUR] = 0;
	g_mv_i[id][MV_UNSTICK_REACTED] = 0;
	g_mv_f[id][MV_UNSTICK_SINCE] = 0.0;
	g_mv_f[id][MV_UNSTICK_LAST] = 0.0;
	g_mv_f[id][MV_UNSTICK_BACK_UNTIL] = 0.0;
	g_mv_f[id][MV_UNSTICK_BACK_SIDE] = 0.0;
	vec_zero(g_mv_v[id][MV_UNSTICK_AT]);
}

// think.rs Bot::spawn_init, movement's part.
stock movement_spawn_init(id)
{
	vec_zero(g_mv_v[id][MV_AVOID_MOVE_TO]);
	g_mv_f[id][MV_AVOID_TIME] = 0.0;
	g_mv_f[id][MV_HOLD_ATTACK_TIME] = 0.0;
	g_mv_i[id][MV_NOT_FOLLOWING_WAYPOINT] = 0;
	g_mv_f[id][MV_LAST_NO_MOVE] = 0.0;
	g_mv_f[id][MV_UP_TIME] = 0.0;
	g_mv_i[id][MV_MOVE_TO_VALID] = 0;
	g_mv_f[id][MV_STUCK_TIME] = 0.0;
	movement_reset_unstick(id);
	g_mv_f[id][MV_DIVE_TIME] = 0.0;
	g_mv_f[id][MV_MOVE_SPEED] = 0.0;
	g_mv_f[id][MV_UP_SPEED] = 0.0;
	g_mv_f[id][MV_STRAFE_SPEED] = 0.0;
	g_mv_f[id][MV_STRAFE_TIME] = 0.0;
	g_mv_f[id][MV_START_JUMP_TIME] = 0.0;
	g_mv_f[id][MV_END_JUMP_TIME] = 0.0;
	g_mv_f[id][MV_START_DUCK_TIME] = 0.0;
	g_mv_f[id][MV_END_DUCK_TIME] = 0.0;
	g_mv_i[id][MV_AVOID_ENT] = 0;
	vec_zero(g_mv_v[id][MV_MOVE_TO]);
	g_mv_f[id][MV_IDEAL_MOVE_SPEED] = g_mv_f[id][MV_MAX_SPEED];
}

// TODO(movement): movement.rs Bot::work_move_direction (move_to → move/strafe speeds,
// avoidance, ladders, water).
stock movement_work_move_direction(id)
{
	#pragma unused id
}

// TODO(movement): movement.rs Bot::check_stuck (+ react_to_stuck, give_up_stuck).
stock movement_check_stuck(id)
{
	#pragma unused id
}

// TODO(movement): movement.rs Bot::stunt_ready.
stock bool:movement_stunt_ready(id)
{
	#pragma unused id
	return false;
}

// TODO(movement): movement.rs Bot::dive (TS's +alt1 stunt on its own).
stock bool:movement_dive(id)
{
	#pragma unused id
	return false;
}

// TODO(movement): movement.rs Bot::dive_landing.
stock movement_dive_landing(id)
{
	#pragma unused id
}

// mod.rs Bot::climb_type (GetClimbType, TS's cases).
stock movement_climb_type(id)
{
	if (pev(id, pev_movetype) == RC_MOVETYPE_FLY)
	{
		return CLIMB_CLIMBING;
	}
	if (g_nv_i[id][NV_CURRENT_WPT] != -1 && (g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_LADDER))
	{
		return CLIMB_NOT_TOUCHING;
	}
	return CLIMB_NONE;
}

// TODO(movement): mod.rs Bot::ladder_dir (1 up, -1 down, 0 stay).
stock movement_ladder_dir(id, bool:check_waypoint)
{
	#pragma unused id, check_waypoint
	return 0;
}
