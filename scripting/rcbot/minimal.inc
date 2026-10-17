// rcbot/minimal.inc: PLACEHOLDER behaviour that proves the frame loop until the subsystems are
// ported. Not RCBot: wander toward far visible spots, jump over low obstacles, shoot the nearest
// visible enemy with a simple aim, buy a fixed loadout.
// Owner: core (think removes it: once think.inc's living branch, combat, look and movement are
// ported, delete this file, its #include and the calls in think.inc).
// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot linking exceptions.
//
// It writes the real state fields (move_to, move speed, v_angle, jump times, enemy, button) so
// movement_run_player_move builds the usercmd as the port will.

#if defined _rcbot_minimal_included
	#endinput
#endif
#define _rcbot_minimal_included

#define MIN_VIEW			2048.0
#define MIN_WANDER_RANGE	1024.0
#define MIN_REACHED			64.0
#define MIN_FIGHT_RANGE		400.0

// The loadout bought for the next life: an AK-47 and a Glock 18.
stock minimal_loadout(id, ids[RC_MAX_LOADOUT])
{
	#pragma unused id
	ids[0] = TSW_AK47;
	ids[1] = TSW_GLOCK18;
	return 2;
}

stock minimal_think_alive(id)
{
	new Float:now = g_now;
	g_co_i[id][CO_BUTTON] = 0;
	// The think-time gate (think.rs:345): vision and decisions at most every think_time.
	if (g_th_f[id][TH_LAST_THINK_TIME] < now)
	{
		g_th_f[id][TH_LAST_THINK_TIME] = now + g_th_f[id][TH_THINK_TIME];
		minimal_find_enemy(id);
		minimal_weapon(id);
	}
	new enemy = g_cb_i[id][CB_ENEMY];
	if (enemy && (!bot_alive(enemy) || world_userid(enemy) != g_cb_i[id][CB_ENEMY_SERIAL]))
	{
		g_cb_i[id][CB_ENEMY] = 0;
		enemy = 0;
	}
	if (enemy)
	{
		minimal_fight(id, enemy);
	}
	else
	{
		minimal_wander(id);
	}
	minimal_obstacles(id);
}

// The nearest living enemy in sight within MIN_VIEW (teams in teamplay, everyone otherwise).
static minimal_find_enemy(id)
{
	new Float:eyes[3], Float:other[3], Float:end[3], hit;
	world_eyes(id, eyes);
	new team = world_team(id), bool:teams = world_teamplay();
	new best = 0, Float:best_dist = MIN_VIEW;
	for (new p = 1; p <= g_maxclients; p++)
	{
		if (p == id || !bot_alive(p))
		{
			continue;
		}
		if (teams && team != 0 && world_team(p) == team)
		{
			continue;
		}
		world_eyes(p, other);
		new Float:d = vec_distance(eyes, other);
		if (d >= best_dist)
		{
			continue;
		}
		new Float:f = world_trace_line(eyes, other, false, true, id, end, hit);
		if (f >= 1.0 || hit == p)
		{
			best = p;
			best_dist = d;
		}
	}
	g_cb_i[id][CB_ENEMY] = best;
	g_cb_i[id][CB_ENEMY_SERIAL] = best ? world_userid(best) : 0;
	if (best)
	{
		g_cb_f[id][CB_LAST_SEE_ENEMY_TIME] = g_now;
	}
}

// Take a gun out when the game left the bot with kung fu, reload an empty clip.
static minimal_weapon(id)
{
	new Float:now = g_now;
	new wid = world_weapon(id);
	if (wid == TSBOT_KUNGFU && g_bw_f[id][BW_SWITCH_UNTIL] < now)
	{
		new n = world_inventory(id);
		for (new k = 0; k < n; k++)
		{
			new w = world_inventory_field(id, k, TSINV_WEAPON);
			if (w != TSW_M61GRENADE && (world_inventory_field(id, k, TSINV_CLIP) > 0 || world_inventory_field(id, k, TSINV_AMMO) > 0))
			{
				bot_action_select_weapon(id, w);
				g_bw_f[id][BW_SWITCH_UNTIL] = now + 2.0;
				break;
			}
		}
	}
	if (wid != TSBOT_KUNGFU && world_clip(id) == 0 && world_ammo(id) > 0 && g_bw_f[id][BW_NEXT_RELOAD_TIME] < now)
	{
		g_bw_f[id][BW_NEXT_RELOAD_TIME] = now + RELOAD_RETRY;
		g_co_i[id][CO_BUTTON] |= IN_RELOAD;
	}
}

// Face the enemy's upper chest, fire with fresh presses when on target, close in or strafe.
static minimal_fight(id, enemy)
{
	new Float:eyes[3], Float:target[3], Float:d[3], Float:ang[3];
	world_eyes(id, eyes);
	world_eyes(enemy, target);
	target[2] -= 6.0;
	vec_sub(target, eyes, d);
	vec_to_angles(d, ang);
	fix_angles(ang);
	minimal_turn(id, -ang[0], ang[1]);
	new Float:err = floatabs(angle_diff(ang[1], g_co_v[id][CO_VANGLE][1])) + floatabs(angle_diff(-ang[0], g_co_v[id][CO_VANGLE][0]));
	if (err < 8.0 && !(g_fi_i[id][FI_SENT] & IN_ATTACK))
	{
		g_co_i[id][CO_BUTTON] |= IN_ATTACK;
	}
	new Float:dist = vec_length(d);
	new Float:now = g_now;
	if (dist > MIN_FIGHT_RANGE)
	{
		vec_copy(target, g_mv_v[id][MV_MOVE_TO]);
		g_mv_i[id][MV_MOVE_TO_VALID] = 1;
		minimal_move_toward(id, target, g_mv_f[id][MV_MAX_SPEED]);
	}
	else
	{
		if (g_fi_f[id][FI_STRAFE_UNTIL] < now)
		{
			g_fi_f[id][FI_STRAFE_UNTIL] = now + random_float(0.6, 1.4);
			g_fi_f[id][FI_STRAFE_DIR] = random_num(0, 1) ? 1.0 : -1.0;
		}
		g_mv_f[id][MV_MOVE_SPEED] = 0.0;
		g_mv_f[id][MV_STRAFE_SPEED] = g_fi_f[id][FI_STRAFE_DIR] * g_mv_f[id][MV_MAX_SPEED];
	}
}

// Walk to move_to; pick another far visible spot when there, when stuck or now and then.
static minimal_wander(id)
{
	new Float:now = g_now;
	new Float:origin[3];
	world_origin(id, origin);
	if (!g_mv_i[id][MV_MOVE_TO_VALID] || vec_distance_2d(origin, g_mv_v[id][MV_MOVE_TO]) < MIN_REACHED || g_mv_f[id][MV_STRAFE_TIME] < now)
	{
		minimal_pick_spot(id);
	}
	new Float:d[3], Float:ang[3];
	vec_sub(g_mv_v[id][MV_MOVE_TO], origin, d);
	vec_to_angles(d, ang);
	fix_angles(ang);
	minimal_turn(id, 0.0, ang[1]);
	minimal_move_toward(id, g_mv_v[id][MV_MOVE_TO], g_mv_f[id][MV_MAX_SPEED]);
}

// Of 8 random directions, the one with the longest clear line at eye height.
static minimal_pick_spot(id)
{
	new Float:eyes[3], Float:end[3], Float:pos[3], Float:dir[3], hit;
	world_eyes(id, eyes);
	new Float:best = -1.0;
	for (new k = 0; k < 8; k++)
	{
		new Float:yaw = random_float(-180.0, 180.0);
		dir[0] = floatcos(yaw, degrees);
		dir[1] = floatsin(yaw, degrees);
		dir[2] = 0.0;
		vec_mad(eyes, dir, MIN_WANDER_RANGE, end);
		new Float:f = world_trace_line(eyes, end, true, false, id, pos, hit);
		if (f > best)
		{
			best = f;
			vec_mad(eyes, dir, fmax(f * MIN_WANDER_RANGE - 32.0, 0.0), g_mv_v[id][MV_MOVE_TO]);
		}
	}
	g_mv_i[id][MV_MOVE_TO_VALID] = 1;
	// MV_STRAFE_TIME doubles as the placeholder's "pick a new spot by" time.
	g_mv_f[id][MV_STRAFE_TIME] = g_now + random_float(4.0, 8.0);
}

// Forward and side speeds that move toward target whatever the view's yaw.
static minimal_move_toward(id, const Float:target[3], Float:speed)
{
	new Float:origin[3], Float:d[3], Float:ang[3];
	world_origin(id, origin);
	vec_sub(target, origin, d);
	vec_to_angles(d, ang);
	new Float:delta = angle_diff(ang[1], g_co_v[id][CO_VANGLE][1]);
	g_mv_f[id][MV_MOVE_SPEED] = floatcos(delta, degrees) * speed;
	g_mv_f[id][MV_STRAFE_SPEED] = -floatsin(delta, degrees) * speed;
}

// Turn the view toward (pitch, yaw) at the bot's turn speed (look.rs set_view_angles' step).
static minimal_turn(id, Float:pitch, Float:yaw)
{
	new Float:turn = g_lk_f[id][LK_TURN_SPEED] > 0.0 ? g_lk_f[id][LK_TURN_SPEED] : 15.0;
	g_lk_f[id][LK_IDEAL_YAW] = yaw;
	g_lk_f[id][LK_IDEAL_PITCH] = pitch;
	new Float:speed = floatabs(angle_diff(yaw, g_co_v[id][CO_VANGLE][1])) / turn * 4.0;
	change_angles(speed, yaw, g_co_v[id][CO_VANGLE][1]);
	speed = floatabs(angle_diff(pitch, g_co_v[id][CO_VANGLE][0])) / turn * 4.0;
	change_angles(speed, pitch, g_co_v[id][CO_VANGLE][0]);
	g_co_v[id][CO_VANGLE][2] = 0.0;
}

// Jump when the way ahead is blocked at the knees but clear at the waist; a new spot when both
// are blocked or the bot hasn't moved for a second.
static minimal_obstacles(id)
{
	new Float:now = g_now;
	if (g_mv_f[id][MV_MOVE_SPEED] == 0.0 && g_mv_f[id][MV_STRAFE_SPEED] == 0.0)
	{
		g_mv_f[id][MV_LAST_NO_MOVE] = now;
		return;
	}
	new Float:origin[3], Float:fwd[3], Float:right[3], Float:dir[3], Float:a[3], Float:b[3], Float:end[3], hit;
	world_origin(id, origin);
	new Float:yaw[3];
	yaw[1] = g_co_v[id][CO_VANGLE][1];
	make_vectors(yaw, fwd, right);
	// The direction of travel from the speeds.
	vec_scale(fwd, g_mv_f[id][MV_MOVE_SPEED], a);
	vec_mad(a, right, g_mv_f[id][MV_STRAFE_SPEED], dir);
	dir[2] = 0.0;
	vec_normalize(dir, dir);
	vec_copy(origin, a);
	a[2] -= 18.0;
	vec_mad(a, dir, 40.0, b);
	new bool:low = world_trace_line(a, b, true, false, id, end, hit) < 1.0;
	vec_copy(origin, a);
	a[2] += 30.0;
	vec_mad(a, dir, 40.0, b);
	new bool:high = world_trace_line(a, b, true, false, id, end, hit) < 1.0;
	if (low && !high && g_mv_f[id][MV_END_JUMP_TIME] < now)
	{
		g_mv_f[id][MV_START_JUMP_TIME] = now;
		g_mv_f[id][MV_END_JUMP_TIME] = now + 0.1;
	}
	if (g_th_f[id][TH_VELOCITY_2D] > 20.0)
	{
		g_mv_f[id][MV_LAST_NO_MOVE] = now;
	}
	else if (now - g_mv_f[id][MV_LAST_NO_MOVE] > 1.0 || (low && high))
	{
		g_mv_f[id][MV_LAST_NO_MOVE] = now;
		g_mv_f[id][MV_START_JUMP_TIME] = now;
		g_mv_f[id][MV_END_JUMP_TIME] = now + 0.1;
		if (!g_cb_i[id][CB_ENEMY])
		{
			minimal_pick_spot(id);
		}
	}
}
