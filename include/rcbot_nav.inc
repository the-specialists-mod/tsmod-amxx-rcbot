// rcbot_nav.inc: RCBot's waypoints, visibility, A* paths and heatmap for AMX Mod X plugins.
// tsmod-amxx-rcbot (https://github.com/the-specialists-mod/tsmod-amxx-rcbot),
// GPL-3.0-or-later with the AMX Mod X and RCBot linking exceptions.
//
// One set of waypoints per server: the map's, from <gamedir>/tsmod/rcbot/waypoints/ts/<map>.rcw
// and its visibility table <map>.rcv, byte for byte as the standalone tsmod_rcbot plugin (and
// RCBot) writes them; the heatmap is <map>.rhm beside them. Writes are atomic. The waypoints stay
// in the module until the next nav_load; path handles (RcPath) die at map change (AMX Mod X
// reloads the plugins). An invalid handle is a native error; an invalid waypoint index is -1 /
// 0 / false, as RCBot's own lookups answer.
//
// Waypoints are indexes 0..nav_count()-1 (deleted slots included: check nav_valid). Teams are
// RCBot's: rules team - 1, -1 for none (team-specific waypoints of other teams are skipped).
// Heatmap counts take the rules team (1, 2, ...). Vectors are Float:v[3]; distances in units.
//
// rcbot_nav is also a metamod plugin (it needs the engine's traces): AMX Mod X attaches it as
// one by itself when it loads the module.

#if defined _rcbot_nav_included
  #endinput
#endif
#define _rcbot_nav_included

#pragma reqlib rcbot_nav
#if !defined AMXMODX_NOAUTOLOAD
  #pragma loadlib rcbot_nav
#endif

// ---- waypoint flags (W_FL_*, the bits .rcw files store) ----

#define NAV_FL_TEAM           3        // a team number 0-3 in the low two bits
#define NAV_FL_TEAM_SPECIFIC  (1<<2)   // only the team in NAV_FL_TEAM uses it
#define NAV_FL_CROUCH         (1<<3)
#define NAV_FL_LADDER         (1<<4)
#define NAV_FL_LIFT           (1<<5)
#define NAV_FL_DOOR           (1<<6)
#define NAV_FL_HEALTH         (1<<7)
#define NAV_FL_ARMOR          (1<<8)
#define NAV_FL_AMMO           (1<<9)
#define NAV_FL_CHECK_LIFT     (1<<10)
#define NAV_FL_IMPORTANT      (1<<11)
#define NAV_FL_RESCUE         (1<<12)
#define NAV_FL_DEFEND_ZONE    (1<<13)
#define NAV_FL_AIMING         (1<<14)  // a camping/aiming point, not walked to
#define NAV_FL_CROUCHJUMP     (1<<16)
#define NAV_FL_WAIT_FOR_LIFT  (1<<17)
#define NAV_FL_STUNT          (1<<18)  // TS dive/stunt (the generic wall-stick bit)
#define NAV_FL_JUMP           (1<<19)
#define NAV_FL_WEAPON         (1<<20)
#define NAV_FL_TELEPORT       (1<<21)  // leaving it costs nothing
#define NAV_FL_TANK           (1<<22)
#define NAV_FL_FLY            (1<<23)  // unusable in TS unless also NAV_FL_STUNT
#define NAV_FL_STAY_NEAR      (1<<24)
#define NAV_FL_ENDLEVEL       (1<<25)
#define NAV_FL_OPENS_LATER    (1<<26)  // A* traces the path before using it
#define NAV_FL_HUMAN_TOWER    (1<<27)
#define NAV_FL_UNREACHABLE    (1<<28)  // a reference point for visibility only
#define NAV_FL_PUSHABLE       (1<<29)
#define NAV_FL_GREN_THROW     (1<<30)
#define NAV_FL_DELETED        (1<<31)  // a free slot

/** REACHABLE_RANGE: the default search radius. */
#define NAV_REACHABLE_RANGE   400.0
/** BOT_WAYPOINT_TOUCH_DIST. */
#define NAV_TOUCH_DIST        32.0
/** MAX_WAYPOINTS. */
#define NAV_MAX_WAYPOINTS     1024

/**
 * Seeds the module's random numbers (random goals, failed-goal timers), for reproducible runs.
 * They start seeded from the clock. Not the engine's RANDOM_LONG sequence (see the README).
 */
native nav_seed(seed);

// ---- the map's waypoints ----

/**
 * WaypointLoad: waypoints/ts/<map>.rcw and, unless its header says it is stale, <map>.rcv. The
 * visibility rows the .rcv doesn't hold (it keeps only the leading rows of RCBot's 1024-wide
 * table) are traced a few per server frame afterwards (nav_vis_pending). Without a usable file
 * the waypoints are empty, but the map name is kept so a new set saves under it.
 *
 * @param map       The map; "" is the current one (from the port).
 * @return          true if the file loaded.
 */
native bool:nav_load(const map[] = "");

/**
 * WaypointSave: <map>.rcw, then <map>.rcv (missing visibility rows are traced first).
 *
 * @return          false with no map (nav_load not called) or when the .rcw can't be written.
 */
native bool:nav_save();

/** num_waypoints: one past the highest slot in use. */
native nav_count();

/** WaypointInit: no waypoints (the map name stays). */
native nav_clear();

/** The map nav_load named. */
native nav_map(out[], len);

/** The waypoint is in use and not deleted. */
native bool:nav_valid(waypoint);

/** WaypointOrigin. @return false for a slot past nav_count(). */
native bool:nav_origin(waypoint, Float:origin[3]);

/** WaypointFlags (NAV_FL_*), 0 for an invalid index. */
native nav_flags(waypoint);

/** WaypointDistance from a point. */
native Float:nav_distance(waypoint, const Float:from[3]);

// nav_nearest flags.
#define NAV_NEAREST_VISIBLE      1  // a clear trace from the point (bGetVisible)
#define NAV_NEAREST_UNREACHABLE  2  // include NAV_FL_UNREACHABLE (bGetUnReachable)
#define NAV_NEAREST_BOT          4  // skip jump/crouch-jump/fly waypoints (bIsBot)
#define NAV_NEAREST_AIMING       8  // aiming waypoints only

/**
 * CWaypointLocations::NearestWaypoint: the nearest waypoint in the 3x3x3 location buckets (256
 * units each) around the point, closer than range. The bot's usual lookup.
 *
 * @param ignore    A waypoint to skip (-1 none).
 * @param failed    Waypoints to skip (the bot's failed goals), num_failed of them.
 * @return          The waypoint, -1 for none.
 */
native nav_nearest(const Float:origin[3], Float:range = NAV_REACHABLE_RANGE, ignore = -1, flags = NAV_NEAREST_VISIBLE, const failed[] = {-1}, num_failed = 0);

/**
 * WaypointFindNearest: the nearest usable waypoint of all, closer than range, with a clear
 * trace from trace_from (the eyes) passing through ignore_ent.
 */
native nav_nearest_scan(const Float:origin[3], const Float:trace_from[3], ignore_ent = 0, Float:range = NAV_REACHABLE_RANGE, team = -1);

/** WaypointFindNearestGoal: the nearest usable waypoint with all of flags (no trace). */
native nav_nearest_flag(const Float:origin[3], flags, Float:range = 99999.0, team = -1, const ignore[] = {-1}, num_ignore = 0);

/** WaypointFindRandomGoal: one of the first 50 usable waypoints with all of flags (upstream's pick). */
native nav_random_flag(flags = 0, team = -1, const ignore[] = {-1}, num_ignore = 0);

/** WaypointFindRandomGoal near a point: as nav_random_flag, closer than range. */
native nav_random_flag_near(const Float:origin[3], Float:range, flags = 0, team = -1, const ignore[] = {-1}, num_ignore = 0);

/** WaypointFindNearestAiming: the nearest aiming waypoint within 40 units. */
native nav_nearest_aiming(const Float:origin[3]);

/** WaypointFindReachable: the nearest usable waypoint visible from origin + view_ofs and walkable from origin. */
native nav_find_reachable(const Float:origin[3], const Float:view_ofs[3], ignore_ent = 0, Float:range = NAV_REACHABLE_RANGE, team = -1);

/**
 * WaypointReachable: can a player walk from src to dest? Closer than 400 (with dist_check), a
 * clear trace, and no mid-air destination or drop deeper than a jump on the way.
 */
native bool:nav_reachable(const Float:src[3], const Float:dest[3], bool:dist_check = true);

/**
 * The engine's PVS test (the host's BotWorld::in_pvs: ENGINE_CHECK_VISIBILITY(viewer,
 * ENGINE_SET_PVS(origin))): is the entity viewer in the potentially visible set of origin? False
 * for viewer 0 or a free edict. AMX Mod X 1.10's fakemeta has no EngFunc_SetFatPVS, hence this.
 */
native bool:nav_in_pvs(viewer, const Float:origin[3]);

/**
 * The paths out of a waypoint usable by team, in WaypointFindPath order.
 *
 * @return          How many there are (at most len written).
 */
native nav_paths_out(waypoint, out[], len, team = -1);

/** The waypoints with a path to this one. @return how many (at most len written). */
native nav_paths_in(waypoint, out[], len);

/** BotNavigate_FindPathFromTo: is there a path from -> to for team? */
native bool:nav_has_path(from, to, team = -1);

/**
 * GetCoverWaypoint: the waypoint nearest the player (in the buckets around it) that the
 * visibility table says can't be seen from the waypoint nearest cover_from. With heatmap, each
 * candidate's distance gets nav_heatmap_cover_penalty for team added (the standalone bot's
 * choice: cover where the team doesn't keep dying).
 *
 * @return          The waypoint, -1 for none.
 */
native nav_cover(const Float:player[3], const Float:cover_from[3], const ignore[] = {-1}, num_ignore = 0, team = -1, bool:heatmap = true);

/** Every waypoint in the 3x3x3 location buckets around the point. @return how many. */
native nav_buckets_around(const Float:origin[3], out[], len);

/** FillNearbyWaypoints: as nav_buckets_around, without unreachable ones. */
native nav_nearby(const Float:origin[3], out[], len);

/** GetVisibilityFromTo: can `to` be seen from `from`? (false for a row not traced yet) */
native bool:nav_visible(from, to);

/** Visibility rows still to trace (a few are done each server frame). */
native nav_vis_pending();

/**
 * Traces per frame for missing visibility rows (default 1024; at least one row a frame).
 *
 * @return          The old budget.
 */
native nav_vis_budget(traces_per_frame);

/** WorkOutVisibilityTable: the whole table again, now or over the next frames. */
native nav_vis_rebuild(bool:now = false);

/** The missing visibility rows, now. */
native nav_vis_finish();

// ---- the heatmap (waypoints/ts/<map>.rhm; A*'s danger with NAV_FIND_TACTICS) ----

/**
 * The map's heatmap for the waypoints as loaded (a new one when the file is missing or was
 * made for another waypoint count), faded once for this load. Call after nav_load.
 *
 * @return          true if the file was used.
 */
native bool:nav_heatmap_load();

/** Writes the heatmap (the standalone plugin does at the map's end). */
native bool:nav_heatmap_save();

native nav_heatmap_add_death(waypoint, rules_team);
native nav_heatmap_add_kill(waypoint, rules_team);
native nav_heatmap_add_damage(waypoint, rules_team, Float:amount);

/** How dangerous the waypoint is for the team: 0..1 (0.5 at 2 deaths). */
native Float:nav_heatmap_danger(waypoint, bot_team);

/** Units to add to a cover waypoint's distance (up to 256). */
native Float:nav_heatmap_cover_penalty(waypoint, bot_team);

/** How good the waypoint has been to kill from, any team: 0..1. */
native Float:nav_heatmap_ambush(waypoint);

// ---- A* paths, one per bot ----

enum NavPathStatus
{
	NAV_PATH_IDLE = 0,   // no search yet
	NAV_PATH_SEARCHING,  // out of expansions: resume on a later frame
	NAV_PATH_FOUND,      // the path is on the stack
	NAV_PATH_FAILED      // no path (or bad waypoints, or from == to)
};

// nav_path_find flags.
#define NAV_FIND_UPSTREAM  0  // BotNavigate_AStarAlgo exactly
#define NAV_FIND_TACTICS   1  // costs avoid heatmap danger, threats' sight lines, teammates' paths
#define NAV_FIND_BACK_OFF  2  // a goal no path reaches stays failed and is left alone 30-60 s

/** m_iPathRevs when unset: expansions per call. */
#define NAV_PATH_REVS      150

/** A bot's path state (search, found path, failed paths and goals, tactics, visits). */
native RcPath:nav_path_create();

/** Frees a path (and zeroes the variable). */
native bool:nav_path_free(&RcPath:path);

/**
 * BotNavigate_AStarAlgo from -> to. Expands at most max_loops waypoints; NAV_PATH_SEARCHING means
 * call again on a later frame with resume = true (or nav_path_continue), as the bot's FIND_PATH
 * task does. A resumed search keeps its open list and must use the same flags. Found, the path
 * is a stack: nav_path_next is the first waypoint after `from`, the goal is at the bottom
 * (`from` itself isn't on it: push it with nav_path_push as the bot does).
 *
 * @param gun_pos   The bot's eyes (upstream's heuristic measures from them).
 * @param team      RCBot team (-1 any); also the heatmap team for NAV_FIND_TACTICS.
 */
native NavPathStatus:nav_path_find(RcPath:path, from, to, const Float:gun_pos[3], team = -1, max_loops = NAV_PATH_REVS, flags = NAV_FIND_UPSTREAM, bool:resume = false);

/** Resumes a NAV_PATH_SEARCHING search with its last request; otherwise returns the status. */
native NavPathStatus:nav_path_continue(RcPath:path);

native NavPathStatus:nav_path_status(RcPath:path);

/** The top of the path stack (the next waypoint), -1 when empty. */
native nav_path_next(RcPath:path);

/** Pops the top (BotNavigate_FindNextWaypoint's pop). @return it, or -1. */
native nav_path_pop(RcPath:path);

/** Pushes a waypoint on top (unless it is the top already, with unless_top). */
native bool:nav_path_push(RcPath:path, waypoint, bool:unless_top = true);

native nav_path_length(RcPath:path);

/** The path in walking order (next first, goal last). @return its length (at most len written). */
native nav_path_get(RcPath:path, out[], len);

native bool:nav_path_contains(RcPath:path, waypoint);

/** GetNextWaypoint: the first waypoint on the stack (from the top) that isn't current, or current. */
native nav_path_after(RcPath:path, current);

/** Empties the path stack (m_stBotPaths.Destroy()). */
native nav_path_clear(RcPath:path);

/** Forgets everything but the tactical weights and visits (failed paths/goals, threats, stack). */
native nav_path_reset(RcPath:path);

/**
 * m_stFailedPaths.AddFailedPath: A* skips the path from -> to (with the other paths in its
 * 4-slot PATH node, as upstream) until a search fails on it.
 *
 * @return          false when there is no such path.
 */
native bool:nav_path_fail(RcPath:path, from, to, team = -1);

native nav_path_failed_count(RcPath:path);
native nav_path_clear_failed_paths(RcPath:path);

/** A goal reached isn't a roaming goal again until Think clears the failed goals (5-10 s). */
native bool:nav_path_goal_reached(RcPath:path, waypoint);

/** m_FailedGoals. @return how many (at most len written). */
native nav_path_failed_goals(RcPath:path, out[], len);

/** The failed goals and those still backed off: what roaming leaves alone now. */
native nav_path_ignored_goals(RcPath:path, out[], len);

native nav_path_clear_failed_goals(RcPath:path);

/** Think's upkeep: clears the failed goals when their time is up, forgets expired back-offs. Call each think. */
native nav_path_think(RcPath:path);

/** The NAV_FIND_TACTICS weights (the standalone bot's: 2.0, 1.5, 0.5). */
native nav_path_set_weights(RcPath:path, Float:danger = 2.0, Float:exposure = 1.5, Float:crowd = 0.5);

/** Waypoints enemies were seen at, with weights (newer: more). @return how many were set. */
native nav_path_set_threats(RcPath:path, const waypoints[], const Float:weights[], num);

/**
 * A sighting: the waypoint nearest it (400 units, no trace) becomes a threat with at least
 * weight 0.1 (the higher weight is kept). @return the waypoint, -1 for none.
 */
native nav_path_add_threat(RcPath:path, const Float:origin[3], Float:weight);

/** @return how many threats there are (at most len written). */
native nav_path_threats(RcPath:path, waypoints[], Float:weights[], len);

native nav_path_clear_threats(RcPath:path);

/** Waypoints on teammates' paths. @return how many (sorted, without repeats). */
native nav_path_set_crowded(RcPath:path, const waypoints[], num);

/** The crowded waypoints from the teammates' path objects' stacks (`path` itself is skipped). */
native nav_path_crowd_from(RcPath:path, const RcPath:others[], num);

/** The bot is at the waypoint now (exploration). */
native bool:nav_path_touch(RcPath:path, waypoint);

/** Seconds since the bot was at the waypoint (at most 120). */
native Float:nav_path_since_visit(RcPath:path, waypoint);

/**
 * A roaming goal: any usable waypoint, more likely the longer the bot hasn't been there (and a
 * quarter as likely within 400 units of origin), less likely near teammates' claimed goals,
 * never a failed or backed-off goal.
 *
 * @param claims    Teammates' goal waypoints (-1 entries are skipped), num_claims of them.
 * @return          The waypoint, -1 for none.
 */
native nav_path_roam_goal(RcPath:path, team, const Float:origin[3], const claims[] = {-1}, num_claims = 0);

// ---- editing ----

/**
 * WaypointAddOrigin. With a player and auto_flags: crouch when ducking, ladder on a ladder;
 * unowned items in sight within 72 units add ammo/weapon/health/armour. auto_path traces the new
 * waypoint's visibility and adds paths both ways to every waypoint reachable from it. With a
 * player and draw, a sound and its beam are queued (nav_event_pop).
 *
 * @return          The waypoint, -1 when full.
 */
native nav_add(const Float:origin[3], flags = 0, player = 0, bool:auto_flags = true, bool:auto_path = false, bool:draw = true);

/** WaypointAddAiming: an aiming waypoint 25 units in front along the yaw of v_angle. */
native nav_add_aiming(const Float:origin[3], const Float:v_angle[3]);

/** WaypointDelete of one waypoint: its paths both ways go, its slot is marked deleted. */
native bool:nav_delete(waypoint);

/** WaypointDelete: the waypoint within 50 units, with its sound queued. @return it, or -1. */
native nav_delete_near(const Float:origin[3]);

/** WaypointAddPath (no duplicate check, as upstream). */
native bool:nav_add_path(from, to);

/** WaypointDeletePath(from, to). */
native bool:nav_remove_path(from, to);

/** WaypointDeletePath(waypoint): every path to it. */
native bool:nav_remove_paths_to(waypoint);

/** Moves a waypoint (the location buckets keep the old place, as upstream's auto-waypointer). */
native bool:nav_set_origin(waypoint, const Float:origin[3]);

/** @return the new flags. */
native nav_set_flags(waypoint, flags);
native nav_add_flags(waypoint, flags);
native nav_clear_flags(waypoint, flags);

/**
 * WaypointCreatePath: cmd 1 picks the source (the waypoint within 50 units the player can see),
 * cmd 2 the destination and adds the path (or queues the error message). Sounds are queued.
 *
 * @return          The picked source, -1 for none.
 */
native nav_edit_create_path(player, const Float:origin[3], cmd);

/** WaypointRemovePath, as nav_edit_create_path. */
native nav_edit_remove_path(player, const Float:origin[3], cmd);

/**
 * CClient::Think's auto-waypointing for a player (walking every 200 units and round corners,
 * jumps and TS stunts, ladders). Call once per think while it is on.
 *
 * @param show      Queue a sound and beam for each new waypoint.
 * @param debug     Queue its check beams.
 * @return          false when the player isn't there.
 */
native bool:nav_auto_waypoint(player, bool:show = true, bool:debug = false, bool:auto_path = true);

/** Starts the player's auto-waypointing over. */
native nav_auto_reset(player);

/** UTIL_CanStand: more than 72 units between floor and ceiling; floor gets the floor. */
native bool:nav_can_stand(const Float:origin[3], Float:floor[3]);

// ---- draw data (the plugin sends the beams: TE_BEAMPOINTS) ----

enum NavEvent
{
	NAV_EVENT_NONE = 0,
	NAV_EVENT_BEAM,     // start, end, params: width, noise, r, g, b, brightness, speed
	NAV_EVENT_SOUND,    // text: the sample to play to the editing player
	NAV_EVENT_MESSAGE   // text: a message for the editing player
};

/** WaypointDrawIndex: the waypoint's beam(s), queued. @return how many. */
native nav_draw_index(waypoint);

/**
 * CWaypointLocations::DrawWaypoints and the path display: beams for the waypoints around the
 * point (in the viewer's PVS, within 256 units of height), and with paths the paths into (yellow)
 * and out of (white) the waypoint the viewer stands at, queued.
 *
 * @return          The waypoint the viewer stands at, -1 for none.
 */
native nav_draw_around(viewer, const Float:origin[3], bool:paths = true);

/** A beam along each step of a bot's path (debug view), queued. @return how many. */
native nav_draw_path(RcPath:path, r = 255, g = 0, b = 0);

/** Queued events (at most 4096 are kept). */
native nav_event_count();

/** The oldest queued event, NAV_EVENT_NONE when there is none. */
native NavEvent:nav_event_pop(Float:start[3], Float:end[3], params[7], text[], len);

native nav_event_clear();
