// rcbot/profile.inc: bot profiles (bot_profiles.ini, botprofiles/<id>.ini).
// Owner: config. Ports config/profile.rs (BotProfile::init, BotProfile::read,
// parse_profile_list, choose_profile, profile_file_name) and host bots.rs profile_for.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Runs when a bot is made (not per frame). The profile lands in state.inc's profile section.

#if defined _rcbot_profile_included
	#endinput
#endif
#define _rcbot_profile_included

#define BOT_PROFILES_FILE		"bot_profiles.ini"
#define BOT_PROFILES_FOLDER		"botprofiles"
#define RC_MAX_PROFILE_IDS		128
#define RC_PROFILE_FILE_LEN		4096

// BotProfile::init (BotFunc_InitProfile): the defaults a profile file is read over.
stock profile_init(id)
{
	g_pr_i[id][PR_ID] = 0;
	g_pr_i[id][PR_FAV_MOD] = 0;
	g_pr_i[id][PR_FAV_TEAM] = TEAM_AUTOTEAM;
	g_pr_i[id][PR_SKILL] = DEF_BOT_SKILL;
	g_pr_i[id][PR_NUM_GAMES] = 0;
	// Upstream draws the bottom colour, then the top colour.
	g_pr_i[id][PR_BOTTOM_COLOUR] = random_num(0, 255);
	g_pr_i[id][PR_TOP_COLOUR] = random_num(0, 255);
	g_pr_i[id][PR_PATH_REVS] = g_cfg_i[CFG_MAX_PATH_REVS];
	g_pr_i[id][PR_VISION_REVS] = g_cfg_i[CFG_MAX_VIS_UPDATE_REVS];
	g_pr_i[id][PR_CLASS] = -1;
	g_pr_f[id][PR_AIM_SPEED] = 0.25;
	g_pr_f[id][PR_AIM_SKILL] = 0.2;
	g_pr_f[id][PR_AIM_TIME] = 1.0;
	g_pr_f[id][PR_VISION_TIME] = g_cfg_f[CFG_UPDATE_VIS_TIME];
	g_pr_model[id][0] = 0;
	copy(g_bot_name[id], charsmax(g_bot_name[]), BOT_DEFAULT_NAME);
}

// host bots.rs profile_for: <again>.ini (a bot coming back after a map change), else
// BotFunc_AddBot's choice among bot_profiles.ini's ids no other bot uses, else the defaults
// (id 0: upstream adds no bot then; the port has to, design §4). id is the slot being filled.
stock profile_for(id, again)
{
	profile_init(id);
	if (again != 0 && profile_read(id, again))
	{
		return;
	}
	static text[RC_PROFILE_FILE_LEN];
	if (files_read_text(BOT_PROFILES_FILE, text, sizeof(text)) < 0)
	{
		return;
	}
	new ids[RC_MAX_PROFILE_IDS], n = profile_parse_list(text, ids, sizeof(ids));
	new can_use[RC_MAX_PROFILE_IDS], m = 0;
	new rel[64], path[RC_PATH_LEN];
	for (new k = 0; k < n; k++)
	{
		new pid = ids[k];
		if (pid == 0 || profile_in_use(pid, id))
		{
			continue;
		}
		formatex(rel, charsmax(rel), "%s/%d.ini", BOT_PROFILES_FOLDER, pid);
		if (files_find(path, charsmax(path), rel))
		{
			can_use[m++] = pid;
		}
		else
		{
			rc_log(TSLOG_INFO, "Error: Bot Profile : %d.ini Expected, Profile Not Found", pid);
		}
	}
	if (m > 0 && profile_read(id, can_use[random_num(0, m - 1)]))
	{
		return;
	}
	rc_log(TSLOG_INFO, "Error: No Bot Profiles have been found that are free...");
}

// A bot in the game (other than slot except) uses profile pid.
stock bool:profile_in_use(pid, except)
{
	for (new b = 1; b <= MAX_PLAYERS; b++)
	{
		if (b != except && g_co_i[b][CO_USED] && g_pr_i[b][PR_ID] == pid)
		{
			return true;
		}
	}
	return false;
}

// parse_profile_list: whitespace-separated words of at most 9 characters, each atoi'd.
// TODO(config): upstream checks the word + ".ini" ("12abc.ini") but loads "%d.ini"; this checks
// "%d.ini" (ProfileListEntry::file).
stock profile_parse_list(const text[], ids[], max)
{
	new n = 0, i = 0, word[10];
	while (text[i] && n < max)
	{
		while (text[i] && isspace(text[i]))
		{
			i++;
		}
		if (!text[i])
		{
			break;
		}
		new w = 0;
		while (text[i] && !isspace(text[i]) && w < 9)
		{
			word[w++] = text[i++];
		}
		word[w] = 0;
		ids[n++] = str_to_num(word);
	}
	return n;
}

// BotProfile::read over botprofiles/<pid>.ini: keys matched by prefix, numbers with atoi/atof,
// strings from after the opening quote to the next quote (model= has none).
// TODO(config): upstream's fgets(127) chunking and "#" comments per chunk, bottomcolor /
// topcolor clamping, favmap/spray/numgames/NS keys, the 63-byte string cut (profile.rs read).
stock bool:profile_read(id, pid)
{
	static text[RC_PROFILE_FILE_LEN];
	new rel[64];
	formatex(rel, charsmax(rel), "%s/%d.ini", BOT_PROFILES_FOLDER, pid);
	if (files_read_text(rel, text, sizeof(text)) < 0)
	{
		return false;
	}
	g_pr_i[id][PR_ID] = pid;
	new line[128], pos = 0, len;
	while ((len = profile_next_line(text, pos, line, charsmax(line))) >= 0)
	{
		if (len == 0 || line[0] == '#')
		{
			continue;
		}
		if (equal(line, "name=", 5))
		{
			profile_string(line[5], g_bot_name[id], charsmax(g_bot_name[]));
		}
		else if (equal(line, "skill=", 6))
		{
			g_pr_i[id][PR_SKILL] = str_to_num(line[6]);
		}
		else if (equal(line, "favteam=", 8))
		{
			g_pr_i[id][PR_FAV_TEAM] = str_to_num(line[8]);
		}
		else if (equal(line, "aim_speed=", 10))
		{
			g_pr_f[id][PR_AIM_SPEED] = str_to_float(line[10]);
		}
		else if (equal(line, "aim_skill=", 10))
		{
			g_pr_f[id][PR_AIM_SKILL] = str_to_float(line[10]);
		}
		else if (equal(line, "aim_time=", 9))
		{
			g_pr_f[id][PR_AIM_TIME] = str_to_float(line[9]);
		}
		else if (equal(line, "max_path_revs=", 14))
		{
			g_pr_i[id][PR_PATH_REVS] = str_to_num(line[14]);
		}
		else if (equal(line, "max_update_vision_revs=", 23))
		{
			g_pr_i[id][PR_VISION_REVS] = str_to_num(line[23]);
		}
		else if (equal(line, "update_vision_time=", 19))
		{
			g_pr_f[id][PR_VISION_TIME] = str_to_float(line[19]);
		}
		else if (equal(line, "class=", 6))
		{
			g_pr_i[id][PR_CLASS] = str_to_num(line[6]);
		}
		else if (equal(line, "model=", 6))
		{
			// No quote skipped: model="x" gives an empty model, as upstream.
			profile_string(line[6], g_pr_model[id], charsmax(g_pr_model[]));
		}
	}
	return true;
}

// A quoted value: skip one opening quote, copy to the next quote or the end.
static profile_string(const from[], out[], len)
{
	new i = from[0] == '"' ? 1 : 0, o = 0;
	while (from[i] && from[i] != '"' && o < len)
	{
		out[o++] = from[i++];
	}
	out[o] = 0;
}

// The next line of text from pos (without "\r\n"); -1 at the end.
static profile_next_line(const text[], &pos, line[], len)
{
	if (!text[pos])
	{
		return -1;
	}
	new o = 0;
	while (text[pos] && text[pos] != '^n')
	{
		if (text[pos] != '^r' && o < len)
		{
			line[o++] = text[pos];
		}
		pos++;
	}
	if (text[pos] == '^n')
	{
		pos++;
	}
	line[o] = 0;
	return o;
}
