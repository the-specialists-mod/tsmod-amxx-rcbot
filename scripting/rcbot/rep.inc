// rcbot/rep.inc: the bots' reputations of players (CBotReputation(s), <id>.rcr).
// Owner: social. Ports config/rep.rs (Reputation to_bytes / from_bytes / describe, parse_rep_file, write_rep, rep_file_name, Reputations all / clear / get / add_rep / new_rep / get_create_rep / average_rep_on_server / client_rep / increase_rep / decrease_rep / random_client / print_reps / add_load_rep / remove_save_rep / save_all_rep, rep_array_num, decrease_rep_roll, increase_rep_roll, PlayerRepId, player_rep_id), host bots.rs new_bot / put_in_server / disconnect's .rcr handling.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(rep). State: state.inc's g_rep_*; client.inc's rep ids.

#if defined _rcbot_rep_included
	#endinput
#endif
#define _rcbot_rep_included

// TODO(rep): host bots.rs new_bot: AddLoadRep for every player here (botprofiles/<id>.rcr).
stock rep_load_all(id)
{
	#pragma unused id
}

// TODO(rep): host bots.rs put_in_server: bot id loads its reputation of player.
stock rep_add_load(id, player)
{
	#pragma unused id, player
}

// TODO(rep): host bots.rs disconnect: bot id saves and drops its reputation of player.
stock rep_remove_save(id, player)
{
	#pragma unused id, player
}

// TODO(rep): Reputations::client_rep: the bot's rep of player (default 5).
stock rep_of(id, player)
{
	#pragma unused id, player
	return 5;
}

// TODO(rep): Reputations::increase_rep.
stock rep_increase(id, player)
{
	#pragma unused id, player
}

// TODO(rep): Reputations::decrease_rep.
stock rep_decrease(id, player)
{
	#pragma unused id, player
}

// TODO(rep): Reputations::average_rep_on_server.
stock rep_average_on_server(id)
{
	#pragma unused id
	return 5;
}
