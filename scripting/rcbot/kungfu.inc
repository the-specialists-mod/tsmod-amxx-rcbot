// rcbot/kungfu.inc: the kung fu fighter (port addition with learned stances, reactions and combos; documented gameplay deviation, rcbot-gameplay-deviations).
// Owner: kungfu. Ports bot/kungfu.rs (Move, MOVES, Reaction, Stance, AttackKind, Combo, seed_combos, Fight, Answering, FightEnd, PerOpponent, KungFu new / reset_fight / opponent / fighting / save / load / combo_runs / combo_done / breed / lesson_weight / stance_estimate / answer_estimate / learn_stance / learn_answer / decay, is_ranged, kungfu_file_name, Bot::kf_power, kf_ready, kf_reaction_time, kungfu_fight, kf_trade, kf_settle, kf_combo_reward, kf_stance_reward, kf_watch, kf_choose_stance, kf_judge_reaction, kf_react, kf_pick_answer, kf_offence, kf_find_wall, kf_pick_combo, kf_run_combo, kf_close_in, kf_next_step, kf_airborne, kf_phase, kungfu_face), host bots.rs's .rkf load/save (botprofiles/<key>.rkf, FILE_KIND "kungfu", FILE_VERSION 2).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(kungfu). State: state.inc's kungfu section.

#if defined _rcbot_kungfu_included
	#endinput
#endif
#define _rcbot_kungfu_included

// kungfu.rs Move codes (a move's code in .rkf files is its place in MOVES).
enum
{
	KF_MOVE_PUNCH = 0,
	KF_MOVE_JAB,
	KF_MOVE_HEAVY_PUNCH,
	KF_MOVE_KICK,
	KF_MOVE_QUICK_KICK,
	KF_MOVE_SWEEP,
	KF_MOVE_BACK_KICK,
	KF_MOVE_JUMP_KICK,
	KF_MOVE_JUMP_PUNCH,
	KF_MOVE_FLY_KICK,
	KF_MOVE_WALL_KICK,
	KF_MOVE_WALL_FLY_KICK,
	KF_MOVES
};

// kungfu.rs Stance, Reaction (ANSWERS order, then not reacting), AttackKind, FightEnd.
enum { KF_STANCE_DEFENSIVE = 0, KF_STANCE_OFFENSIVE };
enum { KF_REACT_BLOCK = 0, KF_REACT_DUCK, KF_REACT_STEP_BACK, KF_REACT_SIDE_STEP, KF_NO_REACTION };
enum { KF_ATTACK_PUNCH = 0, KF_ATTACK_KICK, KF_ATTACK_LOW, KF_ATTACK_AIR, KF_ATTACK_BACK };
enum { KF_END_KILL = 0, KF_END_DEATH, KF_END_BROKEN };

// TODO(kungfu): kungfu.rs KungFu::new (a new bot: seed_combos, the arms at their priors).
stock kungfu_bot_init(id)
{
	#pragma unused id
}

// TODO(kungfu): think.rs spawn_init's kungfu part: kf_settle(Broken) then KungFu::reset_fight.
stock kungfu_spawn_init(id)
{
	#pragma unused id
}

// TODO(kungfu): host bots.rs new_bot: KungFu::load from botprofiles/<key>.rkf (rl_bot_key for the key).
stock kungfu_load(id)
{
	#pragma unused id
}

// TODO(kungfu): host bots.rs save_bot: KungFu::save to botprofiles/<key>.rkf (atomic: temp file renamed).
stock kungfu_save(id)
{
	#pragma unused id
}

// TODO(kungfu): kungfu.rs Bot::kungfu_fight: the fight against enemy at dist (stance, reactions, combos).
stock kungfu_fight(id, enemy, Float:dist)
{
	#pragma unused id, enemy, dist
}

// TODO(kungfu): kungfu.rs Bot::kf_settle: what is still open in the fight when it ends (KF_END_*).
stock kungfu_settle(id, fight_end)
{
	#pragma unused id, fight_end
}

// TODO(kungfu): kungfu.rs Bot::kungfu_face: where to look instead of the opponent.
stock bool:kungfu_face(id, Float:out[3])
{
	#pragma unused id
	vec_zero(out);
	return false;
}

// TODO(kungfu): kungfu.rs KungFu::fighting.
stock bool:kungfu_fighting(id, Float:gap)
{
	#pragma unused id, gap
	return false;
}

// TODO(kungfu): kungfu.rs KungFu::opponent: 0 none.
stock kungfu_opponent(id)
{
	#pragma unused id
	return 0;
}

// TODO(kungfu): kungfu.rs is_ranged (a gun, not a blade or kung fu).
stock bool:kungfu_is_ranged(game_id)
{
	#pragma unused game_id
	return false;
}
