// rcbot/tsweapons.inc: The Specialists 3.0 weapon table.
// Owner: combat. Ports weapons/ts.rs (TsWeapon, from_id, id, model, name, from_game_id) and weapons/ts_data.rs (GunKind, GunData, gun_data, is_melee_or_thrown, needs_fresh_press, press_interval, range_multiplier, grow_penalty, decay_penalty).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported.

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

// ts_data.rs GunKind (weapons.toml's kind).
enum
{
	GK_PISTOL = 0,		// a handgun (the sawed-off counts as one, as in TS)
	GK_SMG,
	GK_RIFLE,			// the USAS-12 and the M60 too
	GK_SNIPER,			// the Barrett
	GK_SHOTGUN,
	GK_AKIMBO,
	GK_GRENADE,			// the M61
	GK_KNIFE,			// thrown with +attack2
	GK_KATANA
};

// ts_data.rs mode: TS 3.0's fire modes (TSPS_FIREMODE).
#define TS_MODE_AUTO		0
#define TS_MODE_SEMI		1
#define TS_MODE_BURST		2
#define TS_MODE_PUMP		3
#define TS_MODE_AKIMBO_SEMI	4
#define TS_MODE_AKIMBO_AUTO	5

// weapons/ts.rs ids the fight treats specially.
#define TSW_ID_DEAGLE		12
#define TSW_ID_AK47			13
#define TSW_ID_BARRETT		18
#define TSW_ID_M61			24
#define TSW_ID_M16			27
#define TSW_ID_RAGING_BULL	31
#define TSW_ID_M60			32
#define TSW_ID_CONTENDER	36

// ts_data.rs GunData fields, by RCBot id; GD_KNOWN 0 is gun_data's None (kung fu, C4, unknown).
enum Gd_I
{
	GD_KNOWN,				// gun_data is Some
	GD_KIND,				// GunKind
	GD_HAS_AUTO,			// an automatic mode (semi then fires every 0.1 s, semi_auto_delay)
	GD_SCALE_SPREAD,		// shots add the recoil penalty
	GD_CLIP					// full clip
};
stock g_gd_i[RC_MAX_WEAPONS][Gd_I];

enum Gd_F
{
	Float:GD_RATE,			// seconds between shots
	Float:GD_RELOAD,		// reload time, seconds (a shotgun's per shell)
	Float:GD_DAMAGE,		// per pellet
	Float:GD_PELLETS,		// per shot (1 for bullets)
	Float:GD_ACCURACY,		// base spread
	Float:GD_ACC_KICK,		// the penalty per shot
	Float:GD_RANGE_UNIT,	// the ammo's range unit: full damage to 10 units, 1/sqrt(units / 10) beyond
	Float:GD_VELOCITY		// muzzle velocity, m/s
};
stock Float:g_gd_f[RC_MAX_WEAPONS][Gd_F];

// ts_data.rs AMMO: (range unit, velocity) by ammo id (ammo.toml).
stock const Float:g_ts_ammo[14][2] =
{
	{80.0, 330.0}, {80.0, 330.0}, {90.0, 280.0}, {90.0, 350.0}, {150.0, 900.0}, {90.0, 480.0}, {170.0, 790.0},
	{100.0, 730.0}, {200.0, 1000.0}, {90.0, 450.0}, {70.0, 350.0}, {20.0, 400.0}, {40.0, 300.0}, {170.0, 850.0}
};

// One ROWS entry: (id, kind, has_auto, rate, reload, damage, pellets, accuracy, scale_spread, acc_kick, ammo, clip).
stock tsweapons_gun(id, kind, bool:has_auto, Float:rate, Float:reload, Float:damage, Float:pellets, Float:accuracy, bool:scale_spread, Float:acc_kick, ammo, clip)
{
	g_gd_i[id][GD_KNOWN] = 1;
	g_gd_i[id][GD_KIND] = kind;
	g_gd_i[id][GD_HAS_AUTO] = _:has_auto;
	g_gd_i[id][GD_SCALE_SPREAD] = _:scale_spread;
	g_gd_i[id][GD_CLIP] = clip;
	g_gd_f[id][GD_RATE] = rate;
	g_gd_f[id][GD_RELOAD] = reload;
	g_gd_f[id][GD_DAMAGE] = damage;
	g_gd_f[id][GD_PELLETS] = pellets;
	g_gd_f[id][GD_ACCURACY] = accuracy;
	g_gd_f[id][GD_ACC_KICK] = acc_kick;
	g_gd_f[id][GD_RANGE_UNIT] = g_ts_ammo[ammo][0];
	g_gd_f[id][GD_VELOCITY] = g_ts_ammo[ammo][1];
}

// ts_data.rs ROWS / gun_data: the table (weapons.toml, ammo.toml), once at plugin_init.
stock tsweapons_init()
{
	for (new w = 0; w < RC_MAX_WEAPONS; w++)
	{
		g_gd_i[w][GD_KNOWN] = 0;
	}
	tsweapons_gun(1, GK_PISTOL, true, 0.054, 1.5, 24.0, 1.0, 0.01, true, 0.0065, 1, 17);
	tsweapons_gun(2, GK_PISTOL, false, 0.1, 1.2, 27.0, 1.0, 0.01, true, 0.0, 1, 15);
	tsweapons_gun(3, GK_SMG, true, 0.06, 2.2, 20.0, 1.0, 0.02, true, 0.0065, 1, 32);
	tsweapons_gun(4, GK_SHOTGUN, false, 0.225, 0.3, 12.0, 8.0, 0.035, false, 0.0, 3, 7);
	tsweapons_gun(5, GK_RIFLE, true, 0.07, 2.8, 33.0, 1.0, 0.005, true, 0.004, 4, 30);
	tsweapons_gun(6, GK_SMG, true, 0.074, 2.3, 19.0, 1.0, 0.005, true, 0.005, 1, 30);
	tsweapons_gun(7, GK_SMG, true, 0.066, 2.3, 23.0, 1.0, 0.02, true, 0.003, 1, 30);
	tsweapons_gun(8, GK_AKIMBO, false, 0.1, 2.4, 27.0, 1.0, 0.02, true, 0.0, 1, 30);
	tsweapons_gun(9, GK_PISTOL, false, 0.1, 1.5, 33.0, 1.0, 0.01, true, 0.0, 2, 12);
	tsweapons_gun(10, GK_AKIMBO, false, 0.1, 2.5, 33.0, 1.0, 0.02, true, 0.0, 2, 24);
	tsweapons_gun(11, GK_RIFLE, true, 0.166, 3.0, 7.0, 8.0, 0.065, false, 0.0, 3, 20);
	tsweapons_gun(12, GK_PISTOL, false, 0.1, 2.1, 49.0, 1.0, 0.005, true, 0.0125, 5, 7);
	tsweapons_gun(13, GK_RIFLE, true, 0.08, 2.5, 39.0, 1.0, 0.0025, true, 0.0001, 6, 30);
	tsweapons_gun(14, GK_PISTOL, false, 0.1, 1.5, 23.0, 1.0, 0.0025, true, 0.01, 7, 20);
	tsweapons_gun(15, GK_RIFLE, true, 0.092, 2.4, 29.0, 1.0, 0.005, true, 0.01, 4, 30);
	tsweapons_gun(16, GK_AKIMBO, true, 0.06, 3.0, 20.0, 1.0, 0.04, true, 0.0065, 1, 64);
	tsweapons_gun(17, GK_SMG, true, 0.07, 1.0, 16.0, 1.0, 0.025, true, 0.0015, 12, 20);
	tsweapons_gun(18, GK_SNIPER, false, 0.7, 2.0, 185.0, 1.0, 0.001, true, 0.1, 8, 5);
	tsweapons_gun(19, GK_SMG, true, 0.063, 2.5, 16.0, 1.0, 0.005, true, 0.0065, 7, 40);
	tsweapons_gun(20, GK_SHOTGUN, false, 0.2, 0.3, 12.0, 8.0, 0.05, false, 0.0, 3, 8);
	tsweapons_gun(21, GK_AKIMBO, false, 0.1, 2.3, 40.0, 1.0, 0.0075, true, 0.0, 2, 16);
	tsweapons_gun(22, GK_PISTOL, false, 0.1, 2.0, 39.0, 1.0, 0.01, true, 0.0125, 9, 15);
	tsweapons_gun(23, GK_SMG, true, 0.092, 2.5, 31.0, 1.0, 0.015, true, 0.0, 2, 25);
	tsweapons_gun(24, GK_GRENADE, true, 1.0, 1.0, 110.0, 1.0, 0.1, false, 0.0, 0, 1);
	tsweapons_gun(25, GK_KNIFE, true, 0.4, 1.0, 30.0, 1.0, 0.1, false, 0.0, 0, 1);
	tsweapons_gun(26, GK_SHOTGUN, false, 0.25, 0.3, 16.0, 8.0, 0.045, false, 0.0, 3, 8);
	tsweapons_gun(27, GK_RIFLE, false, 0.07, 2.8, 40.0, 1.0, 0.0015, true, 0.005, 4, 18);
	tsweapons_gun(28, GK_PISTOL, false, 0.1, 1.3, 15.0, 1.0, 0.005, true, 0.008, 10, 17);
	tsweapons_gun(30, GK_AKIMBO, false, 0.1, 3.0, 23.0, 1.0, 0.005, true, 0.01, 7, 40);
	tsweapons_gun(31, GK_PISTOL, false, 0.15, 2.0, 95.0, 1.0, 0.0025, true, 0.0, 11, 5);
	tsweapons_gun(32, GK_RIFLE, true, 0.109, 3.0, 33.0, 1.0, 0.015, true, 0.005, 4, 100);
	tsweapons_gun(33, GK_PISTOL, false, 0.1, 1.7, 21.0, 8.0, 0.07, false, 0.0, 3, 2);
	tsweapons_gun(34, GK_KATANA, true, 0.7, 1.0, 45.0, 1.0, 0.7, false, 0.0, 0, 1);
	tsweapons_gun(35, GK_KNIFE, true, 0.25, 1.0, 20.0, 1.0, 0.1, false, 0.0, 0, 1);
	tsweapons_gun(36, GK_PISTOL, false, 0.25, 1.35, 100.0, 1.0, 0.001, false, 0.0, 13, 1);
	tsweapons_gun(37, GK_AKIMBO, true, 0.07, 2.0, 16.0, 1.0, 0.05, true, 0.003, 12, 40);
}

// ts_data.rs gun_data(id).is_some().
stock bool:tsweapons_known(w)
{
	return w > 0 && w < RC_MAX_WEAPONS && g_gd_i[w][GD_KNOWN] != 0;
}

// ts_data.rs GunData::is_melee_or_thrown: a blade or the grenade (w must be known).
stock bool:tsweapons_is_melee_or_thrown(w)
{
	new k = g_gd_i[w][GD_KIND];
	return k == GK_GRENADE || k == GK_KNIFE || k == GK_KATANA;
}

// ts_data.rs GunData::needs_fresh_press: whether +attack fires only on a fresh press in
// fire_mode (CTSGun::PrimaryAttack: semi and akimbo semi unless the double fire rate power-up
// runs; burst starts on a press). False for an unknown id.
stock bool:tsweapons_needs_fresh_press(w, fire_mode, bool:double_fire_rate)
{
	if (!tsweapons_known(w) || tsweapons_is_melee_or_thrown(w))
	{
		return false;
	}
	switch (fire_mode)
	{
		case TS_MODE_SEMI, TS_MODE_AKIMBO_SEMI:
		{
			return !double_fire_rate;
		}
		case TS_MODE_BURST:
		{
			return true;
		}
	}
	return false;
}

// ts_data.rs GunData::press_interval: the fewest seconds between two presses that each fire:
// semi is 0.1 s for a gun with an automatic mode, else its rate; a semi shotgun 0.15 s; a burst
// three rounds 0.03 s apart and 0.2 s after.
stock Float:tsweapons_press_interval(w, fire_mode)
{
	if (fire_mode == TS_MODE_BURST)
	{
		return 0.26;
	}
	if (fire_mode == TS_MODE_SEMI)
	{
		if (g_gd_i[w][GD_KIND] == GK_SHOTGUN)
		{
			return 0.15;
		}
		if (g_gd_i[w][GD_HAS_AUTO])
		{
			return 0.1;
		}
	}
	return g_gd_f[w][GD_RATE];
}

// ts_data.rs GunData::range_multiplier: the damage multiplier at dist units.
stock Float:tsweapons_range_multiplier(w, Float:dist)
{
	new Float:idx = fclamp(dist / g_gd_f[w][GD_RANGE_UNIT], 1.0, 9999.0);
	return fmin(1.0 / floatsqroot(idx * 0.1), 1.0);
}

// ts_data.rs GunData::grow_penalty (ScaleSpread): the recoil penalty after a shot fired elapsed
// seconds after the last at time_scale, into pen; false (pen untouched) below 1 ms.
stock bool:tsweapons_grow_penalty(w, &Float:pen, Float:elapsed, Float:time_scale, bool:double_fire_rate, Float:kick_scale)
{
	if (!g_gd_i[w][GD_SCALE_SPREAD])
	{
		return true;
	}
	if (elapsed < 0.001)
	{
		return false;
	}
	new Float:p = time_scale * elapsed;
	new Float:x = p >= 1.0 ? 0.5 : p;
	new Float:k = double_fire_rate ? 1.0 : 2.0;
	pen = fmin(pen + (1.0 - x) * g_gd_f[w][GD_ACC_KICK] * kick_scale * k, 0.1);
	return true;
}

// ts_data.rs decay_penalty (PreThink): 0.1 a second of the player's time.
stock Float:tsweapons_decay_penalty(Float:pen, Float:dt, Float:time_scale)
{
	return fmax(pen - dt * fmax(time_scale, 0.0) * 0.1, 0.0);
}
