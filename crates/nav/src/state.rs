// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions. The file handling
// ports tsmod-rcbot-rs's host: `commands.rs` `load_waypoints` (the `.rcw`, its `.rcv`, the rows
// the `.rcv` doesn't hold worked out) and `waypoint_save`; `bots.rs` `load_heatmap` /
// `save_globals` (the map's `.rhm`). RCBot's `WaypointLoad`, `WaypointSave` and
// `CWaypointVisibilityTable` are by Paul "Cheeseh" Murphy.

//! The module's state: the map's waypoints (upstream's `waypoints`, `paths`,
//! `WaypointLocations`, `WaypointVisibility` globals, one [`Waypoints`] here), the heatmap, the
//! per-bot A* paths by handle, each player's editing state, the editor's event queue, the data
//! directory and the random numbers.
//!
//! AMX Mod X calls natives and metamod calls `StartFrame` on the server's main thread, so the
//! state is thread-local (and each test, on its own thread, gets its own). Nothing in here calls
//! back into a plugin.

use crate::paths::BotPath;
use crate::world::with_world;
use rcbot::bot::BotWorld;
use rcbot::learn::Heatmap;
use rcbot::nav::editor::{AutoWaypointer, EditorEvent, PathEdit};
use rcbot::nav::{Glass, Monsters, VisibilityTable, Waypoints, flags as fl};
use rcbot::{Random, Vector};
use rcbot_amxx_common::{DataDir, Handles, ModuleRandom};
use std::cell::RefCell;
use std::collections::VecDeque;

/// The mod folder the waypoints live under (`waypoints/<mod>/<map>.rcw`; the host's
/// `config.mod_folder`, "ts").
pub const MOD_FOLDER: &str = "ts";
/// Player slots (entity indexes 1-32).
pub const MAX_PLAYERS: usize = 32;
/// The most editor events kept for a plugin that doesn't read them.
pub const MAX_EVENTS: usize = 4096;
/// Visibility traces per frame while rows are missing ([`State::vis_budget`]'s default): about
/// three rows of a 300-waypoint map.
pub const DEFAULT_VIS_BUDGET: usize = 1024;

/// The module's random numbers. `RANDOM_LONG` / `RANDOM_FLOAT` (the standalone host's
/// `EngineRandom`) aren't among tsmod-amxx-rs v0.3.0's engine wrappers, so this is the seeded
/// `ModuleRandom` (`nav_seed`) until they are; swapping it is this type only.
pub type NavRandom = ModuleRandom;

pub struct State {
    pub dir: DataDir,
    pub rng: NavRandom,
    /// The map the waypoints are for (set by `nav_load`, even when it had no file, so a new set
    /// saves under the right name).
    pub map: String,
    pub waypoints: Waypoints,
    /// The next visibility row to work out: rows `vis_next..len()` are missing (a `.rcv` holds
    /// only the leading rows, design §5.1 of tsmod-rcbot-rs) and are traced a few per frame.
    pub vis_next: usize,
    /// Visibility traces per frame (at least one row is done each frame).
    pub vis_budget: usize,
    pub heatmap: Heatmap,
    pub paths: Handles<BotPath>,
    /// Each player's `pathwaypoint create1/remove1` pick (index 1-32).
    pub edit: [PathEdit; MAX_PLAYERS + 1],
    /// Each player's auto-waypointer (index 1-32).
    pub auto: Vec<AutoWaypointer>,
    /// What edits asked to show (beams, sounds, messages), oldest first.
    pub events: VecDeque<EditorEvent>,
}

impl State {
    fn new(dir: DataDir) -> State {
        State {
            dir,
            rng: ModuleRandom::from_clock(),
            map: String::new(),
            waypoints: Waypoints::new(),
            vis_next: 0,
            vis_budget: DEFAULT_VIS_BUDGET,
            heatmap: Heatmap::default(),
            paths: Handles::default(),
            edit: [PathEdit::default(); MAX_PLAYERS + 1],
            auto: vec![AutoWaypointer::default(); MAX_PLAYERS + 1],
            events: VecDeque::new(),
        }
    }

    /// `waypoints/ts/<map>.<ext>`.
    pub fn file(map: &str, ext: &str) -> String {
        format!("waypoints/{MOD_FOLDER}/{map}.{ext}")
    }

    /// The host's `load_waypoints`: `waypoints/ts/<map>.rcw`, and its `.rcv` unless the header
    /// says it is stale. The rows the `.rcv` doesn't hold are worked out over the next frames
    /// ([`State::frame`]) instead of at once. `Err` says why nothing was loaded (the waypoints are
    /// empty then, as the host leaves them).
    pub fn load(&mut self, map: &str) -> Result<usize, String> {
        self.map = map.to_owned();
        self.waypoints = Waypoints::new();
        self.vis_next = 0;
        let rcw = Self::file(map, "rcw");
        let bytes = self.dir.read(&rcw).ok_or_else(|| format!("no waypoints for {map} ({rcw})"))?;
        let (mut w, header) = Waypoints::from_rcw(&bytes, map).map_err(|e| format!("{rcw}: {e}"))?;
        let n = w.len();
        let mut first_row = 0;
        if !header.needs_visibility()
            && let Some(rcv) = self.dir.read(&Self::file(map, "rcv"))
            && w.visibility.read_rcv(&rcv, n).is_ok()
        {
            first_row = VisibilityTable::rows_in_file(n);
        }
        self.waypoints = w;
        self.vis_next = first_row;
        Ok(n)
    }

    /// The host's `waypoint_save` (`WaypointSave(false, NULL)`): the `.rcw`, then the `.rcv`.
    /// Missing visibility rows are worked out first, so the file is whole. False with no map.
    pub fn save(&mut self) -> bool {
        if self.map.is_empty() {
            return false;
        }
        with_world(|w| self.finish_visibility(w));
        let n = self.waypoints.len();
        let rcv = self.waypoints.visibility.to_rcv(n);
        let rcw = self.waypoints.to_rcw(&self.map, rcv.is_some());
        let ok = self.dir.write_logged(&Self::file(&self.map, "rcw"), &rcw);
        if ok && let Some(v) = rcv {
            self.dir.write_logged(&Self::file(&self.map, "rcv"), &v);
        }
        ok
    }

    /// `WaypointInit`: no waypoints (the map name stays).
    pub fn clear(&mut self) {
        self.waypoints = Waypoints::new();
        self.vis_next = 0;
    }

    /// Visibility rows still to work out.
    pub fn vis_pending(&self) -> usize {
        self.waypoints.len().saturating_sub(self.vis_next)
    }

    /// `WorkOutVisibilityTable` for row `i` alone: what
    /// [`VisibilityTable::work_out_rows`] does for each row (traces to 16 units above every other
    /// waypoint; deleted waypoints stay invisible; each waypoint sees itself). Returns the traces
    /// made.
    fn work_out_row(&mut self, i: usize, world: &dyn BotWorld) -> usize {
        let wps = &mut self.waypoints;
        let n = wps.len();
        let Some(w1) = wps.get(i).copied() else { return 0 };
        if w1.flags & fl::DELETED != 0 {
            return 0;
        }
        let mut traces = 0;
        for j in 0..n {
            if i == j {
                wps.visibility.set(i, j, true);
                continue;
            }
            let w2 = wps.get(j).copied().unwrap_or_default();
            if w2.flags & fl::DELETED != 0 {
                continue;
            }
            let tr = world.trace_line(w1.origin, w2.origin + Vector::new(0.0, 0.0, 16.0), Monsters::Ignore, Glass::Ignore, None);
            wps.visibility.set(i, j, tr.fraction >= 1.0);
            traces += 1;
        }
        traces
    }

    /// One frame's share of the missing visibility rows: whole rows while they fit in
    /// [`State::vis_budget`] traces (at least one row).
    pub fn frame_visibility(&mut self, world: &dyn BotWorld) {
        // A row costs a trace per other waypoint; stop before the one that would go over.
        let row_cost = self.waypoints.len().saturating_sub(1).max(1);
        let mut spent = 0;
        while self.vis_next < self.waypoints.len() && (spent == 0 || spent + row_cost <= self.vis_budget) {
            spent += self.work_out_row(self.vis_next, world).max(1);
            self.vis_next += 1;
        }
    }

    /// Every missing visibility row, now.
    pub fn finish_visibility(&mut self, world: &dyn BotWorld) {
        while self.vis_next < self.waypoints.len() {
            self.work_out_row(self.vis_next, world);
            self.vis_next += 1;
        }
    }

    /// The host's `load_heatmap`: the map's `.rhm` for the waypoints as loaded (a new one when the
    /// file is missing or was made for other waypoints), faded once for this load. True if the
    /// file was used.
    pub fn load_heatmap(&mut self) -> bool {
        let (map, n) = (self.map.clone(), self.waypoints.len());
        let loaded = self.dir.read(&heatmap_path(&map)).and_then(|b| Heatmap::from_bytes(&b, &map, n));
        let used = loaded.is_some();
        let mut h = loaded.unwrap_or_else(|| Heatmap::new(&map, n));
        h.decay(rcbot::learn::heatmap::MAP_DECAY);
        self.heatmap = h;
        used
    }

    /// The host's `save_globals` for the heatmap: written when there is one.
    pub fn save_heatmap(&self) -> bool {
        let h = &self.heatmap;
        !h.is_empty() && self.dir.write_logged(&heatmap_path(h.map_name()), &h.to_bytes())
    }

    /// Queues what an edit asked to show, dropping the oldest past [`MAX_EVENTS`].
    pub fn push_events(&mut self, events: impl IntoIterator<Item = EditorEvent>) {
        for e in events {
            if self.events.len() >= MAX_EVENTS {
                self.events.pop_front();
            }
            self.events.push_back(e);
        }
    }

    /// The random numbers, as `rcbot` takes them.
    pub fn rng(&mut self) -> &mut dyn Random {
        &mut self.rng
    }
}

/// `waypoints/ts/<map>.rhm`.
pub fn heatmap_path(map: &str) -> String {
    format!("waypoints/{MOD_FOLDER}/{}", rcbot::learn::heatmap::heatmap_file_name(map))
}

thread_local! {
    static STATE: RefCell<State> = RefCell::new(State::new(DataDir::new(rcbot_amxx_common::DATA_DIR)));
}

/// Runs `f` on the state; `None` if it is already borrowed (never, as nothing re-enters).
pub fn with<R>(f: impl FnOnce(&mut State) -> R) -> Option<R> {
    STATE.with(|s| s.try_borrow_mut().ok().map(|mut s| f(&mut s)))
}

/// `AMXX_Attach`: the data directory as the core names it.
pub fn attach(dir: DataDir) {
    with(|s| s.dir = dir);
}

/// `StartFrame`: a share of the missing visibility rows. Cheap when there are none.
pub fn frame() {
    with(|s| {
        if s.vis_next < s.waypoints.len() {
            with_world(|w| s.frame_visibility(w));
        }
    });
}

/// After the plugins unloaded (map change): their path handles and editing state are gone. The
/// waypoints stay until the next `nav_load` (the next map's plugin_init).
pub fn forget_handles() {
    with(|s| {
        s.paths.clear();
        s.edit = [PathEdit::default(); MAX_PLAYERS + 1];
        s.auto.iter_mut().for_each(AutoWaypointer::reset);
        s.events.clear();
    });
}
