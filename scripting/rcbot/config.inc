// rcbot/config.inc: RCBot's settings (CBotGlobals' configurable part).
// Owner: social. Ports config/mod.rs (Config, its defaults, is_on, change_setting,
// is_debug_on), config/file.rs (read_config: bot_config.ini's tokenizer, read_bot_folder) and
// host lib.rs map_config_frame (map_configs/ts/<map>.cfg, a line a frame).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.

#if defined _rcbot_config_included
	#endinput
#endif
#define _rcbot_config_included

enum Cfg_I
{
	CFG_SETTINGS,			// m_iConfigSettings: CFG_* bits
	CFG_FORCE_TEAM,			// m_iForceTeam: 1-based mp_teamlist index, <= 0 none
	CFG_MIN_BOTS,			// m_iMinBots, -1 off
	CFG_MAX_BOTS,			// m_iMaxBots, -1 off
	CFG_CHAT_PERCENT,		// m_iBotChatPercent
	CFG_CHAT_REPLY_PERCENT,	// m_iBotChatReplyPercent
	CFG_MAX_PATH_REVS,
	CFG_MAX_VIS_UPDATE_REVS,
	CFG_AUTO_PATH_WAYPOINT,
	CFG_DEBUG_LEVELS		// DBG_* bits
};
new g_cfg_i[Cfg_I];

enum Cfg_F
{
	Float:CFG_TURN_SPEED,
	Float:CFG_BOT_STUCK_SPEED,
	Float:CFG_UPDATE_LADDER_TIME,
	Float:CFG_UPDATE_VIS_TIME
};
new Float:g_cfg_f[Cfg_F];

// config/mod.rs Config::default: what upstream has when GameInit reads bot_config.ini.
stock config_defaults()
{
	g_cfg_i[CFG_SETTINGS] = CFG_TOOLTIPS | CFG_BLINKING | CFG_TACTICAL_PATHS;
	g_cfg_i[CFG_FORCE_TEAM] = -1;
	g_cfg_i[CFG_MIN_BOTS] = -1;
	g_cfg_i[CFG_MAX_BOTS] = -1;
	g_cfg_i[CFG_CHAT_PERCENT] = DEFAULT_BOT_CHAT_PERCENT;
	g_cfg_i[CFG_CHAT_REPLY_PERCENT] = 33;
	g_cfg_i[CFG_MAX_PATH_REVS] = 200;
	g_cfg_i[CFG_MAX_VIS_UPDATE_REVS] = 200;
	g_cfg_i[CFG_AUTO_PATH_WAYPOINT] = 1;
	g_cfg_i[CFG_DEBUG_LEVELS] = 0;
	g_cfg_f[CFG_TURN_SPEED] = 15.0;
	g_cfg_f[CFG_BOT_STUCK_SPEED] = 7.0;
	g_cfg_f[CFG_UPDATE_LADDER_TIME] = -1.0;
	g_cfg_f[CFG_UPDATE_VIS_TIME] = 0.0;
}

// Config::is_on (IsConfigSettingOn).
stock bool:config_is_on(setting)
{
	return (g_cfg_i[CFG_SETTINGS] & setting) > 0;
}

// Config::change_setting (ChangeConfigSetting): 0 turns it off, anything else on.
stock config_change_setting(setting, on)
{
	if (on == 0)
	{
		g_cfg_i[CFG_SETTINGS] &= ~setting;
	}
	else
	{
		g_cfg_i[CFG_SETTINGS] |= setting;
	}
}

// Config::is_debug_on (IsDebugLevelOn).
stock bool:config_is_debug_on(level)
{
	return (g_cfg_i[CFG_DEBUG_LEVELS] & level) > 0;
}

// TODO(config): config/file.rs read_config + commands.rs run_config: run bot_config.ini's lines
// as rcbot commands (host commands.rs read_config). Called once from plugin_cfg.
stock config_read()
{
}

// TODO(config): host lib.rs map_config_frame: map_configs/ts/<map>.cfg, a line a frame once
// the map has run 10 s; "rcbot addbot" holds the next line (and the rejoin) back 5 s
// (backend.inc g_rejoin_time). Read the file once at map start (config_read_map_config).
stock config_map_config_frame()
{
}

// TODO(config): host lib.rs server_activate's map config read (into a line queue).
stock config_read_map_config()
{
}
