// rcbot/events.inc: what happens to the bot (BotEvent), reputation changes, boredom, leaving.
// Owner: social. Ports bot/events.rs (BotEvent, Bot::client_rep_id, client_rep, rep_of, increase_rep_of, decrease_rep_of, forget_greeting, team_number, having_fun, getting_bored, boredom_text, want_to_leave_game, event, see_teammate_die, see_player_die, see_enemy_kill, event_kill, event_died, chat_players, task_type_message), manager.rs death's event fan-out.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: forget_greeting is ported; the rest is TODO(events).

#if defined _rcbot_events_included
	#endinput
#endif
#define _rcbot_events_included

// events.rs Bot::forget_greeting: a player left; the bot may greet whoever comes next.
stock events_forget_greeting(id, player)
{
	if (player >= 1 && player <= 32)
	{
		g_ev_i[id][EV_SAID_GREETINGS_TO] &= ~(1 << (player - 1));
	}
}

// TODO(events): BotEvent::Leaving (it says goodbye).
stock events_leaving(id)
{
	#pragma unused id
}

// TODO(events): BotEvent::KillSelf.
stock events_kill_self(id)
{
	#pragma unused id
}

// TODO(events): BotEvent::Kill (event_kill).
stock events_kill(id, victim)
{
	#pragma unused id, victim
}

// TODO(events): BotEvent::Died (event_died): killer 0 is the world.
stock events_died(id, killer)
{
	#pragma unused id, killer
}

// TODO(events): BotEvent::SeeTeammateDie.
stock events_see_teammate_die(id, victim, killer)
{
	#pragma unused id, victim, killer
}

// TODO(events): BotEvent::SeePlayerDie.
stock events_see_player_die(id, a, b)
{
	#pragma unused id, a, b
}

// TODO(events): BotEvent::SeeTeammateKill (does nothing upstream).
stock events_see_teammate_kill(id)
{
	#pragma unused id
}

// TODO(events): BotEvent::SeeEnemyKill.
stock events_see_enemy_kill(id, killer, victim)
{
	#pragma unused id, killer, victim
}

// TODO(events): BotEvent::HearTeammateDie.
stock events_hear_teammate_die(id, victim)
{
	#pragma unused id, victim
}

// TODO(events): Bot::want_to_leave_game.
stock bool:events_want_to_leave_game(id)
{
	#pragma unused id
	return false;
}

// TODO(events): Bot::having_fun.
stock events_having_fun(id)
{
	#pragma unused id
}

// TODO(events): Bot::getting_bored.
stock events_getting_bored(id)
{
	#pragma unused id
}

// TODO(events): Bot::task_type_message (BOT_TASK_TYPE_MESSAGE: say the line once typed).
stock events_task_type_message(id, &bool:done, &bool:failed)
{
	#pragma unused id, done, failed
}
