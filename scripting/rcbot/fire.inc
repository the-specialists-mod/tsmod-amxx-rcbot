// rcbot/fire.inc: the trigger, recoil, aim error, dodges and weapon choice in a fight (port additions, design §4).
// Owner: combat. Ports bot/fire.rs (Fight, Bot::gun_in_hand, double_fire_rate, track_shots, fire_buttons, safe_to_reload, in_aim, capped_speeds, spread_limit, spread_ok, skill_share, ready_to_fire, combat_move, dodge, saw_enemy, enemy_out_of_sight, lost_aim_point, hold_angle, threat, weapon_score, learned_score, best_scored_weapon, choose_weapon, use_powerup, grenade_behind_cover, AIM_RADIUS, throw_pitch), think.rs spawn_init's Fight reset.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: ported. Upstream held +attack for most of every think, which in TS 3.0 fires a
// semi-automatic or burst gun once and then never again until the key is let go; the trigger is
// pressed as the gun's fire mode and rate allow, and the recoil penalty its shots build decides
// when another shot can hit.

#if defined _rcbot_fire_included
	#endinput
#endif
#define _rcbot_fire_included

// The usercmd's attack bits fire.rs Fight::sent keeps.
#define FI_SENT_BITS		(IN_ATTACK | IN_ATTACK2 | IN_ALT1)

// think.rs Bot::spawn_init: fight = Fight::default().
stock fire_spawn_init(id)
{
	for (new Fi_I:k; k < Fi_I; k++) g_fi_i[id][k] = 0;
	for (new Fi_F:k; k < Fi_F; k++) g_fi_f[id][k] = 0.0;
	for (new Fi_V:k; k < Fi_V; k++) vec_zero(g_fi_v[id][k]);
}

// fire.rs Bot::gun_in_hand: the gun data's id of the weapon in hand, -1 for kung fu or none.
stock fire_gun_in_hand(id)
{
	new w = g_bw_i[id][BW_CURRENT];
	return w != RC_KUNGFU && tsweapons_known(w) ? w : -1;
}

// fire.rs Bot::double_fire_rate: the double fire rate power-up runs.
stock bool:fire_double_fire_rate(id)
{
	return (world_field(id, TSPS_ACTIVE_PWUP) & TSPWUP_DFIRERATE) != 0;
}

// fire.rs Bot::skill_share: the skill as a share of the most (0-1).
stock Float:fire_skill_share(id)
{
	return fclamp(float(g_pr_i[id][PR_SKILL]) / float(MAX_BOT_SKILL), 0.0, 1.0);
}

// fire.rs Bot::track_shots: counts the shots the clip shows and keeps the recoil penalty as
// ScaleSpread and PreThink do: the Barrett fired faster than 35 units/s starts from 0.3, the M60
// faster than 175 kicks five times as hard.
stock fire_track_shots(id)
{
	new Float:now = g_now;
	new Float:ts = Float:world_field(id, TSPS_TIMESCALE);
	new Float:scale = ts > 0.0 ? ts : 1.0;
	new bool:dfr = fire_double_fire_rate(id);
	new Float:speed = g_th_f[id][TH_VELOCITY_2D];
	new gun = fire_gun_in_hand(id);
	new weapon = world_weapon(id), clip = world_clip(id);
	g_fi_f[id][FI_PENALTY] = tsweapons_decay_penalty(g_fi_f[id][FI_PENALTY], fmax(now - g_fi_f[id][FI_PENALTY_TIME], 0.0), scale);
	g_fi_f[id][FI_PENALTY_TIME] = now;
	if (gun != -1 && weapon == g_fi_i[id][FI_SHOT_WEAPON] && clip < g_fi_i[id][FI_SHOT_CLIP] && !tsweapons_is_melee_or_thrown(gun))
	{
		new shots = g_fi_i[id][FI_SHOT_CLIP] - clip;
		if (shots > 3)
		{
			shots = 3;
		}
		for (new k = 0; k < shots; k++)
		{
			if (gun == TSW_ID_BARRETT && speed > 35.0)
			{
				g_fi_f[id][FI_PENALTY] = 0.3;
			}
			new Float:kick = gun == TSW_ID_M60 && speed > 175.0 ? 5.0 : 1.0;
			new Float:p = g_fi_f[id][FI_PENALTY];
			if (tsweapons_grow_penalty(gun, p, now - g_fi_f[id][FI_LAST_SHOT], scale, dfr, kick))
			{
				g_fi_f[id][FI_PENALTY] = p;
			}
			g_fi_f[id][FI_LAST_SHOT] = now;
		}
	}
	g_fi_i[id][FI_SHOT_WEAPON] = weapon;
	g_fi_i[id][FI_SHOT_CLIP] = clip;
}

// fire.rs Bot::fire_buttons: the trigger as TS reads it: +attack wanted on a semi-automatic or
// burst gun becomes a press of one usercmd, released at least one, no faster than the gun fires;
// akimbo semi alternates +attack and +attack2. Automatic fire lets go while the recoil passes the
// fight's spread limit. A dodge dive is +alt1 in one usercmd, then +duck through the landing
// (rolls out instead of lying prone).
stock fire_buttons(id, b)
{
	new Float:now = g_now;
	fire_track_shots(id);
	new bool:dfr = fire_double_fire_rate(id);
	new fire_mode = world_field(id, TSPS_FIREMODE);
	new gun = fire_gun_in_hand(id);
	if ((b & IN_ATTACK) && g_fi_f[id][FI_SPREAD_LIMIT_UNTIL] > now && gun != -1 && g_gd_i[gun][GD_SCALE_SPREAD]
		&& g_gd_f[gun][GD_ACCURACY] + g_fi_f[id][FI_PENALTY] > g_fi_f[id][FI_SPREAD_LIMIT])
	{
		b &= ~IN_ATTACK;
	}
	if ((b & IN_ATTACK) && gun != -1 && tsweapons_needs_fresh_press(gun, fire_mode, dfr))
	{
		new bool:akimbo = fire_mode == TS_MODE_AKIMBO_SEMI;
		new key = akimbo && g_fi_i[id][FI_AKIMBO_LEFT] ? IN_ATTACK2 : IN_ATTACK;
		b &= ~IN_ATTACK;
		if (!(g_fi_i[id][FI_SENT] & key) && now >= g_fi_f[id][FI_NEXT_PRESS])
		{
			b |= key;
			g_fi_f[id][FI_NEXT_PRESS] = now + tsweapons_press_interval(gun, fire_mode);
			if (akimbo)
			{
				g_fi_i[id][FI_AKIMBO_LEFT] = _:!g_fi_i[id][FI_AKIMBO_LEFT];
			}
		}
	}
	if (g_fi_i[id][FI_DIVE_AT_SET] && now >= g_fi_f[id][FI_DIVE_AT])
	{
		if (!(g_fi_i[id][FI_SENT] & IN_ALT1))
		{
			b = (b | IN_ALT1) & ~IN_DUCK;
			g_fi_i[id][FI_DIVE_AT_SET] = 0;
			g_fi_f[id][FI_ROLL_UNTIL] = now + 1.2;
		}
		else
		{
			b &= ~IN_ALT1;
		}
	}
	else if (g_fi_f[id][FI_ROLL_UNTIL] > now)
	{
		b |= IN_DUCK;
	}
	g_fi_i[id][FI_SENT] = b & FI_SENT_BITS;
	return b;
}

// fire.rs Bot::safe_to_reload: nothing in sight for 1.5 s, and no infinite clip running.
stock bool:fire_safe_to_reload(id)
{
	new Float:last = g_cb_f[id][CB_LAST_SEE_ENEMY_TIME];
	return !(world_field(id, TSPS_ACTIVE_PWUP) & TSPWUP_INFAMMO) && (last == 0.0 || last + 1.5 < g_now);
}

// fire.rs Bot::in_aim: the view is within 1.5 times the angle a 14-unit radius makes at dist of
// where the bot means to aim (its aim point with its aim error).
stock bool:fire_in_aim(id, enemy, Float:dist)
{
	new Float:target[3];
	if (g_fi_i[id][FI_AIM_TARGET_SET])
	{
		vec_copy(g_fi_v[id][FI_AIM_TARGET], target);
	}
	else
	{
		look_aim_point(id, enemy, target);
	}
	new Float:fwd[3], Float:right[3], Float:gun[3], Float:d[3], Float:to[3];
	make_vectors(g_co_v[id][CO_VANGLE], fwd, right);
	world_eyes(id, gun);
	vec_sub(target, gun, d);
	look_normalize(d, to);
	new Float:off = floatacos(fclamp(vec_dot(fwd, to), -1.0, 1.0), radian);
	return off <= 1.5 * floatatan(AIM_RADIUS / fmax(dist, 1.0), radian);
}

// fire.rs Bot::capped_speeds: the usercmd's forward and side speeds under the fight's speed cap
// (the M60 loses accuracy above 175 units/s).
stock fire_capped_speeds(id, &Float:forward_speed, &Float:side_speed)
{
	new Float:f = g_mv_f[id][MV_MOVE_SPEED], Float:s = g_mv_f[id][MV_STRAFE_SPEED];
	new Float:cap = g_fi_f[id][FI_SPEED_CAP];
	new Float:speed = floatsqroot(f * f + s * s);
	if (g_fi_f[id][FI_SPEED_CAP_UNTIL] > g_now && cap > 0.0 && speed > cap)
	{
		forward_speed = f * cap / speed;
		side_speed = s * cap / speed;
	}
	else
	{
		forward_speed = f;
		side_speed = s;
	}
}

// fire.rs Bot::spread_limit: the most spread a shot at dist may have: 1-3 times (with skill) the
// angle a player makes there, or as tight as the gun gets; twice that with an infinite clip.
// No gun: no limit.
stock Float:fire_spread_limit(id, Float:dist)
{
	new g = fire_gun_in_hand(id);
	if (g == -1)
	{
		return 3.4028235e38;
	}
	new Float:limit = fmax((1.0 + 2.0 * fire_skill_share(id)) * AIM_RADIUS / fmax(dist, 1.0), g_gd_f[g][GD_ACCURACY] + 0.003);
	return world_field(id, TSPS_ACTIVE_PWUP) & TSPWUP_INFAMMO ? limit * 2.0 : limit;
}

// fire.rs Bot::spread_ok: base spread plus the recoil penalty within the spread limit. The
// Barrett moving faster than 35 units/s fires at 0.3: never then.
stock bool:fire_spread_ok(id, Float:dist)
{
	new g = fire_gun_in_hand(id);
	if (g == -1)
	{
		return true;
	}
	if (g == TSW_ID_BARRETT && g_th_f[id][TH_VELOCITY_2D] > 35.0)
	{
		return false;
	}
	return !g_gd_i[g][GD_SCALE_SPREAD] || g_gd_f[g][GD_ACCURACY] + g_fi_f[id][FI_PENALTY] <= fire_spread_limit(id, dist);
}

// fire.rs Bot::ready_to_fire: the reaction time since the enemy appeared has passed, the view is
// on the aim, and the spread allows a hit (design §4; upstream fired on 90% of thinks).
stock bool:fire_ready_to_fire(id, enemy, Float:dist)
{
	return g_now >= g_fi_f[id][FI_REACT_AT] && fire_in_aim(id, enemy, dist) && fire_spread_ok(id, dist);
}

// fire.rs Bot::combat_move (design §4; upstream walked to the cheapest cost bucket, §5.55): to the
// gun's range (close in with a shotgun, back off with a rifle or the Barrett), strafing left and
// right in 0.25-0.6 s spells at full speed, crouching in one spell in seven. The Barrett stands
// still to shoot; the M60 keeps under 175 units/s.
stock fire_combat_move(id, const Float:enemy_origin[3], Float:dist)
{
	new Float:now = g_now;
	new gun = fire_gun_in_hand(id);
	new Float:near = 150.0, Float:far = 800.0;
	if (gun != -1)
	{
		new kind = g_gd_i[gun][GD_KIND];
		if (g_gd_f[gun][GD_PELLETS] > 1.0 || kind == GK_SHOTGUN)
		{
			near = 0.0;
			far = 250.0;
		}
		else if (kind == GK_SNIPER)
		{
			near = 500.0;
			far = 3.4028235e38;
		}
		else if (kind == GK_RIFLE)
		{
			near = 300.0;
			far = 1400.0;
		}
	}
	new Float:me[3], Float:away[3], Float:n[3], Float:to[3];
	world_origin(id, me);
	vec_sub(me, enemy_origin, away);
	away[2] = 0.0;
	look_normalize(away, n);
	if (dist > far)
	{
		movement_set_move_vector(id, enemy_origin);
	}
	else if (dist < near)
	{
		vec_mad(me, n, 128.0, to);
		movement_set_move_vector(id, to);
	}
	else
	{
		movement_stop_moving(id);
	}
	if (gun == TSW_ID_BARRETT)
	{
		movement_stop_moving(id);
		g_mv_f[id][MV_STRAFE_SPEED] = 0.0;
		g_mv_f[id][MV_STRAFE_TIME] = 0.0;
		return;
	}
	if (gun == TSW_ID_M60)
	{
		g_fi_f[id][FI_SPEED_CAP] = 170.0;
		g_fi_f[id][FI_SPEED_CAP_UNTIL] = now + 0.2;
	}
	if (g_fi_f[id][FI_STRAFE_UNTIL] < now)
	{
		// Mostly the other way: a change of direction is what throws an aim off.
		new bool:flip = random_num(0, 100) < 70;
		if (g_fi_f[id][FI_STRAFE_DIR] == 0.0 || !flip)
		{
			g_fi_f[id][FI_STRAFE_DIR] = random_num(0, 1) == 0 ? -1.0 : 1.0;
		}
		else
		{
			g_fi_f[id][FI_STRAFE_DIR] = -g_fi_f[id][FI_STRAFE_DIR];
		}
		g_fi_f[id][FI_STRAFE_UNTIL] = now + random_float(0.25, 0.6);
		if (random_num(0, 6) == 0)
		{
			g_mv_f[id][MV_START_DUCK_TIME] = now;
			g_mv_f[id][MV_END_DUCK_TIME] = g_fi_f[id][FI_STRAFE_UNTIL];
		}
	}
	// The move speeds and times are movement's fields, which fire.rs sets in a fight.
	g_mv_f[id][MV_STRAFE_TIME] = g_fi_f[id][FI_STRAFE_UNTIL];
	g_mv_f[id][MV_STRAFE_SPEED] = g_fi_f[id][FI_STRAFE_DIR] * g_mv_f[id][MV_MAX_SPEED];
}

// fire.rs Bot::dodge (design §4; upstream pressed +alt1 on every think its stunt perceptron
// fired): while moving sideways faster than 80 units/s, just hurt or with the enemy's view on
// the bot, light enough to dive (fewer than 62 of 81 slots used), on the ground, not in a stunt
// nor while one cools down, at most once every 3-6 s; when the perceptron fires, or by a
// skill-scaled chance a think.
stock fire_dodge(id, enemy, bool:perceptron)
{
	new Float:now = g_now;
	if (g_fi_f[id][FI_NEXT_DIVE] > now || g_fi_i[id][FI_DIVE_AT_SET] || !movement_stunt_ready(id) || !(world_flags(id) & TSPF_ON_GROUND)
		|| 81 - world_field(id, TSPS_FREESLOTS) >= 62)
	{
		return;
	}
	new Float:ang[3], Float:fwd[3], Float:right[3], Float:vel[3];
	ang[1] = g_co_v[id][CO_VANGLE][1];
	make_vectors(ang, fwd, right);
	world_velocity(id, vel);
	if (floatabs(vec_dot(vel, right)) <= 80.0)
	{
		return;
	}
	new Float:hurt_time = g_lk_f[id][LK_HURT_TIME];
	new bool:hurt = hurt_time > 0.0 && hurt_time + 0.5 > now;
	new Float:eang[3], Float:efwd[3], Float:eright[3], Float:gun[3], Float:eo[3], Float:eofs[3], Float:d[3], Float:n[3];
	combat_ent_vangle(enemy, eang);
	make_vectors(eang, efwd, eright);
	world_eyes(id, gun);
	combat_ent_origin(enemy, eo);
	combat_ent_view_ofs(enemy, eofs);
	vec_add(eo, eofs, eo);
	vec_sub(gun, eo, d);
	look_normalize(d, n);
	new bool:aimed_at = vec_dot(efwd, n) > 0.95;
	if (!(hurt || aimed_at) || !(perceptron || random_float(0.0, 1.0) < 0.05 + 0.1 * fire_skill_share(id)))
	{
		return;
	}
	g_fi_i[id][FI_DIVE_AT_SET] = 1;
	g_fi_f[id][FI_DIVE_AT] = now;
	g_fi_f[id][FI_NEXT_DIVE] = now + random_float(3.0, 6.0);
}

// fire.rs Bot::saw_enemy: the enemy is in sight: remember how it moved and where its head was.
stock fire_saw_enemy(id, enemy)
{
	new Float:v[3], Float:o[3], Float:ofs[3];
	g_fi_f[id][FI_LAST_SIGHT] = g_now;
	combat_ent_velocity(enemy, v);
	vec_set(g_fi_v[id][FI_SIGHT_VELOCITY], v[0], v[1], 0.0);
	combat_ent_origin(enemy, o);
	combat_ent_view_ofs(enemy, ofs);
	vec_set(g_fi_v[id][FI_SIGHT_HEAD], o[0] + ofs[0], o[1] + ofs[1], o[2] + ofs[2] - 2.0);
	g_fi_f[id][FI_HOLD_UNTIL] = 0.0;
}

// fire.rs Bot::enemy_out_of_sight: seen once, not in the last 0.15 s.
stock bool:fire_enemy_out_of_sight(id)
{
	new Float:last = g_fi_f[id][FI_LAST_SIGHT];
	return last > 0.0 && last + 0.15 < g_now;
}

// fire.rs Bot::lost_aim_point (design §4; upstream aimed at its live position through the wall):
// its head where it went, carried on by its last velocity for up to 0.5 s if that point is in
// sight, else where it was last seen.
stock fire_lost_aim_point(id, Float:out[3])
{
	new Float:p[3];
	vec_mad(g_fi_v[id][FI_SIGHT_HEAD], g_fi_v[id][FI_SIGHT_VELOCITY], fclamp(g_now - g_fi_f[id][FI_LAST_SIGHT], 0.0, 0.5), p);
	if (combat_fvisible_point(id, p))
	{
		vec_copy(p, out);
	}
	else
	{
		vec_copy(g_fi_v[id][FI_SIGHT_HEAD], out);
	}
}

// fire.rs Bot::hold_angle: hold the angle where the enemy went out of sight? Decided once per
// loss: for 1-3 s when healthy (over 60%) with a loaded gun; otherwise the bot pushes on.
stock bool:fire_hold_angle(id)
{
	new Float:now = g_now;
	if (g_fi_f[id][FI_HOLD_UNTIL] == 0.0)
	{
		new w = g_bw_i[id][BW_CURRENT];
		new bool:loaded = w >= 0 && w < RC_MAX_WEAPONS && !botweapons_is_melee(id, w) && g_bw_weapon[id][w][BW_CLIP] > 0;
		g_fi_f[id][FI_HOLD_UNTIL] = loaded && world_health(id) > combat_ent_max_health(id) * 0.6 ? now + random_float(1.0, 3.0) : -1.0;
	}
	return g_fi_f[id][FI_HOLD_UNTIL] > now;
}

// fire.rs Bot::threat with the bot's reputation of ent, if any (rep_set).
stock Float:fire_threat_rep(id, ent, rep, bool:rep_set)
{
	if (!combat_ent_is_client(ent))
	{
		// Upstream's breakable priority, 5.
		return 1.5;
	}
	new Float:now = g_now;
	new Float:priority = float(rep_set ? BOT_MAX_REP - rep : 10);
	new Float:score = priority * 0.3;
	new Float:eang[3], Float:fwd[3], Float:right[3], Float:gun[3], Float:eo[3], Float:eofs[3], Float:d[3], Float:n[3];
	combat_ent_vangle(ent, eang);
	make_vectors(eang, fwd, right);
	world_eyes(id, gun);
	combat_ent_origin(ent, eo);
	combat_ent_view_ofs(ent, eofs);
	vec_add(eo, eofs, d);
	vec_sub(gun, d, d);
	look_normalize(d, n);
	if (vec_dot(fwd, n) > 0.95)
	{
		score += 2.0;
	}
	if (g_fi_i[id][FI_ATTACKER] == ent && g_fi_f[id][FI_ATTACKER_TIME] + 3.0 > now)
	{
		score += 2.5;
	}
	new Float:max_health = combat_ent_max_health(ent);
	if (max_health > 0.0)
	{
		score += (1.0 - fclamp(combat_ent_health(ent) / max_health, 0.0, 1.0)) * 1.5;
	}
	if (world_weapon(ent) != 0)
	{
		score += 1.0;
	}
	new Float:me[3];
	world_origin(id, me);
	return score + 2.0 * (1.0 - fmin(vec_distance(eo, me), 2000.0) / 2000.0);
}

// fire.rs Bot::threat (who to fight first, design §4): its reputation's priority, more if it
// aims at the bot, hurt it in the last 3 s, is hurt, holds a gun, and the nearer it is.
stock Float:fire_threat(id, ent)
{
	new rep = 0;
	new bool:rep_set = combat_ent_is_client(ent) && combat_rep_of(id, ent, rep);
	return fire_threat_rep(id, ent, rep, rep_set);
}

// fire.rs Bot::weapon_score (design §4): damage a second, times the range falloff and the share
// of shots that hit a player there with its base spread and the recoil it builds; blades within
// 80 units; nothing for an empty clip (unless the clip is infinite), the grenade or kung fu.
stock Float:fire_weapon_score(id, weapon, Float:dist)
{
	if (!botweapons_has(id, weapon) || !tsweapons_known(weapon))
	{
		return 0.0;
	}
	new kind = g_gd_i[weapon][GD_KIND];
	if (kind == GK_GRENADE)
	{
		return 0.0;
	}
	if (kind == GK_KNIFE || kind == GK_KATANA)
	{
		return dist < 80.0 ? g_gd_f[weapon][GD_DAMAGE] / g_gd_f[weapon][GD_RATE] : 0.0;
	}
	if (g_bw_weapon[id][weapon][BW_CLIP] <= 0 && !(world_field(id, TSPS_ACTIVE_PWUP) & TSPWUP_INFAMMO))
	{
		return 0.0;
	}
	new Float:interval = g_gd_i[weapon][GD_HAS_AUTO] ? g_gd_f[weapon][GD_RATE] : tsweapons_press_interval(weapon, TS_MODE_SEMI);
	new Float:steady = 0.0;
	if (g_gd_i[weapon][GD_SCALE_SPREAD])
	{
		new Float:grow = g_gd_f[weapon][GD_ACC_KICK] * 2.0 * (1.0 - fmin(interval, 1.0)) - 0.1 * interval;
		steady = fmin(fmax(grow, 0.0) * 3.0, 0.1);
	}
	new Float:hit = fmin((AIM_RADIUS / fmax(dist, 1.0)) / fmax(g_gd_f[weapon][GD_ACCURACY] + steady, 0.0001), 1.0);
	return g_gd_f[weapon][GD_DAMAGE] * g_gd_f[weapon][GD_PELLETS] * tsweapons_range_multiplier(weapon, dist) * hit * hit / interval;
}

// fire.rs Bot::learned_score: weapon_score corrected by what the weapon has done for this bot at
// that range (the learned table, design §4), once the bot has enough time with it there: the
// learned damage a second over the theory's, square-rooted and kept within 0.5-1.4x.
stock Float:fire_learned_score(id, weapon, Float:dist)
{
	new Float:theory = fire_weapon_score(id, weapon, dist);
	if (theory <= 0.0)
	{
		return theory;
	}
	new Float:learned;
	if (learning_weapon_score(id, weapon, dist, learned))
	{
		return theory * fclamp(floatsqroot(fmax(learned / theory, 0.0)), 0.5, 1.4);
	}
	return theory;
}

// fire.rs Bot::best_scored_weapon: the best weapon the bot owns for dist other than except (-1
// none), with its score. RCBot id, -1 none.
stock fire_best_scored_weapon(id, Float:dist, except, &Float:score)
{
	new best = -1;
	score = 0.0;
	for (new w = 1; w < RC_MAX_WEAPONS; w++)
	{
		if (w == except)
		{
			continue;
		}
		new Float:s = fire_learned_score(id, w, dist);
		if (s > 0.0 && (best == -1 || s > score))
		{
			best = w;
			score = s;
		}
	}
	return best;
}

// fire.rs Bot::choose_weapon (design §4; upstream chose only when the clip ran dry): every 0.5 s,
// not while a reload runs or a switch is pending, when another weapon is worth 30% more (and a
// little) than the one in hand. A kung fu power-up running makes the bot fight hand to hand
// within 300 units.
stock fire_choose_weapon(id, Float:dist)
{
	new Float:now = g_now;
	if (g_fi_f[id][FI_NEXT_WEAPON_CHOICE] > now || g_bw_i[id][BW_SWITCH_PENDING] != -1)
	{
		return;
	}
	g_fi_f[id][FI_NEXT_WEAPON_CHOICE] = now + 0.5;
	new cur = g_bw_i[id][BW_CURRENT];
	if (cur < 0)
	{
		return;
	}
	if ((world_field(id, TSPS_ACTIVE_PWUP) & TSPWUP_KUNGFU) && dist < 300.0)
	{
		think_switch_weapon(id, RC_KUNGFU);
		return;
	}
	if (cur >= RC_MAX_WEAPONS)
	{
		return;
	}
	// A reload under way, or an empty clip (Think's empty-gun choice decides that one).
	new bool:reloading = g_bw_f[id][BW_NEXT_RELOAD_TIME] > now && g_bw_weapon[id][cur][BW_CLIP] < g_bw_weapon[id][cur][BW_MAX_CLIP];
	if (reloading || botweapons_clip_is_empty(id, cur))
	{
		return;
	}
	new Float:here = fire_learned_score(id, cur, dist);
	new Float:score;
	new best = fire_best_scored_weapon(id, dist, cur, score);
	if (best != -1 && score > here * 1.3 + 1.0)
	{
		think_switch_weapon(id, best);
	}
}

// fire.rs Bot::use_powerup (design §4; upstream sent usepowerup on first seeing an enemy): slow
// motion or slow pause in a mid-range fight (200-1200 units), an infinite clip or double fire
// rate with a gun out, kung fu within 300 units, health or armour under 60% health, the rest at
// once; at most every 2 s.
stock fire_use_powerup(id, Float:dist)
{
	new Float:now = g_now;
	new stored = world_field(id, TSPS_STORED_PWUP);
	if (stored == 0 || world_field(id, TSPS_ACTIVE_PWUP) != 0 || g_fi_f[id][FI_NEXT_POWERUP] > now)
	{
		return;
	}
	new g = fire_gun_in_hand(id);
	new bool:gun = g != -1 && !tsweapons_is_melee_or_thrown(g);
	new bool:hurt = world_health(id) < combat_ent_max_health(id) * 0.6;
	new bool:useful;
	if (stored & (TSPWUP_SLOWMO | TSPWUP_SLOWPAUSE))
	{
		useful = dist >= 200.0 && dist < 1200.0;
	}
	else if (stored & (TSPWUP_INFAMMO | TSPWUP_DFIRERATE))
	{
		useful = gun;
	}
	else if (stored & TSPWUP_KUNGFU)
	{
		useful = dist < 300.0;
	}
	else if (stored & (TSPWUP_HEALTH | TSPWUP_ARMOR))
	{
		useful = hurt;
	}
	else
	{
		useful = true;
	}
	if (useful)
	{
		bot_action_command(id, "usepowerup");
		g_fi_f[id][FI_NEXT_POWERUP] = now + 2.0;
	}
}

// fire.rs Bot::grenade_behind_cover (design §4): an M61 in the inventory, the enemy out of sight
// for half a second to 3 s, last seen 300-750 units away, at most every 8 s, by a skill-scaled
// chance: a priority THROW_GRENADE at where it was last seen, 4 s to complete.
stock fire_grenade_behind_cover(id)
{
	new Float:now = g_now;
	new Float:lost_for = now - g_fi_f[id][FI_LAST_SIGHT];
	new Float:dist = combat_distance_from(id, g_cb_v[id][CB_LAST_SEE_ENEMY_POS]);
	if (g_fi_f[id][FI_NEXT_GRENADE] > now || !(lost_for >= 0.5 && lost_for < 3.0) || !(dist >= 300.0 && dist < 750.0)
		|| !(botweapons_has(id, TSW_ID_M61) && g_bw_weapon[id][TSW_ID_M61][BW_CLIP] > 0) || tasks_has(id, TASK_THROW_GRENADE))
	{
		return;
	}
	g_fi_f[id][FI_NEXT_GRENADE] = now + 8.0;
	if (random_float(0.0, 1.0) < 0.3 + 0.5 * fire_skill_share(id))
	{
		new sched = tasks_new_schedule_id(id);
		new slot = tasks_add_priority(id, TASK_THROW_GRENADE, sched, 0, 0, 0, 0.0, g_cb_v[id][CB_LAST_SEE_ENEMY_POS]);
		// Task::with_timeout(now, 4.0): the task's time to complete (tasks' field) set here.
		if (slot >= 0 && slot < RC_MAX_TASKS)
		{
			g_tk_f[id][slot][TK_TIME_TO_COMPLETE] = now + 4.0;
		}
	}
}

// fire.rs throw_pitch: TS 3.0's M61 throw (GrenadeIdle): the view pitch p (down positive) becomes
// p * (10/9 or 8/9) - 10, thrown at min(750, (90 - p') * 7). The view pitch that lands a grenade
// from src nearest target under gravity (the lowest arc within a 3 s fuse), and its miss in
// units; false when nothing comes within 64 units.
stock bool:fire_throw_pitch(const Float:src[3], const Float:target[3], Float:gravity, &Float:pitch, &Float:miss)
{
	new Float:d = vec_distance_2d(target, src);
	new Float:h = target[2] - src[2];
	new bool:found = false;
	pitch = 0.0;
	miss = 0.0;
	// From looking down (flat throws) up: the first that lands is the lowest arc.
	for (new step = 0; step <= 320; step++)
	{
		new Float:p = 80.0 - float(step) * 0.5;
		new Float:thrown = p * (p >= 0.0 ? 10.0 / 9.0 : 8.0 / 9.0) - 10.0;
		new Float:speed = fmin((90.0 - thrown) * 7.0, 750.0);
		new Float:s = floatsin(-thrown, degrees), Float:c = floatcos(-thrown, degrees);
		if (c > 0.01)
		{
			new Float:t = d / (speed * c);
			if (t <= 3.0)
			{
				new Float:m = floatabs(speed * s * t - 0.5 * gravity * t * t - h);
				if (!found || m < miss - 1.0)
				{
					found = true;
					pitch = p;
					miss = m;
				}
			}
		}
	}
	return found && miss < 64.0;
}
