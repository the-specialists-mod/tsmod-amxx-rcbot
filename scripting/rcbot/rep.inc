// rcbot/rep.inc: the bots' reputations of players (CBotReputation(s), <id>.rcr).
// Owner: social. Ports config/rep.rs (Reputation describe, parse_rep_file, write_rep,
// rep_file_name, Reputations get / add_rep / average_rep_on_server / client_rep / increase_rep /
// decrease_rep / random_client / print_reps / add_load_rep / remove_save_rep, decrease_rep_roll,
// increase_rep_roll), bot/events.rs Bot::client_rep_id / client_rep / rep_of /
// increase_rep_of / decrease_rep_of, host bots.rs new_bot / put_in_server / disconnect's .rcr
// handling. player_rep_id (bot_playerids.ini) is client.inc's.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// botprofiles/<profile>.rcr, byte for byte as tsmod_rcbot (and RCBot) writes it: raw 8-byte
// records, int m_iPlayerRepId then int m_iRep, little-endian (fread/fwrite BLOCK_INT). A file
// whose size isn't a multiple of 8 is neither read nor written. A bot without a profile (id 0)
// keeps its reputations in memory only. The list (g_rep_*, state.inc) is a stack: newest at the
// end, searched from the top.
//
// Not ported: Reputations::save_all_rep (SaveAllRep; the host never calls it), new_rep /
// get_create_rep (nothing calls them).

#if defined _rcbot_rep_included
	#endinput
#endif
#define _rcbot_rep_included

#define REP_RECORD_SIZE			8

// events.rs Bot::client_rep_id (State::rep_ids): a player's reputation id, NO_CLIENT without a
// CClient.
stock rep_client_id(player)
{
	if (player < 1 || player > MAX_PLAYERS || !g_cl_i[player][CL_USED])
	{
		return NO_CLIENT;
	}
	return g_cl_i[player][CL_REP_ID];
}

// Reputations::get (GetRep): the list slot of a reputation id, -1 none.
stock rep_find(id, rep_id)
{
	for (new k = g_rep_count[id] - 1; k >= 0; k--)
	{
		if (g_rep_id[id][k] == rep_id)
		{
			return k;
		}
	}
	return -1;
}

// Pushes a reputation (the newest is dropped when the list is full).
static rep_push(id, rep_id, rep)
{
	new n = g_rep_count[id];
	if (n >= RC_MAX_REPS)
	{
		rc_debug(DBG_MESSAGE, "%s: reputation list full, rep id %d dropped", g_bot_name[id], rep_id);
		return;
	}
	g_rep_id[id][n] = rep_id;
	g_rep_value[id][n] = rep;
	g_rep_count[id] = n + 1;
}

// Reputations::add_rep (AddRep): unless the player has one.
stock rep_add_rep(id, rep_id, rep)
{
	if (rep_find(id, rep_id) == -1)
	{
		rep_push(id, rep_id, rep);
	}
}

// Reputations::client_rep (GetClientRep) by reputation id: NO_CLIENT (upstream's NULL client) and
// -1 and an unknown id are BOT_MID_REP.
stock rep_client_rep(id, rep_id)
{
	if (rep_id == NO_CLIENT || rep_id == -1)
	{
		return BOT_MID_REP;
	}
	new k = rep_find(id, rep_id);
	return k == -1 ? BOT_MID_REP : g_rep_value[id][k];
}

// events.rs Bot::client_rep (m_Profile.m_Rep.GetClientRep(pClient)): the bot's rep of player,
// BOT_MID_REP when unknown.
stock rep_of(id, player)
{
	return rep_client_rep(id, rep_client_id(player));
}

// events.rs Bot::rep_of (m_Profile.m_Rep.GetRep(id)): the bot's reputation of player if it has
// one (false: no CClient, or no reputation for its id).
stock bool:rep_lookup(id, player, &rep)
{
	new rid = rep_client_id(player);
	if (rid == NO_CLIENT)
	{
		return false;
	}
	new k = rep_find(id, rid);
	if (k == -1)
	{
		return false;
	}
	rep = g_rep_value[id][k];
	return true;
}

// Reputations::step (IncreaseRep / DecreaseRep): -1 is ignored; within 0..10 only. Upstream's
// BugMessage for a missing reputation is dropped, as the Rust callers drop it.
static rep_step(id, rep_id, by)
{
	if (rep_id == -1 || rep_id == NO_CLIENT)
	{
		return;
	}
	new k = rep_find(id, rep_id);
	if (k == -1)
	{
		return;
	}
	new v = g_rep_value[id][k] + by;
	if (v >= BOT_MIN_REP && v <= BOT_MAX_REP)
	{
		g_rep_value[id][k] = v;
	}
}

// events.rs Bot::increase_rep_of.
stock rep_increase(id, player)
{
	rep_step(id, rep_client_id(player), 1);
}

// events.rs Bot::decrease_rep_of.
stock rep_decrease(id, player)
{
	rep_step(id, rep_client_id(player), -1);
}

// Reputations::average_rep_on_server (AverageRepOnServer): the integer mean, 0 with none.
stock rep_average_on_server(id)
{
	new n = g_rep_count[id];
	if (n == 0)
	{
		return 0;
	}
	new sum = 0;
	for (new k = 0; k < n; k++)
	{
		sum += g_rep_value[id][k];
	}
	return sum / n;
}

// Reputations::random_client (GetRandomClient(iRep)): a random player the bot dislikes (-1:
// rep <= 3), is neutral about (0: 4-6) or likes (1: >= 7), among the matches top first, found
// by reputation id (GetClientByRepId). 0 when nobody matches (no random number drawn) or no
// player has the id.
stock rep_random_client(id, rep_class)
{
	new ids[RC_MAX_REPS], n = 0;
	for (new k = g_rep_count[id] - 1; k >= 0; k--)
	{
		new r = g_rep_value[id][k];
		new bool:m = rep_class == -1 ? r <= 3 : (rep_class == 0 ? (r > 3 && r < 7) : (rep_class == 1 && r >= 7));
		if (m)
		{
			ids[n++] = g_rep_id[id][k];
		}
	}
	if (n == 0)
	{
		return 0;
	}
	new pick = ids[clamp(random_num(0, n - 1), 0, n - 1)];
	for (new p = 1; p <= MAX_PLAYERS; p++)
	{
		if (g_cl_i[p][CL_USED] && g_cl_i[p][CL_REP_ID] == pick)
		{
			return p;
		}
	}
	return 0;
}

// Reputation::describe (printRep): false for reputations outside 0-10.
stock bool:rep_describe(rep, const bot_name[], const player_name[], out[], len)
{
	static const what[11][] = {"really hates", "hates", "kind of hates", "doesn't like", "likes", "likes", "likes",
		"really likes", "loves", "really loves", "LURRVESS"};
	if (rep < 0 || rep > 10)
	{
		return false;
	}
	formatex(out, len, "%s %s %s%s (%d)", bot_name, what[rep], player_name, rep == 10 ? " !!!" : "", rep);
	return true;
}

// Reputations::print_reps (printReps) for util showreps: a line per reputation, top first, to
// player (0: the server console). A reputation no player has is skipped (upstream dereferences
// a NULL client).
stock rep_print_reps(id, to)
{
	new line[160], name[RC_NAME_LEN];
	for (new k = g_rep_count[id] - 1; k >= 0; k--)
	{
		new p = 0;
		for (new i = 1; i <= MAX_PLAYERS; i++)
		{
			if (g_cl_i[i][CL_USED] && g_cl_i[i][CL_REP_ID] == g_rep_id[id][k])
			{
				p = i;
				break;
			}
		}
		if (p == 0)
		{
			continue;
		}
		get_user_name(p, name, charsmax(name));
		if (rep_describe(g_rep_value[id][k], g_bot_name[id], name, line, charsmax(line)))
		{
			commands_print(to, line);
		}
	}
}

// BotFunc_DecreaseRep (decrease_rep_roll): with a distance (info >= 0) only if
// min(info / 800, 1) >= skill / 100; then (always without one) with chance
// RANDOM_LONG(0, 100) >= rep * 10.
stock bool:rep_decrease_roll(rep, Float:info, Float:skill)
{
	if (rep == -1)
	{
		return false;
	}
	if (info >= 0.0)
	{
		new Float:s = skill / float(MAX_BOT_SKILL);
		new Float:i = info / 800.0;
		if (i > 1.0)
		{
			i = 1.0;
		}
		if (i >= s)
		{
			return random_num(0, 100) >= rep * 10;
		}
		return false;
	}
	return random_num(0, 100) >= rep * 10;
}

// BotFunc_IncreaseRep: the opposite of one decrease roll (the same draw).
stock bool:rep_increase_roll(rep, Float:info, Float:skill)
{
	if (rep == -1)
	{
		return false;
	}
	return !rep_decrease_roll(rep, info, skill);
}

// ---- .rcr files --------------------------------------------------------------------------------

// rep_file_name in the profiles folder: botprofiles/<id>.rcr.
static rep_file_rel(pid, out[], len)
{
	formatex(out, len, "%s/%d.rcr", BOT_PROFILES_FOLDER, pid);
}

// The file's state: -1 no file, -2 a size that isn't a multiple of 8, else the record count.
static rep_file_records(const rel[])
{
	new path[RC_PATH_LEN];
	files_path(path, charsmax(path), rel);
	if (!file_exists(path))
	{
		return -1;
	}
	new size = file_size(path);
	if (size < 0)
	{
		return -1;
	}
	if (size % REP_RECORD_SIZE != 0)
	{
		return -2;
	}
	return size / REP_RECORD_SIZE;
}

// The file's record for rep_id (the first), false if it has none.
static bool:rep_file_lookup(const rel[], rep_id, &rep)
{
	new path[RC_PATH_LEN];
	files_path(path, charsmax(path), rel);
	new f = fopen(path, "rb");
	if (!f)
	{
		return false;
	}
	new rec[2], bool:found = false;
	while (fread_blocks(f, rec, 2, BLOCK_INT) == 2)
	{
		if (rec[0] == rep_id)
		{
			rep = rec[1];
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

// write_rep (CBotReputations::WriteToFile): the first record of the same player overwritten in
// place, else the record appended (or the file made with it); nothing for a file of the wrong
// size. Atomic: a copy is written and renamed over the file.
static rep_file_write(const rel[], rep_id, rep)
{
	new state_ = rep_file_records(rel);
	if (state_ == -2)
	{
		return;
	}
	new out = cfg_write_begin(rel);
	if (!out)
	{
		return;
	}
	new rec[2], bool:done = false;
	if (state_ >= 0)
	{
		new path[RC_PATH_LEN];
		files_path(path, charsmax(path), rel);
		new f = fopen(path, "rb");
		if (f)
		{
			while (fread_blocks(f, rec, 2, BLOCK_INT) == 2)
			{
				if (!done && rec[0] == rep_id)
				{
					rec[1] = rep;
					done = true;
				}
				fwrite_blocks(out, rec, 2, BLOCK_INT);
			}
			fclose(f);
		}
	}
	if (!done)
	{
		rec[0] = rep_id;
		rec[1] = rep;
		fwrite_blocks(out, rec, 2, BLOCK_INT);
	}
	cfg_write_end(out, rel);
}

// Reputations::add_load_rep (AddLoadRep): the player's reputation from the profile's file; a
// player it doesn't have starts at BOT_MID_REP, which is saved. A file of the wrong size is
// ignored and the player gets no reputation at all. Profile 0: no file.
static rep_add_load_rep(id, rep_id)
{
	new pid = g_pr_i[id][PR_ID];
	if (pid == 0)
	{
		rep_push(id, rep_id, BOT_MID_REP);
		return;
	}
	new rel[64];
	rep_file_rel(pid, rel, charsmax(rel));
	new n = rep_file_records(rel), rep;
	if (n == -2)
	{
		return;
	}
	if (n > 0 && rep_file_lookup(rel, rep_id, rep))
	{
		rep_push(id, rep_id, rep);
		return;
	}
	rep_push(id, rep_id, BOT_MID_REP);
	rep_file_write(rel, rep_id, BOT_MID_REP);
}

// host bots.rs new_bot: AddLoadRep for every player here (bots too, as upstream).
stock rep_load_all(id)
{
	g_rep_count[id] = 0;
	if (g_e2e && g_pr_i[id][PR_ID] != 0)
	{
		new rel[64], what[32];
		rep_file_rel(g_pr_i[id][PR_ID], rel, charsmax(rel));
		new n = rep_file_records(rel);
		if (n >= 0)
		{
			formatex(what, charsmax(what), "loaded: %d records", n);
		}
		copy(what, charsmax(what), n == -1 ? "missing" : (n == -2 ? "not parsed: the size isn't records" : what));
		report_file(rel, what);
	}
	for (new p = 1; p <= MAX_PLAYERS; p++)
	{
		if (g_cl_i[p][CL_USED])
		{
			rep_add_load_rep(id, g_cl_i[p][CL_REP_ID]);
		}
	}
}

// host bots.rs put_in_server: bot id loads its reputation of the new player.
stock rep_add_load(id, player)
{
	if (player >= 1 && player <= MAX_PLAYERS && g_cl_i[player][CL_USED])
	{
		rep_add_load_rep(id, g_cl_i[player][CL_REP_ID]);
	}
}

// host bots.rs disconnect (RemoveSaveRep): bot id saves its reputation of the leaving player (a
// profile's file) and drops it. Players without a reputation id (-1) are skipped.
stock rep_remove_save(id, player)
{
	new rid = rep_client_id(player);
	if (rid == NO_CLIENT || rid < 0)
	{
		return;
	}
	new k = rep_find(id, rid);
	if (k == -1)
	{
		return;
	}
	new rep = g_rep_value[id][k];
	new n = g_rep_count[id] - 1;
	for (new j = k; j < n; j++)
	{
		g_rep_id[id][j] = g_rep_id[id][j + 1];
		g_rep_value[id][j] = g_rep_value[id][j + 1];
	}
	g_rep_count[id] = n;
	if (g_pr_i[id][PR_ID] != 0)
	{
		new rel[64];
		rep_file_rel(g_pr_i[id][PR_ID], rel, charsmax(rel));
		rep_file_write(rel, rid, rep);
	}
}
