// rcbot/world.inc: the bots' view of the server for one frame.
// Owner: core. Ports host world.rs (refresh_frame: the frame info and every player's snapshot
// read once per frame; HostWorld::cached: entities read once per frame; map_items read once per
// world; inventory; game_mode; teamplay / NON_TS_TEAMPLAY / model_teams; trace_line,
// trace_point_hull, point_contents, in_pvs, entities_in_sphere, cvar_float) and lib.rs
// read_masters (CMasterEntities) and the team scores (m_iTeamScores).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Everything a bot reads about players comes from g_ws (the frame's snapshots, refreshed once
// in world_refresh_frame before any bot thinks); a change a native makes shows next frame
// (tsbot.inc). Engine queries (traces, PVS, contents, entvars) are AMX Mod X's engine and
// fakemeta natives. No string work happens here per frame.

#if defined _rcbot_world_included
	#endinput
#endif
#define _rcbot_world_included

#define RC_MAX_ITEMS		128
#define RC_MAX_INVENTORY	16
#define RC_MAX_MASTERS		64

// ---- the frame -------------------------------------------------------------------------------

new g_frame_stamp;				// bumped every refresh: per-frame caches compare against it
new Float:g_now;				// TSFI_TIME (gpGlobals->time)
stock Float:g_frametime;			// TSFI_FRAMETIME
new g_highest_edict;			// TSFI_HIGHEST_EDICT (scan entities up to it)
new bool:g_game_teamplay;		// TSFI_TEAMPLAY
new bool:g_non_ts_teamplay;		// a tm_ map (CBotGlobals::MapInit's NON_TS_TEAMPLAY)
stock g_game_mode;				// TSMODE_*
stock bool:g_game_ctb;
stock g_game_round;				// TSROUND_*
new g_team_scores[4];			// m_iTeamScores by rules team - 1
new g_maxclients;

// The frame's player snapshots (TSPS_*), by player index; g_ws[i][TSPS_USERID] 0 = empty slot.
new g_ws[RC_SLOTS][TSPS_SIZE];

// Lazily read per frame.
new g_items[RC_MAX_ITEMS * TSITM_SIZE];
new g_items_count;
new g_items_stamp = -1;
new g_inv[RC_SLOTS][RC_MAX_INVENTORY * TSINV_SIZE];
new g_inv_count[RC_SLOTS];
new g_inv_stamp[RC_SLOTS] = {-1, ...};
stock g_gs[TSGS_SIZE];
stock g_gs_stamp = -1;

// Per-frame entity cache (HostWorld::cached): entity_origin and effects.
stock g_ent_stamp[RC_MAX_EDICTS];
stock Float:g_ent_origin[RC_MAX_EDICTS][3];
stock g_ent_effects[RC_MAX_EDICTS];

// m_Masters: entities given a "master" key, read once a map at its first frame.
stock g_master_count;
stock g_master_ent[RC_MAX_MASTERS];
stock g_master_name[RC_MAX_MASTERS][32];
new bool:g_masters_read;

new g_tr;						// our trace handle (create_tr2), freed in world_end
new g_pcvar_gravity;

// ---- life cycle ------------------------------------------------------------------------------

// plugin_init: the map's teamplay kind and the handles.
stock world_init(const map[])
{
	g_non_ts_teamplay = equal(map, "tm_", 3) != 0;
	g_tr = create_tr2();
	g_pcvar_gravity = get_cvar_pointer("sv_gravity");
	g_maxclients = get_maxplayers();
	g_masters_read = false;
}

stock world_end()
{
	if (g_tr)
	{
		free_tr2(g_tr);
		g_tr = 0;
	}
}

// world.rs refresh_frame: the frame info and every slot's snapshot, once for every bot and
// forward of this frame.
stock world_refresh_frame()
{
	static fi[TSFI_SIZE];
	g_frame_stamp++;
	if (ts_get_frameinfo(fi) == 1)
	{
		g_now = Float:fi[TSFI_TIME];
		g_frametime = Float:fi[TSFI_FRAMETIME];
		g_highest_edict = fi[TSFI_HIGHEST_EDICT] > 0 ? fi[TSFI_HIGHEST_EDICT] : global_get(glb_maxEntities) - 1;
		g_game_teamplay = fi[TSFI_TEAMPLAY] != 0;
		g_game_ctb = fi[TSFI_CTB] != 0;
		g_game_mode = fi[TSFI_MODE];
		g_game_round = fi[TSFI_ROUND];
		for (new t = 0; t < 4; t++)
		{
			g_team_scores[t] = fi[TSFI_SCORES + t];
		}
	}
	else
	{
		g_now = get_gametime();
		g_frametime = 0.0;
		g_highest_edict = global_get(glb_maxEntities) - 1;
	}
	if (g_highest_edict >= RC_MAX_EDICTS)
	{
		g_highest_edict = RC_MAX_EDICTS - 1;
	}
	// One call per slot (tsbot.md: the API has no single-slot query, so this is the cheapest).
	for (new i = 1; i <= g_maxclients; i++)
	{
		ts_get_player_snapshot(i, g_ws[i]);
	}
	if (!g_masters_read)
	{
		g_masters_read = true;
		world_read_masters();
	}
}

// lib.rs read_masters: CMasterEntities, from every entity's "master" key (ts_entity_key).
stock world_read_masters()
{
	g_master_count = 0;
	new master[32];
	for (new e = 1; e <= g_highest_edict && g_master_count < RC_MAX_MASTERS; e++)
	{
		if (!pev_valid(e))
		{
			continue;
		}
		if (ts_entity_key(e, "master", master, charsmax(master)) > 0)
		{
			g_master_ent[g_master_count] = e;
			copy(g_master_name[g_master_count], charsmax(g_master_name[]), master);
			g_master_count++;
		}
	}
}

// ---- players (the frame's snapshots) -----------------------------------------------------------

stock world_userid(id)
{
	return g_ws[id][TSPS_USERID];
}

stock world_flags(id)
{
	return g_ws[id][TSPS_FLAGS];
}

// PlayerState::alive: alive and not observing.
stock bool:world_alive(id)
{
	return (g_ws[id][TSPS_FLAGS] & TSPF_ALIVE) != 0;
}

// The rules team, 1.. in teamplay, 0 none.
stock world_team(id)
{
	return g_ws[id][TSPS_TEAM];
}

stock world_origin(id, Float:out[3])
{
	out[0] = Float:g_ws[id][TSPS_ORIGIN];
	out[1] = Float:g_ws[id][TSPS_ORIGIN + 1];
	out[2] = Float:g_ws[id][TSPS_ORIGIN + 2];
}

stock world_velocity(id, Float:out[3])
{
	out[0] = Float:g_ws[id][TSPS_VELOCITY];
	out[1] = Float:g_ws[id][TSPS_VELOCITY + 1];
	out[2] = Float:g_ws[id][TSPS_VELOCITY + 2];
}

stock world_vangle(id, Float:out[3])
{
	out[0] = Float:g_ws[id][TSPS_VANGLE];
	out[1] = Float:g_ws[id][TSPS_VANGLE + 1];
	out[2] = Float:g_ws[id][TSPS_VANGLE + 2];
}

// GetGunPosition: origin + view_ofs.
stock world_eyes(id, Float:out[3])
{
	out[0] = Float:g_ws[id][TSPS_ORIGIN] + Float:g_ws[id][TSPS_VIEWOFS];
	out[1] = Float:g_ws[id][TSPS_ORIGIN + 1] + Float:g_ws[id][TSPS_VIEWOFS + 1];
	out[2] = Float:g_ws[id][TSPS_ORIGIN + 2] + Float:g_ws[id][TSPS_VIEWOFS + 2];
}

// Entity::entity_origin for a player: the middle of its box.
stock world_player_center(id, Float:out[3])
{
	out[0] = (Float:g_ws[id][TSPS_ABSMIN] + Float:g_ws[id][TSPS_ABSMAX]) * 0.5;
	out[1] = (Float:g_ws[id][TSPS_ABSMIN + 1] + Float:g_ws[id][TSPS_ABSMAX + 1]) * 0.5;
	out[2] = (Float:g_ws[id][TSPS_ABSMIN + 2] + Float:g_ws[id][TSPS_ABSMAX + 2]) * 0.5;
}

stock Float:world_health(id)
{
	return Float:g_ws[id][TSPS_HEALTH];
}

stock Float:world_armor(id)
{
	return Float:g_ws[id][TSPS_ARMOR];
}

// The weapon in hand, the game's id (0 kung fu).
stock world_weapon(id)
{
	return g_ws[id][TSPS_WEAPON];
}

stock world_clip(id)
{
	return g_ws[id][TSPS_CLIP];
}

stock world_ammo(id)
{
	return g_ws[id][TSPS_AMMO];
}

stock world_frags(id)
{
	return g_ws[id][TSPS_FRAGS];
}

stock world_field(id, field)
{
	return g_ws[id][field];
}

stock Float:world_combat(id, timer)
{
	return Float:g_ws[id][TSPS_COMBAT + timer];
}

stock bool:world_is_bot(id)
{
	return (g_ws[id][TSPS_FLAGS] & TSPF_BOT) != 0;
}

// ---- rules -----------------------------------------------------------------------------------

// BotWorld::teamplay: the game's teamplay, or a tm_ map.
stock bool:world_teamplay()
{
	return g_game_teamplay || g_non_ts_teamplay;
}

// BotWorld::model_teams: teams by model key on a tm_ map the game doesn't run as teamplay.
stock bool:world_model_teams()
{
	return !g_game_teamplay && g_non_ts_teamplay;
}

// ts_get_gamestate, once per frame.
stock world_gamestate(field)
{
	if (g_gs_stamp != g_frame_stamp)
	{
		g_gs_stamp = g_frame_stamp;
		if (ts_get_gamestate(g_gs) != 1)
		{
			arrayset(g_gs, 0, sizeof(g_gs));
		}
	}
	return g_gs[field];
}

stock Float:world_cvar_gravity()
{
	return g_pcvar_gravity ? get_pcvar_float(g_pcvar_gravity) : 800.0;
}

// ---- lists (read once per frame on first use) --------------------------------------------------

// BotWorld::map_items: the frame's items (TSITM_SIZE cells each, at index * TSITM_SIZE).
stock world_items()
{
	if (g_items_stamp != g_frame_stamp)
	{
		g_items_stamp = g_frame_stamp;
		new n = ts_get_items(g_items, RC_MAX_ITEMS);
		g_items_count = n < 0 ? 0 : (n > RC_MAX_ITEMS ? RC_MAX_ITEMS : n);
	}
	return g_items_count;
}

stock world_item_field(index, field)
{
	return g_items[index * TSITM_SIZE + field];
}

stock world_item_origin(index, Float:out[3])
{
	new base = index * TSITM_SIZE + TSITM_ORIGIN;
	out[0] = Float:g_items[base];
	out[1] = Float:g_items[base + 1];
	out[2] = Float:g_items[base + 2];
}

// BotWorld::inventory: how many weapons id owns (TSINV_SIZE cells each, world_inventory_field);
// kung fu isn't one. -1 when the read failed (tsx's status: the Rust None), which isn't "no
// weapons": callers skip what they'd do with the list (botweapons_sync_inventory).
stock world_inventory(id)
{
	if (g_inv_stamp[id] != g_frame_stamp)
	{
		g_inv_stamp[id] = g_frame_stamp;
		new n = ts_get_inventory_ex(id, g_inv[id], RC_MAX_INVENTORY);
		g_inv_count[id] = n < 0 ? -1 : (n > RC_MAX_INVENTORY ? RC_MAX_INVENTORY : n);
	}
	return g_inv_count[id];
}

stock world_inventory_field(id, index, field)
{
	return g_inv[id][index * TSINV_SIZE + field];
}

// False when the inventory couldn't be read (n is -1: the loop doesn't run).
stock bool:world_owns(id, game_weapon)
{
	new n = world_inventory(id);
	for (new k = 0; k < n; k++)
	{
		if (g_inv[id][k * TSINV_SIZE + TSINV_WEAPON] == game_weapon)
		{
			return true;
		}
	}
	return false;
}

// ---- entities --------------------------------------------------------------------------------

// Entity::entity_origin (BotWorld::entity_glance), cached for the frame: monsters at half eye
// height, everything else the middle of its box. False for a free edict.
stock bool:world_entity_origin(ent, Float:out[3])
{
	if (ent <= 0 || ent >= RC_MAX_EDICTS)
	{
		return false;
	}
	if (ent <= MAX_PLAYERS)
	{
		if (g_ws[ent][TSPS_USERID] == 0)
		{
			return false;
		}
		world_player_center(ent, out);
		return true;
	}
	if (g_ent_stamp[ent] != g_frame_stamp)
	{
		if (!pev_valid(ent))
		{
			return false;
		}
		g_ent_stamp[ent] = g_frame_stamp;
		new Float:a[3], Float:b[3];
		if (pev(ent, pev_flags) & FL_MONSTER)
		{
			pev(ent, pev_origin, a);
			pev(ent, pev_view_ofs, b);
			vec_mad(a, b, 0.5, g_ent_origin[ent]);
		}
		else
		{
			pev(ent, pev_absmin, a);
			pev(ent, pev_size, b);
			vec_mad(a, b, 0.5, g_ent_origin[ent]);
		}
		g_ent_effects[ent] = pev(ent, pev_effects);
	}
	vec_copy(g_ent_origin[ent], out);
	return true;
}

// ---- engine queries ------------------------------------------------------------------------------

// nav World::trace_line (UTIL_TraceLine): the fraction; end_pos and hit (entity index, -1 none,
// 0 the world) out.
stock Float:world_trace_line(const Float:start[3], const Float:end[3], bool:ignore_monsters, bool:ignore_glass, ignore, Float:end_pos[3], &hit)
{
	new flags = (ignore_monsters ? IGNORE_MONSTERS : DONT_IGNORE_MONSTERS) | (ignore_glass ? IGNORE_GLASS : 0);
	engfunc(EngFunc_TraceLine, start, end, flags, ignore, g_tr);
	new Float:fraction;
	get_tr2(g_tr, TR_flFraction, fraction);
	get_tr2(g_tr, TR_vecEndPos, end_pos);
	hit = get_tr2(g_tr, TR_pHit);
	return fraction;
}

// BotWorld::trace_point_hull (pfnTraceHull, point hull 0).
stock Float:world_trace_hull(const Float:start[3], const Float:end[3], bool:ignore_monsters, hull, ignore, Float:end_pos[3], &hit)
{
	engfunc(EngFunc_TraceHull, start, end, ignore_monsters ? IGNORE_MONSTERS : DONT_IGNORE_MONSTERS, hull, ignore, g_tr);
	new Float:fraction;
	get_tr2(g_tr, TR_flFraction, fraction);
	get_tr2(g_tr, TR_vecEndPos, end_pos);
	hit = get_tr2(g_tr, TR_pHit);
	return fraction;
}

// nav World::point_contents (CONTENTS_*).
stock world_point_contents(const Float:at[3])
{
	return engfunc(EngFunc_PointContents, at);
}

// BotWorld::in_pvs: ENGINE_CHECK_VISIBILITY(viewer, ENGINE_SET_PVS(origin)), host api.rs in_pvs.
// rcbot_nav's nav_in_pvs does it: AMX Mod X 1.10's fakemeta has no EngFunc_SetFatPVS.
stock bool:world_in_pvs(viewer, const Float:origin[3])
{
	return nav_in_pvs(viewer, origin);
}

// BotWorld::entities_in_sphere (UTIL_FindEntityInSphere, every match): up to max into out.
stock world_entities_in_sphere(const Float:origin[3], Float:radius, out[], max)
{
	new n = 0, ent = 0;
	while (n < max && (ent = find_ent_in_sphere(ent, origin, radius)) > 0)
	{
		out[n++] = ent;
	}
	return n;
}
