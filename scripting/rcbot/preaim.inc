// rcbot/preaim.inc: pre-aiming where an enemy may appear (port addition, design §4).
// Owner: combat. Ports bot/preaim.rs (PreAim, Bot::watch, pre_aim_target, update_pre_aim, pre_aim_look).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(preaim).

#if defined _rcbot_preaim_included
	#endinput
#endif
#define _rcbot_preaim_included

// TODO(preaim): preaim.rs PreAim::default (a new bot).
stock preaim_bot_init(id)
{
	g_pa_checked_count[id] = 0;
}

// TODO(preaim): preaim.rs Bot::watch: look at a point first for secs.
stock preaim_watch(id, const Float:at[3], Float:secs)
{
	#pragma unused id, at, secs
}

// TODO(preaim): preaim.rs Bot::pre_aim_target.
stock preaim_target(id, cur, next)
{
	#pragma unused id, cur, next
	return -1;
}

// TODO(preaim): preaim.rs Bot::update_pre_aim.
stock preaim_update(id)
{
	#pragma unused id
}

// TODO(preaim): preaim.rs Bot::pre_aim_look.
stock preaim_look(id)
{
	#pragma unused id
}
