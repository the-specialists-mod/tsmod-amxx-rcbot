// perf_alive: what `cargo xtask perf-compare` reads over rcon while it measures a bot backend.
//
// Part of tsmod-amxx-rcbot (RCBot by Paul "Cheeseh" Murphy, hosted by AMX Mod X).
// GPL-3.0-or-later, with the AMX Mod X and RCBot exceptions.
//
// The server command `perf_alive` prints one line with running totals (the xtask takes the
// differences): the bots connected and alive, the deaths (DeathMsg) and the server frames
// (engine's server_frame) since the map started, and the game time. It is loaded the same way
// for every backend (native, rcbot, rcbot_amxx), so its cost (a counter per frame and per death)
// is the same in every run.
#include <amxmodx>
#include <engine>

new g_frames;
new g_deaths;

public plugin_init()
{
	register_plugin("perf_alive", "1.0", "tsmod-amxx-rcbot");
	register_srvcmd("perf_alive", "cmd_alive");
	register_event("DeathMsg", "ev_death", "a");
}

public server_frame()
{
	g_frames++;
}

public ev_death()
{
	g_deaths++;
}

public cmd_alive()
{
	new players[32], bots, alive;
	// "d": no human clients, "h": no HLTV; "a": no dead clients.
	get_players(players, bots, "dh");
	get_players(players, alive, "adh");
	server_print("perf_alive: bots=%d alive=%d deaths=%d frames=%d time=%.3f", bots, alive, g_deaths, g_frames, get_gametime());
	return PLUGIN_HANDLED;
}
