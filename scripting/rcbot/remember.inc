// rcbot/remember.inc: where the bot last saw things (CRememberPositions).
// Owner: movement. Ports bot/remember.rs (RememberPosition, RememberPositions add / tick / hunted / forget / iter / any / newest / nearest / clear; MAX_REMEMBER_POSITIONS, REMEMBER_LOST_ENEMY, REMEMBER_FOR, SAME_PLACE).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(remember).

#if defined _rcbot_remember_included
	#endinput
#endif
#define _rcbot_remember_included

#define REMEMBER_LOST_ENEMY		(1<<1)
#define REMEMBER_FOR			15.0

// TODO(remember): RememberPositions::default (a new bot).
stock remember_bot_init(id)
{
	g_rm_count[id] = 0;
}

// TODO(remember): RememberPositions::forget (manager.rs death: a dead player isn't where it was seen).
stock remember_forget(id, ent, serial)
{
	#pragma unused id, ent, serial
}

// TODO(remember): RememberPositions::add (replaces by entity, keeps 8).
stock remember_add(id, const Float:origin[3], ent, serial, flags, const Float:visible_origin[3])
{
	#pragma unused id, origin, ent, serial, flags, visible_origin
}

// TODO(remember): RememberPositions::tick.
stock remember_tick(id)
{
	#pragma unused id
}

// TODO(remember): RememberPositions::hunted.
stock remember_hunted(id, const Float:origin[3])
{
	#pragma unused id, origin
}

// TODO(remember): RememberPositions::newest: its slot, -1 none.
stock remember_newest(id)
{
	#pragma unused id
	return -1;
}

// TODO(remember): RememberPositions::nearest: its slot, -1 none.
stock remember_nearest(id, const Float:origin[3], const Float:from[3])
{
	#pragma unused id, origin, from
	return -1;
}

// TODO(remember): RememberPositions::clear.
stock remember_clear(id)
{
	#pragma unused id
}
