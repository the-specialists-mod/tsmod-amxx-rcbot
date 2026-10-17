// rcbot/users.inc: who may use the rcbot commands (bot_users.ini).
// Owner: social. Ports config/users.rs (AllowedPlayer new / steam_id_defined / is_for_steam_id /
// is_for_name / is_for_pass / is_for_client / show, AllowedPlayers add_player / remove_player /
// get_player / show_users / clear / read_users) and host commands.rs read_users.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// bot_users.ini, as upstream reads it: fgets(255) chunks, "#" comments, one trailing "\n"
// removed, empty lines skipped; then everything up to the first quote is skipped, the name runs
// to the next quote (at most 64 bytes), one byte is skipped, and after runs of spaces come the
// password (16 bytes), the access level (8, atoi) and the Steam ID (32):
//     "Name" password 31 STEAM_0:1:2345
// The access level is a set of CMD_ACCESS_* bits (commands.inc). The list is a stack (newest
// at the end, searched from the top).

#if defined _rcbot_users_included
	#endinput
#endif
#define _rcbot_users_included

// users.rs BOT_MAX_PASSWORD_LEN, STEAM_ID_LEN.
#define BOT_MAX_PASSWORD_LEN	16
#define STEAM_ID_LEN			32
#define RC_MAX_USERS			64

new g_us_name[RC_MAX_USERS][65];
new g_us_pass[RC_MAX_USERS][BOT_MAX_PASSWORD_LEN];
new g_us_level[RC_MAX_USERS];
new g_us_steam[RC_MAX_USERS][STEAM_ID_LEN];
new g_us_count;

// AllowedPlayers::clear (Destroy).
stock users_clear()
{
	g_us_count = 0;
}

// AllowedPlayer::matches (operator == as dataStack::Remove calls it): the removed entry's name
// and password, or its Steam ID.
static bool:users_matches(k, const name[], const pass[], const steam[])
{
	if (equal(g_us_name[k], name) && equal(g_us_pass[k], pass))
	{
		return true;
	}
	return steam[0] && g_us_steam[k][0] && equal(g_us_steam[k], steam);
}

// AllowedPlayers::remove: the first matching entry from the top (password cut to 15 bytes and
// Steam ID to 31, as AllowedPlayer::new).
static users_remove(const name[], const pass[], const steam[])
{
	new p[BOT_MAX_PASSWORD_LEN], s[STEAM_ID_LEN];
	copy(p, charsmax(p), pass);
	copy(s, charsmax(s), steam);
	for (new k = g_us_count - 1; k >= 0; k--)
	{
		if (users_matches(k, name, p, s))
		{
			for (new j = k; j < g_us_count - 1; j++)
			{
				copy(g_us_name[j], charsmax(g_us_name[]), g_us_name[j + 1]);
				copy(g_us_pass[j], charsmax(g_us_pass[]), g_us_pass[j + 1]);
				g_us_level[j] = g_us_level[j + 1];
				copy(g_us_steam[j], charsmax(g_us_steam[]), g_us_steam[j + 1]);
			}
			g_us_count--;
			return;
		}
	}
}

// AllowedPlayers::add_player (AddPlayer): the first entry matching it goes, then it is pushed.
stock users_add_player(const name[], const pass[], level, const steam[])
{
	users_remove(name, pass, steam);
	if (g_us_count >= RC_MAX_USERS)
	{
		rc_log(TSLOG_WARNING, "%s: more than %d users, ^"%s^" not added", BOT_USERS_FILE, RC_MAX_USERS, name);
		return;
	}
	new k = g_us_count++;
	copy(g_us_name[k], charsmax(g_us_name[]), name);
	copy(g_us_pass[k], charsmax(g_us_pass[]), pass);
	g_us_level[k] = level;
	copy(g_us_steam[k], charsmax(g_us_steam[]), steam);
}

// AllowedPlayers::remove_player (RemovePlayer).
stock users_remove_player(const name[], const pass[], const steam[])
{
	users_remove(name, pass, steam);
}

// AllowedPlayer::is_for_client (IsForClient): by Steam ID when the entry has one, else by name
// (the client's current name) and password.
static bool:users_is_for_client(k, player, const name[])
{
	if (g_us_steam[k][0])
	{
		return g_cl_steam[player][0] && equal(g_us_steam[k], g_cl_steam[player]);
	}
	if (!g_cl_i[player][CL_USED])
	{
		return false;
	}
	return g_us_name[k][0] && equal(g_us_name[k], name) && equal(g_us_pass[k], g_cl_pass[player]);
}

// AllowedPlayers::get_player (GetPlayer): the newest entry for the client, -1 none.
stock users_get_player(player, const name[])
{
	for (new k = g_us_count - 1; k >= 0; k--)
	{
		if (users_is_for_client(k, player, name))
		{
			return k;
		}
	}
	return -1;
}

// The access level of the client's entry, 0 none.
stock users_access(player)
{
	new name[RC_NAME_LEN];
	get_user_name(player, name, charsmax(name));
	new k = users_get_player(player, name);
	return k == -1 ? 0 : g_us_level[k];
}

// AllowedPlayers::show_users (ShowUsers): a line per entry, newest first.
stock users_show(to)
{
	new line[192];
	for (new k = g_us_count - 1; k >= 0; k--)
	{
		formatex(line, charsmax(line), "name=^"%s^", pass=^"%s^", accesslevel=%d, steamID=^"%s^"", g_us_name[k], g_us_pass[k], g_us_level[k],
			g_us_steam[k]);
		commands_print(to, line);
	}
}

// A field of a users line: up to stop or max bytes.
static users_field(const b[], len, &i, stop, max, out[])
{
	new start = i < len ? i : len, j = 0;
	while (i < len && b[i] != stop && j < max)
	{
		i++;
		j++;
	}
	for (new k = 0; k < j; k++)
	{
		out[k] = b[start + k];
	}
	out[j] = 0;
}

static users_skip_spaces(const b[], len, &i)
{
	while (i < len && b[i] == ' ')
	{
		i++;
	}
}

// AllowedPlayers::read_users (ReadBotUsersConfig): adds the users, the messages to the server
// console (to: commands_print's target; -1 the log, as host commands.rs read_users).
stock users_read_file(to)
{
	new path[RC_PATH_LEN], msg[256];
	if (!cfg_open(BOT_USERS_FILE, path, charsmax(path)))
	{
		formatex(msg, charsmax(msg),
			"!!! Cannot find bot_users.ini !!!! (trying to look in %s - not found) permissions are bad or rcbot is not installed properly", path);
		users_say(to, msg);
		return;
	}
	new b[256], line = 0, users = 0;
	new name[65], pass[17], level[9], steam[STEAM_ID_LEN + 1];
	while (cfg_fgets(b, 255) != -1)
	{
		line++;
		if (!cfg_strip_line(b) || !b[0])
		{
			continue;
		}
		new len = strlen(b), i = 0;
		while (i < len && b[i] != '"')
		{
			i++;
		}
		i++;
		users_field(b, len, i, '"', 64, name);
		i++;
		users_skip_spaces(b, len, i);
		users_field(b, len, i, ' ', 16, pass);
		users_skip_spaces(b, len, i);
		users_field(b, len, i, ' ', 8, level);
		users_skip_spaces(b, len, i);
		users_field(b, len, i, ' ', STEAM_ID_LEN, steam);
		if ((name[0] && pass[0] && level[0]) || (steam[0] && level[0]))
		{
			formatex(msg, charsmax(msg), "Added: name=^"%s^", pass=%s, accesslev=%s, steamid=%s", name, pass, level, steam);
			users_say(to, msg);
			users_add_player(name, pass, str_to_num(level), steam);
			users++;
		}
		else
		{
			formatex(msg, charsmax(msg), "Error in bot_users.ini, error on line %d (bad format?)", line);
			users_say(to, msg);
		}
	}
	cfg_close();
	formatex(msg, charsmax(msg), "%d users added to list of RCBot users", users);
	users_say(to, msg);
}

static users_say(to, const msg[])
{
	if (to == -1)
	{
		rc_log(TSLOG_INFO, "%s", msg);
	}
	else
	{
		commands_print(to, msg);
	}
}

// host commands.rs read_users (ReadBotUsersConfig at GameDLLInit). From plugin_cfg (every map:
// AMX Mod X reloads the plugin, so the list starts empty).
stock users_read()
{
	users_clear();
	users_read_file(-1);
}
