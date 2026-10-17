// rcbot/botweapons.inc: the weapons a bot owns (CBotWeapons) and its weapon in hand.
// Owner: combat. Ports bot/weapons.rs (BotWeapon is_melee, clip_is_empty, out_of_ammo, low_on_ammo, can_reload, need_to_reload, can_be_used_underwater, priority, can_shoot_primary, set_weapon; BotWeapons get, has, add, remove, best_loaded_gun, weapon_info, record, sync_inventory, best_weapon_id), think.rs Bot::frame's weapon block (BotClient_TS_WeaponInfo, the inventory sync), spawn_init's weapon part, mod.rs Bot::reload / weapon_select_failed.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ensure_kungfu, select_failed and spawn_init are ported; the rest is TODO(botweapons).

#if defined _rcbot_botweapons_included
	#endinput
#endif
#define _rcbot_botweapons_included

// mod.rs Bot::weapon_select_failed: the game refused to take out game_id (0 kung fu): the bot
// doesn't own it; it is forgotten and the inventory is read again next frame.
stock botweapons_select_failed(id, game_id)
{
	new w = ts_weapon_from_game_id(game_id);
	if (w != RC_KUNGFU && w > 0 && w < RC_MAX_WEAPONS)
	{
		botweapons_remove(id, w);
	}
	g_bw_i[id][BW_SWITCH_PENDING] = -1;
	g_bw_f[id][BW_NEXT_INVENTORY_SYNC] = 0.0;
}

// think.rs Bot::frame: TS bots always have kung fu.
stock botweapons_ensure_kungfu(id)
{
	if (!g_bw_weapon[id][RC_KUNGFU][BW_HAS_WEAPON])
	{
		botweapons_add(id, RC_KUNGFU);
	}
}

// think.rs Bot::spawn_init's weapon part: a new life starts with what the game gives it.
stock botweapons_spawn_init(id)
{
	for (new w = 0; w < RC_MAX_WEAPONS; w++)
	{
		for (new Bw_I:k; k < Bw_I; k++) g_bw_weapon[id][w][k] = 0;
	}
	botweapons_add(id, RC_KUNGFU);
	g_bw_i[id][BW_CURRENT] = -1;
	g_bw_i[id][BW_LAST_INFO_SET] = 0;
	g_bw_i[id][BW_SWITCH_PENDING] = -1;
	g_bw_f[id][BW_NEXT_INVENTORY_SYNC] = 0.0;
	g_bw_i[id][BW_INVENTORY_SERIAL] = 0;
	g_bw_f[id][BW_NEXT_RELOAD_TIME] = 0.0;
}

// BotWeapons::add (SetWeapon): the registry's info, m_bHasWeapon.
// TODO(botweapons): has_weapon only when weapons.inc registered the id (set_weapon).
stock botweapons_add(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return;
	}
	for (new Bw_I:k; k < Bw_I; k++) g_bw_weapon[id][w][k] = 0;
	g_bw_weapon[id][w][BW_ID] = w;
	g_bw_weapon[id][w][BW_HAS_WEAPON] = 1;
}

// BotWeapons::remove.
stock botweapons_remove(id, w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS)
	{
		return;
	}
	for (new Bw_I:k; k < Bw_I; k++) g_bw_weapon[id][w][k] = 0;
}

// BotWeapons::has.
stock bool:botweapons_has(id, w)
{
	return w >= 0 && w < RC_MAX_WEAPONS && g_bw_weapon[id][w][BW_HAS_WEAPON] != 0;
}

// TODO(botweapons): mod.rs Bot::new's BotWeapons::default (a new bot).
stock botweapons_bot_init(id)
{
	botweapons_spawn_init(id);
}

// TODO(botweapons): think.rs Bot::frame's weapon block: current weapon from the snapshot, switch_pending, the inventory serial and sync (world_inventory), last_weapon_info and weapon_info (drop task on empty).
stock botweapons_frame_sync(id)
{
	#pragma unused id
}

// TODO(botweapons): bot/weapons.rs BotWeapons::best_loaded_gun: RCBot id, -1 none.
stock botweapons_best_loaded_gun(id)
{
	#pragma unused id
	return -1;
}

// TODO(botweapons): bot/weapons.rs BotWeapons::weapon_info (+ record): returns the current id; drop set when empty.
stock botweapons_weapon_info(id, game_id, clip, ammo, &bool:drop)
{
	#pragma unused id, game_id, clip, ammo
	drop = false;
	return -1;
}

// TODO(botweapons): bot/weapons.rs BotWeapons::sync_inventory (from world_inventory).
stock botweapons_sync_inventory(id)
{
	#pragma unused id
}

// TODO(botweapons): bot/weapons.rs BotWeapons::best_weapon_id: (RCBot id, primary).
stock botweapons_best_weapon_id(id, Float:enemy_dist, bool:enemy_too_high, bool:underwater, &bool:primary)
{
	#pragma unused id, enemy_dist, enemy_too_high, underwater
	primary = true;
	return -1;
}

// TODO(botweapons): bot/weapons.rs BotWeapon::is_melee (no info counts as melee).
stock bool:botweapons_is_melee(id, w)
{
	#pragma unused id, w
	return true;
}

// TODO(botweapons): bot/weapons.rs BotWeapon::clip_is_empty.
stock bool:botweapons_clip_is_empty(id, w)
{
	#pragma unused id, w
	return false;
}

// TODO(botweapons): bot/weapons.rs BotWeapon::out_of_ammo.
stock bool:botweapons_out_of_ammo(id, w)
{
	#pragma unused id, w
	return false;
}

// TODO(botweapons): bot/weapons.rs BotWeapon::low_on_ammo (TS: under half the clip seen).
stock bool:botweapons_low_on_ammo(id, w)
{
	#pragma unused id, w
	return false;
}

// TODO(botweapons): bot/weapons.rs BotWeapon::can_reload.
stock bool:botweapons_can_reload(id, w)
{
	#pragma unused id, w
	return false;
}

// TODO(botweapons): bot/weapons.rs BotWeapon::need_to_reload.
stock bool:botweapons_need_to_reload(id, w)
{
	#pragma unused id, w
	return false;
}

// TODO(botweapons): bot/weapons.rs BotWeapon::can_shoot_primary.
stock bool:botweapons_can_shoot_primary(id, w, bool:underwater, Float:fire_dist)
{
	#pragma unused id, w, underwater, fire_dist
	return false;
}

// TODO(botweapons): mod.rs Bot::reload: guarded (design §4: once per RELOAD_RETRY, never spammed).
stock botweapons_reload(id)
{
	#pragma unused id
}
