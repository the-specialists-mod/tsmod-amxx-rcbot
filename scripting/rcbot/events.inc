// rcbot/events.inc: what happens to the bot (BotEvent), reputation changes, boredom, leaving.
// Owner: social. Ports bot/events.rs (BotEvent, Bot::forget_greeting, team_number, having_fun,
// getting_bored, boredom_text, want_to_leave_game, event, see_teammate_die, see_player_die,
// see_enemy_kill, event_kill, event_died, task_type_message), manager.rs death's event fan-out
// (core's manager_death calls these). The reputation helpers (client_rep_id, client_rep, rep_of,
// increase_rep_of, decrease_rep_of) are rep.inc's; bot_chat, reply_to_message and chat_players
// are chat.inc's; hear_sound is hearing.inc's.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Players are passed by index (an EntRef of the same frame): one resolves while its slot is
// connected (world_userid). Killer 0 is the world, which has no reputation id.

#if defined _rcbot_events_included
	#endinput
#endif
#define _rcbot_events_included

new g_pcvar_timelimit;

// events.rs Bot::forget_greeting: a player left; the bot may greet whoever comes next.
stock events_forget_greeting(id, player)
{
	if (player >= 1 && player <= 32)
	{
		g_ev_i[id][EV_SAID_GREETINGS_TO] &= ~(1 << (player - 1));
	}
}

// events.rs Bot::team_number (GetTeam()): the rules team.
stock events_team_number(id)
{
	return world_team(id);
}

// events.rs Bot::having_fun (havingFun): boredom (a u8) up, saturating at 255.
stock events_having_fun(id)
{
	if (g_ev_i[id][EV_BOREDOM] < 255)
	{
		g_ev_i[id][EV_BOREDOM]++;
	}
}

// events.rs Bot::getting_bored (gettingBored).
stock events_getting_bored(id)
{
	if (g_ev_i[id][EV_BOREDOM] > 0)
	{
		g_ev_i[id][EV_BOREDOM]--;
	}
}

// events.rs Bot::boredom_text (printBoredoms; the "percent" is the fraction, as upstream).
stock events_boredom_text(id, out[], len)
{
	new b = g_ev_i[id][EV_BOREDOM];
	formatex(out, len, "%s's boredom = %d (%.1f percent)", g_bot_name[id], b, float(b) / 255.0);
}

// events.rs Bot::want_to_leave_game (WantToLeaveGame): (255 - boredom) / 255 times the share of
// mp_timelimit the bot has been on the server, over a random 0.75-1. With mp_timelimit 0 the
// share is infinite (as upstream's float division).
stock bool:events_want_to_leave_game(id)
{
	if (!g_pcvar_timelimit)
	{
		g_pcvar_timelimit = get_cvar_pointer("mp_timelimit");
	}
	new Float:limit = g_pcvar_timelimit ? get_pcvar_float(g_pcvar_timelimit) : 0.0;
	new Float:boredom = float(255 - g_ev_i[id][EV_BOREDOM]) / 255.0;
	new Float:long_game = 1.0 - (limit * 60.0 - (g_now - g_ev_f[id][EV_JOIN_SERVER_TIME])) / (limit * 60.0);
	return boredom * long_game > random_float(0.75, 1.0);
}

// A player index that resolves this frame (Bot::resolve of a player's EntRef).
static bool:events_resolves(p)
{
	return p >= 1 && p <= MAX_PLAYERS && world_userid(p) != 0;
}

// The bot attacks k: enemy = k, a priority NORMAL_ATTACK with a new schedule. enemy is combat's
// state; the Rust bot sets it from events.rs.
static events_attack(id, k)
{
	g_cb_i[id][CB_ENEMY] = k;
	g_cb_i[id][CB_ENEMY_SERIAL] = world_userid(k);
	tasks_add_priority(id, TASK_NORMAL_ATTACK, tasks_new_schedule_id(id), k, world_userid(k), 0, 0.0, VEC_ZERO);
}

// BotEvent::Leaving: it says goodbye.
stock events_leaving(id)
{
	chat_bot_chat(id, RcChat_LeaveGame, 0, true);
}

// BotEvent::HearTeammateDie: without an enemy, the bot turns to the victim.
stock events_hear_teammate_die(id, victim)
{
	if (g_cb_i[id][CB_ENEMY] == 0)
	{
		tasks_add_priority(id, TASK_FACE_EDICT, 0, victim, world_userid(victim), 0, 1.0, VEC_ZERO);
	}
}

// BotEvent::KillSelf: maybe a laugh (the server likes it) or a moan.
stock events_kill_self(id)
{
	if (random_num(0, 100) < g_cfg_i[CFG_CHAT_PERCENT])
	{
		if (rep_average_on_server(id) >= BOT_MID_REP)
		{
			chat_bot_chat(id, RcChat_Laugh, 0, false);
			events_having_fun(id);
		}
		else
		{
			chat_bot_chat(id, RcChat_Killed, 0, false);
			events_getting_bored(id);
		}
	}
}

// BotEvent::SeeTeammateDie (BotEvent_SeeTeammateDie): attack a visible enemy killer; a
// teammate's killer may lose reputation if the bot liked the victim; an unseen killer is
// remembered.
stock events_see_teammate_die(id, victim, killer)
{
	if (!events_resolves(killer))
	{
		return;
	}
	if (combat_is_visible(id, killer))
	{
		if (combat_is_enemy(id, killer))
		{
			events_attack(id, killer);
		}
		else if (rep_of(id, victim) > BOT_MID_REP)
		{
			if (rep_decrease_roll(rep_of(id, killer), 0.0, 0.0))
			{
				rep_decrease(id, killer);
			}
		}
	}
	else
	{
		new Float:ko[3], Float:me[3];
		world_player_center(killer, ko);
		world_origin(id, me);
		remember_add(id, ko, killer, world_userid(killer), REMEMBER_LOST_ENEMY, me);
	}
}

// BotEvent::SeePlayerDie (BotEvent_SeePlayerDie, pInfo a, pExtInfo b): someone the bot dislikes
// died: whoever did it gains (or, for a liked victim, loses) reputation, and the bot laughs.
stock events_see_player_die(id, a, b)
{
	if (b <= 0 || rep_client_id(a) == NO_CLIENT || rep_client_id(b) == NO_CLIENT)
	{
		return;
	}
	new rep = rep_of(id, b);
	new Float:dist = -1.0;
	if (events_resolves(a) && events_resolves(b))
	{
		new Float:x[3], Float:y[3];
		world_origin(a, x);
		world_origin(b, y);
		dist = vec_distance(x, y);
	}
	new Float:skill = float(g_pr_i[id][PR_SKILL]);
	new victim_rep = rep_of(id, a);
	if (victim_rep < BOT_MID_REP)
	{
		if (rep_increase_roll(rep, dist, skill))
		{
			rep_increase(id, b);
			chat_bot_chat(id, RcChat_Laugh, a, false);
			events_having_fun(id);
		}
	}
	else if (victim_rep > BOT_MID_REP && rep_decrease_roll(rep, dist, skill))
	{
		rep_decrease(id, b);
		chat_bot_chat(id, RcChat_Laugh, a, false);
		events_having_fun(id);
	}
}

// BotEvent::SeeTeammateKill: does nothing upstream.
stock events_see_teammate_kill(id)
{
	#pragma unused id
}

// BotEvent::SeeEnemyKill (BotEvent_SeeEnemyKill): attack the killer; if the bot liked the
// killer, the victim may lose reputation (upstream's test, kept).
stock events_see_enemy_kill(id, killer, victim)
{
	if (events_resolves(killer) && combat_is_enemy(id, killer))
	{
		events_attack(id, killer);
	}
	if (rep_client_id(killer) == NO_CLIENT || rep_client_id(victim) == NO_CLIENT)
	{
		return;
	}
	new rep = rep_of(id, victim);
	if (rep_of(id, killer) > BOT_MID_REP && rep_decrease_roll(rep, 0.0, 0.0))
	{
		rep_decrease(id, victim);
		events_getting_bored(id);
	}
}

// BotEvent::Kill (event_kill): chat about the kill (else hold "say" messages 10-20 s); a disliked
// victim may gain reputation.
stock events_kill(id, victim)
{
	new Float:now = g_now;
	if (random_num(0, 100) < g_cfg_i[CFG_CHAT_PERCENT])
	{
		chat_bot_chat(id, RcChat_Kills, victim, false);
		events_having_fun(id);
	}
	else if (g_sq_f[id][SQ_NEXT_USE_SAY_MESSAGE] < now)
	{
		g_sq_f[id][SQ_NEXT_USE_SAY_MESSAGE] = now + random_float(10.0, 20.0);
	}
	if (rep_client_id(victim) != NO_CLIENT && rep_of(id, victim) < BOT_MID_REP)
	{
		new rep = rep_of(id, victim);
		new Float:dist = -1.0;
		if (events_resolves(victim))
		{
			new Float:v[3], Float:me[3];
			world_origin(victim, v);
			world_origin(id, me);
			dist = vec_distance(v, me);
		}
		if (rep_increase_roll(rep, dist, float(g_pr_i[id][PR_SKILL])))
		{
			rep_increase(id, victim);
		}
	}
}

// BotEvent::Died (event_died): chat about it; the killer loses reputation (always a teammate:
// the rules team, as TS 3.0 doesn't set pev->team). killer 0 is the world.
stock events_died(id, killer)
{
	events_getting_bored(id);
	if (killer < 0)
	{
		return;
	}
	if (random_num(0, 100) < g_cfg_i[CFG_CHAT_PERCENT])
	{
		// The world (0) is someone talked to that isn't a client: -1 for chat_bot_chat.
		chat_bot_chat(id, RcChat_Killed, killer == 0 ? -1 : killer, false);
	}
	if (rep_client_id(killer) == NO_CLIENT)
	{
		return;
	}
	new bool:resolved = events_resolves(killer);
	new killer_team = resolved ? world_team(killer) : 0;
	if (killer_team != 0 && killer_team == world_team(id))
	{
		rep_decrease(id, killer);
	}
	else if (killer != id)
	{
		new rep = rep_of(id, killer);
		new Float:dist = -1.0;
		if (resolved)
		{
			new Float:k[3], Float:me[3];
			world_origin(killer, k);
			world_origin(id, me);
			dist = vec_distance(k, me);
		}
		if (rep_decrease_roll(rep, dist, float(g_pr_i[id][PR_SKILL])))
		{
			rep_decrease(id, killer);
		}
	}
}

// Bot::task_type_message (BOT_TASK_TYPE_MESSAGE, the current task): stand still "typing" (the
// line's length over the typing speed), then say it, humanised (say_team when the task's int
// is 1). The task's float is tasks' state; the Rust bot sets it here.
stock events_task_type_message(id, &bool:done, &bool:failed)
{
	new Float:now = g_now;
	g_lk_i[id][LK_LOOK_TASK] = LOOK_NONE;
	movement_stop_moving(id);
	if (g_tk_count[id] == 0)
	{
		failed = true;
		return;
	}
	new Float:t = g_tk_f[id][0][TK_FLOAT];
	if (t != 0.0)
	{
		if (t <= now)
		{
			new text[RC_ACTION_TEXT], line[RC_ACTION_TEXT];
			copy(text, charsmax(text), g_ev_chat_string[id]);
			g_ev_chat_string[id][0] = 0;
			chat_humanize(text, charsmax(text));
			formatex(line, charsmax(line), "%s ^"%s^"", g_tk_i[id][0][TK_INT] == 1 ? "say_team" : "say", text);
			bot_action_command(id, line);
			done = true;
		}
	}
	else if (g_ev_chat_string[id][0])
	{
		g_tk_f[id][0][TK_FLOAT] = now + chat_typing_time(g_ev_chat_string[id]);
	}
	else
	{
		failed = true;
	}
}
