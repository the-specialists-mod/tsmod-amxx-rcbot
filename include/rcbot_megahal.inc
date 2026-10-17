// rcbot_megahal.inc: RCBot's MegaHAL brains and bot_chat.ini for AMX Mod X plugins.
// tsmod-amxx-rcbot (https://github.com/the-specialists-mod/tsmod-amxx-rcbot),
// GPL-3.0-or-later with the AMX Mod X and RCBot linking exceptions.
//
// Files are relative to RCBot's data directory, <gamedir>/tsmod/rcbot/ (the folder the standalone
// tsmod_rcbot plugin uses, so both share brains byte for byte). Paths with ".." or absolute
// paths are refused. Handles die at map change (AMX Mod X reloads the plugins): save in
// plugin_end. An invalid handle is a native error.

#if defined _rcbot_megahal_included
  #endinput
#endif
#define _rcbot_megahal_included

#pragma reqlib rcbot_megahal
#if !defined AMXMODX_NOAUTOLOAD
  #pragma loadlib rcbot_megahal
#endif

// ---- MegaHAL brains ----

/**
 * Wakes a profile's brain up, as tsmod_rcbot does (PrepareHALBrainForPersonality and
 * LoadHALBrainForPersonality): reads the keyword lists, then botprofiles/<id>_hal.brn; when that
 * is missing or damaged, writes an empty brain there and pretrains it from the training text.
 * Profile 0 gets a brain in memory only, pretrained, never saved.
 *
 * Default file names (upstream's, note the missing separator): botprofiles<id>_hal.trn, .aux,
 * .ban, .swp. A non-empty override is the profile's hal_pretrain_file (botprofiles/<file>),
 * hal_aux_file, hal_ban_file or hal_swap_file (botprofiles<file>).
 *
 * @return          The brain's handle (never 0).
 */
native RcHal:hal_load(profile_id, const pretrain[] = "", const aux[] = "", const ban[] = "", const swap[] = "");

/**
 * Saves the brain to botprofiles/<id>_hal.brn (atomically: a temporary file renamed over it).
 *
 * @return          true if written; false for profile 0 or a write error (logged).
 */
native bool:hal_save(RcHal:hal);

/**
 * Frees a brain and sets the variable to 0.
 *
 * @param save      Save it first (hal_save).
 */
native bool:hal_free(&RcHal:hal, bool:save = false);

/**
 * Learns a line, as a reply learns what it heard (lower-cased).
 *
 * @return          The number of words learnt (punctuation and spaces count as MegaHAL words).
 */
native hal_learn(RcHal:hal, const text[]);

/**
 * MegaHAL's reply to a line, without learning it.
 *
 * @return          Characters written to out (0: no reply).
 */
native hal_reply(RcHal:hal, const text[], out[], len);

// hal_bot_reply flags.
#define HAL_SENDER_SELF  1   // the bot itself said it (no reply)
#define HAL_SENDER_BOT   2   // a bot said it (bots don't learn from bots)
#define HAL_CHATTING     4   // BOT_CONFIG_CHATTING: without it, no reply
#define HAL_REAL_MODE    8   // BOT_CONFIG_REAL_MODE: replies also depend on the reputation
#define HAL_DONT_LEARN   16  // BOT_CONFIG_CHAT_DONT_LEARN

/**
 * RCBot's BotChatReply for one bot: maybe replies (by reply_percent, the reputation in real
 * mode, always when the bot's name is in the message or there are two players), then learns the
 * message unless HAL_DONT_LEARN or HAL_SENDER_BOT.
 *
 * The message is changed in place, as upstream does with its shared buffer: lower-cased, with
 * the bot's name (without clan tags) replaced by %n. Pass the same buffer to each bot in turn.
 *
 * @param sender_rep    The bot's reputation of the sender (0-10).
 * @param num_clients   Players on the server.
 * @param reply_percent bot_chat_reply_percent (33 by default).
 * @return              Characters of the reply written to out (0: no reply).
 */
native hal_bot_reply(RcHal:hal, message[], const bot_name[], const sender_name[], flags, sender_rep, num_clients, reply_percent, out[], len);

/**
 * The brain's dictionary size (the <ERROR> and <FIN> symbols included: 2 when empty).
 */
native hal_words(RcHal:hal);

/**
 * The profile id the brain was loaded for.
 */
native hal_profile(RcHal:hal);

/**
 * Seeds the module's random numbers (replies, chat picks, typos), for reproducible runs. They
 * start seeded from the clock. Not the engine's RANDOM_LONG sequence.
 */
native hal_seed(seed);

// ---- bot_chat.ini ----

// The chat lists (eBotChatType). The first six have three lists by reputation (array_num 0
// unfriendly, 1 neutral, 2 friendly: chat_rep_array); idle and help have one.
enum RcChatKind
{
	RcChat_Laugh = 0,
	RcChat_Thanks,
	RcChat_Greetings,
	RcChat_Killed,
	RcChat_Kills,
	RcChat_LeaveGame,
	RcChat_Idle,
	RcChat_Help
};

/**
 * Reads a chat file (CBotGlobals::SetupBotChat).
 *
 * @return          Its handle, 0 if it can't be read.
 */
native RcChat:chat_load(const file[] = "bot_chat.ini");

/**
 * Frees chat lines and sets the variable to 0.
 */
native bool:chat_free(&RcChat:chat);

/**
 * The number of lines of a list. array_num -1 (talking to nobody) reads the previous kind's
 * friendly list, as upstream does; for laugh that is none (-1).
 */
native chat_count(RcChat:chat, RcChatKind:kind, array_num);

/**
 * A line as written in the file (with its %n, %r, %g, %b, %e, %m).
 *
 * @return          Characters written (0: no such line).
 */
native chat_get(RcChat:chat, RcChatKind:kind, array_num, index, out[], len);

/**
 * Starts the context chat_pick fills lines from: who the bot talks to (%n; "" for nobody, which
 * drops lines with %n) and the map (%m). Clears the players.
 */
native chat_context(const chat_to[] = "", const map[] = "");

/**
 * Adds a player to the context for %r (anyone), %g (rep > 6), %b (rep < 3), %e (enemy). Leave
 * out the bot itself.
 *
 * @return          The number of players in the context.
 */
native chat_context_player(const name[], rep, bool:enemy);

/**
 * The second half of CBot::BotChat: a random line of the list, filled from the context (names
 * without clan tags). Run chat_humanize on it before saying it.
 *
 * @param team_only Set to true for help lines (say_team).
 * @return          Characters written (0: nothing to say).
 */
native chat_pick(RcChat:chat, RcChatKind:kind, array_num, out[], len, &bool:team_only);

/**
 * BotFunc_GetRepArrayNum: 0 unfriendly (rep < 3), 1 neutral (3-6, and -1), 2 friendly.
 */
native chat_rep_array(rep);

/**
 * HumanizeString: typos (swapped, dropped, capitalised and lowered letters), in place.
 *
 * @return          The new length.
 */
native chat_humanize(text[], len);

/**
 * RemoveNameTags: a name without clan tags, lower case, as bots say it.
 */
native chat_remove_name_tags(const name[], out[], len);

/**
 * Seconds a bot "types" a line before saying it (9 characters a second).
 */
native Float:chat_typing_time(const text[]);
