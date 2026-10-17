// rcbot/remember.inc: where the bot last saw things (CRememberPositions).
// Owner: movement. Ports bot/remember.rs (RememberPosition, RememberPositions add / tick / hunted / forget / iter / any / newest / nearest / clear; MAX_REMEMBER_POSITIONS, REMEMBER_LOST_ENEMY, REMEMBER_FOR, SAME_PLACE).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. The positions are state.inc's g_rm_* (slot k of g_rm_count[id], oldest
// first); REMEMBER_LOST_ENEMY and REMEMBER_FOR are consts.inc's (combat reads them).
//
// Port changes (remember.rs, design §4): positions are stamped with the time and forgotten after
// REMEMBER_FOR seconds or when their player dies, at most 8 are kept, and positions without an
// entity are the same position only when they are near each other.

#if defined _rcbot_remember_included
	#endinput
#endif
#define _rcbot_remember_included

// remember.rs SAME_PLACE: positions without an entity nearer than this are the same position.
#define REMEMBER_SAME_PLACE		64.0

// RememberPositions::default (a new bot).
stock remember_bot_init(id)
{
	g_rm_count[id] = 0;
	g_rm_now[id] = 0.0;
}

// Drops slot k, keeping the order.
static stock rm_remove(id, k)
{
	new n = g_rm_count[id];
	for (new j = k; j + 1 < n; j++)
	{
		for (new Rm_I:f; f < Rm_I; f++)
		{
			g_rm_i[id][j][f] = g_rm_i[id][j + 1][f];
		}
		g_rm_time[id][j] = g_rm_time[id][j + 1];
		vec_copy(g_rm_origin[id][j + 1], g_rm_origin[id][j]);
		vec_copy(g_rm_visible_origin[id][j + 1], g_rm_visible_origin[id][j]);
	}
	g_rm_count[id] = n - 1;
}

// RememberPositions::forget (manager.rs death: a dead player isn't where it was seen).
stock remember_forget(id, ent, serial)
{
	for (new k = g_rm_count[id] - 1; k >= 0; k--)
	{
		if (g_rm_i[id][k][RM_ENT] == ent && g_rm_i[id][k][RM_SERIAL] == serial && ent != 0)
		{
			rm_remove(id, k);
		}
	}
}

// RememberPositions::add (addPosition): an earlier position of the same entity (or, without one,
// the same place) is replaced, and the oldest goes beyond RC_MAX_REMEMBER (upstream kept 9).
// ent 0: no entity.
stock remember_add(id, const Float:origin[3], ent, serial, flags, const Float:visible_origin[3])
{
	new n = g_rm_count[id];
	new same = -1;
	for (new k = 0; k < n; k++)
	{
		new e = g_rm_i[id][k][RM_ENT];
		if (e == 0 && ent == 0)
		{
			if (vec_distance(g_rm_origin[id][k], origin) < REMEMBER_SAME_PLACE)
			{
				same = k;
				break;
			}
		}
		else if (e == ent && g_rm_i[id][k][RM_SERIAL] == serial)
		{
			same = k;
			break;
		}
	}
	if (same != -1)
	{
		rm_remove(id, same);
	}
	else if (n >= RC_MAX_REMEMBER)
	{
		rm_remove(id, 0);
	}
	new k = g_rm_count[id];
	g_rm_i[id][k][RM_ENT] = ent;
	g_rm_i[id][k][RM_SERIAL] = ent ? serial : 0;
	g_rm_i[id][k][RM_FLAGS] = flags;
	g_rm_i[id][k][RM_HUNTED] = 0;
	g_rm_time[id][k] = g_rm_now[id];
	vec_copy(origin, g_rm_origin[id][k]);
	vec_copy(visible_origin, g_rm_visible_origin[id][k]);
	g_rm_count[id] = k + 1;
}

// RememberPositions::tick: the clock moves on to now; positions older than REMEMBER_FOR are
// forgotten (a clock that went back is a new map: everything goes).
stock remember_tick(id)
{
	new Float:now = g_now;
	if (now < g_rm_now[id])
	{
		g_rm_count[id] = 0;
	}
	g_rm_now[id] = now;
	for (new k = g_rm_count[id] - 1; k >= 0; k--)
	{
		if (now - g_rm_time[id][k] > REMEMBER_FOR)
		{
			rm_remove(id, k);
		}
	}
}

// RememberPositions::hunted: the bot goes after the position at origin.
stock remember_hunted(id, const Float:origin[3])
{
	for (new k = 0; k < g_rm_count[id]; k++)
	{
		if (g_rm_origin[id][k][0] == origin[0] && g_rm_origin[id][k][1] == origin[1] && g_rm_origin[id][k][2] == origin[2])
		{
			g_rm_i[id][k][RM_HUNTED] = 1;
		}
	}
}

// RememberPositions::any (gotPosition).
stock bool:remember_any(id)
{
	return g_rm_count[id] > 0;
}

// RememberPositions::newest (getNewest): its slot, -1 none.
stock remember_newest(id)
{
	return g_rm_count[id] - 1;
}

// RememberPositions::nearest (positionNearest): the least |p - origin| + |p - from|; its slot,
// -1 none.
stock remember_nearest(id, const Float:origin[3], const Float:from[3])
{
	new best = -1;
	new Float:best_d;
	for (new k = 0; k < g_rm_count[id]; k++)
	{
		new Float:d = vec_distance(g_rm_origin[id][k], origin) + vec_distance(g_rm_origin[id][k], from);
		if (best == -1 || d < best_d)
		{
			best = k;
			best_d = d;
		}
	}
	return best;
}

// RememberPositions::clear (a new life).
stock remember_clear(id)
{
	g_rm_count[id] = 0;
}
