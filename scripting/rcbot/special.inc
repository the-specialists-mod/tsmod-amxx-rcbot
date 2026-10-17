// rcbot/special.inc: lifts, buttons, pushables, human towers, touches, special waypoint tasks.
// Owner: think. Ports bot/special.rs (MasterEntity is world.inc's g_master_*; is_button, best_pushable_distance, desired_pushable_vector, acceptable_pushable_vector, find_nearest_entity, player_near_vector, master_find_button, find_nearest_button, Bot::player_standing_on_me, standing_on_player, check_lift, touched_pushable_waypoint, next_waypoint_tasks, touched_human_tower, touch, task_push_pushable, task_wait_for_lift, task_wait_for_entity, task_human_tower, task_wait_for_bot_at_wpt, task_face_edict, task_throw_grenade, OtherBot).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(special).

#if defined _rcbot_special_included
	#endinput
#endif
#define _rcbot_special_included

// TODO(special): special.rs Bot::touch (DispatchTouch for a living bot): true if handled.
stock bool:special_touch(id, other)
{
	#pragma unused id, other
	return false;
}

// TODO(special): special.rs Bot::player_standing_on_me: the player, 0 none.
stock special_player_standing_on_me(id)
{
	#pragma unused id
	return 0;
}

// TODO(special): special.rs Bot::standing_on_player: the player, 0 none.
stock special_standing_on_player(id)
{
	#pragma unused id
	return 0;
}

// TODO(special): special.rs Bot::check_lift.
stock bool:special_check_lift(id, const Float:from[3], const Float:to[3])
{
	#pragma unused id, from, to
	return false;
}

// TODO(special): special.rs Bot::touched_pushable_waypoint.
stock special_touched_pushable_waypoint(id, const Float:wpt_origin[3])
{
	#pragma unused id, wpt_origin
}

// TODO(special): special.rs Bot::next_waypoint_tasks.
stock special_next_waypoint_tasks(id, prev_flags)
{
	#pragma unused id, prev_flags
}

// TODO(special): special.rs Bot::touched_human_tower.
stock bool:special_touched_human_tower(id, const Float:wpt_origin[3], Float:distance)
{
	#pragma unused id, wpt_origin, distance
	return false;
}

// TODO(special): special.rs find_nearest_button: the button and where to press it, 0 none.
stock special_find_nearest_button(const Float:origin[3], door, Float:press_at[3])
{
	#pragma unused origin, door
	vec_zero(press_at);
	return 0;
}

// TODO(special): special.rs find_nearest_entity (by classname list), 0 none.
stock special_find_nearest_entity(const classnames[][], num, const Float:origin[3], Float:range, bool:visible)
{
	#pragma unused classnames, num, origin, range, visible
	return 0;
}

// TODO(special): special.rs player_near_vector, 0 none.
stock special_player_near_vector(const Float:origin[3], Float:range)
{
	#pragma unused origin, range
	return 0;
}

// TODO(special): special.rs Bot::task_push_pushable (task slot 0).
stock special_task_push_pushable(id, &bool:done, &bool:failed)
{
	#pragma unused id, done, failed
}

// TODO(special): special.rs Bot::task_wait_for_lift.
stock special_task_wait_for_lift(id, &bool:done)
{
	#pragma unused id, done
}

// TODO(special): special.rs Bot::task_wait_for_entity.
stock special_task_wait_for_entity(id, &bool:done, &bool:failed)
{
	#pragma unused id, done, failed
}

// TODO(special): special.rs Bot::task_human_tower.
stock special_task_human_tower(id, &bool:done)
{
	#pragma unused id, done
}

// TODO(special): special.rs Bot::task_wait_for_bot_at_wpt.
stock special_task_wait_for_bot_at_wpt(id, &bool:done, &bool:failed)
{
	#pragma unused id, done, failed
}

// TODO(special): special.rs Bot::task_face_edict.
stock special_task_face_edict(id, &bool:done, &bool:failed)
{
	#pragma unused id, done, failed
}

// TODO(special): special.rs Bot::task_throw_grenade.
stock special_task_throw_grenade(id, &bool:done, &bool:failed)
{
	#pragma unused id, done, failed
}
