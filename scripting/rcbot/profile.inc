// rcbot/profile.inc: bot profiles (bot_profiles.ini, botprofiles/<id>.ini).
// Owner: social. Ports config/profile.rs (BotProfile::init, BotProfile::read,
// parse_profile_list, choose_profile, profile_file_name) and host bots.rs profile_for.
// BotProfile::write isn't ported: only upstream's old, commented-out disconnect code called it.
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
	for (new k = 0; k < PR_HAL_FILES; k++)
	{
		g_pr_hal_file[id][k][0] = 0;
	}
	copy(g_bot_name[id], charsmax(g_bot_name[]), BOT_DEFAULT_NAME);
}

// host bots.rs profile_for: <again>.ini (a bot coming back after a map change), else
// BotFunc_AddBot's choice among bot_profiles.ini's ids no other bot uses (choose_profile), else
// the defaults (id 0: upstream adds no bot then; the port has to, design §4). id is the slot
// being filled.
stock profile_for(id, again)
{
	profile_init(id);
	if (again != 0 && profile_read(id, again))
	{
		return;
	}
	new ids[RC_MAX_PROFILE_IDS], words[RC_MAX_PROFILE_IDS][10];
	new n = profile_parse_list(ids, words, RC_MAX_PROFILE_IDS);
	if (n < 0)
	{
		return;
	}
	new can_use[RC_MAX_PROFILE_IDS], m = 0;
	new rel[64], path[RC_PATH_LEN];
	for (new k = 0; k < n; k++)
	{
		new pid = ids[k];
		if (pid == 0 || profile_in_use(pid, id))
		{
			continue;
		}
		// Upstream checks the word + ".ini" ("12abc.ini") and loads "%d.ini" (ProfileListEntry).
		formatex(rel, charsmax(rel), "%s/%s.ini", BOT_PROFILES_FOLDER, words[k]);
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

// parse_profile_list over bot_profiles.ini (fscanf "%9s"): whitespace-separated words of at
// most 9 bytes (longer words split), each atoi'd. -1 without the file.
stock profile_parse_list(ids[], words[][10], max)
{
	new path[RC_PATH_LEN];
	if (!cfg_open(BOT_PROFILES_FILE, path, charsmax(path)))
	{
		return -1;
	}
	new n = 0, w = 0, c;
	new word[10];
	for (;;)
	{
		c = cfg_profile_list_getc();
		if (c == -1 || cfg_is_space(c) || w == 9)
		{
			if (w > 0 && n < max)
			{
				word[w] = 0;
				copy(words[n], 9, word);
				ids[n++] = str_to_num(word);
			}
			w = 0;
			if (c == -1)
			{
				break;
			}
			if (cfg_is_space(c))
			{
				continue;
			}
		}
		word[w++] = c;
	}
	cfg_close();
	return n;
}

// fscanf reads the file as it is (no fgets chunks): a byte at a time through cfg_fgets' reader.
static cfg_profile_list_getc()
{
	new b[2];
	return cfg_fgets(b, 2) == -1 ? -1 : (b[0] ? b[0] : ' ');
}

// BotProfile::read over botprofiles/<pid>.ini: fgets(127) chunks, "#" comments, keys matched by
// prefix (case-sensitive), numbers with atoi/atof, strings read from a fixed offset to the next
// quote (at most 63 bytes): the offset skips the opening quote of name="...", favmap, spray and
// the hal_*_file keys, but not model=, so model="gordon" gives an empty model. favmap, spray and
// the NS percentages aren't kept (nothing reads them on TS).
stock bool:profile_read(id, pid)
{
	new rel[64], path[RC_PATH_LEN];
	formatex(rel, charsmax(rel), "%s/%d.ini", BOT_PROFILES_FOLDER, pid);
	if (!cfg_open(rel, path, charsmax(path)))
	{
		return false;
	}
	g_pr_i[id][PR_ID] = pid;
	new line[128];
	while (cfg_fgets(line, 127) != -1)
	{
		if (!cfg_strip_line(line))
		{
			continue;
		}
		new len = strlen(line);
		if (equal(line, "name=", 5))
		{
			profile_string(line, len, 6, g_bot_name[id], charsmax(g_bot_name[]));
		}
		else if (equal(line, "class=", 6))
		{
			g_pr_i[id][PR_CLASS] = profile_num(line, len, 6);
		}
		else if (equal(line, "model=", 6))
		{
			profile_string(line, len, 6, g_pr_model[id], charsmax(g_pr_model[]));
		}
		else if (equal(line, "favmod=", 7))
		{
			g_pr_i[id][PR_FAV_MOD] = profile_num(line, len, 7);
		}
		else if (equal(line, "favteam=", 8))
		{
			g_pr_i[id][PR_FAV_TEAM] = profile_num(line, len, 8);
		}
		else if (equal(line, "skill=", 6))
		{
			g_pr_i[id][PR_SKILL] = profile_num(line, len, 6);
		}
		else if (equal(line, "aim_speed=", 10))
		{
			g_pr_f[id][PR_AIM_SPEED] = profile_float(line, len, 10);
		}
		else if (equal(line, "aim_skill=", 10))
		{
			g_pr_f[id][PR_AIM_SKILL] = profile_float(line, len, 10);
		}
		else if (equal(line, "aim_time=", 9))
		{
			g_pr_f[id][PR_AIM_TIME] = profile_float(line, len, 9);
		}
		else if (equal(line, "bottomcolor=", 12))
		{
			g_pr_i[id][PR_BOTTOM_COLOUR] = clamp(profile_num(line, len, 12), 0, 255);
		}
		else if (equal(line, "topcolor=", 9))
		{
			g_pr_i[id][PR_TOP_COLOUR] = clamp(profile_num(line, len, 9), 0, 255);
		}
		else if (equal(line, "numgames=", 9))
		{
			g_pr_i[id][PR_NUM_GAMES] = profile_num(line, len, 9);
		}
		else if (equal(line, "hal_pretrain_file=", 18))
		{
			profile_string(line, len, 19, g_pr_hal_file[id][PR_HAL_PRETRAIN], charsmax(g_pr_hal_file[][]));
		}
		else if (equal(line, "hal_aux_file=", 13))
		{
			profile_string(line, len, 14, g_pr_hal_file[id][PR_HAL_AUX], charsmax(g_pr_hal_file[][]));
		}
		else if (equal(line, "hal_ban_file=", 13))
		{
			profile_string(line, len, 14, g_pr_hal_file[id][PR_HAL_BAN], charsmax(g_pr_hal_file[][]));
		}
		else if (equal(line, "hal_swap_file=", 14))
		{
			profile_string(line, len, 15, g_pr_hal_file[id][PR_HAL_SWAP], charsmax(g_pr_hal_file[][]));
		}
		else if (equal(line, "max_path_revs=", 14))
		{
			g_pr_i[id][PR_PATH_REVS] = profile_num(line, len, 14);
		}
		else if (equal(line, "max_update_vision_revs=", 23))
		{
			g_pr_i[id][PR_VISION_REVS] = profile_num(line, len, 23);
		}
		else if (equal(line, "update_vision_time=", 19))
		{
			g_pr_f[id][PR_VISION_TIME] = profile_float(line, len, 19);
		}
	}
	cfg_close();
	return true;
}

// read's string(at): from byte at to the next quote or the end, at most 63 bytes (out's size
// is the smaller bound for the name).
static profile_string(const line[], len, at, out[], max)
{
	new o = 0;
	if (max > 63)
	{
		max = 63;
	}
	for (new i = at; i < len && line[i] != '"' && o < max; i++)
	{
		out[o++] = line[i];
	}
	out[o] = 0;
}

// read's num(at): atoi of the rest, 0 past the end.
static profile_num(const line[], len, at)
{
	return at <= len ? str_to_num(line[at]) : 0;
}

static Float:profile_float(const line[], len, at)
{
	return at <= len ? str_to_float(line[at]) : 0.0;
}
