// rcbot/botcam.inc: the bot camera (CBotCam).
// Owner: social. Ports bot/botcam.rs (CamState, Camera, BotCam clear / is_working / current_bot /
// set_current_bot / tune_in_error / think / move_camera), host bots.rs botcam_frame /
// spawn_camera / botcam_tune_in and put_in_server's TuneOff, manager.rs frame's other_has_enemy.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// An invisible info_target (models/mechgibs.mdl, rendered with amount 0) follows a bot: from
// behind, from its eyes, over its goal waypoint, or past its enemy, switching bots every 7.5-10 s
// and views every 4-6 s. Players watch it with `rcbot botcam` (attach_view). The entity is made
// once a map when `rcbot config allow_botcam 1`, if clients_precache ran (plugin_precache).

#if defined _rcbot_botcam_included
	#endinput
#endif
#define _rcbot_botcam_included

// botcam.rs CamState (eCamLookState).
enum { CAM_NONE = 0, CAM_BOT, CAM_ENEMY, CAM_WAYPOINT, CAM_FIRST_PERSON };

new g_cam_entity;				// m_pCameraEdict, 0 none
new g_cam_current_bot;			// 0 none
new g_cam_state;				// CAM_*
new bool:g_cam_tried_to_spawn;	// m_bTriedToSpawn
stock bool:g_cam_other_has_enemy;
new Float:g_cam_next_change_bot_time;
new Float:g_cam_next_change_state;
new g_cam_position_set;
new Float:g_cam_origin[3];		// Camera::origin, angles, v_angle
new Float:g_cam_angles[3];
new Float:g_cam_v_angle[3];

// BotCam::clear (Clear, each map).
stock botcam_clear()
{
	g_cam_entity = 0;
	g_cam_current_bot = 0;
	g_cam_state = CAM_NONE;
	g_cam_tried_to_spawn = false;
	g_cam_other_has_enemy = false;
	g_cam_next_change_bot_time = 0.0;
	g_cam_next_change_state = 0.0;
	g_cam_position_set = 0;
	vec_zero(g_cam_origin);
	vec_zero(g_cam_angles);
	vec_zero(g_cam_v_angle);
}

// BotCam::is_working (IsWorking): there is a camera and allow_botcam is on.
stock bool:botcam_is_working()
{
	return g_cam_entity != 0 && pev_valid(g_cam_entity) && config_is_on(CFG_ENABLE_BOTCAM);
}

// BotCam::set_current_bot (SetCurrentBot).
stock botcam_set_current_bot(index)
{
	g_cam_current_bot = index;
	g_cam_next_change_bot_time = g_now + random_float(5.0, 7.5);
	g_cam_next_change_state = g_now;
}

// manager.rs frame: BotCam::other_has_enemy (BotHasEnemy) for a current bot other than the one
// about to think.
stock botcam_set_other_has_enemy(id)
{
	new c = g_cam_current_bot;
	g_cam_other_has_enemy = c != 0 && c != id && is_rcbot(c) && g_cb_i[c][CB_ENEMY] != 0;
}

// host bots.rs spawn_camera (CBotCam::Spawn): an invisible info_target with the mechgibs model,
// once a map.
static botcam_spawn()
{
	g_cam_tried_to_spawn = true;
	if (!g_botcam_model_ok)
	{
		// Without plugin_precache's clients_precache, set_model would stop the server.
		rc_log(TSLOG_WARNING, "bot camera: models/mechgibs.mdl isn't precached (clients_precache)");
		return;
	}
	new e = create_entity("info_target");
	if (!e)
	{
		return;
	}
	entity_set_model(e, "models/mechgibs.mdl");
	set_pev(e, pev_takedamage, 0.0);
	set_pev(e, pev_solid, SOLID_NOT);
	set_pev(e, pev_movetype, MOVETYPE_FLY);
	set_pev(e, pev_classname, "entity_botcam");
	set_pev(e, pev_nextthink, g_now);
	set_pev(e, pev_renderamt, 0.0);
	g_cam_entity = e;
}

// host bots.rs botcam_frame (StartFrame's m_BotCam.Think / Spawn): the camera made once
// allow_botcam is on, then moved after its bot each frame.
stock botcam_frame()
{
	new bool:enabled = config_is_on(CFG_ENABLE_BOTCAM);
	if (!(g_cam_entity != 0 && enabled))
	{
		if (!g_cam_tried_to_spawn && enabled)
		{
			botcam_spawn();
		}
		return;
	}
	if (!pev_valid(g_cam_entity))
	{
		g_cam_entity = 0;
		return;
	}
	botcam_think();
	set_pev(g_cam_entity, pev_origin, g_cam_origin);
	set_pev(g_cam_entity, pev_angles, g_cam_angles);
	set_pev(g_cam_entity, pev_v_angle, g_cam_v_angle);
}

// Bot::distance_from(v, false): from the bot's gun position.
static Float:botcam_bot_distance(b, const Float:v[3])
{
	new Float:eyes[3];
	world_eyes(b, eyes);
	return vec_distance(v, eyes);
}

// BotCam::think (CBotCam::Think): which bot, which view, then move.
static botcam_think()
{
	new Float:now = g_now;
	new bots[MAX_PLAYERS], nb = 0;
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (g_co_i[b][CO_USED])
		{
			bots[nb++] = b;
		}
	}
	if (nb == 0)
	{
		return;
	}
	new cur = is_rcbot(g_cam_current_bot) ? g_cam_current_bot : 0;
	new bool:not_alive = cur != 0 && !bot_alive(cur);
	if (cur == 0 || g_cam_next_change_bot_time < now || not_alive)
	{
		new old = g_cam_current_bot;
		g_cam_next_change_bot_time = now + random_float(7.5, 10.0);
		g_cam_current_bot = 0;
		if (not_alive)
		{
			new Float:nearest = 0.0;
			for (new k = 0; k < nb; k++)
			{
				new b = bots[k];
				if (b == old || !bot_alive(b))
				{
					continue;
				}
				new Float:d = botcam_bot_distance(b, g_cam_origin);
				if (g_cam_current_bot == 0 || d < nearest)
				{
					g_cam_current_bot = b;
					nearest = d;
					g_cam_state = g_cb_i[b][CB_ENEMY] != 0 ? CAM_ENEMY : CAM_BOT;
				}
			}
		}
		else
		{
			new possible[MAX_PLAYERS], np = 0;
			for (new k = 0; k < nb; k++)
			{
				if (bots[k] != old)
				{
					possible[np++] = bots[k];
				}
			}
			g_cam_current_bot = np == 0 ? old : possible[random_num(0, np - 1)];
		}
	}
	new bot = g_cam_current_bot;
	if (!is_rcbot(bot))
	{
		return;
	}
	if (g_cam_state == CAM_NONE || g_cam_next_change_state < now)
	{
		new states[4], ns = 0;
		states[ns++] = CAM_BOT;
		states[ns++] = CAM_FIRST_PERSON;
		new goal = g_nv_i[bot][NV_GOAL_WPT];
		if (goal != -1)
		{
			new Float:wo[3];
			nav_origin(goal, wo);
			if (botcam_bot_distance(bot, wo) < 512.0)
			{
				states[ns++] = CAM_WAYPOINT;
			}
		}
		if (g_cb_i[bot][CB_ENEMY] != 0)
		{
			states[ns++] = CAM_ENEMY;
		}
		g_cam_state = states[random_num(0, ns - 1)];
		g_cam_position_set = -1;
		g_cam_next_change_state = now + random_float(4.0, 6.0);
	}
	if (g_cam_state == CAM_NONE)
	{
		return;
	}
	botcam_move_camera(bot);
}

// BotCam::move_camera.
static botcam_move_camera(bot)
{
	new Float:look_at[3], Float:old_origin[3], Float:fwd[3], Float:right[3], Float:vang[3], Float:end[3], hit;
	world_eyes(bot, look_at);
	vec_copy(g_cam_origin, old_origin);
	world_vangle(bot, vang);
	new enemy = g_cb_i[bot][CB_ENEMY];
	if (enemy != 0 && !entref_alive(enemy, g_cb_i[bot][CB_ENEMY_SERIAL]))
	{
		enemy = 0;
	}
	switch (g_cam_state)
	{
		case CAM_BOT:
		{
			if (enemy)
			{
				make_vectors(vang, fwd, right);
				vec_mad(look_at, fwd, -128.0, g_cam_origin);
				if (g_cam_position_set == -1)
				{
					g_cam_position_set = random_num(0, 1);
				}
				vec_mad(g_cam_origin, right, g_cam_position_set == 1 ? 64.0 : -64.0, g_cam_origin);
				world_entity_origin(enemy, look_at);
			}
			else
			{
				new Float:ang[3];
				pev(bot, pev_angles, ang);
				make_vectors(ang, fwd, right);
				vec_mad(look_at, fwd, -256.0, g_cam_origin);
			}
			if (world_trace_line(look_at, g_cam_origin, true, true, g_cam_entity, end, hit) < 1.0)
			{
				vec_copy(end, g_cam_origin);
			}
		}
		case CAM_WAYPOINT:
		{
			new goal = g_nv_i[bot][NV_GOAL_WPT];
			if (goal != -1)
			{
				nav_origin(goal, g_cam_origin);
				g_cam_origin[2] += 16.0;
			}
			else
			{
				g_cam_next_change_state = 0.0;
			}
		}
		case CAM_ENEMY:
		{
			if (enemy)
			{
				new Float:eo[3], Float:ofs[3], Float:comp[3], Float:n[3], Float:side[3], Float:size[3];
				pev(enemy, pev_origin, eo);
				pev(enemy, pev_view_ofs, ofs);
				vec_add(eo, ofs, comp);
				vec_sub(comp, look_at, comp);
				new Float:len = vec_length(comp);
				vec_normalize(comp, n);
				vec_scale(n, len + 128.0, comp);
				vec_add(look_at, comp, g_cam_origin);
				g_cam_origin[2] += 24.0;
				if (g_cam_position_set == -1)
				{
					g_cam_position_set = random_num(0, 1);
				}
				vec_normalize(comp, n);
				pev(enemy, pev_size, size);
				vec_set(side, n[1], -n[0], 0.0);
				vec_scale(side, vec_length_2d(size), side);
				vec_mad(g_cam_origin, side, g_cam_position_set == 1 ? 1.0 : -1.0, g_cam_origin);
				if (world_trace_line(eo, g_cam_origin, true, true, g_cam_entity, end, hit) < 1.0)
				{
					vec_mad(end, n, -32.0, g_cam_origin);
				}
			}
			else
			{
				g_cam_next_change_state = 0.0;
			}
		}
		case CAM_FIRST_PERSON:
		{
			world_eyes(bot, g_cam_origin);
			make_vectors(vang, fwd, right);
			vec_mad(g_cam_origin, fwd, 512.0, look_at);
			pev(bot, pev_angles, g_cam_angles);
			vec_copy(vang, g_cam_v_angle);
		}
	}
	new Float:dir[3], Float:ideal[3];
	vec_sub(look_at, g_cam_origin, dir);
	vec_to_angles(dir, ideal);
	fix_angles(ideal);
	// BotFunc_ChangeAngles(speed, ideal, &v_angle, &angles): angles follow unless the view is
	// already within 0.1 degrees.
	new Float:speed = floatabs((180.0 + ideal[0]) - (180.0 + g_cam_angles[0])) / 10.0;
	if (floatabs(g_cam_v_angle[0] - ideal[0]) > 0.1)
	{
		change_angles(speed, ideal[0], g_cam_v_angle[0]);
		g_cam_angles[0] = g_cam_v_angle[0];
	}
	speed = floatabs((180.0 + ideal[1]) - (180.0 + g_cam_angles[1])) / 20.0;
	if (floatabs(g_cam_v_angle[1] - ideal[1]) > 0.1)
	{
		change_angles(speed, ideal[1], g_cam_v_angle[1]);
		g_cam_angles[1] = g_cam_v_angle[1];
	}
	new Float:moved[3];
	vec_sub(g_cam_origin, old_origin, moved);
	vec_mad(g_cam_origin, moved, -0.5, g_cam_origin);
}

// host bots.rs botcam_tune_in (TuneIn): true if the player now watches the camera; else it is
// told why (tune_in_error).
stock bool:botcam_tune_in(player)
{
	new nb = 0;
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (g_co_i[b][CO_USED])
		{
			nb++;
		}
	}
	if (nb == 0)
	{
		commands_print(player, "No Bots are running for bot cam");
		return false;
	}
	if (g_cam_entity == 0 || !pev_valid(g_cam_entity))
	{
		commands_print(player, "Oops, looks like camera never worked...");
		return false;
	}
	attach_view(player, g_cam_entity);
	return true;
}
