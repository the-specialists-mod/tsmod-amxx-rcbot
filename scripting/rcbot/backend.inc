// rcbot/backend.inc: the port's bot backend rcbot_amxx, making and dropping bots.
// Owner: core. Ports host lib.rs (TSRS_PluginLoad's register_bot_backend, create, add_bot,
// activate, deactivate, adopt_bot, save_all, server_deactivate, rejoin_frame), bots.rs (new_bot,
// save_bot, put_in_server, disconnect) and commands.rs add_bot_now / Host::add_bot.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Quota: the port's ts_bot_quota and addbot / addcustombot call ts_bot_add (RC_ADDED_QUOTA); the
// port counts and kicks those. `rcbot addbot` and the rejoin code (min_bots / max_bots /
// bots_leave_and_join) call ts_bot_create directly (RC_ADDED_RCBOT), as the standalone host's
// BotFunc_AddBot does; leave ts_bot_quota at 0 when RCBot's own bot counts are used.

#if defined _rcbot_backend_included
	#endinput
#endif
#define _rcbot_backend_included

new g_register_status = TSBOT_NO_API;
// previous_profiles: the profile each bot had, for the bots the engine keeps across a level
// change (ts_bot_adopt). Saved in plugin_end; AMX Mod X reloads the plugin, so it is kept in a
// localinfo key (rcbot_amxx_prof) between maps.
new g_prev_profile[RC_SLOTS];
// m_fMapInitTime, m_bBotCanRejoin, m_fBotRejoinTime.
new Float:g_map_init_time;
new bool:g_can_rejoin;
new Float:g_rejoin_time;

#define RC_PREV_PROFILES_KEY	"rcbot_amxx_prof"

// plugin_init: TSRS_PluginLoad's register_bot_backend. Every map (tsx binds the slot again).
stock backend_register()
{
	g_register_status = ts_bot_register_backend(RC_BACKEND);
	if (g_register_status != TSBOT_OK)
	{
		log_amx("bot backend ^"%s^" not registered (%d)", RC_BACKEND, g_register_status);
		rc_log(TSLOG_WARNING, "bot backend ^"%s^" not registered (%d)", RC_BACKEND, g_register_status);
	}
	g_map_init_time = get_gametime();
	g_rejoin_time = g_map_init_time;
	g_can_rejoin = false;
	backend_load_prev_profiles();
}

// ---- backend forwards ---------------------------------------------------------------------------

public ts_bot_activate()
{
	g_active = true;
	rc_log(TSLOG_INFO, "bot backend active");
	report_backend_active();
}

public ts_bot_deactivate()
{
	g_active = false;
	backend_save_all();
	arrayset(g_prev_profile, 0, sizeof(g_prev_profile));
	rc_log(TSLOG_INFO, "bot backend inactive");
}

// addbot, addcustombot, the quota.
public ts_bot_add(const name[], const model[], Float:skill)
{
	new idx = backend_create(name, model, skill, RC_ADDED_QUOTA);
	return idx > 0 ? idx : 0;
}

// A bot the engine kept across the level change: a new CBot for it, with the profile it had.
public ts_bot_adopt(id)
{
	if (id < 1 || id > MAX_PLAYERS)
	{
		return;
	}
	state_clear_bot(id);
	profile_for(id, g_prev_profile[id]);
	g_prev_profile[id] = 0;
	get_user_name(id, g_bot_name[id], charsmax(g_bot_name[]));
	backend_new_bot(id, RC_ADDED_ADOPT);
	rc_log(TSLOG_INFO, "adopted ^"%s^" (#%d)", g_bot_name[id], id);
}

// ---- making bots ---------------------------------------------------------------------------------

// lib.rs create: the rest of BotFunc_AddBot once the port asks for a bot: a profile from
// bot_profiles.ini, the fake client, the CBot with its brain, learned data and reputations, the
// greeting. The profile is chosen into slot 0 (unused by players) before the bot has a slot.
stock backend_create(const name[], const model[], Float:skill, how)
{
	state_clear_bot(0);
	profile_for(0, 0);
	if (skill > 0.0)
	{
		g_pr_i[0][PR_SKILL] = skill <= 1.0 ? floatround(skill * 100.0, floatround_tozero) : floatround(skill, floatround_tozero);
	}
	new bot_name[RC_NAME_LEN];
	copy(bot_name, charsmax(bot_name), name[0] ? name : g_bot_name[0]);
	if (!bot_name[0])
	{
		copy(bot_name, charsmax(bot_name), BOT_DEFAULT_NAME);
	}
	new idx = ts_bot_create(bot_name, model, float(g_pr_i[0][PR_SKILL]) / 100.0);
	if (idx <= 0)
	{
		rc_log(TSLOG_WARNING, "create_bot ^"%s^": %d", bot_name, idx);
		return idx;
	}
	state_clear_bot(idx);
	backend_copy_profile(0, idx);
	copy(g_bot_name[idx], charsmax(g_bot_name[]), bot_name);
	backend_new_bot(idx, how);
	rc_log(TSLOG_INFO, "added ^"%s^" (#%d, profile %d)", bot_name, idx, g_pr_i[idx][PR_ID]);
	report_bot_added(idx, how);
	chat_greet(idx);
	return idx;
}

stock backend_copy_profile(from, to)
{
	for (new Pr_I:k; k < Pr_I; k++) g_pr_i[to][k] = g_pr_i[from][k];
	for (new Pr_F:k; k < Pr_F; k++) g_pr_f[to][k] = g_pr_f[from][k];
	copy(g_pr_model[to], charsmax(g_pr_model[]), g_pr_model[from]);
}

// bots.rs new_bot: the rest of BotFunc_ReadProfile and setupDataStructures: the HAL brain, the
// learned data, the reputations of every player here.
stock backend_new_bot(id, how)
{
	g_co_i[id][CO_USED] = 1;
	g_co_i[id][CO_USERID] = get_user_userid(id);
	g_co_i[id][CO_ADDED_BY] = how;
	g_ev_f[id][EV_JOIN_SERVER_TIME] = get_gametime();
	manager_bot_init(id);
	chat_wake_hal(id);
	learning_bot_load(id);
	kungfu_load(id);
	rep_load_all(id);
}

// bots.rs save_bot: SaveHALBrainForPersonality and saveLearnedData (every bot, design §4).
stock backend_save_bot(id)
{
	chat_save_hal(id);
	learning_bot_save(id);
	kungfu_save(id);
}

// The slot's bot is gone: save, free its handles, forget it.
stock backend_remove_bot(id)
{
	backend_save_bot(id);
	chat_free_hal(id);
	learning_bot_free(id);
	navigate_bot_free(id);
	g_co_i[id][CO_USED] = 0;
}

// lib.rs save_all: the bots (remembering their profiles for adopt), then the globals.
stock backend_save_all()
{
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (g_co_i[id][CO_USED])
		{
			backend_save_bot(id);
			g_prev_profile[id] = g_pr_i[id][PR_ID];
		}
	}
	learning_save_globals();
	backend_store_prev_profiles();
}

// previous_profiles across the plugin reload: "id:profile id:profile ..." in localinfo.
stock backend_store_prev_profiles()
{
	new text[256], len = 0;
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (g_prev_profile[id] != 0)
		{
			len += formatex(text[len], charsmax(text) - len, "%d:%d ", id, g_prev_profile[id]);
		}
	}
	set_localinfo(RC_PREV_PROFILES_KEY, text);
}

stock backend_load_prev_profiles()
{
	new text[256], word[16], left[8], right[8];
	get_localinfo(RC_PREV_PROFILES_KEY, text, charsmax(text));
	new pos = 0;
	while ((pos = argparse(text, pos, word, charsmax(word))) != -1)
	{
		strtok2(word, left, charsmax(left), right, charsmax(right), ':');
		new id = str_to_num(left);
		if (id >= 1 && id <= MAX_PLAYERS)
		{
			g_prev_profile[id] = str_to_num(right);
		}
	}
	set_localinfo(RC_PREV_PROFILES_KEY, "");
}

// commands.rs Host::add_bot (rcbot addbot, the rejoin code): a bot through ts_bot_create, with
// RCBot's team choice as its model in teamplay.
stock backend_add_bot_now(team = 0)
{
	if (!g_active)
	{
		rc_log(TSLOG_WARNING, "the rcbot_amxx bot backend isn't active (ts_bot_backend rcbot_amxx, then change map)");
		return 0;
	}
	new model[64];
	if (world_teamplay())
	{
		commands_ts_team_choice(team, model, charsmax(model));
	}
	return backend_create("", model, 0.0, RC_ADDED_RCBOT);
}

// lib.rs rejoin_frame (StartFrame's rejoin part): with max_bots, RCBot adds bots while there are
// fewer clients; with min_bots, while it has fewer bots; with neither, under
// bots_leave_and_join, at random while the server is less than about an eighth full. One bot
// per 5 s, from 10 s into the map.
stock backend_rejoin_frame()
{
	new Float:now = g_now;
	if (!g_can_rejoin && g_map_init_time + 10.0 < now && g_rejoin_time < now)
	{
		g_can_rejoin = true;
	}
	new max_clients = g_maxclients > 0 ? g_maxclients : 1;
	new num_clients = get_playersnum(1);
	new bool:bot_join = config_is_on(CFG_BOTS_LEAVE_AND_JOIN)
		&& float(num_clients) / float(max_clients) * random_float(4.9, 6.3) < 0.75;
	if (num_clients >= max_clients || !g_can_rejoin)
	{
		return;
	}
	new num_bots = 0;
	for (new id = 1; id <= MAX_PLAYERS; id++)
	{
		if (g_co_i[id][CO_USED])
		{
			num_bots++;
		}
	}
	new mn = g_cfg_i[CFG_MIN_BOTS], mx = g_cfg_i[CFG_MAX_BOTS];
	new bool:add = false;
	if (mx == -1 && mn == -1)
	{
		add = bot_join;
	}
	if (mx != -1 && num_clients < mx)
	{
		add = true;
	}
	if (mn != -1 && num_bots < mn)
	{
		add = true;
	}
	if (add)
	{
		backend_add_bot_now();
		g_can_rejoin = false;
		g_rejoin_time = now + 5.0;
	}
}

// ---- players joining and leaving ------------------------------------------------------------------

// bots.rs put_in_server (CClients::ClientConnected): the player's reputation id, every bot loads
// its reputation of them.
stock backend_put_in_server(id)
{
	clients_connect(id);
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (b != id && g_co_i[b][CO_USED])
		{
			rep_add_load(b, id);
		}
	}
}

// bots.rs disconnect (ClientDisconnect, CClients::ClientDisconnected): a leading player hands the
// squad on, every bot saves and drops its reputation of them and may greet them again; a bot
// saves its brain and learned data.
stock backend_disconnect(id)
{
	commands_force_grip_release_player(id);
	squad_player_left(id);
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (g_co_i[b][CO_USED])
		{
			rep_remove_save(b, id);
			events_forget_greeting(b, id);
		}
	}
	if (g_co_i[id][CO_USED])
	{
		backend_remove_bot(id);
	}
	clients_disconnect(id);
}
