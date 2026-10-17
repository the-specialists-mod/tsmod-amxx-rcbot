// rcbot/board.inc: each team's shared board: sightings, sounds, claims, paths (port addition, design §4).
// Owner: movement. Ports bot/board.rs (Sighting, TeamSound, Claim, TeamBoard sight / hear / claim / post / leave / expire / forget / mates / paths_of_others, TeamBoards get / get_mut / died / clear; SIGHTING_FOR, SOUND_FOR, POST_FOR).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. Teams are the rules teams 1..BOARD_TEAMS; team 0 (deathmatch) has no board, so
// nobody shares. Upstream's bots shared nothing but squads, which TS bots never form on their own.
//
// This file is included after the files that read the board (goals, navigate), so they read it
// through the accessor stocks at the end, never the arrays.
//
// Layout: sightings and sounds are lists per team (oldest first). A bot's claim and its post
// (position, and its path: its RcPath, read live by board_mate_paths) are one per bot, tagged
// with the team they were made on. The Rust board copies the path at each post (at most POST_FOR
// old when read); the live path object is what it would hold now.

#if defined _rcbot_board_included
	#endinput
#endif
#define _rcbot_board_included

#define BOARD_TEAMS				4
#define BOARD_SOUND_FOR			8.0
#define BOARD_POST_FOR			2.0
// One sighting per enemy (sight replaces the enemy's last one): every entity a bot can fight
// fits in practice; past that the newest is dropped.
#define BOARD_MAX_SIGHTINGS		RC_SLOTS
#define BOARD_MAX_SOUNDS		16

// Sightings by team: who, where, when, by which bot.
new g_bd_sight_ent[BOARD_TEAMS][BOARD_MAX_SIGHTINGS];
new g_bd_sight_serial[BOARD_TEAMS][BOARD_MAX_SIGHTINGS];
new g_bd_sight_by[BOARD_TEAMS][BOARD_MAX_SIGHTINGS];
new Float:g_bd_sight_time[BOARD_TEAMS][BOARD_MAX_SIGHTINGS];
new Float:g_bd_sight_origin[BOARD_TEAMS][BOARD_MAX_SIGHTINGS][3];
new g_bd_sight_count[BOARD_TEAMS];
// Sounds by team.
new g_bd_sound_by[BOARD_TEAMS][BOARD_MAX_SOUNDS];
new Float:g_bd_sound_time[BOARD_TEAMS][BOARD_MAX_SOUNDS];
new Float:g_bd_sound_loud[BOARD_TEAMS][BOARD_MAX_SOUNDS];
new Float:g_bd_sound_origin[BOARD_TEAMS][BOARD_MAX_SOUNDS][3];
new g_bd_sound_count[BOARD_TEAMS];
// Claims and members' posts are per bot: the team (0 none), its goal and until when.
new g_bd_claim_team[RC_SLOTS];
new g_bd_claim_wpt[RC_SLOTS];
new g_bd_claim_item[RC_SLOTS];
new g_bd_claim_item_serial[RC_SLOTS];
new Float:g_bd_claim_until[RC_SLOTS];
new g_bd_post_team[RC_SLOTS];
new Float:g_bd_post_time[RC_SLOTS];
new Float:g_bd_post_at[RC_SLOTS][3];

// TeamBoards::get: the board index of a rules team, -1 none (team 0, deathmatch).
static stock bd_index(team)
{
	return team >= 1 && team <= BOARD_TEAMS ? team - 1 : -1;
}

static stock bd_remove_sighting(t, k)
{
	new n = g_bd_sight_count[t];
	for (new j = k; j + 1 < n; j++)
	{
		g_bd_sight_ent[t][j] = g_bd_sight_ent[t][j + 1];
		g_bd_sight_serial[t][j] = g_bd_sight_serial[t][j + 1];
		g_bd_sight_by[t][j] = g_bd_sight_by[t][j + 1];
		g_bd_sight_time[t][j] = g_bd_sight_time[t][j + 1];
		vec_copy(g_bd_sight_origin[t][j + 1], g_bd_sight_origin[t][j]);
	}
	g_bd_sight_count[t] = n - 1;
}

static stock bd_remove_sound(t, k)
{
	new n = g_bd_sound_count[t];
	for (new j = k; j + 1 < n; j++)
	{
		g_bd_sound_by[t][j] = g_bd_sound_by[t][j + 1];
		g_bd_sound_time[t][j] = g_bd_sound_time[t][j + 1];
		g_bd_sound_loud[t][j] = g_bd_sound_loud[t][j + 1];
		vec_copy(g_bd_sound_origin[t][j + 1], g_bd_sound_origin[t][j]);
	}
	g_bd_sound_count[t] = n - 1;
}

// TeamBoard::forget: an enemy that died isn't anywhere.
static stock bd_forget(t, enemy, serial)
{
	for (new k = g_bd_sight_count[t] - 1; k >= 0; k--)
	{
		if (g_bd_sight_ent[t][k] == enemy && g_bd_sight_serial[t][k] == serial)
		{
			bd_remove_sighting(t, k);
		}
	}
}

// TeamBoards::died: a player died: no board sights it any more, and a bot's posts go.
stock board_died(player, serial)
{
	for (new t = 0; t < BOARD_TEAMS; t++)
	{
		bd_forget(t, player, serial);
	}
	if (player >= 1 && player <= MAX_PLAYERS)
	{
		board_leave(player);
	}
}

// TeamBoards::clear (a new map; plugin_end).
stock board_clear()
{
	for (new t = 0; t < BOARD_TEAMS; t++)
	{
		g_bd_sight_count[t] = 0;
		g_bd_sound_count[t] = 0;
	}
	for (new b = 0; b < RC_SLOTS; b++)
	{
		g_bd_claim_team[b] = 0;
		g_bd_post_team[b] = 0;
	}
}

// TeamBoard::sight: a bot saw enemy at origin (its earlier sighting replaced).
stock board_sight(team, by, enemy, enemy_serial, const Float:origin[3])
{
	new t = bd_index(team);
	if (t == -1)
	{
		return;
	}
	bd_forget(t, enemy, enemy_serial);
	new k = g_bd_sight_count[t];
	if (k >= BOARD_MAX_SIGHTINGS)
	{
		rc_debug(DBG_THINK, "team %d board full: sighting of %d dropped", team, enemy);
		return;
	}
	g_bd_sight_ent[t][k] = enemy;
	g_bd_sight_serial[t][k] = enemy_serial;
	g_bd_sight_by[t][k] = by;
	g_bd_sight_time[t][k] = g_now;
	vec_copy(origin, g_bd_sight_origin[t][k]);
	g_bd_sight_count[t] = k + 1;
}

// TeamBoard::hear: a bot heard something at origin (a sound within 200 units is that one again).
stock board_hear(team, by, const Float:origin[3], Float:loud)
{
	new t = bd_index(team);
	if (t == -1)
	{
		return;
	}
	new n = g_bd_sound_count[t];
	for (new k = 0; k < n; k++)
	{
		if (vec_distance(g_bd_sound_origin[t][k], origin) < 200.0)
		{
			vec_copy(origin, g_bd_sound_origin[t][k]);
			g_bd_sound_time[t][k] = g_now;
			g_bd_sound_loud[t][k] = fmax(g_bd_sound_loud[t][k], loud);
			g_bd_sound_by[t][k] = by;
			return;
		}
	}
	if (n >= BOARD_MAX_SOUNDS)
	{
		rc_debug(DBG_HEAR, "team %d board full: sound dropped", team);
		return;
	}
	vec_copy(origin, g_bd_sound_origin[t][n]);
	g_bd_sound_time[t][n] = g_now;
	g_bd_sound_loud[t][n] = loud;
	g_bd_sound_by[t][n] = by;
	g_bd_sound_count[t] = n + 1;
}

// TeamBoard::claim: bot `by` goes for a waypoint (-1 none) or an item (0 none) until `until`, on
// its rules team's board (nothing outside teamplay).
stock board_claim(by, waypoint, item, item_serial, Float:until)
{
	if (by < 1 || by > MAX_PLAYERS)
	{
		return;
	}
	new team = world_team(by);
	if (bd_index(team) == -1)
	{
		return;
	}
	g_bd_claim_team[by] = team;
	g_bd_claim_wpt[by] = waypoint;
	g_bd_claim_item[by] = item;
	g_bd_claim_item_serial[by] = item ? item_serial : 0;
	g_bd_claim_until[by] = until;
}

// TeamBoard::post: bot `by`'s position (and path: its RcPath) now, on its rules team's board.
stock board_post(by, const Float:at[3])
{
	if (by < 1 || by > MAX_PLAYERS)
	{
		return;
	}
	new team = world_team(by);
	if (bd_index(team) == -1)
	{
		return;
	}
	g_bd_post_team[by] = team;
	g_bd_post_time[by] = g_now;
	vec_copy(at, g_bd_post_at[by]);
}

// TeamBoard::leave: a bot left the team (died, left, changed team): what it posted goes.
stock board_leave(by)
{
	if (by >= 0 && by < RC_SLOTS)
	{
		g_bd_claim_team[by] = 0;
		g_bd_post_team[by] = 0;
	}
}

// TeamBoard::expire: forgets what is old on the team's board.
stock board_expire(team)
{
	new t = bd_index(team);
	if (t == -1)
	{
		return;
	}
	new Float:now = g_now;
	for (new k = g_bd_sight_count[t] - 1; k >= 0; k--)
	{
		new Float:at = g_bd_sight_time[t][k];
		if (at > now || now - at > BOARD_SIGHTING_FOR)
		{
			bd_remove_sighting(t, k);
		}
	}
	for (new k = g_bd_sound_count[t] - 1; k >= 0; k--)
	{
		new Float:at = g_bd_sound_time[t][k];
		if (at > now || now - at > BOARD_SOUND_FOR)
		{
			bd_remove_sound(t, k);
		}
	}
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (g_bd_claim_team[b] == team && g_bd_claim_until[b] <= now)
		{
			g_bd_claim_team[b] = 0;
		}
		if (g_bd_post_team[b] == team)
		{
			new Float:at = g_bd_post_time[b];
			if (at > now || now - at > BOARD_POST_FOR)
			{
				g_bd_post_team[b] = 0;
			}
		}
	}
}

// ---- readers (goals.inc, navigate.inc) ----------------------------------------------------------

// TeamBoards::get(team).is_some(): the rules team has a board (teamplay, 1..BOARD_TEAMS).
stock bool:board_exists(team)
{
	return bd_index(team) != -1;
}

// The team's sightings (board.sightings), oldest first: how many.
stock board_sighting_count(team)
{
	new t = bd_index(team);
	return t == -1 ? 0 : g_bd_sight_count[t];
}

// Sighting k of the team: its origin and time, the enemy (EntRef); the bot that saw it.
stock board_sighting(team, k, Float:origin[3], &Float:time, &enemy, &enemy_serial)
{
	new t = bd_index(team);
	if (t == -1 || k < 0 || k >= g_bd_sight_count[t])
	{
		return 0;
	}
	vec_copy(g_bd_sight_origin[t][k], origin);
	time = g_bd_sight_time[t][k];
	enemy = g_bd_sight_ent[t][k];
	enemy_serial = g_bd_sight_serial[t][k];
	return g_bd_sight_by[t][k];
}

// The team's sounds (board.sounds), oldest first: how many.
stock board_sound_count(team)
{
	new t = bd_index(team);
	return t == -1 ? 0 : g_bd_sound_count[t];
}

// Sound k of the team: its origin, time and loudness; the bot that heard it.
stock board_sound(team, k, Float:origin[3], &Float:time, &Float:loud)
{
	new t = bd_index(team);
	if (t == -1 || k < 0 || k >= g_bd_sound_count[t])
	{
		return 0;
	}
	vec_copy(g_bd_sound_origin[t][k], origin);
	time = g_bd_sound_time[t][k];
	loud = g_bd_sound_loud[t][k];
	return g_bd_sound_by[t][k];
}

// goals.rs item_value's claim check: a teammate (not me) claimed the item.
stock bool:board_item_claimed(team, me, item, item_serial)
{
	if (bd_index(team) == -1 || item == 0)
	{
		return false;
	}
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (b != me && g_bd_claim_team[b] == team && g_bd_claim_item[b] == item && g_bd_claim_item_serial[b] == item_serial)
		{
			return true;
		}
	}
	return false;
}

// navigate.rs roam_goal / claim_factor: the goal waypoints the teammates (not me) claimed, into
// out; how many.
stock board_claimed_waypoints(team, me, out[], max)
{
	new n = 0;
	if (bd_index(team) == -1)
	{
		return 0;
	}
	for (new b = 1; b <= MAX_PLAYERS && n < max; b++)
	{
		if (b != me && g_bd_claim_team[b] == team && g_bd_claim_wpt[b] >= 0)
		{
			out[n++] = g_bd_claim_wpt[b];
		}
	}
	return n;
}

// TeamBoard::mates: the teammates' (not me) posted positions, into out; how many.
stock board_mates(team, me, Float:out[][3], max)
{
	new n = 0;
	if (bd_index(team) == -1)
	{
		return 0;
	}
	for (new b = 1; b <= MAX_PLAYERS && n < max; b++)
	{
		if (b != me && g_bd_post_team[b] == team)
		{
			vec_copy(g_bd_post_at[b], out[n++]);
		}
	}
	return n;
}

// TeamBoard::paths_of_others' sources: the path objects of the teammates (not me) with a current
// post, into out; how many (nav_path_crowd_from merges them).
stock board_mate_paths(team, me, RcPath:out[], max)
{
	new n = 0;
	if (bd_index(team) == -1)
	{
		return 0;
	}
	for (new b = 1; b <= MAX_PLAYERS && n < max; b++)
	{
		if (b != me && g_bd_post_team[b] == team && g_nv_i[b][NV_PATH])
		{
			out[n++] = RcPath:g_nv_i[b][NV_PATH];
		}
	}
	return n;
}

// navigate.rs threat_waypoints' team part: each sighting a threat for path, weight
// 1 - age / BOARD_SIGHTING_FOR (nav_path_add_threat keeps at least 0.1 and the highest).
stock board_add_threats(team, RcPath:path)
{
	new t = bd_index(team);
	if (t == -1)
	{
		return;
	}
	new Float:now = g_now;
	for (new k = 0; k < g_bd_sight_count[t]; k++)
	{
		nav_path_add_threat(path, g_bd_sight_origin[t][k], 1.0 - (now - g_bd_sight_time[t][k]) / BOARD_SIGHTING_FOR);
	}
}
