// rcbot/look.inc: where the bot looks.
// Owner: combat. Ports bot/look.rs (Bot::yaw_angle_to, dot_from_origin, in_view_cone, work_view_angles, set_view_angles, aim_point, aim_vector, turn_to; the free helpers are math.inc), mod.rs Bot::hurt_by / hurt's look part, think.rs teleported / spawn_init's look part.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. The view is g_co_v[id][CO_VANGLE] (Bot::v_angle); entities are read through
// combat.inc's combat_ent_* helpers.

#if defined _rcbot_look_included
	#endinput
#endif
#define _rcbot_look_included

// TAU, for the aim error's direction.
#define LK_TAU				6.2831853

// think.rs Bot::teleported, look's part: the ladder angles belong to where it was.
stock look_teleported(id)
{
	g_lk_i[id][LK_LADDER_ANGLES_SET] = 0;
}

// think.rs Bot::spawn_init, look's part.
stock look_spawn_init(id)
{
	g_lk_f[id][LK_LAST_LOOK_TIME] = 0.0;
	g_lk_i[id][LK_LOOK_TASK] = LOOK_NEXT_WAYPOINT;
	vec_zero(g_lk_v[id][LK_HURT_ORIGIN]);
	g_lk_f[id][LK_HURT_TIME] = 0.0;
}

// e^x (float.inc has no exp).
stock Float:look_exp(Float:x)
{
	return floatpower(2.7182818, x);
}

// look.rs Bot::work_view_angles (CBot::WorkViewAngles): the look task's target.
stock look_work_view_angles(id)
{
	new Float:now = g_now;
	switch (g_lk_i[id][LK_LOOK_TASK])
	{
		case LOOK_FACE_NEAREST_REMEMBER_POS:
		{
			if (g_cb_i[id][CB_NEAREST_REMEMBER_VISIBLE] && !g_cb_i[id][CB_ENEMY] && g_cb_i[id][CB_LOOKING_FOR_ENEMY])
			{
				look_set_view_angles(id, g_cb_v[id][CB_NEAREST_REMEMBER_POINT]);
			}
			else
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_NEXT_WAYPOINT;
			}
		}
		case LOOK_FACE_HURT_ORIGIN:
		{
			if (g_cb_i[id][CB_ENEMY] || g_lk_f[id][LK_HURT_TIME] + BOT_FACE_HURT_TIME < now)
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_NEXT_WAYPOINT;
			}
			else
			{
				look_set_view_angles(id, g_lk_v[id][LK_HURT_ORIGIN]);
			}
		}
		case LOOK_NEXT_WAYPOINT:
		{
			look_set_view_angles(id, g_mv_v[id][MV_MOVE_TO]);
		}
		case LOOK_LOOK_AROUND:
		{
			if (g_lk_f[id][LK_LAST_LOOK_TIME] < now)
			{
				g_lk_f[id][LK_LAST_LOOK_TIME] = now + random_float(2.0, 5.0);
				new Float:yaw = fix_float_angle(g_lk_f[id][LK_IDEAL_YAW] + (random_float(0.0, 180.0) - 90.0));
				new Float:ang[3], Float:fwd[3], Float:right[3], Float:gun[3];
				ang[1] = yaw;
				make_vectors(ang, fwd, right);
				world_eyes(id, gun);
				vec_mad(gun, fwd, 64.0, g_lk_v[id][LK_CURRENT_LOOK_DIR]);
			}
			look_set_view_angles(id, g_lk_v[id][LK_CURRENT_LOOK_DIR]);
		}
		case LOOK_SEARCH_FOR_LAST_ENEMY:
		{
			if (g_lk_f[id][LK_SEARCH_ENEMY_TIME] < now)
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_NEXT_WAYPOINT;
			}
			else if (!g_cb_i[id][CB_LAST_ENEMY])
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_SEARCH_FOR_ENEMY;
			}
			else
			{
				look_set_view_angles(id, g_cb_v[id][CB_LAST_SEE_ENEMY_POS]);
			}
		}
		case LOOK_SEARCH_FOR_ENEMY:
		{
			if (g_lk_f[id][LK_SEARCH_ENEMY_TIME] < now)
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_NEXT_WAYPOINT;
				return;
			}
			if (g_lk_f[id][LK_LAST_LOOK_TIME] < now)
			{
				new Float:base[3];
				new last = g_cb_i[id][CB_LAST_ENEMY];
				if (entref_alive(last, g_cb_i[id][CB_LAST_ENEMY_SERIAL]) && combat_ent_is_alive(last))
				{
					vec_copy(g_cb_v[id][CB_LAST_SEE_ENEMY_POS], base);
				}
				else if (g_mv_i[id][MV_MOVE_TO_VALID])
				{
					vec_copy(g_mv_v[id][MV_MOVE_TO], base);
				}
				else
				{
					g_lk_i[id][LK_LOOK_TASK] = LOOK_LOOK_AROUND;
					return;
				}
				new Float:r[3], Float:n[3];
				r[0] = random_float(0.0, 128.0) - 64.0;
				r[1] = random_float(0.0, 128.0) - 64.0;
				r[2] = random_float(0.0, 128.0) - 64.0;
				vec_normalize(r, n);
				vec_mad(base, n, 64.0, g_lk_v[id][LK_CURRENT_LOOK_DIR]);
				g_lk_f[id][LK_LAST_LOOK_TIME] = now + random_float(2.0, 4.0);
			}
			look_set_view_angles(id, g_lk_v[id][LK_CURRENT_LOOK_DIR]);
		}
		case LOOK_FACE_ENEMY:
		{
			new enemy = g_cb_i[id][CB_ENEMY];
			if (!entref_alive(enemy, g_cb_i[id][CB_ENEMY_SERIAL]))
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_SEARCH_FOR_LAST_ENEMY;
				g_lk_f[id][LK_SEARCH_ENEMY_TIME] = now + random_float(5.0, 8.0);
				return;
			}
			new Float:v[3];
			if (kungfu_face(id, v))
			{
				// A kung fu wall jump faces the wall, not the enemy (design §4).
				new Float:saved = g_lk_f[id][LK_TURN_SPEED];
				g_lk_f[id][LK_TURN_SPEED] = fmax(15.0 * (1.0 - g_pr_f[id][PR_AIM_SPEED]), 1.0);
				look_set_view_angles(id, v);
				g_lk_f[id][LK_TURN_SPEED] = saved;
			}
			else if (fire_enemy_out_of_sight(id))
			{
				// Out of sight: pre-aim where it will show, not where it is (design §4).
				fire_lost_aim_point(id, v);
				vec_copy(v, g_fi_v[id][FI_AIM_TRUE]);
				g_fi_i[id][FI_AIM_TRUE_SET] = 1;
				look_turn_to(id, v);
			}
			else
			{
				look_aim_vector(id, enemy, v);
				look_turn_to(id, v);
			}
		}
		case LOOK_FACE_TASK_VECTOR:
		{
			if (g_tk_count[id] > 0)
			{
				look_set_view_angles(id, g_tk_vec[id][0]);
			}
			else
			{
				g_lk_i[id][LK_LOOK_TASK] = LOOK_NEXT_WAYPOINT;
			}
		}
		case LOOK_PRE_AIM:
		{
			preaim_look(id);
		}
		case LOOK_FACE_TASK_EDICT:
		{
			g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_VECTOR;
			if (g_tk_count[id] > 0)
			{
				new ent = g_tk_i[id][0][TK_EDICT];
				if (entref_alive(ent, g_tk_i[id][0][TK_EDICT_SERIAL]))
				{
					new Float:mid[3];
					combat_ent_box_center(ent, mid);
					look_set_view_angles(id, mid);
					g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_TASK_EDICT;
				}
			}
		}
	}
}

// look.rs Bot::set_view_angles (CBot::SetViewAngles): turns toward origin, m_fTurnSpeed at a
// time. Pitch is used only for the looking tasks, in water, on ladders and for move targets
// over a jump away.
stock look_set_view_angles(id, const Float:origin[3])
{
	new bool:climbing = movement_climb_type(id) != CLIMB_NONE;
	new bool:use_pitch = false;
	new Float:angles[3];
	new look = g_lk_i[id][LK_LOOK_TASK];
	if (climbing && g_lk_i[id][LK_LADDER_ANGLES_SET])
	{
		vec_copy(g_lk_v[id][LK_LADDER_ANGLES], angles);
		use_pitch = true;
	}
	else
	{
		new Float:gun[3], Float:d[3];
		world_eyes(id, gun);
		vec_sub(origin, gun, d);
		vec_to_angles(d, angles);
		fix_angles(angles);
		if (climbing && look == LOOK_NEXT_WAYPOINT)
		{
			// Upstream's ladder direction here reads the waypoint stack; the move vector is
			// what GetLadderDir() without arguments uses.
			new Float:me[3];
			world_origin(id, me);
			new Float:mz = g_mv_v[id][MV_MOVE_TO][2];
			if (mz > me[2] - 8.0)
			{
				angles[0] = 40.0;
			}
			else if (mz < me[2])
			{
				angles[0] = -40.0;
			}
			vec_copy(angles, g_lk_v[id][LK_LADDER_ANGLES]);
			g_lk_i[id][LK_LADDER_ANGLES_SET] = 1;
			use_pitch = true;
			g_lk_f[id][LK_TURN_SPEED] = RC_MAX_JUMP_HEIGHT;
		}
		else if (g_lk_i[id][LK_LADDER_ANGLES_SET])
		{
			g_lk_i[id][LK_LADDER_ANGLES_SET] = 0;
		}
	}
	angles[2] = 0.0;
	g_co_v[id][CO_VANGLE][2] = 0.0;
	new Float:me[3];
	world_origin(id, me);
	new Float:z_comp = floatabs(g_mv_v[id][MV_MOVE_TO][2] - me[2]);
	if (!use_pitch)
	{
		use_pitch = look == LOOK_SEARCH_FOR_ENEMY || look == LOOK_FACE_ENEMY || look == LOOK_FACE_TASK_VECTOR || look == LOOK_FACE_TASK_EDICT
			|| look == LOOK_SEARCH_FOR_LAST_ENEMY || look == LOOK_LOOK_AROUND;
	}
	if (!use_pitch && (pev(id, pev_waterlevel) > 0 || z_comp > RC_MAX_JUMP_HEIGHT))
	{
		use_pitch = true;
	}
	g_lk_f[id][LK_IDEAL_PITCH] = use_pitch ? -angles[0] : 0.0;
	g_lk_f[id][LK_IDEAL_YAW] = angles[1];
	new Float:turn = g_lk_f[id][LK_TURN_SPEED];
	new Float:cur = g_co_v[id][CO_VANGLE][1];
	new Float:speed = floatabs(angle_diff(g_lk_f[id][LK_IDEAL_YAW], cur)) / turn;
	change_angles(speed, g_lk_f[id][LK_IDEAL_YAW], cur);
	g_co_v[id][CO_VANGLE][1] = cur;
	cur = g_co_v[id][CO_VANGLE][0];
	speed = floatabs(angle_diff(g_lk_f[id][LK_IDEAL_PITCH], cur)) / turn;
	change_angles(speed, g_lk_f[id][LK_IDEAL_PITCH], cur);
	g_co_v[id][CO_VANGLE][0] = cur;
	g_lk_f[id][LK_TURN_SPEED] = g_lk_f[id][LK_BASE_TURN_SPEED];
}

// look.rs Bot::yaw_angle_to (UTIL_YawAngleBetweenOrigin(pev, vOrigin)).
stock Float:look_yaw_angle_to(id, const Float:origin[3])
{
	new Float:me[3], Float:d[3], Float:a[3];
	world_origin(id, me);
	vec_sub(origin, me, d);
	vec_to_angles(d, a);
	return fix_float_angle(g_co_v[id][CO_VANGLE][1] - a[1]);
}

// look.rs Bot::dot_from_origin (DotProductFromOrigin).
stock Float:look_dot_from_origin(id, const Float:origin[3])
{
	new Float:fwd[3], Float:right[3], Float:gun[3], Float:d[3], Float:n[3];
	make_vectors(g_co_v[id][CO_VANGLE], fwd, right);
	world_eyes(id, gun);
	vec_sub(origin, gun, d);
	vec_normalize(d, n);
	return vec_dot(n, fwd);
}

// look.rs Bot::in_view_cone (FInViewCone): within 60 degrees of the view.
stock bool:look_in_view_cone(id, const Float:origin[3])
{
	return look_dot_from_origin(id, origin) > 0.5;
}

// look.rs Bot::aim_point (design §4; upstream: the middle of its box): a player's upper chest;
// its head (2 units under the eyes) when only the head is in sight, or for a skilled bot (70+)
// with a precise gun (Desert Eagle, Raging Bull, M16, AK-47, Barrett, Contender); the middle
// with a shotgun. Anything else: its middle.
stock look_aim_point(id, enemy, Float:out[3])
{
	if (!combat_ent_is_client(enemy))
	{
		world_entity_origin(enemy, out);
		return;
	}
	new gun = fire_gun_in_hand(id);
	if (gun != -1 && g_gd_f[gun][GD_PELLETS] > 1.0)
	{
		world_entity_origin(enemy, out);
		return;
	}
	new cond = g_th_i[id][TH_CONDITIONS];
	new bool:head_only = (cond & COND_SEE_ENEMY_HEAD) && !(cond & COND_SEE_ENEMY_BODY);
	new bool:precise = gun == TSW_ID_DEAGLE || gun == TSW_ID_AK47 || gun == TSW_ID_BARRETT || gun == TSW_ID_M16 || gun == TSW_ID_RAGING_BULL
		|| gun == TSW_ID_CONTENDER;
	new Float:o[3], Float:ofs[3];
	world_origin(enemy, o);
	combat_ent_view_ofs(enemy, ofs);
	if (head_only || (precise && g_pr_i[id][PR_SKILL] >= 70))
	{
		out[0] = o[0] + ofs[0];
		out[1] = o[1] + ofs[1];
		out[2] = o[2] + ofs[2] - 2.0;
	}
	else
	{
		out[0] = o[0];
		out[1] = o[1];
		out[2] = o[2] + ofs[2] * 0.6;
	}
}

// look.rs Bot::aim_vector (CBot::GetAimVector, rewritten: design §4): the aim point led by the
// enemy's velocity over a frame (and the bullet's flight in slow motion), off by an angular
// error in the view plane: a skill-scaled base plus parts of the enemy's angular speed and of
// the bot's own turn rate, settling over about 0.2 s; its direction redrawn every 0.25-0.5 s.
stock look_aim_vector(id, enemy, Float:out[3])
{
	new Float:now = g_now;
	new Float:gun[3], Float:point[3], Float:d[3];
	world_eyes(id, gun);
	look_aim_point(id, enemy, point);
	vec_sub(point, gun, d);
	new Float:dist = fmax(vec_length(d), 1.0);
	new Float:lag = fmax(g_frametime, 0.01);
	new Float:ts = Float:world_field(id, TSPS_TIMESCALE);
	new g = fire_gun_in_hand(id);
	if (ts > 0.0 && ts < 0.99 && g != -1)
	{
		// GetBulletVelocityMult: the bullet flies at velocity * 40 u/m times this.
		new Float:mult = ts / fmax(1.0 - ts * ts, 1.0);
		lag += dist / (g_gd_f[g][GD_VELOCITY] * 40.0 * mult);
	}
	new Float:ev[3], Float:mv[3];
	combat_ent_velocity(enemy, ev);
	vec_mad(point, ev, lag, point);
	vec_copy(point, g_fi_v[id][FI_AIM_TRUE]);
	g_fi_i[id][FI_AIM_TRUE_SET] = 1;

	new Float:dir[3], Float:rel[3], Float:perp[3];
	vec_sub(point, gun, d);
	vec_normalize(d, dir);
	world_velocity(id, mv);
	vec_sub(ev, mv, rel);
	vec_mad(rel, dir, -vec_dot(rel, dir), perp);
	new Float:target_rate = vec_length(perp) / dist * 57.29578;
	new Float:skill = fclamp(float(g_pr_i[id][PR_SKILL]) / float(MAX_BOT_SKILL), 0.0, 1.0);
	new Float:base = (0.3 + 1.2 * (1.0 - skill)) * (1.0 - 0.5 * fclamp(g_pr_f[id][PR_AIM_SKILL], 0.0, 1.0));
	new Float:settle = 1.0 + 2.0 * look_exp(-fmax(now - g_fi_f[id][FI_ON_TARGET_SINCE], 0.0) / 0.2);
	new Float:error = (base + 0.02 * target_rate + 0.01 * g_fi_f[id][FI_TURN_RATE]) * settle;
	if (g_cb_i[id][CB_ZOOM])
	{
		error *= 0.5;
	}
	if (g_cb_f[id][CB_GET_AIM_VECTOR_TIME] < now)
	{
		g_fi_f[id][FI_ERROR_DIR] = random_float(0.0, LK_TAU);
		g_fi_f[id][FI_ERROR_SIZE] = floatsqroot(random_float(0.0, 1.0));
		g_cb_f[id][CB_GET_AIM_VECTOR_TIME] = now + random_float(0.25, 0.5);
	}
	new Float:ang[3], Float:fwd[3], Float:right[3], Float:up[3];
	vec_to_angles(dir, ang);
	make_vectors(ang, fwd, right);
	up[0] = right[1] * fwd[2] - right[2] * fwd[1];
	up[1] = right[2] * fwd[0] - right[0] * fwd[2];
	up[2] = right[0] * fwd[1] - right[1] * fwd[0];
	new Float:off = floattan(error, degrees) * g_fi_f[id][FI_ERROR_SIZE] * dist;
	new Float:s = floatsin(g_fi_f[id][FI_ERROR_DIR], radian), Float:c = floatcos(g_fi_f[id][FI_ERROR_DIR], radian);
	for (new k = 0; k < 3; k++)
	{
		out[k] = point[k] + right[k] * (off * c) + up[k] * (off * s);
	}
	vec_copy(out, g_fi_v[id][FI_AIM_TARGET]);
	g_fi_i[id][FI_AIM_TARGET_SET] = 1;
}

// look.rs turn_to's ideal: the view (pitch, yaw) at v from gun.
stock look_ideal(const Float:gun[3], const Float:v[3], &Float:pitch, &Float:yaw)
{
	new Float:d[3], Float:a[3];
	vec_sub(v, gun, d);
	vec_to_angles(d, a);
	fix_angles(a);
	pitch = -a[0];
	yaw = a[1];
}

// look.rs turn_to's step: closes err with the time constant, follows ff, at most max_step.
stock Float:look_step(Float:err, Float:ff, Float:close, Float:dt, Float:max_step)
{
	return fclamp(err * close + fclamp(ff, -720.0, 720.0) * dt, -max_step, max_step);
}

// look.rs Bot::turn_to (design §4; upstream turned a fixed share of what was left every frame):
// each axis closes the error with a time constant of 0.04-0.16 s (shorter with skill and aim
// speed) and follows the aim point's own angular speed, at most 360-1080 degrees a second. A
// view more than 10 degrees off restarts the aim's settling. frame_time is turn_to's third
// argument, the step used on the first turn (or after a stall): the frame's (w.frame_time(), the
// default, any negative value) or a caller's (special.rs task_throw_grenade passes 0.04).
stock look_turn_to(id, const Float:target[3], Float:frame_time = -1.0)
{
	new Float:now = g_now;
	new Float:gun[3];
	world_eyes(id, gun);
	new Float:pitch, Float:yaw, Float:true_pitch, Float:true_yaw;
	look_ideal(gun, target, pitch, yaw);
	if (g_fi_i[id][FI_AIM_TRUE_SET])
	{
		look_ideal(gun, g_fi_v[id][FI_AIM_TRUE], true_pitch, true_yaw);
	}
	else
	{
		true_pitch = pitch;
		true_yaw = yaw;
	}
	new Float:last_turn = g_fi_f[id][FI_LAST_TURN];
	new Float:dt = last_turn > 0.0 && now > last_turn ? fmin(now - last_turn, 0.1) : fclamp(frame_time < 0.0 ? g_frametime : frame_time, 0.001, 0.1);
	new Float:ff_pitch = 0.0, Float:ff_yaw = 0.0;
	if (g_fi_i[id][FI_LAST_IDEAL_SET] && last_turn > 0.0)
	{
		ff_pitch = angle_diff(true_pitch, g_fi_f[id][FI_LAST_IDEAL_PITCH]) / dt;
		ff_yaw = angle_diff(true_yaw, g_fi_f[id][FI_LAST_IDEAL_YAW]) / dt;
	}
	new Float:skill = fclamp(float(g_pr_i[id][PR_SKILL]) / float(MAX_BOT_SKILL), 0.0, 1.0);
	new Float:tau = 0.04 + 0.12 * (1.0 - skill) * (1.0 - fclamp(g_pr_f[id][PR_AIM_SPEED], 0.0, 1.0));
	new Float:max_step = (360.0 + 720.0 * skill) * dt;
	new Float:close = 1.0 - look_exp(-dt / tau);
	new Float:err_p = angle_diff(pitch, g_co_v[id][CO_VANGLE][0]);
	new Float:err_y = angle_diff(yaw, g_co_v[id][CO_VANGLE][1]);
	new Float:sp = look_step(err_p, ff_pitch, close, dt, max_step);
	new Float:sy = look_step(err_y, ff_yaw, close, dt, max_step);
	g_co_v[id][CO_VANGLE][0] = fix_float_angle(g_co_v[id][CO_VANGLE][0] + sp);
	g_co_v[id][CO_VANGLE][1] = fix_float_angle(g_co_v[id][CO_VANGLE][1] + sy);
	g_co_v[id][CO_VANGLE][2] = 0.0;
	g_lk_f[id][LK_IDEAL_PITCH] = pitch;
	g_lk_f[id][LK_IDEAL_YAW] = yaw;
	g_fi_f[id][FI_TURN_RATE] = floatsqroot(sp * sp + sy * sy) / dt;
	if (floatsqroot(err_p * err_p + err_y * err_y) > 10.0)
	{
		g_fi_f[id][FI_ON_TARGET_SINCE] = now;
	}
	g_fi_f[id][FI_LAST_TURN] = now;
	g_fi_f[id][FI_LAST_IDEAL_PITCH] = true_pitch;
	g_fi_f[id][FI_LAST_IDEAL_YAW] = true_yaw;
	g_fi_i[id][FI_LAST_IDEAL_SET] = 1;
}
