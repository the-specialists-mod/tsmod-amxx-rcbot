// rcbot/client.inc: per-player state RCBot keeps (CClient / CClients) and the waypoint editor's
// per-frame part.
// Owner: social. Ports config/client.rs (Client is_used / pass / set_pass / has_cmd / toggle_cmd /
// set_auto_waypoint / has_tooltip_sent, Clients connect / recheck_auth / disconnect), config/rep.rs
// player_rep_id (bot_playerids.ini), host bots.rs put_in_server / disconnect's client part and
// State::rep_ids, host editor.rs (frame, send: the beams as TE_BEAMPOINTS, the sounds, the
// messages from rcbot_nav's nav_event_pop) and lib.rs precache (the beam sprite, the camera's
// model, the fireball).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// The state is state.inc's client section (g_cl_*), by player index (CClients' slot is
// ENTINDEX - 1). Clients::index_of is the player index itself here.

#if defined _rcbot_client_included
	#endinput
#endif
#define _rcbot_client_included

// client.rs CLIENT_CMD_*.
#define CLIENT_CMD_PROJ_REPEL	1
#define CLIENT_CMD_GREN_DEACT	2

// client.rs ToolTip (eToolTip): bits of CL_TOOLTIPS_SENT.
enum
{
	TOOLTIP_SERVER_WELCOME = 0,
	TOOLTIP_CLIENT_WELCOME,
	TOOLTIP_SQUAD_HELP,
	TOOLTIP_ADMIN_HELP,
	TOOLTIP_CPU_HELP,
	TOOLTIP_NO_WAYPOINTS,
	TOOLTIP_CREATE_SQUAD,
	TOOLTIP_COMMANDER_WELCOME,
	TOOLTIP_COMMANDER_MARINE_ORDER,
	TOOLTIP_COMMANDER_SQUAD,
	TOOLTIP_COMMANDER_SQUAD2,
	TOOLTIP_COMMANDER_HEALTH,
	TOOLTIP_BOTCAM_HELP,
	TOOLTIP_BOTCAM_ON
};

// rep.rs BOT_PLAYER_ID_FILE_HEADER.
#define BOT_PLAYER_ID_FILE_HEADER	"# Automatically generated, do not edit.^n# however this file is safe to delete^n# but will be re-generated.^n"

// lib.rs precache: m_iWaypointTexture (the beams) and m_sModelIndexFireball (util explo).
new g_beam_sprite;
new g_fireball_sprite;

// lib.rs precache (plugin_precache): the beam sprite, the bot camera's model, the fireball.
stock clients_precache()
{
	g_beam_sprite = precache_model("sprites/lgtning.spr");
	precache_model("models/mechgibs.mdl");
	g_fireball_sprite = precache_model("sprites/zerogxplode.spr");
}

// CClient::Init: everything zero except the -1 indices.
static clients_init_slot(player)
{
	for (new Cl_I:k; k < Cl_I; k++)
	{
		g_cl_i[player][k] = 0;
	}
	g_cl_i[player][CL_REP_ID] = -1;
	g_cl_i[player][CL_MENU] = -1;
	g_cl_i[player][CL_LAST_KEY] = -1;
	g_cl_i[player][CL_CURRENT_WPT] = -1;
	g_cl_i[player][CL_DEBUG_BOT] = -1;
	g_cl_pass[player][0] = 0;
	g_cl_steam[player][0] = 0;
	g_cl_display_time[player] = 0.0;
	vec_zero(g_cl_teleport[player]);
}

// host bots.rs put_in_server + Clients::connect (CClients::ClientConnected): the player's
// reputation id (bot_playerids.ini, bots too as upstream), the access of its bot_users.ini entry
// ("You have been authenticated"), or on a listen server the first player without an entry
// becomes the host with access level 10; then CBotCam::TuneOff.
// The Steam ID is the player's auth id (upstream's setSteamID); the standalone host passes ""
// (the port's API has none), so its Steam ID entries never match.
stock clients_connect(player)
{
	if (player < 1 || player > MAX_PLAYERS)
	{
		return;
	}
	new name[RC_NAME_LEN];
	get_user_name(player, name, charsmax(name));
	new rep_id = clients_player_rep_id(name);
	clients_init_slot(player);
	g_cl_i[player][CL_USED] = 1;
	get_user_authid(player, g_cl_steam[player], charsmax(g_cl_steam[]));
	g_cl_i[player][CL_REP_ID] = rep_id;
	new k = users_get_player(player, name);
	if (k != -1)
	{
		g_cl_i[player][CL_ACCESS] = g_us_level[k];
		if (!is_user_bot(player))
		{
			client_print(player, print_console, "%sYou have been authenticated", RC_MSG_TAG);
		}
	}
	else if (!is_dedicated_server() && g_cl_listen_server == 0)
	{
		g_cl_listen_server = player;
		g_cl_i[player][CL_ACCESS] = 10;
	}
	attach_view(player, player);
}

// Clients::recheck_auth (CClient::Think's m_bRecheckAuth): look the player up again. The host
// never runs it; here it runs right after the command that asked for it (commands.inc).
stock clients_recheck_auth(player)
{
	if (!g_cl_i[player][CL_RECHECK_AUTH])
	{
		return;
	}
	g_cl_i[player][CL_RECHECK_AUTH] = 0;
	new name[RC_NAME_LEN];
	get_user_name(player, name, charsmax(name));
	new k = users_get_player(player, name);
	if (k == -1)
	{
		return;
	}
	g_cl_i[player][CL_ACCESS] = g_us_level[k];
	if (is_user_connected(player) && !is_user_bot(player))
	{
		client_print(player, print_console, "%sYou have been authenticated", RC_MSG_TAG);
	}
}

// Clients::disconnect (CClients::ClientDisconnected): the slot is reset and stops being the
// listen server's host.
stock clients_disconnect(player)
{
	if (player < 1 || player > MAX_PLAYERS)
	{
		return;
	}
	if (g_cl_i[player][CL_USED] && g_cl_listen_server == player)
	{
		g_cl_listen_server = 0;
	}
	if (g_cl_i[player][CL_AUTO_RUNNING])
	{
		nav_auto_reset(player);
	}
	clients_init_slot(player);
}

// Client::set_pass (SetPass): cut to 15 bytes; asks for an access recheck.
stock clients_set_pass(player, const pass[])
{
	copy(g_cl_pass[player], charsmax(g_cl_pass[]), pass);
	g_cl_i[player][CL_RECHECK_AUTH] = 1;
}

// Client::has_cmd (hasCmd).
stock bool:clients_has_cmd(player, cmd)
{
	return (g_cl_i[player][CL_CMD] & cmd) == cmd;
}

// Client::toggle_cmd (toggleCmd).
stock clients_toggle_cmd(player, cmd)
{
	if (clients_has_cmd(player, cmd))
	{
		g_cl_i[player][CL_CMD] &= ~cmd;
	}
	else
	{
		g_cl_i[player][CL_CMD] |= cmd;
	}
}

// Client::set_auto_waypoint (AutoWaypoint(state)): -1 toggles, 2 is on with debug beams,
// otherwise state > 0.
stock clients_set_auto_waypoint(player, state_)
{
	g_cl_i[player][CL_DEBUG_AUTO_WPT] = 0;
	g_cl_i[player][CL_SET_UP_AUTO_WPT] = 0;
	if (state_ == 2)
	{
		g_cl_i[player][CL_DEBUG_AUTO_WPT] = 1;
	}
	if (state_ == -1)
	{
		g_cl_i[player][CL_AUTO_WPT] = !g_cl_i[player][CL_AUTO_WPT];
	}
	else
	{
		g_cl_i[player][CL_AUTO_WPT] = state_ > 0;
	}
}

// Client::has_tooltip_sent (HasToolTipSent).
stock bool:clients_has_tooltip_sent(player, tip)
{
	return (g_cl_i[player][CL_TOOLTIPS_SENT] & (1 << tip)) != 0;
}

// ---- bot_playerids.ini (rep.rs player_rep_id) ------------------------------------------------

// GetPlayerRepId(szPlayerName): the number of the line holding the name in quotes, counting
// every fgets(127) chunk except those starting with "#" (so an empty line counts); the name is
// what follows the first quote up to the next (at most 64 bytes), case-sensitive. An unknown
// name is appended (a missing file is made with the header). -1 for an empty name.
stock clients_player_rep_id(const name[])
{
	if (!name[0])
	{
		return -1;
	}
	new path[RC_PATH_LEN];
	if (!cfg_open(BOT_PLAYER_ID_FILE, path, charsmax(path)))
	{
		new f = cfg_write_begin(BOT_PLAYER_ID_FILE);
		if (f)
		{
			cfg_write_text(f, BOT_PLAYER_ID_FILE_HEADER);
			clients_write_entry(f, name);
			cfg_write_end(f, BOT_PLAYER_ID_FILE);
		}
		return 0;
	}
	new b[128], id = 0, entry[65];
	while (cfg_fgets(b, 127) != -1)
	{
		if (b[0] == '#' || !b[0])
		{
			continue;
		}
		new len = strlen(b);
		if (b[len - 1] == '^n')
		{
			b[--len] = 0;
		}
		new i = 0;
		while (i < len && b[i] != '"')
		{
			i++;
		}
		if (i < len)
		{
			i++;
		}
		new o = 0;
		while (i < len && b[i] != '"' && o < 64)
		{
			entry[o++] = b[i++];
		}
		entry[o] = 0;
		if (equal(entry, name))
		{
			cfg_close();
			return id;
		}
		id++;
	}
	cfg_close();
	clients_append_entry(name);
	return id;
}

// "\"<name>\"\n".
static clients_write_entry(f, const name[])
{
	new line[RC_NAME_LEN + 4];
	formatex(line, charsmax(line), "^"%s^"^n", name);
	cfg_write_text(f, line);
}

// The file with the name appended, written atomically (a copy, then renamed over it).
static clients_append_entry(const name[])
{
	new path[RC_PATH_LEN];
	if (!cfg_open(BOT_PLAYER_ID_FILE, path, charsmax(path)))
	{
		return;
	}
	cfg_close();
	new in = fopen(path, "rb");
	if (!in)
	{
		return;
	}
	new out = cfg_write_begin(BOT_PLAYER_ID_FILE);
	if (!out)
	{
		fclose(in);
		return;
	}
	static buf[1024];
	new n;
	while ((n = fread_blocks(in, buf, sizeof(buf), BLOCK_CHAR)) > 0)
	{
		fwrite_blocks(out, buf, n, BLOCK_CHAR);
	}
	fclose(in);
	clients_write_entry(out, name);
	cfg_write_end(out, BOT_PLAYER_ID_FILE);
}

// ---- the waypoint editor (host editor.rs) ------------------------------------------------------

// editor.rs frame: for each player with waypoints on or auto-waypointing, the waypoint display
// once a second (nav_draw_around: the waypoint they stand at becomes the menu's), and the
// auto-waypointer's step; then what rcbot_nav asked for is sent to them. Every frame: the
// players without either stop at two cell reads.
stock clients_editor_frame()
{
	new Float:now = get_gametime(), Float:origin[3];
	for (new p = 1; p <= MAX_PLAYERS; p++)
	{
		if (!g_cl_i[p][CL_USED])
		{
			continue;
		}
		new bool:on = g_cl_i[p][CL_WAYPOINT_ON] != 0, bool:autow = g_cl_i[p][CL_AUTO_WPT] != 0;
		if (!on && !autow)
		{
			continue;
		}
		if (!is_user_connected(p))
		{
			continue;
		}
		pev(p, pev_origin, origin);
		if (on && g_cl_display_time[p] < now)
		{
			g_cl_display_time[p] = now + 1.0;
			new cur = nav_draw_around(p, origin, g_cl_i[p][CL_PATHS_ON] != 0);
			if (cur != -1)
			{
				g_cl_i[p][CL_CURRENT_WPT] = cur;
			}
		}
		if (autow)
		{
			g_cl_i[p][CL_AUTO_RUNNING] = 1;
			nav_auto_waypoint(p, on, g_cl_i[p][CL_DEBUG_AUTO_WPT] != 0, g_cfg_i[CFG_AUTO_PATH_WAYPOINT] != 0);
		}
		else if (g_cl_i[p][CL_AUTO_RUNNING])
		{
			g_cl_i[p][CL_AUTO_RUNNING] = 0;
			nav_auto_reset(p);
		}
		clients_send_nav_events(p);
	}
}

// editor.rs send: rcbot_nav's queued events for player: beams (WaypointDrawBeam's
// TE_BEAMPOINTS, as the host's Engine::beam), sounds (UTIL_PlaySound, not to bots) and
// messages (the console, with RCBot's tag).
stock clients_send_nav_events(player)
{
	new Float:start[3], Float:end[3], params[7], text[128];
	new NavEvent:ev;
	new bool:there = player >= 1 && player <= MAX_PLAYERS && is_user_connected(player) != 0;
	while ((ev = nav_event_pop(start, end, params, text, charsmax(text))) != NAV_EVENT_NONE)
	{
		if (!there)
		{
			continue;
		}
		switch (ev)
		{
			case NAV_EVENT_BEAM:
			{
				clients_beam(player, start, end, params[0], params[1], params[2], params[3], params[4], params[5], params[6]);
			}
			case NAV_EVENT_SOUND:
			{
				if (!is_user_bot(player))
				{
					client_cmd(player, "play %s", text);
				}
			}
			case NAV_EVENT_MESSAGE:
			{
				client_print(player, print_console, "%s%s", RC_MSG_TAG, text);
			}
		}
	}
}

// Engine::beam (WaypointDrawBeam): TE_BEAMPOINTS to one player, frame start 1, frame rate 10,
// life 1 s.
stock clients_beam(player, const Float:start[3], const Float:end[3], width, noise, r, g, b, brightness, speed)
{
	if (!g_beam_sprite)
	{
		return;
	}
	message_begin(MSG_ONE_UNRELIABLE, SVC_TEMPENTITY, _, player);
	write_byte(TE_BEAMPOINTS);
	write_coord_f(start[0]);
	write_coord_f(start[1]);
	write_coord_f(start[2]);
	write_coord_f(end[0]);
	write_coord_f(end[1]);
	write_coord_f(end[2]);
	write_short(g_beam_sprite);
	write_byte(1);
	write_byte(10);
	write_byte(10);
	write_byte(width);
	write_byte(noise);
	write_byte(r);
	write_byte(g);
	write_byte(b);
	write_byte(brightness);
	write_byte(speed);
	message_end();
}
