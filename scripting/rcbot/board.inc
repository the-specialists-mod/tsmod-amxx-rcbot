// rcbot/board.inc: each team's shared board: sightings, sounds, claims, paths (port addition, design §4).
// Owner: movement. Ports bot/board.rs (Sighting, TeamSound, Claim, TeamBoard sight / hear / claim / post / leave / expire / forget / mates / paths_of_others, TeamBoards get / get_mut / died / clear; SIGHTING_FOR, SOUND_FOR, POST_FOR).
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Status: the layout is defined; stubs, TODO(board).

#if defined _rcbot_board_included
	#endinput
#endif
#define _rcbot_board_included

#define BOARD_TEAMS				4
#define BOARD_SIGHTING_FOR		10.0
#define BOARD_MAX_SIGHTINGS		16
#define BOARD_MAX_SOUNDS		16
#define BOARD_MAX_CLAIMS		RC_SLOTS

// Sightings by team: who, where, when, by which bot.
stock g_bd_sight_ent[BOARD_TEAMS][BOARD_MAX_SIGHTINGS];
stock g_bd_sight_serial[BOARD_TEAMS][BOARD_MAX_SIGHTINGS];
stock g_bd_sight_by[BOARD_TEAMS][BOARD_MAX_SIGHTINGS];
stock Float:g_bd_sight_time[BOARD_TEAMS][BOARD_MAX_SIGHTINGS];
stock Float:g_bd_sight_origin[BOARD_TEAMS][BOARD_MAX_SIGHTINGS][3];
stock g_bd_sight_count[BOARD_TEAMS];
// Sounds by team.
stock g_bd_sound_by[BOARD_TEAMS][BOARD_MAX_SOUNDS];
stock Float:g_bd_sound_time[BOARD_TEAMS][BOARD_MAX_SOUNDS];
stock Float:g_bd_sound_loud[BOARD_TEAMS][BOARD_MAX_SOUNDS];
stock Float:g_bd_sound_origin[BOARD_TEAMS][BOARD_MAX_SOUNDS][3];
stock g_bd_sound_count[BOARD_TEAMS];
// Claims and members' posts are per bot (a bot is on one team): its claim and its post.
stock g_bd_claim_wpt[RC_SLOTS];
stock g_bd_claim_item[RC_SLOTS];
stock g_bd_claim_item_serial[RC_SLOTS];
stock Float:g_bd_claim_until[RC_SLOTS];
stock Float:g_bd_post_time[RC_SLOTS];
stock Float:g_bd_post_at[RC_SLOTS][3];

// TODO(board): TeamBoards::died: forget a dead player's sightings.
stock board_died(player, serial)
{
	#pragma unused player, serial
}

// TODO(board): TeamBoards::clear (map change).
stock board_clear()
{
}

// TODO(board): TeamBoard::sight.
stock board_sight(team, by, enemy, enemy_serial, const Float:origin[3])
{
	#pragma unused team, by, enemy, enemy_serial, origin
}

// TODO(board): TeamBoard::hear.
stock board_hear(team, by, const Float:origin[3], Float:loud)
{
	#pragma unused team, by, origin, loud
}

// TODO(board): TeamBoard::claim.
stock board_claim(by, waypoint, item, item_serial, Float:until)
{
	#pragma unused by, waypoint, item, item_serial, until
}

// TODO(board): TeamBoard::post (position and path).
stock board_post(by, const Float:at[3])
{
	#pragma unused by, at
}

// TODO(board): TeamBoard::leave.
stock board_leave(by)
{
	#pragma unused by
}

// TODO(board): TeamBoard::expire.
stock board_expire(team)
{
	#pragma unused team
}
