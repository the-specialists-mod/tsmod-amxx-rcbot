// rcbot/files.inc: where RCBot's files are, and the map name.
// Owner: core. Ports host lib.rs game_init's data directory (api.data_dir()), bots.rs
// State::path / read, and server_activate's map name.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Two folders:
//  * g_rcbot_dir, "tsmod/rcbot/" (relative to the game directory, which AMX Mod X's file natives
//    resolve against): RCBot's shared data folder. The modules (rcbot_megahal, rcbot_learn,
//    rcbot_nav) read and write here, and the standalone tsmod_rcbot uses the same folder and
//    names, so bot_profiles.ini, botprofiles/<id>.ini, bot_weapons.ini, bot_chat.ini,
//    bot_config.ini, bot_users.ini, botprofiles/<key>.rkf are read from (and written to) it too.
//  * g_backend_dir, ts_bot_data_dir(): the backend's own folder (<gamedir>/tsmod/rcbot_amxx/,
//    made by tsx). Files only this plugin has; also the fallback for reading the ini files when
//    the shared folder lacks one.
// Paths are built at load and map start only, never per frame.

#if defined _rcbot_files_included
	#endinput
#endif
#define _rcbot_files_included

new g_rcbot_dir[RC_PATH_LEN] = "tsmod/rcbot/";
new g_backend_dir[RC_PATH_LEN];
new g_map[64];

// plugin_init, after ts_bot_register_backend (tsx makes the backend folder then).
stock files_init()
{
	get_mapname(g_map, charsmax(g_map));
	if (ts_bot_data_dir(g_backend_dir, charsmax(g_backend_dir)) < 0)
	{
		g_backend_dir[0] = 0;
	}
}

// <rcbot dir><rel> into out.
stock files_path(out[], len, const rel[])
{
	formatex(out, len, "%s%s", g_rcbot_dir, rel);
}

// The file to read for rel: the shared folder's, else the backend folder's. False if neither
// exists (out then holds the shared folder's path).
stock bool:files_find(out[], len, const rel[])
{
	files_path(out, len, rel);
	if (file_exists(out))
	{
		return true;
	}
	if (g_backend_dir[0])
	{
		new alt[RC_PATH_LEN];
		formatex(alt, charsmax(alt), "%s%s", g_backend_dir, rel);
		if (file_exists(alt))
		{
			copy(out, len, alt);
			return true;
		}
	}
	return false;
}

// Reads a whole small text file (an ini) into out; returns its length, -1 if missing.
stock files_read_text(const rel[], out[], len)
{
	new path[RC_PATH_LEN];
	if (!files_find(path, charsmax(path), rel))
	{
		out[0] = 0;
		return -1;
	}
	new f = fopen(path, "rt");
	if (!f)
	{
		out[0] = 0;
		return -1;
	}
	new n = fread_blocks(f, out, len - 1, BLOCK_CHAR);
	fclose(f);
	if (n < 0)
	{
		n = 0;
	}
	out[n] = 0;
	return n;
}
