// rcbot/learning.inc: the bot's learners: glue to the rcbot_learn module.
// Owner: kungfu. Ports bot/learning.rs (BotLearning, Bot::ledger, learned_threat, waypoint_danger, weapon_stats, learned_weapon_score, blow_weapon, combat_event, learn_think, choose_loadout, credit_loadout, learn_on_death), think.rs's death learning (death_fitness, learned.on_death) and spawn (learned.on_spawn), manager.rs combat_event / update_opponents / update_heatmap / refresh_player_keys / player_stunt, host bots.rs new_bot / save_bot / save_globals and lib.rs game_init's global load / server_activate's opponents.new_map. Mapping of natives: crates/learn/src/natives.rs.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// The learned objects (BotLearnedData, the ledger, the weapon table, the loadout bandit, the
// opponent model) live in rcbot_learn; the bookkeeping learning.rs keeps beside them (the life's
// start, kills and damage, the last clip, the loadout bought and credited) is state.inc's
// learning section. The heatmap is rcbot_nav's (navigate.inc).

#if defined _rcbot_learning_included
	#endinput
#endif
#define _rcbot_learning_included

// learning.rs MAX_TICK: the longest think gap counted as exposure.
#define LN_MAX_TICK				0.25
// MAX_SHOTS_PER_TICK: most rounds a clip can drop between two thinks by firing.
#define LN_MAX_SHOTS_PER_TICK	10
// FIGHT_RECENT: seconds after its last think in which a kung fu fight still counts as under way.
#define LN_FIGHT_RECENT			1.0
// ledger.rs HITGROUP_HEAD.
#define LN_HITGROUP_HEAD		1
// localinfo key: the globals were read in this server process (lib.rs game_init reads them once;
// the plugin is reloaded every map, the module's data is not).
#define LN_LOADED_KEY			"rcbot_amxx_learn"

// PlayerKeys (g.learned.player_keys): each player's opponent key as last given to rl_opp_set_key
// (RlKeyKind, id), by entity index.
new g_ln_key_kind[RC_SLOTS];
new g_ln_key_id[RC_SLOTS];

// The last blow each player took from another (ts_damage), for the kill events tsx lacks.
new g_ln_blow_attacker[RC_SLOTS];
new g_ln_blow_weapon[RC_SLOTS];
new g_ln_blow_hitgroup[RC_SLOTS];

// ---- the globals and a bot's files -----------------------------------------------------------

// host lib.rs game_init (once per server process): rl_load_globals (team<n>.rld, tsweaps.rld,
// weaponstats.rwr, loadouts.rlo, botprofiles/players.rop); server_activate's
// opponents.new_map (every map). Called from plugin_init.
stock learning_load_globals()
{
	new loaded[4];
	get_localinfo(LN_LOADED_KEY, loaded, charsmax(loaded));
	if (loaded[0] != '1')
	{
		rl_load_globals();
		set_localinfo(LN_LOADED_KEY, "1");
	}
	rl_opp_new_map();
	// The module keeps the last map's keys; start both sides from none.
	for (new i = 1; i <= MAX_PLAYERS; i++)
	{
		rl_opp_set_key(i, RlKey_None, 0);
		g_ln_key_kind[i] = 0;
		g_ln_key_id[i] = 0;
		g_ln_blow_attacker[i] = 0;
	}
}

// host bots.rs save_globals: rl_save_globals (the heatmap is navigate's).
stock learning_save_globals()
{
	rl_save_globals();
}

// host bots.rs new_bot: the bot's .rld, .rwr and .rlo (rl_bot_load with the profile id and name).
stock learning_bot_load(id)
{
	g_ln_i[id][LN_HANDLE] = _:rl_bot_load(g_pr_i[id][PR_ID], g_bot_name[id]);
}

// host bots.rs save_bot: rl_bot_save.
stock learning_bot_save(id)
{
	new h = g_ln_i[id][LN_HANDLE];
	if (h != 0)
	{
		rl_bot_save(RcLearn:h);
	}
}

// The bot left: its learned data freed (saved by backend_save_bot before).
stock learning_bot_free(id)
{
	new RcLearn:h = RcLearn:g_ln_i[id][LN_HANDLE];
	if (h != RcLearn:0)
	{
		rl_bot_free(h);
	}
	g_ln_i[id][LN_HANDLE] = 0;
}

// ---- player keys --------------------------------------------------------------------------------

// manager.rs refresh_player_keys (PlayerKeys::refresh): RCBot's bots by profile, the rest by
// reputation id; the module is told of every change.
stock learning_refresh_player_keys()
{
	for (new i = 1; i <= MAX_PLAYERS; i++)
	{
		new kind = 0, kid = 0;
		if (is_rcbot(i))
		{
			kind = _:RlKey_Bot;
			kid = g_pr_i[i][PR_ID];
		}
		else if (g_client_rep_id[i] != NO_CLIENT)
		{
			kind = _:RlKey_Player;
			kid = g_client_rep_id[i];
		}
		if (kind != g_ln_key_kind[i] || kid != g_ln_key_id[i])
		{
			rl_opp_set_key(i, RlKeyKind:kind, kid);
			g_ln_key_kind[i] = kind;
			g_ln_key_id[i] = kid;
		}
	}
}

// PlayerKeys::get: the key of the player at index (RlKeyKind, id), false for none.
stock bool:learning_player_key(index, &kind, &kid)
{
	if (index < 1 || index > MAX_PLAYERS || g_ln_key_kind[index] == 0)
	{
		kind = 0;
		kid = 0;
		return false;
	}
	kind = g_ln_key_kind[index];
	kid = g_ln_key_id[index];
	return true;
}

// ---- queries ----------------------------------------------------------------------------------------

// learning.rs Bot::learned_threat: 1 for an average or unknown player, 0.25-4.
stock Float:learning_learned_threat(id, player)
{
	#pragma unused id
	if (player < 1 || player > MAX_PLAYERS)
	{
		return 1.0;
	}
	return rl_opp_threat(player);
}

// learning.rs Bot::waypoint_danger: the heatmap for the bot's team (0..1).
stock Float:learning_waypoint_danger(id, waypoint)
{
	return nav_heatmap_danger(waypoint, g_th_i[id][TH_TEAM]);
}

// learning.rs Bot::learned_weapon_score (a TS id, 0 or 38 kung fu): false until the bot has
// MIN_SAMPLES seconds of its own there.
stock bool:learning_weapon_score(id, weapon, Float:dist, &Float:score)
{
	score = 0.0;
	new h = g_ln_i[id][LN_HANDLE];
	if (h == 0)
	{
		return false;
	}
	return rl_ws_score(RcLearn:h, weapon, dist, score);
}

// ---- events -----------------------------------------------------------------------------------------

// manager.rs update_opponents' is_bot: a bot of any backend.
static stock bool:ln_is_bot(i)
{
	return i >= 1 && i <= MAX_PLAYERS && (world_is_bot(i) || is_rcbot(i));
}

// An entity's origin (Entity::origin: a player's snapshot, else entvars).
static stock ln_origin(e, Float:out[3])
{
	if (e >= 1 && e <= MAX_PLAYERS)
	{
		world_origin(e, out);
	}
	else if (e > 0 && pev_valid(e))
	{
		pev(e, pev_origin, out);
	}
	else
	{
		vec_zero(out);
	}
}

// learning.rs combat_event's entry: a ledger record with the distance to other (0 the world)
// and the bot's waypoint.
static stock ln_record(id, Float:now, other, weapon, Float:dealt, Float:taken, bool:kill, bool:death)
{
	new h = g_ln_i[id][LN_HANDLE];
	if (h == 0)
	{
		return;
	}
	new Float:dist = 0.0;
	if (other != 0)
	{
		new Float:me[3], Float:at[3];
		world_origin(id, me);
		ln_origin(other, at);
		dist = vec_distance(at, me);
	}
	rl_ledger_record(RcLearn:h, now, other, weapon, dealt, taken, kill, death, dist, g_nv_i[id][NV_CURRENT_WPT]);
}

// learning.rs Bot::blow_weapon: a grenade (24) or another projectile (-1) when something other
// than the attacker dealt the blow, else the attacker's weapon in hand.
static stock ln_blow_weapon(attacker, inflictor)
{
	if (inflictor > 0 && inflictor != attacker)
	{
		if (!pev_valid(inflictor))
		{
			return -1;
		}
		new cls[32];
		pev(inflictor, pev_classname, cls, charsmax(cls));
		return contain(cls, "grenade") != -1 ? 24 : -1;
	}
	return attacker >= 1 && attacker <= MAX_PLAYERS ? world_weapon(attacker) : -1;
}

// manager.rs combat_event for a damage (attacker another player, as ts_damage passes it on):
// update_heatmap, update_opponents (rl_opp_hit_on_bot), every bot's Bot::combat_event (the
// ledger, the life's damage, the weapon table). wid is the rules' weapon (24 a grenade, -1 other):
// without it, Bot::blow_weapon's guess from the inflictor.
stock learning_damage_event(victim, attacker, inflictor, Float:damage, hitgroup, wid)
{
	if (victim >= 1 && victim <= MAX_PLAYERS)
	{
		g_ln_blow_attacker[victim] = attacker;
		g_ln_blow_weapon[victim] = wid;
		g_ln_blow_hitgroup[victim] = hitgroup;
	}
	learning_refresh_player_keys();
	navigate_heatmap_damage(victim, damage);
	new kind, kid;
	new weapon = wid >= 0 ? wid : ln_blow_weapon(attacker, inflictor);
	if (learning_player_key(attacker, kind, kid) && attacker != victim && ln_is_bot(victim))
	{
		new Float:a[3], Float:v[3];
		ln_origin(attacker, a);
		ln_origin(victim, v);
		rl_opp_hit_on_bot(attacker, weapon, vec_distance(a, v), damage, hitgroup == LN_HITGROUP_HEAD);
	}
	new Float:now = g_now;
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (!g_co_i[id][CO_USED] || g_ln_i[id][LN_HANDLE] == 0)
		{
			continue;
		}
		if (attacker == id && victim != id)
		{
			ln_record(id, now, victim, weapon, damage, 0.0, false, false);
			g_ln_f[id][LN_LIFE_DEALT] += damage;
			if (victim > 0)
			{
				new Float:me[3], Float:v[3];
				world_origin(id, me);
				ln_origin(victim, v);
				rl_ws_add_hit(RcLearn:g_ln_i[id][LN_HANDLE], weapon, vec_distance(v, me), damage);
			}
		}
		else if (victim == id && attacker != id)
		{
			ln_record(id, now, attacker, weapon, 0.0, damage, false, false);
		}
	}
}

// The port's kill_scored built from ts_player_killed: the killer (0 for none, the victim for a
// suicide), and the weapon and headshot of the victim's last blow when the killer dealt it
// (else -1, no headshot). Call it for every death (core: ts_player_killed).
stock learning_player_killed(victim, killer)
{
	if (victim < 1 || victim > MAX_PLAYERS)
	{
		return;
	}
	if (killer < 0)
	{
		killer = 0;
	}
	new wid = -1, bool:headshot = false;
	if (killer != 0 && killer != victim && g_ln_blow_attacker[victim] == killer)
	{
		wid = g_ln_blow_weapon[victim];
		headshot = g_ln_blow_hitgroup[victim] == LN_HITGROUP_HEAD;
	}
	g_ln_blow_attacker[victim] = 0;
	learning_kill_event(victim, killer, wid, headshot);
}

// manager.rs combat_event for a kill (KillEvent): update_heatmap, update_opponents (kills of and
// deaths to bots), every bot's Bot::combat_event (the ledger, the life's kills, the kung fu fight
// that ends with it).
stock learning_kill_event(victim, killer, wid, bool:headshot)
{
	#pragma unused headshot
	learning_refresh_player_keys();
	navigate_heatmap_kill(victim, killer);
	new kind, kid;
	if (killer != 0 && killer != victim)
	{
		if (learning_player_key(killer, kind, kid) && ln_is_bot(victim))
		{
			rl_opp_kill_of_bot(killer);
		}
		if (learning_player_key(victim, kind, kid) && ln_is_bot(killer))
		{
			rl_opp_death_to_bot(victim);
		}
	}
	new Float:now = g_now;
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (!g_co_i[id][CO_USED] || g_ln_i[id][LN_HANDLE] == 0)
		{
			continue;
		}
		if (killer == id && victim != id)
		{
			ln_record(id, now, victim, wid, 0.0, 0.0, true, false);
			g_ln_i[id][LN_LIFE_KILLS]++;
		}
		else if (victim == id)
		{
			ln_record(id, now, killer == id ? 0 : killer, wid, 0.0, 0.0, false, true);
			rl_ledger_close(RcLearn:g_ln_i[id][LN_HANDLE]);
		}
		// The fight ends with the opponent's death (by anyone) or the bot's.
		if (kungfu_opponent(id) == victim || victim == id)
		{
			new fight_end;
			if (!kungfu_fighting(id, LN_FIGHT_RECENT))
			{
				fight_end = KF_END_BROKEN;
			}
			else if (victim == id)
			{
				fight_end = KF_END_DEATH;
			}
			else if (killer == id)
			{
				fight_end = KF_END_KILL;
			}
			else
			{
				fight_end = KF_END_BROKEN;
			}
			kungfu_settle(id, fight_end);
		}
	}
}

// manager.rs player_stunt: a player started a stunt (rl_opp_stunt; tsfun's client_stunt).
stock learning_player_stunt(player)
{
	if (player < 1 || player > MAX_PLAYERS)
	{
		return;
	}
	learning_refresh_player_keys();
	new kind, kid;
	if (learning_player_key(player, kind, kid))
	{
		rl_opp_stunt(player, g_now);
	}
}

// ---- the life -----------------------------------------------------------------------------------

// RCBot's team as an index of the team GAs: anything outside 0-3 is 0.
static stock ln_team(id)
{
	new t = g_th_i[id][TH_TEAM];
	return t >= 0 && t < 4 ? t : 0;
}

// think.rs spawn_init: learned.on_spawn (the next bred personal genes, the team's survival genes).
stock learning_on_spawn(id)
{
	new h = g_ln_i[id][LN_HANDLE];
	if (h != 0)
	{
		rl_bot_on_spawn(RcLearn:h, ln_team(id));
	}
}

// think.rs Think's death (the first think dead): death_fitness, learn_on_death, learned.on_death.
// prev_team_score is think's field; think.rs updates it here.
stock learning_on_death(id)
{
	new Float:now = g_now;
	new Float:survival = now - g_th_f[id][TH_SPAWN_TIME];
	new frags_since = world_frags(id) - g_th_i[id][TH_FRAGS_SINCE_DEATH];
	new team = ln_team(id);
	new team_score_since = g_team_scores[team] - g_th_i[id][TH_PREV_TEAM_SCORE];
	g_th_i[id][TH_PREV_TEAM_SCORE] = g_team_scores[team];
	new Float:fitness = rl_death_fitness(survival, frags_since, team_score_since);
	ln_learn_on_death(id, now);
	new h = g_ln_i[id][LN_HANDLE];
	if (h != 0)
	{
		rl_bot_on_death(RcLearn:h, team, fitness, g_th_i[id][TH_USED_MELEE] != 0);
	}
}

// learning.rs Bot::learn_on_death: the life's learners settled; a kung fu fight under way was
// lost.
static stock ln_learn_on_death(id, Float:now)
{
	if (kungfu_fighting(id, LN_FIGHT_RECENT))
	{
		kungfu_settle(id, KF_END_DEATH);
	}
	new h = g_ln_i[id][LN_HANDLE];
	new arm = g_ln_i[id][LN_CREDITED_LOADOUT];
	g_ln_i[id][LN_CREDITED_LOADOUT] = -1;
	if (h != 0 && arm >= 0 && g_ln_f[id][LN_LIFE_START] > 0.0 && !config_is_on(CFG_DISABLE_WEAPON_LEARN))
	{
		new Float:r = rl_life_reward(g_ln_i[id][LN_LIFE_KILLS], 1, g_ln_f[id][LN_LIFE_DEALT], now - g_ln_f[id][LN_LIFE_START]);
		// The bot's estimates and every bot's (loadout_prior) together.
		rl_loadout_reward(RcLearn:h, arm, r);
	}
	if (h != 0)
	{
		rl_ledger_close(RcLearn:h);
	}
	g_ln_f[id][LN_LIFE_START] = 0.0;
	g_ln_f[id][LN_LAST_TICK] = 0.0;
	g_ln_i[id][LN_LAST_CLIP_SET] = 0;
	g_ln_i[id][LN_PENDING_LOADOUT] = -1;
	g_ln_i[id][LN_CREDIT_CHECKED] = 0;
	g_ln_i[id][LN_LIFE_KILLS] = 0;
	g_ln_f[id][LN_LIFE_DEALT] = 0.0;
}

// learning.rs Bot::learn_think: every think while alive, after the tasks ran: the episode with
// the enemy stays open, stale ones close; time with the enemy in sight and rounds fired go to
// the weapon table (the bot's and every bot's); the loadout bought is credited.
stock learning_think(id)
{
	new h = g_ln_i[id][LN_HANDLE];
	if (h == 0)
	{
		return;
	}
	new RcLearn:bot = RcLearn:h;
	new Float:now = g_now;
	if (g_ln_f[id][LN_LIFE_START] == 0.0)
	{
		g_ln_f[id][LN_LIFE_START] = now;
	}
	new Float:dt = g_ln_f[id][LN_LAST_TICK] > 0.0 ? fclamp(now - g_ln_f[id][LN_LAST_TICK], 0.0, LN_MAX_TICK) : 0.0;
	g_ln_f[id][LN_LAST_TICK] = now;
	new enemy = g_cb_i[id][CB_ENEMY];
	if (enemy != 0)
	{
		rl_ledger_engage(bot, now, enemy);
	}
	rl_ledger_tick(bot, now);

	// Bot::resolve: the enemy while its handle is still good.
	new bool:have = enemy != 0 && entref_alive(enemy, g_cb_i[id][CB_ENEMY_SERIAL]);
	new Float:me[3], Float:at[3], Float:enemy_dist = 0.0;
	world_origin(id, me);
	if (have)
	{
		ln_origin(enemy, at);
		enemy_dist = vec_distance(at, me);
	}
	new weapon = world_weapon(id);
	new bool:sees = (g_th_i[id][TH_CONDITIONS] & COND_SEE_ENEMY) != 0;
	new kind, kid;
	if (have && sees && learning_player_key(enemy, kind, kid))
	{
		// Dodging: the enemy's speed across the bot's view.
		new Float:to[3], Float:vel[3];
		vec_sub(at, me, to);
		to[2] = 0.0;
		vec_normalize(to, to);
		world_velocity(enemy, vel);
		new Float:sideways = floatabs(vel[0] * -to[1] + vel[1] * to[0]) / 290.0;
		rl_opp_exposure(enemy, dt, sideways);
	}
	if (have)
	{
		g_ln_f[id][LN_LAST_ENEMY_DIST] = enemy_dist;
		if (sees)
		{
			rl_ws_add_exposure(bot, weapon, enemy_dist, dt);
		}
	}
	// Rounds fired: the clip of the same gun going down (kung fu's clip is its power bar).
	new clip = world_clip(id);
	if (g_ln_i[id][LN_LAST_CLIP_SET] && have && g_ln_i[id][LN_LAST_CLIP_WEAPON] == weapon && weapon != 0)
	{
		new fired = g_ln_i[id][LN_LAST_CLIP] - clip;
		if (fired >= 1 && fired <= LN_MAX_SHOTS_PER_TICK)
		{
			rl_ws_add_shots(bot, weapon, g_ln_f[id][LN_LAST_ENEMY_DIST], float(fired));
		}
	}
	g_ln_i[id][LN_LAST_CLIP_SET] = 1;
	g_ln_i[id][LN_LAST_CLIP_WEAPON] = weapon;
	g_ln_i[id][LN_LAST_CLIP] = clip;
	ln_credit_loadout(id, now);
}

// learning.rs Bot::credit_loadout: early in a life, which loadout the game's inventory shows
// (only that is credited).
static stock ln_credit_loadout(id, Float:now)
{
	new pending = g_ln_i[id][LN_PENDING_LOADOUT];
	if (g_ln_i[id][LN_CREDIT_CHECKED] || pending < 0)
	{
		return;
	}
	if (now - g_ln_f[id][LN_LIFE_START] > RL_CREDIT_WINDOW)
	{
		g_ln_i[id][LN_CREDIT_CHECKED] = 1;
		return;
	}
	new n = world_inventory(id);
	new owned[RC_MAX_INVENTORY];
	for (new k = 0; k < n; k++)
	{
		owned[k] = world_inventory_field(id, k, TSINV_WEAPON);
	}
	new arm = rl_credited_loadout(pending, owned, n);
	if (arm >= 0)
	{
		g_ln_i[id][LN_CREDITED_LOADOUT] = arm;
		g_ln_i[id][LN_CREDIT_CHECKED] = 1;
	}
}

// learning.rs Bot::choose_loadout: the bandit's pick among the loadouts that fit the slots the
// bot has (what is free and what it carries, which the new loadout replaces), at random with
// weapon learning off. Its weapon ids (TS ids, RCBot's for 1-37) into ids; the count.
stock learning_choose_loadout(id, ids[], max)
{
	new h = g_ln_i[id][LN_HANDLE];
	if (h == 0)
	{
		return 0;
	}
	new carried = 0;
	for (new w = 1; w <= 37; w++)
	{
		if (botweapons_has(id, w))
		{
			carried += rl_weapon_slots(w);
		}
	}
	new free_slots = world_field(id, TSPS_FREESLOTS);
	new capacity = free_slots > 0 ? free_slots + carried : 100;
	new arm = rl_loadout_choose(RcLearn:h, capacity, config_is_on(CFG_DISABLE_WEAPON_LEARN));
	g_ln_i[id][LN_PENDING_LOADOUT] = arm;
	return rl_loadout_weapons(arm, ids, max);
}
