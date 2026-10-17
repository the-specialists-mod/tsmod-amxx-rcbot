// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions. Ports what
// tsmod-rcbot-rs's bot keeps per bot for A* and does around it: `bot/navigate.rs`
// (`search_path`, `threat_waypoints`, `crowded_waypoints`, `fail_path`, `roam_goal`,
// `claim_factor`, `got_stuck`), `bot/think.rs` (`task_find_path`, `push_current_on_path`, the
// failed-goal clearing of `Think`), `bot/mod.rs` (`next_waypoint`) and `bot/goals.rs`
// (`Explore`). RCBot's `BotNavigate_AStarAlgo` and `CBotFailedPaths` are by Paul "Cheeseh" Murphy.

//! Per-bot A* paths by handle (`RcPath`): the search state that carries over between frames
//! (`CBot::aPathsFound`, `sOpenList`), the found path (`m_stBotPaths`), failed paths and goals,
//! the tactical inputs (threats, teammates' paths) and the bot's exploration times.
//!
//! **Incremental search**: `nav_path_find(..., resume = false)` starts a search and expands at
//! most `max_loops` waypoints (the profile's `m_iPathRevs`); when it runs out the status is
//! `NAV_PATH_SEARCHING` and the open list stays in the path object. The bot calls
//! `nav_path_find(..., resume = true)` (or `nav_path_continue`) on a later frame to carry on,
//! exactly as `task_find_path` does with `continue_search` (task float -3). A resumed search must
//! use the same flags (tactics are read again at each call, as the standalone bot rebuilds them).

use crate::natives::{size, vec3, waypoint_arg, write_cells};
use crate::state::{NavRandom, State, with};
use crate::world::with_world;
use amxx::Cell;
use amxx::native::Args;
use rcbot::learn::Heatmap;
use rcbot::nav::astar::DEFAULT_PATH_REVS;
use rcbot::nav::{
    AStar, AStarBot, AStarQuery, AStarResult, DangerSource, MAX_WAYPOINTS, NearestOptions, SearchOptions, Tactics, Waypoints,
};
use rcbot::{Random, Vector};

/// `nav_path_find` flags: upstream's search.
pub const FIND_UPSTREAM: Cell = 0;
/// Tactical costs (danger from the heatmap, exposure to threats, teammates' paths).
pub const FIND_TACTICS: Cell = 1;
/// A goal no path reaches stays failed and is backed off for 30-60 s.
pub const FIND_BACK_OFF: Cell = 2;

/// The search's state (`nav_path_status`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    /// No search yet (or reset).
    Idle = 0,
    /// Out of expansions: resume it on a later frame.
    Searching = 1,
    /// A path was found: the stack holds it.
    Found = 2,
    /// No path (or invalid waypoints, or start = goal).
    Failed = 3,
}

/// `goals.rs` `FORGET_VISIT`: seconds after which a visit doesn't count.
const FORGET_VISIT: f32 = 120.0;
/// `goals.rs` `NEAR_GOAL`: goals nearer than this are worth a quarter.
const NEAR_GOAL: f32 = 400.0;
/// `navigate.rs` `CLAIM_RANGE`: teammates' goals nearer than this make one worth less.
const CLAIM_RANGE: f32 = 500.0;

/// One bot's navigation state.
pub struct BotPath {
    pub astar: AStar,
    pub bot: AStarBot,
    pub status: Status,
    /// The last search's request and flags (`nav_path_continue` reuses them).
    pub query: AStarQuery,
    pub flags: Cell,
    /// Tactical weights: danger, exposure, crowding (`search_path`: 2.0, 1.5, 0.5).
    pub weights: [f32; 3],
    /// Waypoints enemies were seen at, with weights.
    pub threats: Vec<(usize, f32)>,
    /// Waypoints on teammates' paths, sorted.
    pub crowded: Vec<usize>,
    /// When the bot was last at each waypoint (`Explore::visited`).
    pub visited: Vec<f32>,
}

impl Default for BotPath {
    fn default() -> Self {
        BotPath {
            astar: AStar::new(),
            bot: AStarBot::default(),
            status: Status::Idle,
            query: AStarQuery { from: None, to: None, gun_position: Vector::ZERO, team: -1, max_loops: DEFAULT_PATH_REVS, time: 0.0 },
            flags: FIND_UPSTREAM,
            weights: [2.0, 1.5, 0.5],
            threats: Vec::new(),
            crowded: Vec::new(),
            visited: vec![f32::NEG_INFINITY; MAX_WAYPOINTS],
        }
    }
}

impl BotPath {
    /// `Explore::since_visit`.
    fn since_visit(&self, i: usize, now: f32) -> f32 {
        self.visited.get(i).map_or(FORGET_VISIT, |&t| (now - t).clamp(0.0, FORGET_VISIT))
    }

    /// `GetNextWaypoint`: the waypoint after `current` on the path (the first stack entry from
    /// the top that isn't `current`), or `current`.
    pub fn next_after(&self, current: i32) -> i32 {
        let mut next = current;
        for &w in self.bot.bot_paths.iter().rev() {
            if next != current {
                break;
            }
            next = w as i32;
        }
        next
    }
}

/// What a path native may use besides the path: the other parts of the state.
pub struct Ctx<'a> {
    pub waypoints: &'a mut Waypoints,
    pub heatmap: &'a Heatmap,
    pub rng: &'a mut NavRandom,
}

/// Runs `f` on the path behind argument 1 with the rest of the state, or raises the native
/// error.
fn with_path(a: &mut dyn Args, f: impl FnOnce(&mut BotPath, Ctx<'_>, &mut dyn Args) -> Cell) -> Cell {
    let h = a.cell(1);
    let r = with(|s| {
        let State { paths, waypoints, heatmap, rng, .. } = s;
        paths.get_mut(h).map(|p| f(p, Ctx { waypoints, heatmap, rng }, a))
    })
    .flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid path handle {h}"));
        0
    })
}

/// Runs `f` on the path behind argument 1 alone (no state access), or raises the native error.
fn with_path_only(a: &mut dyn Args, f: impl FnOnce(&mut BotPath, &mut dyn Args) -> Cell) -> Cell {
    let h = a.cell(1);
    let r = with(|s| s.paths.get_mut(h).map(|p| f(p, a))).flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid path handle {h}"));
        0
    })
}

/// `RcPath:nav_path_create()`
pub fn nav_path_create(_: &mut dyn Args) -> Cell {
    with(|s| s.paths.insert(BotPath::default())).unwrap_or(0)
}

/// `bool:nav_path_free(&RcPath:path)`
pub fn nav_path_free(a: &mut dyn Args) -> Cell {
    let h = a.read_array(1, 1)[0];
    if with(|s| s.paths.remove(h)).flatten().is_none() {
        a.error(&format!("Invalid path handle {h}"));
        return 0;
    }
    a.set_ref(1, 0);
    1
}

/// `search_path`: A* with the path's options; the heatmap is the danger source.
fn search(p: &mut BotPath, s: Ctx<'_>, resume: bool) -> Status {
    let Ctx { waypoints, heatmap, rng } = s;
    let BotPath { astar, bot, query, flags, weights, threats, crowded, .. } = p;
    let danger: &dyn DangerSource = heatmap;
    let tactics = (*flags & FIND_TACTICS != 0).then_some(Tactics {
        danger,
        team: query.team,
        danger_weight: weights[0],
        threats,
        exposure_weight: weights[1],
        crowded,
        crowd_weight: weights[2],
    });
    let opts = SearchOptions { back_off_failed_goals: *flags & FIND_BACK_OFF != 0, tactics };
    let r = with_world(|w| astar.search_with(bot, query, resume, waypoints, w, rng, &opts));
    p.status = match r {
        AStarResult::Next(_) => Status::Found,
        AStarResult::NoPath => Status::Failed,
        AStarResult::Continue => Status::Searching,
    };
    p.status
}

/// `NavPathStatus:nav_path_find(RcPath:path, from, to, const Float:gun_pos[3], team = -1,
/// max_loops = 150, flags = NAV_FIND_UPSTREAM, bool:resume = false)`
pub fn nav_path_find(a: &mut dyn Args) -> Cell {
    let time = with_world(|w| w.time());
    let q = AStarQuery {
        from: waypoint_arg(a, 2),
        to: waypoint_arg(a, 3),
        gun_position: vec3(a, 4),
        team: a.cell(5),
        max_loops: a.cell(6).max(1),
        time,
    };
    let (flags, resume) = (a.cell(7), a.cell(8) != 0);
    with_path(a, |p, s, _| {
        p.query = q;
        p.flags = flags;
        search(p, s, resume) as Cell
    })
}

/// `NavPathStatus:nav_path_continue(RcPath:path)`
pub fn nav_path_continue(a: &mut dyn Args) -> Cell {
    let time = with_world(|w| w.time());
    with_path(a, |p, s, _| {
        if p.status != Status::Searching {
            return p.status as Cell;
        }
        p.query.time = time;
        search(p, s, true) as Cell
    })
}

/// `NavPathStatus:nav_path_status(RcPath:path)`
pub fn nav_path_status(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| p.status as Cell)
}

/// `nav_path_next(RcPath:path)`: the top of the stack.
pub fn nav_path_next(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| p.bot.bot_paths.last().map_or(-1, |&w| w as Cell))
}

/// `nav_path_pop(RcPath:path)`
pub fn nav_path_pop(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| p.bot.bot_paths.pop().map_or(-1, |w| w as Cell))
}

/// `nav_path_push(RcPath:path, waypoint, bool:unless_top = true)`
pub fn nav_path_push(a: &mut dyn Args) -> Cell {
    let (w, unless_top) = (waypoint_arg(a, 2), a.cell(3) != 0);
    with_path_only(a, |p, _| {
        let Some(w) = w else { return 0 };
        if unless_top && p.bot.bot_paths.last() == Some(&w) {
            return 0;
        }
        p.bot.bot_paths.push(w);
        1
    })
}

/// `nav_path_length(RcPath:path)`
pub fn nav_path_length(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| p.bot.bot_paths.len() as Cell)
}

/// `nav_path_get(RcPath:path, out[], len)`: walking order (next first, goal last).
pub fn nav_path_get(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, a| {
        let n = size(a, 3);
        write_cells(a, 2, n, p.bot.bot_paths.iter().rev().map(|&w| w as Cell))
    })
}

/// `bool:nav_path_contains(RcPath:path, waypoint)`
pub fn nav_path_contains(a: &mut dyn Args) -> Cell {
    let w = waypoint_arg(a, 2);
    with_path_only(a, |p, _| w.is_some_and(|w| p.bot.bot_paths.contains(&w)) as Cell)
}

/// `nav_path_after(RcPath:path, current)`: `GetNextWaypoint`.
pub fn nav_path_after(a: &mut dyn Args) -> Cell {
    let cur = a.cell(2);
    with_path_only(a, |p, _| p.next_after(cur))
}

/// `nav_path_clear(RcPath:path)`: the found path only (`m_stBotPaths.Destroy()`).
pub fn nav_path_clear(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| {
        p.bot.bot_paths.clear();
        1
    })
}

/// `nav_path_reset(RcPath:path)`: everything but the tactical weights and the visits.
pub fn nav_path_reset(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| {
        p.bot = AStarBot::default();
        p.status = Status::Idle;
        p.threats.clear();
        p.crowded.clear();
        1
    })
}

/// `bool:nav_path_fail(RcPath:path, from, to, team = -1)`: `fail_path`.
pub fn nav_path_fail(a: &mut dyn Args) -> Cell {
    let (from, to, team) = (waypoint_arg(a, 2), waypoint_arg(a, 3), a.cell(4));
    with_path(a, |p, s, _| {
        let (Some(from), Some(to)) = (from, to) else { return 0 };
        match s.waypoints.find_path_from_to(from, to, team) {
            Some(r) => {
                p.bot.failed_paths.add(r);
                1
            }
            None => 0,
        }
    })
}

/// `nav_path_failed_count(RcPath:path)`
pub fn nav_path_failed_count(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| p.bot.failed_paths.len() as Cell)
}

/// `nav_path_clear_failed_paths(RcPath:path)`
pub fn nav_path_clear_failed_paths(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| {
        p.bot.failed_paths.clear();
        1
    })
}

/// `nav_path_goal_reached(RcPath:path, waypoint)`: `update_waypoint`'s "a goal reached isn't a
/// roaming goal again for a while".
pub fn nav_path_goal_reached(a: &mut dyn Args) -> Cell {
    let w = waypoint_arg(a, 2);
    let now = with_world(|w| w.time());
    with_path(a, |p, s, _| {
        let Some(w) = w else { return 0 };
        if p.bot.failed_goals.contains(&w) {
            return 0;
        }
        p.bot.failed_goals.push(w);
        p.bot.next_clear_failed_goals = now + s.rng.random_float(5.0, 10.0);
        1
    })
}

/// `nav_path_failed_goals(RcPath:path, out[], len)`
pub fn nav_path_failed_goals(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, a| {
        let n = size(a, 3);
        write_cells(a, 2, n, p.bot.failed_goals.iter().map(|&w| w as Cell))
    })
}

/// `nav_path_ignored_goals(RcPath:path, out[], len)`
pub fn nav_path_ignored_goals(a: &mut dyn Args) -> Cell {
    let now = with_world(|w| w.time());
    with_path_only(a, |p, a| {
        let g = p.bot.ignored_goals(now);
        let n = size(a, 3);
        write_cells(a, 2, n, g.iter().map(|&w| w as Cell))
    })
}

/// `nav_path_clear_failed_goals(RcPath:path)`
pub fn nav_path_clear_failed_goals(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| {
        p.bot.failed_goals.clear();
        p.bot.next_clear_failed_goals = 0.0;
        1
    })
}

/// `nav_path_think(RcPath:path)`: `Think`'s clearing of failed goals and expired back-offs.
pub fn nav_path_think(a: &mut dyn Args) -> Cell {
    let now = with_world(|w| w.time());
    with_path_only(a, |p, _| {
        if !p.bot.failed_goals.is_empty() && p.bot.next_clear_failed_goals < now {
            p.bot.failed_goals.clear();
        }
        p.bot.expire_backed_off_goals(now);
        1
    })
}

/// `nav_path_set_weights(RcPath:path, Float:danger = 2.0, Float:exposure = 1.5, Float:crowd = 0.5)`
pub fn nav_path_set_weights(a: &mut dyn Args) -> Cell {
    let w = [a.float(2), a.float(3), a.float(4)];
    with_path_only(a, |p, _| {
        p.weights = w;
        1
    })
}

/// `nav_path_set_threats(RcPath:path, const waypoints[], const Float:weights[], num)`
pub fn nav_path_set_threats(a: &mut dyn Args) -> Cell {
    let n = size(a, 4);
    let (wps, weights) = (a.read_array(2, n), a.read_array(3, n));
    with_path_only(a, |p, _| {
        p.threats.clear();
        for (&w, &wt) in wps.iter().zip(&weights) {
            if let Ok(w) = usize::try_from(w) {
                p.threats.push((w, f32::from_bits(wt as u32)));
            }
        }
        p.threats.len() as Cell
    })
}

/// `nav_path_add_threat(RcPath:path, const Float:origin[3], Float:weight)`: `threat_waypoints`
/// for one sighting: the waypoint nearest it (400 units, no visibility check), weight at least
/// 0.1, the highest kept. Returns the waypoint or -1.
pub fn nav_path_add_threat(a: &mut dyn Args) -> Cell {
    let (origin, weight) = (vec3(a, 2), a.float(3));
    with_path(a, |p, s, _| {
        let opts = NearestOptions { visible: false, unreachable: false, is_bot: false, aiming_only: false };
        let Some(i) = with_world(|w| s.waypoints.nearest_waypoint(origin, 400.0, None, opts, &[], w)) else { return -1 };
        let weight = weight.max(0.1);
        match p.threats.iter_mut().find(|(x, _)| *x == i) {
            Some(e) => e.1 = e.1.max(weight),
            None => p.threats.push((i, weight)),
        }
        i as Cell
    })
}

/// `nav_path_threats(RcPath:path, waypoints[], Float:weights[], len)`
pub fn nav_path_threats(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, a| {
        let n = size(a, 4);
        write_cells(a, 3, n, p.threats.iter().map(|&(_, wt)| wt.to_bits() as Cell));
        write_cells(a, 2, n, p.threats.iter().map(|&(w, _)| w as Cell))
    })
}

/// `nav_path_clear_threats(RcPath:path)`
pub fn nav_path_clear_threats(a: &mut dyn Args) -> Cell {
    with_path_only(a, |p, _| {
        p.threats.clear();
        1
    })
}

/// `nav_path_set_crowded(RcPath:path, const waypoints[], num)`
pub fn nav_path_set_crowded(a: &mut dyn Args) -> Cell {
    let n = size(a, 3);
    let wps = a.read_array(2, n);
    with_path_only(a, |p, _| {
        p.crowded.clear();
        p.crowded.extend(wps.iter().filter_map(|&w| usize::try_from(w).ok()));
        p.crowded.sort_unstable();
        p.crowded.dedup();
        p.crowded.len() as Cell
    })
}

/// `nav_path_crowd_from(RcPath:path, const RcPath:others[], num)`: `TeamBoard::paths_of_others`
/// from the teammates' path objects (the path itself is skipped).
pub fn nav_path_crowd_from(a: &mut dyn Args) -> Cell {
    let h = a.cell(1);
    let n = size(a, 3);
    let others = a.read_array(2, n);
    let r = with(|s| {
        // Into the path's own buffer, so a bot doing this each think allocates nothing.
        let mut crowded = std::mem::take(&mut s.paths.get_mut(h)?.crowded);
        crowded.clear();
        for &o in others.iter().filter(|&&o| o != h) {
            if let Some(o) = s.paths.get(o) {
                crowded.extend_from_slice(&o.bot.bot_paths);
            }
        }
        crowded.sort_unstable();
        crowded.dedup();
        let len = crowded.len() as Cell;
        s.paths.get_mut(h)?.crowded = crowded;
        Some(len)
    })
    .flatten();
    r.unwrap_or_else(|| {
        a.error(&format!("Invalid path handle {h}"));
        0
    })
}

/// `nav_path_touch(RcPath:path, waypoint)`: `Explore::touch` now.
pub fn nav_path_touch(a: &mut dyn Args) -> Cell {
    let w = waypoint_arg(a, 2);
    let now = with_world(|w| w.time());
    with_path_only(a, |p, _| match w.and_then(|w| p.visited.get_mut(w)) {
        Some(v) => {
            *v = now;
            1
        }
        None => 0,
    })
}

/// `Float:nav_path_since_visit(RcPath:path, waypoint)`
pub fn nav_path_since_visit(a: &mut dyn Args) -> Cell {
    let w = waypoint_arg(a, 2);
    let now = with_world(|w| w.time());
    with_path_only(a, |p, _| rcbot_amxx_common::float_cell(w.map_or(FORGET_VISIT, |w| p.since_visit(w, now))))
}

/// `nav_path_roam_goal(RcPath:path, team, const Float:origin[3], const claims[] = {}, num_claims = 0)`:
/// `roam_goal`: a usable waypoint drawn from all of them, more likely the longer the bot hasn't
/// been there (`Explore::weight`) and the farther from teammates' claimed goals (`claim_factor`),
/// never a failed or backed-off goal. -1 for none.
pub fn nav_path_roam_goal(a: &mut dyn Args) -> Cell {
    let (team, from) = (a.cell(2), vec3(a, 3));
    let n = size(a, 5);
    let claims: Vec<i32> = a.read_array(4, n).into_iter().filter(|&c| c >= 0).collect();
    let now = with_world(|w| w.time());
    with_path(a, |p, s, _| {
        let ignore = p.bot.ignored_goals(now);
        let Ctx { waypoints, rng, .. } = s;
        let weight = |i: usize| {
            let mut w = 0.25 + p.since_visit(i, now) / 30.0;
            let at = waypoints.origin(i as i32);
            if (at - from).length() < NEAR_GOAL {
                w *= 0.25;
            }
            let mut f = 1.0f32;
            for &c in &claims {
                let d = (waypoints.origin(c) - at).length();
                if d < CLAIM_RANGE {
                    f = f.min(0.2 + 0.8 * d / CLAIM_RANGE);
                }
            }
            w * f
        };
        waypoints.find_weighted_goal(team, 0, &ignore, weight, rng).map_or(-1, |i| i as Cell)
    })
}
