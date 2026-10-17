// rcbot/consts.inc: constants every part of the Pawn bot shares.
// Owner: core. Ports bot/mod.rs (buttons, cond, LookTask, Climb, Action, the BOT_* constants),
// bot/tasks.rs (TaskKind, ScheduleDesc), config/mod.rs (BOT_CONFIG_*, BOT_DEBUG_*, limits) and
// weapons/ts.rs (kung fu's id) of ../tsmod-rcbot-rs/crates/rcbot/src.
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// Subsystem-private constants go in the subsystem's own file, not here.

#if defined _rcbot_consts_included
	#endinput
#endif
#define _rcbot_consts_included

#define RC_PLUGIN			"RCBot AMXX"
#define RC_VERSION			"0.1.0"
#define RC_AUTHOR			"tsmod-amxx-rcbot"
// The port bot backend name (ts_bot_backend rcbot_amxx); also tsx's data directory name.
#define RC_BACKEND			"rcbot_amxx"
// config/mod.rs BOT_VER: the RCBot version the logic follows.
#define RC_BOT_VER			"1.51b14"
// config/mod.rs BOT_DBG_MSG_TAG.
#define RC_MSG_TAG			"[RCBOT] "

// ---- array sizes -----------------------------------------------------------------------------

// Per-bot arrays are indexed by player index 1..MAX_PLAYERS (amxconst.inc's MAX_PLAYERS, 32 =
// config/mod.rs MAX_PLAYERS); slot 0 is unused, as BotManager's.
#define RC_SLOTS			(MAX_PLAYERS + 1)
// Entities the per-frame cache and the visibility bits cover (GoldSrc's default edict limit is
// below this; indices above are ignored).
#define RC_MAX_EDICTS		2048
// weapons/mod.rs MAX_WEAPONS: ids 0..38 (38 = RCBot's kung fu).
#define RC_MAX_WEAPONS		39
// nav: the most waypoints a map has (nav/waypoints.rs MAX_WAYPOINTS).
#define RC_MAX_WAYPOINTS	1024
// Lengths of fixed strings.
#define RC_NAME_LEN			32
#define RC_PATH_LEN			128

// ---- bot/mod.rs constants --------------------------------------------------------------------

#define RELOAD_RETRY				2.0
#define BOT_DEFAULT_REACTION_TIME	0.3
#define BOT_DEFAULT_THINK_TIME		0.04
#define BOT_VIEW_DISTANCE			2048.0
#define BOT_MAX_REP					10
#define BOT_FACE_HURT_TIME			0.5
#define BOT_STUCK_CLIMB_SPEED		10.0
#define BOT_STUCK_SWIM_SPEED		32.0
#define BOT_COST_RANGE				64.0
#define BOT_COST_BUCKETS			7
#define TS_STATE_STUNT				2
// bot/think.rs TELEPORT_DISTANCE: a living bot moved further than this in a frame was teleported.
#define TELEPORT_DISTANCE			256.0
// bot/think.rs INVENTORY_SYNC: seconds between inventory reads without an inventory serial.
#define INVENTORY_SYNC				1.0
// bot/events.rs BOT_HEAR_DISTANCE.
#define BOT_HEAR_DISTANCE			650.0
// bot/events.rs NO_CLIENT: no reputation id.
#define NO_CLIENT					cellmin

// events.rs SoundKind (hearing.inc classifies; only PLAYER and PLAYER_PAIN are heard).
enum
{
	SOUND_UNKNOWN = 0,
	SOUND_PLAYER,
	SOUND_PLAYER_PAIN,
	SOUND_DOOR,
	SOUND_WEAPON,
	SOUND_OTHER
};

// config/profile.rs BOT_DEFAULT_NAME.
#define BOT_DEFAULT_NAME			"RCBot"

// config/mod.rs.
#define MAX_BOT_SKILL				100
#define MIN_BOT_SKILL				1
#define DEF_BOT_SKILL				50
#define TEAM_AUTOTEAM				5
#define DEFAULT_BOT_CHAT_PERCENT	15
#define BOT_WAYPOINT_TOUCH_DIST		32.0

// pev->movetype / solid / effects / deadflag values bot/mod.rs reads.
#define RC_MOVETYPE_WALK			3
#define RC_MOVETYPE_FLY				5
#define RC_SOLID_NOT				0
#define RC_SOLID_TRIGGER			1
#define RC_EF_NODRAW				128
#define RC_DEAD_NO					0

// weapons/ts.rs: RCBot numbers kung fu 38 (tsconst.inc's TSW_KUNG_FU); the game reports 0
// (tsbot.inc's TSBOT_KUNGFU). Convert with ts_weapon_from_game_id / ts_weapon_to_game_id.
#define RC_KUNGFU					38

// ---- eBotCondition (bot/mod.rs cond) ---------------------------------------------------------

#define COND_SEE_ENEMY				(1<<0)
#define COND_SEE_ENEMY_HEAD			(1<<1)
#define COND_SEE_ENEMY_BODY			(1<<2)
#define COND_ENEMY_OCCLUDED			(1<<3)
#define COND_ENEMY_DEAD				(1<<4)
#define COND_HAS_WEAPON				(1<<5)
#define COND_SEE_NEXT_WAYPOINT		(1<<6)
#define COND_SEE_TASK_EDICT			(1<<7)
#define COND_TASK_EDICT_NA			(1<<8)
#define COND_TASK_EDICT_PAIN		(1<<9)
#define COND_CANT_SHOOT				(1<<10)
#define COND_SELECTED_GUN			(1<<11)
#define COND_NEED_WEAPONS			(1<<12)
#define COND_SEE_SQUAD_LEADER		(1<<13)
#define COND_SQUAD_LEADER_DEAD		(1<<14)
#define COND_WANT_TO_LEAVE_GAME		(1<<15)

// ---- eBotLookTask (bot/mod.rs LookTask) ------------------------------------------------------

enum
{
	LOOK_NONE = 0,
	LOOK_FACE_NEAREST_REMEMBER_POS,
	LOOK_FACE_HURT_ORIGIN,
	LOOK_NEXT_WAYPOINT,
	LOOK_LOOK_AROUND,
	LOOK_SEARCH_FOR_LAST_ENEMY,
	LOOK_SEARCH_FOR_ENEMY,
	LOOK_FACE_ENEMY,
	LOOK_FACE_TASK_VECTOR,
	LOOK_FACE_TASK_EDICT,
	LOOK_PRE_AIM		// port addition (preaim.rs)
};

// ---- eClimbType (bot/mod.rs Climb) -----------------------------------------------------------

enum
{
	CLIMB_NONE = 0,
	CLIMB_CLIMBING,
	CLIMB_NOT_TOUCHING
};

// ---- eBotTask (bot/tasks.rs TaskKind) --------------------------------------------------------

enum
{
	TASK_NONE = 0,
	TASK_FIND_PATH,
	TASK_NORMAL_ATTACK,
	TASK_SEARCH_FOR_ENEMY,
	TASK_RELOAD,
	TASK_WAIT_AND_FACE_VECTOR,
	TASK_FACE_VECTOR,
	TASK_MOVE_TO_VECTOR,
	TASK_PICKUP_ITEM,
	TASK_USE,
	TASK_WAIT,
	TASK_DEFEND,
	TASK_LISTEN_TO_SOUND,
	TASK_CHANGE_WEAPON,
	TASK_DROP_WEAPON,
	TASK_CROUCH,
	TASK_PUSH_PUSHABLE,
	TASK_WAIT_FOR_LIFT,
	TASK_WAIT_FOR_ENTITY,
	TASK_HUMAN_TOWER,
	TASK_THROW_GRENADE,
	TASK_WAIT_FOR_BOT_AT_WPT,
	TASK_FACE_EDICT,
	TASK_FOLLOW_LEADER,
	TASK_TYPE_MESSAGE,
	TASK_KINDS
};

// eScheduleDesc (bot/tasks.rs ScheduleDesc).
enum
{
	SCHED_NONE = 0,
	SCHED_RUN_FOR_COVER,
	SCHED_USE_LIFT
};

// ---- Action (bot/mod.rs): what a bot asks of the game besides its usercmd ---------------------

enum
{
	ACT_NONE = 0,
	ACT_COMMAND,		// ts_bot_command(text)
	ACT_SELECT_WEAPON,	// ts_bot_select_weapon(arg: the game's id, 0 kung fu)
	ACT_LOADOUT,		// ts_bot_clear_loadout + ts_bot_buy of the bot's g_loadout_ids
	ACT_KILL,			// "kill"
	ACT_KICK			// ts_bot_kick
};

// ---- BOT_CONFIG_* (config/mod.rs flags) ------------------------------------------------------

#define CFG_WAIT_FOR_ORDERS			(1<<0)
#define CFG_DONT_SHOOT				(1<<1)
#define CFG_CHATTING				(1<<2)
#define CFG_REAL_MODE				(1<<3)
#define CFG_COMMANDING				(1<<4)
#define CFG_RESERVE_BOT_SLOTS		(1<<5)
#define CFG_CHAT_REPLY_TO_BOTS		(1<<6)
#define CFG_MARINE_AUTO_BUILD		(1<<7)
#define CFG_CHAT_DONT_LEARN			(1<<8)
#define CFG_BALANCE_TEAMS			(1<<9)
#define CFG_TOOLTIPS				(1<<10)
#define CFG_ENABLE_BOTCAM			(1<<11)
#define CFG_AUTOWAYPOINT_HUMANS		(1<<12)
#define CFG_UNSTICK					(1<<13)
#define CFG_ABNORMAL_GAME			(1<<14)
#define CFG_BOTS_LEAVE_AND_JOIN		(1<<15)
#define CFG_BOTS_WAIT_FOR_BOTS		(1<<16)
#define CFG_NOT_NS3_FINAL			(1<<17)
#define CFG_BLINKING				(1<<18)
#define CFG_WAIT_AT_RESOURCES		(1<<19)
#define CFG_DISABLE_WEAPON_LEARN	(1<<20)
#define CFG_TS_KUNGFU				(1<<21)
#define CFG_DISABLE_BOT_SQUADS		(1<<22)
#define CFG_TS_DONT_STEAL_WEAPONS	(1<<23)
#define CFG_TS_DONT_PICKUP_WEAPONS	(1<<24)
#define CFG_TACTICAL_PATHS			(1<<25)

// BOT_DEBUG_*_LEVEL (config/mod.rs debug).
#define DBG_TOUCH					(1<<0)
#define DBG_THINK					(1<<1)
#define DBG_HEAR					(1<<2)
#define DBG_MESSAGE					(1<<3)
#define DBG_BLOCK					(1<<4)
#define DBG_MOVE					(1<<5)
#define DBG_AIM						(1<<6)
#define DBG_NAV						(1<<7)
#define DBG_SEE						(1<<8)
