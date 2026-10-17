// rcbot/squad.inc: squads (CBotSquad) and the bots' squad behaviour.
// Owner: social. Ports bot/squad.rs (SquadForm, CombatType, Squad leader / is_leader / is_member / add_member / remove_member / num_members / toggle_fire_mode / can_shoot / is_stealth / is_prone / is_crouch / set_combat_type / change_formation / spread / change_spread / formation_position / formation_vector / update_angles / change_leader; Squads clear / get / find_by_leader / add_member / remove_member / remove / change_leader / return_all_to_formation / update_angles; SquadOrder; Bot::clear_squad / set_squad / squad / is_squad_leader / can_add_to_squad / add_to_squad / remove_my_squad / squad_order / squad_think / update_squad_conditions / follow_formation / squad_leader_think / squad_modes / squad_enemy_found / see_leader_again / say_in_position / task_follow_leader), manager.rs apply_squad_orders / make_squad, host bots.rs flush_squad_messages / remove_all_bot_squads, disconnect's change_leader.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(squad).

#if defined _rcbot_squad_included
	#endinput
#endif
#define _rcbot_squad_included

#define SQUAD_DEFAULT_SPREAD	50.0
#define RC_MAX_SQUADS			16
#define RC_MAX_SQUAD_MEMBERS	8

enum { FORM_NONE = 0, FORM_WEDGE, FORM_LINE, FORM_ECH_LEFT, FORM_ECH_RIGHT, FORM_COLUMN, FORM_VEE };
enum { COMBAT_NONE = 0, COMBAT_STEALTH, COMBAT_COMBAT, COMBAT_CROUCH, COMBAT_PRONE };

// Squads: slot = squad id (-1 none in g_sq_i[SQ_SQUAD]); members a stack (top at the end).
stock bool:g_squad_used[RC_MAX_SQUADS];
stock g_squad_leader[RC_MAX_SQUADS];
stock g_squad_leader_serial[RC_MAX_SQUADS];
stock g_squad_members[RC_MAX_SQUADS][RC_MAX_SQUAD_MEMBERS];
stock g_squad_member_count[RC_MAX_SQUADS];
stock g_squad_formation[RC_MAX_SQUADS];
stock Float:g_squad_spread[RC_MAX_SQUADS];
stock bool:g_squad_can_fire[RC_MAX_SQUADS];
stock g_squad_combat_type[RC_MAX_SQUADS];
stock Float:g_squad_leader_angle[RC_MAX_SQUADS][3];

// TODO(squad): manager.rs apply_squad_orders: hand the squads' orders to their bots.
stock squad_apply_orders()
{
}

// TODO(squad): Squads::update_angles: the squads follow their leaders' headings.
stock squad_update_angles()
{
}

// TODO(squad): host bots.rs flush_squad_messages (ClientPrint HUD_PRINTTALK to leaders).
stock squad_flush_messages()
{
}

// TODO(squad): Squads::clear (map change).
stock squad_clear()
{
}

// TODO(squad): host bots.rs disconnect: a leading player hands the squad on (Squads::change_leader).
stock squad_player_left(player)
{
	#pragma unused player
}

// TODO(squad): Bot::squad_think.
stock squad_think(id)
{
	#pragma unused id
}

// TODO(squad): Bot::can_add_to_squad.
stock bool:squad_can_add(id, leader)
{
	#pragma unused id, leader
	return false;
}

// TODO(squad): Bot::add_to_squad.
stock squad_add(id, leader, bool:leader_is_bot)
{
	#pragma unused id, leader, leader_is_bot
}

// TODO(squad): Bot::remove_my_squad.
stock bool:squad_remove_mine(id)
{
	#pragma unused id
	return false;
}

// TODO(squad): Bot::squad_modes.
stock squad_modes(id)
{
	#pragma unused id
}

// TODO(squad): Bot::follow_formation: true while following.
stock bool:squad_follow_formation(id)
{
	#pragma unused id
	return false;
}

// TODO(squad): Bot::update_squad_conditions.
stock squad_update_conditions(id)
{
	#pragma unused id
}

// TODO(squad): Bot::squad_enemy_found.
stock squad_enemy_found(id)
{
	#pragma unused id
}

// TODO(squad): Bot::see_leader_again.
stock squad_see_leader_again(id, bool:saw_leader, bool:need_to_follow)
{
	#pragma unused id, saw_leader, need_to_follow
}

// TODO(squad): Bot::task_follow_leader.
stock squad_task_follow_leader(id, &bool:failed)
{
	#pragma unused id, failed
}

// TODO(squad): host bots.rs remove_all_bot_squads (BotMenu_Func_Squad_RemoveAllBotSquads): how many.
stock squad_remove_all_bot_squads(team)
{
	#pragma unused team
	return 0;
}
