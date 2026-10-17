// rcbot_amxx: RCBot (Paul "Cheeseh" Murphy's Half-Life bot) for The Specialists 3.0 on the Rust
// port, as an AMX Mod X plugin: the port's bot backend rcbot_amxx (ts_bot_backend rcbot_amxx).
// The bot logic is Pawn, ported from tsmod-rcbot-rs's rcbot crate (bot/, config/, weapons/) and
// host crate; MegaHAL, learning and navigation are the rcbot_megahal, rcbot_learn and rcbot_nav
// modules. Layout and conventions: docs/pawn.md; the Rust -> Pawn map: docs/design.md.
// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot linking exceptions.
//
// This file only wires AMX Mod X and tsx's forwards to the subsystems in scripting/rcbot/; it
// holds no bot logic.

#include <amxmodx>
#include <engine>
#include <fakemeta>
#include <tsconst>
#include <tsfun>
#include <tsbot>
#include <rcbot_megahal>
#include <rcbot_learn>
#include <rcbot_nav>

// Big per-bot tables live in the data segment; the heap and stack only hold locals.
#pragma dynamic 32768

// Core (owner: core).
#include "rcbot/consts.inc"
#include "rcbot/math.inc"
#include "rcbot/config.inc"
#include "rcbot/state.inc"
#include "rcbot/util.inc"
#include "rcbot/world.inc"
#include "rcbot/files.inc"
#include "rcbot/report.inc"
#include "rcbot/backend.inc"
#include "rcbot/manager.inc"

// Subsystems (owners: docs/pawn.md "File ownership").
#include "rcbot/tsweapons.inc"
#include "rcbot/weapons.inc"
#include "rcbot/botweapons.inc"
#include "rcbot/profile.inc"
#include "rcbot/rep.inc"
#include "rcbot/users.inc"
#include "rcbot/client.inc"
#include "rcbot/menu.inc"
#include "rcbot/commands.inc"
#include "rcbot/look.inc"
#include "rcbot/preaim.inc"
#include "rcbot/combat.inc"
#include "rcbot/fire.inc"
#include "rcbot/movement.inc"
#include "rcbot/navigate.inc"
#include "rcbot/remember.inc"
#include "rcbot/hearing.inc"
#include "rcbot/items.inc"
#include "rcbot/board.inc"
#include "rcbot/kungfu.inc"
#include "rcbot/learning.inc"
#include "rcbot/squad.inc"
#include "rcbot/botcam.inc"
#include "rcbot/events.inc"
#include "rcbot/chat.inc"
// think last: it reads every subsystem's tables (board.inc's, the remembered and heard lists).
#include "rcbot/tasks.inc"
#include "rcbot/goals.inc"
#include "rcbot/think.inc"
#include "rcbot/special.inc"

// ---- AMX Mod X life cycle ----------------------------------------------------------------------

public plugin_init()
{
	register_plugin(RC_PLUGIN, RC_VERSION, RC_AUTHOR);
	register_cvar("rcbot_amxx_ver", RC_VERSION, FCVAR_SERVER | FCVAR_SPONLY);
	config_defaults();
	new map[64];
	get_mapname(map, charsmax(map));
	world_init(map);
	// GameDLLInit's part: the backend (every map), the data folders, then the files.
	backend_register();
	files_init();
	report_init();
	tsweapons_init();
	weapons_read_presets();
	learning_load_globals();
	chat_init();
	commands_init();
	register_clcmd("say", "on_say");
	register_clcmd("say_team", "on_say_team");
}

public plugin_precache()
{
	navigate_precache();
	clients_precache();
}

public plugin_cfg()
{
	// bot_config.ini runs as rcbot commands, after the configs set cvars (rcbot_e2e).
	report_refresh_mode();
	users_read();
	config_read();
	config_read_map_config();
	navigate_load_map();
}

// ServerDeactivate (lib.rs server_deactivate): the bots and the learned data saved for the next
// map; squads and the camera forgotten.
public plugin_end()
{
	backend_save_all();
	navigate_save_map();
	squad_clear();
	botcam_clear();
	board_clear();
	world_end();
}

public client_putinserver(id)
{
	backend_put_in_server(id);
}

public client_disconnected(id)
{
	navigate_editor_player_left(id);
	backend_disconnect(id);
}

public on_say(id)
{
	return chat_say_command(id, false);
}

public on_say_team(id)
{
	return chat_say_command(id, true);
}

// ---- tsx's game forwards (tsbot.inc) ---------------------------------------------------------------

public ts_frame()
{
	manager_frame();
}

public ts_player_killed(victim, attacker, inflictor)
{
	#pragma unused inflictor
	manager_death(attacker, victim);
	learning_player_killed(victim, attacker);
}

public ts_player_spawned(id)
{
	report_spawned(id);
}

public ts_damage(victim, attacker, inflictor, Float:damage, bits, Float:health, Float:armor, hitgroup, wid, Float:dir[3], Float:pos[3])
{
	#pragma unused bits, health, armor, pos
	// lib.rs damage: the world's and self-inflicted damage stop here.
	if (attacker <= 0 || attacker == victim || victim <= 0)
	{
		return;
	}
	manager_damage(victim, attacker, inflictor, damage, hitgroup, wid, dir);
}

public ts_sound(ent, Float:origin[3], const sample[], Float:vol, Float:attn, channel, source_player)
{
	#pragma unused attn, channel, source_player
	manager_sound(ent, origin, sample, vol);
}

public ts_bot_touch(bot, other)
{
	if (bot <= 0 || other <= 0 || bot == other)
	{
		return;
	}
	manager_touch(bot, other);
}

public ts_weapon_fired(id, wid, Float:origin[3], Float:dir[3], silenced)
{
	#pragma unused wid, dir
	report_fired(id);
	manager_weapon_fired(id, origin, silenced != 0);
}

public ts_item_pickup(id, ent, kind, itemid)
{
	#pragma unused kind, itemid
	if (id <= 0 || ent <= 0)
	{
		return;
	}
	manager_item_pickup(id, ent);
}

// tsfun's client_stunt: a player started a stunt (the opponent model).
public client_stunt(id, stunttype)
{
	#pragma unused stunttype
	learning_player_stunt(id);
}
