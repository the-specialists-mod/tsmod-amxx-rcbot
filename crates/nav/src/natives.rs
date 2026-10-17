// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions. The queries wrap
// `rcbot::nav` (RCBot's waypoint.cpp and bot_navigate.cpp, by Paul "Cheeseh" Murphy) as the
// standalone bot calls them (`bot/navigate.rs`, `think.rs`, `combat.rs` `run_for_cover`); the
// editor natives wrap `rcbot::nav::editor` as the host's commands do (`crates/host/src/editor.rs`).

//! The natives for the map's waypoints (load, save, queries, visibility, location buckets), the
//! heatmap, and waypoint editing with its draw data, as `fn(&mut dyn Args) -> Cell` so tests run
//! them against `FakeArgs`. The per-bot A* paths are in [`crate::paths`].
//! `include/rcbot_nav.inc` documents each one.

use crate::state::{State, with};
use crate::world::with_world;
use amxx::Cell;
use amxx::native::Args;
use rcbot::Vector;
use rcbot::nav::editor::{self, EditorEvent};
use rcbot::nav::{MAX_WAYPOINTS, NearestOptions, Waypoints, flags as fl};
use rcbot_amxx_common::{ModuleRandom, count, float_cell, read_floats, set_out, write_floats};

/// The most cells an array argument may name (a guard against a wrong size).
pub const MAX_ARRAY: usize = 4096;

/// `nav_nearest` flags: only waypoints with a clear trace from the point (`bGetVisible`).
pub const NEAREST_VISIBLE: Cell = 1;
/// Include `W_FL_UNREACHABLE` waypoints (`bGetUnReachable`).
pub const NEAREST_UNREACHABLE: Cell = 2;
/// Skip waypoints a bot can't start from (`bIsBot`: jump, crouch-jump, fly).
pub const NEAREST_BOT: Cell = 4;
/// Only aiming waypoints (`bNearestAimingOnly`).
pub const NEAREST_AIMING: Cell = 8;

/// Editor event kinds (`nav_event_pop`).
pub const EVENT_BEAM: Cell = 1;
pub const EVENT_SOUND: Cell = 2;
pub const EVENT_MESSAGE: Cell = 3;

/// A count argument, at most [`MAX_ARRAY`].
pub fn size(a: &dyn Args, i: usize) -> usize {
    count(a, i).min(MAX_ARRAY)
}

/// A `Float:v[3]` argument.
pub fn vec3(a: &mut dyn Args, i: usize) -> Vector {
    let v = read_floats(a, i, 3);
    Vector::new(v[0], v[1], v[2])
}

/// Writes a vector into the `Float:v[3]` argument `i`.
pub fn set_vec3(a: &mut dyn Args, i: usize, v: Vector) {
    write_floats(a, i, &v.to_array(), 3);
}

/// A waypoint argument: an index into the table (`0..MAX_WAYPOINTS`), `None` otherwise (-1).
pub fn waypoint_arg(a: &dyn Args, i: usize) -> Option<usize> {
    usize::try_from(a.cell(i)).ok().filter(|&w| w < MAX_WAYPOINTS)
}

/// Writes `values` (at most `max`) into the array argument `i`; returns how many there are in
/// all (so a plugin can tell its array was short).
pub fn write_cells(a: &mut dyn Args, i: usize, max: usize, values: impl Iterator<Item = Cell>) -> Cell {
    let mut buf: Vec<Cell> = Vec::new();
    let mut total = 0;
    for v in values {
        if buf.len() < max {
            buf.push(v);
        }
        total += 1;
    }
    if !buf.is_empty() {
        a.write_array(i, 0, &buf);
    }
    total
}

/// The waypoint indices in the array argument `i` of `n` cells (an ignore or failed list).
fn index_list(a: &mut dyn Args, i: usize, n: usize) -> Vec<usize> {
    if n == 0 {
        return Vec::new();
    }
    a.read_array(i, n).into_iter().filter_map(|c| usize::try_from(c).ok()).collect()
}

fn opt_index(o: Option<usize>) -> Cell {
    o.map_or(-1, |i| i as Cell)
}

/// An entity argument: positive cells are entity indexes, 0 and below none.
fn entity_arg(a: &dyn Args, i: usize) -> Option<u32> {
    u32::try_from(a.cell(i)).ok().filter(|&e| e > 0)
}

// ---- the module ----

/// `nav_seed(seed)`
pub fn nav_seed(a: &mut dyn Args) -> Cell {
    let seed = a.cell(1) as u32 as u64;
    with(|s| s.rng = ModuleRandom::seeded(seed));
    1
}

// ---- the map's waypoints ----

/// The current map's name, from the port's plugin API.
fn current_map() -> Option<String> {
    amxx::tsapi::Api::find().ok().map(|api| api.map_name()).filter(|m| !m.is_empty())
}

/// `bool:nav_load(const map[] = "")`
pub fn nav_load(a: &mut dyn Args) -> Cell {
    let mut map = a.string(1);
    if map.is_empty() {
        match current_map() {
            Some(m) => map = m,
            None => {
                a.error("nav_load: no map name (the port's plugin API isn't there)");
                return 0;
            }
        }
    }
    match with(|s| s.load(&map)) {
        Some(Ok(_)) => 1,
        Some(Err(e)) => {
            rcbot_amxx_common::log(&e);
            0
        }
        None => 0,
    }
}

/// `bool:nav_save()`
pub fn nav_save(_: &mut dyn Args) -> Cell {
    with(|s| s.save()).unwrap_or(false) as Cell
}

/// `nav_count()`
pub fn nav_count(_: &mut dyn Args) -> Cell {
    with(|s| s.waypoints.len() as Cell).unwrap_or(0)
}

/// `nav_clear()`
pub fn nav_clear(_: &mut dyn Args) -> Cell {
    with(State::clear);
    1
}

/// `nav_map(out[], len)`
pub fn nav_map(a: &mut dyn Args) -> Cell {
    let map = with(|s| s.map.clone()).unwrap_or_default();
    set_out(a, 1, map.as_bytes())
}

/// `bool:nav_valid(waypoint)`: in use and not deleted.
pub fn nav_valid(a: &mut dyn Args) -> Cell {
    let w = waypoint_arg(a, 1);
    with(|s| w.is_some_and(|w| w < s.waypoints.len() && s.waypoints.flags(w as i32) & fl::DELETED == 0)).unwrap_or(false) as Cell
}

/// `bool:nav_origin(waypoint, Float:origin[3])`
pub fn nav_origin(a: &mut dyn Args) -> Cell {
    let w = waypoint_arg(a, 1);
    let Some(o) = with(|s| w.filter(|&w| w < s.waypoints.len()).map(|w| s.waypoints.origin(w as i32))).flatten() else {
        return 0;
    };
    set_vec3(a, 2, o);
    1
}

/// `nav_flags(waypoint)`
pub fn nav_flags(a: &mut dyn Args) -> Cell {
    let w = a.cell(1);
    with(|s| s.waypoints.flags(w) as Cell).unwrap_or(0)
}

/// `Float:nav_distance(waypoint, const Float:from[3])`: `WaypointDistance`.
pub fn nav_distance(a: &mut dyn Args) -> Cell {
    let (w, from) = (a.cell(1), vec3(a, 2));
    float_cell(with(|s| s.waypoints.distance(from, w)).unwrap_or(0.0))
}

/// `nav_nearest(const Float:origin[3], Float:range = 400.0, ignore = -1, flags = NAV_NEAREST_VISIBLE,
/// const failed[] = {}, num_failed = 0)`: `CWaypointLocations::NearestWaypoint`.
pub fn nav_nearest(a: &mut dyn Args) -> Cell {
    let (origin, range, ignore, flags) = (vec3(a, 1), a.float(2), waypoint_arg(a, 3), a.cell(4));
    let n = size(a, 6);
    let failed = index_list(a, 5, n);
    let opts = NearestOptions {
        visible: flags & NEAREST_VISIBLE != 0,
        unreachable: flags & NEAREST_UNREACHABLE != 0,
        is_bot: flags & NEAREST_BOT != 0,
        aiming_only: flags & NEAREST_AIMING != 0,
    };
    with(|s| opt_index(with_world(|w| s.waypoints.nearest_waypoint(origin, range, ignore, opts, &failed, w)))).unwrap_or(-1)
}

/// `nav_nearest_scan(const Float:origin[3], const Float:trace_from[3], ignore_ent = 0,
/// Float:range = 400.0, team = -1)`: `WaypointFindNearest` over every waypoint.
pub fn nav_nearest_scan(a: &mut dyn Args) -> Cell {
    let (origin, from, ent, range, team) = (vec3(a, 1), vec3(a, 2), entity_arg(a, 3), a.float(4), a.cell(5));
    with(|s| opt_index(with_world(|w| s.waypoints.find_nearest(origin, from, ent, range, team, w)))).unwrap_or(-1)
}

/// `nav_nearest_flag(const Float:origin[3], flags, Float:range = 99999.0, team = -1,
/// const ignore[] = {}, num_ignore = 0)`: `WaypointFindNearestGoal`.
pub fn nav_nearest_flag(a: &mut dyn Args) -> Cell {
    let (origin, flags, range, team) = (vec3(a, 1), a.cell(2) as u32, a.float(3), a.cell(4));
    let n = size(a, 6);
    let ignore = index_list(a, 5, n);
    with(|s| opt_index(s.waypoints.find_nearest_goal(origin, range, team, flags, &ignore))).unwrap_or(-1)
}

/// `nav_random_flag(flags = 0, team = -1, const ignore[] = {}, num_ignore = 0)`:
/// `WaypointFindRandomGoal` (one of the first 50 matches, as upstream).
pub fn nav_random_flag(a: &mut dyn Args) -> Cell {
    let (flags, team) = (a.cell(1) as u32, a.cell(2));
    let n = size(a, 4);
    let ignore = index_list(a, 3, n);
    with(|s| {
        let State { waypoints, rng, .. } = s;
        opt_index(waypoints.find_random_goal(team, flags, &ignore, rng))
    })
    .unwrap_or(-1)
}

/// `nav_random_flag_near(const Float:origin[3], Float:range, flags = 0, team = -1,
/// const ignore[] = {}, num_ignore = 0)`: `WaypointFindRandomGoal` near a point.
pub fn nav_random_flag_near(a: &mut dyn Args) -> Cell {
    let (origin, range, flags, team) = (vec3(a, 1), a.float(2), a.cell(3) as u32, a.cell(4));
    let n = size(a, 6);
    let ignore = index_list(a, 5, n);
    with(|s| {
        let State { waypoints, rng, .. } = s;
        opt_index(waypoints.find_random_goal_near(origin, range, team, flags, &ignore, rng))
    })
    .unwrap_or(-1)
}

/// `nav_nearest_aiming(const Float:origin[3])`: `WaypointFindNearestAiming`.
pub fn nav_nearest_aiming(a: &mut dyn Args) -> Cell {
    let origin = vec3(a, 1);
    with(|s| opt_index(s.waypoints.find_nearest_aiming(origin))).unwrap_or(-1)
}

/// `nav_find_reachable(const Float:origin[3], const Float:view_ofs[3], ignore_ent = 0,
/// Float:range = 400.0, team = -1)`: `WaypointFindReachable`.
pub fn nav_find_reachable(a: &mut dyn Args) -> Cell {
    let (origin, ofs, ent, range, team) = (vec3(a, 1), vec3(a, 2), entity_arg(a, 3), a.float(4), a.cell(5));
    with(|s| opt_index(with_world(|w| s.waypoints.find_reachable(origin, ofs, ent, range, team, w)))).unwrap_or(-1)
}

/// `bool:nav_reachable(const Float:src[3], const Float:dest[3], bool:dist_check = true)`:
/// `WaypointReachable`.
pub fn nav_reachable(a: &mut dyn Args) -> Cell {
    let (src, dest, check) = (vec3(a, 1), vec3(a, 2), a.cell(3) != 0);
    with_world(|w| Waypoints::reachable(src, dest, check, w)) as Cell
}

/// `bool:nav_in_pvs(viewer, const Float:origin[3])`: `BotWorld::in_pvs`, the host's
/// `ENGINE_CHECK_VISIBILITY(viewer, ENGINE_SET_PVS(origin))` (`crates/host/src/api.rs` `in_pvs`):
/// is the entity `viewer` in the potentially visible set of `origin`? False for no viewer (0 or
/// less) or a free edict; true without the engine table (nothing to test against, as the host).
pub fn nav_in_pvs(a: &mut dyn Args) -> Cell {
    let (viewer, origin) = (entity_arg(a, 1), vec3(a, 2));
    viewer.is_some_and(|v| with_world(|w| w.in_pvs(v, origin))) as Cell
}

/// `nav_paths_out(waypoint, out[], len, team = -1)`: successors in `WaypointFindPath` order.
pub fn nav_paths_out(a: &mut dyn Args) -> Cell {
    let (w, max, team) = (waypoint_arg(a, 1), size(a, 3), a.cell(4));
    let Some(w) = w else { return 0 };
    let succ = with(|s| s.waypoints.successors(w, team)).unwrap_or_default();
    write_cells(a, 2, max, succ.into_iter().map(|i| i as Cell))
}

/// `nav_paths_in(waypoint, out[], len)`: waypoints with a path to it.
pub fn nav_paths_in(a: &mut dyn Args) -> Cell {
    let (w, max) = (a.cell(1), size(a, 3));
    let from: Vec<Cell> =
        with(|s| s.waypoints.all_paths().filter(|&(_, to)| i32::from(to) == w).map(|(f, _)| f as Cell).collect()).unwrap_or_default();
    write_cells(a, 2, max, from.into_iter())
}

/// `bool:nav_has_path(from, to, team = -1)`: `BotNavigate_FindPathFromTo`.
pub fn nav_has_path(a: &mut dyn Args) -> Cell {
    let (Some(from), Some(to), team) = (waypoint_arg(a, 1), waypoint_arg(a, 2), a.cell(3)) else { return 0 };
    with(|s| s.waypoints.find_path_from_to(from, to, team).is_some()).unwrap_or(false) as Cell
}

/// `nav_cover(const Float:player[3], const Float:cover_from[3], const ignore[] = {},
/// num_ignore = 0, team = -1, bool:heatmap = true)`: `GetCoverWaypoint`, with the heatmap's
/// cover penalty as `run_for_cover` adds it.
pub fn nav_cover(a: &mut dyn Args) -> Cell {
    let (player, from) = (vec3(a, 1), vec3(a, 2));
    let n = size(a, 4);
    let ignore = index_list(a, 3, n);
    let (team, use_heat) = (a.cell(5), a.cell(6) != 0);
    with(|s| {
        let heat = &s.heatmap;
        let penalty = |i: usize| if use_heat { heat.cover_penalty(i, team) } else { 0.0 };
        opt_index(with_world(|w| s.waypoints.cover_waypoint_weighted(player, from, &ignore, w, &penalty)))
    })
    .unwrap_or(-1)
}

/// `nav_buckets_around(const Float:origin[3], out[], len)`: every waypoint in the 3x3x3
/// location buckets around the point (`DrawWaypoints`' loop).
pub fn nav_buckets_around(a: &mut dyn Args) -> Cell {
    let (origin, max) = (vec3(a, 1), size(a, 3));
    let v = with(|s| s.waypoints.in_buckets_around(origin)).unwrap_or_default();
    write_cells(a, 2, max, v.into_iter().map(|i| i as Cell))
}

/// `nav_nearby(const Float:origin[3], out[], len)`: `FillNearbyWaypoints`.
pub fn nav_nearby(a: &mut dyn Args) -> Cell {
    let (origin, max) = (vec3(a, 1), size(a, 3));
    let v = with(|s| s.waypoints.nearby_waypoints(origin)).unwrap_or_default();
    write_cells(a, 2, max, v.into_iter().map(|i| i as Cell))
}

/// `bool:nav_visible(from, to)`: `GetVisibilityFromTo`.
pub fn nav_visible(a: &mut dyn Args) -> Cell {
    let (Some(from), Some(to)) = (waypoint_arg(a, 1), waypoint_arg(a, 2)) else { return 0 };
    with(|s| s.waypoints.visibility.get(from, to)).unwrap_or(false) as Cell
}

/// `nav_vis_pending()`
pub fn nav_vis_pending(_: &mut dyn Args) -> Cell {
    with(|s| s.vis_pending() as Cell).unwrap_or(0)
}

/// `nav_vis_budget(traces_per_frame)`: returns the old budget.
pub fn nav_vis_budget(a: &mut dyn Args) -> Cell {
    let b = count(a, 1);
    with(|s| std::mem::replace(&mut s.vis_budget, b) as Cell).unwrap_or(0)
}

/// `nav_vis_rebuild(bool:now = false)`: `WorkOutVisibilityTable`, now or over the next frames.
pub fn nav_vis_rebuild(a: &mut dyn Args) -> Cell {
    let now = a.cell(1) != 0;
    with(|s| {
        s.waypoints.visibility.clear();
        s.vis_next = 0;
        if now {
            with_world(|w| s.finish_visibility(w));
        }
    });
    1
}

/// `nav_vis_finish()`: the missing rows, now.
pub fn nav_vis_finish(_: &mut dyn Args) -> Cell {
    with(|s| with_world(|w| s.finish_visibility(w)));
    1
}

// ---- the heatmap ----

/// `bool:nav_heatmap_load()`
pub fn nav_heatmap_load(_: &mut dyn Args) -> Cell {
    with(|s| s.load_heatmap() as Cell).unwrap_or(0)
}

/// `bool:nav_heatmap_save()`
pub fn nav_heatmap_save(_: &mut dyn Args) -> Cell {
    with(|s| s.save_heatmap()).unwrap_or(false) as Cell
}

/// `nav_heatmap_add_death(waypoint, rules_team)`
pub fn nav_heatmap_add_death(a: &mut dyn Args) -> Cell {
    let (Some(w), t) = (waypoint_arg(a, 1), a.cell(2)) else { return 0 };
    with(|s| s.heatmap.add_death(w, t));
    1
}

/// `nav_heatmap_add_kill(waypoint, rules_team)`
pub fn nav_heatmap_add_kill(a: &mut dyn Args) -> Cell {
    let (Some(w), t) = (waypoint_arg(a, 1), a.cell(2)) else { return 0 };
    with(|s| s.heatmap.add_kill(w, t));
    1
}

/// `nav_heatmap_add_damage(waypoint, rules_team, Float:amount)`
pub fn nav_heatmap_add_damage(a: &mut dyn Args) -> Cell {
    let (Some(w), t, d) = (waypoint_arg(a, 1), a.cell(2), a.float(3)) else { return 0 };
    with(|s| s.heatmap.add_damage(w, t, d));
    1
}

/// `Float:nav_heatmap_danger(waypoint, bot_team)`
pub fn nav_heatmap_danger(a: &mut dyn Args) -> Cell {
    let (Some(w), t) = (waypoint_arg(a, 1), a.cell(2)) else { return 0 };
    float_cell(with(|s| s.heatmap.danger(w, t)).unwrap_or(0.0))
}

/// `Float:nav_heatmap_cover_penalty(waypoint, bot_team)`
pub fn nav_heatmap_cover_penalty(a: &mut dyn Args) -> Cell {
    let (Some(w), t) = (waypoint_arg(a, 1), a.cell(2)) else { return 0 };
    float_cell(with(|s| s.heatmap.cover_penalty(w, t)).unwrap_or(0.0))
}

/// `Float:nav_heatmap_ambush(waypoint)`
pub fn nav_heatmap_ambush(a: &mut dyn Args) -> Cell {
    let Some(w) = waypoint_arg(a, 1) else { return 0 };
    float_cell(with(|s| s.heatmap.ambush_value(w)).unwrap_or(0.0))
}

// ---- editing ----

/// A player argument (1-32).
fn player_arg(a: &dyn Args, i: usize) -> Option<usize> {
    usize::try_from(a.cell(i)).ok().filter(|&p| (1..=crate::state::MAX_PLAYERS).contains(&p))
}

/// `nav_add(const Float:origin[3], flags = 0, player = 0, bool:auto_flags = true,
/// bool:auto_path = false, bool:draw = true)`: `WaypointAddOrigin`.
pub fn nav_add(a: &mut dyn Args) -> Cell {
    let (origin, flags, player, auto_flags, auto_path, draw) =
        (vec3(a, 1), a.cell(2) as u32, entity_arg(a, 3), a.cell(4) != 0, a.cell(5) != 0, a.cell(6) != 0);
    with(|s| {
        let mut out = Vec::new();
        let r = with_world(|w| {
            let p = player.and_then(|i| w.entity(i));
            editor::add_origin(&mut s.waypoints, w, origin, flags, p.as_ref(), draw, auto_flags, auto_path, &mut out)
        });
        s.push_events(out);
        opt_index(r)
    })
    .unwrap_or(-1)
}

/// `nav_add_aiming(const Float:origin[3], const Float:v_angle[3])`: `WaypointAddAiming`.
pub fn nav_add_aiming(a: &mut dyn Args) -> Cell {
    let (origin, angle) = (vec3(a, 1), vec3(a, 2));
    with(|s| opt_index(s.waypoints.add_aiming(origin, angle))).unwrap_or(-1)
}

/// `bool:nav_delete(waypoint)`: the core of `WaypointDelete`.
pub fn nav_delete(a: &mut dyn Args) -> Cell {
    let w = waypoint_arg(a, 1);
    with(|s| match w.filter(|&w| w < s.waypoints.len()) {
        Some(w) => {
            s.waypoints.delete(w);
            1
        }
        None => 0,
    })
    .unwrap_or(0)
}

/// `nav_delete_near(const Float:origin[3])`: `WaypointDelete`, the waypoint within 50 units.
pub fn nav_delete_near(a: &mut dyn Args) -> Cell {
    let origin = vec3(a, 1);
    with(|s| {
        let mut out = Vec::new();
        let r = with_world(|w| editor::delete(&mut s.waypoints, w, origin, &mut out));
        s.push_events(out);
        r
    })
    .unwrap_or(-1)
}

/// `bool:nav_add_path(from, to)`: `WaypointAddPath` (no duplicate check, as upstream).
pub fn nav_add_path(a: &mut dyn Args) -> Cell {
    let (from, to) = (waypoint_arg(a, 1), waypoint_arg(a, 2));
    with(|s| match (from, to) {
        (Some(f), Some(t)) if f < s.waypoints.len() && t < s.waypoints.len() => {
            s.waypoints.add_path(f, t as i16);
            1
        }
        _ => 0,
    })
    .unwrap_or(0)
}

/// `bool:nav_remove_path(from, to)`: `WaypointDeletePath(from, to)`.
pub fn nav_remove_path(a: &mut dyn Args) -> Cell {
    let (Some(from), Some(to)) = (waypoint_arg(a, 1), waypoint_arg(a, 2)) else { return 0 };
    with(|s| s.waypoints.delete_path(from, to as i16));
    1
}

/// `bool:nav_remove_paths_to(waypoint)`: `WaypointDeletePath(waypoint)`.
pub fn nav_remove_paths_to(a: &mut dyn Args) -> Cell {
    let Some(w) = waypoint_arg(a, 1) else { return 0 };
    with(|s| s.waypoints.delete_paths_to(w as i16));
    1
}

/// `bool:nav_set_origin(waypoint, const Float:origin[3])`: as the auto-waypointer moves one (the
/// location buckets keep the old place, as upstream).
pub fn nav_set_origin(a: &mut dyn Args) -> Cell {
    let (w, o) = (waypoint_arg(a, 1), vec3(a, 2));
    with(|s| match w.filter(|&w| w < s.waypoints.len()) {
        Some(w) => {
            s.waypoints.set_origin(w, o);
            1
        }
        None => 0,
    })
    .unwrap_or(0)
}

fn edit_flags(a: &mut dyn Args, f: impl FnOnce(u32, u32) -> u32) -> Cell {
    let (w, bits) = (waypoint_arg(a, 1), a.cell(2) as u32);
    with(|s| match w.filter(|&w| w < s.waypoints.len()) {
        Some(w) => {
            let new = f(s.waypoints.flags(w as i32), bits);
            s.waypoints.set_flags(w, new);
            new as Cell
        }
        None => 0,
    })
    .unwrap_or(0)
}

/// `nav_set_flags(waypoint, flags)`: returns the new flags.
pub fn nav_set_flags(a: &mut dyn Args) -> Cell {
    edit_flags(a, |_, b| b)
}

/// `nav_add_flags(waypoint, flags)`
pub fn nav_add_flags(a: &mut dyn Args) -> Cell {
    edit_flags(a, |old, b| old | b)
}

/// `nav_clear_flags(waypoint, flags)`
pub fn nav_clear_flags(a: &mut dyn Args) -> Cell {
    edit_flags(a, |old, b| old & !b)
}

fn path_edit(a: &mut dyn Args, create: bool) -> Cell {
    let (player, origin, cmd) = (player_arg(a, 1), vec3(a, 2), a.cell(3));
    let Some(player) = player else {
        a.error(&format!("Player {} is not valid", a.cell(1)));
        return 0;
    };
    with(|s| {
        let mut out = Vec::new();
        let State { waypoints, edit, .. } = s;
        let e = &mut edit[player];
        with_world(|w| {
            if create {
                editor::create_path(waypoints, w, e, origin, cmd, &mut out);
            } else {
                editor::remove_path(waypoints, w, e, origin, cmd, &mut out);
            }
        });
        let picked = if create { e.create_from } else { e.remove_from };
        s.push_events(out);
        picked
    })
    .unwrap_or(-1)
}

/// `nav_edit_create_path(player, const Float:origin[3], cmd)`: `WaypointCreatePath` (1 picks the
/// source, 2 the destination); returns the picked source.
pub fn nav_edit_create_path(a: &mut dyn Args) -> Cell {
    path_edit(a, true)
}

/// `nav_edit_remove_path(player, const Float:origin[3], cmd)`: `WaypointRemovePath`.
pub fn nav_edit_remove_path(a: &mut dyn Args) -> Cell {
    path_edit(a, false)
}

/// `bool:nav_auto_waypoint(player, bool:show = true, bool:debug = false, bool:auto_path = true)`:
/// `CClient::Think`'s auto-waypointing for the player, once per think.
pub fn nav_auto_waypoint(a: &mut dyn Args) -> Cell {
    let (player, show, debug, auto_path) = (player_arg(a, 1), a.cell(2) != 0, a.cell(3) != 0, a.cell(4) != 0);
    let Some(player) = player else {
        a.error(&format!("Player {} is not valid", a.cell(1)));
        return 0;
    };
    with(|s| {
        let mut out = Vec::new();
        let State { waypoints, auto, .. } = s;
        let done = with_world(|w| match w.entity(player as u32) {
            Some(p) => {
                auto[player].think(waypoints, w, &p, show, debug, auto_path, &mut out);
                true
            }
            None => false,
        });
        s.push_events(out);
        done as Cell
    })
    .unwrap_or(0)
}

/// `nav_auto_reset(player)`
pub fn nav_auto_reset(a: &mut dyn Args) -> Cell {
    let Some(player) = player_arg(a, 1) else { return 0 };
    with(|s| s.auto[player].reset());
    1
}

/// `bool:nav_can_stand(const Float:origin[3], Float:floor[3])`: `UTIL_CanStand`.
pub fn nav_can_stand(a: &mut dyn Args) -> Cell {
    let origin = vec3(a, 1);
    let (ok, floor) = with_world(|w| editor::can_stand(w, origin));
    set_vec3(a, 2, floor);
    ok as Cell
}

// ---- draw data ----

/// `nav_draw_index(waypoint)`: `WaypointDrawIndex`'s beams, queued.
pub fn nav_draw_index(a: &mut dyn Args) -> Cell {
    let Some(w) = waypoint_arg(a, 1) else { return 0 };
    with(|s| {
        let ev = editor::draw_index(&s.waypoints, w);
        let n = ev.len();
        s.push_events(ev);
        n as Cell
    })
    .unwrap_or(0)
}

/// `nav_draw_around(viewer, const Float:origin[3], bool:paths = true)`: `DrawWaypoints` and the
/// path display, queued; returns the waypoint the viewer stands at or -1.
pub fn nav_draw_around(a: &mut dyn Args) -> Cell {
    let (viewer, origin, paths) = (a.cell(1).max(0) as u32, vec3(a, 2), a.cell(3) != 0);
    with(|s| {
        let mut out = Vec::new();
        let cur = with_world(|w| editor::draw_waypoints(&s.waypoints, w, viewer, origin, paths, &mut out));
        s.push_events(out);
        cur.unwrap_or(-1)
    })
    .unwrap_or(-1)
}

/// `nav_draw_path(RcPath:path, r = 255, g = 0, b = 0)`: a beam along each step of a bot's path
/// (debug view; a port addition), queued; returns the beams.
pub fn nav_draw_path(a: &mut dyn Args) -> Cell {
    let h = a.cell(1);
    let rgb = [a.cell(2) as u8, a.cell(3) as u8, a.cell(4) as u8];
    let r = with(|s| {
        let p = s.paths.get(h)?;
        let up = Vector::new(0.0, 0.0, 8.0);
        let pts: Vec<Vector> = p.bot.bot_paths.iter().rev().map(|&w| s.waypoints.origin(w as i32) + up).collect();
        let beams: Vec<EditorEvent> = pts
            .windows(2)
            .map(|w| EditorEvent::Beam(editor::Beam { start: w[0], end: w[1], width: 10, noise: 0, rgb, brightness: 200, speed: 10 }))
            .collect();
        let n = beams.len();
        s.push_events(beams);
        Some(n as Cell)
    })
    .flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid path handle {h}"));
        0
    })
}

/// `nav_event_count()`
pub fn nav_event_count(_: &mut dyn Args) -> Cell {
    with(|s| s.events.len() as Cell).unwrap_or(0)
}

/// `NavEvent:nav_event_pop(Float:start[3], Float:end[3], params[7], text[], len)`: the oldest
/// queued event (`NAV_EVENT_NONE` when there is none). Beams fill `start`, `end` and `params`
/// (width, noise, r, g, b, brightness, speed); sounds and messages fill `text`.
pub fn nav_event_pop(a: &mut dyn Args) -> Cell {
    let Some(e) = with(|s| s.events.pop_front()).flatten() else { return 0 };
    match e {
        EditorEvent::Beam(b) => {
            set_vec3(a, 1, b.start);
            set_vec3(a, 2, b.end);
            let p = [b.width, b.noise, b.rgb[0].into(), b.rgb[1].into(), b.rgb[2].into(), b.brightness, b.speed];
            a.write_array(3, 0, &p);
            EVENT_BEAM
        }
        EditorEvent::Sound(sample) => {
            set_out(a, 4, sample.as_bytes());
            EVENT_SOUND
        }
        EditorEvent::Message(text) => {
            set_out(a, 4, text.as_bytes());
            EVENT_MESSAGE
        }
    }
}

/// `nav_event_clear()`
pub fn nav_event_clear(_: &mut dyn Args) -> Cell {
    with(|s| s.events.clear());
    1
}
