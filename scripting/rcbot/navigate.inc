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
// Status: per-bot path life cycle, teleported / spawn_init and next_waypoint are ported; the
// rest is TODO(navigate).

#if defined _rcbot_navigate_included
	#endinput
#endif
#define _rcbot_navigate_included

// navigate.rs CLAIM_RANGE.
#define NAV_CLAIM_RANGE		500.0

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

// think.rs Bot::spawn_init, navigation's part (paths.bot_paths cleared,
// next_clear_failed_goals 0).
// TODO(navigate): spawn_init's next_clear_failed_goals = 0 has no native yet (nav_path_think
// keeps its own timer); check the module's reset matches.
stock navigate_spawn_init(id)
{
	if (g_nv_i[id][NV_PATH])
	{
		nav_path_clear(RcPath:g_nv_i[id][NV_PATH]);
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
	return g_nv_i[id][NV_PATH] ? nav_path_after(RcPath:g_nv_i[id][NV_PATH], cur) : cur;
}

// TODO(navigate): host commands.rs load_waypoints (nav_load("")) and bots.rs load_heatmap
// (nav_heatmap_load: waypoints/ts/<map>.rhm for these waypoints, decayed once). From plugin_cfg.
stock navigate_load_map()
{
}

// TODO(navigate): bots.rs save_globals' heatmap (nav_heatmap_save). From plugin_end.
stock navigate_save_map()
{
}

// TODO(navigate): host editor.rs frame (nav_auto_waypoint / nav_draw_around for editing players,
// sending nav_event_pop's beams, sounds and messages), every frame.
stock navigate_editor_frame()
{
}

// TODO(navigate): think.rs Think's failed-goal upkeep (nav_path_think), each think.
stock navigate_think(id)
{
	#pragma unused id
}

// TODO(navigate): navigate.rs Bot::update_waypoint (touch by flag distance, nav_path_touch,
// next waypoint, special waypoints, lifts, ladders, the stay-near speed).
stock bool:navigate_update_waypoint(id)
{
	#pragma unused id
	return false;
}

// TODO(navigate): navigate.rs Bot::find_next_waypoint (nav_path_pop / nav_path_next).
stock navigate_find_next_waypoint(id)
{
	#pragma unused id
	return -1;
}

// TODO(navigate): navigate.rs Bot::nearest_waypoint (nav_nearest with the failed goals).
stock navigate_nearest_waypoint(id, const Float:origin[3], Float:range, bool:visible)
{
	#pragma unused id, origin, range, visible
	return -1;
}

// TODO(navigate): navigate.rs Bot::roam_goal (nav_path_roam_goal with the team's claims).
stock navigate_roam_goal(id)
{
	#pragma unused id
	return -1;
}

// TODO(navigate): navigate.rs Bot::claim_factor.
stock Float:navigate_claim_factor(id, waypoint)
{
	#pragma unused id, waypoint
	return 1.0;
}

// TODO(navigate): navigate.rs Bot::search_path and think.rs task_find_path: nav_path_find
// (gun position, team, the profile's path revs, NAV_FIND_TACTICS when CFG_TACTICAL_PATHS, back
// off), resumed across frames while NAV_PATH_SEARCHING; push the current waypoint.
stock NavPathStatus:navigate_search_path(id, from, to, bool:resume)
{
	#pragma unused id, from, to, resume
	return NAV_PATH_FAILED;
}

// TODO(navigate): navigate.rs Bot::threat_waypoints → nav_path_set_threats / nav_path_add_threat.
stock navigate_update_threats(id)
{
	#pragma unused id
}

// TODO(navigate): navigate.rs Bot::crowded_waypoints → nav_path_crowd_from (teammates' paths).
stock navigate_update_crowding(id)
{
	#pragma unused id
}

// TODO(navigate): navigate.rs Bot::claim (board.inc claims).
stock navigate_claim(id, waypoint, item, item_serial)
{
	#pragma unused id, waypoint, item, item_serial
}

// TODO(navigate): navigate.rs Bot::share_with_team (board.inc posts: position and path).
stock navigate_share_with_team(id)
{
	#pragma unused id
}

// TODO(navigate): navigate.rs Bot::search_move.
stock navigate_search_move(id, const Float:center[3])
{
	#pragma unused id, center
}

// TODO(navigate): navigate.rs Bot::fail_path (nav_path_fail).
stock navigate_fail_path(id, from, to)
{
	#pragma unused id, from, to
}

// TODO(navigate): navigate.rs Bot::scan_fov.
stock navigate_scan_fov(id, Float:out[3])
{
	#pragma unused id
	vec_zero(out);
}

// TODO(navigate): navigate.rs Bot::wall_on_side.
stock bool:navigate_wall_on_side(id, Float:side)
{
	#pragma unused id, side
	return false;
}

// TODO(navigate): navigate.rs Bot::got_stuck.
stock navigate_got_stuck(id)
{
	#pragma unused id
}

// TODO(navigate): mod.rs Bot::go_to_waypoint (util bot_goto: flush, FIND_PATH -2).
stock navigate_go_to_waypoint(id, waypoint)
{
	#pragma unused id, waypoint
}

// TODO(navigate): manager.rs player_waypoint / update_heatmap: a death at the victim's
// waypoint, a kill at the killer's, damage at the victim's (nav_heatmap_add_*; rules team).
stock navigate_heatmap_damage(victim, Float:amount)
{
	#pragma unused victim, amount
}

stock navigate_heatmap_kill(victim, killer)
{
	#pragma unused victim, killer
}
