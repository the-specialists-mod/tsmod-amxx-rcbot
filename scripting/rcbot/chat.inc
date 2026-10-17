// rcbot/chat.inc: the bots' chat: MegaHAL brains and bot_chat.ini through rcbot_megahal.
// Owner: social. Ports bot/events.rs Bot::bot_chat / reply_to_message / chat_players, manager.rs
// say's shared buffer (core's manager_say), host bots.rs wake_hal / save_bot's brain / greet /
// say_command, globals.cpp SetupBotChat (chat_load). Natives: include/rcbot_megahal.inc.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// The brain (hal_load, EV_HAL) is woken when the bot is made, saved with the bot (profiles
// only: botprofiles/<id>_hal.brn) and freed when it leaves. A line to say goes into the bot's
// chat_string (g_ev_chat_string) and a TYPE_MESSAGE task (events_task_type_message types it).

#if defined _rcbot_chat_included
	#endinput
#endif
#define _rcbot_chat_included

#define RC_SAY_LEN		192

new RcChat:g_chat;			// m_BotChat (chat_load in plugin_init), 0 none

// globals.cpp SetupBotChat: bot_chat.ini. From plugin_init.
stock chat_init()
{
	g_chat = chat_load("bot_chat.ini");
	if (g_chat == RcChat:0)
	{
		rc_log(TSLOG_INFO, "no bot_chat.ini: the bots have nothing to say");
	}
}

// host bots.rs wake_hal (PrepareHALBrainForPersonality, LoadHALBrainForPersonality): the
// keyword lists, the brain (a new one, saved, when the file is missing or damaged, then
// pretrained), with the profile's hal_*_file overrides. Profile 0 gets a brain in memory only.
stock chat_wake_hal(id)
{
	chat_free_hal(id);
	g_ev_i[id][EV_HAL] = _:hal_load(g_pr_i[id][PR_ID], g_pr_hal_file[id][PR_HAL_PRETRAIN], g_pr_hal_file[id][PR_HAL_AUX],
		g_pr_hal_file[id][PR_HAL_BAN], g_pr_hal_file[id][PR_HAL_SWAP]);
}

// host bots.rs save_bot (SaveHALBrainForPersonality): bots with a profile.
stock chat_save_hal(id)
{
	if (g_ev_i[id][EV_HAL] && g_pr_i[id][PR_ID] != 0)
	{
		hal_save(RcHal:g_ev_i[id][EV_HAL]);
	}
}

// The brain goes with the bot (saved before, by backend_save_bot).
stock chat_free_hal(id)
{
	if (g_ev_i[id][EV_HAL])
	{
		new RcHal:h = RcHal:g_ev_i[id][EV_HAL];
		hal_free(h);
		g_ev_i[id][EV_HAL] = 0;
	}
}

// host bots.rs greet (BotFunc_AddBot's greeting): a liked player by name, or (with a chance
// growing with the server's average reputation) everyone, which upstream's ENTINDEX(NULL) check
// stops (the draw is kept).
stock chat_greet(id)
{
	if (!config_is_on(CFG_CHATTING))
	{
		return;
	}
	new liked = rep_random_client(id, 1);
	new avg = rep_average_on_server(id);
	if (liked)
	{
		chat_bot_chat(id, RcChat_Greetings, liked, false);
	}
	else if (random_num(0, 10) < avg)
	{
		chat_bot_chat(id, RcChat_Greetings, 0, false);
	}
}

// events.rs Bot::bot_chat (CBot::BotChat(type, pChatEdict, bSayNow)): a line of the list for the
// player talked to (to: a player index; 0 nobody; -1 an entity that isn't a client, the world),
// by the bot's reputation of them (the server's average for a non-client, the "bad -1" list for
// nobody), typed in a message task (a priority one when say_now). Greetings go once to each
// player.
stock chat_bot_chat(id, RcChatKind:kind, to, bool:say_now)
{
	if (g_chat == RcChat:0 || !config_is_on(CFG_CHATTING) || tasks_has(id, TASK_TYPE_MESSAGE))
	{
		return;
	}
	new bool:to_is_client = to >= 1 && to <= MAX_PLAYERS && world_userid(to) != 0;
	new to_client = to_is_client && rep_client_id(to) != NO_CLIENT ? to : 0;
	new array_num = -1;
	if (to != 0)
	{
		array_num = chat_rep_array(to_client ? rep_of(id, to_client) : rep_average_on_server(id));
	}
	if (kind == RcChat_Greetings)
	{
		// ENTINDEX(pChatEdict) - 1: nobody is -1.
		if (to < 1 || to > MAX_PLAYERS)
		{
			return;
		}
		new bit = 1 << (to - 1);
		if (g_ev_i[id][EV_SAID_GREETINGS_TO] & bit)
		{
			return;
		}
		g_ev_i[id][EV_SAID_GREETINGS_TO] |= bit;
		// client_rep(to_client): BOT_MID_REP without one.
		array_num = chat_rep_array(to_client ? rep_of(id, to_client) : BOT_MID_REP);
	}
	new name[RC_NAME_LEN], map[64];
	if (to_is_client)
	{
		get_user_name(to, name, charsmax(name));
	}
	get_mapname(map, charsmax(map));
	chat_context(name, map);
	chat_players(id);
	new line[RC_ACTION_TEXT], bool:team_only;
	if (chat_pick(g_chat, kind, array_num, line, charsmax(line), team_only) == 0)
	{
		return;
	}
	copy(g_ev_chat_string[id], charsmax(g_ev_chat_string[]), line);
	if (say_now)
	{
		tasks_add_priority(id, TASK_TYPE_MESSAGE, 0, 0, 0, team_only ? 1 : 0, 0.0, VEC_ZERO);
	}
	else
	{
		tasks_add(id, TASK_TYPE_MESSAGE, 0, 0, 0, team_only ? 1 : 0, 0.0, VEC_ZERO);
	}
}

// events.rs Bot::chat_players: everyone but the bot, with the bot's reputation of them and
// whether they are its enemies, for %r / %g / %b / %e.
static chat_players(id)
{
	new name[RC_NAME_LEN];
	for (new p = 1; p <= g_maxclients; p++)
	{
		if (p == id || world_userid(p) == 0)
		{
			continue;
		}
		get_user_name(p, name, charsmax(name));
		chat_context_player(name, rep_of(id, p), combat_is_enemy(id, p));
	}
}

// events.rs Bot::reply_to_message (CBot::ReplyToMessage with BotChatReply): a MegaHAL reply,
// typed in a priority message task. message is the buffer every bot sees in turn (hal_bot_reply
// lower-cases it and puts %n for the bot's name, as upstream's shared buffer).
stock chat_reply_to_message(id, message[], sender, bool:sender_is_bot, bool:team_only)
{
	if (tasks_has(id, TASK_TYPE_MESSAGE) || !g_ev_i[id][EV_HAL])
	{
		return;
	}
	new num_clients = 0, name[RC_NAME_LEN];
	for (new p = 1; p <= g_maxclients; p++)
	{
		if (world_userid(p) != 0)
		{
			get_user_name(p, name, charsmax(name));
			if (name[0])
			{
				num_clients++;
			}
		}
	}
	new sender_rep = rep_client_rep(id, rep_client_id(sender));
	new flags = 0;
	if (sender == id)
	{
		flags |= HAL_SENDER_SELF;
	}
	if (sender_is_bot)
	{
		flags |= HAL_SENDER_BOT;
	}
	if (config_is_on(CFG_CHATTING))
	{
		flags |= HAL_CHATTING;
	}
	if (config_is_on(CFG_REAL_MODE))
	{
		flags |= HAL_REAL_MODE;
	}
	if (config_is_on(CFG_CHAT_DONT_LEARN))
	{
		flags |= HAL_DONT_LEARN;
	}
	get_user_name(sender, name, charsmax(name));
	new reply[RC_ACTION_TEXT];
	if (hal_bot_reply(RcHal:g_ev_i[id][EV_HAL], message, g_bot_name[id], name, flags, sender_rep, num_clients,
		g_cfg_i[CFG_CHAT_REPLY_PERCENT], reply, charsmax(reply)) > 0)
	{
		copy(g_ev_chat_string[id], charsmax(g_ev_chat_string[]), reply);
		tasks_add_priority(id, TASK_TYPE_MESSAGE, 0, sender, world_userid(sender), team_only ? 1 : 0, 0.0, VEC_ZERO);
	}
}

// host bots.rs say_command (ClientCommand's say / say_team): say_team "form up" makes a squad;
// anything else may get MegaHAL replies (not from bots unless chat_reply_to_bots). A message said
// in several arguments counts only its last non-empty one (upstream's szTemp is never set, kept).
// The game still sees the command (PLUGIN_CONTINUE).
stock chat_say_command(player, bool:team_only)
{
	if (player < 1 || player > MAX_PLAYERS || world_userid(player) == 0)
	{
		return PLUGIN_CONTINUE;
	}
	new arg1[RC_SAY_LEN], arg2[RC_SAY_LEN];
	read_argv(1, arg1, charsmax(arg1));
	read_argv(2, arg2, charsmax(arg2));
	if (team_only && equal(arg1, "form", 4) && (equal(arg1[4], " up", 3) || equal(arg2, "up", 2)))
	{
		manager_make_squad(player);
		squad_flush_messages();
		return PLUGIN_CONTINUE;
	}
	if (is_rcbot(player) && !config_is_on(CFG_CHAT_REPLY_TO_BOTS))
	{
		return PLUGIN_CONTINUE;
	}
	new message[RC_SAY_LEN];
	if (arg2[0])
	{
		new argc = read_argc(), tmp[RC_SAY_LEN];
		for (new k = argc - 1; k >= 1; k--)
		{
			read_argv(k, tmp, charsmax(tmp));
			if (tmp[0])
			{
				copy(message, charsmax(message), tmp);
				break;
			}
		}
	}
	else
	{
		copy(message, charsmax(message), arg1);
	}
	if (!message[0])
	{
		return PLUGIN_CONTINUE;
	}
	manager_say(player, message, team_only);
	return PLUGIN_CONTINUE;
}
