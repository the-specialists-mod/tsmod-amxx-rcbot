// rcbot/state.inc: every bot's state, struct Bot (bot/mod.rs) and the structs it holds, as
// enum-indexed per-bot arrays (docs/pawn.md "Data layout").
// RCBot by Paul "Cheeseh" Murphy; tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot
// linking exceptions.
//
// One section per owning subsystem, each with its own enums and arrays, so subsystem agents
// add fields inside their own section only (append at the end of their enums). Per section:
//   enum Xx_I { ... }   integer/bool/handle fields       new g_xx_i[RC_SLOTS][Xx_I]
//   enum Xx_F { ... }   Float: fields                    new Float:g_xx_f[RC_SLOTS][Xx_F]
//   enum Xx_V { ... }   Float:[3] vectors                new Float:g_xx_v[RC_SLOTS][Xx_V][3]
// Lists (Rust Vec) are fixed arrays with a count; Option<T> is a sentinel (-1 for indices and
// ids, a *_SET flag for vectors and floats); EntRef is an (index, serial) pair of int fields
// (serial: the player's user id, an item's TSITM_SERIAL, else 0). Arrays nobody reads yet are
// `stock` (no memory, no "never used" warning until a subsystem uses them).
//
// Reset per life: CBot::SpawnInit (think.rs spawn_init) is think's think_spawn_init, which
// calls each subsystem's *_spawn_init. Reset per bot: state_clear_bot (below), at creation.

#if defined _rcbot_state_included
	#endinput
#endif
#define _rcbot_state_included

// ============================================================================================
// core (owner: core; manager.inc, backend.inc). bot/mod.rs Bot identity, kick/kill, button,
// v_angle, game_view; think.rs Bot::frame's teleport check; the frame's usercmd (BotCmd) and
// actions (Frame::actions); lib.rs/bots.rs per-bot host data.
// ============================================================================================

enum Co_I
{
	CO_USED,			// the slot holds one of our bots (BotManager's Some)
	CO_USERID,			// its user id when added (a new player in the slot isn't it)
	CO_KICK,			// m_bKick
	CO_KILL,			// m_bKill
	CO_BUTTON,			// pev->button being built this frame (IN_*)
	CO_GAME_VIEW_SET,	// game_view: Some (TSBOT_EV_FIXANGLE)
	CO_PREV_ALIVE,		// alive at the last frame (think.rs frame's was_alive)
	CO_HAS_CMD,			// the frame produced a usercmd (Frame::cmd is Some)
	CO_CMD_BUTTONS,		// BotCmd::buttons
	CO_CMD_IMPULSE,		// BotCmd::impulse
	CO_ACT_COUNT,		// actions queued this frame
	CO_LOADOUT_COUNT,	// weapons in g_loadout_ids for ACT_LOADOUT
	CO_ADDED_BY,		// RC_ADDED_* (how it joined)
	CO_LAST_EVENTS		// TSBOT_EV_* of the last ts_bot_runcmd
};
new g_co_i[RC_SLOTS][Co_I];

enum Co_F
{
	Float:CO_NOW,		// Bot::now: this frame's time
	Float:CO_CMD_FWD,	// BotCmd::forward
	Float:CO_CMD_SIDE,	// BotCmd::side
	Float:CO_CMD_UP		// BotCmd::up
};
new Float:g_co_f[RC_SLOTS][Co_F];

enum Co_V
{
	CO_VANGLE,			// Bot::v_angle
	CO_GAME_VIEW,		// Bot::game_view
	CO_PREV_ORIGIN,		// think.rs frame's was_at
	CO_CMD_ANGLES		// BotCmd::angles
};
new Float:g_co_v[RC_SLOTS][Co_V][3];

// How a bot joined (CO_ADDED_BY).
enum
{
	RC_ADDED_QUOTA = 1,	// ts_bot_add (ts_bot_quota, addbot, addcustombot)
	RC_ADDED_RCBOT,		// rcbot addbot, the rejoin code (BotFunc_AddBot)
	RC_ADDED_ADOPT		// ts_bot_adopt (kept across a level change)
};

// State::active: we are the port's bot backend now (backend.inc's forwards set it).
new bool:g_active;

// m_szBotName.
new g_bot_name[RC_SLOTS][RC_NAME_LEN];

// The frame's actions (Frame::actions), run in order after every bot thought (lib.rs:516-548).
#define RC_MAX_ACTIONS		6
#define RC_ACTION_TEXT		128
#define RC_MAX_LOADOUT		8
new g_act_kind[RC_SLOTS][RC_MAX_ACTIONS];				// ACT_*
new g_act_arg[RC_SLOTS][RC_MAX_ACTIONS];				// ACT_SELECT_WEAPON's id
new g_act_text[RC_SLOTS][RC_MAX_ACTIONS][RC_ACTION_TEXT];	// ACT_COMMAND's line
new g_loadout_ids[RC_SLOTS][RC_MAX_LOADOUT];			// Action::Loadout's ids (RCBot ids)

// ============================================================================================
// profile (owner: social; profile.inc, rep.inc). config/profile.rs BotProfile (m_Profile),
// config/rep.rs Reputations (m_Profile.m_Rep).
// ============================================================================================

enum Pr_I
{
	PR_ID,				// m_iProfileId: 0 none
	PR_FAV_MOD,
	PR_FAV_TEAM,		// TEAM_AUTOTEAM for any
	PR_SKILL,			// 1..100
	PR_NUM_GAMES,
	PR_BOTTOM_COLOUR,
	PR_TOP_COLOUR,
	PR_PATH_REVS,
	PR_VISION_REVS,
	PR_CLASS			// -1 none
};
new g_pr_i[RC_SLOTS][Pr_I];

enum Pr_F
{
	Float:PR_AIM_SPEED,
	Float:PR_AIM_SKILL,
	Float:PR_AIM_TIME,
	Float:PR_VISION_TIME
};
new Float:g_pr_f[RC_SLOTS][Pr_F];

// m_szModel (empty: none); m_szHAL_*File (empty: none; PR_HAL_* order).
enum { PR_HAL_PRETRAIN = 0, PR_HAL_AUX, PR_HAL_BAN, PR_HAL_SWAP, PR_HAL_FILES };
new g_pr_model[RC_SLOTS][RC_NAME_LEN];
stock g_pr_hal_file[RC_SLOTS][PR_HAL_FILES][64];

// Reputations: player rep id -> rep (0..BOT_MAX_REP), unordered, g_rep_count entries.
#define RC_MAX_REPS			64
stock g_rep_id[RC_SLOTS][RC_MAX_REPS];
stock g_rep_value[RC_SLOTS][RC_MAX_REPS];
stock g_rep_count[RC_SLOTS];

// ============================================================================================
// think (owner: think; think.inc, goals.inc, special.inc). think.rs Bot::think, spawn_init,
// look_for_new_tasks, objectives; mod.rs conditions.
// ============================================================================================

enum Th_I
{
	TH_CONDITIONS,			// COND_* bits
	TH_LOOK_FOR_NEW_TASKS,
	TH_WANT_TO_FIND_ENEMY,
	TH_TEAM,				// RCBot's team from 0 (rules team - 1), -1 none
	TH_NEED_TO_INIT,
	TH_NUM_DEATHS,
	TH_FRAGS_SINCE_DEATH,
	TH_PREV_TEAM_SCORE,
	TH_USED_MELEE,
	TH_PICKUP_ENT,			// pickup_entity: EntRef, 0 none
	TH_PICKUP_SERIAL,
	TH_OBJ_ATTEMPT,			// objective_attempt: id, -1 none
	TH_OBJ_FAILED_COUNT,
	TH_SEARCH_POINT_SET		// search_point: Some
};
new g_th_i[RC_SLOTS][Th_I];

enum Th_F
{
	Float:TH_THINK_TIME,
	Float:TH_LAST_THINK_TIME,
	Float:TH_REACTION_TIME,
	Float:TH_LAST_UPDATE_CONDITIONS,
	Float:TH_VELOCITY_3D,
	Float:TH_VELOCITY_2D,
	Float:TH_SPAWN_TIME,
	Float:TH_RESPAWN_TIME,
	Float:TH_PICKUP_ITEM_TIME,
	Float:TH_CHAT_MESSAGE_TIME,
	Float:TH_LISTEN_TO_SOUND_TIME,
	Float:TH_INVESTIGATE_SOUND_TIME,
	Float:TH_PREV_HEALTH,
	Float:TH_HOLD_AGAIN,
	Float:TH_OBJ_ATTEMPT_UNTIL
};
new Float:g_th_f[RC_SLOTS][Th_F];

enum Th_V
{
	TH_SPAWN_POSITION,
	TH_SEARCH_POINT
};
new Float:g_th_v[RC_SLOTS][Th_V][3];

// objective_failed: (objective id, defended until).
#define RC_MAX_OBJ_FAILED	8
stock g_th_obj_failed_id[RC_SLOTS][RC_MAX_OBJ_FAILED];
stock Float:g_th_obj_failed_until[RC_SLOTS][RC_MAX_OBJ_FAILED];

// ============================================================================================
// tasks (owner: think; tasks.inc). tasks.rs Tasks: a queue, current first (index 0).
// ============================================================================================

#define RC_MAX_TASKS		16

enum Tk_I
{
	TK_KIND,				// TASK_*
	TK_INT,					// m_iInfo
	TK_EDICT,				// m_pInfo: EntRef index, 0 none
	TK_EDICT_SERIAL,
	TK_SCHEDULE_ID,			// 0 none
	TK_SCHEDULE_DESC,		// SCHED_*
	TK_FOUND_PATH
};
stock g_tk_i[RC_SLOTS][RC_MAX_TASKS][Tk_I];

enum Tk_F
{
	Float:TK_FLOAT,			// m_fInfo
	Float:TK_TIME_TO_COMPLETE	// -1.0 no limit
};
stock Float:g_tk_f[RC_SLOTS][RC_MAX_TASKS][Tk_F];
stock Float:g_tk_vec[RC_SLOTS][RC_MAX_TASKS][3];		// m_vInfo
stock g_tk_count[RC_SLOTS];

// ============================================================================================
// look (owner: combat; look.inc, preaim.inc). look.rs work_view_angles, set_view_angles,
// turn_to; mod.rs look_task, ideal_*, turn_speed, ladder_angles, hurt_*.
// ============================================================================================

enum Lk_I
{
	LK_LOOK_TASK,			// LOOK_*
	LK_LADDER_ANGLES_SET
};
new g_lk_i[RC_SLOTS][Lk_I];

enum Lk_F
{
	Float:LK_IDEAL_YAW,
	Float:LK_IDEAL_PITCH,
	Float:LK_TURN_SPEED,
	Float:LK_BASE_TURN_SPEED,
	Float:LK_LAST_LOOK_TIME,
	Float:LK_SEARCH_ENEMY_TIME,
	Float:LK_HURT_TIME
};
new Float:g_lk_f[RC_SLOTS][Lk_F];

enum Lk_V
{
	LK_LADDER_ANGLES,
	LK_CURRENT_LOOK_DIR,
	LK_HURT_ORIGIN
};
new Float:g_lk_v[RC_SLOTS][Lk_V][3];

// preaim.rs PreAim.
enum Pa_F
{
	Float:PA_UNTIL,
	Float:PA_WATCH_UNTIL
};
stock Float:g_pa_f[RC_SLOTS][Pa_F];

enum Pa_V
{
	PA_POINT,
	PA_WATCH
};
stock Float:g_pa_v[RC_SLOTS][Pa_V][3];

// PreAim::checked: (waypoint, when).
#define RC_MAX_PREAIM_CHECKED	16
stock g_pa_checked_wpt[RC_SLOTS][RC_MAX_PREAIM_CHECKED];
stock Float:g_pa_checked_time[RC_SLOTS][RC_MAX_PREAIM_CHECKED];
stock g_pa_checked_count[RC_SLOTS];

// ============================================================================================
// movement (owner: movement; movement.inc). movement.rs (Unstick, work_move_direction,
// check_stuck, dive, run_player_move); mod.rs move_*, avoid_*, duck/jump times.
// ============================================================================================

enum Mv_I
{
	MV_MOVE_TO_VALID,
	MV_NOT_FOLLOWING_WAYPOINT,
	MV_AVOID_ENT,			// avoid_entity: EntRef, 0 none
	MV_AVOID_SERIAL,
	MV_STUCK_SUICIDES,
	MV_UNSTICK_LEVEL,
	MV_UNSTICK_EDGE_PREV,
	MV_UNSTICK_EDGE_CUR,
	MV_UNSTICK_REACTED
};
new g_mv_i[RC_SLOTS][Mv_I];

enum Mv_F
{
	Float:MV_MOVE_SPEED,
	Float:MV_STRAFE_SPEED,
	Float:MV_UP_SPEED,
	Float:MV_IDEAL_MOVE_SPEED,
	Float:MV_MAX_SPEED,
	Float:MV_UP_TIME,
	Float:MV_STRAFE_TIME,
	Float:MV_AVOID_TIME,
	Float:MV_START_DUCK_TIME,
	Float:MV_END_DUCK_TIME,
	Float:MV_START_JUMP_TIME,
	Float:MV_END_JUMP_TIME,
	Float:MV_HOLD_ATTACK_TIME,
	Float:MV_LAST_NO_MOVE,
	Float:MV_STUCK_TIME,
	Float:MV_UNSTICK_SINCE,
	Float:MV_UNSTICK_LAST,
	Float:MV_UNSTICK_BACK_UNTIL,
	Float:MV_UNSTICK_BACK_SIDE,
	Float:MV_DIVE_TIME
};
new Float:g_mv_f[RC_SLOTS][Mv_F];

enum Mv_V
{
	MV_MOVE_TO,
	MV_AVOID_MOVE_TO,
	MV_UNSTICK_AT
};
new Float:g_mv_v[RC_SLOTS][Mv_V][3];

// ============================================================================================
// navigate (owner: movement; navigate.inc). navigate.rs; mod.rs current/prev/goal waypoint,
// touched_waypoints. The waypoints, visibility, heatmap and each bot's A* state (the path stack,
// failed paths and goals, tactics inputs, goals.rs Explore's visits) are the rcbot_nav module's:
// one RcPath per bot (NV_PATH).
// ============================================================================================

enum Nv_I
{
	NV_CURRENT_WPT,			// -1 none
	NV_CURRENT_WPT_FLAGS,
	NV_PREV_WPT,
	NV_GOAL_WPT,
	NV_LAST_FAILED_WPT,
	NV_GOAL_ORIGIN_SET,
	NV_TOUCHED_WPTS,
	NV_PATH					// RcPath (nav_path_create), 0 none
};
new g_nv_i[RC_SLOTS][Nv_I];

enum Nv_F
{
	Float:NV_PREV_WPT_DIST,
	Float:NV_LAST_SEE_WPT,
	Float:NV_UPDATE_WPT_TIME,
	Float:NV_FIND_PATH_TIME
};
new Float:g_nv_f[RC_SLOTS][Nv_F];

enum Nv_V
{
	NV_GOAL_ORIGIN
};
new Float:g_nv_v[RC_SLOTS][Nv_V][3];

// ============================================================================================
// combat (owner: combat; combat.inc). combat.rs (Visibles, enemy choice, range costs).
// ============================================================================================

#define RC_MAX_VISIBLES		64
#define RC_VIS_WORDS		(RC_MAX_EDICTS / 32)

enum Cb_I
{
	CB_ENEMY,				// enemy: EntRef, 0 none
	CB_ENEMY_SERIAL,
	CB_LAST_ENEMY,
	CB_LAST_ENEMY_SERIAL,
	CB_ENEMY_REP,
	CB_ENEMY_REP_SET,		// enemy_rep: Some
	CB_ZOOM,
	CB_VIS_UPDATE_REVS,
	CB_LOOKING_FOR_ENEMY,
	CB_VIS_COUNT,			// g_cb_vis_order entries
	CB_NEAREST_REMEMBER_VISIBLE
};
new g_cb_i[RC_SLOTS][Cb_I];

enum Cb_F
{
	Float:CB_NEXT_GET_ENEMY_TIME,
	Float:CB_LAST_SEE_ENEMY_TIME,
	Float:CB_FIRST_SEE_ENEMY_TIME,
	Float:CB_GET_AIM_VECTOR_TIME,
	Float:CB_DISTANCE_FROM_WALL,
	Float:CB_UPDATE_VISIBLES_TIME,
	Float:CB_NEAREST_AVOID_DIST,
	Float:CB_LOWEST_ENEMY_COST,
	Float:CB_NEXT_WORK_RANGE_COSTS
};
new Float:g_cb_f[RC_SLOTS][Cb_F];

enum Cb_V
{
	CB_LAST_SEE_ENEMY_POS,
	CB_LOWEST_ENEMY_COST_VEC,
	CB_NEAREST_REMEMBER_POINT
};
new Float:g_cb_v[RC_SLOTS][Cb_V][3];

// Visibles: the order things came into view, and a bit per entity index.
stock g_cb_vis_order[RC_SLOTS][RC_MAX_VISIBLES];
stock g_cb_vis_bits[RC_SLOTS][RC_VIS_WORDS];
// range_costs[BOT_COST_BUCKETS][BOT_COST_BUCKETS].
stock Float:g_cb_range_costs[RC_SLOTS][BOT_COST_BUCKETS][BOT_COST_BUCKETS];

// ============================================================================================
// fire (owner: combat; fire.inc). fire.rs Fight: trigger, recoil, aim error, dodge, strafe.
// ============================================================================================

enum Fi_I
{
	FI_SENT,				// attack bits of the last usercmd
	FI_AKIMBO_LEFT,
	FI_SHOT_WEAPON,
	FI_SHOT_CLIP,
	FI_AIM_TARGET_SET,
	FI_AIM_TRUE_SET,
	FI_LAST_IDEAL_SET,
	FI_ATTACKER,			// attacker: index, 0 none
	FI_DIVE_AT_SET
};
new g_fi_i[RC_SLOTS][Fi_I];

enum Fi_F
{
	Float:FI_NEXT_PRESS,
	Float:FI_LAST_SHOT,
	Float:FI_PENALTY,
	Float:FI_PENALTY_TIME,
	Float:FI_ERROR_DIR,
	Float:FI_ERROR_SIZE,
	Float:FI_ON_TARGET_SINCE,
	Float:FI_TURN_RATE,
	Float:FI_LAST_TURN,
	Float:FI_LAST_IDEAL_PITCH,
	Float:FI_LAST_IDEAL_YAW,
	Float:FI_REACT_AT,
	Float:FI_STRAFE_DIR,
	Float:FI_STRAFE_UNTIL,
	Float:FI_SPEED_CAP,
	Float:FI_SPEED_CAP_UNTIL,
	Float:FI_SPREAD_LIMIT,
	Float:FI_SPREAD_LIMIT_UNTIL,
	Float:FI_NEXT_DIVE,
	Float:FI_DIVE_AT,
	Float:FI_ROLL_UNTIL,
	Float:FI_ATTACKER_TIME,
	Float:FI_NEXT_WEAPON_CHOICE,
	Float:FI_LAST_SIGHT,
	Float:FI_HOLD_UNTIL,
	Float:FI_NEXT_GRENADE,
	Float:FI_NEXT_POWERUP
};
new Float:g_fi_f[RC_SLOTS][Fi_F];

enum Fi_V
{
	FI_AIM_TARGET,
	FI_AIM_TRUE,
	FI_SIGHT_VELOCITY,
	FI_SIGHT_HEAD
};
new Float:g_fi_v[RC_SLOTS][Fi_V][3];

// ============================================================================================
// remember (owner: movement; remember.inc). remember.rs RememberPositions: oldest first.
// ============================================================================================

#define RC_MAX_REMEMBER		8

enum Rm_I
{
	RM_ENT,					// entity: EntRef, 0 none
	RM_SERIAL,
	RM_FLAGS,
	RM_HUNTED
};
stock g_rm_i[RC_SLOTS][RC_MAX_REMEMBER][Rm_I];
stock Float:g_rm_time[RC_SLOTS][RC_MAX_REMEMBER];
stock Float:g_rm_origin[RC_SLOTS][RC_MAX_REMEMBER][3];
stock Float:g_rm_visible_origin[RC_SLOTS][RC_MAX_REMEMBER][3];
stock g_rm_count[RC_SLOTS];
stock Float:g_rm_now[RC_SLOTS];

// ============================================================================================
// hearing (owner: movement; hearing.inc). hearing.rs Hearing: the sounds of the last HEARD_FOR s.
// ============================================================================================

#define RC_MAX_SOUNDS		8

enum Hr_I
{
	HR_SOURCE,				// source: EntRef, 0 none
	HR_SOURCE_SERIAL,
	HR_INVESTIGATED
};
stock g_hr_i[RC_SLOTS][RC_MAX_SOUNDS][Hr_I];

enum Hr_F
{
	Float:HR_TIME,
	Float:HR_LOUD
};
stock Float:g_hr_f[RC_SLOTS][RC_MAX_SOUNDS][Hr_F];
stock Float:g_hr_origin[RC_SLOTS][RC_MAX_SOUNDS][3];
stock g_hr_count[RC_SLOTS];

// ============================================================================================
// items (owner: movement; items.inc). items.rs ItemMemory / KnownItem.
// ============================================================================================

#define RC_MAX_KNOWN_ITEMS	48

enum It_I
{
	IT_ENT,					// handle: EntRef
	IT_SERIAL,
	IT_KIND,				// TSENT_*
	IT_WEAPON,
	IT_POWERUP
};
stock g_it_i[RC_SLOTS][RC_MAX_KNOWN_ITEMS][It_I];

enum It_F
{
	Float:IT_AVAILABLE_AT,
	Float:IT_GONE_SEEN,
	Float:IT_RESPAWN,
	Float:IT_IGNORE_UNTIL,
	Float:IT_TOUCHED_AT
};
stock Float:g_it_f[RC_SLOTS][RC_MAX_KNOWN_ITEMS][It_F];
stock Float:g_it_origin[RC_SLOTS][RC_MAX_KNOWN_ITEMS][3];
stock g_it_count[RC_SLOTS];
stock Float:g_it_next_scan[RC_SLOTS];
stock Float:g_it_next_look[RC_SLOTS];

// ============================================================================================
// botweapons (owner: combat; botweapons.inc). bot/weapons.rs BotWeapons (CBotWeapon by RCBot
// id 0..38) and mod.rs current_weapon, last_weapon_info, switch_pending, inventory sync, reload.
// ============================================================================================

enum Bw_I
{
	BW_ID,					// m_iId, 0 not set
	BW_CLIP,
	BW_RESERVE,
	BW_MAX_CLIP,
	BW_HAS_WEAPON			// m_bHasWeapon (registered: weapons.inc g_wp_*)
};
new g_bw_weapon[RC_SLOTS][RC_MAX_WEAPONS][Bw_I];

enum Bw_I2
{
	BW_CURRENT,				// current_weapon: RCBot id, -1 none
	BW_LAST_INFO_SET,		// last_weapon_info: Some
	BW_LAST_WEAPON,			// its game id, clip, ammo
	BW_LAST_CLIP,
	BW_LAST_AMMO,
	BW_SWITCH_PENDING,		// switch_pending: RCBot id, -1 none
	BW_INVENTORY_SERIAL
};
new g_bw_i[RC_SLOTS][Bw_I2];

enum Bw_F
{
	Float:BW_SWITCH_UNTIL,
	Float:BW_NEXT_INVENTORY_SYNC,
	Float:BW_NEXT_RELOAD_TIME
};
new Float:g_bw_f[RC_SLOTS][Bw_F];

// ============================================================================================
// kungfu (owner: kungfu; kungfu.inc). kungfu.rs KungFu (learned, saved in botprofiles/<key>.rkf)
// and its Fight (not saved). Arms (learn/bandit.rs Arm) are (value, n) float pairs.
// ============================================================================================

#define KF_OPP_CLASSES		4
#define KF_STANCES			2
#define KF_ATTACK_KINDS		5
#define KF_ANSWERS			5
#define KF_MAX_COMBOS		14
#define KF_MAX_COMBO_LEN	5
#define KF_ARM_VALUE		0
#define KF_ARM_N			1

// KungFu::stances[class][stance], stance_all[stance], reactions[kind][answer], reaction_all.
stock Float:g_kf_stances[RC_SLOTS][KF_OPP_CLASSES * KF_STANCES][2];
stock Float:g_kf_stance_all[RC_SLOTS][KF_STANCES][2];
stock Float:g_kf_reactions[RC_SLOTS][KF_ATTACK_KINDS * KF_ANSWERS][2];
stock Float:g_kf_reaction_all[RC_SLOTS][KF_ANSWERS][2];
// KungFu::combos: moves (kungfu.rs Move codes), length, value, uses.
stock g_kf_combo_moves[RC_SLOTS][KF_MAX_COMBOS][KF_MAX_COMBO_LEN];
stock g_kf_combo_len[RC_SLOTS][KF_MAX_COMBOS];
stock Float:g_kf_combo_value[RC_SLOTS][KF_MAX_COMBOS];
stock g_kf_combo_uses[RC_SLOTS][KF_MAX_COMBOS];
stock g_kf_combo_count[RC_SLOTS];
stock g_kf_finished[RC_SLOTS];
// KungFu::opponents (PerOpponent, this map), by opponent player index.
stock Float:g_kf_opp_stances[RC_SLOTS][RC_SLOTS][KF_STANCES * 2];
stock Float:g_kf_opp_reactions[RC_SLOTS][RC_SLOTS][KF_ATTACK_KINDS * KF_ANSWERS * 2];

// kungfu.rs Fight.
enum Kf_I
{
	KF_OPPONENT,			// 0 none
	KF_OPP_HUMAN,
	KF_STANCE,				// -1 none
	KF_STANCE_CLASS,
	KF_COMBO,				// running combo index, -1 none
	KF_COMBO_STEP,
	KF_PHASE,				// airborne move: 0 not started, 1 jump pressed, 2 in the air
	KF_FACE_SET,
	KF_WALL_SET,
	KF_LAST_BUTTONS,
	KF_OPP_BUTTONS,
	KF_PENDING_SET,
	KF_PENDING_SEEN,
	KF_PENDING_KIND,
	KF_REACTING,			// Answering::answer, -1 none
	KF_REACT_SEEN,
	KF_REACT_KIND,
	KF_OPP_BLOCKS
};
stock g_kf_i[RC_SLOTS][Kf_I];

enum Kf_F
{
	Float:KF_UPDATED,
	Float:KF_STANCE_UNTIL,
	Float:KF_STANCE_START,
	Float:KF_COMBO_AT,
	Float:KF_COMBO_START,
	Float:KF_PHASE_TIME,
	Float:KF_OPP_LAST_ATTACK,
	Float:KF_OPP_INTERVAL,
	Float:KF_OPP_BLOCK_UNTIL,
	Float:KF_OPP_WINDOW,
	Float:KF_PENDING_AT,
	Float:KF_REACT_UNTIL,
	Float:KF_REACT_START,
	Float:KF_REACT_JUDGE_AT,
	Float:KF_BLOCK_UNTIL,
	Float:KF_RECOVER_UNTIL,
	Float:KF_COUNTER_UNTIL,
	Float:KF_POWER,
	Float:KF_POWER_TIME,
	Float:KF_SIDE
};
stock Float:g_kf_f[RC_SLOTS][Kf_F];

enum Kf_V
{
	KF_FACE,
	KF_WALL
};
stock Float:g_kf_v[RC_SLOTS][Kf_V][3];
stock g_kf_opp_attacks[RC_SLOTS][KF_ATTACK_KINDS];

// ============================================================================================
// learning (owner: kungfu; learning.inc). learning.rs BotLearning; the rcbot_learn
// handle (BotLearnedData, ledger, weapon stats and loadout bandit live in the module).
// ============================================================================================

enum Ln_I
{
	LN_HANDLE,				// RcLearn (rl_bot_load), 0 none
	LN_PENDING_LOADOUT,		// -1 none
	LN_CREDITED_LOADOUT,	// -1 none
	LN_CREDIT_CHECKED,
	LN_LIFE_KILLS,
	LN_LAST_CLIP_SET,		// last_clip: Some((weapon, clip))
	LN_LAST_CLIP_WEAPON,
	LN_LAST_CLIP
};
new g_ln_i[RC_SLOTS][Ln_I];

enum Ln_F
{
	Float:LN_LIFE_DEALT,
	Float:LN_LIFE_START,
	Float:LN_LAST_TICK,
	Float:LN_LAST_ENEMY_DIST
};
new Float:g_ln_f[RC_SLOTS][Ln_F];

// ============================================================================================
// squad (owner: social; squad.inc). squad.rs Bot's squad fields.
// ============================================================================================

enum Sq_I
{
	SQ_SQUAD,				// squad id (squad.inc), -1 none
	SQ_LEADER,				// squad_leader: EntRef, 0 none
	SQ_LEADER_SERIAL,
	SQ_SAID_MOVE_OUT,
	SQ_SAID_IN_POSITION
};
new g_sq_i[RC_SLOTS][Sq_I];

enum Sq_F
{
	Float:SQ_NEXT_USE_SAY_MESSAGE
};
new Float:g_sq_f[RC_SLOTS][Sq_F];

// ============================================================================================
// events / chat (owner: social; events.inc, chat.inc). events.rs boredom, greetings, leaving;
// the MegaHAL brain (m_Profile.m_HAL, rcbot_megahal); the line being typed (chat_string).
// ============================================================================================

enum Ev_I
{
	EV_BOREDOM,				// m_iBoredom 0..255
	EV_SAID_GREETINGS_TO,	// bit per player index
	EV_HAL					// RcHal (hal_load), 0 none
};
new g_ev_i[RC_SLOTS][Ev_I];

enum Ev_F
{
	Float:EV_JOIN_SERVER_TIME,
	Float:EV_LEAVE_TIME
};
new Float:g_ev_f[RC_SLOTS][Ev_F];

stock g_ev_chat_string[RC_SLOTS][RC_ACTION_TEXT];

// ============================================================================================
// client (owner: social; client.inc, commands.inc, menu.inc, rep.inc). config/client.rs Client
// (CClient's command part) per player index 1..32 (CClients' slot ENTINDEX - 1), host bots.rs
// State::rep_ids and lib.rs force_grips. Here, not in client.inc, because rep.inc and menu.inc
// (included before client.inc) read it.
// ============================================================================================

enum Cl_I
{
	CL_USED,				// m_pPlayer: Some
	CL_ACCESS,				// m_iAccessLevel: CMD_ACCESS_* bits
	CL_REP_ID,				// m_iPlayerId: the reputation id, -1 none
	CL_RECHECK_AUTH,		// m_bRecheckAuth
	CL_MENU,				// m_pMenu: MENU_* (menu.inc), -1 none
	CL_LAST_KEY,			// m_iLastMenuItemSelected, -1 none
	CL_CURRENT_WPT,			// m_iCurrentWaypoint, -1 none
	CL_WAYPOINT_ON,			// m_bWaypointOn
	CL_PATHS_ON,			// m_bWaypointPathsOn
	CL_DEBUG_BOT,			// m_iDebugBot: bot index (player - 1), -1 none
	CL_DEBUG_ENT,			// m_pDebugEnt: entity index, 0 none
	CL_TELEPORT_SET,		// m_vTeleportVector: Some
	CL_NO_TOUCH,			// m_bNoTouch
	CL_CMD,					// m_iCmd: CLIENT_CMD_* bits
	CL_AUTO_WPT,			// m_bAutoWaypoint
	CL_DEBUG_AUTO_WPT,		// m_bDebugAutoWaypoint
	CL_SET_UP_AUTO_WPT,		// m_bSetUpAutoWaypoint
	CL_TOOLTIPS_SENT,		// m_iToolTipsSent: a bit per TOOLTIP_*
	CL_GRIP					// force_grips: the entity +rcbot_force_grip holds, 0 none
};
new g_cl_i[RC_SLOTS][Cl_I];
new Float:g_cl_teleport[RC_SLOTS][3];		// m_vTeleportVector
new g_cl_pass[RC_SLOTS][16];				// m_szPass (BOT_MAX_PASSWORD_LEN - 1 bytes)
new g_cl_steam[RC_SLOTS][32];				// m_szSteamId (STEAM_ID_LEN - 1 bytes)
new g_cl_listen_server;						// m_pListenServerEdict: player index, 0 none

// ============================================================================================
// Reset
// ============================================================================================

// CBot::Init (mod.rs Bot::new): every section's fields to their initial values, before the
// subsystems' own *_bot_init run (manager.inc manager_bot_init).
stock state_clear_bot(id)
{
	new Float:turn = g_cfg_f[CFG_TURN_SPEED];
	for (new Co_I:k; k < Co_I; k++) g_co_i[id][k] = 0;
	for (new Co_F:k; k < Co_F; k++) g_co_f[id][k] = 0.0;
	for (new Co_V:k; k < Co_V; k++) vec_zero(g_co_v[id][k]);
	g_bot_name[id][0] = 0;

	for (new Th_I:k; k < Th_I; k++) g_th_i[id][k] = 0;
	for (new Th_F:k; k < Th_F; k++) g_th_f[id][k] = 0.0;
	for (new Th_V:k; k < Th_V; k++) vec_zero(g_th_v[id][k]);
	g_th_i[id][TH_LOOK_FOR_NEW_TASKS] = 1;
	g_th_i[id][TH_WANT_TO_FIND_ENEMY] = 1;
	g_th_i[id][TH_TEAM] = -1;
	g_th_i[id][TH_OBJ_ATTEMPT] = -1;
	g_th_f[id][TH_THINK_TIME] = BOT_DEFAULT_THINK_TIME;
	g_th_f[id][TH_REACTION_TIME] = BOT_DEFAULT_REACTION_TIME;

	for (new Lk_I:k; k < Lk_I; k++) g_lk_i[id][k] = 0;
	for (new Lk_F:k; k < Lk_F; k++) g_lk_f[id][k] = 0.0;
	for (new Lk_V:k; k < Lk_V; k++) vec_zero(g_lk_v[id][k]);
	g_lk_i[id][LK_LOOK_TASK] = LOOK_NONE;
	g_lk_f[id][LK_TURN_SPEED] = turn;
	g_lk_f[id][LK_BASE_TURN_SPEED] = turn;

	for (new Mv_I:k; k < Mv_I; k++) g_mv_i[id][k] = 0;
	for (new Mv_F:k; k < Mv_F; k++) g_mv_f[id][k] = 0.0;
	for (new Mv_V:k; k < Mv_V; k++) vec_zero(g_mv_v[id][k]);
	g_mv_f[id][MV_MAX_SPEED] = 290.0;

	for (new Nv_I:k; k < Nv_I; k++) g_nv_i[id][k] = 0;
	for (new Nv_F:k; k < Nv_F; k++) g_nv_f[id][k] = 0.0;
	vec_zero(g_nv_v[id][NV_GOAL_ORIGIN]);
	g_nv_i[id][NV_CURRENT_WPT] = -1;
	g_nv_i[id][NV_PREV_WPT] = -1;
	g_nv_i[id][NV_GOAL_WPT] = -1;
	g_nv_i[id][NV_LAST_FAILED_WPT] = -1;

	for (new Cb_I:k; k < Cb_I; k++) g_cb_i[id][k] = 0;
	for (new Cb_F:k; k < Cb_F; k++) g_cb_f[id][k] = 0.0;
	for (new Cb_V:k; k < Cb_V; k++) vec_zero(g_cb_v[id][k]);
	g_cb_f[id][CB_LOWEST_ENEMY_COST] = 99999.0;

	for (new Fi_I:k; k < Fi_I; k++) g_fi_i[id][k] = 0;
	for (new Fi_F:k; k < Fi_F; k++) g_fi_f[id][k] = 0.0;
	for (new Fi_V:k; k < Fi_V; k++) vec_zero(g_fi_v[id][k]);

	for (new w = 0; w < RC_MAX_WEAPONS; w++)
	{
		for (new Bw_I:k; k < Bw_I; k++) g_bw_weapon[id][w][k] = 0;
	}
	for (new Bw_I2:k; k < Bw_I2; k++) g_bw_i[id][k] = 0;
	for (new Bw_F:k; k < Bw_F; k++) g_bw_f[id][k] = 0.0;
	g_bw_i[id][BW_CURRENT] = -1;
	g_bw_i[id][BW_SWITCH_PENDING] = -1;

	for (new Ln_I:k; k < Ln_I; k++) g_ln_i[id][k] = 0;
	for (new Ln_F:k; k < Ln_F; k++) g_ln_f[id][k] = 0.0;
	g_ln_i[id][LN_PENDING_LOADOUT] = -1;
	g_ln_i[id][LN_CREDITED_LOADOUT] = -1;

	for (new Sq_I:k; k < Sq_I; k++) g_sq_i[id][k] = 0;
	for (new Sq_F:k; k < Sq_F; k++) g_sq_f[id][k] = 0.0;
	g_sq_i[id][SQ_SQUAD] = -1;

	for (new Ev_I:k; k < Ev_I; k++) g_ev_i[id][k] = 0;
	for (new Ev_F:k; k < Ev_F; k++) g_ev_f[id][k] = 0.0;
	g_ev_i[id][EV_BOREDOM] = 127;

	for (new Pr_I:k; k < Pr_I; k++) g_pr_i[id][k] = 0;
	for (new Pr_F:k; k < Pr_F; k++) g_pr_f[id][k] = 0.0;
	g_pr_model[id][0] = 0;
}

// EntRef helpers: a handle is (index, serial); a player's serial is its user id.
stock bool:entref_alive(index, serial)
{
	if (index <= 0)
	{
		return false;
	}
	if (index <= MAX_PLAYERS)
	{
		return world_userid(index) == serial && serial != 0;
	}
	return pev_valid(index) != 0;
}
