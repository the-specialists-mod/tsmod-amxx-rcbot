// rcbot/learning.inc: the bot's learners: glue to the rcbot_learn module.
// Owner: kungfu. Ports bot/learning.rs (BotLearning, Bot::ledger, learned_threat, waypoint_danger, weapon_stats, learned_weapon_score, blow_weapon, combat_event, learn_think, choose_loadout, credit_loadout, learn_on_death), think.rs's death learning (death_fitness, learned.on_death) and spawn (learned.on_spawn), manager.rs update_opponents / update_heatmap / refresh_player_keys / player_stunt, host bots.rs new_bot / save_bot / save_globals and lib.rs game_init's global load. Mapping of natives: crates/learn/src/natives.rs.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(learning). rl_heatmap_* move to rcbot_nav (navigate.inc).

#if defined _rcbot_learning_included
	#endinput
#endif
#define _rcbot_learning_included

// TODO(learning): host lib.rs game_init: rl_load_globals (team<n>.rld, tsweaps.rld, weaponstats.rwr, loadouts.rlo, players.rop). Called from plugin_init.
stock learning_load_globals()
{
}

// TODO(learning): host bots.rs save_globals: rl_save_globals.
stock learning_save_globals()
{
}

// TODO(learning): host bots.rs new_bot: rl_bot_load(profile id, name) into LN_HANDLE.
stock learning_bot_load(id)
{
	#pragma unused id
}

// TODO(learning): host bots.rs save_bot: rl_bot_save.
stock learning_bot_save(id)
{
	#pragma unused id
}

// TODO(learning): free the handle (rl_bot_free) when the bot leaves.
stock learning_bot_free(id)
{
	#pragma unused id
}

// TODO(learning): think.rs Think's death: death_fitness, learn_on_death (loadout reward, ledger), learned.on_death (rl_bot_on_death).
stock learning_on_death(id)
{
	#pragma unused id
}

// TODO(learning): think.rs spawn_init: learned.on_spawn (rl_bot_on_spawn).
stock learning_on_spawn(id)
{
	#pragma unused id
}

// TODO(learning): learning.rs Bot::learn_think (exposure, shots, ledger tick).
stock learning_think(id)
{
	#pragma unused id
}

// TODO(learning): learning.rs Bot::choose_loadout (the bandit's pick, rl_loadout_choose): RCBot ids into ids, the count.
stock learning_choose_loadout(id, ids[], max)
{
	#pragma unused id, ids, max
	return 0;
}

// TODO(learning): manager.rs combat_event for a damage: update_heatmap, update_opponents (rl_opp_hit_on_bot), every bot's Bot::combat_event (rl_ledger_record).
stock learning_damage_event(victim, attacker, Float:damage, hitgroup, wid)
{
	#pragma unused victim, attacker, damage, hitgroup, wid
}

// TODO(learning): manager.rs combat_event for a kill (KillEvent: tsx has no kill_scored forward; build it from ts_player_killed and the victim's last ts_damage).
stock learning_kill_event(victim, killer, wid, bool:headshot)
{
	#pragma unused victim, killer, wid, headshot
}

// TODO(learning): manager.rs player_stunt (rl_opp_stunt; tsfun's client_stunt).
stock learning_player_stunt(player)
{
	#pragma unused player
}

// TODO(learning): learning.rs Bot::learned_threat (rl_opp_threat).
stock Float:learning_learned_threat(id, player)
{
	#pragma unused id, player
	return 0.0;
}

// TODO(learning): learning.rs Bot::waypoint_danger (the heatmap, rcbot_nav).
stock Float:learning_waypoint_danger(id, waypoint)
{
	#pragma unused id, waypoint
	return 0.0;
}

// TODO(learning): learning.rs Bot::learned_weapon_score (rl_ws_score): false for none.
stock bool:learning_weapon_score(id, weapon, Float:dist, &Float:score)
{
	#pragma unused id, weapon, dist
	score = 0.0;
	return false;
}
