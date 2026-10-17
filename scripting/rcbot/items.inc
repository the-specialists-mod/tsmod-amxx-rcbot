// rcbot/items.inc: the map's items as the bot knows them (port addition, design §4).
// Owner: movement. Ports bot/items.rs (KnownItem there_at, ItemMemory iter / get / untakeable / ignored / scan / learn / learn_item / look / seen / taken / touched; SCAN_INTERVAL, DEFAULT_WEAPON_RESPAWN, DEFAULT_POWERUP_RESPAWN, UNTAKEABLE_FOR, TAKE_TIME, SEE_ITEM_RANGE), think.rs Bot::item_taken.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. The known items are state.inc's g_it_* (slot k of g_it_count[id], in the order
// learned); ITEMS_UNTAKEABLE_FOR and ITEMS_SEE_RANGE are consts.inc's. The game's list is
// world_items (ts_get_items: places, kinds, availability and respawn times).
//
// What a bot may know (items.rs): every item's place and kind from the map's start, as a player
// who knows the map does; whether one is there only from seeing it, or from seeing it taken (its
// own pickup, or another player's in its view: then the game's respawn time), and otherwise from
// the respawn times it learned.

#if defined _rcbot_items_included
	#endinput
#endif
#define _rcbot_items_included

#define ITEMS_SCAN_INTERVAL				5.0
#define ITEMS_DEFAULT_WEAPON_RESPAWN	20.0
#define ITEMS_DEFAULT_POWERUP_RESPAWN	60.0
#define ITEMS_TAKE_TIME					2.0

// ItemMemory::default (a new bot).
stock items_bot_init(id)
{
	g_it_count[id] = 0;
	g_it_next_scan[id] = 0.0;
	g_it_next_look[id] = 0.0;
}

// The frame's list entry of entity ent (world_items), -1 none.
static stock it_list_entry(ent)
{
	return world_item_of(ent);
}

// ItemMemory::get: the slot of (ent, serial), -1 none.
stock items_find(id, ent, serial)
{
	for (new k = 0; k < g_it_count[id]; k++)
	{
		if (g_it_i[id][k][IT_ENT] == ent && g_it_i[id][k][IT_SERIAL] == serial)
		{
			return k;
		}
	}
	return -1;
}

// KnownItem::there_at: the bot thinks the item in slot k is there at `at` (and isn't left alone).
stock bool:items_there_at(id, k, Float:at)
{
	return g_it_f[id][k][IT_AVAILABLE_AT] <= at && g_it_f[id][k][IT_IGNORE_UNTIL] <= g_now;
}

// ItemMemory::untakeable: the bot couldn't take it: left alone for ITEMS_UNTAKEABLE_FOR.
stock items_untakeable(id, ent, serial)
{
	new k = items_find(id, ent, serial);
	if (k != -1)
	{
		g_it_f[id][k][IT_IGNORE_UNTIL] = g_now + ITEMS_UNTAKEABLE_FOR;
	}
}

// ItemMemory::ignored.
stock bool:items_ignored(id, ent, serial)
{
	new k = items_find(id, ent, serial);
	return k != -1 && g_it_f[id][k][IT_IGNORE_UNTIL] > g_now;
}

// ItemMemory::learn_item: an item's place and kind (not one it knows already); assumed there
// (the map starts with every item). When the table is full the new one is dropped.
static stock it_learn_item(id, ent, serial, const Float:origin[3], kind, weapon, powerup)
{
	if (items_find(id, ent, serial) != -1)
	{
		return;
	}
	new k = g_it_count[id];
	if (k >= RC_MAX_KNOWN_ITEMS)
	{
		rc_debug(DBG_THINK, "bot %d knows %d items: item %d not learned", id, k, ent);
		return;
	}
	g_it_i[id][k][IT_ENT] = ent;
	g_it_i[id][k][IT_SERIAL] = serial;
	g_it_i[id][k][IT_KIND] = kind;
	g_it_i[id][k][IT_WEAPON] = weapon;
	g_it_i[id][k][IT_POWERUP] = powerup;
	g_it_f[id][k][IT_AVAILABLE_AT] = 0.0;
	g_it_f[id][k][IT_GONE_SEEN] = 0.0;
	g_it_f[id][k][IT_RESPAWN] = kind == TSENT_POWERUP ? ITEMS_DEFAULT_POWERUP_RESPAWN : ITEMS_DEFAULT_WEAPON_RESPAWN;
	g_it_f[id][k][IT_IGNORE_UNTIL] = 0.0;
	g_it_f[id][k][IT_TOUCHED_AT] = 0.0;
	vec_copy(origin, g_it_origin[id][k]);
	g_it_count[id] = k + 1;
}

// Drops slot k, keeping the order.
static stock it_remove(id, k)
{
	new n = g_it_count[id];
	for (new j = k; j + 1 < n; j++)
	{
		for (new It_I:f; f < It_I; f++)
		{
			g_it_i[id][j][f] = g_it_i[id][j + 1][f];
		}
		for (new It_F:f; f < It_F; f++)
		{
			g_it_f[id][j][f] = g_it_f[id][j + 1][f];
		}
		vec_copy(g_it_origin[id][j + 1], g_it_origin[id][j]);
	}
	g_it_count[id] = n - 1;
}

// ItemMemory::scan: every ITEMS_SCAN_INTERVAL the game's list: items gone from the map are
// forgotten, ground weapons and power-ups learned (places and kinds only).
stock items_scan(id)
{
	new Float:now = g_now;
	if (g_it_next_scan[id] > now)
	{
		return;
	}
	g_it_next_scan[id] = now + ITEMS_SCAN_INTERVAL;
	new n = world_items();
	for (new k = g_it_count[id] - 1; k >= 0; k--)
	{
		new e = it_list_entry(g_it_i[id][k][IT_ENT]);
		if (e == -1 || world_item_field(e, TSITM_SERIAL) != g_it_i[id][k][IT_SERIAL])
		{
			it_remove(id, k);
		}
	}
	new Float:o[3];
	for (new k = 0; k < n; k++)
	{
		new kind = world_item_field(k, TSITM_KIND);
		if (kind == TSENT_GROUND_WEAPON || kind == TSENT_POWERUP)
		{
			world_item_origin(k, o);
			it_learn_item(id, world_item_field(k, TSITM_ENT), world_item_field(k, TSITM_SERIAL), o, kind, world_item_field(k, TSITM_WEAPON), world_item_field(k, TSITM_POWERUP));
		}
	}
}

// ItemMemory::learn: an item the bot sees or was told of (think's learn_items, for the visible
// entities): learned if it is a ground weapon or power-up on the game's list.
stock items_learn(id, ent)
{
	new e = it_list_entry(ent);
	if (e == -1)
	{
		return;
	}
	new kind = world_item_field(e, TSITM_KIND);
	if (kind != TSENT_GROUND_WEAPON && kind != TSENT_POWERUP)
	{
		return;
	}
	new Float:o[3];
	world_item_origin(e, o);
	it_learn_item(id, ent, world_item_field(e, TSITM_SERIAL), o, kind, world_item_field(e, TSITM_WEAPON), world_item_field(e, TSITM_POWERUP));
}

// think.rs learn_items' in_view: in front of the bot (dot > 0.5) or within 200 units.
static stock bool:it_in_view(id, const Float:eye[3], const Float:o[3])
{
	new Float:to[3], Float:dir[3], Float:fwd[3], Float:right[3];
	vec_sub(o, eye, to);
	if (vec_length(to) < 200.0)
	{
		return true;
	}
	vec_normalize(to, dir);
	look_view_vectors(id, fwd, right);
	return vec_dot(dir, fwd) > 0.5;
}

// Whether known item k is there now: the same entity on the game's list, drawn and available.
static stock bool:it_there(id, k)
{
	new ent = g_it_i[id][k][IT_ENT];
	new e = it_list_entry(ent);
	if (e == -1 || world_item_field(e, TSITM_SERIAL) != g_it_i[id][k][IT_SERIAL] || !pev_valid(ent))
	{
		return false;
	}
	return !(pev(ent, pev_effects) & RC_EF_NODRAW) && world_item_field(e, TSITM_AVAILABLE) != 0;
}

// ItemMemory::look (with think.rs learn_items' view cone): every 0.5 s, the items the bot can see
// (within ITEMS_SEE_RANGE, in view, a clear line): taken ones are expected back after their
// respawn time, and the time between seeing one gone and seeing it back teaches it.
stock items_look(id)
{
	new Float:now = g_now;
	if (g_it_next_look[id] > now)
	{
		return;
	}
	g_it_next_look[id] = now + 0.5;
	new Float:eye[3], hit;
	world_eyes(id, eye);
	for (new k = 0; k < g_it_count[id]; k++)
	{
		if (vec_distance(g_it_origin[id][k], eye) > ITEMS_SEE_RANGE || !it_in_view(id, eye, g_it_origin[id][k]))
		{
			continue;
		}
		if (world_trace_hit(eye, g_it_origin[id][k], true, true, id, hit) < 1.0 && hit != g_it_i[id][k][IT_ENT])
		{
			continue;
		}
		it_seen(id, k, it_there(id, k), now);
	}
}

// ItemMemory::seen: what the bot saw of item k at now.
static stock it_seen(id, k, bool:there, Float:now)
{
	new Float:touched = g_it_f[id][k][IT_TOUCHED_AT];
	if (there && touched > 0.0 && now - touched > ITEMS_TAKE_TIME)
	{
		// It touched it and pressed use, and the item is still there: the game won't give it.
		g_it_f[id][k][IT_TOUCHED_AT] = 0.0;
		g_it_f[id][k][IT_IGNORE_UNTIL] = now + ITEMS_UNTAKEABLE_FOR;
	}
	else if (!there)
	{
		g_it_f[id][k][IT_TOUCHED_AT] = 0.0;
	}
	if (there)
	{
		new Float:gone = g_it_f[id][k][IT_GONE_SEEN];
		if (gone > 0.0)
		{
			new Float:took = now - gone;
			if (took > 1.0 && took < 180.0)
			{
				g_it_f[id][k][IT_RESPAWN] = (g_it_f[id][k][IT_RESPAWN] + took) * 0.5;
			}
		}
		g_it_f[id][k][IT_AVAILABLE_AT] = 0.0;
		g_it_f[id][k][IT_GONE_SEEN] = 0.0;
	}
	else if (g_it_f[id][k][IT_GONE_SEEN] == 0.0)
	{
		g_it_f[id][k][IT_GONE_SEEN] = now;
		g_it_f[id][k][IT_AVAILABLE_AT] = now + g_it_f[id][k][IT_RESPAWN];
	}
	else if (g_it_f[id][k][IT_AVAILABLE_AT] <= now)
	{
		// Still not back: give it a while longer.
		g_it_f[id][k][IT_AVAILABLE_AT] = now + g_it_f[id][k][IT_RESPAWN] * 0.5;
	}
}

// think.rs Bot::item_taken (the port's item_pickup): player took ent. The bot counts it gone
// only if it took it itself or saw it taken (in front of it, with a clear line), with the game's
// respawn time when it gives one (ItemMemory::taken); a pickup it went for is done.
stock items_taken(id, player, ent)
{
	new k = -1;
	for (new j = 0; j < g_it_count[id]; j++)
	{
		if (g_it_i[id][j][IT_ENT] == ent)
		{
			k = j;
			break;
		}
	}
	if (k == -1)
	{
		return;
	}
	new bool:mine = player == id;
	if (!mine)
	{
		new Float:eye[3], Float:end_pos[3], hit;
		world_eyes(id, eye);
		if (!bot_alive(id) || vec_distance(g_it_origin[id][k], eye) > ITEMS_SEE_RANGE || !it_in_view(id, eye, g_it_origin[id][k]))
		{
			return;
		}
		if (world_trace_line(eye, g_it_origin[id][k], true, true, id, end_pos, hit) < 1.0 && hit != ent)
		{
			return;
		}
	}
	new Float:now = g_now;
	new Float:respawn_at = 0.0;
	new e = it_list_entry(ent);
	if (e != -1)
	{
		respawn_at = Float:world_item_field(e, TSITM_RESPAWN_AT);
	}
	g_it_f[id][k][IT_TOUCHED_AT] = 0.0;
	g_it_f[id][k][IT_GONE_SEEN] = now;
	if (respawn_at > now)
	{
		g_it_f[id][k][IT_RESPAWN] = respawn_at - now;
		g_it_f[id][k][IT_AVAILABLE_AT] = respawn_at;
	}
	else
	{
		g_it_f[id][k][IT_AVAILABLE_AT] = now + g_it_f[id][k][IT_RESPAWN];
	}
	if (mine && g_th_i[id][TH_PICKUP_ENT] == ent)
	{
		// pickup_entity is think's field and the task queue think's; think.rs item_taken ends them.
		if (g_tk_count[id] > 0 && g_tk_i[id][0][TK_KIND] == TASK_PICKUP_ITEM && g_tk_i[id][0][TK_EDICT] == ent
			&& g_tk_i[id][0][TK_EDICT_SERIAL] == g_th_i[id][TH_PICKUP_SERIAL])
		{
			tasks_finished_current(id, false);
		}
		g_th_i[id][TH_PICKUP_ENT] = 0;
		g_th_i[id][TH_PICKUP_SERIAL] = 0;
	}
}

// ItemMemory::touched: the bot touched it to take it.
stock items_touched(id, ent, serial)
{
	new k = items_find(id, ent, serial);
	if (k != -1)
	{
		g_it_f[id][k][IT_TOUCHED_AT] = g_now;
	}
}
