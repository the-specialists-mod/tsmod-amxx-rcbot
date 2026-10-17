// rcbot/client.inc: per-player state RCBot keeps (CClient / CClients) and the waypoint editor's
// per-frame part.
// Owner: social. Ports config/client.rs (Client is_used / pass / set_pass / has_cmd / toggle_cmd /
// set_auto_waypoint / has_tooltip_sent, Clients connect / recheck_auth / disconnect), config/rep.rs
// player_rep_id (bot_playerids.ini), host bots.rs put_in_server / disconnect's client part and
// State::rep_ids, lib.rs precache (the camera's model, the fireball; the beam sprite's index).
// The waypoint editor's frame and its events (host editor.rs) are navigate.inc's: the client's
// waypoint_on / waypoint_paths_on / auto_waypoint / debug_auto_waypoint are mirrored there with
// navigate_editor_set (clients_editor_sync).
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

// lib.rs precache: m_iWaypointTexture (util explo's beam) and m_sModelIndexFireball.
new g_beam_sprite;
new g_fireball_sprite;
// State::rep_ids: each player's reputation id by index, NO_CLIENT without a CClient (a mirror of
// g_cl_i[CL_REP_ID] for the files included before state.inc's readers; rep_lookup is the API).
stock g_client_rep_id[RC_SLOTS] = {NO_CLIENT, ...};

// models/mechgibs.mdl precached (the bot camera may set it).
new bool:g_botcam_model_ok;

// lib.rs precache (plugin_precache): the bot camera's model, the fireball (util explo), and the
// beam sprite's index (navigate_precache precaches it too; util explo's beam uses it).
stock clients_precache()
{
	g_beam_sprite = precache_model("sprites/lgtning.spr");
	precache_model("models/mechgibs.mdl");
	g_botcam_model_ok = true;
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
	vec_zero(g_cl_teleport[player]);
}

// The client's editor switches to navigate.inc (host editor.rs reads them from the client).
stock clients_editor_sync(player)
{
	navigate_editor_set(player, g_cl_i[player][CL_WAYPOINT_ON] != 0, g_cl_i[player][CL_PATHS_ON] != 0, g_cl_i[player][CL_AUTO_WPT] != 0,
		g_cl_i[player][CL_DEBUG_AUTO_WPT] != 0);
}

// Client::current_waypoint (m_iCurrentWaypoint): the editor's display writes it once a second
// while waypoints are shown (navigate.inc keeps that one), the waypoint menu command when it
// opens; the newer of the two, as far as can be told: the display's when it has one.
stock clients_current_waypoint(player)
{
	new cur = g_cl_i[player][CL_WAYPOINT_ON] ? navigate_editor_current_waypoint(player) : -1;
	return cur != -1 ? cur : g_cl_i[player][CL_CURRENT_WPT];
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
	g_client_rep_id[player] = rep_id;
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
	// navigate_editor_player_left (rcbot_amxx.sma) drops the editor's side.
	clients_init_slot(player);
	g_client_rep_id[player] = NO_CLIENT;
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
	clients_editor_sync(player);
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
