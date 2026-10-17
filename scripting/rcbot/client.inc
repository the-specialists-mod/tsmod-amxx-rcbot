// rcbot/client.inc: per-player state RCBot keeps (CClient / CClients).
// Owner: social. Ports config/client.rs (PlayerId, ToolTip, Client is_used / pass / set_pass / has_cmd / toggle_cmd / set_auto_waypoint / has_tooltip_sent, Clients get / index_of / index_by_rep_id / connect / recheck_auth / disconnect), config/rep.rs player_rep_id (bot_playerids.ini), host bots.rs put_in_server / disconnect's client part and State::rep_ids.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: the rep id table is laid out; stubs, TODO(client).

#if defined _rcbot_client_included
	#endinput
#endif
#define _rcbot_client_included

// Each player's reputation id by index (State::rep_ids), NO_CLIENT for none.
stock g_client_rep_id[RC_SLOTS] = {NO_CLIENT, ...};
stock g_client_access[RC_SLOTS];

// TODO(client): Clients::connect (CClients::ClientConnected): the player's rep id from bot_playerids.ini (player_rep_id), access, the welcome message.
stock clients_connect(player)
{
	#pragma unused player
}

// TODO(client): Clients::disconnect.
stock clients_disconnect(player)
{
	#pragma unused player
}
