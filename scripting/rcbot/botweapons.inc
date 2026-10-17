// rcbot/botweapons.inc: the weapons a bot owns (CBotWeapons) and its weapon in hand.
// Owner: combat. Ports bot/weapons.rs (BotWeapon is_melee, clip_is_empty, out_of_ammo, low_on_ammo, can_reload, need_to_reload, can_be_used_underwater, priority, can_shoot_primary, set_weapon; BotWeapons get, has, add, remove, best_loaded_gun, weapon_info, record, sync_inventory, best_weapon_id), think.rs Bot::frame's weapon block (BotClient_TS_WeaponInfo, the inventory sync), spawn_init's weapon part, mod.rs Bot::reload / weapon_select_failed.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. A BotWeapon is g_bw_weapon[id][w] (state.inc); its m_pWeaponInfo is the
// registry's entry for w (weapons.inc) when BW_INFO is set.

#if defined _rcbot_botweapons_included
	#endinput
#endif
#define _rcbot_botweapons_included

// mod.rs Bot::weapon_select_failed: the game refused to take out game_id (0 kung fu): the bot
// doesn't own it; it is forgotten and the inventory is read again next frame.
stock botweapons_select_failed(id, game_id)
{
	new w = ts_weapon_from_game_id(game_id);
	if (w != RC_KUNGFU)
	{
		botweapons_remove(id, w);
	}
	g_bw_i[id][BW_SWITCH_PENDING] = -1;
	g_bw_f[id][BW_NEXT_INVENTORY_SYNC] = 0.0;
}

// think.rs Bot::frame: TS bots always have kung fu (StartFrame; upstream added 36, TS 3.0's
// Contender: design §5).
stock botweapons_ensure_kungfu(id)
{
	if (!botweapons_has(id, RC_KUNGFU))
	{
		botweapons_add(id, RC_KUNGFU);
	}
}

// think.rs Bot::spawn_init's weapon part: a new life starts with what the game gives it (forget
// the last one's guns, keep kung fu) and reads the inventory at the first frame alive.
stock botweapons_spawn_init(id)
{
	botweapons_bot_init(id);
	botweapons_add(id, RC_KUNGFU);
	g_bw_i[id][BW_CURRENT] = -1;
	g_bw_i[id][BW_LAST_INFO_SET] = 0;
	g_bw_i[id][BW_SWITCH_PENDING] = -1;
	g_bw_f[id][BW_NEXT_INVENTORY_SYNC] = 0.0;
	g_bw_i[id][BW_INVENTORY_SERIAL] = 0;
	g_bw_f[id][BW_NEXT_RELOAD_TIME] = 0.0;
}

// bot/weapons.rs BotWeapon::set_weapon (SetWeapon(iId, NULL)): Reset, then the registered info
// (and m_bHasWeapon) if any.
stock botweapons_set_weapon(id, w)
{
	for (new Bw_I:k; k < Bw_I; k++) g_bw_weapon[id][w][k] = 0;
	g_bw_weapon[id][w][BW_ID] = w;
	g_bw_weapon[id][w][BW_INFO] = _:weapons_registered(w);
	g_bw_weapon[id][w][BW_HAS_WEAPON] = g_bw_weapon[id][w][BW_INFO];
}

// BotWeapons::add (AddWeapon(iId)).
stock botweapons_add(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return;
	}
	botweapons_set_weapon(id, w);
}

// BotWeapons::remove: the bot dropped the weapon (design §4).
stock botweapons_remove(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return;
	}
	for (new Bw_I:k; k < Bw_I; k++) g_bw_weapon[id][w][k] = 0;
}

// BotWeapons::has (CBot::HasWeapon, TS: the CBotWeapon's own flag).
stock bool:botweapons_has(id, w)
{
	return w >= 0 && w < RC_MAX_WEAPONS && g_bw_weapon[id][w][BW_HAS_WEAPON] != 0;
}

// mod.rs Bot::new's BotWeapons::default (a new bot): every weapon unset.
stock botweapons_bot_init(id)
{
	for (new w = 0; w < RC_MAX_WEAPONS; w++)
	{
		for (new Bw_I:k; k < Bw_I; k++) g_bw_weapon[id][w][k] = 0;
	}
}

// think.rs Bot::frame's weapon block (BotClient_TS_WeaponInfo): the weapon in hand is the bot's
// current weapon every frame (design §4); a change asks for the inventory, which is read again
// when the game's inventory serial changes (every INVENTORY_SYNC s without one); a new weapon
// or clip lets the reload key be pressed again; an empty gun (not kung fu) is dropped.
// The caller checks the bot is alive (state.alive).
stock botweapons_frame_sync(id)
{
	new Float:now = g_now;
	new held = ts_weapon_from_game_id(world_weapon(id));
	if (g_bw_i[id][BW_CURRENT] != held)
	{
		g_bw_f[id][BW_NEXT_INVENTORY_SYNC] = 0.0;
	}
	new pending = g_bw_i[id][BW_SWITCH_PENDING];
	if (pending != -1 && (pending == held || g_bw_f[id][BW_SWITCH_UNTIL] <= now))
	{
		g_bw_i[id][BW_SWITCH_PENDING] = -1;
	}
	g_bw_i[id][BW_CURRENT] = held;
	new serial = world_field(id, TSPS_INVSERIAL);
	new bool:changed = serial != 0 && serial != g_bw_i[id][BW_INVENTORY_SERIAL];
	new Float:next = g_bw_f[id][BW_NEXT_INVENTORY_SYNC];
	if (changed || (next <= now && (serial == 0 || next == 0.0)))
	{
		g_bw_f[id][BW_NEXT_INVENTORY_SYNC] = now + INVENTORY_SYNC;
		// think.rs: `if let Some(owned) = w.inventory(..)`: a failed read changes nothing, and the
		// serial stays old so the next change reads again.
		if (botweapons_sync_inventory(id))
		{
			g_bw_i[id][BW_INVENTORY_SERIAL] = serial;
		}
	}
	new game_id = world_weapon(id), clip = world_clip(id), ammo = world_ammo(id);
	new bool:set = g_bw_i[id][BW_LAST_INFO_SET] != 0;
	if (!set || g_bw_i[id][BW_LAST_WEAPON] != game_id || g_bw_i[id][BW_LAST_CLIP] != clip || g_bw_i[id][BW_LAST_AMMO] != ammo)
	{
		if (!set || g_bw_i[id][BW_LAST_WEAPON] != game_id || g_bw_i[id][BW_LAST_CLIP] != clip)
		{
			g_bw_f[id][BW_NEXT_RELOAD_TIME] = 0.0;
		}
		g_bw_i[id][BW_LAST_INFO_SET] = 1;
		g_bw_i[id][BW_LAST_WEAPON] = game_id;
		g_bw_i[id][BW_LAST_CLIP] = clip;
		g_bw_i[id][BW_LAST_AMMO] = ammo;
		new bool:drop;
		new current = botweapons_weapon_info(id, game_id, clip, ammo, drop);
		// Kung fu reports no clip and no ammo too; there is nothing to drop then.
		if (drop && current != RC_KUNGFU)
		{
			tasks_add_priority(id, TASK_DROP_WEAPON, 0, 0, 0, 0, 0.0, VEC_ZERO);
		}
	}
}

// bot/weapons.rs BotWeapons::best_loaded_gun: the highest-priority gun the bot has seen it owns
// with rounds left, whatever the range (design §4: what to take out after dropping an empty
// gun). RCBot id, -1 none.
stock botweapons_best_loaded_gun(id)
{
	new best = -1;
	for (new w = 1; w < RC_MAX_WEAPONS; w++)
	{
		if (g_bw_weapon[id][w][BW_HAS_WEAPON] && !botweapons_is_melee(id, w) && !botweapons_out_of_ammo(id, w)
			&& (best == -1 || botweapons_priority(id, best) < botweapons_priority(id, w)))
		{
			best = w;
		}
	}
	return best == -1 ? -1 : g_bw_weapon[id][best][BW_ID];
}

// bot/weapons.rs BotWeapons::record: what WeaponInfo does for one weapon (registers it, keeps
// its clip, reserve and the largest clip seen; TS 3.0's full clip when the game's table has the
// gun, so one first seen empty can be reloaded: design §4, §5.49). The bot's id of it.
stock botweapons_record(id, game_id, clip, ammo)
{
	new w = ts_weapon_from_game_id(game_id);
	weapons_add(w);
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return w;
	}
	new seen = g_bw_weapon[id][w][BW_ID] == w ? g_bw_weapon[id][w][BW_MAX_CLIP] : 0;
	botweapons_set_weapon(id, w);
	g_bw_weapon[id][w][BW_MAX_CLIP] = seen;
	g_bw_weapon[id][w][BW_CLIP] = clip;
	g_bw_weapon[id][w][BW_HAS_WEAPON] = 1;
	g_bw_weapon[id][w][BW_RESERVE] = ammo;
	new full = tsweapons_known(w) && !tsweapons_is_melee_or_thrown(w) ? g_gd_i[w][GD_CLIP] : 0;
	new m = seen > clip ? seen : clip;
	g_bw_weapon[id][w][BW_MAX_CLIP] = m > full ? m : full;
	return w;
}

// bot/weapons.rs BotWeapons::weapon_info (BotClient_TS_WeaponInfo): the game says the bot holds
// game_id (0 kung fu) with clip rounds and ammo in reserve. Returns the weapon now in hand;
// drop is upstream's !ammo && !clip (BOT_TASK_DROP_WEAPON).
stock botweapons_weapon_info(id, game_id, clip, ammo, &bool:drop)
{
	new w = botweapons_record(id, game_id, clip, ammo);
	drop = ammo == 0 && clip == 0;
	return w;
}

// bot/weapons.rs BotWeapons::sync_inventory (design §4): the weapons the game says the bot owns
// (world_inventory) are recorded as WeaponInfo records the one in hand; one no longer owned is
// forgotten. Kung fu isn't listed and stays. False (nothing changed) when the inventory couldn't
// be read (world_inventory -1, the Rust None): a failed read mustn't forget every gun.
stock bool:botweapons_sync_inventory(id)
{
	new n = world_inventory(id);
	if (n < 0)
	{
		return false;
	}
	for (new w = 0; w < RC_MAX_WEAPONS; w++)
	{
		if (!g_bw_weapon[id][w][BW_HAS_WEAPON] || g_bw_weapon[id][w][BW_ID] == RC_KUNGFU)
		{
			continue;
		}
		new bool:owned = false;
		for (new k = 0; k < n; k++)
		{
			if (ts_weapon_from_game_id(world_inventory_field(id, k, TSINV_WEAPON)) == g_bw_weapon[id][w][BW_ID])
			{
				owned = true;
				break;
			}
		}
		if (!owned)
		{
			for (new Bw_I:f; f < Bw_I; f++) g_bw_weapon[id][w][f] = 0;
		}
	}
	for (new k = 0; k < n; k++)
	{
		new game_id = world_inventory_field(id, k, TSINV_WEAPON);
		if (game_id > 0)
		{
			botweapons_record(id, game_id, world_inventory_field(id, k, TSINV_CLIP), world_inventory_field(id, k, TSINV_AMMO));
		}
	}
	return true;
}

// bot/weapons.rs BotWeapons::best_weapon_id (CBotWeapons::GetBestWeaponId, TS): among the
// weapons the bot has (melee only within 40 units of an enemy, [APG]RoboCop's TS rule), the
// highest priority one that can fire or just needs a reload; else the best of any; else kung
// fu. Ties go to the first by id. enemy_dist < 0.0 is "no enemy". need_weapons is the second
// value (SetWeaponsNeeded + BOT_CONDITION_NEED_WEAPONS).
stock botweapons_best_weapon_id(id, Float:enemy_dist, bool:enemy_too_high, bool:underwater, &bool:need_weapons)
{
	new bool:has_enemy = enemy_dist >= 0.0;
	new Float:fire_dist = has_enemy ? enemy_dist : 0.0;
	new usable = -1, other = -1;
	for (new w = 1; w < RC_MAX_WEAPONS; w++)
	{
		if (!g_bw_weapon[id][w][BW_HAS_WEAPON])
		{
			continue;
		}
		if (underwater && !botweapons_can_be_used_underwater(id, w))
		{
			continue;
		}
		new bool:use = false, bool:listed = false;
		if (botweapons_is_melee(id, w))
		{
			if (enemy_too_high)
			{
				continue;
			}
			listed = true;
			use = !(has_enemy && enemy_dist > 40.0);
		}
		else if (!botweapons_can_shoot_primary(id, w, underwater, fire_dist))
		{
			if (botweapons_need_to_reload(id, w))
			{
				listed = true;
				use = true;
			}
		}
		else
		{
			listed = true;
			use = !botweapons_out_of_ammo(id, w);
		}
		// std::max_element: the first of the highest priority.
		if (listed && (other == -1 || botweapons_priority(id, other) < botweapons_priority(id, w)))
		{
			other = w;
		}
		if (use && (usable == -1 || botweapons_priority(id, usable) < botweapons_priority(id, w)))
		{
			usable = w;
		}
	}
	if (usable != -1)
	{
		need_weapons = false;
		return g_bw_weapon[id][usable][BW_ID];
	}
	need_weapons = true;
	return other != -1 ? g_bw_weapon[id][other][BW_ID] : RC_KUNGFU;
}

// bot/weapons.rs BotWeapon::is_melee: no info counts as melee. The BotWeapon accessors answer
// as an unset weapon for an id out of range.
stock bool:botweapons_is_melee(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return true;
	}
	return !g_bw_weapon[id][w][BW_INFO] || weapons_is_melee(w);
}

// bot/weapons.rs BotWeapon::clip_is_empty.
stock bool:botweapons_clip_is_empty(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return false;
	}
	if (botweapons_is_melee(id, w) || g_bw_weapon[id][w][BW_MAX_CLIP] == 0)
	{
		return false;
	}
	return g_bw_weapon[id][w][BW_CLIP] <= 0;
}

// bot/weapons.rs BotWeapon::out_of_ammo (OutOfAmmo without m_iAmmo1, always so in TS).
stock bool:botweapons_out_of_ammo(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return true;
	}
	return g_bw_weapon[id][w][BW_CLIP] <= 0 && g_bw_weapon[id][w][BW_RESERVE] <= 0;
}

// bot/weapons.rs BotWeapon::low_on_ammo (TS: under half the clip seen; upstream's was always
// false, design §5).
stock bool:botweapons_low_on_ammo(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return false;
	}
	new max_clip = g_bw_weapon[id][w][BW_MAX_CLIP];
	return !botweapons_is_melee(id, w) && max_clip > 0 && g_bw_weapon[id][w][BW_CLIP] * 2 < max_clip;
}

// bot/weapons.rs BotWeapon::can_reload (TS: room in the clip and rounds in reserve; design §5).
stock bool:botweapons_can_reload(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return false;
	}
	new max_clip = g_bw_weapon[id][w][BW_MAX_CLIP];
	return !botweapons_is_melee(id, w) && max_clip > 0 && g_bw_weapon[id][w][BW_CLIP] < max_clip && g_bw_weapon[id][w][BW_RESERVE] > 0;
}

// bot/weapons.rs BotWeapon::need_to_reload (TS: an empty clip with a reserve, for a gun whose
// clip was seen).
stock bool:botweapons_need_to_reload(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return false;
	}
	if (botweapons_is_melee(id, w) || g_bw_weapon[id][w][BW_MAX_CLIP] == 0)
	{
		return false;
	}
	return g_bw_weapon[id][w][BW_CLIP] <= 0 && g_bw_weapon[id][w][BW_RESERVE] > 0;
}

// bot/weapons.rs BotWeapon::can_be_used_underwater (no info: yes).
stock bool:botweapons_can_be_used_underwater(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return true;
	}
	return !g_bw_weapon[id][w][BW_INFO] || weapons_can_be_used_underwater(w);
}

// bot/weapons.rs BotWeapon::priority (GetPriority; no info: 0).
stock botweapons_priority(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return 0;
	}
	return g_bw_weapon[id][w][BW_INFO] ? weapons_priority(w) : 0;
}

// bot/weapons.rs BotWeapon::can_shoot_primary (CanShootPrimary(pEdict, flFireDist, flWallDist)).
stock bool:botweapons_can_shoot_primary(id, w, bool:underwater, Float:fire_dist)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return true;
	}
	if (!g_bw_weapon[id][w][BW_INFO])
	{
		return true;
	}
	return !(botweapons_out_of_ammo(id, w) || botweapons_clip_is_empty(id, w) || (underwater && !botweapons_can_be_used_underwater(id, w))
		|| !weapons_can_use_primary(w) || !weapons_primary_in_range(w, fire_dist));
}

// mod.rs Bot::reload, guarded (design §4): only a gun whose clip has room and whose reserve has
// rounds, once per RELOAD_RETRY (or sooner when the clip changes: frame_sync), so the key is
// never spammed. With kung fu or a blade TS's reload key is the block, which kungfu presses.
stock botweapons_reload(id)
{
	new w = g_bw_i[id][BW_CURRENT];
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return;
	}
	new max_clip = g_bw_weapon[id][w][BW_MAX_CLIP];
	if (botweapons_is_melee(id, w) || g_bw_weapon[id][w][BW_RESERVE] <= 0 || (max_clip > 0 && g_bw_weapon[id][w][BW_CLIP] >= max_clip)
		|| g_bw_f[id][BW_NEXT_RELOAD_TIME] > g_now)
	{
		return;
	}
	g_bw_f[id][BW_NEXT_RELOAD_TIME] = g_now + RELOAD_RETRY;
	g_co_i[id][CO_BUTTON] |= IN_RELOAD;
}
