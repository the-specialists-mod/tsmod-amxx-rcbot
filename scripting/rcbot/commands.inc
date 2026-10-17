// rcbot/commands.inc: the rcbot server and client commands.
// Owner: social. Ports config/commands.rs (CommandId, the help, run / client_command / server_command / run_config, Run's action / add_bot / menu_select / set_pass / waypoint_menu / util / users / debug_ent / debug_bot / debug / config / squad / path_waypoint / auto_waypoint / botcam / help_command / waypoint / waypoint_flag, ts_team_choice), host commands.rs (server_command, client_command, Host: CommandHost, add_bot, view_trace, give_ts_weapon, explosion_create, radius_damage) and bots.rs force_grip_frame / release_force_grip.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: `rcbot addbot` works (backend_add_bot_now); every other sub-command is TODO(commands).

#if defined _rcbot_commands_included
	#endinput
#endif
#define _rcbot_commands_included

// lib.rs TSRS_PluginLoad's register_command(BOT_COMMAND_ACCESS): `rcbot` from the server
// console and from clients.
stock commands_init()
{
	register_srvcmd("rcbot", "commands_server_rcbot");
	register_clcmd("rcbot", "commands_client_rcbot");
}

// commands.rs server_command.
public commands_server_rcbot()
{
	new cmd[32];
	read_argv(1, cmd, charsmax(cmd));
	if (equali(cmd, "addbot"))
	{
		new team[8];
		read_argv(2, team, charsmax(team));
		backend_add_bot_now(str_to_num(team));
		return PLUGIN_HANDLED;
	}
	server_print("%s^"rcbot %s^" isn't ported to rcbot_amxx yet", RC_MSG_TAG, cmd);
	return PLUGIN_HANDLED;
}

// commands.rs client_command (with the users' access levels, bot_users.ini).
// TODO(commands): access checks (users.inc), menus (menu.inc), every sub-command.
public commands_client_rcbot(id)
{
	client_print(id, print_console, "%srcbot commands aren't ported to rcbot_amxx yet", RC_MSG_TAG);
	return PLUGIN_HANDLED;
}

// TODO(commands): commands.rs ts_team_choice: the team name (mp_teamlist entry) a new bot joins as its model in teamplay; force_team 1..4 or the config's (empty: let the port pick).
stock commands_ts_team_choice(force_team, model[], len)
{
	#pragma unused force_team, len
	model[0] = 0;
}

// TODO(commands): host bots.rs force_grip_frame (CClient::Think's force grip), every frame.
stock commands_force_grip_frame()
{
}

// TODO(commands): host bots.rs disconnect: a leaving player's grips end (release_force_grip).
stock commands_force_grip_release_player(player)
{
	#pragma unused player
}
