// rcbot/weapons.inc: the weapon registry (CWeapons) and bot_weapons.ini presets (CWeaponPresets).
// Owner: combat. Ports weapons/mod.rs (WeaponInfo can_be_used_underwater, can_use_primary, can_use_secondary, priority, primary_in_range, secondary_in_range, prim_max_range, prim_min_range, is_melee; Weapons add, get, clear), weapons/presets.rs (WeaponPreset, WeaponPresets::parse, get: the last preset for an id wins), stoi.rs (std::stoi), host lib.rs game_init's bot_weapons.ini read.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. Only MOD_TS's presets are kept (WeaponPresets::get is only ever asked for
// MOD_TS), each id's last one, as GetPreset's stack answers.

#if defined _rcbot_weapons_included
	#endinput
#endif
#define _rcbot_weapons_included

// weapons/mod.rs MOD_TS: The Specialists' [mod_id=...] in bot_weapons.ini.
#define MOD_TS			8
// weapons/mod.rs BOT_VIEW_DISTANCE: a weapon's max range when it has no preset.
#define WP_NO_PRESET_MAX_RANGE	2048.0

// presets.rs WeaponPreset (weapon_preset_t) of MOD_TS, by id: the last one in the file wins.
enum Wpp_I
{
	WPP_SET,				// WeaponPresets::get(MOD_TS, id) is Some
	WPP_UNDERWATER,
	WPP_PRIMARY_FIRE,
	WPP_SECONDARY_FIRE,
	WPP_IS_MELEE,
	WPP_PRIORITY,
	WPP_MAX_CLIP
};
stock g_wpp_i[RC_MAX_WEAPONS][Wpp_I];

enum Wpp_F
{
	Float:WPP_PRIM_MIN_RANGE,
	Float:WPP_PRIM_MAX_RANGE,
	Float:WPP_SEC_MIN_RANGE,
	Float:WPP_SEC_MAX_RANGE
};
stock Float:g_wpp_f[RC_MAX_WEAPONS][Wpp_F];

// WeaponInfo by RCBot id (0..38): the registration (Weapons::get is Some) with its preset copied.
enum Wp_I
{
	WP_REGISTERED,		// Weapons::get is Some
	WP_HAS_PRESET,		// WeaponInfo::preset is Some
	WP_UNDERWATER,
	WP_PRIMARY_FIRE,
	WP_SECONDARY_FIRE,
	WP_IS_MELEE,
	WP_PRIORITY,
	WP_MAX_CLIP,
	WP_AMMO_INDEX1		// m_iAmmoIndex1 (-1 none)
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

// stoi.rs StoiError.
#define STOI_OK				0
#define STOI_INVALID		1	// std::invalid_argument: no digits
#define STOI_OUT_OF_RANGE	2	// std::out_of_range

// stoi.rs stoi (std::stoi, base 10): leading whitespace, an optional sign, digits; whatever
// follows is ignored.
stock weapons_stoi(const s[], &v)
{
	new i = 0;
	while (s[i] == ' ' || s[i] == '^t' || s[i] == '^n' || s[i] == 11 || s[i] == 12 || s[i] == '^r')
	{
		i++;
	}
	new bool:neg = false;
	if (s[i] == '-')
	{
		neg = true;
		i++;
	}
	else if (s[i] == '+')
	{
		i++;
	}
	if (s[i] < '0' || s[i] > '9')
	{
		return STOI_INVALID;
	}
	new r = 0;
	for (; s[i] >= '0' && s[i] <= '9'; i++)
	{
		new d = s[i] - '0';
		// Kept within a cell: negative values accumulate downwards (i32::MIN is valid).
		if (neg)
		{
			if (r < -214748364 || (r == -214748364 && d > 8))
			{
				return STOI_OUT_OF_RANGE;
			}
			r = r * 10 - d;
		}
		else
		{
			if (r > 214748364 || (r == 214748364 && d > 7))
			{
				return STOI_OUT_OF_RANGE;
			}
			r = r * 10 + d;
		}
	}
	v = r;
	return STOI_OK;
}

// presets.rs bracket_value: substr(start, len - start - 1) of a [key=N] line.
stock weapons_bracket_value(const line[], start, out[], len)
{
	new n = strlen(line) - 1;
	new k = 0;
	for (new i = start; i < n && k < len - 1; i++)
	{
		out[k++] = line[i];
	}
	out[k] = 0;
}

// presets.rs PresetError's report: the host logs each line (lib.rs game_init).
stock weapons_report(line_no, const text[], err)
{
	#pragma unused err
	rc_log(TSLOG_WARNING, "bot_weapons.ini line %d: %s", line_no, text);
}

// presets.rs: one preset being read (WeaponPreset, mod_id, id) and stored at [/weapon].
static g_wpr_mod;
static g_wpr_id;
static g_wpr_i[Wpp_I];
static Float:g_wpr_f[Wpp_F];

stock weapons_store_preset()
{
	// Only MOD_TS's are ever asked for (WeaponPresets::get(MOD_TS, id)); a later one replaces.
	if (g_wpr_mod != MOD_TS || g_wpr_id < 0 || g_wpr_id >= RC_MAX_WEAPONS)
	{
		return;
	}
	for (new Wpp_I:k; k < Wpp_I; k++) g_wpp_i[g_wpr_id][k] = g_wpr_i[k];
	for (new Wpp_F:k; k < Wpp_F; k++) g_wpp_f[g_wpr_id][k] = g_wpr_f[k];
	g_wpp_i[g_wpr_id][WPP_SET] = 1;
}

// host lib.rs game_init: WeaponPresets::parse(bot_weapons.ini, MOD_TS) (presets.rs, CWeaponPresets::
// ReadPresets), each line it reports logged as a warning. Called from plugin_init.
stock weapons_read_presets()
{
	for (new w = 0; w < RC_MAX_WEAPONS; w++)
	{
		for (new Wpp_I:k; k < Wpp_I; k++) g_wpp_i[w][k] = 0;
		for (new Wpp_F:k; k < Wpp_F; k++) g_wpp_f[w][k] = 0.0;
	}
	weapons_clear();
	new path[RC_PATH_LEN];
	if (!files_find(path, charsmax(path), "bot_weapons.ini"))
	{
		return;
	}
	new f = fopen(path, "rt");
	if (!f)
	{
		return;
	}
	static line[512], value[512];
	new line_no = 0, mod_id = 0, bool:skip_mod = false, v, err;
	g_wpr_mod = 0;
	g_wpr_id = 0;
	for (new Wpp_I:k; k < Wpp_I; k++) g_wpr_i[k] = 0;
	for (new Wpp_F:k; k < Wpp_F; k++) g_wpr_f[k] = 0.0;
	while (fgets(f, line, charsmax(line)) > 0)
	{
		line_no++;
		// std::getline: the line without its '\n'.
		new n = strlen(line);
		if (n > 0 && line[n - 1] == '^n')
		{
			line[--n] = 0;
		}
		// Empty or '#' before trimming (an indented '#' is no comment), then trailing whitespace.
		if (n == 0 || line[0] == '#')
		{
			continue;
		}
		while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '^n' || line[n - 1] == '^r' || line[n - 1] == '^t'))
		{
			line[--n] = 0;
		}
		if (n == 0)
		{
			continue;
		}
		if (equal(line, "[mod_id=", 8))
		{
			weapons_bracket_value(line, 8, value, charsmax(value));
			err = weapons_stoi(value, v);
			if (err == STOI_OK)
			{
				mod_id = v;
				skip_mod = mod_id != MOD_TS;
			}
			else
			{
				weapons_report(line_no, line, err);
			}
		}
		else if (skip_mod)
		{
			continue;
		}
		else if (equal(line, "[weapon_id=", 11))
		{
			for (new Wpp_I:k; k < Wpp_I; k++) g_wpr_i[k] = 0;
			for (new Wpp_F:k; k < Wpp_F; k++) g_wpr_f[k] = 0.0;
			g_wpr_mod = mod_id;
			g_wpr_id = 0;
			weapons_bracket_value(line, 11, value, charsmax(value));
			err = weapons_stoi(value, v);
			if (err == STOI_OK)
			{
				g_wpr_id = v;
			}
			else
			{
				weapons_report(line_no, line, err);
			}
		}
		else if (equal(line, "[/weapon]"))
		{
			weapons_store_preset();
		}
		else
		{
			new eq = contain(line, "=");
			if (eq < 0)
			{
				continue;
			}
			err = weapons_stoi(line[eq + 1], v);
			if (err != STOI_OK)
			{
				weapons_report(line_no, line, err);
				continue;
			}
			line[eq] = 0;
			if (equal(line, "underwater")) g_wpr_i[WPP_UNDERWATER] = _:(v != 0);
			else if (equal(line, "primaryfire")) g_wpr_i[WPP_PRIMARY_FIRE] = _:(v != 0);
			else if (equal(line, "secondaryfire")) g_wpr_i[WPP_SECONDARY_FIRE] = _:(v != 0);
			else if (equal(line, "primary_min_range")) g_wpr_f[WPP_PRIM_MIN_RANGE] = float(v);
			else if (equal(line, "primary_max_range")) g_wpr_f[WPP_PRIM_MAX_RANGE] = float(v);
			else if (equal(line, "secondary_min_range")) g_wpr_f[WPP_SEC_MIN_RANGE] = float(v);
			else if (equal(line, "secondary_max_range")) g_wpr_f[WPP_SEC_MAX_RANGE] = float(v);
			else if (equal(line, "is_melee")) g_wpr_i[WPP_IS_MELEE] = _:(v != 0);
			else if (equal(line, "priority")) g_wpr_i[WPP_PRIORITY] = v;
			else if (equal(line, "maxclip")) g_wpr_i[WPP_MAX_CLIP] = v;
		}
	}
	fclose(f);
}

// weapons/mod.rs Weapons::clear (CWeapons::Init, ServerActivate): forgets every weapon. The
// plugin is reloaded every map, so plugin_init's read does it.
stock weapons_clear()
{
	for (new w = 0; w < RC_MAX_WEAPONS; w++)
	{
		for (new Wp_I:k; k < Wp_I; k++) g_wp_i[w][k] = 0;
		for (new Wp_F:k; k < Wp_F; k++) g_wp_f[w][k] = 0.0;
	}
}

// weapons/mod.rs Weapons::add (CWeapons::AddWeapon) as bot/weapons.rs record calls it for TS
// (classname "tsweapon", ammo index 0): registers w once, with MOD_TS's preset for it if any.
stock weapons_add(w)
{
	if (w < 0 || w >= RC_MAX_WEAPONS || g_wp_i[w][WP_REGISTERED])
	{
		return;
	}
	g_wp_i[w][WP_REGISTERED] = 1;
	g_wp_i[w][WP_AMMO_INDEX1] = 0;
	if (g_wpp_i[w][WPP_SET])
	{
		g_wp_i[w][WP_HAS_PRESET] = 1;
		g_wp_i[w][WP_UNDERWATER] = g_wpp_i[w][WPP_UNDERWATER];
		g_wp_i[w][WP_PRIMARY_FIRE] = g_wpp_i[w][WPP_PRIMARY_FIRE];
		g_wp_i[w][WP_SECONDARY_FIRE] = g_wpp_i[w][WPP_SECONDARY_FIRE];
		g_wp_i[w][WP_IS_MELEE] = g_wpp_i[w][WPP_IS_MELEE];
		g_wp_i[w][WP_PRIORITY] = g_wpp_i[w][WPP_PRIORITY];
		g_wp_i[w][WP_MAX_CLIP] = g_wpp_i[w][WPP_MAX_CLIP];
		g_wp_f[w][WP_PRIM_MIN_RANGE] = g_wpp_f[w][WPP_PRIM_MIN_RANGE];
		g_wp_f[w][WP_PRIM_MAX_RANGE] = g_wpp_f[w][WPP_PRIM_MAX_RANGE];
		g_wp_f[w][WP_SEC_MIN_RANGE] = g_wpp_f[w][WPP_SEC_MIN_RANGE];
		g_wp_f[w][WP_SEC_MAX_RANGE] = g_wpp_f[w][WPP_SEC_MAX_RANGE];
	}
	else
	{
		// No preset: the base class's answers (usable underwater, both fire modes, any range,
		// priority 0, melee only without a primary ammo type).
		g_wp_i[w][WP_HAS_PRESET] = 0;
		g_wp_i[w][WP_UNDERWATER] = 1;
		g_wp_i[w][WP_PRIMARY_FIRE] = 1;
		g_wp_i[w][WP_SECONDARY_FIRE] = 1;
		g_wp_i[w][WP_IS_MELEE] = _:(g_wp_i[w][WP_AMMO_INDEX1] == -1);
		g_wp_i[w][WP_PRIORITY] = 0;
		g_wp_i[w][WP_MAX_CLIP] = 0;
	}
}

// weapons/mod.rs Weapons::get(w).is_some().
stock bool:weapons_registered(w)
{
	return w >= 0 && w < RC_MAX_WEAPONS && g_wp_i[w][WP_REGISTERED] != 0;
}

// weapons/mod.rs WeaponInfo::can_be_used_underwater / can_use_primary / can_use_secondary /
// priority / is_melee of a registered w (the preset's, or the defaults above).
stock bool:weapons_can_be_used_underwater(w)
{
	return g_wp_i[w][WP_UNDERWATER] != 0;
}

stock bool:weapons_can_use_primary(w)
{
	return g_wp_i[w][WP_PRIMARY_FIRE] != 0;
}

stock bool:weapons_can_use_secondary(w)
{
	return g_wp_i[w][WP_SECONDARY_FIRE] != 0;
}

stock weapons_priority(w)
{
	return g_wp_i[w][WP_PRIORITY];
}

stock bool:weapons_is_melee(w)
{
	return g_wp_i[w][WP_IS_MELEE] != 0;
}

// weapons/mod.rs WeaponInfo::primary_in_range: min <= range <= max (any range without a preset).
stock bool:weapons_primary_in_range(w, Float:range)
{
	return !g_wp_i[w][WP_HAS_PRESET] || (range >= g_wp_f[w][WP_PRIM_MIN_RANGE] && range <= g_wp_f[w][WP_PRIM_MAX_RANGE]);
}

// weapons/mod.rs WeaponInfo::secondary_in_range.
stock bool:weapons_secondary_in_range(w, Float:range)
{
	return !g_wp_i[w][WP_HAS_PRESET] || (range >= g_wp_f[w][WP_SEC_MIN_RANGE] && range <= g_wp_f[w][WP_SEC_MAX_RANGE]);
}

// weapons/mod.rs WeaponInfo::prim_max_range (BOT_VIEW_DISTANCE without a preset): what the name
// says, not upstream's swapped PrimMaxRange.
stock Float:weapons_prim_max_range(w)
{
	return g_wp_i[w][WP_HAS_PRESET] ? g_wp_f[w][WP_PRIM_MAX_RANGE] : WP_NO_PRESET_MAX_RANGE;
}

// weapons/mod.rs WeaponInfo::prim_min_range (0 without a preset).
stock Float:weapons_prim_min_range(w)
{
	return g_wp_i[w][WP_HAS_PRESET] ? g_wp_f[w][WP_PRIM_MIN_RANGE] : 0.0;
}
