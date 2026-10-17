// rcbot/navigate.inc: waypoint navigation per bot, over the rcbot_nav module.
// Owner: movement. Ports bot/navigate.rs (nearest_waypoint, roam_goal, claim_factor,
// search_path, threat_waypoints, claim, share_with_team, search_move, crowded_waypoints,
// fail_path, find_next_waypoint, update_waypoint, scan_fov, wall_on_side, got_stuck), mod.rs
// next_waypoint / go_to_waypoint, think.rs teleported / spawn_init's waypoint part and the
// failed-goal upkeep of Think, host commands.rs load_waypoints, bots.rs load_heatmap /
// save_globals' heatmap, editor.rs frame, manager.rs player_waypoint / update_heatmap.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// The module (include/rcbot_nav.inc) has the waypoints, the visibility table, the heatmap and
// each bot's RcPath (g_nv_i[id][NV_PATH]): the A* search (incremental: nav_path_find with
// resume), the path stack, failed paths and goals with their back-offs (nav_path_think), the
// tactics inputs (threats, crowding, weights) and the exploration visits (nav_path_touch,
// nav_path_roam_goal). What stays here (crates/nav/src/paths.rs cites each): when to search and
// to which goal (task_find_path, roam_goal's claims from board.inc), pushing the current
// waypoint, touching waypoints and following the stack (update_waypoint, find_next_waypoint:
// touch distances by flag, ladders, lifts, special waypoints), feeding threats (sightings) and
// crowding (teammates' paths) in, failing a path when stuck, and the heatmap's events.
//
// Status: ported. The editor's toggles (waypoint display, paths, auto-waypointing) are set by
// commands.inc through navigate_editor_set; the beam sprite needs navigate_precache from
// plugin_precache (core).

#if defined _rcbot_navigate_included
	#endinput
#endif
#define _rcbot_navigate_included

// navigate.rs CLAIM_RANGE.
#define NAV_CLAIM_RANGE		500.0

// host editor.rs Editor: per player, the waypoint display (m_fWaypointDisplayTime, the waypoint
// it stands at) and auto-waypointing; config/client.rs Client's waypoint_on, waypoint_paths_on,
// auto_waypoint, debug_auto_waypoint (set through navigate_editor_set).
new bool:g_ed_on[RC_SLOTS];
new bool:g_ed_paths[RC_SLOTS];
new bool:g_ed_auto[RC_SLOTS];
new bool:g_ed_debug[RC_SLOTS];
new bool:g_ed_auto_running[RC_SLOTS];
new Float:g_ed_display_time[RC_SLOTS];
new g_ed_current_wpt[RC_SLOTS] = {-1, ...};
// m_iWaypointTexture (sprites/lgtning.spr, navigate_precache); 0 not precached: no beams.
new g_nv_beam_sprite;

// mod.rs Bot::new's AStar / AStarBot / Explore: the bot's path object.
stock navigate_bot_init(id)
{
	if (!g_nv_i[id][NV_PATH])
	{
		g_nv_i[id][NV_PATH] = _:nav_path_create();
	}
}

// The bot left: its path object goes.
stock navigate_bot_free(id)
{
	if (g_nv_i[id][NV_PATH])
	{
		new RcPath:p = RcPath:g_nv_i[id][NV_PATH];
		nav_path_free(p);
		g_nv_i[id][NV_PATH] = 0;
	}
}

// think.rs Bot::teleported, navigation's part: the waypoints and path belong to where it was.
stock navigate_teleported(id)
{
	g_nv_i[id][NV_CURRENT_WPT] = -1;
	g_nv_i[id][NV_PREV_WPT] = -1;
	g_nv_i[id][NV_CURRENT_WPT_FLAGS] = 0;
	g_nv_f[id][NV_PREV_WPT_DIST] = 4096.0;
	if (g_nv_i[id][NV_PATH])
	{
		nav_path_clear(RcPath:g_nv_i[id][NV_PATH]);
	}
	g_nv_f[id][NV_UPDATE_WPT_TIME] = 0.0;
}

// think.rs Bot::spawn_init, navigation's part (paths.bot_paths cleared, next_clear_failed_goals
// 0). The module has no native for the timer alone; 0 means "clear the failed goals at the next
// think" (navigate_think), which follows this at once, so they are cleared here.
stock navigate_spawn_init(id)
{
	if (g_nv_i[id][NV_PATH])
	{
		new RcPath:p = RcPath:g_nv_i[id][NV_PATH];
		nav_path_clear(p);
		nav_path_clear_failed_goals(p);
	}
	g_nv_i[id][NV_LAST_FAILED_WPT] = -1;
	g_nv_f[id][NV_UPDATE_WPT_TIME] = 0.0;
	g_nv_i[id][NV_GOAL_WPT] = -1;
	g_nv_i[id][NV_CURRENT_WPT] = -1;
	g_nv_i[id][NV_PREV_WPT] = -1;
	g_nv_i[id][NV_CURRENT_WPT_FLAGS] = 0;
	g_nv_f[id][NV_PREV_WPT_DIST] = 4096.0;
	g_nv_f[id][NV_LAST_SEE_WPT] = 0.0;
	g_nv_f[id][NV_FIND_PATH_TIME] = 0.0;
	g_nv_i[id][NV_TOUCHED_WPTS] = 0;
}

// mod.rs Bot::next_waypoint (GetNextWaypoint): the waypoint after the current one on the path.
stock navigate_next_waypoint(id)
{
	new cur = g_nv_i[id][NV_CURRENT_WPT];
	if (!g_nv_i[id][NV_PATH])
	{
		return cur;
	}
	return nav_path_after(RcPath:g_nv_i[id][NV_PATH], cur);
}

// lib.rs map precache (m_iWaypointTexture): the editor's beam sprite. From plugin_precache.
stock navigate_precache()
{
	g_nv_beam_sprite = precache_model("sprites/lgtning.spr");
}

// host commands.rs load_waypoints (nav_load: <map>.rcw and its .rcv, missing visibility rows
// traced over the next frames) and bots.rs load_heatmap (nav_heatmap_load: <map>.rhm for these
// waypoints, faded once). From plugin_cfg, every map.
stock navigate_load_map()
{
	new map[64];
	new bool:loaded = nav_load("");
	nav_map(map, charsmax(map));
	if (loaded)
	{
		rc_log(TSLOG_INFO, "loaded %d waypoints for %s (%d visibility rows to trace)", nav_count(), map, nav_vis_pending());
	}
	else
	{
		rc_log(TSLOG_WARNING, "no waypoints for %s", map);
	}
	nav_heatmap_load();
	for (new p = 1; p <= MAX_PLAYERS; p++)
	{
		g_ed_auto_running[p] = false;
		g_ed_current_wpt[p] = -1;
		g_ed_display_time[p] = 0.0;
	}
}

// host bots.rs save_globals' heatmap (nav_heatmap_save; the module writes it only for the map's
// own heatmap). From plugin_end.
stock navigate_save_map()
{
	if (nav_count() > 0)
	{
		nav_heatmap_save();
	}
}

// config/client.rs Client's waypoint_on / waypoint_paths_on / auto_waypoint /
// debug_auto_waypoint for a player: commands.inc's `rcbot waypoint on|off`, `pathwaypoint`,
// `autowaypoint` set them here (host editor.rs reads them each frame).
stock navigate_editor_set(player, bool:on, bool:paths, bool:auto, bool:debug_auto)
{
	if (player < 1 || player > MAX_PLAYERS)
	{
		return;
	}
	g_ed_on[player] = on;
	g_ed_paths[player] = paths;
	g_ed_auto[player] = auto;
	g_ed_debug[player] = debug_auto;
	if (!on)
	{
		g_ed_current_wpt[player] = -1;
	}
}

// config/client.rs Client::current_waypoint: the waypoint the editing player stands at (the
// display's), -1 none.
stock navigate_editor_current_waypoint(player)
{
	return player >= 1 && player <= MAX_PLAYERS ? g_ed_current_wpt[player] : -1;
}

// A player left: its editor state goes (Clients::disconnect).
stock navigate_editor_player_left(player)
{
	navigate_editor_set(player, false, false, false, false);
	g_ed_auto_running[player] = false;
	g_ed_display_time[player] = 0.0;
}

// host editor.rs send: the module's queued editor events to player (TE_BEAMPOINTS beams with
// m_iWaypointTexture, "play <sample>" for a sound, a console message). Commands that edit
// (nav_add, nav_delete_near, nav_edit_create_path, ...) call this after the native.
stock navigate_send_events(player)
{
	static text[192];
	new Float:start[3], Float:end[3], params[7];
	new bool:human = player >= 1 && player <= MAX_PLAYERS && is_user_connected(player) && !is_user_bot(player);
	for (;;)
	{
		new NavEvent:ev = nav_event_pop(start, end, params, text, charsmax(text));
		if (ev == NAV_EVENT_NONE)
		{
			break;
		}
		if (!human)
		{
			continue;
		}
		switch (ev)
		{
			case NAV_EVENT_BEAM:
			{
				if (g_nv_beam_sprite)
				{
					message_begin(MSG_ONE_UNRELIABLE, SVC_TEMPENTITY, _, player);
					write_byte(TE_BEAMPOINTS);
					write_coord_f(start[0]);
					write_coord_f(start[1]);
					write_coord_f(start[2]);
					write_coord_f(end[0]);
					write_coord_f(end[1]);
					write_coord_f(end[2]);
					write_short(g_nv_beam_sprite);
					write_byte(1);	// framestart
					write_byte(10);	// framerate
					write_byte(10);	// life in 0.1 s
					write_byte(params[0]);	// width
					write_byte(params[1]);	// noise
					write_byte(params[2]);
					write_byte(params[3]);
					write_byte(params[4]);
					write_byte(params[5]);	// brightness
					write_byte(params[6]);	// speed
					message_end();
				}
			}
			case NAV_EVENT_SOUND:
			{
				client_cmd(player, "play %s", text);
			}
			case NAV_EVENT_MESSAGE:
			{
				client_print(player, print_console, "%s%s", RC_MSG_TAG, text);
			}
		}
	}
}

// host editor.rs frame: for each editing player, the waypoint display once a second
// (nav_draw_around) and auto-waypointing (nav_auto_waypoint), then the queued events sent to
// them. Every frame; nothing for a server with no editing player.
stock navigate_editor_frame()
{
	new Float:now = g_now;
	new Float:origin[3];
	for (new p = 1; p <= MAX_PLAYERS; p++)
	{
		if (!g_ed_on[p] && !g_ed_auto[p])
		{
			if (g_ed_auto_running[p])
			{
				g_ed_auto_running[p] = false;
				nav_auto_reset(p);
			}
			continue;
		}
		if (world_userid(p) == 0)
		{
			continue;
		}
		world_origin(p, origin);
		if (g_ed_on[p] && g_ed_display_time[p] < now)
		{
			g_ed_display_time[p] = now + 1.0;
			g_ed_current_wpt[p] = nav_draw_around(p, origin, g_ed_paths[p]);
		}
		if (g_ed_auto[p])
		{
			g_ed_auto_running[p] = true;
			nav_auto_waypoint(p, g_ed_on[p], g_ed_debug[p], g_cfg_i[CFG_AUTO_PATH_WAYPOINT] != 0);
		}
		else if (g_ed_auto_running[p])
		{
			g_ed_auto_running[p] = false;
			nav_auto_reset(p);
		}
		navigate_send_events(p);
	}
}

// think.rs Think's failed-goal upkeep (failed goals cleared when their time is up, expired
// back-offs forgotten), each think before anything else (nav_path_think).
stock navigate_think(id)
{
	if (g_nv_i[id][NV_PATH])
	{
		nav_path_think(RcPath:g_nv_i[id][NV_PATH]);
	}
}

// Entity::is_alive for an EntRef: a player's snapshot, else deadflag and health.
static stock bool:nv_ent_alive(ent, serial)
{
	if (!entref_alive(ent, serial))
	{
		return false;
	}
	if (ent <= MAX_PLAYERS)
	{
		return world_alive(ent);
	}
	new Float:health;
	pev(ent, pev_health, health);
	return pev(ent, pev_deadflag) == RC_DEAD_NO && health > 0.0;
}

// navigate.rs Bot::update_waypoint (BotNavigate_UpdateWaypoint): checks whether the bot reached
// its waypoint (touch distance by its flags, ladders, human towers, walking past it) and moves on
// to the next; a waypoint not seen for 2 s is given up with its path. False when it has no
// waypoint to go to.
stock bool:navigate_update_waypoint(id)
{
	if (!g_nv_i[id][NV_PATH])
	{
		return false;
	}
	new RcPath:p = RcPath:g_nv_i[id][NV_PATH];
	new Float:now = g_now;
	new Float:origin[3];
	world_origin(id, origin);
	new cur = g_nv_i[id][NV_CURRENT_WPT];
	new bool:touched = false;

	if (g_nv_i[id][NV_GOAL_WPT] == -1)
	{
		// look_task is look's field; navigate.rs sets it here.
		g_lk_i[id][LK_LOOK_TASK] = nv_ent_alive(g_cb_i[id][CB_LAST_ENEMY], g_cb_i[id][CB_LAST_ENEMY_SERIAL]) ? LOOK_SEARCH_FOR_LAST_ENEMY : LOOK_LOOK_AROUND;
		if (g_nv_i[id][NV_CURRENT_WPT] == -1)
		{
			if (g_mv_i[id][MV_NOT_FOLLOWING_WAYPOINT])
			{
				g_nv_i[id][NV_CURRENT_WPT] = navigate_nearest_waypoint(id, origin, 200.0, false);
			}
			if (g_nv_i[id][NV_CURRENT_WPT] == -1)
			{
				if (!g_mv_i[id][MV_MOVE_TO_VALID] || movement_distance_from(id, g_mv_v[id][MV_MOVE_TO], true) < BOT_WAYPOINT_TOUCH_DIST)
				{
					new Float:v[3];
					navigate_scan_fov(id, v);
					movement_set_move_vector(id, v);
					g_mv_i[id][MV_NOT_FOLLOWING_WAYPOINT] = 1;
				}
			}
		}
	}

	if (cur != -1 && !(g_th_i[id][TH_CONDITIONS] & COND_SEE_NEXT_WAYPOINT))
	{
		new Float:last = g_nv_f[id][NV_LAST_SEE_WPT];
		if (last != 0.0 && last + 2.0 <= now)
		{
			// Not seen for two seconds: give up on it and its path.
			navigate_fail_path(id, g_nv_i[id][NV_PREV_WPT], g_nv_i[id][NV_CURRENT_WPT]);
			cur = navigate_nearest_waypoint(id, origin, NAV_REACHABLE_RANGE, true, g_nv_i[id][NV_LAST_FAILED_WPT], false, true);
			g_nv_i[id][NV_CURRENT_WPT] = cur;
			if (g_tk_count[id] > 0)
			{
				// The current task's found_path is the task queue's field; navigate.rs clears it.
				g_tk_i[id][0][TK_FOUND_PATH] = 0;
			}
			if (cur != -1)
			{
				g_nv_f[id][NV_PREV_WPT_DIST] = nav_distance(cur, origin);
			}
			g_nv_f[id][NV_LAST_SEE_WPT] = 0.0;
		}
		else if (last == 0.0)
		{
			g_nv_f[id][NV_LAST_SEE_WPT] = now;
		}
	}

	if (cur == -1)
	{
		cur = navigate_nearest_waypoint(id, origin, NAV_REACHABLE_RANGE, true, g_nv_i[id][NV_LAST_FAILED_WPT], false, true);
		g_nv_i[id][NV_CURRENT_WPT] = cur;
		if (cur == -1)
		{
			// No waypoint near: look again in half a second (Think's wait).
			g_nv_f[id][NV_UPDATE_WPT_TIME] = now + 0.5;
			return false;
		}
		g_nv_f[id][NV_PREV_WPT_DIST] = nav_distance(cur, origin);
	}

	g_mv_i[id][MV_NOT_FOLLOWING_WAYPOINT] = 0;
	new Float:wpt_origin[3], Float:size[3];
	nav_origin(cur, wpt_origin);
	movement_size(id, size);
	new wpt_flags = nav_flags(cur);
	new climb = movement_climb_type(id);
	new Float:distance;
	if (climb != CLIMB_NONE || floatabs(wpt_origin[2] - origin[2]) >= 72.0)
	{
		distance = vec_distance(wpt_origin, origin);
	}
	else
	{
		distance = vec_distance_2d(wpt_origin, origin);
	}

	if ((wpt_flags & (NAV_FL_LADDER | NAV_FL_FLY)) || climb == CLIMB_CLIMBING)
	{
		new next = navigate_next_waypoint(id);
		if (next == cur)
		{
			touched = true;
		}
		else if (g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_FLY)
		{
			// TS bots can't fly (CanFly).
			touched = true;
		}
		else if (next != -1 && (nav_flags(next) & (NAV_FL_LADDER | NAV_FL_FLY)))
		{
			new dir = movement_ladder_dir(id, false);
			new next_dir = movement_ladder_dir(id, true);
			if (vec_distance_2d(wpt_origin, origin) <= vec_length_2d(size) / 2.0)
			{
				if (dir == 0 || (dir == -1 && next_dir != -1) || (dir == 1 && next_dir != 1))
				{
					touched = true;
				}
				else if (dir == -1)
				{
					touched = wpt_origin[2] >= origin[2];
				}
				else
				{
					touched = wpt_origin[2] <= origin[2];
				}
			}
		}
		else
		{
			if (pev(id, pev_movetype) == RC_MOVETYPE_FLY && !(g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_LADDER) && next != -1
				&& g_nv_i[id][NV_PREV_WPT] != -1)
			{
				new Float:co[3], Float:po[3], Float:no[3];
				nav_origin(g_nv_i[id][NV_CURRENT_WPT], co);
				nav_origin(g_nv_i[id][NV_PREV_WPT], po);
				nav_origin(next, no);
				if (co[2] <= po[2] && vec_distance_2d(origin, no) > 100.0 && random_num(0, 1) != 0)
				{
					movement_jump(id);
				}
			}
			touched = vec_distance_2d(wpt_origin, origin) <= vec_length_2d(size) / 2.0 && floatabs(wpt_origin[2] - origin[2]) <= size[2] / 2.0;
		}
	}
	else if (wpt_flags & NAV_FL_HUMAN_TOWER)
	{
		touched = special_touched_human_tower(id, wpt_origin, distance);
	}
	else if (wpt_flags & (NAV_FL_STAY_NEAR | NAV_FL_CROUCH))
	{
		if (distance <= 24.0)
		{
			touched = true;
		}
		else if (distance <= 64.0)
		{
			// PlayerNearVector.
			touched = nv_client_near(wpt_origin, 24.0);
		}
	}
	else if (wpt_flags & (NAV_FL_STUNT | NAV_FL_JUMP | NAV_FL_CROUCHJUMP))
	{
		touched = distance < 32.0;
	}
	else if (movement_distance_from(id, wpt_origin, true) < BOT_WAYPOINT_TOUCH_DIST && floatabs(wpt_origin[2] - origin[2]) <= size[2])
	{
		touched = true;
	}
	else if (pev(id, pev_waterlevel) > 1)
	{
		touched = distance < BOT_WAYPOINT_TOUCH_DIST * 2.0;
	}
	else if (g_cb_i[id][CB_ENEMY] == 0)
	{
		// Walking away from it again: it was passed.
		touched = distance - 1.0 > g_nv_f[id][NV_PREV_WPT_DIST];
	}

	if (touched)
	{
		g_nv_i[id][NV_TOUCHED_WPTS]++;
		nav_path_touch(p, g_nv_i[id][NV_CURRENT_WPT]);
		if (g_tk_count[id] > 0 && g_tk_i[id][0][TK_KIND] == TASK_FIND_PATH)
		{
			// Task::set_time_to_complete on the current task (the queue's field; navigate.rs).
			g_tk_f[id][0][TK_TIME_TO_COMPLETE] = now + random_float(20.0, 30.0);
		}
		g_nv_f[id][NV_PREV_WPT_DIST] = 9999.0;
		if (g_nv_i[id][NV_CURRENT_WPT] == g_nv_i[id][NV_GOAL_WPT])
		{
			// A goal reached isn't a roaming goal again for a while (design §4).
			nav_path_goal_reached(p, g_nv_i[id][NV_CURRENT_WPT]);
		}
		// ladder_angles is look's field; navigate.rs clears it here.
		g_lk_i[id][LK_LADDER_ANGLES_SET] = 0;
		g_nv_i[id][NV_PREV_WPT] = g_nv_i[id][NV_CURRENT_WPT];
		new cflags = nav_flags(g_nv_i[id][NV_CURRENT_WPT]);
		g_nv_i[id][NV_CURRENT_WPT_FLAGS] = cflags;
		if (cflags & NAV_FL_JUMP)
		{
			movement_jump_and_duck(id);
		}
		else if (cflags & NAV_FL_CROUCHJUMP)
		{
			movement_duck_and_jump(id);
		}
		if (cflags & NAV_FL_STUNT)
		{
			new weapon = g_bw_i[id][BW_CURRENT];
			new bool:kung_fu = weapon == -1 || weapon == RC_KUNGFU;
			if (!kung_fu || g_cb_i[id][CB_ENEMY] == 0)
			{
				// Upstream pressed the key alone, so the bot landed prone and lay there (design §4).
				movement_dive(id);
			}
		}
		special_touched_pushable_waypoint(id, wpt_origin);
		if (g_nv_i[id][NV_GOAL_WPT] == g_nv_i[id][NV_CURRENT_WPT])
		{
			if (g_tk_count[id] > 0 && g_tk_i[id][0][TK_KIND] == TASK_FIND_PATH)
			{
				new Float:t_float = g_tk_f[id][0][TK_FLOAT];
				new t_edict = g_tk_i[id][0][TK_EDICT], t_serial = g_tk_i[id][0][TK_EDICT_SERIAL];
				new Float:t_vec[3];
				vec_copy(g_tk_vec[id][0], t_vec);
				tasks_finished_current(id, false);
				if (t_float >= 0.0 && t_vec[2] - origin[2] <= RC_MAX_JUMP_HEIGHT && movement_distance_from(id, t_vec, false) > BOT_WAYPOINT_TOUCH_DIST)
				{
					tasks_add_priority(id, TASK_MOVE_TO_VECTOR, tasks_new_schedule_id(id), t_edict, t_serial, 0, 0.0, t_vec);
				}
			}
			movement_stop_moving(id);
			g_nv_i[id][NV_GOAL_WPT] = -1;
			return false;
		}
		new prev_flags = g_nv_i[id][NV_CURRENT_WPT_FLAGS];
		g_nv_i[id][NV_PREV_WPT] = g_nv_i[id][NV_CURRENT_WPT];
		cur = navigate_find_next_waypoint(id);
		g_nv_i[id][NV_CURRENT_WPT] = cur;
		if (config_is_on(CFG_BOTS_WAIT_FOR_BOTS) && cur != -1)
		{
			// g.other_bots: every other bot (dead ones too), in index order.
			for (new b = 1; b <= MAX_PLAYERS; b++)
			{
				if (b != id && g_co_i[b][CO_USED] && g_nv_i[b][NV_CURRENT_WPT] == cur)
				{
					tasks_add_priority(id, TASK_WAIT_FOR_BOT_AT_WPT, 0, b, world_userid(b), cur, 0.0, VEC_ZERO);
					break;
				}
			}
		}
		if (cur == -1)
		{
			g_nv_i[id][NV_CURRENT_WPT_FLAGS] = 0;
			return false;
		}
		new Float:wpt[3];
		nav_origin(cur, wpt);
		g_nv_i[id][NV_CURRENT_WPT_FLAGS] = nav_flags(cur);
		special_next_waypoint_tasks(id, prev_flags);
		new bool:on_ladder = pev(id, pev_movetype) == RC_MOVETYPE_FLY;
		new bool:wpt_on_ladder = (g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_LADDER) != 0;
		if (!on_ladder && wpt_on_ladder)
		{
			if (wpt[2] - 16.0 > origin[2])
			{
				movement_jump(id);
			}
		}
		else if (on_ladder && !wpt_on_ladder)
		{
			g_mv_f[id][MV_START_DUCK_TIME] = now;
			g_mv_f[id][MV_END_DUCK_TIME] = now + 1.0;
		}
		else if (g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_HUMAN_TOWER)
		{
			tasks_add_priority(id, TASK_HUMAN_TOWER, 0, 0, 0, 0, 0.0, wpt);
		}
	}
	else
	{
		g_nv_f[id][NV_PREV_WPT_DIST] = distance;
	}

	cur = g_nv_i[id][NV_CURRENT_WPT];
	if (cur == -1)
	{
		g_nv_i[id][NV_CURRENT_WPT_FLAGS] = 0;
		return false;
	}
	g_nv_i[id][NV_CURRENT_WPT_FLAGS] = nav_flags(cur);
	new Float:to[3];
	nav_origin(cur, to);
	movement_set_move_vector(id, to);
	return true;
}

// special.rs player_near_vector as update_waypoint uses it: a client within range of origin.
static stock bool:nv_client_near(const Float:origin[3], Float:range)
{
	static ents[64];
	new n = world_entities_in_sphere(origin, range, ents, sizeof(ents));
	for (new k = 0; k < n; k++)
	{
		if (ents[k] >= 1 && ents[k] <= MAX_PLAYERS && world_userid(ents[k]) != 0)
		{
			return true;
		}
	}
	return false;
}

// navigate.rs Bot::find_next_waypoint (BotNavigate_FindNextWaypoint): the next waypoint on the
// path (a missed one popped: the current one is looked for further on, else the edge fails);
// -1 to get a new goal (a roaming goal is picked, FIND_PATH works the path out), 0 when the bot
// has no waypoint (upstream's answer, kept).
stock navigate_find_next_waypoint(id)
{
	if (!g_nv_i[id][NV_PATH])
	{
		return 0;
	}
	new RcPath:p = RcPath:g_nv_i[id][NV_PATH];
	new cur = g_nv_i[id][NV_CURRENT_WPT];
	if (g_nv_i[id][NV_GOAL_WPT] != -1)
	{
		new index = nav_path_pop(p);
		if (index != -1)
		{
			new top;
			if (index != cur)
			{
				// Missed a waypoint: find the current one further on the path.
				while ((top = nav_path_next(p)) != -1)
				{
					if (top == cur)
					{
						return top;
					}
					nav_path_pop(p);
				}
				if (g_nv_i[id][NV_PREV_WPT] != -1)
				{
					navigate_fail_path(id, g_nv_i[id][NV_PREV_WPT], cur);
				}
			}
			else if ((top = nav_path_next(p)) != -1)
			{
				return top;
			}
		}
	}
	nav_path_clear(p);
	if (g_nv_i[id][NV_GOAL_WPT] == -1)
	{
		g_nv_i[id][NV_GOAL_WPT] = navigate_roam_goal(id);
	}
	if (g_nv_i[id][NV_CURRENT_WPT] == -1)
	{
		new Float:origin[3];
		world_origin(id, origin);
		g_nv_i[id][NV_CURRENT_WPT] = navigate_nearest_waypoint(id, origin, NAV_REACHABLE_RANGE, true, g_nv_i[id][NV_LAST_FAILED_WPT], false, true);
	}
	if (g_nv_i[id][NV_CURRENT_WPT] == -1)
	{
		return 0;
	}
	return -1;
}

// navigate.rs Bot::nearest_waypoint (WaypointLocations.NearestWaypoint(origin, range, ignore,
// visible, unreachable, is_bot)). The Rust passes no failed goals; neither does this.
stock navigate_nearest_waypoint(id, const Float:origin[3], Float:range, bool:visible, ignore = -1, bool:unreachable = false, bool:is_bot = false)
{
	#pragma unused id
	new flags = (visible ? NAV_NEAREST_VISIBLE : 0) | (unreachable ? NAV_NEAREST_UNREACHABLE : 0) | (is_bot ? NAV_NEAREST_BOT : 0);
	return nav_nearest(origin, range, ignore, flags);
}

// navigate.rs Bot::roam_goal (WaypointFindRandomGoal for TS): any usable waypoint, more likely
// the longer the bot hasn't been there and the farther from its teammates' claimed goals, not
// the failed or backed-off goals (nav_path_roam_goal). -1 for none.
stock navigate_roam_goal(id)
{
	if (!g_nv_i[id][NV_PATH])
	{
		return -1;
	}
	static claims[RC_SLOTS];
	new n = board_claimed_waypoints(world_team(id), id, claims, sizeof(claims));
	new Float:origin[3];
	world_origin(id, origin);
	return nav_path_roam_goal(RcPath:g_nv_i[id][NV_PATH], g_th_i[id][TH_TEAM], origin, claims, n);
}

// navigate.rs Bot::claim_factor: how much the bot still wants a goal its teammates have claimed
// (1: nobody has; less the nearer their goal is to this one).
stock Float:navigate_claim_factor(id, waypoint)
{
	static claims[RC_SLOTS];
	new n = board_claimed_waypoints(world_team(id), id, claims, sizeof(claims));
	new Float:at[3], Float:o[3];
	nav_origin(waypoint, at);
	new Float:f = 1.0;
	for (new k = 0; k < n; k++)
	{
		nav_origin(claims[k], o);
		new Float:d = vec_distance(o, at);
		if (d < NAV_CLAIM_RANGE)
		{
			f = fmin(f, 0.2 + 0.8 * d / NAV_CLAIM_RANGE);
		}
	}
	return f;
}

// navigate.rs Bot::search_path (BotNavigate_AStarAlgo for the bot): a goal no path reaches is
// backed off (design §4), and with BOT_CONFIG_TACTICAL_PATHS the costs avoid the heatmap's
// danger, the waypoints enemies were lately seen from and teammates' paths (rebuilt at every
// call, resumed searches too). Expands the profile's path revs per call; NAV_PATH_SEARCHING:
// call again on a later frame with resume (think.rs task_find_path's float -3). Found, `from` is
// pushed on the path (think.rs push_current_on_path; pushing again is harmless).
stock NavPathStatus:navigate_search_path(id, from, to, bool:resume)
{
	if (!g_nv_i[id][NV_PATH])
	{
		return NAV_PATH_FAILED;
	}
	new RcPath:p = RcPath:g_nv_i[id][NV_PATH];
	new flags = NAV_FIND_BACK_OFF;
	if (config_is_on(CFG_TACTICAL_PATHS))
	{
		flags |= NAV_FIND_TACTICS;
		navigate_update_threats(id);
		navigate_update_crowding(id);
	}
	new Float:gun[3];
	world_eyes(id, gun);
	new revs = g_pr_i[id][PR_PATH_REVS];
	new NavPathStatus:st = nav_path_find(p, from, to, gun, g_th_i[id][TH_TEAM], revs < 1 ? 1 : revs, flags, resume);
	if (st == NAV_PATH_FOUND && from >= 0)
	{
		nav_path_push(p, from, true);
	}
	return st;
}

// navigate.rs Bot::threat_waypoints: the waypoints nearest where enemies were lately seen, by
// the bot (remember.inc, weight 1 - age / REMEMBER_FOR) or its team (board.inc, 1 - age /
// SIGHTING_FOR), at least 0.1, the highest kept (nav_path_add_threat does the nearest waypoint
// within 400 units and the weights).
stock navigate_update_threats(id)
{
	if (!g_nv_i[id][NV_PATH])
	{
		return;
	}
	new RcPath:p = RcPath:g_nv_i[id][NV_PATH];
	new Float:now = g_now;
	nav_path_clear_threats(p);
	for (new k = 0; k < g_rm_count[id]; k++)
	{
		nav_path_add_threat(p, g_rm_origin[id][k], 1.0 - (now - g_rm_time[id][k]) / REMEMBER_FOR);
	}
	board_add_threats(world_team(id), p);
}

// navigate.rs Bot::crowded_waypoints: the waypoints on teammates' paths (nav_path_crowd_from
// with the path objects of the teammates whose posts on the board are current). The Rust reads
// each mate's path as posted (at most POST_FOR old); this reads it as it is now.
stock navigate_update_crowding(id)
{
	if (!g_nv_i[id][NV_PATH])
	{
		return;
	}
	static RcPath:others[RC_SLOTS];
	new RcPath:p = RcPath:g_nv_i[id][NV_PATH];
	new n = board_mate_paths(world_team(id), id, others, sizeof(others));
	if (n > 0)
	{
		nav_path_crowd_from(p, others, n);
	}
	else
	{
		new none[1];
		nav_path_set_crowded(p, none, 0);
	}
}

// navigate.rs Bot::claim: tells the bot's team where it is going (a waypoint, or an item) for
// the next 30 s: teammates leave it to the bot.
stock navigate_claim(id, waypoint, item, item_serial)
{
	board_claim(id, waypoint, item, item_serial, g_now + 30.0);
}

// navigate.rs Bot::share_with_team: tells the bot's team what it knows now (its position and
// path, the enemy it fights, the sounds it heard this frame) and forgets what is old on the
// board. Nothing outside teamplay.
stock navigate_share_with_team(id)
{
	new team = world_team(id);
	if (!board_exists(team))
	{
		return;
	}
	new Float:now = g_now;
	board_expire(team);
	new Float:origin[3];
	world_origin(id, origin);
	board_post(id, origin);
	new enemy = g_cb_i[id][CB_ENEMY];
	new Float:eo[3];
	if (enemy && (g_th_i[id][TH_CONDITIONS] & COND_SEE_ENEMY) && entref_alive(enemy, g_cb_i[id][CB_ENEMY_SERIAL]) && world_entity_origin(enemy, eo))
	{
		board_sight(team, id, enemy, g_cb_i[id][CB_ENEMY_SERIAL], eo);
	}
	for (new k = 0; k < g_hr_count[id]; k++)
	{
		if (g_hr_f[id][k][HR_TIME] == now)
		{
			board_hear(team, id, g_hr_origin[id][k], g_hr_f[id][k][HR_LOUD]);
		}
	}
}

// navigate.rs Bot::search_move: a moving search around center (SEARCH_FOR_ENEMY with a place):
// walks to random waypoints within 500 units of it that the bot can see, one after another.
// search_point is think's field; navigate.rs keeps it.
stock navigate_search_move(id, const Float:center[3])
{
	static near[RC_MAX_WAYPOINTS];
	new bool:arrived = !g_th_i[id][TH_SEARCH_POINT_SET] || movement_distance_from(id, g_th_v[id][TH_SEARCH_POINT], true) < BOT_WAYPOINT_TOUCH_DIST * 1.5;
	if (arrived)
	{
		g_th_i[id][TH_SEARCH_POINT_SET] = 0;
		new n = 0, count = nav_count();
		new Float:o[3];
		for (new i = 0; i < count && n < sizeof(near); i++)
		{
			if (nav_flags(i) & (NAV_FL_DELETED | NAV_FL_AIMING))
			{
				continue;
			}
			nav_origin(i, o);
			if (vec_distance(o, center) < 500.0 && movement_distance_from(id, o, true) > 64.0)
			{
				near[n++] = i;
			}
		}
		new Float:gun[3], Float:end_pos[3], hit;
		world_eyes(id, gun);
		for (new k = 0; k < 4 && n > 0; k++)
		{
			nav_origin(near[random_num(0, n - 1)], o);
			if (world_trace_line(gun, o, true, true, id, end_pos, hit) >= 1.0)
			{
				g_th_i[id][TH_SEARCH_POINT_SET] = 1;
				vec_copy(o, g_th_v[id][TH_SEARCH_POINT]);
				break;
			}
		}
	}
	if (g_th_i[id][TH_SEARCH_POINT_SET])
	{
		movement_set_move_vector(id, g_th_v[id][TH_SEARCH_POINT]);
	}
	else
	{
		movement_stop_moving(id);
	}
}

// navigate.rs Bot::fail_path (m_stFailedPaths.AddFailedPath): the path from -> to for the bot's
// team, if there is one.
stock navigate_fail_path(id, from, to)
{
	if (from < 0 || to < 0 || !g_nv_i[id][NV_PATH])
	{
		return;
	}
	nav_path_fail(RcPath:g_nv_i[id][NV_PATH], from, to, g_th_i[id][TH_TEAM]);
}

// navigate.rs Bot::scan_fov (BotNavigate_ScanFOV): with no waypoints, the furthest open point
// ahead (or along the velocity, counted double), one of the furthest at random; a crouch jump
// when something low is close in front (design §4: upstream jumped).
stock navigate_scan_fov(id, Float:out[3])
{
	static Float:positions[24][3];
	new count = 0;
	new Float:start[3], Float:angles[3], Float:vel[3], Float:a[3], Float:fwd[3], Float:right[3], Float:end[3], Float:end_pos[3], hit;
	world_eyes(id, start);
	pev(id, pev_angles, angles);
	world_velocity(id, vel);
	new Float:max_distance = BOT_WAYPOINT_TOUCH_DIST * 2.0;
	for (new step = -60; step <= 60; step += 10)
	{
		vec_copy(angles, a);
		a[1] = fix_float_angle(a[1] + float(step) - 30.0);
		make_vectors(a, fwd, right);
		vec_mad(start, fwd, NAV_REACHABLE_RANGE, end);
		world_trace_line(start, end, true, true, id, end_pos, hit);
		nv_consider(positions, count, end_pos, movement_distance_from(id, end_pos, false), max_distance);
	}
	new Float:high = 0.0;
	new Float:vang[3];
	vec_to_angles(vel, vang);
	for (new step = -60; step <= 60; step += 15)
	{
		vec_copy(vang, a);
		a[1] = fix_float_angle(a[1] + float(step) - 30.0);
		make_vectors(a, fwd, right);
		vec_mad(start, fwd, NAV_REACHABLE_RANGE, end);
		world_trace_line(start, end, true, true, id, end_pos, hit);
		new Float:d = movement_distance_from(id, end_pos, false) * 2.0;
		if (step == 30)
		{
			high = d;
		}
		nv_consider(positions, count, end_pos, d, max_distance);
	}
	new Float:low[3], Float:size[3];
	world_origin(id, low);
	movement_size(id, size);
	low[2] -= size[2] / 3.0;
	vec_mad(low, vel, 2.0, end);
	world_trace_line(low, end, true, true, id, end_pos, hit);
	new Float:low_distance = movement_distance_from(id, end_pos, false);
	if (low_distance < 64.0 && low_distance < high)
	{
		// A crouch jump clears what a plain jump hits with its feet (design §4).
		movement_jump_and_duck(id);
	}
	if (count == 0)
	{
		// look_task is look's field; navigate.rs sets it here.
		g_lk_i[id][LK_LOOK_TASK] = LOOK_NEXT_WAYPOINT;
		vec_zero(a);
		a[1] = angles[1];
		make_vectors(a, fwd, right);
		vec_mad(start, fwd, -NAV_REACHABLE_RANGE, end);
		world_trace_line(start, end, true, true, id, out, hit);
		return;
	}
	vec_copy(positions[random_num(0, count - 1)], out);
}

// scan_fov's consider: a further point replaces the list, an equally far one joins it.
static stock nv_consider(Float:positions[][3], &count, const Float:end[3], Float:d, &Float:max)
{
	if (d > max)
	{
		count = 0;
		max = d;
		vec_copy(end, positions[count++]);
	}
	else if (count > 0 && count < 24 && floatabs(d - max) < 0.00000012)
	{
		vec_copy(end, positions[count++]);
	}
}

// navigate.rs Bot::wall_on_side (BotCheckWallOnLeft side -1 / BotCheckWallOnRight 1): a wall
// within 40 units.
stock bool:navigate_wall_on_side(id, Float:side)
{
	new Float:fwd[3], Float:right[3], Float:src[3], Float:end[3], Float:end_pos[3], hit;
	make_vectors(g_co_v[id][CO_VANGLE], fwd, right);
	world_origin(id, src);
	vec_mad(src, right, 40.0 * side, end);
	return world_trace_line(src, end, false, false, id, end_pos, hit) < 1.0;
}

// navigate.rs Bot::got_stuck (CBot::gotStuck): back to the previous waypoint, with the one it
// was heading for on the path again after it (upstream decremented the waypoint index, every
// frame of the second second of being stuck; design §4).
stock navigate_got_stuck(id)
{
	new prev = g_nv_i[id][NV_PREV_WPT], cur = g_nv_i[id][NV_CURRENT_WPT];
	if (prev != -1 && prev != cur && g_nv_i[id][NV_PATH])
	{
		new RcPath:p = RcPath:g_nv_i[id][NV_PATH];
		// The path stack's top is the current waypoint: touching prev pops it and goes on to cur
		// (find_next_waypoint).
		if (cur != -1)
		{
			nav_path_push(p, cur, true);
		}
		nav_path_push(p, prev, false);
		g_nv_i[id][NV_CURRENT_WPT] = prev;
		g_nv_i[id][NV_PREV_WPT] = cur;
		// The "walked past it" touch compares with the old waypoint's distance otherwise.
		g_nv_f[id][NV_PREV_WPT_DIST] = 9999.0;
	}
	g_mv_i[id][MV_NOT_FOLLOWING_WAYPOINT] = 1;
}

// mod.rs Bot::go_to_waypoint (util bot_goto): the tasks flushed, FIND_PATH to the waypoint with
// float -2 (finish once the path is found... and walked: think's task_find_path).
stock navigate_go_to_waypoint(id, waypoint)
{
	tasks_flush(id);
	tasks_add_priority(id, TASK_FIND_PATH, 0, 0, 0, waypoint, -2.0, VEC_ZERO);
}

// manager.rs player_waypoint: the waypoint a player is at (a bot's current one, else the
// nearest to its origin, unreachable ones included) and its rules team. -1 none.
static stock nv_player_waypoint(index, &team)
{
	if (index < 1 || index > MAX_PLAYERS || world_userid(index) == 0)
	{
		return -1;
	}
	team = world_team(index);
	if (is_rcbot(index) && g_nv_i[index][NV_CURRENT_WPT] >= 0)
	{
		return g_nv_i[index][NV_CURRENT_WPT];
	}
	new Float:origin[3];
	world_origin(index, origin);
	return nav_nearest(origin, NAV_REACHABLE_RANGE, -1, NAV_NEAREST_UNREACHABLE);
}

// manager.rs update_heatmap for a damage (the port's damage event): at the victim's waypoint.
// Core calls it from manager_damage.
stock navigate_heatmap_damage(victim, Float:amount)
{
	if (nav_count() == 0)
	{
		return;
	}
	new team;
	new wpt = nv_player_waypoint(victim, team);
	if (wpt >= 0)
	{
		nav_heatmap_add_damage(wpt, team, amount);
	}
}

// manager.rs update_heatmap for a kill: a death at the victim's waypoint, a kill at the
// killer's (not the world's, not a suicide). Core calls it from manager_death.
stock navigate_heatmap_kill(victim, killer)
{
	if (nav_count() == 0)
	{
		return;
	}
	new team;
	new wpt = nv_player_waypoint(victim, team);
	if (wpt >= 0)
	{
		nav_heatmap_add_death(wpt, team);
	}
	if (killer != 0 && killer != victim)
	{
		wpt = nv_player_waypoint(killer, team);
		if (wpt >= 0)
		{
			nav_heatmap_add_kill(wpt, team);
		}
	}
}
