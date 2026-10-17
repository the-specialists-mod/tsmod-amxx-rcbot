// rcbot/look.inc: where the bot looks.
// Owner: combat. Ports bot/look.rs (Bot::yaw_angle_to, dot_from_origin, in_view_cone, work_view_angles, set_view_angles, aim_point, aim_vector, turn_to; the free helpers are math.inc), mod.rs Bot::hurt_by / hurt's look part, think.rs teleported / spawn_init's look part.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: teleported / spawn_init are ported; the rest is TODO(look).

#if defined _rcbot_look_included
	#endinput
#endif
#define _rcbot_look_included

// think.rs Bot::teleported, look's part: the ladder angles belong to where it was.
stock look_teleported(id)
{
	g_lk_i[id][LK_LADDER_ANGLES_SET] = 0;
}

// think.rs Bot::spawn_init, look's part.
stock look_spawn_init(id)
{
	g_lk_f[id][LK_LAST_LOOK_TIME] = 0.0;
	g_lk_i[id][LK_LOOK_TASK] = LOOK_NEXT_WAYPOINT;
	vec_zero(g_lk_v[id][LK_HURT_ORIGIN]);
	g_lk_f[id][LK_HURT_TIME] = 0.0;
}

// TODO(look): look.rs Bot::work_view_angles (CBot::WorkViewAngles): the look task's target.
stock look_work_view_angles(id)
{
	#pragma unused id
}

// TODO(look): look.rs Bot::set_view_angles (CBot::SetViewAngles): turn toward origin, turn_speed at a time.
stock look_set_view_angles(id, const Float:origin[3])
{
	#pragma unused id, origin
}

// TODO(look): look.rs Bot::yaw_angle_to.
stock Float:look_yaw_angle_to(id, const Float:origin[3])
{
	#pragma unused id, origin
	return 0.0;
}

// TODO(look): look.rs Bot::dot_from_origin.
stock Float:look_dot_from_origin(id, const Float:origin[3])
{
	#pragma unused id, origin
	return 0.0;
}

// TODO(look): look.rs Bot::in_view_cone.
stock bool:look_in_view_cone(id, const Float:origin[3])
{
	#pragma unused id, origin
	return false;
}

// TODO(look): look.rs Bot::aim_point (design §4: chest, head for skilled bots with precise guns).
stock look_aim_point(id, enemy, Float:out[3])
{
	#pragma unused id, enemy
	vec_zero(out);
}

// TODO(look): look.rs Bot::aim_vector (GetAimVector with the bot's aim error).
stock look_aim_vector(id, enemy, Float:out[3])
{
	#pragma unused id, enemy
	vec_zero(out);
}

// TODO(look): look.rs Bot::turn_to (the aimed turn: lead, settle, max turn rate).
stock look_turn_to(id, const Float:target[3])
{
	#pragma unused id, target
}
