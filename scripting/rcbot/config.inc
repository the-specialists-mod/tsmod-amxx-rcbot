// rcbot/config.inc: RCBot's settings (CBotGlobals' configurable part).
// Owner: social. Ports config/mod.rs (Config, its defaults, is_on, change_setting,
// is_debug_on, the file names), config/file.rs (read_config: bot_config.ini's tokenizer),
// config/cstr.rs (fgets chunks in text mode: the readers of every RCBot text file), host
// commands.rs read_config, bots.rs State::write (atomic writes) and lib.rs server_activate /
// map_config_frame (map_configs/ts/<map>.cfg, a line a frame).
//
// Not ported: read_bot_folder (rcbot_folder.ini): the folder is tsmod/rcbot/, as the standalone
// plugin's data directory (files.inc). config.inc is included before world.inc, files.inc and
// backend.inc, so it only calls their functions (no globals of theirs).
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

// config/mod.rs file names.
#define BOT_CONFIG_FILE			"bot_config.ini"
#define BOT_USERS_FILE			"bot_users.ini"
#define BOT_PLAYER_ID_FILE		"bot_playerids.ini"
// The mod folder of the waypoints and map configs (Config::mod_folder).
#define RC_MOD_FOLDER			"ts"
// file.rs CONFIG_TOKEN_LEN.
#define CONFIG_TOKEN_LEN		64

// ---- reading text files as upstream's fgets does (cstr.rs) ----------------------------------

// One file is read at a time (every reader runs to its end inside one event).
new g_rd_file;
new g_rd_buf[1024];
new g_rd_len;
new g_rd_pos;
new g_rd_back = -1;			// a byte read ahead (text mode's "\r\n" check), -1 none

// Opens rel (files_find: the shared folder's, else the backend folder's) for cfg_fgets; out
// gets the path tried. False if it can't be opened.
stock bool:cfg_open(const rel[], out[], len)
{
	new path[RC_PATH_LEN];
	files_find(path, charsmax(path), rel);
	copy(out, len, path);
	cfg_close();
	g_rd_file = fopen(path, "rb");
	g_rd_len = 0;
	g_rd_pos = 0;
	g_rd_back = -1;
	return g_rd_file != 0;
}

stock cfg_close()
{
	if (g_rd_file)
	{
		fclose(g_rd_file);
		g_rd_file = 0;
	}
}

// The next byte (0..255), -1 at the end.
static cfg_getc_raw()
{
	if (g_rd_back != -1)
	{
		new c = g_rd_back;
		g_rd_back = -1;
		return c;
	}
	if (g_rd_pos >= g_rd_len)
	{
		if (!g_rd_file)
		{
			return -1;
		}
		g_rd_len = fread_blocks(g_rd_file, g_rd_buf, sizeof(g_rd_buf), BLOCK_CHAR);
		g_rd_pos = 0;
		if (g_rd_len <= 0)
		{
			g_rd_len = 0;
			return -1;
		}
	}
	// fread_blocks stores signed chars; RCBot's bytes are unsigned (as AMX Mod X's strings).
	return g_rd_buf[g_rd_pos++] & 0xFF;
}

// cstr.rs text_mode: "\r\n" reads as "\n" (Windows' text mode, on every platform).
static cfg_getc()
{
	new c = cfg_getc_raw();
	if (c == '^r')
	{
		new d = cfg_getc_raw();
		if (d == '^n')
		{
			return d;
		}
		g_rd_back = d;
	}
	return c;
}

// fgets(buffer, n) (cstr.rs fgets_chunks): at most n - 1 bytes, up to and with a "\n". out is
// what C sees of the chunk (c_str: up to a NUL byte). Returns the chunk's length in the file
// (with what follows a NUL), -1 at the end of the file.
stock cfg_fgets(out[], n)
{
	new k = 0, o = 0, bool:nul = false, c;
	while (k < n - 1 && (c = cfg_getc()) != -1)
	{
		k++;
		if (c == 0)
		{
			nul = true;
		}
		if (!nul)
		{
			out[o++] = c;
		}
		if (c == '^n')
		{
			break;
		}
	}
	out[o] = 0;
	return k == 0 ? -1 : k;
}

// cstr.rs is_c_space.
stock bool:cfg_is_space(c)
{
	return c == ' ' || c == '^t' || c == '^n' || c == 11 || c == 12 || c == '^r';
}

// The common start of RCBot's line readers: a chunk starting with "#" is skipped (false), one
// trailing "\n" is removed.
stock bool:cfg_strip_line(line[])
{
	if (line[0] == '#')
	{
		return false;
	}
	new len = strlen(line);
	if (len > 0 && line[len - 1] == '^n')
	{
		line[len - 1] = 0;
	}
	return true;
}

// ---- writing (host bots.rs State::write) -----------------------------------------------------

// Makes the folders of a file path relative to the game directory (create_dir_all).
stock cfg_make_dirs(const path[])
{
	new dir[RC_PATH_LEN];
	for (new i = 0; path[i] && i < charsmax(dir); i++)
	{
		if (path[i] == '/' && i > 0)
		{
			dir[i] = 0;
			if (!dir_exists(dir))
			{
				mkdir(dir);
			}
		}
		dir[i] = path[i];
	}
}

// State::write's first half: a temporary file beside rel (in the shared folder), its folders
// made. The handle, 0 on failure.
stock cfg_write_begin(const rel[])
{
	new path[RC_PATH_LEN], tmp[RC_PATH_LEN + 4];
	files_path(path, charsmax(path), rel);
	cfg_make_dirs(path);
	formatex(tmp, charsmax(tmp), "%s.tmp", path);
	new f = fopen(tmp, "wb");
	if (!f)
	{
		rc_log(TSLOG_WARNING, "can't write %s", path);
	}
	return f;
}

// State::write's second half: the temporary file renamed over rel, so a crash mid-save leaves
// the last good file. Windows' MoveFile doesn't replace a file, so the old one goes first there.
stock bool:cfg_write_end(f, const rel[])
{
	new path[RC_PATH_LEN], tmp[RC_PATH_LEN + 4];
	files_path(path, charsmax(path), rel);
	formatex(tmp, charsmax(tmp), "%s.tmp", path);
	fclose(f);
	if (rename_file(tmp, path, 1))
	{
		return true;
	}
	delete_file(path);
	if (rename_file(tmp, path, 1))
	{
		return true;
	}
	delete_file(tmp);
	rc_log(TSLOG_WARNING, "can't write %s", path);
	return false;
}

// Writes a string's bytes (no NUL).
stock cfg_write_text(f, const text[])
{
	new n = strlen(text);
	if (n > 0)
	{
		fwrite_blocks(f, text, n, BLOCK_CHAR);
	}
}

// ---- bot_config.ini (file.rs read_config, commands.rs run_config) -----------------------------

// host commands.rs read_config (CBotGlobals::ReadConfig): each line of bot_config.ini runs as an
// rcbot command without a client and without access checks. Once, from plugin_cfg.
stock config_read()
{
	new path[RC_PATH_LEN];
	if (!cfg_open(BOT_CONFIG_FILE, path, charsmax(path)))
	{
		rc_log(TSLOG_INFO, "no %s (defaults)", path);
		return;
	}
	static lines[64][5][CONFIG_TOKEN_LEN];
	new n = 0;
	new buffer[128];
	// The whole file is read first (host: parse, then run), as a command may read files too.
	while (cfg_fgets(buffer, 127) != -1)
	{
		if (!cfg_strip_line(buffer) || !buffer[0])
		{
			continue;
		}
		if (n >= sizeof(lines))
		{
			rc_log(TSLOG_WARNING, "%s: more than %d commands, the rest ignored", BOT_CONFIG_FILE, sizeof(lines));
			break;
		}
		config_tokenize(buffer, lines[n]);
		n++;
	}
	cfg_close();
	for (new k = 0; k < n; k++)
	{
		commands_run_config_line(lines[k][0], lines[k][1], lines[k][2], lines[k][3], lines[k][4]);
	}
}

// file.rs read_config's tokens: split on spaces only (a tab is part of a token), the command and
// four arguments, each cut to 63 bytes (upstream overruns its 64-byte buffers); missing ones
// are empty, the rest of the line is ignored.
stock config_tokenize(const buffer[], tokens[5][CONFIG_TOKEN_LEN])
{
	new i = 0;
	for (new t = 0; t < 5; t++)
	{
		while (buffer[i] == ' ')
		{
			i++;
		}
		new o = 0;
		while (buffer[i] && buffer[i] != ' ')
		{
			if (o < CONFIG_TOKEN_LEN - 1)
			{
				tokens[t][o++] = buffer[i];
			}
			i++;
		}
		tokens[t][o] = 0;
	}
}

// ---- map configs (host lib.rs server_activate, map_config_frame) -----------------------------

#define RC_MAP_CONFIG_LINES		128
#define RC_MAP_CONFIG_LINE_LEN	192

new g_mapcfg[RC_MAP_CONFIG_LINES][RC_MAP_CONFIG_LINE_LEN];
new g_mapcfg_count;
new g_mapcfg_next;
new Float:g_mapcfg_start;			// m_fMapInitTime
new Float:g_read_config_time;		// m_fReadConfigTime

// lib.rs server_activate's ReadMapConfig: map_configs/ts/<map>.cfg, its lines kept (with their
// "\n", "\r\n" read as "\n") for config_map_config_frame. From plugin_cfg, at the map's start.
stock config_read_map_config()
{
	new map[64], rel[128], path[RC_PATH_LEN];
	get_mapname(map, charsmax(map));
	formatex(rel, charsmax(rel), "map_configs/%s/%s.cfg", RC_MOD_FOLDER, map);
	g_mapcfg_count = 0;
	g_mapcfg_next = 0;
	g_mapcfg_start = get_gametime();
	g_read_config_time = 0.0;
	if (!cfg_open(rel, path, charsmax(path)))
	{
		rc_log(TSLOG_INFO, "No map specific config file found (%s)", path);
		return;
	}
	rc_log(TSLOG_INFO, "map specific config file found...");
	// Lines of any length (split at "\n"): a longer one is cut to the buffer.
	new line[RC_MAP_CONFIG_LINE_LEN], o = 0, c;
	new bool:any = false;
	while (g_mapcfg_count < RC_MAP_CONFIG_LINES)
	{
		c = cfg_getc();
		if (c == -1)
		{
			break;
		}
		any = true;
		if (o < charsmax(line))
		{
			line[o++] = c;
		}
		if (c == '^n')
		{
			line[o] = 0;
			copy(g_mapcfg[g_mapcfg_count++], charsmax(g_mapcfg[]), line);
			o = 0;
			any = false;
		}
	}
	if (any && g_mapcfg_count < RC_MAP_CONFIG_LINES)
	{
		line[o] = 0;
		copy(g_mapcfg[g_mapcfg_count++], charsmax(g_mapcfg[]), line);
	}
	cfg_close();
}

// lib.rs map_config_frame (ReadMapConfig): a line a frame once the map has run 10 s, each a
// server command; "rcbot addbot" holds the next line and the rejoin code's bots back 5 s.
// Comment lines use up their frame. Every frame, so the early return comes first.
stock config_map_config_frame()
{
	if (g_mapcfg_next >= g_mapcfg_count)
	{
		return;
	}
	new Float:now = get_gametime();
	if (g_mapcfg_start + 10.0 >= now || g_read_config_time >= now)
	{
		return;
	}
	new line[RC_MAP_CONFIG_LINE_LEN];
	copy(line, charsmax(line), g_mapcfg[g_mapcfg_next++]);
	if (line[0] == '#')
	{
		return;
	}
	if (equal(line, "rcbot addbot", 12))
	{
		commands_hold_rejoin(now + 5.0);
		g_read_config_time = now + 5.0;
	}
	// server_cmd adds the "\n" SERVER_COMMAND needs.
	new len = strlen(line);
	if (len > 0 && line[len - 1] == '^n')
	{
		line[len - 1] = 0;
	}
	server_cmd("%s", line);
}
