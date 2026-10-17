// rcbot/hearing.inc: what the bot hears (port addition, design §4).
// Owner: movement. Ports bot/hearing.rs (Heard, Hearing add / tick / iter / investigate / newest_new; HEARD_FOR, SAME_SOUND, MAX_SOUNDS, LOUD_STEPS, LOUD_GUNFIRE, GUNFIRE_RANGE, SILENCED_RANGE), events.rs SoundKind::of_sample / is_heard and Bot::hear_sound, think.rs Bot::hear_gunfire, manager.rs sound / weapon_fired's per-bot part.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. The sounds are state.inc's g_hr_* (slot k of g_hr_count[id], oldest first);
// HEARD_FOR, LOUD_*, GUNFIRE_RANGE and SILENCED_RANGE are consts.inc's (goals reads them).
//
// Upstream turned an enemy's footsteps into a LISTEN_TO_SOUND task without an entity, which
// failed at once, and heard gunfire by polling for IN_ATTACK within 750 units; the port keeps the
// sounds for a few seconds (where, when, how loud, from whom) for the goal chooser, and hears each
// shot (ts_weapon_fired).

#if defined _rcbot_hearing_included
	#endinput
#endif
#define _rcbot_hearing_included

// hearing.rs SAME_SOUND: sounds this near each other (or from the same player) are one.
#define HEARING_SAME_SOUND		200.0

// hearing.rs Hearing::default (a new bot).
stock hearing_bot_init(id)
{
	g_hr_count[id] = 0;
}

// events.rs SoundKind::of_sample filtered by SoundKind::is_heard: SOUND_PLAYER ("player/...")
// or SOUND_PLAYER_PAIN ("player/pain..."); every other kind (doors/, plats/, weapons/, buttons/,
// the rest) is SOUND_UNKNOWN, since no bot reacts to it. Runs per game sound: one prefix check
// stops most samples.
stock hearing_sound_kind(const sample[])
{
	if (sample[0] != 'p' || !equal(sample, "player/", 7))
	{
		return SOUND_UNKNOWN;
	}
	return equal(sample[7], "pain", 4) ? SOUND_PLAYER_PAIN : SOUND_PLAYER;
}

// An EntRef's serial (state.inc): a player's user id, nothing for another entity.
static stock hr_serial(ent)
{
	return ent >= 1 && ent <= MAX_PLAYERS ? world_userid(ent) : 0;
}

// events.rs Bot::hear_sound: a player's pain from the current task's entity sets
// COND_TASK_EDICT_PAIN; an enemy's steps, with no enemy and no human tower going, are
// remembered and glanced at (preaim's watch, 0.8 s). Teammates' sounds are ignored.
stock hearing_hear_sound(id, kind, ent, const Float:origin[3])
{
	new serial = hr_serial(ent);
	if (kind == SOUND_PLAYER_PAIN)
	{
		if (g_tk_count[id] > 0 && ent != 0 && g_tk_i[id][0][TK_EDICT] == ent && g_tk_i[id][0][TK_EDICT_SERIAL] == serial)
		{
			// The conditions are think's field; events.rs sets this one here.
			g_th_i[id][TH_CONDITIONS] |= COND_TASK_EDICT_PAIN;
		}
	}
	else if (kind == SOUND_PLAYER)
	{
		if (ent != 0 && combat_is_enemy(id, ent) && g_cb_i[id][CB_ENEMY] == 0 && tasks_current_kind(id) != TASK_HUMAN_TOWER)
		{
			hearing_add(id, origin, LOUD_STEPS, ent, serial);
			preaim_watch(id, origin, 0.8);
		}
	}
}

// think.rs Bot::hear_gunfire (UTIL_UpdateSounds, from ts_weapon_fired): an enemy's shot from
// origin is heard within GUNFIRE_RANGE, SILENCED_RANGE with a silencer (upstream: the nearest
// player within 750 units, teammates too; design §4), and remembered.
stock hearing_hear_gunfire(id, shooter, const Float:origin[3], bool:silenced)
{
	if (shooter == id || !bot_alive(id))
	{
		return;
	}
	new Float:me[3];
	world_origin(id, me);
	if (vec_distance(me, origin) >= (silenced ? SILENCED_RANGE : GUNFIRE_RANGE) || !combat_is_enemy(id, shooter))
	{
		return;
	}
	hearing_add(id, origin, LOUD_GUNFIRE, shooter, hr_serial(shooter));
}

// hearing.rs Hearing::add: the same player's sound, or one near an earlier one, updates that one
// (somewhere new: worth a look again); else a new one, the oldest dropped beyond RC_MAX_SOUNDS.
// source 0: nobody.
stock hearing_add(id, const Float:origin[3], Float:loud, source, source_serial)
{
	new Float:now = g_now;
	new n = g_hr_count[id];
	for (new k = 0; k < n; k++)
	{
		new Float:d = vec_distance(g_hr_origin[id][k], origin);
		new bool:same_source = source != 0 && g_hr_i[id][k][HR_SOURCE] == source && g_hr_i[id][k][HR_SOURCE_SERIAL] == source_serial;
		if (same_source || d < HEARING_SAME_SOUND)
		{
			if (d >= HEARING_SAME_SOUND)
			{
				g_hr_i[id][k][HR_INVESTIGATED] = 0;
			}
			vec_copy(origin, g_hr_origin[id][k]);
			g_hr_f[id][k][HR_TIME] = now;
			g_hr_f[id][k][HR_LOUD] = fmax(g_hr_f[id][k][HR_LOUD], loud);
			return;
		}
	}
	if (n >= RC_MAX_SOUNDS)
	{
		hr_remove(id, 0);
		n--;
	}
	g_hr_i[id][n][HR_SOURCE] = source;
	g_hr_i[id][n][HR_SOURCE_SERIAL] = source ? source_serial : 0;
	g_hr_i[id][n][HR_INVESTIGATED] = 0;
	g_hr_f[id][n][HR_TIME] = now;
	g_hr_f[id][n][HR_LOUD] = loud;
	vec_copy(origin, g_hr_origin[id][n]);
	g_hr_count[id] = n + 1;
}

// Drops slot k, keeping the order.
static stock hr_remove(id, k)
{
	new n = g_hr_count[id];
	for (new j = k; j + 1 < n; j++)
	{
		for (new Hr_I:f; f < Hr_I; f++)
		{
			g_hr_i[id][j][f] = g_hr_i[id][j + 1][f];
		}
		for (new Hr_F:f; f < Hr_F; f++)
		{
			g_hr_f[id][j][f] = g_hr_f[id][j + 1][f];
		}
		vec_copy(g_hr_origin[id][j + 1], g_hr_origin[id][j]);
	}
	g_hr_count[id] = n - 1;
}

// hearing.rs Hearing::tick: forgets sounds older than HEARD_FOR (or from a later time: a new map).
stock hearing_tick(id)
{
	new Float:now = g_now;
	for (new k = g_hr_count[id] - 1; k >= 0; k--)
	{
		new Float:t = g_hr_f[id][k][HR_TIME];
		if (t > now || now - t > HEARD_FOR)
		{
			hr_remove(id, k);
		}
	}
}

// hearing.rs Hearing::investigate: the bot goes to see the sound at origin.
stock hearing_investigate(id, const Float:origin[3])
{
	for (new k = 0; k < g_hr_count[id]; k++)
	{
		if (vec_distance(g_hr_origin[id][k], origin) < HEARING_SAME_SOUND)
		{
			g_hr_i[id][k][HR_INVESTIGATED] = 1;
		}
	}
}

// hearing.rs Hearing::newest_new: the newest sound not investigated yet (the last of equal
// times, as max_by); its slot, -1 none.
stock hearing_newest_new(id)
{
	new best = -1;
	for (new k = 0; k < g_hr_count[id]; k++)
	{
		if (!g_hr_i[id][k][HR_INVESTIGATED] && (best == -1 || g_hr_f[id][k][HR_TIME] >= g_hr_f[id][best][HR_TIME]))
		{
			best = k;
		}
	}
	return best;
}
