// rcbot/goals.inc: where the bot goes next (port addition, design §4).
// Owner: think. Ports bot/goals.rs (Explore is navigate.inc's; Goal, score, Bot::weapon_value, powerup_value, item_value, danger_near, choose_goal, hold_spot, claim_near, fight_readiness, predict_enemy, claim_item_factor, regroup_point; FORGET_VISIT, NEAR_GOAL, WEAPON_SLOTS, ROAM_SCORE).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(goals).

#if defined _rcbot_goals_included
	#endinput
#endif
#define _rcbot_goals_included

// goals.rs ROAM_SCORE: a goal must score this much to beat roaming.
#define GOALS_ROAM_SCORE	10.0

// goals.rs Goal.
enum
{
	GOAL_ITEM = 0,		// arg: the item (EntRef)
	GOAL_HUNT,			// vec + time: an enemy last seen there and then
	GOAL_INVESTIGATE,	// vec: a sound or a teammate's sighting
	GOAL_REGROUP,		// vec: the teammate to join
	GOAL_HOLD,			// arg: the waypoint to hold
	GOAL_ROAM
};

// TODO(goals): goals.rs score: a goal's value over the way there.
stock Float:goals_score(Float:value, Float:distance, Float:danger)
{
	#pragma unused value, distance, danger
	return 0.0;
}

// TODO(goals): goals.rs Bot::weapon_value.
stock Float:goals_weapon_value(id, weapon)
{
	#pragma unused id, weapon
	return 0.0;
}

// TODO(goals): goals.rs Bot::powerup_value.
stock Float:goals_powerup_value(id, bits)
{
	#pragma unused id, bits
	return 0.0;
}

// TODO(goals): goals.rs Bot::item_value (a KnownItem, items.inc slot).
stock Float:goals_item_value(id, item_slot)
{
	#pragma unused id, item_slot
	return 0.0;
}

// TODO(goals): goals.rs Bot::danger_near.
stock Float:goals_danger_near(id, const Float:at[3])
{
	#pragma unused id, at
	return 0.0;
}

// TODO(goals): goals.rs Bot::choose_goal: GOAL_*, its argument/vector and score out.
stock goals_choose(id, &arg, Float:vec[3], &Float:score)
{
	#pragma unused id
	arg = 0;
	vec_zero(vec);
	score = 0.0;
	return GOAL_ROAM;
}

// TODO(goals): goals.rs Bot::hold_spot: the waypoint and how long, -1 none.
stock goals_hold_spot(id, &Float:secs)
{
	#pragma unused id
	secs = 0.0;
	return -1;
}

// TODO(goals): goals.rs Bot::fight_readiness.
stock Float:goals_fight_readiness(id)
{
	#pragma unused id
	return 1.0;
}

// TODO(goals): goals.rs Bot::predict_enemy.
stock bool:goals_predict_enemy(id, Float:out[3])
{
	#pragma unused id
	vec_zero(out);
	return false;
}

// TODO(goals): goals.rs Bot::claim_item_factor.
stock Float:goals_claim_item_factor(id, item, item_serial)
{
	#pragma unused id, item, item_serial
	return 1.0;
}

// TODO(goals): goals.rs Bot::regroup_point.
stock bool:goals_regroup_point(id, Float:out[3])
{
	#pragma unused id
	vec_zero(out);
	return false;
}
