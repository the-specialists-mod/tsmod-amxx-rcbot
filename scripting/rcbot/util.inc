// rcbot/util.inc: logging and small helpers.
// Owner: core. RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX
// Mod X and RCBot linking exceptions.
//
// rc_log formats, so it is for events (a bot added, a file missing), never per frame.

#if defined _rcbot_util_included
	#endinput
#endif
#define _rcbot_util_included

// A server console line "[rcbot_amxx] ..." through tsx (TSLOG_*; TSLOG_DEBUG only with developer).
stock rc_log(level, const fmt[], any:...)
{
	static msg[256];
	vformat(msg, charsmax(msg), fmt, 3);
	ts_bot_log(level, msg);
}

// rc_log at TSLOG_DEBUG when the config's debug level bit is on (BotMessage with a level).
stock rc_debug(level_bit, const fmt[], any:...)
{
	if (!config_is_debug_on(level_bit))
	{
		return;
	}
	static msg[256];
	vformat(msg, charsmax(msg), fmt, 3);
	ts_bot_log(TSLOG_DEBUG, msg);
}

// A connected player that is one of our bots.
stock bool:is_rcbot(id)
{
	return id >= 1 && id <= MAX_PLAYERS && g_co_i[id][CO_USED] != 0;
}
