// rcbot/users.inc: who may use the rcbot commands (bot_users.ini).
// Owner: social. Ports config/users.rs (AllowedPlayer new / steam_id_defined / is_for_steam_id / is_for_name / is_for_pass / is_for_client / show, AllowedPlayers add_player / remove_player / get_player / show_users / clear / all / read_users), host commands.rs read_users.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(users).

#if defined _rcbot_users_included
	#endinput
#endif
#define _rcbot_users_included

// TODO(users): users.rs AllowedPlayers::read_users over bot_users.ini. Called from plugin_cfg.
stock users_read()
{
}

// TODO(users): users.rs AllowedPlayers::get_player's access level for a client, 0 none.
stock users_access(player)
{
	#pragma unused player
	return 0;
}
