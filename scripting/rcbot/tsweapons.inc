// rcbot/tsweapons.inc: The Specialists 3.0 weapon table.
// Owner: combat. Ports weapons/ts.rs (TsWeapon, from_id, id, model, name, from_game_id) and weapons/ts_data.rs (GunKind, GunData, gun_data, is_melee_or_thrown, needs_fresh_press, press_interval, range_multiplier, grow_penalty, decay_penalty).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: the id conversion and names are ported; the gun data table is TODO(tsweapons).

#if defined _rcbot_tsweapons_included
	#endinput
#endif
#define _rcbot_tsweapons_included

// TsWeapon::from_game_id: the game reports kung fu as 0 (TSBOT_KUNGFU), RCBot numbers it 38.
stock ts_weapon_from_game_id(game_id)
{
	return game_id == TSBOT_KUNGFU ? RC_KUNGFU : game_id;
}

// The game's id of an RCBot id (ts_bot_select_weapon, ts_bot_buy).
stock ts_weapon_to_game_id(w)
{
	return w == RC_KUNGFU ? TSBOT_KUNGFU : w;
}

// TsWeapon::name: bot_weapons.ini's comment names without "TS_", by RCBot id (0 unused).
stock const g_ts_weapon_names[RC_MAX_WEAPONS][] =
{
	"", "Glock18", "Beretta", "MiniUzi", "BenelliM3", "M4A1", "MP5SD", "MP5K", "AkimboBerettas",
	"SocomMk23", "AkimboSocomMk23", "Usas12", "DesertEagle", "Ak47", "FiveSeven", "SteyrAug",
	"AkimboMiniUzis", "Skorpion", "BarrettM82", "HKPdw", "Spas12", "Akimbocolts", "Glock20", "Ump45",
	"M61Grenade", "CombatKnife", "Mossberg500", "M16A4", "RugerMk1", "C4", "AkimboFiveSeven",
	"RagingBull", "M60", "Sawedoff", "Katana", "SealKnife", "ContenderG2", "AkimboSkorpions", "KungFu"
};

// ts_data.rs GunData fields, by RCBot id.
enum Gd_I
{
	GD_KIND,				// GunKind
	GD_KNOWN				// gun_data is Some
};
stock g_gd_i[RC_MAX_WEAPONS][Gd_I];

enum Gd_F
{
	Float:GD_RELOAD			// reload time, seconds
};
stock Float:g_gd_f[RC_MAX_WEAPONS][Gd_F];

// TODO(tsweapons): ts_data.rs gun_data: fill g_gd_* (the table) once at plugin_init.
stock tsweapons_init()
{
}

// TODO(tsweapons): ts_data.rs GunData::needs_fresh_press.
stock bool:tsweapons_needs_fresh_press(w, fire_mode, bool:double_fire_rate)
{
	#pragma unused w, fire_mode, double_fire_rate
	return true;
}

// TODO(tsweapons): ts_data.rs GunData::press_interval.
stock Float:tsweapons_press_interval(w, fire_mode)
{
	#pragma unused w, fire_mode
	return 0.1;
}

// TODO(tsweapons): ts_data.rs GunData::range_multiplier.
stock Float:tsweapons_range_multiplier(w, Float:dist)
{
	#pragma unused w, dist
	return 1.0;
}

// TODO(tsweapons): ts_data.rs GunData::grow_penalty: false for None.
stock bool:tsweapons_grow_penalty(w, &Float:pen, Float:elapsed, Float:time_scale, bool:double_fire_rate, Float:kick_scale)
{
	#pragma unused w, pen, elapsed, time_scale, double_fire_rate, kick_scale
	return false;
}

// TODO(tsweapons): ts_data.rs decay_penalty.
stock Float:tsweapons_decay_penalty(Float:pen, Float:dt, Float:time_scale)
{
	#pragma unused dt, time_scale
	return pen;
}
