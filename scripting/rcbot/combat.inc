// rcbot/combat.inc: seeing, choosing and fighting enemies.
// Owner: combat. Ports bot/combat.rs (Visibles, Bot::is_visible, set_visible, fvisible, fvisible_point, is_enemy, breakable_is_enemy, can_avoid, update_visibles, is_inanimate, work_enemy_costs, clear_enemy_costs, find_enemy, enemy_found, enemy_down, decide_jump_duck_strafe, want_to_follow_enemy, run_for_cover, flee_grenades, remember_lost_enemy, update_conditions, best_weapon, box_in_sphere), mod.rs Bot::hurt_by / hurt, think.rs spawn_init's combat part.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported, but enemy_found's bot camera hand-off waits for botcam.inc (TODO(botcam)).
// flee_grenades uses think.inc's think_is_grenade_rocket and think_trace_toss (think.inc comes
// after this file; Pawn functions may be called before their definition).
//
// Entities: players are read from the frame's snapshots (world.inc), everything else with pev,
// only for the entities a test needs (the vision pass reads the cached origin and effects first,
// as upstream's cheap tests come first). An EntRef's serial is the player's user id, else 0.

#if defined _rcbot_combat_included
	#endinput
#endif
#define _rcbot_combat_included

// combat.rs REMEMBER_1ST_SEE_ENEMY (BOT_REMEMBER_1ST_SEE_ENEMY); REMEMBER_LOST_ENEMY is consts.inc's.
#define CB_REMEMBER_1ST_SEE_ENEMY	(1<<3)
// combat.rs SF_BREAK_TRIGGER_ONLY.
#define CB_SF_BREAK_TRIGGER_ONLY	1
// Entities a 400-unit sphere is searched for (flee_grenades).
#define CB_MAX_NEAR					64

// ---- entities (Entity fields the combat code reads) --------------------------------------------

// BotWorld::entity(i).is_some(): a player in its slot, or a valid edict.
stock bool:combat_ent_exists(ent)
{
	if (ent <= 0 || ent >= RC_MAX_EDICTS)
	{
		return false;
	}
	if (ent <= MAX_PLAYERS)
	{
		return world_userid(ent) != 0;
	}
	return pev_valid(ent) != 0;
}

// Entity::serial: a player's user id, else 0 (docs/design.md §2).
stock combat_ent_serial(ent)
{
	return ent >= 1 && ent <= MAX_PLAYERS ? world_userid(ent) : 0;
}

// Entity::is_client (FL_CLIENT): a player in its slot.
stock bool:combat_ent_is_client(ent)
{
	return ent >= 1 && ent <= g_maxclients && world_userid(ent) != 0;
}

// Entity::is_alive: deadflag DEAD_NO and health above 0.
stock bool:combat_ent_is_alive(ent)
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		return world_userid(ent) != 0 && world_health(ent) > 0.0 && pev(ent, pev_deadflag) == DEAD_NO;
	}
	new Float:h;
	pev(ent, pev_health, h);
	return pev(ent, pev_deadflag) == DEAD_NO && h > 0.0;
}

stock combat_ent_origin(ent, Float:out[3])
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		world_origin(ent, out);
		return;
	}
	pev(ent, pev_origin, out);
}

stock combat_ent_view_ofs(ent, Float:out[3])
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		out[0] = Float:g_ws[ent][TSPS_VIEWOFS];
		out[1] = Float:g_ws[ent][TSPS_VIEWOFS + 1];
		out[2] = Float:g_ws[ent][TSPS_VIEWOFS + 2];
		return;
	}
	pev(ent, pev_view_ofs, out);
}

stock combat_ent_velocity(ent, Float:out[3])
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		world_velocity(ent, out);
		return;
	}
	pev(ent, pev_velocity, out);
}

stock combat_ent_vangle(ent, Float:out[3])
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		world_vangle(ent, out);
		return;
	}
	pev(ent, pev_v_angle, out);
}

// (absmin + absmax) / 2.
stock combat_ent_box_center(ent, Float:out[3])
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		world_player_center(ent, out);
		return;
	}
	new Float:a[3], Float:b[3];
	pev(ent, pev_absmin, a);
	pev(ent, pev_absmax, b);
	for (new k = 0; k < 3; k++) out[k] = (a[k] + b[k]) * 0.5;
}

stock combat_ent_absbox(ent, Float:mins[3], Float:maxs[3])
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		for (new k = 0; k < 3; k++)
		{
			mins[k] = Float:g_ws[ent][TSPS_ABSMIN + k];
			maxs[k] = Float:g_ws[ent][TSPS_ABSMAX + k];
		}
		return;
	}
	pev(ent, pev_absmin, mins);
	pev(ent, pev_absmax, maxs);
}

stock combat_ent_size(ent, Float:out[3])
{
	pev(ent, pev_size, out);
}

stock Float:combat_ent_health(ent)
{
	if (ent >= 1 && ent <= MAX_PLAYERS)
	{
		return world_health(ent);
	}
	new Float:h;
	pev(ent, pev_health, h);
	return h;
}

stock Float:combat_ent_max_health(ent)
{
	new Float:h;
	pev(ent, pev_max_health, h);
	return h;
}

// The bot's EntRef of ent is its enemy (self.enemy == Some(e.handle())).
stock bool:combat_is_my_enemy(id, ent)
{
	return ent != 0 && g_cb_i[id][CB_ENEMY] == ent && g_cb_i[id][CB_ENEMY_SERIAL] == combat_ent_serial(ent);
}

// ---- life cycle --------------------------------------------------------------------------------

// think.rs Bot::spawn_init, combat's part.
stock combat_spawn_init(id)
{
	g_cb_i[id][CB_LAST_ENEMY] = 0;
	g_cb_i[id][CB_LAST_ENEMY_SERIAL] = 0;
	g_cb_f[id][CB_UPDATE_VISIBLES_TIME] = 0.0;
	g_cb_i[id][CB_ENEMY] = 0;
	g_cb_i[id][CB_ENEMY_SERIAL] = 0;
	g_cb_f[id][CB_FIRST_SEE_ENEMY_TIME] = 0.0;
	g_cb_f[id][CB_GET_AIM_VECTOR_TIME] = 0.0;
	g_cb_i[id][CB_ENEMY_REP_SET] = 0;
	g_cb_i[id][CB_VIS_UPDATE_REVS] = 0;
}

// mod.rs Bot::new's Visibles::default and range_costs (a new bot).
stock combat_bot_init(id)
{
	g_cb_i[id][CB_VIS_COUNT] = 0;
	for (new k = 0; k < RC_VIS_WORDS; k++) g_cb_vis_bits[id][k] = 0;
	for (new x = 0; x < BOT_COST_BUCKETS; x++)
	{
		for (new y = 0; y < BOT_COST_BUCKETS; y++) g_cb_range_costs[id][x][y] = 0.0;
	}
}

// ---- Visibles ------------------------------------------------------------------------------------

// combat.rs Visibles::contains (IsInVisibleList).
stock bool:combat_is_visible(id, ent)
{
	if (ent <= 0 || ent >= RC_MAX_EDICTS)
	{
		return false;
	}
	return (g_cb_vis_bits[id][ent >> 5] & (1 << (ent & 31))) != 0;
}

// combat.rs Visibles::set: adds ent at the end (AddVisible) or removes it (RemoveVisible). A
// full list keeps what it has (the newest is dropped).
stock combat_set_visible(id, ent, bool:on)
{
	if (ent <= 0 || ent >= RC_MAX_EDICTS || combat_is_visible(id, ent) == on)
	{
		return;
	}
	new n = g_cb_i[id][CB_VIS_COUNT];
	if (on)
	{
		if (n >= RC_MAX_VISIBLES)
		{
			rc_debug(DBG_SEE, "bot %d: visible list full, %d not added", id, ent);
			return;
		}
		g_cb_vis_order[id][n] = ent;
		g_cb_i[id][CB_VIS_COUNT] = n + 1;
	}
	else
	{
		for (new k = 0; k < n; k++)
		{
			if (g_cb_vis_order[id][k] == ent)
			{
				for (new j = k + 1; j < n; j++) g_cb_vis_order[id][j - 1] = g_cb_vis_order[id][j];
				g_cb_i[id][CB_VIS_COUNT] = n - 1;
				break;
			}
		}
	}
	g_cb_vis_bits[id][ent >> 5] ^= (1 << (ent & 31));
}

// ---- seeing --------------------------------------------------------------------------------------

// combat.rs Bot::fvisible (CBot::FVisible(pEntity)): a clear line to its middle or its eyes;
// keeps the enemy's see-body / see-head conditions.
stock bool:combat_fvisible(id, ent)
{
	new bool:is_enemy = combat_is_my_enemy(id, ent);
	if (is_enemy)
	{
		g_th_i[id][TH_CONDITIONS] |= COND_SEE_ENEMY_BODY;
		g_th_i[id][TH_CONDITIONS] &= ~COND_SEE_ENEMY_HEAD;
	}
	new Float:gun[3], Float:to[3], Float:end[3], hit;
	world_eyes(id, gun);
	if (!world_entity_origin(ent, to))
	{
		return false;
	}
	if (world_trace_line(gun, to, false, false, id, end, hit) >= 1.0 || hit == ent)
	{
		return true;
	}
	if (is_enemy)
	{
		g_th_i[id][TH_CONDITIONS] &= ~COND_SEE_ENEMY_BODY;
	}
	new Float:o[3], Float:ofs[3];
	combat_ent_origin(ent, o);
	combat_ent_view_ofs(ent, ofs);
	vec_add(o, ofs, to);
	if (world_trace_line(gun, to, false, false, id, end, hit) >= 1.0 || hit == ent)
	{
		if (is_enemy)
		{
			g_th_i[id][TH_CONDITIONS] |= COND_SEE_ENEMY_HEAD;
		}
		return true;
	}
	if (is_enemy)
	{
		g_th_i[id][TH_CONDITIONS] &= ~(COND_SEE_ENEMY_BODY | COND_SEE_ENEMY_HEAD | COND_SEE_ENEMY);
	}
	return false;
}

// combat.rs Bot::fvisible_point (CBot::FVisible(vecOrigin)): a clear line from the eyes
// (monsters ignored).
stock bool:combat_fvisible_point(id, const Float:v[3])
{
	new Float:eyes[3], Float:end[3], hit;
	world_eyes(id, eyes);
	return world_trace_line(eyes, v, true, false, id, end, hit) >= 1.0;
}

// combat.rs box_in_sphere (pfnFindEntityInSphere's test): the entity's box comes nearer origin
// than radius.
stock bool:combat_box_in_sphere(ent, const Float:origin[3], Float:radius)
{
	new Float:lo[3], Float:hi[3];
	combat_ent_absbox(ent, lo, hi);
	new Float:dist_sq = 0.0;
	for (new k = 0; k < 3; k++)
	{
		new Float:d = 0.0;
		if (origin[k] < lo[k])
		{
			d = origin[k] - lo[k];
		}
		else if (origin[k] > hi[k])
		{
			d = origin[k] - hi[k];
		}
		dist_sq += d * d;
	}
	return dist_sq < radius * radius;
}

// combat.rs Bot::breakable_is_enemy (BotFunc_BreakableIsEnemy).
stock bool:combat_breakable_is_enemy(id, b)
{
	if ((pev(b, pev_flags) & FL_WORLDBRUSH) || (pev(b, pev_spawnflags) & CB_SF_BREAK_TRIGGER_ONLY))
	{
		return false;
	}
	new Float:me[3], Float:size[3], target[2];
	combat_ent_size(id, me);
	combat_ent_size(b, size);
	pev(b, pev_target, target, charsmax(target));
	if (target[0] != 0 || size[0] >= me[0] || size[1] >= me[1] || size[2] >= me[2] / 2.0)
	{
		return combat_ent_health(b) < 1000.0 && !(pev(b, pev_effects) & RC_EF_NODRAW);
	}
	return false;
}

// combat.rs Bot::is_enemy (CBot::IsEnemy, TS): a living player of another team (any player
// outside teamplay), or a breakable the bot or a visible enemy near it stands by. Teams are the
// port's rules teams (the-specialistsmod-rs#104); on a tm_ map the game doesn't run as teamplay,
// the players' models (upstream's model keys) compare by model index, which is the same test
// without reading key strings every frame.
stock bool:combat_is_enemy(id, ent)
{
	if (ent <= 0 || ent >= RC_MAX_EDICTS || ent == id || !combat_ent_exists(ent))
	{
		return false;
	}
	new Float:takedamage;
	pev(ent, pev_takedamage, takedamage);
	if (takedamage == 0.0 || (pev(ent, pev_effects) & RC_EF_NODRAW) || !combat_ent_is_alive(ent))
	{
		return false;
	}
	if (combat_ent_is_client(ent))
	{
		if (!world_alive(ent))
		{
			return false;
		}
		if (world_teamplay())
		{
			if (world_model_teams())
			{
				return pev(id, pev_modelindex) != pev(ent, pev_modelindex);
			}
			return world_team(ent) != world_team(id);
		}
		return true;
	}
	if (ent <= MAX_PLAYERS)
	{
		return false;
	}
	new classname[16];
	pev(ent, pev_classname, classname, charsmax(classname));
	if (!equal(classname, "func_breakable"))
	{
		return false;
	}
	new Float:origin[3];
	world_entity_origin(ent, origin);
	// Upstream's UTIL_FindEntityInSphere loop looks for the first client: clients are the first
	// entities, so the clients alone, in order, give the same one.
	new player = 0;
	for (new i = 1; i <= g_maxclients; i++)
	{
		if (!combat_ent_is_client(i) || !combat_box_in_sphere(i, origin, 200.0))
		{
			continue;
		}
		if (i == id)
		{
			if (combat_distance_from(id, origin) < 80.0)
			{
				player = i;
				break;
			}
		}
		else
		{
			player = i;
			break;
		}
	}
	new bool:mine = player == id;
	new bool:near_enemy = player != 0 && player != id && combat_is_visible(id, player) && combat_is_enemy(id, player);
	return ((world_field(id, TSPS_STANCE) != TS_STATE_STUNT && mine) || near_enemy) && combat_breakable_is_enemy(id, ent);
}

// mod.rs Bot::distance_from(v, false): from the gun position.
stock Float:combat_distance_from(id, const Float:v[3])
{
	new Float:gun[3];
	world_eyes(id, gun);
	return vec_distance(v, gun);
}

// combat.rs Bot::can_avoid (CBot::CanAvoid, TS).
stock bool:combat_can_avoid(id, ent, Float:dist, Float:avoid_dist)
{
	if (pev(id, pev_groundentity) == ent || ent == id || (combat_ent_effects(ent) & RC_EF_NODRAW))
	{
		return false;
	}
	// Upstream stepped around its enemy unless it held a blade; the fight's own movement keeps
	// the distance now (design §4).
	if (combat_is_my_enemy(id, ent))
	{
		return false;
	}
	if (dist > avoid_dist || pev(id, pev_movetype) == RC_MOVETYPE_FLY || (g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_LADDER))
	{
		return false;
	}
	if (g_tk_count[id] > 0 && g_tk_i[id][0][TK_EDICT] == ent && g_tk_i[id][0][TK_EDICT_SERIAL] == combat_ent_serial(ent))
	{
		return false;
	}
	new solid = pev(ent, pev_solid);
	if (solid == RC_SOLID_NOT || solid == RC_SOLID_TRIGGER)
	{
		return false;
	}
	if (ent > MAX_PLAYERS)
	{
		new classname[16];
		pev(ent, pev_classname, classname, charsmax(classname));
		if (equal(classname, "func_door", 9))
		{
			return false;
		}
	}
	if (pev(ent, pev_flags) & FL_WORLDBRUSH)
	{
		new Float:o[3];
		world_entity_origin(ent, o);
		new cur = g_nv_i[id][NV_CURRENT_WPT];
		if (cur != -1 && nav_distance(cur, o) < 100.0)
		{
			return false;
		}
		return look_dot_from_origin(id, o) > 0.9;
	}
	return true;
}

// Entity::effects (BotWorld::entity_glance's second value).
stock combat_ent_effects(ent)
{
	if (ent > MAX_PLAYERS && ent < RC_MAX_EDICTS && g_ent_stamp[ent] == g_frame_stamp)
	{
		return g_ent_effects[ent];
	}
	return pev(ent, pev_effects);
}

// combat.rs Bot::update_visibles (CBot::UpdateVisibles): up to the profile's vision revs
// entities per call; each one in view, in the PVS and with a clear line counts as visible. True
// when the pass over every entity ended. The first call of a pass also measures the wall ahead
// and starts the cost buckets (clear_enemy_costs, which looks for an enemy).
stock bool:combat_update_visibles(id)
{
	new Float:now = g_now;
	new bool:work_costs = false;
	new Float:gun[3], Float:end[3], hit;
	world_eyes(id, gun);
	if (g_cb_i[id][CB_VIS_UPDATE_REVS] == 0)
	{
		g_cb_i[id][CB_VIS_UPDATE_REVS] = 1;
		// avoid_entity (movement's field) is the vision pass's to set (combat.rs).
		g_mv_i[id][MV_AVOID_ENT] = 0;
		g_mv_i[id][MV_AVOID_SERIAL] = 0;
		g_th_i[id][TH_CONDITIONS] &= ~(COND_SEE_ENEMY | COND_SEE_ENEMY_HEAD);
		new Float:fwd[3], Float:right[3], Float:far[3];
		make_vectors(g_co_v[id][CO_VANGLE], fwd, right);
		vec_mad(gun, fwd, 768.0, far);
		world_trace_line(gun, far, false, false, id, end, hit);
		g_cb_f[id][CB_DISTANCE_FROM_WALL] = vec_distance(end, gun);
		work_costs = true;
		combat_clear_enemy_costs(id);
	}
	new bool:need_to_follow = !tasks_has(id, TASK_FOLLOW_LEADER) && g_sq_i[id][SQ_LEADER] != 0;
	new bool:saw_leader = false;
	new Float:me_size[3], Float:me_origin[3];
	combat_ent_size(id, me_size);
	world_origin(id, me_origin);
	new Float:avoid_dist = vec_length_2d(me_size) * 3.0;
	// Nothing exists above the frame's highest index: the sweep ends there.
	new top = g_highest_edict;
	new revs = g_pr_i[id][PR_VISION_REVS];
	new start = g_cb_i[id][CB_VIS_UPDATE_REVS];
	new max = start + (revs > 1 ? revs : 1);
	if (max > top)
	{
		max = top;
	}

	// Upstream checks the bot against its own PVS, which always passes: any remembered position
	// counts as visible. Kept.
	new slot = remember_nearest(id, g_nv_i[id][NV_GOAL_ORIGIN_SET] ? g_nv_v[id][NV_GOAL_ORIGIN] : VEC_ZERO, me_origin);
	if (slot >= 0)
	{
		vec_copy(g_rm_origin[id][slot], g_cb_v[id][CB_NEAREST_REMEMBER_POINT]);
		g_cb_i[id][CB_NEAREST_REMEMBER_VISIBLE] = 1;
	}
	else
	{
		g_cb_i[id][CB_NEAREST_REMEMBER_VISIBLE] = 0;
	}

	new Float:origin[3];
	new leader = g_sq_i[id][SQ_LEADER], leader_serial = g_sq_i[id][SQ_LEADER_SERIAL];
	for (new i = start; i <= max; i++)
	{
		// Upstream's tests in its order, the cheap ones on a glance at the entity.
		if (!world_entity_origin(i, origin) || (combat_ent_effects(i) & RC_EF_NODRAW))
		{
			combat_set_visible(id, i, false);
			continue;
		}
		new Float:dist = vec_distance(origin, gun);
		if (dist > BOT_VIEW_DISTANCE || !look_in_view_cone(id, origin))
		{
			combat_set_visible(id, i, false);
			continue;
		}
		// combat.rs:256: the entity's origin in the bot's PVS, before the trace.
		if (!world_in_pvs(id, origin) || !combat_fvisible(id, i))
		{
			combat_set_visible(id, i, false);
			continue;
		}
		if (combat_is_my_enemy(id, i))
		{
			g_th_i[id][TH_CONDITIONS] |= COND_SEE_ENEMY;
		}
		if ((!g_mv_i[id][MV_AVOID_ENT] || dist < g_cb_f[id][CB_NEAREST_AVOID_DIST]) && !(g_nv_i[id][NV_CURRENT_WPT_FLAGS] & NAV_FL_JUMP)
			&& combat_can_avoid(id, i, dist, avoid_dist) && g_mv_f[id][MV_AVOID_TIME] < now)
		{
			g_cb_f[id][CB_NEAREST_AVOID_DIST] = dist;
			g_mv_i[id][MV_AVOID_ENT] = i;
			g_mv_i[id][MV_AVOID_SERIAL] = combat_ent_serial(i);
		}
		if (!saw_leader && leader == i && leader_serial == combat_ent_serial(i))
		{
			saw_leader = true;
		}
		combat_set_visible(id, i, true);
		if (work_costs)
		{
			combat_work_enemy_costs(id, i, dist);
		}
	}
	new bool:finished;
	if (max >= top)
	{
		g_cb_i[id][CB_VIS_UPDATE_REVS] = 0;
		// Entities seen above the top are gone (their slots freed since).
		for (new k = g_cb_i[id][CB_VIS_COUNT] - 1; k >= 0; k--)
		{
			new e = g_cb_vis_order[id][k];
			if (e > top)
			{
				combat_set_visible(id, e, false);
			}
		}
		finished = true;
	}
	else
	{
		g_cb_i[id][CB_VIS_UPDATE_REVS] = max + 1;
		finished = false;
	}
	squad_see_leader_again(id, saw_leader, need_to_follow);
	return finished;
}

// combat.rs Bot::is_inanimate (CBot::isInAnimate).
stock bool:combat_is_inanimate(ent)
{
	return (pev(ent, pev_flags) & (FL_WORLDBRUSH | FL_DORMANT | FL_KILLME)) != 0;
}

// combat.rs Bot::work_enemy_costs (CBot::workEnemyCosts, design §4): adds the entity's danger (a
// survival gene by kind, over distance squared, scaled by size) to every cost bucket around the
// bot, measured from each bucket, and remembers the cheapest (nearest the bot on ties).
stock combat_work_enemy_costs(id, ent, Float:dist)
{
	if (g_cb_f[id][CB_NEXT_WORK_RANGE_COSTS] > g_now || dist > 768.0)
	{
		return;
	}
	new state_kind;
	if (combat_is_inanimate(ent))
	{
		state_kind = 2;
	}
	else if (combat_is_enemy(id, ent))
	{
		state_kind = 1;
	}
	else
	{
		new classname[32];
		pev(ent, pev_classname, classname, charsmax(classname));
		if (contain(classname, "grenade") != -1)
		{
			state_kind = 3;
		}
		else
		{
			state_kind = combat_ent_is_alive(ent) ? 0 : 2;
		}
	}
	new Float:es[3], Float:ms[3], Float:me[3], Float:eo[3];
	combat_ent_size(ent, es);
	combat_ent_size(id, ms);
	world_origin(id, me);
	combat_ent_origin(ent, eo);
	new RcLearn:h = RcLearn:g_ln_i[id][LN_HANDLE];
	new Float:gene = h ? rl_bot_gene(h, RlGenes_Survival, state_kind) : 0.0;
	new Float:weight = floatabs(gene) * (vec_length(es) / vec_length(ms));
	new mid = BOT_COST_BUCKETS / 2;
	new Float:at[3];
	for (new x = 0; x < BOT_COST_BUCKETS; x++)
	{
		for (new y = 0; y < BOT_COST_BUCKETS; y++)
		{
			at[0] = me[0] + float(x - mid) * BOT_COST_RANGE;
			at[1] = me[1] + float(y - mid) * BOT_COST_RANGE;
			at[2] = me[2];
			new Float:d = fmax(vec_distance_2d(at, eo), BOT_COST_RANGE / 4.0);
			g_cb_range_costs[id][x][y] += weight / (d * d);
		}
	}
	// The cheapest after this entity, nearest the bot first on ties (the centre outward).
	new Float:lowest = 3.4028235e38;
	new best_ring = cellmax;
	for (new x = 0; x < BOT_COST_BUCKETS; x++)
	{
		for (new y = 0; y < BOT_COST_BUCKETS; y++)
		{
			new Float:c = g_cb_range_costs[id][x][y];
			new rx = x - mid, ry = y - mid;
			if (rx < 0) rx = -rx;
			if (ry < 0) ry = -ry;
			new ring = rx > ry ? rx : ry;
			if (c < lowest || (c == lowest && ring < best_ring))
			{
				lowest = c;
				best_ring = ring;
				g_cb_v[id][CB_LOWEST_ENEMY_COST_VEC][0] = me[0] + float(x - mid) * BOT_COST_RANGE;
				g_cb_v[id][CB_LOWEST_ENEMY_COST_VEC][1] = me[1] + float(y - mid) * BOT_COST_RANGE;
				g_cb_v[id][CB_LOWEST_ENEMY_COST_VEC][2] = me[2];
			}
		}
	}
	g_cb_f[id][CB_LOWEST_ENEMY_COST] = lowest;
}

// combat.rs Bot::clear_enemy_costs (CBot::clearEnemyCosts), which also looks for an enemy when
// the bot wants one (WantToFindEnemy, think's TH_WANT_TO_FIND_ENEMY).
stock combat_clear_enemy_costs(id)
{
	for (new x = 0; x < BOT_COST_BUCKETS; x++)
	{
		for (new y = 0; y < BOT_COST_BUCKETS; y++) g_cb_range_costs[id][x][y] = 0.0;
	}
	g_cb_f[id][CB_LOWEST_ENEMY_COST] = 99999.0;
	world_origin(id, g_cb_v[id][CB_LOWEST_ENEMY_COST_VEC]);
	new old = g_cb_i[id][CB_ENEMY], old_serial = g_cb_i[id][CB_ENEMY_SERIAL];
	if (g_th_i[id][TH_WANT_TO_FIND_ENEMY])
	{
		combat_find_enemy(id);
		new e = g_cb_i[id][CB_ENEMY];
		if (old != e || old_serial != g_cb_i[id][CB_ENEMY_SERIAL])
		{
			if (old)
			{
				combat_enemy_down(id, old);
			}
			if (entref_alive(e, g_cb_i[id][CB_ENEMY_SERIAL]))
			{
				combat_enemy_found(id, e);
			}
		}
	}
	else
	{
		g_cb_i[id][CB_ENEMY] = 0;
		g_cb_i[id][CB_ENEMY_SERIAL] = 0;
	}
}

// events.rs Bot::rep_of: the bot's reputation of player, if it has one (client.inc's rep id,
// then the profile's reputations). Read here as the enemy's priority.
stock bool:combat_rep_of(id, player, &rep)
{
	new rep_id = g_client_rep_id[player];
	if (rep_id == NO_CLIENT)
	{
		return false;
	}
	for (new k = 0; k < g_rep_count[id]; k++)
	{
		if (g_rep_id[id][k] == rep_id)
		{
			rep = g_rep_value[id][k];
			return true;
		}
	}
	return false;
}

// combat.rs Bot::find_enemy (CBot::FindEnemy, TS, rewritten: design §4): every 0.4-0.6 s the
// visible enemies (and whoever hurt the bot in the last 3 s, seen or not, if the line to it is
// clear) are scored (fire_threat, times the learned threat's square root); the enemy is kept
// unless another scores 25% (and a little) more, or it is out of sight and another is in it. A
// new enemy starts a priority NORMAL_ATTACK schedule. The enemy's index, 0 none.
stock combat_find_enemy(id)
{
	new Float:now = g_now;
	new cur = g_cb_i[id][CB_ENEMY];
	new bool:current = entref_alive(cur, g_cb_i[id][CB_ENEMY_SERIAL]) && combat_is_enemy(id, cur);
	if (current && g_cb_f[id][CB_NEXT_GET_ENEMY_TIME] >= now)
	{
		return cur;
	}
	if (!current)
	{
		g_cb_i[id][CB_ENEMY] = 0;
		g_cb_i[id][CB_ENEMY_SERIAL] = 0;
		cur = 0;
	}
	g_cb_f[id][CB_NEXT_GET_ENEMY_TIME] = now + random_float(0.4, 0.6);
	static candidates[RC_MAX_VISIBLES + 1];
	new n = g_cb_i[id][CB_VIS_COUNT];
	for (new k = 0; k < n; k++) candidates[k] = g_cb_vis_order[id][k];
	new attacker = g_fi_i[id][FI_ATTACKER];
	if (attacker && g_fi_f[id][FI_ATTACKER_TIME] + 3.0 > now && !combat_is_visible(id, attacker))
	{
		candidates[n++] = attacker;
	}
	new best = 0, best_rep = 0, bool:best_rep_set = false, Float:best_score = 0.0, Float:current_score = 0.0;
	for (new k = 0; k < n; k++)
	{
		new e = candidates[k];
		if (!combat_ent_exists(e))
		{
			continue;
		}
		// The visible list can be a vision pass old: check the line now, so a new enemy is
		// never one behind a wall (design §4).
		if (!combat_is_enemy(id, e) || !combat_fvisible(id, e))
		{
			continue;
		}
		new rep = 0, bool:rep_set = false;
		new Float:learned = 1.0;
		if (combat_ent_is_client(e))
		{
			rep_set = combat_rep_of(id, e, rep);
			// What every bot learned of this player (0.25-4x), damped (design §4).
			learned = floatsqroot(learning_learned_threat(id, e));
		}
		new Float:score = fire_threat_rep(id, e, rep, rep_set) * learned;
		if (current && e == cur)
		{
			current_score = score;
		}
		if (!best || score > best_score)
		{
			best = e;
			best_score = score;
			best_rep = rep;
			best_rep_set = rep_set;
		}
	}
	if (current && (!best || best == cur || best_score <= current_score * 1.25 + 0.5))
	{
		return cur;
	}
	if (!best)
	{
		return cur;
	}
	g_cb_i[id][CB_ENEMY] = best;
	g_cb_i[id][CB_ENEMY_SERIAL] = combat_ent_serial(best);
	g_cb_i[id][CB_ENEMY_REP] = best_rep;
	g_cb_i[id][CB_ENEMY_REP_SET] = _:best_rep_set;
	tasks_remove_similar(id, TASK_NORMAL_ATTACK);
	new sched = tasks_new_schedule_id(id);
	tasks_add_priority(id, TASK_NORMAL_ATTACK, sched, best, combat_ent_serial(best), 0, 0.0, VEC_ZERO);
	combat_enemy_found(id, best);
	return best;
}

// combat.rs Bot::enemy_found (CBot::EnemyFound): a new enemy starts the reaction time (0.35 s,
// down to 0.15 s with skill: no shot before it) and the aim's settling (design §4); the same
// enemy found again while still fighting it is no new sight. Upstream's strafe time and TS
// power-up / stunt press are replaced by the fight's own (design §4, §5.51).
stock combat_enemy_found(id, ent)
{
	new Float:now = g_now;
	new serial = combat_ent_serial(ent);
	g_th_i[id][TH_CONDITIONS] |= COND_SEE_ENEMY;
	g_cb_i[id][CB_ENEMY] = ent;
	g_cb_i[id][CB_ENEMY_SERIAL] = serial;
	g_cb_f[id][CB_FIRST_SEE_ENEMY_TIME] = now;
	if (g_cb_i[id][CB_LAST_ENEMY] != ent || g_cb_i[id][CB_LAST_ENEMY_SERIAL] != serial || g_fi_f[id][FI_LAST_SIGHT] + 1.0 < now)
	{
		g_fi_f[id][FI_REACT_AT] = now + 0.35 - 0.2 * fire_skill_share(id);
		g_fi_f[id][FI_ON_TARGET_SINCE] = now;
	}
	squad_enemy_found(id);
	new Float:o[3], Float:me[3];
	world_entity_origin(ent, o);
	world_origin(id, me);
	remember_add(id, o, ent, serial, CB_REMEMBER_1ST_SEE_ENEMY, me);
	// investigate_sound_time (think's field) is set here in combat.rs.
	g_th_f[id][TH_INVESTIGATE_SOUND_TIME] = now + 2.0;
	// TODO(botcam): BotHasEnemy: unless the camera's current bot is this one or another has an
	// enemy, BotCam::set_current_bot(id) (botcam.inc has no stock for it yet).
}

// combat.rs Bot::enemy_down (CBot::EnemyDown): upstream removes the enemy's remembered position,
// called with the enemy already cleared, so nothing.
stock combat_enemy_down(id, ent)
{
	#pragma unused id, ent
}

// combat.rs Bot::decide_jump_duck_strafe (CBot::decideJumpDuckStrafe): the personal genes drive
// three perceptrons (rl_bot_decide) that jump, duck and strafe in melee. The strafe's sigmoid
// 0..1 maps to -1..1 (upstream only ever strafed right, design §4).
stock combat_decide_jump_duck_strafe(id, Float:enemy_dist, const Float:enemy_origin[3])
{
	new Float:now = g_now;
	new RcLearn:h = RcLearn:g_ln_i[id][LN_HANDLE];
	if (h)
	{
		new Float:size[3], Float:vel[3], Float:me[3];
		combat_ent_size(id, size);
		world_velocity(id, vel);
		world_origin(id, me);
		new Float:my_size = vec_length(size);
		new Float:inputs[5];
		inputs[0] = vec_length(vel) / my_size;
		inputs[1] = enemy_dist * 0.01;
		inputs[2] = (enemy_origin[2] - me[2]) * 0.1;
		inputs[3] = 1.0;
		new enemy = g_cb_i[id][CB_ENEMY];
		if (entref_alive(enemy, g_cb_i[id][CB_ENEMY_SERIAL]))
		{
			new Float:es[3];
			combat_ent_size(enemy, es);
			inputs[3] = my_size / vec_length(es);
		}
		inputs[4] = (world_flags(id) & TSPF_ON_GROUND) && pev(id, pev_movetype) == RC_MOVETYPE_WALK ? 1.0 : 0.0;
		new Float:out;
		new bool:jump = rl_bot_decide(h, RlDecide_Jump, inputs, 5, out);
		new bool:duck = rl_bot_decide(h, RlDecide_Duck, inputs, 5, out);
		rl_bot_decide(h, RlDecide_Strafe, inputs, 5, out);
		new Float:strafe = 2.0 * out - 1.0;
		if (jump)
		{
			movement_jump(id);
		}
		if (duck)
		{
			movement_jump_and_duck(id);
		}
		if (g_mv_f[id][MV_STRAFE_TIME] + 0.75 < now && floatabs(strafe) >= 0.5)
		{
			g_mv_f[id][MV_STRAFE_SPEED] = strafe * g_mv_f[id][MV_MAX_SPEED];
		}
	}
	// used_melee (think's field) is set here in combat.rs.
	g_th_i[id][TH_USED_MELEE] = 1;
}

// combat.rs Bot::want_to_follow_enemy (CBot::WantToFollowEnemy): upstream runs the follow
// perceptron but ignores it; the bot follows while above 30% health.
stock bool:combat_want_to_follow_enemy(id)
{
	return world_health(id) > combat_ent_max_health(id) * 0.3;
}

// combat.rs Bot::run_for_cover (CBot::RunForCover): crouch, then go to a waypoint hidden from
// origin where the team doesn't keep dying (the heatmap, design §4), or near the cheapest cost
// bucket, reloading on the way.
stock combat_run_for_cover(id, const Float:origin[3], bool:now_flag)
{
	new bool:covering = tasks_has_schedule(id, SCHED_RUN_FOR_COVER);
	if (!(now_flag || !covering))
	{
		return;
	}
	if (covering)
	{
		tasks_finish_schedule_desc(id, SCHED_RUN_FOR_COVER);
	}
	new sched = tasks_new_schedule_id(id);
	new Float:me[3];
	world_origin(id, me);
	static failed[64];
	new num_failed = 0;
	if (g_nv_i[id][NV_PATH])
	{
		num_failed = nav_path_failed_goals(RcPath:g_nv_i[id][NV_PATH], failed, sizeof(failed));
		if (num_failed > sizeof(failed))
		{
			num_failed = sizeof(failed);
		}
	}
	new cover = num_failed > 0 ? nav_cover(me, origin, failed, num_failed, g_th_i[id][TH_TEAM], true)
		: nav_cover(me, origin, {-1}, 0, g_th_i[id][TH_TEAM], true);
	tasks_add_priority(id, TASK_CROUCH, sched, 0, 0, 0, random_float(1.0, 2.0), VEC_ZERO);
	if (cover == -1)
	{
		// Bot::nearest_waypoint(lowest_enemy_cost_vec, 320, -1, visible, ...).
		cover = nav_nearest(g_cb_v[id][CB_LOWEST_ENEMY_COST_VEC], 320.0, -1, NAV_NEAREST_VISIBLE);
		if (cover == -1)
		{
			return;
		}
	}
	if (g_nv_i[id][NV_GOAL_WPT] != cover)
	{
		tasks_add_priority(id, TASK_FIND_PATH, sched, 0, 0, cover, -2.0, VEC_ZERO);
	}
	new w = g_bw_i[id][BW_CURRENT];
	if (w >= 0 && w < RC_MAX_WEAPONS && botweapons_need_to_reload(id, w))
	{
		tasks_add_priority(id, TASK_RELOAD, sched, 0, 0, 1, 0.0, VEC_ZERO);
	}
	tasks_give_schedule_description(id, sched, SCHED_RUN_FOR_COVER);
	tasks_set_time_to_complete_schedule(id, sched, random_float(5.0, 10.0));
}

// combat.rs Bot::flee_grenades (design §4; upstream fled only its avoid entity, §5.65): runs
// from a grenade that will land within its 300-unit blast (and a bit) of the bot: any grenade
// within 400 units, in view or not, its landing flown out. An existing cover schedule is kept.
stock combat_flee_grenades(id)
{
	static near[CB_MAX_NEAR];
	new Float:me[3], Float:land[3];
	world_origin(id, me);
	new n = world_entities_in_sphere(me, 400.0, near, CB_MAX_NEAR);
	for (new k = 0; k < n; k++)
	{
		new e = near[k];
		if (!combat_ent_exists(e) || !think_is_grenade_rocket(e))
		{
			continue;
		}
		think_trace_toss(e, land);
		if (vec_distance(land, me) < 330.0)
		{
			combat_run_for_cover(id, land, false);
			return;
		}
	}
}

// combat.rs Bot::remember_lost_enemy (RememberPosition(v, enemy, BOT_REMEMBER_LOST_ENEMY)).
stock combat_remember_lost_enemy(id, const Float:v[3], ent)
{
	new Float:me[3];
	world_origin(id, me);
	remember_add(id, v, ent, ent ? combat_ent_serial(ent) : 0, REMEMBER_LOST_ENEMY, me);
}

// combat.rs Bot::update_conditions (CBot::UpdateConditions). The conditions and the update time
// are think's fields, which combat.rs keeps.
stock combat_update_conditions(id)
{
	new Float:now = g_now;
	squad_update_conditions(id);
	if (g_th_f[id][TH_LAST_UPDATE_CONDITIONS] > now)
	{
		return;
	}
	g_th_i[id][TH_CONDITIONS] &= ~(COND_HAS_WEAPON | COND_ENEMY_OCCLUDED | COND_TASK_EDICT_NA);
	new bool:need;
	if (combat_best_weapon(id, 0, need) > 0)
	{
		g_th_i[id][TH_CONDITIONS] |= COND_HAS_WEAPON;
	}
	new e = g_cb_i[id][CB_ENEMY];
	if (e)
	{
		if (!(g_th_i[id][TH_CONDITIONS] & COND_SEE_ENEMY))
		{
			g_th_i[id][TH_CONDITIONS] |= COND_ENEMY_OCCLUDED;
		}
		else if (!entref_alive(e, g_cb_i[id][CB_ENEMY_SERIAL]) || !combat_ent_is_alive(e))
		{
			g_th_i[id][TH_CONDITIONS] |= COND_ENEMY_DEAD;
		}
	}
	if (g_tk_count[id] > 0)
	{
		new t = g_tk_i[id][0][TK_EDICT], ts = g_tk_i[id][0][TK_EDICT_SERIAL];
		if (t && !entref_alive(t, ts))
		{
			g_th_i[id][TH_CONDITIONS] |= COND_TASK_EDICT_NA;
			tasks_forget_edict(id, t, ts);
		}
	}
	g_th_f[id][TH_LAST_UPDATE_CONDITIONS] = now + g_th_f[id][TH_REACTION_TIME];
}

// combat.rs Bot::best_weapon (m_Weapons.GetBestWeaponId(this, pEnemy)): the RCBot id for the
// enemy (0 none); need_weapons is BOT_CONDITION_NEED_WEAPONS.
stock combat_best_weapon(id, enemy, &bool:need_weapons)
{
	new Float:dist = -1.0;
	new bool:too_high = false;
	if (enemy)
	{
		new Float:o[3], Float:me[3];
		world_entity_origin(enemy, o);
		world_origin(id, me);
		dist = combat_distance_from(id, o);
		too_high = o[2] > me[2] + RC_MAX_JUMP_HEIGHT;
	}
	return botweapons_best_weapon_id(id, dist, too_high, pev(id, pev_waterlevel) == 3, need_weapons);
}

// ---- hurt ------------------------------------------------------------------------------------------

// mod.rs Bot::hurt_by (BotEvent HURT for the port's damage report): the bot turns to where the
// shot came from, along the shot's direction (it doesn't learn where the shooter stands), else
// toward the attacker (damage without a trace: a grenade, a blow).
stock combat_hurt_by(id, attacker, const Float:dir[3])
{
	new Float:from[3];
	if (!vec_is_zero(dir))
	{
		new Float:gun[3], Float:n[3];
		world_eyes(id, gun);
		vec_normalize(dir, n);
		vec_mad(gun, n, -512.0, from);
	}
	else if (combat_ent_exists(attacker))
	{
		combat_ent_origin(attacker, from);
	}
	else
	{
		return;
	}
	combat_hurt(id, attacker, from);
}

// mod.rs Bot::hurt (BotEvent(BOT_EVENT_HURT, attacker, origin), design §4): hurt by someone other
// than its enemy, the bot weighs its enemy again at once (the attacker scoring higher, in sight
// or not); without an enemy it faces where the hurt came from. attacker 0 is none.
stock combat_hurt(id, attacker, const Float:from[3])
{
	new Float:now = g_now;
	vec_copy(from, g_lk_v[id][LK_HURT_ORIGIN]);
	g_lk_f[id][LK_HURT_TIME] = now;
	if (attacker > 0)
	{
		g_fi_i[id][FI_ATTACKER] = attacker;
		g_fi_f[id][FI_ATTACKER_TIME] = now;
		new e = g_cb_i[id][CB_ENEMY];
		if (e && e != attacker)
		{
			g_cb_f[id][CB_NEXT_GET_ENEMY_TIME] = 0.0;
		}
	}
	new look = g_lk_i[id][LK_LOOK_TASK];
	if (!g_cb_i[id][CB_ENEMY] && (look == LOOK_NEXT_WAYPOINT || look == LOOK_LOOK_AROUND || look == LOOK_NONE))
	{
		g_lk_i[id][LK_LOOK_TASK] = LOOK_FACE_HURT_ORIGIN;
	}
}
