// rcbot/fire.inc: the trigger, recoil, aim error, dodges and weapon choice in a fight (port additions, design §4).
// Owner: combat. Ports bot/fire.rs (Fight, Bot::gun_in_hand, double_fire_rate, track_shots, fire_buttons, safe_to_reload, in_aim, capped_speeds, spread_limit, spread_ok, skill_share, ready_to_fire, combat_move, dodge, saw_enemy, enemy_out_of_sight, lost_aim_point, hold_angle, threat, weapon_score, learned_score, best_scored_weapon, choose_weapon, use_powerup, grenade_behind_cover, AIM_RADIUS, throw_pitch), think.rs spawn_init's Fight reset.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: fire_buttons records the attack bits sent (fresh presses) and capped_speeds passes the speeds through; the rest is TODO(fire).

#if defined _rcbot_fire_included
	#endinput
#endif
#define _rcbot_fire_included

// fire.rs AIM_RADIUS.
#define FIRE_AIM_RADIUS		14.0

// think.rs Bot::spawn_init: fight = Fight::default().
stock fire_spawn_init(id)
{
	for (new Fi_I:k; k < Fi_I; k++) g_fi_i[id][k] = 0;
	for (new Fi_F:k; k < Fi_F; k++) g_fi_f[id][k] = 0.0;
	for (new Fi_V:k; k < Fi_V; k++) vec_zero(g_fi_v[id][k]);
}

// fire.rs Bot::fire_buttons: the buttons the usercmd sends.
// TODO(fire): fresh presses per gun (needs_fresh_press, press_interval), akimbo semi, the
// spread limit's release, the dodge dive and roll; only `sent` is kept now.
stock fire_buttons(id, b)
{
	g_fi_i[id][FI_SENT] = b & (IN_ATTACK | IN_ATTACK2);
	return b;
}

// fire.rs Bot::capped_speeds: (forward, side) with the fight's speed cap.
// TODO(fire): the M60's accuracy cap (speed_cap) and the fight's strafe.
stock fire_capped_speeds(id, &Float:forward_speed, &Float:side_speed)
{
	forward_speed = g_mv_f[id][MV_MOVE_SPEED];
	side_speed = g_mv_f[id][MV_STRAFE_SPEED];
}

// TODO(fire): fire.rs Bot::track_shots (count shots by clip for the recoil penalty).
stock fire_track_shots(id)
{
	#pragma unused id
}

// TODO(fire): fire.rs Bot::safe_to_reload.
stock bool:fire_safe_to_reload(id)
{
	#pragma unused id
	return true;
}

// TODO(fire): fire.rs Bot::in_aim.
stock bool:fire_in_aim(id, enemy, Float:dist)
{
	#pragma unused id, enemy, dist
	return false;
}

// TODO(fire): fire.rs Bot::spread_limit.
stock Float:fire_spread_limit(id, Float:dist)
{
	#pragma unused id, dist
	return 0.0;
}

// TODO(fire): fire.rs Bot::ready_to_fire.
stock bool:fire_ready_to_fire(id, enemy, Float:dist)
{
	#pragma unused id, enemy, dist
	return false;
}

// TODO(fire): fire.rs Bot::combat_move.
stock fire_combat_move(id, const Float:enemy_origin[3], Float:dist)
{
	#pragma unused id, enemy_origin, dist
}

// TODO(fire): fire.rs Bot::dodge.
stock fire_dodge(id, enemy, bool:perceptron)
{
	#pragma unused id, enemy, perceptron
}

// TODO(fire): fire.rs Bot::saw_enemy.
stock fire_saw_enemy(id, enemy)
{
	#pragma unused id, enemy
}

// TODO(fire): fire.rs Bot::enemy_out_of_sight.
stock bool:fire_enemy_out_of_sight(id)
{
	#pragma unused id
	return false;
}

// TODO(fire): fire.rs Bot::lost_aim_point.
stock fire_lost_aim_point(id, Float:out[3])
{
	#pragma unused id
	vec_zero(out);
}

// TODO(fire): fire.rs Bot::hold_angle.
stock bool:fire_hold_angle(id)
{
	#pragma unused id
	return false;
}

// TODO(fire): fire.rs Bot::threat (who to fight first).
stock Float:fire_threat(id, ent)
{
	#pragma unused id, ent
	return 0.0;
}

// TODO(fire): fire.rs Bot::weapon_score.
stock Float:fire_weapon_score(id, weapon, Float:dist)
{
	#pragma unused id, weapon, dist
	return 0.0;
}

// TODO(fire): fire.rs Bot::learned_score (rl_ws_score).
stock Float:fire_learned_score(id, weapon, Float:dist)
{
	#pragma unused id, weapon, dist
	return 0.0;
}

// TODO(fire): fire.rs Bot::best_scored_weapon: RCBot id, -1 none.
stock fire_best_scored_weapon(id, Float:dist, except, &Float:score)
{
	#pragma unused id, dist, except
	score = 0.0;
	return -1;
}

// TODO(fire): fire.rs Bot::choose_weapon.
stock fire_choose_weapon(id, Float:dist)
{
	#pragma unused id, dist
}

// TODO(fire): fire.rs Bot::use_powerup.
stock fire_use_powerup(id, Float:dist)
{
	#pragma unused id, dist
}

// TODO(fire): fire.rs Bot::grenade_behind_cover.
stock fire_grenade_behind_cover(id)
{
	#pragma unused id
}

// TODO(fire): fire.rs throw_pitch: (pitch, time) of a throw from src to target, false if out of reach.
stock bool:fire_throw_pitch(const Float:src[3], const Float:target[3], Float:gravity, &Float:pitch, &Float:time)
{
	#pragma unused src, target, gravity
	pitch = 0.0;
	time = 0.0;
	return false;
}
