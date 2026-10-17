// rcbot/chat.inc: the bots' chat: MegaHAL brains and bot_chat.ini through rcbot_megahal.
// Owner: social. Ports bot/events.rs Bot::bot_chat / reply_to_message / chat_players, host bots.rs wake_hal / save_bot's brain / greet / say_command, globals.cpp SetupBotChat (chat_load). Natives: include/rcbot_megahal.inc.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: stubs, TODO(chat).

#if defined _rcbot_chat_included
	#endinput
#endif
#define _rcbot_chat_included

stock RcChat:g_chat;			// m_BotChat (chat_load in plugin_init), 0 none

// TODO(chat): globals.cpp SetupBotChat: chat_load("bot_chat.ini") into g_chat. Called from plugin_init.
stock chat_init()
{
}

// TODO(chat): host bots.rs wake_hal: hal_load(profile id, the profile's hal_* files) into EV_HAL.
stock chat_wake_hal(id)
{
	#pragma unused id
}

// TODO(chat): host bots.rs save_bot: hal_save (profiles only).
stock chat_save_hal(id)
{
	#pragma unused id
}

// TODO(chat): free the brain (hal_free) when the bot leaves.
stock chat_free_hal(id)
{
	#pragma unused id
}

// TODO(chat): host bots.rs greet (BotFunc_AddBot's greeting), if CFG_CHATTING.
stock chat_greet(id)
{
	#pragma unused id
}

// TODO(chat): events.rs Bot::bot_chat (chat_pick into a TYPE_MESSAGE task, or say now).
stock chat_bot_chat(id, RcChatKind:kind, to, bool:say_now)
{
	#pragma unused id, kind, to, say_now
}

// TODO(chat): events.rs Bot::reply_to_message (hal_bot_reply on the shared buffer).
stock chat_reply_to_message(id, message[], sender, bool:sender_is_bot, bool:team_only)
{
	#pragma unused id, message, sender, sender_is_bot, team_only
}

// TODO(chat): host bots.rs say_command (say / say_team: "form up" makes a squad, else replies). Returns PLUGIN_CONTINUE.
stock chat_say_command(player, bool:team_only)
{
	#pragma unused player, team_only
	return PLUGIN_CONTINUE;
}
