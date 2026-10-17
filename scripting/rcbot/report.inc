// rcbot/report.inc: the end-to-end checks, the bots' status lines and the frame profiler.
// Owner: core. Ports host lib.rs State::check / report (the e2e markers and the 5 s status line
// per bot) for `cargo xtask e2e` (xtask/src/e2e.rs).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// With the cvar rcbot_e2e 1 (or localinfo rcbot_e2e 1, read in plugin_init, for a server.cfg
// that runs before the plugin registers the cvar) the plugin logs to
// addons/amxmodx/logs/rcbot_amxx_e2e.log:
//   start: map=<map> phase=<n>           the plugin loaded on a map (phase 1 the first map of
//                                        the server's life, 2 every later one)
//   CHECK <name> PASS|FAIL|SKIP <detail> once per check (SKIP: a soft check not seen; the reason)
//   kill "<killer>" killed "<victim>"    a kill by one of our bots
//   bot #<id> "<name>" alive=... moved=... weapon=... enemy=...   every 5 s per bot
//   DONE map=<map> pass=<n> fail=<n> skip=<n>   when the phase's checks are done, or after
//                                        RC_E2E_TIMEOUT
// Phase 1 runs every check but rejoined and kungfu; phase 2 (after a changelevel) the backend, the bots
// back (adopted or added again), the waypoints, runcmd and moved, and kung fu (soft; the second
// map's config turns ts_kungfu on, and the bots must meet).
// Nothing here formats a string per frame outside e2e mode.
//
// With rcbot_profile <seconds> (0 off) the plugin times ts_frame with the rcbot_nav module's
// microsecond clock (nav_clock_us) and logs to rcbot_amxx_profile.log every <seconds>: frames,
// the average and the worst frame, and the parts (the world refresh, think, act).

#if defined _rcbot_report_included
	#endinput
#endif
#define _rcbot_report_included

#define RC_E2E_LOG			"rcbot_amxx_e2e.log"
#define RC_PROFILE_LOG		"rcbot_amxx_profile.log"
#define RC_E2E_TIMEOUT		240.0
// After this long (from the start) a phase whose required checks passed ends; its soft checks
// not seen yet are SKIPs.
#define RC_E2E_SOFT_WAIT	150.0
// A bot must get this far (2D) from where a life started for "moved".
#define RC_E2E_MOVED		256.0
// Bots that must have spawned for "spawned" (xtask adds 4).
#define RC_E2E_BOTS			4
// Waypoints one bot must touch in a life for "waypoints" (host report: touched_waypoints >= 3).
#define RC_E2E_WAYPOINTS	5
// The view must turn this far (yaw, degrees) from where a life started for "turned" (host report).
#define RC_E2E_TURNED		30.0
#define RC_E2E_PHASE_KEY	"rcbot_e2e_phase"

enum
{
	RC_CHECK_BACKEND = 0,	// ts_bot_activate came
	RC_CHECK_ADD_QUOTA,		// a bot made through ts_bot_add (addbot)
	RC_CHECK_ADD_RCBOT,		// a bot made through rcbot addbot
	RC_CHECK_SPAWNED,		// RC_E2E_BOTS of our bots spawned
	RC_CHECK_RUNCMD,		// ts_bot_runcmd returned TSBOT_OK
	RC_CHECK_MOVED,			// a bot moved RC_E2E_MOVED units in one life
	RC_CHECK_FIRED,			// one of our bots fired (ts_weapon_fired)
	RC_CHECK_WPT_LOADED,	// nav_load found the map's waypoints
	RC_CHECK_PATH_FOUND,	// an A* search (nav_path_find) returned NAV_PATH_FOUND
	RC_CHECK_WAYPOINTS,		// a bot touched RC_E2E_WAYPOINTS waypoints in one life
	RC_CHECK_WEAPON_SWITCH,	// a living bot's weapon changed (or ts_bot_select_weapon took it)
	RC_CHECK_TURNED,		// a living bot's view (the engine's v_angle) turned RC_E2E_TURNED
	RC_CHECK_KILL,			// one of our bots killed another player
	RC_CHECK_KUNGFU,		// a bot fought with kung fu (kungfu_fight)
	RC_CHECK_CHAT,			// a bot said a line (an Action::Command say)
	RC_CHECK_HAL,			// a MegaHAL reply (hal_bot_reply)
	RC_CHECK_REJOINED,		// on a later map, a bot came back: adopted (ts_bot_adopt, engines that keep
							// bots) or added again (rcbot's rejoin code, the port's quota)
	RC_CHECKS
};

stock const g_check_names[RC_CHECKS][] = {"backend", "add_quota", "add_rcbot", "spawned", "runcmd", "moved", "fired",
	"waypoints_loaded", "path_found", "waypoints", "weapon_switch", "turned", "kill", "kungfu", "chat", "hal", "rejoined"};

// Which phase runs a check: bit 0 phase 1 (the first map), bit 1 phase 2 (later maps); RC_SOFT: a
// SKIP rather than a FAIL when not seen (it depends on the bots meeting).
#define RC_P1		1
#define RC_P2		2
#define RC_SOFT		4
stock const g_check_flags[RC_CHECKS] = {
	RC_P1 | RC_P2, RC_P1, RC_P1, RC_P1, RC_P1 | RC_P2, RC_P1 | RC_P2, RC_P1,
	RC_P1 | RC_P2, RC_P1, RC_P1, RC_P1, RC_P1, RC_P1, RC_P2 | RC_SOFT, RC_P1, RC_P1 | RC_SOFT, RC_P2};

new bool:g_e2e;
new g_e2e_phase;
new g_pcvar_e2e;
new bool:g_check_done[RC_CHECKS];
new bool:g_e2e_finished;
new Float:g_e2e_start;
new Float:g_next_report;
new g_spawned_bits;				// our bots seen spawning (bit per player index)
new g_runcmd_fail;
new Float:g_life_start[RC_SLOTS][3];
new Float:g_life_start_yaw[RC_SLOTS];
new g_life_weapon[RC_SLOTS];
new bool:g_life_start_set[RC_SLOTS];

// The profiler (rcbot_profile): the frame's start and its parts, in nav_clock_us microseconds.
new g_pcvar_profile;
new bool:g_prof_on;
new g_prof_t0;
new g_prof_frames;
new g_prof_sum;
new g_prof_max;
new g_prof_part[3];
new g_prof_mark[3];
new Float:g_prof_next;

stock report_init()
{
	g_pcvar_e2e = register_cvar("rcbot_e2e", "0");
	g_pcvar_profile = register_cvar("rcbot_profile", "0");
	report_refresh_mode();
}

// plugin_init and plugin_cfg: the localinfo key or the cvar (a config may set it after
// plugin_init).
stock report_refresh_mode()
{
	new li[8];
	get_localinfo("rcbot_e2e", li, charsmax(li));
	if (li[0] == '1')
	{
		set_pcvar_num(g_pcvar_e2e, 1);
	}
	new bool:on = get_pcvar_num(g_pcvar_e2e) != 0;
	if (on && !g_e2e)
	{
		// The phase: maps this server ran with e2e on (localinfo outlives the plugin's reloads).
		get_localinfo(RC_E2E_PHASE_KEY, li, charsmax(li));
		g_e2e_phase = str_to_num(li) + 1;
		num_to_str(g_e2e_phase, li, charsmax(li));
		set_localinfo(RC_E2E_PHASE_KEY, li);
		g_e2e_start = get_gametime();
		log_to_file(RC_E2E_LOG, "start: map=%s phase=%d maxplayers=%d", g_map, g_e2e_phase, g_maxclients);
	}
	g_e2e = on;
}

// The check belongs to this phase.
static bool:report_wanted(check)
{
	return (g_check_flags[check] & (g_e2e_phase > 1 ? RC_P2 : RC_P1)) != 0;
}

stock report_check_pass(check, const detail[])
{
	if (g_check_done[check])
	{
		return;
	}
	g_check_done[check] = true;
	if (g_e2e && !g_e2e_finished && report_wanted(check))
	{
		log_to_file(RC_E2E_LOG, "CHECK %s PASS %s", g_check_names[check], detail);
	}
}

// A data file the Pawn bot reads itself (.rkf, .rcr) and what came of it (e2e only: the file
// compatibility run, xtask compat, looks for these lines).
stock report_file(const path[], const what[])
{
	if (g_e2e)
	{
		log_to_file(RC_E2E_LOG, "file %s %s", path, what);
	}
}

// ts_bot_activate came.
stock report_backend_active()
{
	report_check_pass(RC_CHECK_BACKEND, "");
}

stock report_bot_added(id, how)
{
	if (how == RC_ADDED_QUOTA)
	{
		report_check_pass(RC_CHECK_ADD_QUOTA, "");
	}
	else if (how == RC_ADDED_RCBOT)
	{
		report_check_pass(RC_CHECK_ADD_RCBOT, "");
	}
	if (g_e2e && g_e2e_phase > 1 && !g_check_done[RC_CHECK_REJOINED])
	{
		new detail[64];
		formatex(detail, charsmax(detail), "^"%s^" %s, profile %d", g_bot_name[id],
			how == RC_ADDED_ADOPT ? "adopted" : (how == RC_ADDED_RCBOT ? "rcbot" : "quota"), g_pr_i[id][PR_ID]);
		report_check_pass(RC_CHECK_REJOINED, detail);
	}
}

// navigate_load_map: the map's waypoints.
stock report_waypoints_loaded(const map[], count)
{
	if (count > 0 && g_e2e && !g_check_done[RC_CHECK_WPT_LOADED])
	{
		new detail[96];
		formatex(detail, charsmax(detail), "%d waypoints for %s", count, map);
		report_check_pass(RC_CHECK_WPT_LOADED, detail);
	}
}

// navigate_search_path: an A* search ended with a path.
stock report_path_found(id, from, to)
{
	if (g_e2e && !g_check_done[RC_CHECK_PATH_FOUND])
	{
		new detail[48];
		formatex(detail, charsmax(detail), "bot #%d %d -> %d", id, from, to);
		report_check_pass(RC_CHECK_PATH_FOUND, detail);
	}
}

// ts_player_killed: a kill by one of our bots (the host logs `kill "<killer>" killed "<victim>"`).
stock report_kill(victim, attacker)
{
	if (!g_e2e || !is_rcbot(attacker) || attacker == victim || victim < 1 || victim > MAX_PLAYERS)
	{
		return;
	}
	new vname[RC_NAME_LEN];
	get_user_name(victim, vname, charsmax(vname));
	log_to_file(RC_E2E_LOG, "kill ^"%s^" killed ^"%s^"", g_bot_name[attacker], vname);
	if (!g_check_done[RC_CHECK_KILL])
	{
		new detail[80];
		formatex(detail, charsmax(detail), "#%d killed #%d", attacker, victim);
		report_check_pass(RC_CHECK_KILL, detail);
	}
}

// think's kungfu_fight: a bot fought with kung fu.
stock report_kungfu(id)
{
	if (g_e2e && !g_check_done[RC_CHECK_KUNGFU])
	{
		new detail[16];
		formatex(detail, charsmax(detail), "bot #%d", id);
		report_check_pass(RC_CHECK_KUNGFU, detail);
	}
}

// manager's Action::Command: a bot said a line.
stock report_chat(id, const line[])
{
	if (g_e2e && !g_check_done[RC_CHECK_CHAT])
	{
		new detail[RC_ACTION_TEXT + 16];
		formatex(detail, charsmax(detail), "#%d %s", id, line);
		report_check_pass(RC_CHECK_CHAT, detail);
	}
}

// chat_reply_to_message: MegaHAL replied.
stock report_hal(id, const reply[])
{
	if (g_e2e && !g_check_done[RC_CHECK_HAL])
	{
		new detail[RC_ACTION_TEXT + 16];
		formatex(detail, charsmax(detail), "#%d %s", id, reply);
		report_check_pass(RC_CHECK_HAL, detail);
	}
}

stock report_spawned(id)
{
	if (!is_rcbot(id))
	{
		return;
	}
	g_life_start_set[id] = false;
	g_spawned_bits |= 1 << (id - 1);
	new n = 0;
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (g_spawned_bits & (1 << (b - 1)))
		{
			n++;
		}
	}
	if (n >= RC_E2E_BOTS)
	{
		report_check_pass(RC_CHECK_SPAWNED, "");
	}
}

stock report_fired(id)
{
	if (is_rcbot(id))
	{
		report_check_pass(RC_CHECK_FIRED, "");
	}
}

stock report_runcmd(id, r)
{
	#pragma unused id
	if (r == TSBOT_OK)
	{
		report_check_pass(RC_CHECK_RUNCMD, "");
	}
	else
	{
		g_runcmd_fail++;
	}
}

// lib.rs report: how far bots got (moved, waypoints, weapon switch, turned), a status line per
// bot every 5 s, DONE at the end (e2e only).
stock report_frame()
{
	if (!g_e2e || g_e2e_finished)
	{
		return;
	}
	new Float:origin[3], Float:vang[3];
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (!g_co_i[id][CO_USED])
		{
			continue;
		}
		if (!bot_alive(id))
		{
			g_life_start_set[id] = false;
			continue;
		}
		world_origin(id, origin);
		// The view as the engine has it (pev->v_angle), not as the bot wants it.
		world_vangle(id, vang);
		new weapon = world_weapon(id);
		if (!g_life_start_set[id])
		{
			g_life_start_set[id] = true;
			vec_copy(origin, g_life_start[id]);
			g_life_start_yaw[id] = vang[1];
			g_life_weapon[id] = weapon;
			continue;
		}
		if (vec_distance_2d(origin, g_life_start[id]) > RC_E2E_MOVED)
		{
			report_check_pass(RC_CHECK_MOVED, "");
		}
		new Float:d = floatabs(vang[1] - g_life_start_yaw[id]);
		while (d >= 360.0)
		{
			d -= 360.0;
		}
		if (floatmin(d, 360.0 - d) > RC_E2E_TURNED && !g_check_done[RC_CHECK_TURNED])
		{
			new detail[32];
			formatex(detail, charsmax(detail), "bot #%d %.0f deg", id, floatmin(d, 360.0 - d));
			report_check_pass(RC_CHECK_TURNED, detail);
		}
		if (weapon != g_life_weapon[id])
		{
			if (!g_check_done[RC_CHECK_WEAPON_SWITCH])
			{
				new detail[32];
				formatex(detail, charsmax(detail), "bot #%d %d -> %d", id, g_life_weapon[id], weapon);
				report_check_pass(RC_CHECK_WEAPON_SWITCH, detail);
			}
			g_life_weapon[id] = weapon;
		}
		if (g_nv_i[id][NV_TOUCHED_WPTS] >= RC_E2E_WAYPOINTS && !g_check_done[RC_CHECK_WAYPOINTS])
		{
			new detail[32];
			formatex(detail, charsmax(detail), "bot #%d touched %d", id, g_nv_i[id][NV_TOUCHED_WPTS]);
			report_check_pass(RC_CHECK_WAYPOINTS, detail);
		}
	}
	new Float:now = g_now;
	if (now >= g_next_report)
	{
		g_next_report = now + 5.0;
		for (new id = 1; id <= MAX_PLAYERS; id++)
		{
			if (!g_co_i[id][CO_USED])
			{
				continue;
			}
			world_origin(id, origin);
			log_to_file(RC_E2E_LOG,
				"bot #%d ^"%s^" alive=%d health=%.0f weapon=%d clip=%d enemy=%d wpt=%d touched=%d moved=%.0f task=%d runcmd_fail=%d",
				id, g_bot_name[id], bot_alive(id), world_health(id), world_weapon(id), world_clip(id), g_cb_i[id][CB_ENEMY],
				g_nv_i[id][NV_CURRENT_WPT], g_nv_i[id][NV_TOUCHED_WPTS],
				g_life_start_set[id] ? vec_distance_2d(origin, g_life_start[id]) : 0.0, g_tk_count[id] > 0 ? g_tk_i[id][0][TK_KIND] : -1,
				g_runcmd_fail);
		}
	}
	new pass = 0, wanted = 0, hard_left = 0;
	for (new c = 0; c < RC_CHECKS; c++)
	{
		if (!report_wanted(c))
		{
			continue;
		}
		wanted++;
		if (g_check_done[c])
		{
			pass++;
		}
		else if (!(g_check_flags[c] & RC_SOFT))
		{
			hard_left++;
		}
	}
	new Float:t = get_gametime() - g_e2e_start;
	if (pass == wanted || (hard_left == 0 && t > RC_E2E_SOFT_WAIT) || t > RC_E2E_TIMEOUT)
	{
		new fail = 0, skip = 0;
		for (new c = 0; c < RC_CHECKS; c++)
		{
			if (!report_wanted(c) || g_check_done[c])
			{
				continue;
			}
			if (g_check_flags[c] & RC_SOFT)
			{
				skip++;
				log_to_file(RC_E2E_LOG, "CHECK %s SKIP not seen within %.0f s (the bots must meet for it)", g_check_names[c], t);
			}
			else
			{
				fail++;
				log_to_file(RC_E2E_LOG, "CHECK %s FAIL not seen within %.0f s", g_check_names[c], t);
			}
		}
		log_to_file(RC_E2E_LOG, "DONE map=%s pass=%d fail=%d skip=%d", g_map, pass, fail, skip);
		g_e2e_finished = true;
	}
}

// ---- the profiler (rcbot_profile) ------------------------------------------------------------------

// manager_frame's start.
stock report_profile_begin()
{
	g_prof_on = get_pcvar_num(g_pcvar_profile) > 0;
	if (g_prof_on)
	{
		g_prof_t0 = nav_clock_us();
		arrayset(g_prof_mark, 0, sizeof(g_prof_mark));
	}
}

// The end of a part of the frame (0 the world refresh, 1 think, 2 act): the time since the
// frame's start (report_profile_end turns the marks into the parts' own times).
stock report_profile_mark(part)
{
	if (g_prof_on)
	{
		g_prof_mark[part] = nav_clock_us() - g_prof_t0;
	}
}

// manager_frame's end: the frame's time, and every few seconds the averages.
stock report_profile_end()
{
	if (!g_prof_on)
	{
		return;
	}
	new t = nav_clock_us() - g_prof_t0;
	g_prof_frames++;
	g_prof_sum += t;
	if (t > g_prof_max)
	{
		g_prof_max = t;
	}
	new prev = 0;
	for (new k = 0; k < 3; k++)
	{
		if (g_prof_mark[k] >= prev)
		{
			g_prof_part[k] += g_prof_mark[k] - prev;
			prev = g_prof_mark[k];
		}
		g_prof_mark[k] = prev;
	}
	new Float:now = get_gametime();
	if (now < g_prof_next)
	{
		return;
	}
	new bots = 0;
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (g_co_i[id][CO_USED])
		{
			bots++;
		}
	}
	if (g_prof_frames > 0 && g_prof_next > 0.0)
	{
		log_to_file(RC_PROFILE_LOG, "profile: map=%s bots=%d frames=%d avg_us=%d max_us=%d refresh_us=%d think_us=%d act_us=%d", g_map,
			bots, g_prof_frames, g_prof_sum / g_prof_frames, g_prof_max, g_prof_part[0] / g_prof_frames, g_prof_part[1] / g_prof_frames,
			g_prof_part[2] / g_prof_frames);
	}
	g_prof_frames = 0;
	g_prof_sum = 0;
	g_prof_max = 0;
	arrayset(g_prof_part, 0, sizeof(g_prof_part));
	g_prof_next = now + float(get_pcvar_num(g_pcvar_profile));
}
