// rcbot/weapons.inc: the weapon registry (CWeapons) and bot_weapons.ini presets (CWeaponPresets).
// Owner: combat. Ports weapons/mod.rs (WeaponInfo can_be_used_underwater, can_use_primary, can_use_secondary, priority, primary_in_range, secondary_in_range, prim_max_range, prim_min_range, is_melee; Weapons add, get, clear), weapons/presets.rs (WeaponPreset, WeaponPresets::parse, get: the last preset for an id wins), host lib.rs game_init's bot_weapons.ini read.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: the registry layout is defined; reading bot_weapons.ini ([mod_id=8] sections) is TODO(weapons).

#if defined _rcbot_weapons_included
	#endinput
#endif
#define _rcbot_weapons_included

// weapons/mod.rs MOD_TS: The Specialists' [mod_id=...] in bot_weapons.ini.
#define MOD_TS			8

// WeaponInfo by RCBot id (0..38), from the preset (bot_weapons.ini) and the game's weapon info.
enum Wp_I
{
	WP_REGISTERED,		// Weapons::get is Some
	WP_UNDERWATER,
	WP_PRIMARY_FIRE,
	WP_SECONDARY_FIRE,
	WP_IS_MELEE,
	WP_PRIORITY,
	WP_MAX_CLIP
};
stock g_wp_i[RC_MAX_WEAPONS][Wp_I];

enum Wp_F
{
	Float:WP_PRIM_MIN_RANGE,
	Float:WP_PRIM_MAX_RANGE,
	Float:WP_SEC_MIN_RANGE,
	Float:WP_SEC_MAX_RANGE
};
stock Float:g_wp_f[RC_MAX_WEAPONS][Wp_F];

// TODO(weapons): host lib.rs game_init: WeaponPresets::parse(bot_weapons.ini, MOD_TS) with its error lines. Called from plugin_init.
stock weapons_read_presets()
{
}

// TODO(weapons): weapons/mod.rs Weapons::clear (ServerActivate).
stock weapons_clear()
{
}

// TODO(weapons): weapons/mod.rs Weapons::add (from the preset).
stock weapons_add(w)
{
	#pragma unused w
}

// TODO(weapons): weapons/mod.rs WeaponInfo::primary_in_range.
stock bool:weapons_primary_in_range(w, Float:range)
{
	#pragma unused w, range
	return false;
}

// TODO(weapons): weapons/mod.rs WeaponInfo::secondary_in_range.
stock bool:weapons_secondary_in_range(w, Float:range)
{
	#pragma unused w, range
	return false;
}
