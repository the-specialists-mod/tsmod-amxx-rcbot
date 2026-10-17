// rcbot/movement.inc: how the bot moves: speeds, avoidance, stuck handling, dives, the usercmd.
// Owner: movement. Ports bot/movement.rs (Unstick, avoid_angle, speed_limit,
// work_move_direction, react_to_stuck, give_up_stuck, stunt_ready, dive, dive_landing,
// check_stuck, run_player_move) and bot/mod.rs's movement helpers (jump, duck, use_button,
// alt_button, stop_moving, set_move_vector, duck_and_jump, jump_and_duck, ladder_dir,
// climb_type) and the movement part of think.rs teleported / spawn_init.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. The stealthy squad's half speed (speed_limit) waits for a squad.inc stock
// (squad_is_stealth, requested from social).

#if defined _rcbot_movement_included
	#endinput
#endif
#define _rcbot_movement_included

// movement.rs the unstick / dive timings (STUCK_SUICIDE is consts.inc's).
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

// mod.rs Bot::distance_from (DistanceFrom): measured from the gun position.
stock Float:movement_distance_from(id, const Float:v[3], bool:two_d)
{
	new Float:eyes[3];
	world_eyes(id, eyes);
	return two_d ? vec_distance_2d(v, eyes) : vec_distance(v, eyes);
}

// mod.rs Entity::size for the bot: absmax - absmin of the frame's snapshot.
stock movement_size(id, Float:out[3])
{
	for (new k = 0; k < 3; k++)
	{
		out[k] = Float:g_ws[id][TSPS_ABSMAX + k] - Float:g_ws[id][TSPS_ABSMIN + k];
	}
}

// movement.rs Bot::avoid_angle (UTIL_GetAvoidAngle).
static stock Float:mv_avoid_angle(id, const Float:origin[3])
{
	new Float:fwd[3], Float:right[3], Float:v[3], Float:view[3], Float:enemy[3], Float:me[3];
	look_view_vectors(id, fwd, right);
	vec_scale(fwd, 4096.0, v);
	vec_to_angles(v, view);
	fix_angles(view);
	world_origin(id, me);
	vec_sub(origin, me, v);
	vec_to_angles(v, enemy);
	fix_angles(enemy);
	new Float:a = view[1] - enemy[1];
	if (a > 180.0)
	{
		a -= 360.0;
	}
	if (a < -180.0)
	{
		a += 360.0;
	}
	return -a;
}

// movement.rs Bot::speed_limit: m_fIdealMoveSpeed, at most half the maximum at a stay-near
// waypoint or in a stealthy squad.
static stock Float:mv_speed_limit(id)
{
	new Float:ideal = g_mv_f[id][MV_IDEAL_MOVE_SPEED];
	if (g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_STAY_NEAR || squad_is_stealth(id))
	{
		return fmin(ideal, g_mv_f[id][MV_MAX_SPEED] / 2.0);
	}
	return ideal;
}

// A player's or entity's velocity (Entity::velocity).
static stock mv_entity_velocity(ent, Float:out[3])
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		world_velocity(ent, out);
	}
	else
	{
		pev(ent, pev_velocity, out);
	}
}

// movement.rs Bot::work_move_direction (CBot::WorkMoveDirection): forward and side speeds toward
// move_to relative to the view, around an entity to avoid. A strafe under way goes on without a
// place to go (design §4).
stock movement_work_move_direction(id)
{
	new Float:now = g_now;
	new climb = movement_climb_type(id);
	new Float:origin[3];
	world_origin(id, origin);
	if (!g_mv_i[id][MV_NOT_FOLLOWING_WAYPOINT] && climb == CLIMB_NONE && g_nv_i[id][NV_CURRENT_WPT] == -1
		&& movement_distance_from(id, g_mv_v[id][MV_MOVE_TO], false) < BOT_WAYPOINT_TOUCH_DIST)
	{
		movement_stop_moving(id);
	}
	new bool:null_move = !g_mv_i[id][MV_MOVE_TO_VALID] && climb != CLIMB_CLIMBING;
	if (vec_distance(g_mv_v[id][MV_MOVE_TO], origin) < 4.0)
	{
		null_move = true;
	}
	else if (g_mv_i[id][MV_NOT_FOLLOWING_WAYPOINT])
	{
		null_move = false;
	}
	if (g_mv_i[id][MV_AVOID_ENT] && g_mv_i[id][MV_MOVE_TO_VALID])
	{
		null_move = false;
	}
	new bool:frozen = (pev(id, pev_flags) & FL_FROZEN) != 0;
	new waterlevel = pev(id, pev_waterlevel);
	if (null_move || frozen)
	{
		g_mv_f[id][MV_MOVE_SPEED] = 0.0;
		if (g_mv_f[id][MV_STRAFE_TIME] < now || frozen)
		{
			g_mv_f[id][MV_STRAFE_SPEED] = 0.0;
		}
		if (waterlevel == 3)
		{
			movement_jump(id);
		}
		return;
	}
	new cur = g_nv_i[id][NV_CURRENT_WPT];
	if (climb != CLIMB_NONE && cur != -1)
	{
		// look_task is look's field; movement.rs sets it here.
		g_lk_i[id][LK_LOOK_TASK] = LOOK_NEXT_WAYPOINT;
		nav_origin(cur, g_mv_v[id][MV_MOVE_TO]);
	}
	new weapon = g_bw_i[id][BW_CURRENT];
	if (weapon >= 0 && botweapons_is_melee(id, weapon))
	{
		g_mv_i[id][MV_AVOID_ENT] = 0;
	}
	new Float:angle;
	new avoid = g_mv_i[id][MV_AVOID_ENT];
	new Float:avoid_origin[3];
	if (avoid && entref_alive(avoid, g_mv_i[id][MV_AVOID_SERIAL]) && world_entity_origin(avoid, avoid_origin))
	{
		new Float:av[3];
		mv_entity_velocity(avoid, av);
		if (g_mv_f[id][MV_AVOID_TIME] <= now && combat_is_enemy(id, avoid) && (av[0] != 0.0 || av[1] != 0.0))
		{
			// Leave room to walk past: my size and a bit.
			new Float:a[3], Float:b[3], Float:size[3], Float:v[3];
			vec_sub(g_mv_v[id][MV_MOVE_TO], origin, v);
			vec_normalize(v, a);
			vec_sub(origin, avoid_origin, v);
			vec_normalize(v, b);
			vec_add(a, b, v);
			movement_size(id, size);
			vec_scale(v, vec_length_2d(size) * 1.1, v);
			vec_add(origin, v, g_mv_v[id][MV_AVOID_MOVE_TO]);
			g_mv_v[id][MV_AVOID_MOVE_TO][2] = origin[2];
			g_mv_f[id][MV_AVOID_TIME] = now + 0.2;
		}
		angle = g_mv_f[id][MV_AVOID_TIME] > now ? look_yaw_angle_to(id, g_mv_v[id][MV_AVOID_MOVE_TO]) : mv_avoid_angle(id, avoid_origin);
	}
	else
	{
		angle = look_yaw_angle_to(id, g_mv_v[id][MV_MOVE_TO]);
	}
	new Float:max_speed = g_mv_f[id][MV_MAX_SPEED];
	if (waterlevel > 0)
	{
		if (g_mv_f[id][MV_UP_TIME] < now)
		{
			if (g_mv_v[id][MV_MOVE_TO][2] < origin[2])
			{
				g_mv_f[id][MV_UP_SPEED] = -max_speed;
			}
			else if (g_mv_v[id][MV_MOVE_TO][2] > origin[2])
			{
				g_mv_f[id][MV_UP_SPEED] = max_speed;
			}
		}
		if (g_mv_f[id][MV_UP_SPEED] > 0.0 && world_point_contents(g_mv_v[id][MV_MOVE_TO]) != RC_CONTENTS_WATER)
		{
			g_co_i[id][CO_BUTTON] &= ~IN_JUMP;
			g_mv_f[id][MV_UP_SPEED] = fmin(g_mv_v[id][MV_MOVE_TO][2] - origin[2], max_speed);
		}
	}
	angle = fix_float_angle(angle);
	new Float:speed = mv_speed_limit(id);
	g_mv_f[id][MV_MOVE_SPEED] = speed * floatcos(angle, degrees);
	if (g_mv_f[id][MV_STRAFE_TIME] < now)
	{
		g_mv_f[id][MV_STRAFE_SPEED] = speed * floatsin(angle, degrees);
	}
	if (climb == CLIMB_CLIMBING)
	{
		g_co_i[id][CO_BUTTON] |= IN_FORWARD;
	}
	if (g_mv_f[id][MV_UNSTICK_BACK_UNTIL] > now && climb == CLIMB_NONE)
	{
		// Stuck again at the same place: step back from it, sideways (design §4).
		g_mv_f[id][MV_MOVE_SPEED] = -g_mv_f[id][MV_MOVE_SPEED];
		g_mv_f[id][MV_STRAFE_SPEED] = g_mv_f[id][MV_UNSTICK_BACK_SIDE] * g_mv_f[id][MV_IDEAL_MOVE_SPEED] * 0.7;
	}
	if (floatabs(g_mv_f[id][MV_MOVE_SPEED]) < 1.0)
	{
		g_mv_f[id][MV_MOVE_SPEED] = 0.0;
	}
	if (floatabs(g_mv_f[id][MV_STRAFE_SPEED]) < 1.0)
	{
		g_mv_f[id][MV_STRAFE_SPEED] = 0.0;
	}
}

// movement.rs Bot::react_to_stuck: the stuck event's reaction after a second (Unstick): the
// first time at a place back to the previous waypoint with a crouch jump, then backing up and
// strafing, a crouch jump, and from then on a wall jump facing a wall or else a dive.
static stock mv_react_to_stuck(id, Float:now, climb)
{
	new Float:origin[3];
	world_origin(id, origin);
	g_mv_i[id][MV_UNSTICK_EDGE_PREV] = g_nv_i[id][NV_PREV_WPT];
	g_mv_i[id][MV_UNSTICK_EDGE_CUR] = g_nv_i[id][NV_CURRENT_WPT];
	new Float:last = g_mv_f[id][MV_UNSTICK_LAST];
	if (last > 0.0 && now - last < UNSTICK_SERIES_TIME && vec_distance(origin, g_mv_v[id][MV_UNSTICK_AT]) < UNSTICK_SERIES_RANGE)
	{
		g_mv_i[id][MV_UNSTICK_LEVEL]++;
	}
	else
	{
		g_mv_i[id][MV_UNSTICK_LEVEL] = 0;
		vec_copy(origin, g_mv_v[id][MV_UNSTICK_AT]);
		g_mv_f[id][MV_UNSTICK_SINCE] = now;
	}
	if (g_mv_i[id][MV_UNSTICK_LEVEL] >= 4 && now - g_mv_f[id][MV_UNSTICK_SINCE] > STUCK_SUICIDE)
	{
		// Nothing got it away from here: the last resort, never in a fight.
		g_mv_f[id][MV_UNSTICK_SINCE] = now;
		mv_give_up_stuck(id, now);
	}
	if (climb == CLIMB_CLIMBING)
	{
		navigate_got_stuck(id);
		return;
	}
	switch (g_mv_i[id][MV_UNSTICK_LEVEL])
	{
		case 0:
		{
			movement_jump_and_duck(id);
			navigate_got_stuck(id);
		}
		case 1:
		{
			g_mv_f[id][MV_UNSTICK_BACK_UNTIL] = now + random_float(0.6, 1.0);
			g_mv_f[id][MV_UNSTICK_BACK_SIDE] = random_num(0, 1) == 0 ? -1.0 : 1.0;
		}
		case 2:
		{
			movement_jump_and_duck(id);
		}
		default:
		{
			new Float:yaw[3], Float:fwd[3], Float:right[3], Float:end[3], Float:end_pos[3], hit;
			yaw[1] = g_co_v[id][CO_VANGLE][1];
			make_vectors(yaw, fwd, right);
			vec_mad(origin, fwd, WALL_JUMP_RANGE, end);
			new Float:fraction = world_trace_line(origin, end, true, false, id, end_pos, hit);
			if (!movement_stunt_ready(id))
			{
				// A stunt cooling down would eat the press: a crouch jump meanwhile.
				movement_jump_and_duck(id);
			}
			else if (fraction < 1.0 && (world_flags(id) & TSPF_ON_GROUND))
			{
				// TS's wall jump: +alt1 facing a wall throws the player up and back off it.
				movement_alt(id);
			}
			else
			{
				movement_dive(id);
			}
		}
	}
}

// movement.rs Bot::give_up_stuck: the last resort against being stuck, suicide (m_bKill), never
// in a fight.
static stock mv_give_up_stuck(id, Float:now)
{
	if (g_cb_i[id][CB_ENEMY] == 0 && g_cb_f[id][CB_LAST_SEE_ENEMY_TIME] + 5.0 < now)
	{
		// m_bKill is core's field; movement.rs sets it here.
		g_co_i[id][CO_KILL] = 1;
		g_mv_i[id][MV_STUCK_SUICIDES]++;
	}
}

// movement.rs Bot::check_stuck (CBot::CheckStuck): no progress on a path for STUCK_SUICIDE
// seconds out of a fight kills the bot (upstream: 8 s); slow for a second it reacts
// (mv_react_to_stuck), for two it fails the path it was on and strafes away from a wall.
stock movement_check_stuck(id)
{
	new Float:now = g_now;
	new climb = movement_climb_type(id);
	new Float:stuck_speed = g_cfg_f[CFG_BOT_STUCK_SPEED];
	new waterlevel = pev(id, pev_waterlevel);
	new Float:v3 = g_th_f[id][TH_VELOCITY_3D];
	new Float:v2 = g_th_f[id][TH_VELOCITY_2D];
	new bool:check = false;
	if (floatabs(stuck_speed + 1.0) > 0.00001)
	{
		new task = tasks_current_kind(id);
		check = g_mv_f[id][MV_IDEAL_MOVE_SPEED] > 0.0 && g_mv_i[id][MV_MOVE_TO_VALID] && waterlevel < 3
			&& (task == TASK_NONE || task == TASK_FIND_PATH || task == TASK_MOVE_TO_VECTOR);
	}
	if (check)
	{
		if (g_mv_f[id][MV_LAST_NO_MOVE] == 0.0)
		{
			if (v3 <= stuck_speed)
			{
				g_mv_f[id][MV_LAST_NO_MOVE] = now;
			}
		}
		else if (v3 > stuck_speed)
		{
			g_mv_f[id][MV_LAST_NO_MOVE] = 0.0;
		}
		else if (g_mv_f[id][MV_LAST_NO_MOVE] + STUCK_SUICIDE < now)
		{
			// Upstream: after 8 s, whatever happened.
			g_mv_f[id][MV_LAST_NO_MOVE] = 0.0;
			mv_give_up_stuck(id, now);
		}
	}
	else
	{
		g_mv_f[id][MV_LAST_NO_MOVE] = 0.0;
	}

	new bool:ducking = (world_flags(id) & TSPF_DUCKING) != 0;
	new Float:vel[3];
	world_velocity(id, vel);
	// The speed asked for, sideways included: a bot looking aside walks with the strafe
	// (upstream compared the forward speed alone).
	new Float:ms = g_mv_f[id][MV_MOVE_SPEED], Float:ss = g_mv_f[id][MV_STRAFE_SPEED];
	new Float:wanted = floatsqroot(ms * ms + ss * ss);
	new Float:stuck_time = g_mv_f[id][MV_STUCK_TIME];
	if (stuck_time == 0.0)
	{
		g_mv_i[id][MV_UNSTICK_REACTED] = 0;
		if (waterlevel != 3)
		{
			if (climb != CLIMB_NONE)
			{
				new dir = movement_ladder_dir(id, false);
				g_co_i[id][CO_BUTTON] &= ~IN_JUMP;
				if ((dir == 1 && vel[2] < BOT_STUCK_CLIMB_SPEED) || (dir == -1 && vel[2] > -BOT_STUCK_CLIMB_SPEED))
				{
					g_mv_f[id][MV_STUCK_TIME] = now;
				}
			}
			else if (ducking)
			{
				if (v2 < wanted / 4.0)
				{
					g_mv_f[id][MV_STUCK_TIME] = now;
				}
			}
			else if (v2 < wanted / 2.0)
			{
				g_mv_f[id][MV_STUCK_TIME] = now;
			}
		}
		else if (v3 < BOT_STUCK_SWIM_SPEED)
		{
			g_mv_f[id][MV_STUCK_TIME] = now;
		}
	}
	else if (stuck_time + 2.0 < now)
	{
		if (waterlevel == 3)
		{
			g_mv_f[id][MV_UP_TIME] = now + random_float(2.0, 3.0);
			new Float:gun[3], Float:up[3], Float:end_pos[3], hit;
			world_eyes(id, gun);
			vec_copy(gun, up);
			up[2] += 64.0;
			new Float:max_speed = g_mv_f[id][MV_MAX_SPEED];
			g_mv_f[id][MV_UP_SPEED] = world_trace_line(gun, up, true, false, id, end_pos, hit) < 1.0 ? -max_speed : max_speed;
		}
		else
		{
			if (climb != CLIMB_NONE)
			{
				if (stuck_time + 4.0 < now)
				{
					// ladder_angles is look's field; movement.rs clears it here.
					g_lk_i[id][LK_LADDER_ANGLES_SET] = 0;
				}
				else
				{
					return;
				}
			}
			else
			{
				movement_jump_and_duck(id);
			}
			// The path the bot was on when it got stuck fails (upstream failed whatever its index
			// decrements had left), and the bot finds another way from the nearest waypoint but
			// that one (design §4).
			new prev, cur;
			if (g_mv_i[id][MV_UNSTICK_REACTED])
			{
				prev = g_mv_i[id][MV_UNSTICK_EDGE_PREV];
				cur = g_mv_i[id][MV_UNSTICK_EDGE_CUR];
			}
			else
			{
				prev = g_nv_i[id][NV_PREV_WPT];
				cur = g_nv_i[id][NV_CURRENT_WPT];
			}
			g_nv_i[id][NV_LAST_FAILED_WPT] = cur;
			if (cur != -1 && prev != -1)
			{
				navigate_fail_path(id, prev, cur);
			}
			if (g_tk_count[id] > 0)
			{
				// The current task's found_path is the task queue's field; movement.rs clears it.
				g_tk_i[id][0][TK_FOUND_PATH] = 0;
			}
			g_mv_f[id][MV_UNSTICK_LAST] = now;
			if (g_mv_f[id][MV_STRAFE_TIME] < now && g_mv_f[id][MV_UNSTICK_BACK_UNTIL] <= now)
			{
				if (navigate_wall_on_side(id, -1.0))
				{
					g_mv_f[id][MV_STRAFE_TIME] = now + random_float(1.5, 2.5);
					g_mv_f[id][MV_STRAFE_SPEED] = g_mv_f[id][MV_MAX_SPEED];
				}
				else if (navigate_wall_on_side(id, 1.0))
				{
					g_mv_f[id][MV_STRAFE_TIME] = now + random_float(1.5, 2.5);
					g_mv_f[id][MV_STRAFE_SPEED] = -g_mv_f[id][MV_MAX_SPEED];
				}
			}
		}
		g_mv_f[id][MV_STUCK_TIME] = 0.0;
	}
	else if (stuck_time + 1.0 < now)
	{
		// Once per stuck event (upstream: every frame, each time one waypoint index back).
		if (!g_mv_i[id][MV_UNSTICK_REACTED])
		{
			g_mv_i[id][MV_UNSTICK_REACTED] = 1;
			mv_react_to_stuck(id, now, climb);
		}
	}
	else if (waterlevel == 3)
	{
		if (v3 >= BOT_STUCK_SWIM_SPEED)
		{
			g_mv_f[id][MV_STUCK_TIME] = 0.0;
		}
	}
	else if (climb != CLIMB_NONE)
	{
		new dir = movement_ladder_dir(id, false);
		if ((dir == 1 && vel[2] >= BOT_STUCK_CLIMB_SPEED) || (dir == -1 && vel[2] <= -BOT_STUCK_CLIMB_SPEED))
		{
			g_mv_f[id][MV_STUCK_TIME] = 0.0;
		}
	}
	else if (ducking)
	{
		if (v2 >= wanted / 4.0)
		{
			g_mv_f[id][MV_STUCK_TIME] = 0.0;
		}
	}
	else if (v2 >= wanted / 2.0)
	{
		g_mv_f[id][MV_STUCK_TIME] = 0.0;
	}
}

// movement.rs Bot::stunt_ready: TS takes a stunt now: none running and its cooldown over (the
// game's next_stunt_at, TSCOMBAT_NEXT_STUNT). A press before then is lost (design §4).
stock bool:movement_stunt_ready(id)
{
	return world_field(id, TSPS_STANCE) != TS_STATE_STUNT && !(world_flags(id) & TSPF_STUNTING)
		&& world_combat(id, TSCOMBAT_NEXT_STUNT) <= g_now;
}

// movement.rs Bot::dive: +alt1 while moving (TS's stunt key), with duck held through the
// landing so the bot rolls out instead of lying prone (movement_dive_landing). Nothing while a
// stunt is cooling down; false then.
stock bool:movement_dive(id)
{
	if (!movement_stunt_ready(id))
	{
		return false;
	}
	new Float:now = g_now;
	movement_alt(id);
	g_mv_f[id][MV_DIVE_TIME] = now;
	g_mv_f[id][MV_START_DUCK_TIME] = now + DIVE_DUCK_FROM;
	g_mv_f[id][MV_END_DUCK_TIME] = now + DIVE_DUCK_UNTIL;
	return true;
}

// movement.rs Bot::dive_landing: a bot that landed prone anyway jumps to get up as soon as the
// game lets it (TSCOMBAT_PRONE_LOCK over; design §4).
stock movement_dive_landing(id)
{
	new Float:now = g_now;
	new Float:dive = g_mv_f[id][MV_DIVE_TIME];
	if (dive <= 0.0)
	{
		return;
	}
	new Float:since = now - dive;
	new bool:prone = (world_flags(id) & TSPF_PRONE) != 0 && world_combat(id, TSCOMBAT_PRONE_LOCK) <= now;
	if (since > 3.0)
	{
		g_mv_f[id][MV_DIVE_TIME] = 0.0;
	}
	else if (prone && (world_flags(id) & TSPF_ON_GROUND))
	{
		g_mv_f[id][MV_START_JUMP_TIME] = now;
		g_mv_f[id][MV_END_JUMP_TIME] = now + 0.1;
		g_mv_f[id][MV_DIVE_TIME] = 0.0;
	}
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

// mod.rs Bot::ladder_dir (1 up, -1 down, 0 stay): toward the next waypoint with
// check_waypoint, else toward move_to.
stock movement_ladder_dir(id, bool:check_waypoint)
{
	new Float:z = Float:g_ws[id][TSPS_ORIGIN + 2];
	new Float:target = g_mv_v[id][MV_MOVE_TO][2];
	if (check_waypoint)
	{
		new next = navigate_next_waypoint(id);
		new Float:o[3];
		if (next != -1 && nav_origin(next, o))
		{
			target = o[2];
		}
	}
	if (target > z - 8.0)
	{
		return 1;
	}
	if (target < z)
	{
		return -1;
	}
	return 0;
}
