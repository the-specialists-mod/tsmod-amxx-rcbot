// rcbot/commands.inc: the rcbot server and client commands.
// Owner: social. Ports config/commands.rs (CommandId setup / name / access_level / lookup /
// needs_access / error / help, command_list, Run's action / add_bot / menu_select / menu_action /
// set_pass / waypoint_menu / util / users / debug_ent / debug_bot / debug / config / squad /
// path_waypoint / auto_waypoint / botcam / help_command / waypoint / waypoint_flag, run,
// client_command, server_command, run_config, ts_team_choice), host commands.rs (server_command,
// client_command, Host: CommandHost's apply / print / find_player / player_by_user_id /
// facing_entity / nearest_waypoint / toggles / add_bot, view_trace, give_ts_weapon,
// explosion_create, radius_damage), bots.rs force_grip_frame / release_force_grip /
// remove_all_bot_squads, lib.rs map_config_frame's rejoin hold.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// A command runs for a client (its player index; 0 for the server console and bot_config.ini)
// with up to four arguments (g_ca). Messages go as the host prints them: to a player's console
// or the server's with "[RCBOT] ", "talk to all" to every player's console without it.
//
// Deviations from the host: util givetsweapon is tsfun's ts_giveweapon (200 extra rounds, the
// game's own pick-up), and util explo's damage is the engine module's fakedamage (the victims
// see a trigger as the attacker; the host's TakeDamage names the caller). Clients::recheck_auth
// runs right after the command that asks for it (the host never runs it).

#if defined _rcbot_commands_included
	#endinput
#endif
#define _rcbot_commands_included

// commands.rs RCBOT_ACCESS_FORCE_GRIP.
#define RCBOT_ACCESS_FORCE_GRIP		9

// commands.rs access (BOT_COMMAND_ACCESS_*): a user's access level is a set of these bits.
#define CMD_ACCESS_ALLOWED			0
#define CMD_ACCESS_WAYPOINTING		(1<<0)
#define CMD_ACCESS_ADD_REMOVE_BOTS	(1<<1)
#define CMD_ACCESS_UTIL				(1<<2)
#define CMD_ACCESS_CONFIG			(1<<3)
#define CMD_ACCESS_DEBUG			(1<<4)
#define CMD_ACCESS_USERS			(1<<5)

// commands.rs CvarState (eBotCvarState).
enum
{
	CVAR_NOTEXIST = 0,
	CVAR_ACCESSED,
	CVAR_NEEDACCESS,
	CVAR_ERROR,
	CVAR_CONTINUE
};

// commands.rs CommandId, SetupCommands' order.
enum
{
	CMD_ADDBOT = 0,
	CMD_WAYPOINT,
	CMD_PATHWAYPOINT,
	CMD_DEBUG,
	CMD_WAYPOINT_MENU,
	CMD_BOT_MENU,
	CMD_MENUSELECT,
	CMD_CONFIG,
	CMD_SET_PASS,
	CMD_UTIL,
	CMD_SQUAD,
	CMD_SQUAD_MENU,
	CMD_BOTCAM,
	CMD_AUTOWAYPOINT,
	CMD_HELP,
	CMD_USERS,
	CMD_DEBUG_BOT,
	CMD_DEBUG_ENT,
	CMD_REMOVEBOT,
	CMD_COUNT
};

// CommandId::setup: (name, access level, can be used on a dedicated server).
stock const g_cmd_names[CMD_COUNT][] = {"addbot", "waypoint", "pathwaypoint", "debug", "waypoint_menu", "bot_menu", "menuselect",
	"config", "set_pass", "util", "squad", "squad_menu", "botcam", "autowaypoint", "help", "users", "debug_bot", "debug_ent",
	"removebot"};
stock const g_cmd_access[CMD_COUNT] = {CMD_ACCESS_ADD_REMOVE_BOTS, CMD_ACCESS_WAYPOINTING, CMD_ACCESS_WAYPOINTING, CMD_ACCESS_DEBUG,
	CMD_ACCESS_WAYPOINTING, CMD_ACCESS_ADD_REMOVE_BOTS, CMD_ACCESS_ALLOWED, CMD_ACCESS_CONFIG, CMD_ACCESS_ALLOWED, CMD_ACCESS_UTIL,
	CMD_ACCESS_ALLOWED, CMD_ACCESS_ALLOWED, CMD_ACCESS_ALLOWED, CMD_ACCESS_WAYPOINTING, CMD_ACCESS_ALLOWED, CMD_ACCESS_USERS,
	CMD_ACCESS_DEBUG, CMD_ACCESS_DEBUG, CMD_ACCESS_ADD_REMOVE_BOTS};
stock const bool:g_cmd_dedicated[CMD_COUNT] = {true, false, false, true, false, false, false, true, false, true, false, false,
	false, true, true, true, false, false, false};

#define RC_ARG_LEN		128

// The running command's arguments (arg1..arg4; "" when missing).
new g_ca[4][RC_ARG_LEN];

// lib.rs TSRS_PluginLoad's register_command(BOT_COMMAND_ACCESS): `rcbot` from the server
// console (RCBot_ServerCommand) and from clients (ClientCommand's rcbot, menuselect and
// +/-rcbot_force_grip).
stock commands_init()
{
	register_srvcmd("rcbot", "commands_server_rcbot");
	register_clcmd("rcbot", "commands_client_rcbot");
	register_clcmd("menuselect", "commands_client_menuselect");
	register_clcmd("+rcbot_force_grip", "commands_client_force_grip");
	register_clcmd("-rcbot_force_grip", "commands_client_force_grip");
}

// ---- printing (Host::print) --------------------------------------------------------------------

// Print::Message: to (a player index) or the server console (0), with RCBot's tag. One trailing
// "\n" is dropped (AMX Mod X adds it).
stock commands_print(to, const text[])
{
	static buf[256];
	copy(buf, charsmax(buf), text);
	new n = strlen(buf);
	if (n > 0 && buf[n - 1] == '^n')
	{
		buf[n - 1] = 0;
	}
	if (to <= 0)
	{
		server_print("%s%s", RC_MSG_TAG, buf);
	}
	else if (is_user_connected(to))
	{
		client_print(to, print_console, "%s%s", RC_MSG_TAG, buf);
	}
}

// Print::TalkAll (BotPrintTalkMessage): every player's console, without the tag.
stock commands_print_all(const text[])
{
	static buf[256];
	copy(buf, charsmax(buf), text);
	new n = strlen(buf);
	if (n > 0 && buf[n - 1] == '^n')
	{
		buf[n - 1] = 0;
	}
	for (new i = 1; i <= g_maxclients; i++)
	{
		if (is_user_connected(i))
		{
			client_print(i, print_console, "%s", buf);
		}
	}
}

// ---- the command table -------------------------------------------------------------------------

// CommandId::lookup (CBotCvarList::GetCvar): exact, case-sensitive; -1 none.
stock commands_lookup(const name[])
{
	for (new c = CMD_COUNT - 1; c >= 0; c--)
	{
		if (equal(g_cmd_names[c], name))
		{
			return c;
		}
	}
	return -1;
}

// CommandId::needs_access (CBotCvar::needAccess): a command with an access level needs one of
// its bits.
stock bool:commands_needs_access(cmd, level)
{
	new need = g_cmd_access[cmd];
	return need > 0 && (level & need) == 0;
}

// command_list (CBotCvarList::PrintCommands): "\"<name>\"" per command, the last registered first.
stock commands_print_list(to)
{
	new line[40];
	for (new c = CMD_COUNT - 1; c >= 0; c--)
	{
		formatex(line, charsmax(line), "^"%s^"", g_cmd_names[c]);
		commands_print(to, line);
	}
}

// CommandId::help (showHelp): the command's lines (the NS-only ones left out).
stock commands_help(cmd, to)
{
	switch (cmd)
	{
		case CMD_REMOVEBOT:
		{
			commands_print(to, "removebot command");
		}
		case CMD_DEBUG_BOT:
		{
			commands_print(to, "debug_bot command");
			commands_print(to, "=================");
			commands_print(to, "commands");
			commands_print(to, "--------");
			commands_print(to, "^"debug_bot bot <bot name>^" : shows bot task & wpt info on screen");
			commands_print(to, "^"debug_bot off^" stops showing bot info on screen");
		}
		case CMD_DEBUG_ENT:
		{
			commands_print(to, "debug_ent command");
			commands_print(to, "=================");
			commands_print(to, "commands");
			commands_print(to, "--------");
			commands_print(to, "^"debug_ent entity^" : shows facing entity info on screen");
			commands_print(to, "^"debug_ent off^" stops showing info on screen");
		}
		case CMD_USERS:
		{
			commands_print(to, "users command");
			commands_print(to, "=============");
			commands_print(to, "commands: ");
			commands_print(to, "---------");
			commands_print(to, "readusers (Reads bot_users.ini file again)");
			commands_print(to, "adduser -- adds a user^nUsage: <player name> <access level>");
			commands_print(to, "removeuser -- removes a user^nUsage: <(player name) / #(user id)>");
			commands_print(to, "showusers -- shows all allowed bot users");
		}
		case CMD_HELP:
		{
			commands_print(to, "help command");
			commands_print(to, "------------");
			commands_print(to, "Usage : rcbot help [command name]");
			commands_print(to, "You can view useful tips on commands");
			commands_print(to, "and how to use them");
			commands_print(to, "-- general --");
			commands_print(to, "commands may have parameters");
			commands_print(to, "- if a parameter is needed that parameter is enclosed in '<' and '>'");
			commands_print(to, "  (example: rcbot util teleport [player name])");
			commands_print(to, "- if a parameter is optional that parameter is enclosed in '[' and ']'");
			commands_print(to, "  (example: rcbot util killbot <bot name>)");
		}
		case CMD_AUTOWAYPOINT:
		{
			commands_print(to, "auto_waypoint command");
			commands_print(to, "---------------------");
			commands_print(to, "Usage : rcbot autowaypoint [player name] [on (1) | off (0)]");
			commands_print(to, "Automatically places waypoints on player");
			commands_print(to, "If no player name is issued, it sets autowaypoint ON/OFF for you");
			commands_print(to, "If no on/off value is issued, it toggles the autowaypoint state");
			commands_print(to, "You must issue a player name if using a dedicated server");
		}
		case CMD_SET_PASS:
		{
			commands_print(to, "set_pass command help");
			commands_print(to, "---------------------");
			commands_print(to, "Usage : rcbot set_pass <password>");
			commands_print(to, "Allows the bot program to check the entered password");
			commands_print(to, "with your name to gain access to more rcbot commands");
			commands_print(to, "Your password is case sensitive and your game name is");
			commands_print(to, "also case sensitive, they must match on list of users");
		}
		case CMD_UTIL:
		{
			commands_print(to, "util command help");
			commands_print(to, "-----------------");
			commands_print(to, "killbot <bot name>");
			commands_print(to, "kills the bot with the name of <bot name>^n");
			commands_print(to, "set_teleport");
			commands_print(to, "sets your current teleport possition, anyone you teleport will teleport where you are^n");
			commands_print(to, "teleport [player name]");
			commands_print(to, "will teleport the player with [player name] or yourself if it is not issued^n");
			commands_print(to, "notouch [player name]");
			commands_print(to, "will stop touch calls reaching the player with [player name] or yourself if it is not issued.");
			commands_print(to, "no touch means you won't pick up objects and wont go through teleports (needed for waypointing teleports)^n");
			commands_print(to, "notarget_mode [player name]");
			commands_print(to, "will stop monsters shooting the player with [player name] or yourself if it is not issued.^n");
			commands_print(to, "send_sound <sound path>[.wav]");
			commands_print(to, "makes the sound in the <sound path> play on everyones client provided they have the sound^n");
			commands_print(to, "noclip_mode [player name]");
			commands_print(to, "will make the player fly through walls with [player name] or yourself if it is not issued^n");
			commands_print(to, "godmode [player name]");
			commands_print(to, "will put god mode on the player with [player name] or yourself if it is not issued^n");
			commands_print(to, "showboredom : will print bot's boredom (CAREFUL!! May overflow your client [bug?])^n");
			commands_print(to, "showreps : will print bots reputations (CAREFUL!! May overflow your client [bug?])^n");
		}
		case CMD_ADDBOT:
		{
			commands_print(to, "Add bot command help");
			commands_print(to, "--------------------");
			commands_print(to, "You can add a bot by typing...");
			commands_print(to, "^"rcbot addbot^" (creates a random bot)");
		}
		case CMD_WAYPOINT:
		{
			commands_print(to, "waypoint command help");
			commands_print(to, "---------------------");
			commands_print(to, "Waypoints are for bots to navigate, if there are no");
			commands_print(to, "waypoints on the map then the bots won't move around");
			commands_print(to, "Waypoints are files stored in the rcbot/waypoints/ts folder");
			commands_print(to, "^"waypoint on^" (displays waypoints)");
			commands_print(to, "^"waypoint off^" (stops displaying waypoints)");
			commands_print(to, "^"waypoint load^" (loads waypoints for current map)");
			commands_print(to, "^"waypoint save^" (saves waypoints to disk for future use)");
			commands_print(to, "^"waypoint flag <name>^" (toggle a flag on nearest waypoint)");
			commands_print(to, "^"waypoint setteam <1-4|clear>^" (assign/clear team on nearest waypoint)");
		}
		case CMD_BOTCAM:
		{
			commands_print(to, "bot cam command help");
			commands_print(to, "--------------------");
			commands_print(to, "Provided that ^"rcbot config allow_botcam^" is 1");
			commands_print(to, "you can view the bot cam, simply type...");
			commands_print(to, "^"rcbot botcam^" to view the bot cam, or");
			commands_print(to, "^"rcbot botcam off^" to stop viewing the bot cam");
		}
		case CMD_SQUAD:
		{
			commands_print(to, "squad command help");
			commands_print(to, "------------------");
			commands_print(to, "Allows you control squad as a leader");
			commands_print(to, "Available sub commands are:");
			commands_print(to, "^"form <formation name>^" (change formation)");
			commands_print(to, "^"spread <spread size>^" (change space between squad members)");
			commands_print(to, "NOTE: I recommend using the ^"rcbot squad_menu^" command instead");
		}
		case CMD_DEBUG:
		{
			commands_print(to, "debug command help");
			commands_print(to, "------------------");
			commands_print(to, "Usage: rcbot debug <option> <on/off>");
			commands_print(to, "on = 1, off = 0");
			commands_print(to, "Available debug options are:");
			static const opts[][] = {"touch", "think", "hear", "message", "block", "move", "aim", "nav", "see"};
			new line[16];
			for (new k = 0; k < sizeof(opts); k++)
			{
				formatex(line, charsmax(line), "^"%s^"", opts[k]);
				commands_print(to, line);
			}
		}
		case CMD_CONFIG:
		{
			static const lines[][] = {"config command help", "-----------------------", "Boolean commands",
				"these commands take booleans as parameters only", "booleans are 0 or 1", ".......................",
				"bots_leave_and_join (incomplete)", "tooltips", "autowaypoint_humans", "allow_botcam", "balance_teams",
				"chat_dont_learn", "chat_reply_to_bots", "wait_for_orders", "reserve_bot_slots", "dont_shoot", "chatting",
				"real_mode", "commanding", "bots_wait_for_bots (bots wait at wpts)", "ts_kungfu (bots use kung fu only in TS",
				"disable_weapon_learning (bots choose random weapons)", "disable_bot_squads (bots dont make squads)",
				"tactical_paths (paths avoid danger and exposure)", "-----------------------", "Integer commands",
				"these commands take integers as parameters only", "integers are -1, 0, 1, 2, 3, 4... etc.",
				".......................", "min_bots", "max_bots", "chat_percent", "chat_reply_percent", "max_path_revs",
				"max_update_vision_revs", "-----------------------", "Float commands",
				"these commands take real numbers as parameters only", "float numbers are 0.0, 0.5, 0.6, 1.0, 2.1... etc.",
				".......................", "bot_stuck_speed", "update_ladder_time", "update_vision_time"};
			for (new k = 0; k < sizeof(lines); k++)
			{
				commands_print(to, lines[k]);
			}
		}
		case CMD_PATHWAYPOINT:
		{
			commands_print(to, "pathwaypoint command help");
			commands_print(to, "-------------------------");
			commands_print(to, "^"pathwaypoint on^" shows paths between waypoints");
			commands_print(to, "^"pathwaypoint off^" stops showing paths");
			commands_print(to, "^"pathwaypoint create1^" marks the waypoint you are standing at to create a path from");
			commands_print(to, "^"pathwaypoint create2^" creates a path from the marked waypoint from, to this waypoint");
			commands_print(to, "^"pathwaypoint remove1^" marks the waypoint you are standing at to remove a path from");
			commands_print(to, "^"pathwaypoint remove2^" removes a path from the marked waypoint from, to this waypoint");
			commands_print(to, "^"pathwaypoint enable^" allows automatic path creation (default = enabled)");
			commands_print(to, "^"pathwaypoint disable^" disallows automatic path creation");
		}
		default:
		{
			commands_print(to, "There is no help for this command");
		}
	}
}

// ---- how commands are reached (commands.rs client_command, server_command, run_config) ---------

// The client's player (pClient->GetPlayer()): the index while its CClient is used, else 0.
static commands_me(client)
{
	return client >= 1 && client <= MAX_PLAYERS && g_cl_i[client][CL_USED] ? client : 0;
}

// Reads argv(first..first+3) into g_ca.
static commands_read_args(first)
{
	for (new k = 0; k < 4; k++)
	{
		read_argv(first + k, g_ca[k], charsmax(g_ca[]));
	}
}

// commands.rs server_command (RCBot_ServerCommand): rcbot <pcmd> args... from the server console,
// with every access; on a listen server it runs for the listen server's client.
public commands_server_rcbot()
{
	new pcmd[64], line[96];
	read_argv(1, pcmd, charsmax(pcmd));
	commands_read_args(2);
	new listen = g_cl_listen_server;
	new st = CVAR_NOTEXIST;
	new cmd = commands_lookup(pcmd);
	if (cmd != -1)
	{
		if (is_dedicated_server())
		{
			if (!g_cmd_dedicated[cmd])
			{
				commands_print(0, "Error: Can't use this command on the dedicated server^n");
				return PLUGIN_HANDLED;
			}
			commands_print(0, "Accessing debug commands : detected dedicated server");
			st = commands_run(0, cmd);
		}
		else if (listen != 0 && g_cl_i[listen][CL_USED])
		{
			st = commands_run(listen, cmd);
		}
		else
		{
			commands_print(0, "Access to debug commands failed : Detected listen server with no listenserver client");
		}
	}
	switch (st)
	{
		case CVAR_ACCESSED:
		{
		}
		case CVAR_ERROR:
		{
			formatex(line, charsmax(line), "The bot command %s returned an error!^n", pcmd);
			commands_print(listen, line);
		}
		default:
		{
			if (!pcmd[0])
			{
				commands_print(listen, "No command issued!^n");
			}
			else
			{
				formatex(line, charsmax(line), "The bot command %s does not exist!^n", pcmd);
				commands_print(listen, line);
			}
			commands_print(listen, "Available SUB-commands are:^n");
			commands_print_list(listen);
		}
	}
	return PLUGIN_HANDLED;
}

// commands.rs client_command for "rcbot <command>": the listen server's own player goes
// through the server command (as upstream sends its rcbot there).
public commands_client_rcbot(id)
{
	if (!is_dedicated_server() && id == g_cl_listen_server)
	{
		return PLUGIN_CONTINUE;
	}
	new sub[64];
	read_argv(1, sub, charsmax(sub));
	commands_read_args(2);
	new me = commands_me(id);
	new st;
	new cmd = commands_lookup(sub);
	if (cmd == -1)
	{
		st = CVAR_NOTEXIST;
	}
	else if (me == 0)
	{
		commands_print(id, "Your client was not found in list of clients");
		return PLUGIN_HANDLED;
	}
	else if (commands_needs_access(cmd, g_cl_i[me][CL_ACCESS]))
	{
		st = CVAR_NEEDACCESS;
	}
	else
	{
		st = commands_run(me, cmd);
	}
	return commands_client_result(id, st);
}

// commands.rs client_command for "menuselect": RCBot's open menu takes the key; otherwise (and
// after it: CvarState::Continue) the game and AMX Mod X see it too.
public commands_client_menuselect(id)
{
	new me = commands_me(id);
	if (me == 0)
	{
		commands_print(id, "Your client was not found in list of clients");
		return PLUGIN_HANDLED;
	}
	commands_read_args(1);
	return commands_client_result(id, commands_run(me, CMD_MENUSELECT));
}

// commands.rs client_command for "+rcbot_force_grip" / "-rcbot_force_grip": the access *level*
// is compared with RCBOT_ACCESS_FORCE_GRIP as a number, though it is a set of bits (kept).
public commands_client_force_grip(id)
{
	new me = commands_me(id);
	new level = me ? g_cl_i[me][CL_ACCESS] : 0;
	if (level < RCBOT_ACCESS_FORCE_GRIP)
	{
		return commands_client_result(id, CVAR_NEEDACCESS);
	}
	new cmd[24];
	read_argv(0, cmd, charsmax(cmd));
	commands_force_grip(me, cmd[0] == '+');
	return PLUGIN_HANDLED;
}

// The end of client_command: the messages for NeedAccess, Error and NotExist; Continue lets
// the game see the command.
static commands_client_result(id, st)
{
	switch (st)
	{
		case CVAR_NEEDACCESS:
		{
			// The AdminHelp tooltip (SayToolTip) does nothing on the port either.
			commands_print(id, "You do not have access to this command...");
		}
		case CVAR_ERROR:
		{
			commands_print(id, "Error accessing command...");
		}
		case CVAR_NOTEXIST:
		{
			commands_print(id, "No command entered or bot command does not exist!");
		}
		case CVAR_CONTINUE:
		{
			return PLUGIN_CONTINUE;
		}
	}
	return PLUGIN_HANDLED;
}

// commands.rs run_config (CBotGlobals::ReadConfig's dispatch): a bot_config.ini line runs
// without a client and without access checks.
stock commands_run_config_line(const cmd[], const a1[], const a2[], const a3[], const a4[])
{
	new c = commands_lookup(cmd);
	if (c == -1)
	{
		new line[96];
		formatex(line, charsmax(line), "Error with config, unknown command : %s", cmd);
		commands_print(0, line);
		return;
	}
	copy(g_ca[0], charsmax(g_ca[]), a1);
	copy(g_ca[1], charsmax(g_ca[]), a2);
	copy(g_ca[2], charsmax(g_ca[]), a3);
	copy(g_ca[3], charsmax(g_ca[]), a4);
	commands_run(0, c);
}

// commands.rs run (Run::action): the command's action for a client (0: the server console or
// bot_config.ini), without access checks. Then what the host does after every effect: the
// squads' messages, and the recheck of access some commands ask for.
stock commands_run(client, cmd)
{
	new st = commands_action(client, cmd);
	squad_flush_messages();
	for (new p = 1; p <= MAX_PLAYERS; p++)
	{
		if (g_cl_i[p][CL_USED] && g_cl_i[p][CL_RECHECK_AUTH])
		{
			clients_recheck_auth(p);
		}
	}
	return st;
}

static commands_action(client, cmd)
{
	new me = commands_me(client);
	switch (cmd)
	{
		case CMD_ADDBOT:
		{
			return commands_add_bot();
		}
		case CMD_WAYPOINT:
		{
			return commands_waypoint(client, me);
		}
		case CMD_PATHWAYPOINT:
		{
			return commands_path_waypoint(client, me);
		}
		case CMD_DEBUG:
		{
			return commands_debug(client, me);
		}
		case CMD_WAYPOINT_MENU:
		{
			return commands_waypoint_menu(me);
		}
		case CMD_BOT_MENU, CMD_SQUAD_MENU:
		{
			if (me == 0)
			{
				commands_print(0, "This command can only be used on the client");
				return CVAR_ERROR;
			}
			menu_show(me, cmd == CMD_BOT_MENU ? MENU_BOT_MAIN : MENU_SQUAD);
			return CVAR_ACCESSED;
		}
		case CMD_MENUSELECT:
		{
			return commands_menu_select(me);
		}
		case CMD_CONFIG:
		{
			return commands_config(me);
		}
		case CMD_SET_PASS:
		{
			if (!g_ca[0][0] || me == 0)
			{
				return CVAR_ERROR;
			}
			clients_set_pass(me, g_ca[0]);
			commands_print(me, "Password Set");
			return CVAR_ACCESSED;
		}
		case CMD_UTIL:
		{
			return commands_util(me);
		}
		case CMD_SQUAD:
		{
			return commands_squad(me);
		}
		case CMD_BOTCAM:
		{
			return commands_botcam(me);
		}
		case CMD_AUTOWAYPOINT:
		{
			return commands_auto_waypoint(me);
		}
		case CMD_HELP:
		{
			return commands_help_command(me);
		}
		case CMD_USERS:
		{
			return commands_users(me);
		}
		case CMD_DEBUG_BOT:
		{
			return commands_debug_bot(me);
		}
		case CMD_DEBUG_ENT:
		{
			return commands_debug_ent(me);
		}
		case CMD_REMOVEBOT:
		{
			// Effect::KickBotFromTeam: the host kicks the last bot, whatever the team.
			commands_kick_random_bot();
			return CVAR_ACCESSED;
		}
	}
	return CVAR_NOTEXIST;
}

// ---- the commands -------------------------------------------------------------------------------

// Run::add_bot (BotFunc_AddBot's arguments; the class and skill are unused on TS) and Host::add_bot.
static commands_add_bot()
{
	new team = 0;
	if (g_ca[0][0])
	{
		team = str_to_num(g_ca[0]);
		if (team <= 0 || team > 5)
		{
			team = 5;
		}
	}
	if (g_ca[1][0] && str_to_num(g_ca[1]) < 0)
	{
		// The class's RANDOM_LONG(0, 9), drawn and unused, as upstream.
		random_num(0, 9);
	}
	return backend_add_bot_now(team) > 0 ? CVAR_ACCESSED : CVAR_ERROR;
}

// commands.rs ts_team_choice (BotFunc_AddBot's TS team): the team name a new bot joins as (its
// model key in teamplay): mp_teamlist split at ";" as upstream splits it (the last byte always
// ends the last team; an empty team repeats the one before it, a leading ";" gives an empty
// team; teams cut at 63 bytes), then team force_team (1-based) if it exists and isn't empty,
// else a random team. force_team 1..4 is the command's team, else the config's.
stock commands_ts_team_choice(force_team, model[], len)
{
	model[0] = 0;
	new force = force_team >= 1 && force_team < 5 ? force_team : g_cfg_i[CFG_FORCE_TEAM];
	new list[512];
	get_cvar_string("mp_teamlist", list, charsmax(list));
	static teams[16][64];
	new n = 0, newteam[64], j = 0;
	new tl = strlen(list);
	for (new i = 0; i < tl; i++)
	{
		new c = list[i];
		new bool:last = i == tl - 1;
		if (last || c == ';')
		{
			if (last)
			{
				commands_newteam_put(newteam, j, c);
			}
			if (n < sizeof(teams))
			{
				copy(teams[n++], charsmax(teams[]), newteam);
			}
			j = 0;
		}
		else
		{
			commands_newteam_put(newteam, j, c);
		}
	}
	if (force > 0 && force <= n && teams[force - 1][0])
	{
		copy(model, len, teams[force - 1]);
		return;
	}
	if (n == 0)
	{
		return;
	}
	copy(model, len, teams[clamp(random_num(0, n - 1), 0, n - 1)]);
}

// newteam[j++] = c; newteam[j] = 0 (within its 64 bytes).
static commands_newteam_put(newteam[64], &j, c)
{
	if (j < 63)
	{
		newteam[j] = c;
		newteam[j + 1] = 0;
	}
	j++;
}

// lib.rs map_config_frame: "rcbot addbot" in a map config holds the rejoin code's bots back
// (m_fBotRejoinTime, backend.inc's, written here as the host's map_config_frame writes it).
stock commands_hold_rejoin(Float:until)
{
	g_rejoin_time = until;
}

// Run::menu_select (CMenuSelectCommand::action).
static commands_menu_select(me)
{
	if (me == 0 || g_cl_i[me][CL_MENU] == -1)
	{
		return CVAR_CONTINUE;
	}
	if (!g_ca[0][0])
	{
		return CVAR_ERROR;
	}
	new key = str_to_num(g_ca[0]);
	if (key < 10)
	{
		g_cl_i[me][CL_LAST_KEY] = key;
		// A negative key indexes before the item array upstream; nothing happens here.
		menu_select(me, key);
	}
	return CVAR_CONTINUE;
}

// Run::menu_action (BotMenu_Func_*): waypoint functions edit the client's current waypoint
// (nothing at -1), the shared ones switch on the key pressed.
stock commands_menu_action(player, action, bits)
{
	new wpt = g_cl_i[player][CL_CURRENT_WPT], key = g_cl_i[player][CL_LAST_KEY];
	switch (action)
	{
		case MA_TOGGLE_FLAG:
		{
			if (wpt != -1)
			{
				commands_toggle_flag(wpt, bits);
			}
		}
		case MA_TEAM_WAYPOINT:
		{
			if (wpt == -1 || key == -1 || key > 4)
			{
				return;
			}
			new f = nav_flags(wpt);
			if (f & NAV_FL_TEAM_SPECIFIC)
			{
				nav_set_flags(wpt, f & ~NAV_FL_TEAM & ~NAV_FL_TEAM_SPECIFIC);
			}
			else
			{
				// Upstream ORs the team in without clearing the old bits.
				nav_set_flags(wpt, f | (key - 1) | NAV_FL_TEAM_SPECIFIC);
			}
		}
		case MA_UNREACHABLE_WAYPOINT:
		{
			if (wpt != -1)
			{
				commands_toggle_flag(wpt, NAV_FL_UNREACHABLE);
				commands_remove_paths(wpt, true, true);
			}
		}
		case MA_REMOVE_ALL_PATHS, MA_REMOVE_PATHS_TO, MA_REMOVE_PATHS_FROM:
		{
			if (wpt != -1)
			{
				commands_remove_paths(wpt, action != MA_REMOVE_PATHS_FROM, action != MA_REMOVE_PATHS_TO);
			}
		}
		case MA_DELETE_WAYPOINTS:
		{
			nav_clear();
		}
		case MA_ADD_BOT:
		{
			backend_add_bot_now(0);
		}
		case MA_KICK_BOT:
		{
			commands_kick_random_bot();
		}
		case MA_MAKE_SQUAD:
		{
			manager_make_squad(player);
		}
		case MA_SQUAD_LEAVE:
		{
			new sq = commands_leader_squad(player);
			if (sq != -1)
			{
				squad_change_leader_of(sq);
				commands_print(player, "You have left your squad^n");
			}
		}
		case MA_SQUAD_REMOVE:
		{
			new sq = commands_leader_squad(player);
			if (sq != -1)
			{
				squad_remove(sq);
				commands_print(player, "Squad removed^n");
			}
		}
		case MA_SQUAD_REMOVE_ALL_BOT_SQUADS:
		{
			new n = squad_remove_all_bot_squads(world_team(player));
			new line[64];
			formatex(line, charsmax(line), "%s %d bot squads removed", RC_MSG_TAG, n);
			commands_print(player, line);
		}
		case MA_SQUAD_MODE1:
		{
			new sq = commands_leader_squad(player);
			if (sq == -1)
			{
				return;
			}
			switch (key)
			{
				case 1:
				{
					squad_toggle_fire_mode(sq);
				}
				case 2:
				{
					squad_toggle_combat_type(sq, COMBAT_CROUCH);
				}
				case 3:
				{
					squad_toggle_combat_type(sq, COMBAT_STEALTH);
				}
				case 4:
				{
					squad_return_all_to_formation(sq);
				}
			}
		}
		case MA_SQUAD_FORM, MA_SQUAD_FORM2:
		{
			new sq = commands_leader_squad(player);
			if (sq == -1 || key < 1 || key > 3)
			{
				return;
			}
			static const forms[2][3] = {{FORM_WEDGE, FORM_LINE, FORM_COLUMN}, {FORM_ECH_LEFT, FORM_ECH_RIGHT, FORM_VEE}};
			static const names[2][3][] = {{"WEDGE", "LINE", "COLUMN"}, {"ECHELON LEFT", "ECHELON RIGHT", "VEE"}};
			new s = action == MA_SQUAD_FORM ? 0 : 1;
			squad_change_formation(sq, forms[s][key - 1]);
			new line[64];
			formatex(line, charsmax(line), "Squad formation is now %s^n", names[s][key - 1]);
			commands_print(player, line);
		}
		case MA_SQUAD_SPREAD:
		{
			new sq = commands_leader_squad(player);
			if (sq == -1)
			{
				return;
			}
			if (key == 1)
			{
				squad_change_spread(sq, squad_spread(sq) + 24.0);
			}
			else if (key == 2)
			{
				squad_change_spread(sq, squad_spread(sq) - 24.0);
			}
		}
	}
}

// Run::menu_action's squad(): the squad the player leads, else "You are not leader of a squad".
static commands_leader_squad(player)
{
	new sq = squad_find_by_leader(player);
	if (sq == -1)
	{
		commands_print(player, "You are not leader of a squad^n");
	}
	return sq;
}

static commands_toggle_flag(wpt, bits)
{
	new f = nav_flags(wpt);
	nav_set_flags(wpt, f & bits ? f & ~bits : f | bits);
}

// Effect::RemovePaths: the paths out of the waypoint, then those into it.
static commands_remove_paths(wpt, bool:into, bool:from)
{
	if (wpt < 0)
	{
		return;
	}
	if (from)
	{
		new out[64];
		new n = nav_paths_out(wpt, out, sizeof(out));
		for (new k = 0; k < n && k < sizeof(out); k++)
		{
			nav_remove_path(wpt, out[k]);
		}
	}
	if (into)
	{
		nav_remove_paths_to(wpt);
	}
}

// Effect::KickBotFromTeam / KickRandomBot as the host does them: the last bot is kicked.
static commands_kick_random_bot()
{
	for (new b = MAX_PLAYERS; b >= 1; b--)
	{
		if (g_co_i[b][CO_USED])
		{
			// Bot::kick, set from the command as the host does.
			g_co_i[b][CO_KICK] = 1;
			return;
		}
	}
}

// Run::waypoint_menu (CWaypointMenuCommand::action).
static commands_waypoint_menu(me)
{
	if (me == 0)
	{
		commands_print(0, "This command can only be used on the client");
		return CVAR_ERROR;
	}
	new Float:origin[3];
	pev(me, pev_origin, origin);
	new wpt = commands_nearest_waypoint(origin, 50.0, true);
	g_cl_i[me][CL_CURRENT_WPT] = wpt;
	if (wpt != -1)
	{
		menu_show(me, MENU_WAYPOINT_MAIN);
	}
	else
	{
		commands_print(me, "Error: You are not standing close enough to a waypoint!");
	}
	return CVAR_ACCESSED;
}

// Host::nearest_waypoint (WaypointLocations.NearestWaypoint(origin, range, -1, false, visible)).
stock commands_nearest_waypoint(const Float:origin[3], Float:range, bool:visible)
{
	return nav_nearest(origin, range, -1, visible ? NAV_NEAREST_VISIBLE : 0);
}

// Host::find_player (UTIL_FindPlayerByTruncName): the first player whose name starts with name,
// case-insensitively; 0 none.
stock commands_find_player(const name[])
{
	new pname[RC_NAME_LEN], len = strlen(name);
	for (new i = 1; i <= g_maxclients; i++)
	{
		if (!is_user_connected(i))
		{
			continue;
		}
		get_user_name(i, pname, charsmax(pname));
		if (pname[0] && equali(pname, name, len))
		{
			return i;
		}
	}
	return 0;
}

// Host::player_by_user_id (#<userid>).
static commands_player_by_user_id(userid)
{
	for (new i = 1; i <= g_maxclients; i++)
	{
		if (is_user_connected(i) && get_user_userid(i) == userid)
		{
			return i;
		}
	}
	return 0;
}

// host view_trace: the entity a player looks at within dist (from the eyes along v_angle,
// monsters and glass counted, the player ignored): -1 none, 0 the world.
static commands_view_trace(player, Float:dist, Float:end_pos[3])
{
	new Float:src[3], Float:ang[3], Float:fwd[3], Float:right[3], Float:end[3], hit;
	commands_eyes(player, src);
	pev(player, pev_v_angle, ang);
	make_vectors(ang, fwd, right);
	vec_mad(src, fwd, dist, end);
	world_trace_line(src, end, false, false, player, end_pos, hit);
	return hit;
}

static commands_eyes(player, Float:out[3])
{
	new Float:ofs[3];
	pev(player, pev_origin, out);
	pev(player, pev_view_ofs, ofs);
	vec_add(out, ofs, out);
}

// Host::facing_entity (UTIL_FacingEnt): the entity the player looks at (players and monsters
// only unless any); -1 none.
static commands_facing_entity(player, bool:any)
{
	new Float:end[3];
	new hit = commands_view_trace(player, 4096.0, end);
	if (hit < 0 || (hit > 0 && !pev_valid(hit)))
	{
		return -1;
	}
	if (any || (hit > 0 && (pev(hit, pev_flags) & (FL_CLIENT | FL_MONSTER))))
	{
		return hit;
	}
	return -1;
}

// Run::util (CUtilCommand::action).
static commands_util(me)
{
	new line[192], name[RC_NAME_LEN], other[RC_NAME_LEN];
	if (!g_ca[0][0])
	{
		commands_print(me, "no command entered");
		return CVAR_ERROR;
	}
	new sub[32];
	copy(sub, charsmax(sub), g_ca[0]);
	if (equal(sub, "showboredom") || equal(sub, "showreps"))
	{
		if (g_ca[1][0])
		{
			new bot = commands_find_player(g_ca[1]);
			if (bot && is_rcbot(bot))
			{
				if (equal(sub, "showreps"))
				{
					rep_print_reps(bot, me);
				}
				else
				{
					events_boredom_text(bot, line, charsmax(line));
					commands_print(me, line);
				}
			}
		}
	}
	else if (equal(sub, "bot_goto"))
	{
		if (me && g_ca[1][0])
		{
			new bot = commands_find_player(g_ca[1]);
			if (bot && is_rcbot(bot))
			{
				new wpt;
				if (g_ca[2][0])
				{
					wpt = str_to_num(g_ca[2]);
				}
				else
				{
					new Float:o[3];
					pev(me, pev_origin, o);
					wpt = commands_nearest_waypoint(o, 100.0, false);
				}
				navigate_go_to_waypoint(bot, wpt);
			}
		}
	}
	else if (equal(sub, "version"))
	{
		formatex(line, charsmax(line), "Bot Version : %s", RC_BOT_VER);
		commands_print(me, line);
	}
	else if (equal(sub, "givetsweapon"))
	{
		if (me && g_ca[1][0])
		{
			new w = str_to_num(g_ca[1]);
			// UTIL_makeTSweapon (nothing for weapon 0): tsfun's ts_giveweapon with 200 extra rounds.
			if (w != 0 && is_user_alive(me))
			{
				ts_giveweapon(me, w, 200, 0);
			}
		}
	}
	else if (equal(sub, "numclients"))
	{
		formatex(line, charsmax(line), "Loop count = %d", commands_num_clients());
		commands_print(me, line);
	}
	else if (equal(sub, "hudtext"))
	{
		if (g_ca[1][0])
		{
			commands_hud_text();
		}
	}
	else if (equal(sub, "explo"))
	{
		if (me == 0)
		{
			commands_print(0, "can't use on dedicated server");
			return CVAR_ERROR;
		}
		new Float:magnitude = g_ca[1][0] ? float(str_to_num(g_ca[1])) : 400.0;
		new bool:damage = !(g_ca[2][0] && str_to_num(g_ca[2]) > 0);
		commands_explosion(me, magnitude, damage);
	}
	else if (equal(sub, "killbot"))
	{
		if (g_ca[1][0])
		{
			new bot = commands_find_player(g_ca[1]);
			if (bot)
			{
				if (is_rcbot(bot))
				{
					// Bot::kill, set from the command as the host does.
					g_co_i[bot][CO_KILL] = 1;
					get_user_name(bot, other, charsmax(other));
					if (me)
					{
						get_user_name(me, name, charsmax(name));
						formatex(line, charsmax(line), "%s is killing bot ^"%s^"...^n", name, other);
					}
					else
					{
						formatex(line, charsmax(line), "Server is killing bot ^"%s^"...^n", other);
					}
					commands_print_all(line);
				}
			}
			else
			{
				commands_print(me, "bot name not found");
			}
		}
		else
		{
			commands_print(me, "usage rcbot util killbot <botname>");
		}
	}
	else if (equal(sub, "set_teleport"))
	{
		if (me)
		{
			pev(me, pev_origin, g_cl_teleport[me]);
			g_cl_i[me][CL_TELEPORT_SET] = 1;
		}
	}
	else if (equal(sub, "projectile_rep") || equal(sub, "grenade_deact"))
	{
		new cmd = equal(sub, "projectile_rep") ? CLIENT_CMD_PROJ_REPEL : CLIENT_CMD_GREN_DEACT;
		if (me)
		{
			clients_toggle_cmd(me, cmd);
			formatex(line, charsmax(line), "%s : %d", sub, clients_has_cmd(me, cmd) ? 1 : 0);
			commands_print(me, line);
		}
	}
	else if (equal(sub, "teleport_ent"))
	{
		if (me)
		{
			if (!g_cl_i[me][CL_TELEPORT_SET])
			{
				commands_print(0, "no teleport vector set yet");
			}
			else
			{
				// UTIL_getEntityInFront: 8192 units, anything but the world (or a world brush).
				new Float:end[3];
				new hit = commands_view_trace(me, 8192.0, end);
				if (hit > 0 && pev_valid(hit) && !(pev(hit, pev_flags) & FL_WORLDBRUSH))
				{
					engfunc(EngFunc_SetOrigin, hit, g_cl_teleport[me]);
				}
			}
		}
	}
	else if (equal(sub, "teleport") && me)
	{
		if (!g_cl_i[me][CL_TELEPORT_SET])
		{
			commands_print(me, "Error: No teleport destination set, use ^"set_teleport^"");
			return CVAR_ACCESSED;
		}
		get_user_name(me, name, charsmax(name));
		if (g_ca[1][0])
		{
			new p = commands_find_player(g_ca[1]);
			if (p)
			{
				engfunc(EngFunc_SetOrigin, p, g_cl_teleport[me]);
				get_user_name(p, other, charsmax(other));
				formatex(line, charsmax(line), "%s teleported %s to another location.^n", name, other);
				commands_print_all(line);
			}
		}
		else
		{
			engfunc(EngFunc_SetOrigin, me, g_cl_teleport[me]);
			formatex(line, charsmax(line), "%s teleported self to another location^n", name);
			commands_print_all(line);
		}
	}
	else if (equal(sub, "notouch") && me)
	{
		g_cl_i[me][CL_NO_TOUCH] = !g_cl_i[me][CL_NO_TOUCH];
		get_user_name(me, name, charsmax(name));
		formatex(line, charsmax(line), "%s used notouch %d on self^n", name, g_cl_i[me][CL_NO_TOUCH] ? 1 : 0);
		commands_print_all(line);
	}
	else if (equal(sub, "notarget_mode") || equal(sub, "noclip_mode") || equal(sub, "godmode"))
	{
		new kind = equal(sub, "notarget_mode") ? 0 : (equal(sub, "noclip_mode") ? 1 : 2);
		static const verbs[3][] = {"notarget_mode", "no_clip", "godmode"};
		if (g_ca[1][0])
		{
			new p = commands_find_player(g_ca[1]);
			if (p)
			{
				new on = commands_toggle_mode(p, kind);
				// Upstream names the caller with a null netname from the server console; "Server".
				if (me)
				{
					get_user_name(me, name, charsmax(name));
				}
				else
				{
					copy(name, charsmax(name), "Server");
				}
				get_user_name(p, other, charsmax(other));
				formatex(line, charsmax(line), "%s used %s %d on %s^n", name, verbs[kind], on, other);
				commands_print_all(line);
			}
		}
		else if (me)
		{
			new on = commands_toggle_mode(me, kind);
			get_user_name(me, name, charsmax(name));
			formatex(line, charsmax(line), "%s used %s %d on self^n", name, verbs[kind], on);
			commands_print_all(line);
		}
	}
	else if (equal(sub, "send_sound"))
	{
		if (g_ca[1][0])
		{
			// UTIL_PlaySoundToAll: every named player (not bots).
			for (new i = 1; i <= g_maxclients; i++)
			{
				if (is_user_connected(i) && !is_user_bot(i))
				{
					get_user_name(i, name, charsmax(name));
					if (name[0])
					{
						client_cmd(i, "play %s", g_ca[1]);
					}
				}
			}
		}
	}
	else if (equal(sub, "search") && me)
	{
		new Float:range = g_ca[1][0] ? str_to_float(g_ca[1]) : 100.0;
		commands_search(me, range);
	}
	return CVAR_ACCESSED;
}

// Host::num_clients (UTIL_GetNumClients(true)).
static commands_num_clients()
{
	new n = 0;
	for (new i = 1; i <= g_maxclients; i++)
	{
		if (is_user_connected(i))
		{
			n++;
		}
	}
	return n;
}

// Host::toggle_notarget / toggle_noclip / toggle_godmode: 1 when now on.
static commands_toggle_mode(p, kind)
{
	switch (kind)
	{
		case 0:
		{
			new f = pev(p, pev_flags) ^ FL_NOTARGET;
			set_pev(p, pev_flags, f);
			return (f & FL_NOTARGET) ? 1 : 0;
		}
		case 1:
		{
			new mt = pev(p, pev_movetype) == MOVETYPE_NOCLIP ? MOVETYPE_WALK : MOVETYPE_NOCLIP;
			set_pev(p, pev_movetype, mt);
			return mt == MOVETYPE_NOCLIP ? 1 : 0;
		}
	}
	new Float:td;
	pev(p, pev_takedamage, td);
	td = td == 0.0 ? 2.0 : 0.0;
	set_pev(p, pev_takedamage, td);
	return td == 0.0 ? 1 : 0;
}

// Effect::HudText (util hudtext): to everyone, channel 3, centred, fade in/out 0.75 s, hold
// 2 s, both colours at alpha 200; "^" becomes a new line and "_" a space (Engine::hud_message:
// TE_TEXTMESSAGE).
static commands_hud_text()
{
	new text[256];
	copy(text, charsmax(text), g_ca[1]);
	replace_all(text, charsmax(text), "^^", "^n");
	replace_all(text, charsmax(text), "_", " ");
	new r = 255, g = 255, b = 255;
	if (equal(g_ca[2], "blue"))
	{
		r = 0; g = 0; b = 255;
	}
	else if (equal(g_ca[2], "green"))
	{
		r = 0; g = 255; b = 0;
	}
	else if (equal(g_ca[2], "red"))
	{
		r = 255; g = 0; b = 0;
	}
	else if (equal(g_ca[2], "yellow"))
	{
		r = 255; g = 255; b = 0;
	}
	else if (equal(g_ca[2], "purple"))
	{
		r = 255; g = 0; b = 255;
	}
	else if (equal(g_ca[2], "cyan"))
	{
		r = 0; g = 255; b = 255;
	}
	for (new i = 1; i <= g_maxclients; i++)
	{
		if (!is_user_connected(i) || is_user_bot(i))
		{
			continue;
		}
		message_begin(MSG_ONE_UNRELIABLE, SVC_TEMPENTITY, _, i);
		write_byte(TE_TEXTMESSAGE);
		write_byte(3);
		write_short(-8192);
		write_short(-8192);
		write_byte(0);
		write_byte(r);
		write_byte(g);
		write_byte(b);
		write_byte(200);
		write_byte(r);
		write_byte(g);
		write_byte(b);
		write_byte(200);
		write_short(192);
		write_short(192);
		write_short(512);
		write_string(text);
		message_end();
	}
}

// Host::explosion (util explo): the view trace (4096 units), its beam to the caller
// (WaypointDrawBeam(..., 20, 10, 200, 1, 1, 160, 10)) and ExplosionCreate 4 units short of
// where it ends: a TE_EXPLOSION of the fireball (scale: the magnitude, at most 255) and, with
// damage, radius damage of the magnitude within half of it (DMG_BLAST).
static commands_explosion(player, Float:magnitude, bool:damage)
{
	new Float:src[3], Float:ang[3], Float:fwd[3], Float:right[3], Float:end[3], Float:tr_end[3], hit;
	commands_eyes(player, src);
	pev(player, pev_v_angle, ang);
	make_vectors(ang, fwd, right);
	vec_mad(src, fwd, 4096.0, end);
	world_trace_line(src, end, false, false, player, tr_end, hit);
	clients_beam(player, src, tr_end, 20, 10, 200, 1, 1, 160, 10);
	new Float:center[3];
	vec_mad(tr_end, fwd, -4.0, center);
	if (g_fireball_sprite)
	{
		message_begin(MSG_PAS, SVC_TEMPENTITY, center);
		write_byte(TE_EXPLOSION);
		write_coord_f(center[0]);
		write_coord_f(center[1]);
		write_coord_f(center[2]);
		write_short(g_fireball_sprite);
		write_byte(min(floatround(magnitude, floatround_tozero), 255));
		write_byte(15);
		write_byte(0);
		message_end();
	}
	if (damage)
	{
		commands_radius_damage(center, player, magnitude, magnitude / 2.0);
	}
}

// RCBot's RadiusDamage(vecSrc, inflictor, attacker, damage, radius, CLASS_NONE, DMG_BLAST): every
// entity within radius that takes damage, on the source's side of the water line, that the
// blast reaches (the trace to its BodyTarget) takes damage less damage / radius per unit and is
// pushed away at that speed. The damage is the engine module's fakedamage (design: deviation).
static commands_radius_damage(const Float:center[3], owner, Float:damage, Float:radius)
{
	new Float:src[3];
	vec_copy(center, src);
	new Float:falloff = radius != 0.0 ? damage / radius : 1.0;
	new bool:in_water = world_point_contents(src) == CONTENTS_WATER;
	src[2] += 1.0;
	new ents[64];
	new n = world_entities_in_sphere(src, radius, ents, sizeof(ents));
	new Float:mn[3], Float:mx[3], Float:spot[3], Float:ofs[3], Float:origin[3], Float:end[3], Float:dir[3], Float:fraction, hit;
	for (new k = 0; k < n; k++)
	{
		new e = ents[k];
		if (!pev_valid(e))
		{
			continue;
		}
		new Float:td;
		pev(e, pev_takedamage, td);
		new wl = pev(e, pev_waterlevel);
		if (td == 0.0 || (in_water && wl == 0) || (!in_water && wl == 3))
		{
			continue;
		}
		pev(e, pev_absmin, mn);
		pev(e, pev_absmax, mx);
		vec_add(mn, mx, spot);
		vec_scale(spot, 0.5, spot);
		if (pev(e, pev_flags) & FL_CLIENT)
		{
			pev(e, pev_view_ofs, ofs);
			vec_mad(spot, ofs, random_float(0.5, 1.1), spot);
		}
		engfunc(EngFunc_TraceLine, src, spot, DONT_IGNORE_MONSTERS, owner, g_tr);
		get_tr2(g_tr, TR_flFraction, fraction);
		hit = get_tr2(g_tr, TR_pHit);
		if (!(fraction == 1.0 || hit == e))
		{
			continue;
		}
		if (get_tr2(g_tr, TR_StartSolid))
		{
			vec_copy(src, end);
		}
		else
		{
			get_tr2(g_tr, TR_vecEndPos, end);
		}
		new Float:adjusted = damage - vec_distance(src, end) * falloff;
		if (adjusted < 0.0)
		{
			adjusted = 0.0;
		}
		fakedamage(e, "rcbot_explosion", adjusted, DMG_BLAST);
		if (pev_valid(e))
		{
			pev(e, pev_origin, origin);
			vec_sub(origin, src, dir);
			vec_normalize(dir, dir);
			vec_scale(dir, adjusted, dir);
			set_pev(e, pev_velocity, dir);
		}
	}
}

// Effect::Search (util search): the entities within range of the player.
static commands_search(player, Float:range)
{
	new Float:po[3], Float:eo[3], ents[128], line[160], cls[32];
	pev(player, pev_origin, po);
	new n = world_entities_in_sphere(po, range, ents, sizeof(ents));
	for (new k = 0; k < n; k++)
	{
		new e = ents[k];
		if (!pev_valid(e))
		{
			continue;
		}
		pev(e, pev_classname, cls, charsmax(cls));
		if (!world_entity_origin(e, eo))
		{
			pev(e, pev_origin, eo);
		}
		formatex(line, charsmax(line), "Found: id: %d, classname:%s distance:%.2f", e, cls, vec_distance(eo, po));
		commands_print(player, line);
	}
}

// Run::users (CUsersCommand::action).
static commands_users(me)
{
	new line[128], name[RC_NAME_LEN];
	if (equal(g_ca[0], "readusers"))
	{
		users_clear();
		commands_print(me, "re-reading bot users...");
		users_read_file(0);
		for (new p = 1; p <= MAX_PLAYERS; p++)
		{
			if (g_cl_i[p][CL_USED])
			{
				g_cl_i[p][CL_ACCESS] = 0;
				g_cl_i[p][CL_RECHECK_AUTH] = 1;
			}
		}
	}
	else if (equal(g_ca[0], "adduser"))
	{
		if (g_ca[1][0] && g_ca[2][0])
		{
			new target = commands_find_player(g_ca[1]);
			if (target == me)
			{
				commands_print(me, is_dedicated_server() ? "Player not found!!" : "Can't give yourself different access!!");
				return CVAR_ERROR;
			}
			if (target == 0 || !g_cl_i[target][CL_USED])
			{
				commands_print(me, "Player not found!!");
				return CVAR_ERROR;
			}
			new level = str_to_num(g_ca[2]);
			get_user_name(target, name, charsmax(name));
			users_add_player(name, "***", level, g_cl_steam[target]);
			g_cl_i[target][CL_RECHECK_AUTH] = 1;
			formatex(line, charsmax(line), "Gave %s access level of %d", name, level);
			commands_print(me, line);
		}
		else
		{
			commands_print(me, "Usage: rcbot readusers add <player name> <access level>");
		}
	}
	else if (equal(g_ca[0], "removeuser"))
	{
		if (g_ca[1][0])
		{
			new target = g_ca[1][0] == '#' ? commands_player_by_user_id(str_to_num(g_ca[1][1])) : commands_find_player(g_ca[1]);
			if (target == 0)
			{
				commands_print(me, "Player not found!!");
				return CVAR_ERROR;
			}
			if (target == me)
			{
				commands_print(me, "Cannot remove yourself!!!");
				return CVAR_ERROR;
			}
			if (!g_cl_i[target][CL_USED])
			{
				commands_print(me, "Player not found!!");
				return CVAR_ERROR;
			}
			get_user_name(target, name, charsmax(name));
			users_remove_player(name, g_cl_pass[target], g_cl_steam[target]);
			formatex(line, charsmax(line), "Removed %s's access", name);
			commands_print(me, line);
		}
		else
		{
			commands_print(me, "Usage: rcbot readusers add <player name>");
		}
	}
	else if (equal(g_ca[0], "showusers"))
	{
		users_show(me);
	}
	else
	{
		commands_print(me, "Error: No command issued or command does not exist!");
		commands_help(CMD_USERS, me);
		return CVAR_ERROR;
	}
	return CVAR_ACCESSED;
}

// Run::debug_ent (CDebugEntCommand::action).
static commands_debug_ent(me)
{
	if (equal(g_ca[0], "off"))
	{
		if (me)
		{
			g_cl_i[me][CL_DEBUG_ENT] = 0;
		}
	}
	else if (me)
	{
		new e = commands_facing_entity(me, true);
		if (e != -1)
		{
			g_cl_i[me][CL_DEBUG_ENT] = e;
		}
		else
		{
			commands_print(me, "No entity found");
		}
	}
	return CVAR_ACCESSED;
}

// Run::debug_bot (CDebugBotCommand::action). Without a client nothing happens (upstream
// dereferences it).
static commands_debug_bot(me)
{
	if (me == 0)
	{
		return CVAR_ACCESSED;
	}
	if (equal(g_ca[0], "bot"))
	{
		if (!g_ca[1][0])
		{
			commands_help(CMD_DEBUG_BOT, me);
			return CVAR_ERROR;
		}
		new p = commands_find_player(g_ca[1]);
		new idx = p && is_rcbot(p) ? p - 1 : -1;
		g_cl_i[me][CL_DEBUG_BOT] = idx;
		if (idx == -1)
		{
			commands_print(me, "Bot not found (not found/ not an RCBot/ or is a human player)");
			return CVAR_ERROR;
		}
	}
	else if (equal(g_ca[0], "facing"))
	{
		new e = commands_facing_entity(me, false);
		if (e != -1)
		{
			new idx = e >= 1 && e <= MAX_PLAYERS && is_rcbot(e) ? e - 1 : -1;
			g_cl_i[me][CL_DEBUG_BOT] = idx;
			if (idx == -1)
			{
				commands_print(me, "Facing entity is not a bot");
				return CVAR_ERROR;
			}
		}
		else
		{
			commands_print(me, "No entity found");
		}
	}
	else if (equal(g_ca[0], "off"))
	{
		g_cl_i[me][CL_DEBUG_BOT] = -1;
	}
	return CVAR_ACCESSED;
}

// Run::debug (CDebugCommand::action): only from the server (the dedicated console, or the listen
// server's own player; bot_config.ini counts as the server).
static commands_debug(client, me)
{
	new bool:error = is_dedicated_server() ? client != 0 : (client != 0 && me != g_cl_listen_server);
	if (error)
	{
		commands_print(0, "You can only use this command on the SERVER");
		return CVAR_ERROR;
	}
	if (!g_ca[0][0] || !g_ca[1][0])
	{
		commands_print(0, "Usage: rcbot debug <option> <1 : on, 0 : off>");
		return CVAR_ERROR;
	}
	static const names[][] = {"touch", "think", "hear", "message", "block", "move", "aim", "nav", "see"};
	new level = 0;
	for (new k = 0; k < sizeof(names); k++)
	{
		if (equal(g_ca[0], names[k]))
		{
			level = 1 << k;
		}
	}
	new line[96];
	if (level == 0)
	{
		commands_print(0, "Debug level not found");
	}
	else if (str_to_num(g_ca[1]) > 0)
	{
		formatex(line, charsmax(line), "debugging ^"%s^" is now ON", g_ca[0]);
		commands_print(0, line);
		g_cfg_i[CFG_DEBUG_LEVELS] |= level;
	}
	else
	{
		formatex(line, charsmax(line), "debugging ^"%s^" is now OFF", g_ca[0]);
		commands_print(0, line);
		// debug message 0's m_pDebugMessage = nullptr has nothing to clear here.
		g_cfg_i[CFG_DEBUG_LEVELS] &= ~level;
	}
	return CVAR_ACCESSED;
}

// Run::config (CConfigCommand::action): the TS settings (NS's left out).
static commands_config(me)
{
	if (!g_ca[0][0])
	{
		commands_print(me, "Error: no sub-command issued");
		commands_help(CMD_CONFIG, me);
		return CVAR_ERROR;
	}
	static const flag_names[][] = {"ts_dont_steal_weapons", "ts_dont_pickup_weapons", "disable_bot_squads", "ts_kungfu",
		"disable_weapon_learning", "tooltips", "autowaypoint_humans", "allow_botcam", "balance_teams", "chat_dont_learn",
		"chat_reply_to_bots", "wait_for_orders", "reserve_bot_slots", "dont_shoot", "chatting", "real_mode", "commanding", "unstick",
		"bots_leave_and_join", "bots_wait_for_bots", "tactical_paths"};
	static const flag_bits[] = {CFG_TS_DONT_STEAL_WEAPONS, CFG_TS_DONT_PICKUP_WEAPONS, CFG_DISABLE_BOT_SQUADS, CFG_TS_KUNGFU,
		CFG_DISABLE_WEAPON_LEARN, CFG_TOOLTIPS, CFG_AUTOWAYPOINT_HUMANS, CFG_ENABLE_BOTCAM, CFG_BALANCE_TEAMS, CFG_CHAT_DONT_LEARN,
		CFG_CHAT_REPLY_TO_BOTS, CFG_WAIT_FOR_ORDERS, CFG_RESERVE_BOT_SLOTS, CFG_DONT_SHOOT, CFG_CHATTING, CFG_REAL_MODE, CFG_COMMANDING,
		CFG_UNSTICK, CFG_BOTS_LEAVE_AND_JOIN, CFG_BOTS_WAIT_FOR_BOTS, CFG_TACTICAL_PATHS};
	new bool:setting = g_ca[1][0] != 0;
	new st = str_to_num(g_ca[1]);
	new Float:value = 0.0;
	new bool:success = true;
	new flag = 0;
	for (new k = 0; k < sizeof(flag_names); k++)
	{
		if (equal(g_ca[0], flag_names[k]))
		{
			flag = flag_bits[k];
			break;
		}
	}
	if (flag == 0)
	{
		if (equal(g_ca[0], "turnspeed"))
		{
			value = g_cfg_f[CFG_TURN_SPEED];
			if (setting)
			{
				new Float:speed = str_to_float(g_ca[1]);
				if (speed > 0.0)
				{
					// SetBotsTurnSpeed: the bots take the configured speed every frame anyway.
					g_cfg_f[CFG_TURN_SPEED] = speed;
					value = speed;
				}
				else
				{
					commands_print(me, "Error, must be > 0");
				}
			}
		}
		else if (equal(g_ca[0], "force_team"))
		{
			if (setting)
			{
				g_cfg_i[CFG_FORCE_TEAM] = str_to_num(g_ca[1]);
			}
			else
			{
				value = float(g_cfg_i[CFG_FORCE_TEAM]);
			}
		}
		else if (equal(g_ca[0], "bot_stuck_speed"))
		{
			if (setting)
			{
				g_cfg_f[CFG_BOT_STUCK_SPEED] = str_to_float(g_ca[1]);
			}
			else
			{
				value = g_cfg_f[CFG_BOT_STUCK_SPEED];
			}
		}
		else if (equal(g_ca[0], "update_ladder_time"))
		{
			if (setting)
			{
				g_cfg_f[CFG_UPDATE_LADDER_TIME] = str_to_float(g_ca[1]);
			}
			else
			{
				value = g_cfg_f[CFG_UPDATE_LADDER_TIME];
			}
		}
		else if (equal(g_ca[0], "min_bots") || equal(g_ca[0], "max_bots") || equal(g_ca[0], "chat_percent")
			|| equal(g_ca[0], "chat_reply_percent"))
		{
			new Cfg_I:field = equal(g_ca[0], "min_bots") ? CFG_MIN_BOTS
				: (equal(g_ca[0], "max_bots") ? CFG_MAX_BOTS : (equal(g_ca[0], "chat_percent") ? CFG_CHAT_PERCENT : CFG_CHAT_REPLY_PERCENT));
			if (setting)
			{
				// The fields are i16 upstream.
				g_cfg_i[field] = commands_i16(str_to_num(g_ca[1]));
			}
			else
			{
				value = float(g_cfg_i[field]);
			}
		}
		else if (equal(g_ca[0], "max_path_revs") || equal(g_ca[0], "max_update_vision_revs"))
		{
			new bool:path = equal(g_ca[0], "max_path_revs");
			if (setting)
			{
				new revs = str_to_num(g_ca[1]);
				success = revs > 0;
				if (success)
				{
					g_cfg_i[path ? CFG_MAX_PATH_REVS : CFG_MAX_VIS_UPDATE_REVS] = revs;
					for (new b = 1; b <= MAX_PLAYERS; b++)
					{
						if (g_co_i[b][CO_USED])
						{
							g_pr_i[b][path ? PR_PATH_REVS : PR_VISION_REVS] = revs;
						}
					}
				}
			}
			else
			{
				value = float(g_cfg_i[path ? CFG_MAX_PATH_REVS : CFG_MAX_VIS_UPDATE_REVS]);
			}
		}
		else if (equal(g_ca[0], "update_vision_time"))
		{
			if (setting)
			{
				new Float:t = str_to_float(g_ca[1]);
				success = t >= 0.0;
				if (success)
				{
					g_cfg_f[CFG_UPDATE_VIS_TIME] = t;
					for (new b = 1; b <= MAX_PLAYERS; b++)
					{
						if (g_co_i[b][CO_USED])
						{
							g_pr_f[b][PR_VISION_TIME] = t;
						}
					}
				}
			}
			else
			{
				value = g_cfg_f[CFG_UPDATE_VIS_TIME];
			}
		}
		else
		{
			commands_print(me, "Error: Command does not exist/command not issued!");
			commands_print(me, "-------------------------------------------------");
			commands_help(CMD_CONFIG, me);
			return CVAR_ACCESSED;
		}
	}
	new text[128];
	if (flag != 0)
	{
		if (setting)
		{
			config_change_setting(flag, st);
			formatex(text, charsmax(text), "%s is now %d", g_ca[0], st);
		}
		else
		{
			formatex(text, charsmax(text), "^"%s^" is currently ^"%d^"", g_ca[0], config_is_on(flag) ? 1 : 0);
		}
	}
	else if (!setting)
	{
		formatex(text, charsmax(text), "^"%s^" is currently ^"%.2f^"", g_ca[0], value);
	}
	else if (success)
	{
		formatex(text, charsmax(text), "%s set to %s", g_ca[0], g_ca[1]);
	}
	else
	{
		copy(text, charsmax(text), "Error with command, wrong argument");
	}
	commands_print(me, text);
	return CVAR_ACCESSED;
}

// atoi(...) as i16 (min_bots, max_bots and the chat percentages are shorts).
static commands_i16(v)
{
	v &= 0xFFFF;
	return v >= 0x8000 ? v - 0x10000 : v;
}

// Run::squad (CBotSquadCommand::action).
static commands_squad(me)
{
	if (me == 0 || !g_ca[0][0] || !g_ca[1][0])
	{
		return CVAR_ERROR;
	}
	new sq = squad_find_by_leader(me);
	if (sq == -1)
	{
		commands_print(me, "You are not leader of a squad^n");
		return CVAR_ERROR;
	}
	if (equal(g_ca[0], "form"))
	{
		// Upstream's "vee" sets the wedge; kept.
		static const names[][] = {"line", "vee", "wedge", "column", "echleft", "echright"};
		static const forms[] = {FORM_LINE, FORM_WEDGE, FORM_WEDGE, FORM_COLUMN, FORM_ECH_LEFT, FORM_ECH_RIGHT};
		static const says[][] = {"LINE", "VEE", "WEDGE", "COLUMN", "ECHELON LEFT", "ECHELON RIGHT"};
		for (new k = 0; k < sizeof(names); k++)
		{
			if (equal(g_ca[1], names[k]))
			{
				squad_change_formation(sq, forms[k]);
				new line[64];
				formatex(line, charsmax(line), "Squad formation is now %s^n", says[k]);
				commands_print(me, line);
				break;
			}
		}
	}
	else if (equal(g_ca[0], "spread"))
	{
		new Float:spread = str_to_float(g_ca[1]);
		if (spread != 0.0)
		{
			squad_change_spread(sq, spread);
			commands_print(me, "Squad spread changed^n");
		}
	}
	return CVAR_ACCESSED;
}

// Run::path_waypoint (CPathWaypointCommand::action).
static commands_path_waypoint(client, me)
{
	if (client == 0)
	{
		commands_print(0, "You can only use this command on the client");
		return CVAR_ERROR;
	}
	if (equal(g_ca[0], "on"))
	{
		g_cl_i[client][CL_PATHS_ON] = 1;
		commands_print(me, "Waypoint paths are now being shown");
	}
	else if (equal(g_ca[0], "off"))
	{
		g_cl_i[client][CL_PATHS_ON] = 0;
		commands_print(me, "Waypoints paths are not being shown");
	}
	else if (equal(g_ca[0], "create", 6) || equal(g_ca[0], "remove", 6))
	{
		g_cl_i[client][CL_PATHS_ON] = 1;
		new n = g_ca[0][6] == '1' ? 1 : (g_ca[0][6] == '2' ? 2 : 0);
		if (n != 0 && me)
		{
			new Float:origin[3];
			pev(me, pev_origin, origin);
			if (g_ca[0][0] == 'c')
			{
				nav_edit_create_path(me, origin, n);
			}
			else
			{
				nav_edit_remove_path(me, origin, n);
			}
			clients_send_nav_events(me);
		}
	}
	else if (equal(g_ca[0], "enable"))
	{
		g_cfg_i[CFG_AUTO_PATH_WAYPOINT] = 1;
		commands_print(me, "automatic path generation is now enabled");
	}
	else if (equal(g_ca[0], "disable"))
	{
		g_cfg_i[CFG_AUTO_PATH_WAYPOINT] = 0;
		commands_print(me, "automatic path generation is now disabled");
	}
	else
	{
		commands_print(me, "Error: You must issue a command!");
		commands_print(me, "--------------------------------");
		commands_help(CMD_PATHWAYPOINT, me);
	}
	return CVAR_ACCESSED;
}

// Run::auto_waypoint (CAutoWaypointCommand::action).
static commands_auto_waypoint(me)
{
	if (me == 0 && !g_ca[0][0])
	{
		commands_print(0, "You must issue a player name");
		return CVAR_ERROR;
	}
	new p = g_ca[0][0] ? commands_find_player(g_ca[0]) : me;
	if (p == 0)
	{
		commands_print(me, "Player not found");
		return CVAR_ERROR;
	}
	if (!g_cl_i[p][CL_USED])
	{
		return CVAR_ACCESSED;
	}
	new st = 0;
	if (g_ca[1][0])
	{
		st = str_to_num(g_ca[1]);
		clients_set_auto_waypoint(p, st);
	}
	else
	{
		clients_set_auto_waypoint(p, -1);
	}
	new on = g_cl_i[p][CL_AUTO_WPT] ? 1 : 0;
	new name[RC_NAME_LEN], line[96];
	get_user_name(p, name, charsmax(name));
	if (me && st == 2)
	{
		static const text[] = "autowaypoint set to 2 (debug) on you, you will see beams which are check points";
		commands_print(me, text);
		if (me != p)
		{
			commands_print(p, text);
		}
	}
	else
	{
		formatex(line, charsmax(line), "autowaypoint set to %d on %s", on, name);
		commands_print(me, line);
	}
	return CVAR_ACCESSED;
}

// Run::botcam (CBotCamCommand::action).
static commands_botcam(me)
{
	if (me == 0)
	{
		commands_print(0, "Can't use botcam on dedicated server");
		return CVAR_ERROR;
	}
	if (equal(g_ca[0], "off"))
	{
		// CBotCam::TuneOff.
		attach_view(me, me);
		commands_print(me, "Finished using botcam");
	}
	else if (botcam_tune_in(me))
	{
		// The BotcamOn tooltip (AddNewToolTip) does nothing on the port either.
		commands_print(me, "Using botcam");
	}
	else
	{
		commands_print(me, "Camera never worked or is not enabled (see command ^"rcbot config allow_botcam^")");
	}
	return CVAR_ACCESSED;
}

// Run::help_command (CHelpCommand::action).
static commands_help_command(me)
{
	if (!g_ca[0][0])
	{
		commands_print(me, "No command specified for help");
		commands_print(me, "Usage: rcbot help <command name>");
		return CVAR_ERROR;
	}
	new cmd = commands_lookup(g_ca[0]);
	if (cmd != -1)
	{
		commands_help(cmd, me);
	}
	else
	{
		commands_print(me, "command specified does not exist!^nAvailable commands are");
		commands_print_list(me);
	}
	return CVAR_ACCESSED;
}

// Run::waypoint (CWaypointCommand::action). waypoint save's WhichBot (NS) and GraveBot
// converters aren't ported (the standalone plugin has none either).
static commands_waypoint(client, me)
{
	if (client == 0)
	{
		commands_print(0, "This command can only be used on the client");
		return CVAR_ERROR;
	}
	new Float:origin[3];
	if (me)
	{
		pev(me, pev_origin, origin);
	}
	if (equal(g_ca[0], "on") || equal(g_ca[0], "off"))
	{
		new bool:on = equal(g_ca[0], "on");
		g_cl_i[client][CL_WAYPOINT_ON] = on;
		commands_print(me, on ? "Waypoints are now being shown" : "Waypoints are not being shown");
	}
	else if (equal(g_ca[0], "load"))
	{
		navigate_load_map();
		commands_print(me, "Waypoints have been loaded");
	}
	else if (equal(g_ca[0], "save"))
	{
		if (nav_save())
		{
			commands_print(me, "Waypoints have been saved");
		}
		else
		{
			commands_print(me, "ERROR: CANNOT save waypoints! (File cannot be opened for writing : missing directory/bad permissions)");
		}
	}
	else if (equal(g_ca[0], "add"))
	{
		g_cl_i[client][CL_WAYPOINT_ON] = 1;
		if (me)
		{
			nav_add(origin, 0, me, true, g_cfg_i[CFG_AUTO_PATH_WAYPOINT] != 0, true);
			clients_send_nav_events(me);
		}
	}
	else if (equal(g_ca[0], "delete"))
	{
		g_cl_i[client][CL_WAYPOINT_ON] = 1;
		if (me)
		{
			nav_delete_near(origin);
			clients_send_nav_events(me);
		}
	}
	else if (equal(g_ca[0], "info"))
	{
		if (me)
		{
			commands_waypoint_info(me, origin);
		}
	}
	else if (equal(g_ca[0], "teleport"))
	{
		if (g_ca[1][0] && me)
		{
			new Float:to[3];
			nav_origin(str_to_num(g_ca[1]), to);
			engfunc(EngFunc_SetOrigin, me, to);
		}
	}
	else if (equal(g_ca[0], "flag") || equal(g_ca[0], "setteam"))
	{
		return commands_waypoint_flag(me, origin);
	}
	else
	{
		commands_print(me, "Error: No command issued or command does not exist!");
		commands_help(CMD_WAYPOINT, me);
		return CVAR_ERROR;
	}
	return CVAR_ACCESSED;
}

// Effect::WaypointPrintInfo (WaypointPrintInfo): the waypoint within 50 units, its flags (and
// their names) and its paths.
static commands_waypoint_info(me, const Float:origin[3])
{
	new i = commands_nearest_waypoint(origin, 50.0, false);
	if (i == -1)
	{
		commands_print(me, "No waypoint nearby");
		return;
	}
	static const bits[] = {NAV_FL_TEAM_SPECIFIC, NAV_FL_CROUCH, NAV_FL_LADDER, NAV_FL_LIFT, NAV_FL_DOOR, NAV_FL_HEALTH, NAV_FL_ARMOR,
		NAV_FL_AMMO, NAV_FL_CHECK_LIFT, NAV_FL_IMPORTANT, NAV_FL_RESCUE, NAV_FL_DEFEND_ZONE, NAV_FL_AIMING, NAV_FL_CROUCHJUMP,
		NAV_FL_WAIT_FOR_LIFT, NAV_FL_STUNT, NAV_FL_JUMP, NAV_FL_WEAPON, NAV_FL_TELEPORT, NAV_FL_TANK, NAV_FL_FLY, NAV_FL_STAY_NEAR,
		NAV_FL_ENDLEVEL, NAV_FL_OPENS_LATER, NAV_FL_HUMAN_TOWER, NAV_FL_UNREACHABLE, NAV_FL_PUSHABLE, NAV_FL_GREN_THROW};
	static const names[][] = {"team specific", "crouch", "ladder", "lift", "door", "health", "armour", "ammo", "check lift",
		"important", "rescue", "defend zone", "aiming", "crouch jump", "wait for lift", "stunt", "jump", "weapon", "teleport", "tank",
		"fly", "stay near", "end level", "opens later", "human tower", "unreachable", "pushable", "grenade throw"};
	new f = nav_flags(i);
	static line[512];
	new n = formatex(line, charsmax(line), "Waypoint %d flags 0x%x (", i, f);
	new bool:first = true;
	for (new k = 0; k < sizeof(bits); k++)
	{
		if (f & bits[k])
		{
			n += formatex(line[n], charsmax(line) - n, "%s%s", first ? "" : ", ", names[k]);
			first = false;
		}
	}
	n += formatex(line[n], charsmax(line) - n, ") paths [");
	new out[64];
	new m = nav_paths_out(i, out, sizeof(out));
	for (new k = 0; k < m && k < sizeof(out); k++)
	{
		n += formatex(line[n], charsmax(line) - n, "%s%d", k ? ", " : "", out[k]);
	}
	formatex(line[n], charsmax(line) - n, "]");
	commands_print(me, line);
}

// Run::waypoint_flag: waypoint flag <name> and waypoint setteam <1-4|clear> on the waypoint
// within 50 units.
static commands_waypoint_flag(me, const Float:origin[3])
{
	new wpt = commands_nearest_waypoint(origin, 50.0, true);
	if (wpt == -1)
	{
		commands_print(me, "Error: You are not standing close enough to a waypoint!");
		return CVAR_ERROR;
	}
	new f = nav_flags(wpt);
	new line[96];
	if (equal(g_ca[0], "setteam"))
	{
		if (!g_ca[1][0])
		{
			commands_print(me, "Usage: rcbot waypoint setteam <1-4|clear>");
			commands_print(me, "  1-4 = assign to team (SI: 1=MCL, 2=AFD)");
			commands_print(me, "  clear = remove team restriction");
			return CVAR_ERROR;
		}
		if (equal(g_ca[1], "clear"))
		{
			nav_set_flags(wpt, f & ~NAV_FL_TEAM & ~NAV_FL_TEAM_SPECIFIC);
			formatex(line, charsmax(line), "Cleared team restriction on waypoint %d", wpt);
			commands_print(me, line);
		}
		else
		{
			new team = str_to_num(g_ca[1]);
			if (team < 1 || team > 4)
			{
				commands_print(me, "Error: Team must be 1-4 or 'clear'");
				return CVAR_ERROR;
			}
			nav_set_flags(wpt, (f & ~NAV_FL_TEAM) | (team - 1) | NAV_FL_TEAM_SPECIFIC);
			formatex(line, charsmax(line), "Set waypoint %d to team %d", wpt, team);
			commands_print(me, line);
		}
		return CVAR_ACCESSED;
	}
	if (!g_ca[1][0])
	{
		commands_print(me, "Usage: rcbot waypoint flag <flagname>");
		commands_print(me, "Flags: jump, crouch, crouchjump, lift, wallstick, fly,");
		commands_print(me, "       teleport, tank, waitlift, endlevel, stayclose,");
		commands_print(me, "       openslater, humantower, unreachable, ladder,");
		commands_print(me, "       important, scientist, barney (or gman),");
		commands_print(me, "       pushable, checklift, defend");
		return CVAR_ERROR;
	}
	// W_FL_WALL_STICK is TS's stunt bit; W_FL_SCIENTIST_POINT and W_FL_BARNEY_POINT share bits 11
	// and 12.
	static const names[][] = {"jump", "crouch", "crouchjump", "health", "armor", "armour", "lift", "wallstick", "fly", "teleport",
		"tank", "waitlift", "endlevel", "stayclose", "openslater", "humantower", "unreachable", "ladder", "important", "scientist",
		"barney", "gman", "pushable", "checklift", "defend"};
	static const bits[] = {NAV_FL_JUMP, NAV_FL_CROUCH, NAV_FL_CROUCHJUMP, NAV_FL_HEALTH, NAV_FL_ARMOR, NAV_FL_ARMOR, NAV_FL_LIFT,
		NAV_FL_STUNT, NAV_FL_FLY, NAV_FL_TELEPORT, NAV_FL_TANK, NAV_FL_WAIT_FOR_LIFT, NAV_FL_ENDLEVEL, NAV_FL_STAY_NEAR,
		NAV_FL_OPENS_LATER, NAV_FL_HUMAN_TOWER, NAV_FL_UNREACHABLE, NAV_FL_LADDER, NAV_FL_IMPORTANT, NAV_FL_IMPORTANT, NAV_FL_RESCUE,
		NAV_FL_RESCUE, NAV_FL_PUSHABLE, NAV_FL_CHECK_LIFT, NAV_FL_DEFEND_ZONE};
	new b = 0;
	for (new k = 0; k < sizeof(names); k++)
	{
		if (equal(g_ca[1], names[k]))
		{
			b = bits[k];
			break;
		}
	}
	if (b == 0)
	{
		formatex(line, charsmax(line), "Error: Unknown flag '%s'", g_ca[1]);
		commands_print(me, line);
		return CVAR_ERROR;
	}
	if (f & b)
	{
		nav_set_flags(wpt, f & ~b);
		formatex(line, charsmax(line), "Removed flag '%s' from waypoint %d", g_ca[1], wpt);
	}
	else
	{
		nav_set_flags(wpt, f | b);
		formatex(line, charsmax(line), "Added flag '%s' to waypoint %d", g_ca[1], wpt);
	}
	commands_print(me, line);
	return CVAR_ACCESSED;
}

// ---- force grip (host bots.rs force_grip_frame, release_force_grip; Effect::ForceGrip) ---------

// +rcbot_force_grip grips the player or monster the player faces (or nothing);
// -rcbot_force_grip lets go.
static commands_force_grip(player, bool:grip)
{
	if (player == 0)
	{
		return;
	}
	if (grip)
	{
		new e = commands_facing_entity(player, false);
		g_cl_i[player][CL_GRIP] = e > 0 ? e : 0;
	}
	else if (g_cl_i[player][CL_GRIP])
	{
		commands_release_grip(g_cl_i[player][CL_GRIP]);
		g_cl_i[player][CL_GRIP] = 0;
	}
}

// release_force_grip: FL_FROZEN off and DROP_TO_FLOOR.
static commands_release_grip(e)
{
	if (pev_valid(e))
	{
		set_pev(e, pev_flags, pev(e, pev_flags) & ~FL_FROZEN);
		engfunc(EngFunc_DropToFloor, e);
	}
}

// bots.rs force_grip_frame (CClient::Think's force grip): each gripped entity is held 210 units
// in front of its player's eyes (or short of what is in the way), turned to face the player, and
// frozen. Every frame: players without a grip stop at a cell read.
stock commands_force_grip_frame()
{
	for (new p = 1; p <= MAX_PLAYERS; p++)
	{
		new g = g_cl_i[p][CL_GRIP];
		if (!g)
		{
			continue;
		}
		if (!is_user_connected(p) || !pev_valid(g))
		{
			g_cl_i[p][CL_GRIP] = 0;
			continue;
		}
		new Float:ang[3], Float:fwd[3], Float:right[3], Float:src[3], Float:end[3], Float:tr_end[3], hit;
		pev(p, pev_v_angle, ang);
		make_vectors(ang, fwd, right);
		commands_eyes(p, src);
		vec_mad(src, fwd, 210.0, end);
		if (world_trace_line(src, end, true, false, p, tr_end, hit) < 1.0)
		{
			new Float:comp[3], Float:size[3];
			vec_sub(tr_end, src, comp);
			new Float:dist = vec_length(comp);
			pev(g, pev_size, size);
			if (dist > 0.0)
			{
				vec_mad(src, comp, (1.0 / dist) * (dist - vec_length_2d(size)), end);
			}
		}
		engfunc(EngFunc_SetOrigin, g, end);
		new Float:to_player[3], Float:angles[3], Float:gv[3];
		vec_sub(src, end, to_player);
		vec_to_angles(to_player, angles);
		fix_angles(angles);
		angles[2] = 0.0;
		pev(g, pev_v_angle, gv);
		angles[0] = gv[0];
		set_pev(g, pev_v_angle, angles);
		angles[0] = angles[0] / 3.0;
		set_pev(g, pev_angles, angles);
		set_pev(g, pev_flags, pev(g, pev_flags) | FL_FROZEN);
	}
}

// bots.rs disconnect: a leaving player's grip ends, and a grip on it (it would follow the next
// player in the slot); neither is released, as the host's.
stock commands_force_grip_release_player(player)
{
	if (player < 1 || player > MAX_PLAYERS)
	{
		return;
	}
	g_cl_i[player][CL_GRIP] = 0;
	for (new p = 1; p <= MAX_PLAYERS; p++)
	{
		if (g_cl_i[p][CL_GRIP] == player)
		{
			g_cl_i[p][CL_GRIP] = 0;
		}
	}
}
