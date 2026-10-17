// rcbot/combat.inc: seeing, choosing and fighting enemies.
// Owner: combat. Ports bot/combat.rs (Visibles, Bot::is_visible, set_visible, fvisible, fvisible_point, is_enemy, breakable_is_enemy, can_avoid, update_visibles, is_inanimate, work_enemy_costs, clear_enemy_costs, find_enemy, enemy_found, enemy_down, decide_jump_duck_strafe, want_to_follow_enemy, run_for_cover, flee_grenades, remember_lost_enemy, update_conditions, best_weapon, box_in_sphere), mod.rs Bot::hurt_by / hurt, think.rs spawn_init's combat part.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: is_visible / is_enemy have placeholder bodies (line of sight, rules teams) that manager_death uses; the rest is TODO(combat).

#if defined _rcbot_combat_included
	#endinput
#endif
#define _rcbot_combat_included

// think.rs Bot::spawn_init, combat's part.
stock combat_spawn_init(id)
{
	g_cb_i[id][CB_LAST_ENEMY] = 0;
	g_cb_f[id][CB_UPDATE_VISIBLES_TIME] = 0.0;
	g_cb_i[id][CB_ENEMY] = 0;
	g_cb_f[id][CB_FIRST_SEE_ENEMY_TIME] = 0.0;
	g_cb_f[id][CB_GET_AIM_VECTOR_TIME] = 0.0;
	g_cb_i[id][CB_ENEMY_REP_SET] = 0;
	g_cb_i[id][CB_VIS_UPDATE_REVS] = 0;
}

// combat.rs Bot::is_visible (IsInVisibleList).
// TODO(combat): the Visibles bits (g_cb_vis_bits) once update_visibles is ported; until then a
// line of sight from the eyes.
stock bool:combat_is_visible(id, ent)
{
	new Float:o[3];
	if (!world_entity_origin(ent, o))
	{
		return false;
	}
	return combat_fvisible_point(id, o);
}

// combat.rs Bot::fvisible_point: a clear line from the eyes (monsters ignored).
stock bool:combat_fvisible_point(id, const Float:v[3])
{
	new Float:eyes[3], Float:end[3], hit;
	world_eyes(id, eyes);
	return world_trace_line(eyes, v, true, false, id, end, hit) >= 1.0;
}

// combat.rs Bot::is_enemy (players): in teamplay another rules team, else everyone.
// TODO(combat): model teams on tm_ maps, breakables, the rest of is_enemy.
stock bool:combat_is_enemy(id, ent)
{
	if (ent < 1 || ent > MAX_PLAYERS || ent == id)
	{
		return false;
	}
	if (world_teamplay())
	{
		new t = world_team(id);
		return t == 0 || world_team(ent) != t;
	}
	return true;
}

// TODO(combat): combat.rs / mod.rs Bot::new's Visibles and range costs (a new bot).
stock combat_bot_init(id)
{
	g_cb_i[id][CB_VIS_COUNT] = 0;
}

// TODO(combat): mod.rs Bot::hurt_by (BotEvent HURT for a damage report): turn to where the shot came from.
stock combat_hurt_by(id, attacker, const Float:dir[3])
{
	#pragma unused id, attacker, dir
}

// TODO(combat): mod.rs Bot::hurt (hurt_origin, fight.attacker, next_get_enemy_time, FaceHurtOrigin).
stock combat_hurt(id, attacker, const Float:from[3])
{
	#pragma unused id, attacker, from
}

// TODO(combat): combat.rs Bot::fvisible (FVisible: the entity's origin, head and body).
stock bool:combat_fvisible(id, ent)
{
	#pragma unused id, ent
	return false;
}

// TODO(combat): combat.rs Bot::update_visibles (the vision pass, vis_update_revs per call).
stock bool:combat_update_visibles(id)
{
	#pragma unused id
	return false;
}

// TODO(combat): combat.rs Bot::work_enemy_costs.
stock combat_work_enemy_costs(id, ent, Float:dist)
{
	#pragma unused id, ent, dist
}

// TODO(combat): combat.rs Bot::find_enemy (FindEnemy): the enemy's index, 0 none.
stock combat_find_enemy(id)
{
	#pragma unused id
	return 0;
}

// TODO(combat): combat.rs Bot::enemy_found.
stock combat_enemy_found(id, ent)
{
	#pragma unused id, ent
}

// TODO(combat): combat.rs Bot::enemy_down.
stock combat_enemy_down(id, ent)
{
	#pragma unused id, ent
}

// TODO(combat): combat.rs Bot::decide_jump_duck_strafe (the decision perceptrons, rl_bot_decide).
stock combat_decide_jump_duck_strafe(id, Float:enemy_dist, const Float:enemy_origin[3])
{
	#pragma unused id, enemy_dist, enemy_origin
}

// TODO(combat): combat.rs Bot::want_to_follow_enemy.
stock bool:combat_want_to_follow_enemy(id)
{
	#pragma unused id
	return false;
}

// TODO(combat): combat.rs Bot::run_for_cover.
stock combat_run_for_cover(id, const Float:origin[3], bool:now_flag)
{
	#pragma unused id, origin, now_flag
}

// TODO(combat): combat.rs Bot::flee_grenades (design §4: any within 400 units).
stock combat_flee_grenades(id)
{
	#pragma unused id
}

// TODO(combat): combat.rs Bot::remember_lost_enemy.
stock combat_remember_lost_enemy(id, const Float:v[3], ent)
{
	#pragma unused id, v, ent
}

// TODO(combat): combat.rs Bot::update_conditions.
stock combat_update_conditions(id)
{
	#pragma unused id
}

// TODO(combat): combat.rs Bot::best_weapon: (RCBot id, primary) for the enemy.
stock combat_best_weapon(id, enemy, &bool:primary)
{
	#pragma unused id, enemy
	primary = true;
	return -1;
}
