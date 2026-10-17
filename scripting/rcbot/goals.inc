// rcbot/goals.inc: where the bot goes next (port addition, design §4).
// Owner: think. Ports bot/goals.rs (Explore is navigate.inc's / the module's; Goal, score, Bot::weapon_value, powerup_value, item_value, danger_near, choose_goal, hold_spot, claim_near, fight_readiness, predict_enemy, claim_item_factor, regroup_point; FORGET_VISIT, NEAR_GOAL, WEAPON_SLOTS, ROAM_SCORE).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Upstream's TS bot picked up the nearest visible item, went for a random objective, or roamed.
// The port scores what it knows of the map: items are worth value / (distance + danger), enemies
// seen lately and sounds heard by how fresh they are, a spot to hold, a teammate to regroup with.
//
// The team boards are board.inc's tables (read here, written by their owner): sightings and
// sounds by rules team - 1 (TeamBoards::get), claims and posts per bot, on the bot's rules team.
//
// Status: ported.

#if defined _rcbot_goals_included
	#endinput
#endif
#define _rcbot_goals_included

// goals.rs ROAM_SCORE: a goal must score this much to beat roaming.
#define GOALS_ROAM_SCORE	10.0
// board.rs POST_FOR: seconds a bot's post of its position counts.
#define GOALS_POST_FOR		2.0
// board.rs SOUND_FOR, as goals.rs uses it for a teammate's sound.
#define GOALS_SOUND_FOR		8.0
// goals.rs SNIPER (the Barrett).
#define GOALS_SNIPER		18

// goals.rs Goal.
enum
{
	GOAL_ITEM = 0,		// arg: the item (EntRef)
	GOAL_HUNT,			// vec + time: an enemy last seen there and then
	GOAL_INVESTIGATE,	// vec: a sound or a teammate's sighting
	GOAL_REGROUP,		// vec: the teammate to join
	GOAL_HOLD,			// arg: the waypoint to hold
	GOAL_ROAM
};

// goals.rs WEAPON_SLOTS: inventory space each TS weapon takes (TS 3.0's weapon table), by id.
stock const g_goals_weapon_slots[38] =
{
	0, 10, 10, 20, 40, 40, 25, 20, 10, 10, 10, 40, 15, 40, 10, 30, 20, 15, 70, 25, 30, 20, 10, 30, 7, 1, 40, 40, 5, 0, 10, 15, 70, 20, 10,
	1, 15, 15
};

// The port's power-up bits (goals.rs POWERUP_*): health and armour are given at a touch even
// with one held (INSTANT_POWERUPS).
#define GOALS_POWERUP_KUNGFU	4
#define GOALS_POWERUP_HEALTH	64
#define GOALS_POWERUP_ARMOR		128

// goals.rs score: a goal's value over the way there.
stock Float:goals_score(Float:value, Float:distance, Float:danger)
{
	return value * 1000.0 / (distance + 200.0 + 300.0 * fmax(danger, 0.0));
}

// The preset priority of an RCBot weapon id (bot_weapons.ini; 0 for none).
static stock goals_priority(w)
{
	return w >= 0 && w < RC_MAX_WEAPONS ? g_wp_i[w][WP_PRIORITY] : 0;
}

// goals.rs Bot::weapon_value: what weapon w lying in the map is worth to the bot: a better gun
// than it has (more unarmed), ammo for one it has when low; nothing it has no room for.
stock Float:goals_weapon_value(id, weapon)
{
	if (config_is_on(CFG_TS_DONT_PICKUP_WEAPONS) || weapon <= 0 || weapon == RC_KUNGFU)
	{
		return 0.0;
	}
	if (weapon < RC_MAX_WEAPONS && g_bw_weapon[id][weapon][BW_HAS_WEAPON])
	{
		return !botweapons_is_melee(id, weapon) && g_bw_weapon[id][weapon][BW_RESERVE] < max(g_bw_weapon[id][weapon][BW_MAX_CLIP], 1) ? 25.0 : 0.0;
	}
	new slots = weapon < sizeof(g_goals_weapon_slots) ? g_goals_weapon_slots[weapon] : 0;
	if (world_field(id, TSPS_FREESLOTS) < slots)
	{
		return 0.0;
	}
	new best = botweapons_best_loaded_gun(id);
	if (best == -1)
	{
		return 60.0 + 5.0 * float(goals_priority(weapon));
	}
	new gain = goals_priority(weapon) - goals_priority(best);
	return gain > 0 ? 15.0 * float(gain) : 0.0;
}

// goals.rs Bot::powerup_value: health by what the bot lacks, armour some, the others a lot unarmed
// (kung fu most); nothing but the instant kinds while one is held or running (+use on another
// swaps the held one).
stock Float:goals_powerup_value(id, bits)
{
	if (bits & GOALS_POWERUP_HEALTH)
	{
		return fmax(fmax(think_max_health(id), 100.0) - world_health(id), 0.0);
	}
	if (bits & GOALS_POWERUP_ARMOR)
	{
		return 20.0;
	}
	if (!(bits & (GOALS_POWERUP_HEALTH | GOALS_POWERUP_ARMOR)) && (world_field(id, TSPS_STORED_PWUP) != 0 || world_field(id, TSPS_ACTIVE_PWUP) != 0))
	{
		return 0.0;
	}
	if (botweapons_best_loaded_gun(id) == -1)
	{
		return bits & GOALS_POWERUP_KUNGFU ? 80.0 : 50.0;
	}
	return 35.0;
}

// goals.rs Bot::item_value: what a known item (items.inc slot) is worth now.
stock Float:goals_item_value(id, item_slot)
{
	switch (g_it_i[id][item_slot][IT_KIND])
	{
		case TSENT_GROUND_WEAPON:
		{
			return goals_weapon_value(id, g_it_i[id][item_slot][IT_WEAPON]);
		}
		case TSENT_POWERUP:
		{
			return goals_powerup_value(id, g_it_i[id][item_slot][IT_POWERUP]);
		}
	}
	return 0.0;
}

// goals.rs Bot::danger_near: the danger at the waypoint nearest at (the heatmap).
stock Float:goals_danger_near(id, const Float:at[3])
{
	new wpt = think_nearest_waypoint(at, 400.0, -1, false, false, false);
	return wpt >= 0 ? nav_heatmap_danger(wpt, g_th_i[id][TH_TEAM]) : 0.0;
}

// goals.rs Bot::claim_near: navigate's claim factor for the waypoint nearest a place.
static stock Float:goals_claim_near(id, const Float:at[3])
{
	new wpt = think_nearest_waypoint(at, 400.0, -1, false, false, false);
	return wpt >= 0 ? navigate_claim_factor(id, wpt) : 1.0;
}

// TeamBoards::get: the bot's board (rules team - 1), -1 outside teamplay.
static stock goals_board(id)
{
	new t = world_team(id) - 1;
	return t >= 0 && t < BOARD_TEAMS ? t : -1;
}

// goals.rs Bot::choose_goal: the best thing to do now, by value / (distance + danger) against
// ROAM_SCORE: GOAL_*, its argument (item, waypoint) and vector, its score, and for a hunt when the
// enemy was seen.
stock goals_choose(id, &arg, Float:vec[3], &Float:score, &Float:when = 0.0)
{
	new Float:now = g_now;
	new goal = GOAL_ROAM;
	arg = 0;
	vec_zero(vec);
	when = 0.0;
	score = GOALS_ROAM_SCORE;
	new Float:from[3];
	world_origin(id, from);
	new Float:max_speed = fmax(g_mv_f[id][MV_MAX_SPEED], 1.0);
	for (new k = 0; k < g_it_count[id]; k++)
	{
		new Float:dist = vec_distance(g_it_origin[id][k], from) * 1.3;
		new Float:arrive = now + dist / max_speed;
		// KnownItem::there_at.
		if (!(g_it_f[id][k][IT_AVAILABLE_AT] <= arrive && g_it_f[id][k][IT_IGNORE_UNTIL] <= now) || g_it_origin[id][k][2] > from[2] + 1000.0)
		{
			continue;
		}
		new Float:value = goals_item_value(id, k) * goals_claim_item_factor(id, g_it_i[id][k][IT_ENT], g_it_i[id][k][IT_SERIAL]);
		if (value <= 0.0)
		{
			continue;
		}
		new Float:s = goals_score(value, dist, goals_danger_near(id, g_it_origin[id][k]));
		if (s > score)
		{
			goal = GOAL_ITEM;
			arg = g_it_i[id][k][IT_ENT];
			score = s;
		}
	}
	// Enemies seen lately and sounds heard: worth more fresh, to a bot ready to fight.
	new Float:ready = goals_fight_readiness(id);
	for (new k = 0; k < g_rm_count[id]; k++)
	{
		if (!g_rm_i[id][k][RM_ENT] || g_rm_i[id][k][RM_HUNTED])
		{
			continue;
		}
		new Float:fresh = 1.0 - (now - g_rm_time[id][k]) / REMEMBER_FOR;
		new Float:s = goals_score(45.0 * fresh * ready, vec_distance(g_rm_origin[id][k], from), 0.0);
		if (s > score)
		{
			goal = GOAL_HUNT;
			vec_copy(g_rm_origin[id][k], vec);
			when = g_rm_time[id][k];
			score = s;
		}
	}
	for (new k = 0; k < g_hr_count[id]; k++)
	{
		if (g_hr_i[id][k][HR_INVESTIGATED])
		{
			continue;
		}
		new Float:fresh = 1.0 - (now - g_hr_f[id][k][HR_TIME]) / HEARD_FOR;
		new Float:s = goals_score(30.0 * g_hr_f[id][k][HR_LOUD] * fresh * ready, vec_distance(g_hr_origin[id][k], from), 0.0);
		if (s > score)
		{
			goal = GOAL_INVESTIGATE;
			vec_copy(g_hr_origin[id][k], vec);
			score = s;
		}
	}
	// What teammates saw and heard, worth less (they may be on it) and only when nobody claimed it.
	new b = goals_board(id);
	if (b != -1)
	{
		for (new k = 0; k < g_bd_sight_count[b]; k++)
		{
			if (g_bd_sight_by[b][k] == id)
			{
				continue;
			}
			new Float:fresh = 1.0 - (now - g_bd_sight_time[b][k]) / BOARD_SIGHTING_FOR;
			new Float:s = goals_score(25.0 * fresh * ready * goals_claim_near(id, g_bd_sight_origin[b][k]), vec_distance(g_bd_sight_origin[b][k], from), 0.0);
			if (s > score)
			{
				goal = GOAL_INVESTIGATE;
				vec_copy(g_bd_sight_origin[b][k], vec);
				score = s;
			}
		}
		for (new k = 0; k < g_bd_sound_count[b]; k++)
		{
			if (g_bd_sound_by[b][k] == id)
			{
				continue;
			}
			new Float:fresh = 1.0 - (now - g_bd_sound_time[b][k]) / GOALS_SOUND_FOR;
			new Float:s = goals_score(15.0 * g_bd_sound_loud[b][k] * fresh * ready * goals_claim_near(id, g_bd_sound_origin[b][k]),
				vec_distance(g_bd_sound_origin[b][k], from), 0.0);
			if (s > score)
			{
				goal = GOAL_INVESTIGATE;
				vec_copy(g_bd_sound_origin[b][k], vec);
				score = s;
			}
		}
	}
	if (g_th_f[id][TH_HOLD_AGAIN] <= now)
	{
		new Float:value;
		new spot = goals_hold_spot(id, value);
		if (spot != -1)
		{
			new Float:at[3];
			nav_origin(spot, at);
			new Float:s = goals_score(value, vec_distance(at, from), goals_danger_near(id, at));
			if (s > score)
			{
				goal = GOAL_HOLD;
				arg = spot;
				vec_zero(vec);
				score = s;
			}
		}
	}
	new Float:mate[3];
	if (goals_regroup_point(id, mate))
	{
		new Float:s = goals_score(60.0, vec_distance(mate, from), 0.0);
		if (s > score)
		{
			goal = GOAL_REGROUP;
			vec_copy(mate, vec);
			score = s;
		}
	}
	return goal;
}

// goals.rs Bot::hold_spot: whether to hold a place rather than roam, and where: a waypoint within
// 600 units that sees the most of the map (the visibility table), not taken by a teammate. Worth
// it (value out) with a long gun, hurt, or soon after a fight. The waypoint, -1 none.
stock goals_hold_spot(id, &Float:value)
{
	new Float:now = g_now;
	new weapon = max(g_bw_i[id][BW_CURRENT], 0);
	value = 0.0;
	// goals.rs LONG_GUNS: the Barrett and the scoped rifles (M4A1, AK47, AUG, M16A4).
	if (weapon == GOALS_SNIPER || weapon == 5 || weapon == 13 || weapon == 15 || weapon == 27)
	{
		value += weapon == GOALS_SNIPER ? 30.0 : 12.0;
	}
	if (world_health(id) < 50.0)
	{
		value += 10.0;
	}
	new Float:last = g_cb_f[id][CB_LAST_SEE_ENEMY_TIME];
	if (last > 0.0 && now - last < 10.0)
	{
		value += 6.0;
	}
	if (value <= 0.0)
	{
		return -1;
	}
	new Float:from[3], Float:at[3];
	world_origin(id, from);
	new count = min(nav_count(), RC_MAX_WAYPOINTS);
	new best = -1, best_sees = 0;
	for (new i = 0; i < count; i++)
	{
		if ((nav_flags(i) & (NAV_FL_DELETED | NAV_FL_AIMING | NAV_FL_UNREACHABLE)) || !nav_origin(i, at) || vec_distance(at, from) > 600.0
			|| navigate_claim_factor(id, i) < 1.0)
		{
			continue;
		}
		new sees = 0;
		for (new j = 0; j < count; j++)
		{
			if (j != i && nav_visible(i, j))
			{
				sees++;
			}
		}
		if (best == -1 || sees > best_sees)
		{
			best = i;
			best_sees = sees;
		}
	}
	// A spot that sees little is no better than walking.
	return best != -1 && best_sees >= 8 ? best : -1;
}

// goals.rs Bot::fight_readiness: its health, less without a loaded gun.
stock Float:goals_fight_readiness(id)
{
	new Float:health = fclamp(world_health(id) / fmax(think_max_health(id), 100.0), 0.1, 1.0);
	return botweapons_best_loaded_gun(id) != -1 ? health : health * 0.5;
}

// goals.rs Bot::predict_enemy: where an enemy last seen at origin secs ago may be now: a waypoint
// about as far along the graph from there as it could have walked (200 units/s, at most 1500),
// out of the bot's sight; the nearest waypoint to origin when it was seen just now. -1 none.
stock goals_predict_enemy(id, const Float:origin[3], Float:secs)
{
	new start = think_nearest_waypoint(origin, NAV_REACHABLE_RANGE, -1, false, false, false);
	if (start < 0)
	{
		return -1;
	}
	new Float:reach = fmin(secs * 200.0, 1500.0);
	if (reach < 300.0)
	{
		return start;
	}
	// Dijkstra by length from start, up to reach (the Rust stack order).
	static Float:dist[RC_MAX_WAYPOINTS];
	static open[RC_MAX_WAYPOINTS * 8];
	static succ[64];
	static candidates[RC_MAX_WAYPOINTS];
	new const Float:INF = 999999999.0;
	new count = min(nav_count(), RC_MAX_WAYPOINTS);
	for (new i = 0; i < count; i++)
	{
		dist[i] = INF;
	}
	new top = 0;
	open[top++] = start;
	dist[start] = 0.0;
	new Float:a[3], Float:b[3];
	while (top > 0)
	{
		new i = open[--top];
		nav_origin(i, a);
		new ns = min(nav_paths_out(i, succ, sizeof(succ), -1), sizeof(succ));
		for (new k = 0; k < ns; k++)
		{
			new s = succ[k];
			if (s < 0 || s >= count)
			{
				continue;
			}
			nav_origin(s, b);
			new Float:d = dist[i] + vec_distance(b, a);
			if (d < dist[s] && d <= reach)
			{
				dist[s] = d;
				if (top < sizeof(open))
				{
					open[top++] = s;
				}
				else
				{
					rc_debug(DBG_NAV, "bot %d: predict_enemy's open list is full", id);
				}
			}
		}
	}
	new mine = g_nv_i[id][NV_CURRENT_WPT], n = 0;
	for (new i = 0; i < count; i++)
	{
		if (dist[i] >= reach * 0.5 && dist[i] < INF && !(nav_flags(i) & (NAV_FL_DELETED | NAV_FL_AIMING)) && (mine < 0 || !nav_visible(mine, i)))
		{
			candidates[n++] = i;
		}
	}
	return n == 0 ? start : candidates[random_num(0, n - 1)];
}

// goals.rs Bot::claim_item_factor: how much the bot still wants an item a teammate is going for
// (1: nobody is). A claim counts until its time (TeamBoard::expire).
stock Float:goals_claim_item_factor(id, item, item_serial)
{
	new team = world_team(id);
	if (goals_board(id) == -1)
	{
		return 1.0;
	}
	new Float:now = g_now;
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (b != id && world_team(b) == team && g_bd_claim_until[b] > now
			&& think_same_handle(g_bd_claim_item[b], g_bd_claim_item_serial[b], item, item_serial) && item != 0)
		{
			return 0.1;
		}
	}
	return 1.0;
}

// goals.rs Bot::regroup_point: outnumbered (more enemies seen near the bot in the last 5 s, by it
// or its team, than it has teammates near it, by two or more): where the nearest teammate at least
// 300 units away is.
stock bool:goals_regroup_point(id, Float:out[3])
{
	vec_zero(out);
	new board = goals_board(id);
	if (board == -1)
	{
		return false;
	}
	new Float:now = g_now;
	new Float:me[3];
	world_origin(id, me);
	static ents[BOARD_MAX_SIGHTINGS + RC_MAX_REMEMBER], serials[BOARD_MAX_SIGHTINGS + RC_MAX_REMEMBER];
	new n = 0;
	for (new k = 0; k < g_bd_sight_count[board]; k++)
	{
		if (now - g_bd_sight_time[board][k] < 5.0 && vec_distance(g_bd_sight_origin[board][k], me) < 1500.0)
		{
			n = goals_add_enemy(ents, serials, n, g_bd_sight_ent[board][k], g_bd_sight_serial[board][k]);
		}
	}
	for (new k = 0; k < g_rm_count[id]; k++)
	{
		if (g_rm_i[id][k][RM_ENT] && now - g_rm_time[id][k] < 5.0 && vec_distance(g_rm_origin[id][k], me) < 1500.0)
		{
			n = goals_add_enemy(ents, serials, n, g_rm_i[id][k][RM_ENT], g_rm_i[id][k][RM_SERIAL]);
		}
	}
	// TeamBoard::mates: the bots of the team that posted in the last POST_FOR s.
	new team = world_team(id), near = 0, best = 0, Float:best_d = 0.0;
	for (new m = 1; m <= MAX_PLAYERS; m++)
	{
		new Float:t = g_bd_post_time[m];
		if (m == id || t <= 0.0 || t > now || now - t > GOALS_POST_FOR || world_team(m) != team)
		{
			continue;
		}
		new Float:d = vec_distance(g_bd_post_at[m], me);
		if (d < 1000.0)
		{
			near++;
		}
		if (d >= 300.0 && (!best || d < best_d))
		{
			best = m;
			best_d = d;
		}
	}
	if (n < near + 2 || !best)
	{
		return false;
	}
	vec_copy(g_bd_post_at[best], out);
	return true;
}

// enemies.sort + dedup: a handle once.
static stock goals_add_enemy(ents[], serials[], n, ent, serial)
{
	for (new k = 0; k < n; k++)
	{
		if (ents[k] == ent && serials[k] == serial)
		{
			return n;
		}
	}
	ents[n] = ent;
	serials[n] = serial;
	return n + 1;
}
