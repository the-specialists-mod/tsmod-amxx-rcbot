// rcbot/report.inc: the end-to-end checks and the bots' status lines.
// Owner: core. Ports host lib.rs State::check / report (the e2e markers and the 5 s status line
// per bot) for `cargo xtask e2e` (xtask/src/main.rs).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// With the cvar rcbot_e2e 1 (or localinfo rcbot_e2e 1, read in plugin_init, for a server.cfg
// that runs before the plugin registers the cvar) the plugin logs to
// addons/amxmodx/logs/rcbot_amxx_e2e.log:
//   CHECK <name> PASS|FAIL <detail>      once per check
//   bot #<id> "<name>" alive=... moved=... weapon=... enemy=...   every 5 s per bot
//   DONE pass=<n> fail=<n>               when every check passed, or RC_E2E_TIMEOUT after start
// Nothing here formats a string per frame outside e2e mode.

#if defined _rcbot_report_included
	#endinput
#endif
#define _rcbot_report_included

#define RC_E2E_LOG			"rcbot_amxx_e2e.log"
#define RC_E2E_TIMEOUT		240.0
// A bot must get this far (2D) from where a life started for "moved".
#define RC_E2E_MOVED		256.0
// Bots that must have spawned for "spawned" (xtask adds 4).
#define RC_E2E_BOTS			4

enum
{
	RC_CHECK_BACKEND = 0,	// ts_bot_activate came
	RC_CHECK_ADD_QUOTA,		// a bot made through ts_bot_add (addbot)
	RC_CHECK_ADD_RCBOT,		// a bot made through rcbot addbot
	RC_CHECK_SPAWNED,		// RC_E2E_BOTS of our bots spawned
	RC_CHECK_RUNCMD,		// ts_bot_runcmd returned TSBOT_OK
	RC_CHECK_MOVED,			// a bot moved RC_E2E_MOVED units in one life
	RC_CHECK_FIRED,			// one of our bots fired (ts_weapon_fired)
	RC_CHECKS
};

stock const g_check_names[RC_CHECKS][] = {"backend", "add_quota", "add_rcbot", "spawned", "runcmd", "moved", "fired"};

new bool:g_e2e;
new g_pcvar_e2e;
new bool:g_check_done[RC_CHECKS];
new bool:g_e2e_finished;
new Float:g_e2e_deadline;
new Float:g_next_report;
new g_spawned_bits;				// our bots seen spawning (bit per player index)
new g_runcmd_fail;
new Float:g_life_start[RC_SLOTS][3];
new bool:g_life_start_set[RC_SLOTS];

stock report_init()
{
	g_pcvar_e2e = register_cvar("rcbot_e2e", "0");
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
		g_e2e_deadline = get_gametime() + RC_E2E_TIMEOUT;
		log_to_file(RC_E2E_LOG, "start: map=%s maxplayers=%d", g_map, g_maxclients);
	}
	g_e2e = on;
}

stock report_check_pass(check, const detail[])
{
	if (g_check_done[check])
	{
		return;
	}
	g_check_done[check] = true;
	if (g_e2e)
	{
		log_to_file(RC_E2E_LOG, "CHECK %s PASS %s", g_check_names[check], detail);
	}
}

// ts_bot_activate came.
stock report_backend_active()
{
	report_check_pass(RC_CHECK_BACKEND, "");
}

stock report_bot_added(id, how)
{
	#pragma unused id
	if (how == RC_ADDED_QUOTA)
	{
		report_check_pass(RC_CHECK_ADD_QUOTA, "");
	}
	else if (how == RC_ADDED_RCBOT)
	{
		report_check_pass(RC_CHECK_ADD_RCBOT, "");
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

// lib.rs report: how far bots got, a status line per bot every 5 s, DONE at the end (e2e only).
stock report_frame()
{
	if (!g_e2e || g_e2e_finished)
	{
		return;
	}
	new Float:origin[3];
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (!g_co_i[id][CO_USED] || !bot_alive(id))
		{
			continue;
		}
		world_origin(id, origin);
		if (!g_life_start_set[id])
		{
			g_life_start_set[id] = true;
			vec_copy(origin, g_life_start[id]);
		}
		else if (vec_distance_2d(origin, g_life_start[id]) > RC_E2E_MOVED)
		{
			report_check_pass(RC_CHECK_MOVED, "");
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
			log_to_file(RC_E2E_LOG, "bot #%d ^"%s^" alive=%d health=%.0f weapon=%d clip=%d enemy=%d moved=%.0f runcmd_fail=%d",
				id, g_bot_name[id], bot_alive(id), world_health(id), world_weapon(id), world_clip(id), g_cb_i[id][CB_ENEMY],
				g_life_start_set[id] ? vec_distance_2d(origin, g_life_start[id]) : 0.0, g_runcmd_fail);
		}
	}
	new pass = 0;
	for (new c = 0; c < RC_CHECKS; c++)
	{
		if (g_check_done[c])
		{
			pass++;
		}
	}
	if (pass == RC_CHECKS || get_gametime() > g_e2e_deadline)
	{
		for (new c = 0; c < RC_CHECKS; c++)
		{
			if (!g_check_done[c])
			{
				log_to_file(RC_E2E_LOG, "CHECK %s FAIL not seen within %.0f s", g_check_names[c], RC_E2E_TIMEOUT);
			}
		}
		log_to_file(RC_E2E_LOG, "DONE pass=%d fail=%d", pass, RC_CHECKS - pass);
		g_e2e_finished = true;
	}
}
