// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions.

//! The natives against a fake AMX ([`FakeArgs`]), a fake world and a temporary data directory.
//! Each test runs on its own thread, so each has its own module state and world. File checks
//! compare against what `rcbot::nav` itself writes, which is what the standalone plugin writes;
//! searches compare against `rcbot`'s A* called directly.

use crate::natives::{self as n, EVENT_BEAM, EVENT_SOUND, NEAREST_BOT, NEAREST_VISIBLE};
use crate::paths::{self as p, FIND_BACK_OFF, FIND_TACTICS, FIND_UPSTREAM, Status};
use crate::state::{self, with};
use crate::world::set_test_world;
use amxx::Cell;
use amxx::native::FakeArgs;
use rcbot::bot::{BotWorld, Entity, entflags};
use rcbot::learn::Heatmap;
use rcbot::nav::{
    AStar, AStarBot, AStarQuery, Glass, Monsters, SearchOptions, Tactics, Trace, VisibilityTable, Waypoints, World, flags as fl,
};
use rcbot::{Vector, XorShiftRandom};
use rcbot_amxx_common::data_dir::temp_dir;
use rcbot_amxx_common::{DataDir, ModuleRandom, float_cell};
use std::cell::{Cell as StdCell, RefCell};
use std::path::PathBuf;
use std::rc::Rc;

/// Walls on planes `x = c`, a clock, entities by index, and a PVS that ends at `x = pvs_end`.
#[derive(Default)]
struct FakeWorld {
    walls: Vec<f32>,
    time: StdCell<f32>,
    entities: RefCell<Vec<Entity>>,
    traces: StdCell<usize>,
    pvs_end: Option<f32>,
    pvs_viewer: StdCell<u32>,
}

impl World for FakeWorld {
    fn trace_line(&self, start: Vector, end: Vector, _: Monsters, _: Glass, _: Option<u32>) -> Trace {
        self.traces.set(self.traces.get() + 1);
        if self.walls.iter().any(|&x| (start.x < x) != (end.x < x)) {
            return Trace { fraction: 0.5, end_pos: start, hit: Some(0) };
        }
        Trace::clear(end)
    }
    fn point_contents(&self, _: Vector) -> i32 {
        -1
    }
}

impl BotWorld for FakeWorld {
    fn time(&self) -> f32 {
        self.time.get()
    }
    fn max_entities(&self) -> u32 {
        64
    }
    fn max_clients(&self) -> u32 {
        32
    }
    fn entity(&self, index: u32) -> Option<Entity> {
        self.entities.borrow().iter().find(|e| e.index == index).cloned()
    }
    fn in_pvs(&self, viewer: u32, origin: Vector) -> bool {
        self.pvs_viewer.set(viewer);
        self.pvs_end.is_none_or(|x| origin.x < x)
    }
    fn entities_in_sphere(&self, origin: Vector, radius: f32) -> Vec<u32> {
        self.entities.borrow().iter().filter(|e| (e.origin - origin).length() <= radius).map(|e| e.index).collect()
    }
    fn teamplay(&self) -> bool {
        false
    }
}

fn setup(name: &str, world: FakeWorld) -> (PathBuf, Rc<FakeWorld>) {
    let d = temp_dir(name);
    state::attach(DataDir::new(&d));
    with(|s| s.rng = ModuleRandom::seeded(1));
    let w = Rc::new(world);
    set_test_world(Some(w.clone()));
    (d, w)
}

fn f(v: f32) -> Cell {
    float_cell(v)
}

fn fl_(c: Cell) -> f32 {
    f32::from_bits(c as u32)
}

fn v3(a: &mut FakeArgs, x: f32, y: f32, z: f32) -> Cell {
    a.alloc_floats(&[x, y, z])
}

/// A 3x3 grid, 100 units apart, connected both ways along rows and columns (rcbot's A* tests).
fn grid() -> Waypoints {
    let mut w = Waypoints::new();
    for y in 0..3 {
        for x in 0..3 {
            w.add_origin(Vector::new(x as f32 * 100.0, y as f32 * 100.0, 0.0), 0, None);
        }
    }
    for i in 0..9usize {
        let (x, y) = (i % 3, i / 3);
        if x < 2 {
            w.add_path(i, (i + 1) as i16);
            w.add_path(i + 1, i as i16);
        }
        if y < 2 {
            w.add_path(i, (i + 3) as i16);
            w.add_path(i + 3, i as i16);
        }
    }
    w
}

/// Writes `w` for `map` as rcbot writes it (with its whole visibility table worked out in
/// `world`), and loads it through the native.
fn install(d: &std::path::Path, map: &str, mut w: Waypoints, world: &dyn World) -> (Vec<u8>, Vec<u8>) {
    let wpts = w.as_slice().to_vec();
    w.visibility.work_out(&wpts, world);
    let rcv = w.visibility.to_rcv(w.len()).unwrap();
    let rcw = w.to_rcw(map, true);
    std::fs::create_dir_all(d.join("waypoints/ts")).unwrap();
    std::fs::write(d.join(format!("waypoints/ts/{map}.rcw")), &rcw).unwrap();
    std::fs::write(d.join(format!("waypoints/ts/{map}.rcv")), &rcv).unwrap();
    let mut a = FakeArgs::new(&[]);
    let m = a.alloc_str(map);
    a.params = vec![m];
    assert_eq!(n::nav_load(&mut a), 1, "{:?}", a.errors);
    (rcw, rcv)
}

#[test]
fn files_round_trip_byte_for_byte() {
    let (d, world) = setup("roundtrip", FakeWorld { walls: vec![150.0], ..FakeWorld::default() });
    let mut w = grid();
    w.set_flags(4, fl::CROUCH | fl::STUNT);
    let (rcw, rcv) = install(&d, "ts_grid", w, &*world);
    let mut a = FakeArgs::new(&[]);
    assert_eq!(n::nav_count(&mut a), 9);
    // A 9-waypoint .rcv holds row 0 only (ceil(81 / 8) bytes of a 1024-wide table).
    assert_eq!(n::nav_vis_pending(&mut a), 8);
    state::frame();
    assert_eq!(n::nav_vis_pending(&mut a), 0, "eight rows fit one frame's budget");
    // Saved again: rcbot's own bytes.
    assert_eq!(n::nav_save(&mut a), 1);
    assert_eq!(std::fs::read(d.join("waypoints/ts/ts_grid.rcw")).unwrap(), rcw);
    assert_eq!(std::fs::read(d.join("waypoints/ts/ts_grid.rcv")).unwrap(), rcv);
    let out = a.alloc(vec![0; 32]);
    a.params = vec![out, 31];
    n::nav_map(&mut a);
    assert_eq!(a.read_str(out), "ts_grid");
    // An edit, saved: what rcbot writes for the same edit.
    a.params = vec![4, fl::JUMP as Cell];
    assert_eq!(n::nav_add_flags(&mut a), (fl::CROUCH | fl::STUNT | fl::JUMP) as Cell);
    a.params = vec![];
    assert_eq!(n::nav_save(&mut a), 1);
    let want = with(|s| s.waypoints.to_rcw("ts_grid", true)).unwrap();
    let (back, _) = Waypoints::from_rcw(&std::fs::read(d.join("waypoints/ts/ts_grid.rcw")).unwrap(), "ts_grid").unwrap();
    assert_eq!(back.to_rcw("ts_grid", true), want);
    assert_eq!(back.flags(4), fl::CROUCH | fl::STUNT | fl::JUMP);
    // Another map's file is refused; the waypoints are empty, the name stays for saving.
    let m = a.alloc_str("ts_other");
    std::fs::copy(d.join("waypoints/ts/ts_grid.rcw"), d.join("waypoints/ts/ts_other.rcw")).unwrap();
    a.params = vec![m];
    assert_eq!(n::nav_load(&mut a), 0);
    a.params = vec![];
    assert_eq!(n::nav_count(&mut a), 0);
    a.params = vec![out, 31];
    n::nav_map(&mut a);
    assert_eq!(a.read_str(out), "ts_other");
}

#[test]
fn missing_visibility_rows_are_traced_over_frames() {
    let (d, world) = setup("visrows", FakeWorld { walls: vec![2000.0], ..FakeWorld::default() });
    // 154 waypoints: a .rcv holds rows 0-23 only (tsmod-rcbot-rs design §5.1).
    let mut w = Waypoints::new();
    for i in 0..154 {
        w.add_origin(Vector::new((i % 40) as f32 * 100.0 - 2000.0, (i / 40) as f32 * 100.0, 0.0), 0, None);
    }
    let mut full = w.clone();
    let wpts = w.as_slice().to_vec();
    full.visibility.work_out(&wpts, &*world);
    let (_, rcv) = install(&d, "ts_big", w, &*world);
    let mut a = FakeArgs::new(&[]);
    assert_eq!(n::nav_vis_pending(&mut a) as usize, 154 - VisibilityTable::rows_in_file(154));
    a.params = vec![154 * 2];
    assert_eq!(n::nav_vis_budget(&mut a), state::DEFAULT_VIS_BUDGET as Cell);
    world.traces.set(0);
    state::frame();
    assert_eq!(n::nav_vis_pending(&mut a), 130 - 2, "two rows per frame at this budget");
    assert_eq!(world.traces.get(), 2 * 153);
    for _ in 0..100 {
        state::frame();
    }
    assert_eq!(n::nav_vis_pending(&mut a), 0);
    assert_eq!(with(|s| s.waypoints.visibility.as_bytes() == full.visibility.as_bytes()), Some(true));
    // Saving while rows are missing finishes them first: rcbot's file of the whole table.
    a.params = vec![0];
    n::nav_vis_rebuild(&mut a);
    assert_eq!(n::nav_vis_pending(&mut a), 154);
    assert_eq!(n::nav_save(&mut a), 1);
    assert_eq!(std::fs::read(d.join("waypoints/ts/ts_big.rcv")).unwrap(), rcv);
    assert_eq!(full.visibility.to_rcv(154).unwrap(), rcv);
    // A table read in full from file and the visibility native.
    a.params = vec![0, 153];
    assert_eq!(n::nav_visible(&mut a), full.visibility.get(0, 153) as Cell);
}

#[test]
fn queries() {
    let (d, world) = setup("queries", FakeWorld { walls: vec![150.0], ..FakeWorld::default() });
    let mut w = grid();
    w.set_flags(2, fl::HEALTH);
    w.set_flags(8, fl::HEALTH);
    w.set_flags(3, fl::JUMP);
    install(&d, "ts_q", w, &*world);
    let mut a = FakeArgs::new(&[]);
    n::nav_vis_finish(&mut a);
    // Nearest from (120, 0): 1, visible; with a wall at 150 the nearest visible from (160, 0) is 2.
    let o = v3(&mut a, 120.0, 0.0, 0.0);
    a.params = vec![o, f(400.0), -1, NEAREST_VISIBLE, 0, 0];
    assert_eq!(n::nav_nearest(&mut a), 1);
    let ign = a.alloc(vec![1]);
    a.params = vec![o, f(400.0), -1, NEAREST_VISIBLE, ign, 1];
    assert_eq!(n::nav_nearest(&mut a), 4, "failed list");
    let o2 = v3(&mut a, 160.0, 0.0, 0.0);
    a.params = vec![o2, f(400.0), -1, NEAREST_VISIBLE, 0, 0];
    assert_eq!(n::nav_nearest(&mut a), 2);
    let o3 = v3(&mut a, 0.0, 90.0, 0.0);
    a.params = vec![o3, f(400.0), -1, NEAREST_VISIBLE | NEAREST_BOT, 0, 0];
    assert_eq!(n::nav_nearest(&mut a), 0, "a bot doesn't start from a jump waypoint");
    a.params = vec![o, o, 0, f(400.0), -1];
    assert_eq!(n::nav_nearest_scan(&mut a), 1);
    // Goals by flag.
    a.params = vec![o, fl::HEALTH as Cell, f(99999.0), -1, 0, 0];
    assert_eq!(n::nav_nearest_flag(&mut a), 2);
    a.params = vec![fl::HEALTH as Cell, -1, 0, 0];
    let g = n::nav_random_flag(&mut a);
    assert!(g == 2 || g == 8);
    a.params = vec![o, f(150.0), fl::HEALTH as Cell, -1, 0, 0];
    assert_eq!(n::nav_random_flag_near(&mut a), 2);
    // Paths.
    let out = a.alloc(vec![0; 8]);
    a.params = vec![4, out, 8, -1];
    assert_eq!(n::nav_paths_out(&mut a), 4);
    assert_eq!(&a.memory[out as usize][..4], &[1, 3, 5, 7]);
    a.params = vec![4, out, 2];
    assert_eq!(n::nav_paths_in(&mut a), 4, "the count is all of them; two written");
    a.params = vec![0, 1, -1];
    assert_eq!(n::nav_has_path(&mut a), 1);
    a.params = vec![0, 4, -1];
    assert_eq!(n::nav_has_path(&mut a), 0);
    a.params = vec![2, f(0.0)];
    let dist_from = v3(&mut a, 0.0, 0.0, 0.0);
    a.params = vec![2, dist_from];
    assert_eq!(fl_(n::nav_distance(&mut a)), 200.0);
    let og = a.alloc_floats(&[0.0; 3]);
    a.params = vec![5, og];
    assert_eq!(n::nav_origin(&mut a), 1);
    assert_eq!(a.read_floats(og, 3), [200.0, 100.0, 0.0]);
    a.params = vec![77, og];
    assert_eq!(n::nav_origin(&mut a), 0);
    a.params = vec![8];
    assert_eq!(n::nav_flags(&mut a), fl::HEALTH as Cell);
    assert_eq!(n::nav_valid(&mut a), 1);
    // Buckets: every waypoint is in the one bucket around the origin.
    a.params = vec![o, out, 8];
    assert_eq!(n::nav_buckets_around(&mut a), 9);
    assert_eq!(n::nav_nearby(&mut a), 9);
    // Reachability (rcbot's rules): too far, blocked by the wall.
    let (s1, s2, s3) = (v3(&mut a, 0.0, 0.0, 0.0), v3(&mut a, 100.0, 0.0, 0.0), v3(&mut a, 200.0, 0.0, 0.0));
    a.params = vec![s1, s2, 1];
    assert_eq!(n::nav_reachable(&mut a), 1);
    a.params = vec![s1, s3, 1];
    assert_eq!(n::nav_reachable(&mut a), 0);
    let ofs = v3(&mut a, 0.0, 0.0, 0.0);
    a.params = vec![o2, ofs, 0, f(400.0), -1];
    assert_eq!(n::nav_find_reachable(&mut a), 2);
    // Visibility from the table: 0 can't see 2 through the wall.
    a.params = vec![0, 2];
    assert_eq!(n::nav_visible(&mut a), 0);
    a.params = vec![0, 1];
    assert_eq!(n::nav_visible(&mut a), 1);
}

#[test]
fn cover_takes_the_heatmap_penalty() {
    let (d, world) = setup("cover", FakeWorld::default());
    let mut w = Waypoints::new();
    let enemy = w.add_origin(Vector::new(0.0, 0.0, 0.0), 0, None).unwrap();
    let seen = w.add_origin(Vector::new(100.0, 0.0, 0.0), 0, None).unwrap();
    let hidden = w.add_origin(Vector::new(150.0, 0.0, 0.0), 0, None).unwrap();
    let far = w.add_origin(Vector::new(300.0, 0.0, 0.0), 0, None).unwrap();
    install(&d, "ts_cover", w, &*world);
    // An empty world sees everything: hide `hidden` and `far` from the enemy's waypoint.
    with(|s| {
        s.waypoints.visibility.clear();
        s.waypoints.visibility.set(enemy, seen, true);
    });
    let mut a = FakeArgs::new(&[]);
    let (pl, from) = (v3(&mut a, 110.0, 0.0, 0.0), v3(&mut a, 0.0, 0.0, 0.0));
    a.params = vec![pl, from, 0, 0, 0, 1];
    assert_eq!(n::nav_cover(&mut a), hidden as Cell);
    a.params = vec![];
    n::nav_heatmap_load(&mut a);
    for _ in 0..10 {
        // Deaths of rules team 1 (RCBot team 0) at the near hiding place.
        a.params = vec![hidden as Cell, 1];
        n::nav_heatmap_add_death(&mut a);
    }
    a.params = vec![hidden as Cell, 0];
    let pen = fl_(n::nav_heatmap_cover_penalty(&mut a));
    assert!(pen > 150.0, "{pen}");
    a.params = vec![pl, from, 0, 0, 0, 1];
    assert_eq!(n::nav_cover(&mut a), far as Cell);
    a.params = vec![pl, from, 0, 0, 0, 0];
    assert_eq!(n::nav_cover(&mut a), hidden as Cell, "without the heatmap: upstream's choice");
    let ign = a.alloc(vec![hidden as Cell, far as Cell]);
    a.params = vec![pl, from, ign, 2, 0, 0];
    assert_eq!(n::nav_cover(&mut a), -1);
}

#[test]
fn heatmap_files() {
    let (d, _) = setup("heatmap", FakeWorld::default());
    let mut a = FakeArgs::new(&[]);
    let mut w = Waypoints::new();
    for i in 0..10 {
        w.add_origin(Vector::new(i as f32 * 50.0, 0.0, 0.0), 0, None);
    }
    install(&d, "ts_dojo", w, &rcbot::nav::EmptyWorld);
    assert_eq!(n::nav_heatmap_load(&mut a), 0, "no file yet");
    for _ in 0..3 {
        a.params = vec![4, 1];
        n::nav_heatmap_add_death(&mut a);
    }
    a.params = vec![4, 1, f(50.0)];
    n::nav_heatmap_add_damage(&mut a);
    a.params = vec![5, 2];
    n::nav_heatmap_add_kill(&mut a);
    // Counts take the rules team (1), queries RCBot's team (rules team - 1).
    a.params = vec![4, 1];
    assert_eq!(fl_(n::nav_heatmap_danger(&mut a)), 0.0);
    a.params = vec![4, 0];
    let want = with(|s| s.heatmap.danger(4, 0)).unwrap();
    assert_eq!(fl_(n::nav_heatmap_danger(&mut a)), want);
    assert!(want > 0.5);
    a.params = vec![5];
    assert_eq!(fl_(n::nav_heatmap_ambush(&mut a)), with(|s| s.heatmap.ambush_value(5)).unwrap());
    a.params = vec![];
    assert_eq!(n::nav_heatmap_save(&mut a), 1);
    let file = d.join("waypoints/ts/ts_dojo.rhm");
    let bytes = std::fs::read(&file).unwrap();
    assert_eq!(bytes, with(|s| s.heatmap.to_bytes()).unwrap());
    // rcbot reads it back as the same counts (then fades it once, as each load does).
    let mut back = Heatmap::from_bytes(&bytes, "ts_dojo", 10).unwrap();
    assert_eq!(n::nav_heatmap_load(&mut a), 1);
    back.decay(rcbot::learn::heatmap::MAP_DECAY);
    assert_eq!(with(|s| s.heatmap.to_bytes()).unwrap(), back.to_bytes());
    // Other waypoints: a new heatmap.
    a.params = vec![v3(&mut a, 900.0, 0.0, 0.0), 0, 0, 0, 0, 0];
    n::nav_add(&mut a);
    a.params = vec![];
    assert_eq!(n::nav_heatmap_load(&mut a), 0);
}

fn query(from: usize, to: usize, max_loops: i32) -> AStarQuery {
    AStarQuery { from: Some(from), to: Some(to), gun_position: Vector::ZERO, team: -1, max_loops, time: 0.0 }
}

/// `nav_path_find(path, from, to, gun, team, max_loops, flags, resume)`.
fn find(a: &mut FakeArgs, path: Cell, from: Cell, to: Cell, max_loops: Cell, flags: Cell, resume: bool) -> Cell {
    let gun = a.alloc_floats(&[0.0; 3]);
    a.params = vec![path, from, to, gun, -1, max_loops, flags, resume as Cell];
    p::nav_path_find(a)
}

fn path_list(a: &mut FakeArgs, path: Cell) -> Vec<Cell> {
    let out = a.alloc(vec![0; 64]);
    a.params = vec![path, out, 64];
    let len = p::nav_path_get(a) as usize;
    a.memory[out as usize][..len].to_vec()
}

#[test]
fn incremental_search_matches_rcbot() {
    let (d, world) = setup("astar", FakeWorld::default());
    install(&d, "ts_grid", grid(), &*world);
    let mut a = FakeArgs::new(&[]);
    let path = p::nav_path_create(&mut a);
    assert!(path > 0);
    // Two expansions per call: searching, then resumed until found.
    assert_eq!(find(&mut a, path, 0, 8, 2, FIND_UPSTREAM, false), Status::Searching as Cell);
    a.params = vec![path];
    assert_eq!(p::nav_path_status(&mut a), Status::Searching as Cell);
    let mut calls = 1;
    loop {
        a.params = vec![path];
        let st = p::nav_path_continue(&mut a);
        calls += 1;
        if st != Status::Searching as Cell {
            assert_eq!(st, Status::Found as Cell);
            break;
        }
    }
    assert!(calls > 2);
    // rcbot's own incremental search gives the same path.
    let mut w = grid();
    let (mut astar, mut bot, mut rng) = (AStar::new(), AStarBot::default(), XorShiftRandom::new(1));
    let q = query(0, 8, 2);
    let mut r = astar.search(&mut bot, &q, false, &mut w, &rcbot::nav::EmptyWorld, &mut rng);
    while r == rcbot::nav::AStarResult::Continue {
        r = astar.search(&mut bot, &q, true, &mut w, &rcbot::nav::EmptyWorld, &mut rng);
    }
    let want: Vec<Cell> = bot.bot_paths.iter().rev().map(|&w| w as Cell).collect();
    assert_eq!(path_list(&mut a, path), want);
    assert_eq!(*want.last().unwrap(), 8);
    // Walking it: next, pop, and GetNextWaypoint.
    a.params = vec![path];
    assert_eq!(p::nav_path_next(&mut a), want[0]);
    a.params = vec![path, want[0]];
    assert_eq!(p::nav_path_after(&mut a), want[1], "the current one on top is skipped");
    a.params = vec![path, 0, 1];
    assert_eq!(p::nav_path_push(&mut a), 1);
    assert_eq!(p::nav_path_push(&mut a), 0, "already on top");
    a.params = vec![path];
    assert_eq!(p::nav_path_pop(&mut a), 0);
    assert_eq!(p::nav_path_pop(&mut a), want[0]);
    assert_eq!(p::nav_path_length(&mut a), want.len() as Cell - 1);
    a.params = vec![path, 8];
    assert_eq!(p::nav_path_contains(&mut a), 1);
    a.params = vec![path];
    p::nav_path_clear(&mut a);
    assert_eq!(p::nav_path_next(&mut a), -1);
    // A resume with nothing searching: the status stays.
    assert_eq!(p::nav_path_continue(&mut a), Status::Found as Cell);
    a.params = vec![path];
    assert_eq!(p::nav_path_free(&mut a), 0, "free takes a reference");
    let r = a.alloc(vec![path]);
    a.params = vec![r];
    assert_eq!(p::nav_path_free(&mut a), 1);
    assert_eq!(a.memory[r as usize][0], 0);
    a.params = vec![path];
    p::nav_path_next(&mut a);
    assert_eq!(a.errors.last().unwrap(), &format!("Invalid path handle {path}"));
}

#[test]
fn tactics_use_the_heatmap_threats_and_teammates() {
    let (d, world) = setup("tactics", FakeWorld::default());
    install(&d, "ts_grid", grid(), &*world);
    let mut a = FakeArgs::new(&[]);
    a.params = vec![];
    n::nav_heatmap_load(&mut a);
    for wp in [3, 4, 6] {
        for _ in 0..5 {
            a.params = vec![wp, 1];
            n::nav_heatmap_add_death(&mut a);
        }
    }
    let path = p::nav_path_create(&mut a);
    // Upstream's search goes through the danger; tactical costs go round it.
    assert_eq!(find(&mut a, path, 0, 8, 150, FIND_UPSTREAM, false), Status::Found as Cell);
    let plain = path_list(&mut a, path);
    let gun = a.alloc_floats(&[0.0; 3]);
    a.params = vec![path, 0, 8, gun, 0, 150, FIND_TACTICS | FIND_BACK_OFF, 0];
    assert_eq!(p::nav_path_find(&mut a), Status::Found as Cell);
    let tactical = path_list(&mut a, path);
    // What rcbot's search_with finds with the same heatmap and weights.
    let heat = with(|s| s.heatmap.clone()).unwrap();
    let mut w = grid();
    let t = Tactics { danger: &heat, team: 0, danger_weight: 2.0, threats: &[], exposure_weight: 1.5, crowded: &[], crowd_weight: 0.5 };
    let opts = SearchOptions { back_off_failed_goals: true, tactics: Some(t) };
    let mut bot = AStarBot::default();
    let q = AStarQuery { team: 0, ..query(0, 8, 150) };
    AStar::new().search_with(&mut bot, &q, false, &mut w, &rcbot::nav::EmptyWorld, &mut XorShiftRandom::new(1), &opts);
    let want: Vec<Cell> = bot.bot_paths.iter().rev().map(|&w| w as Cell).collect();
    assert_eq!(tactical, want);
    assert_eq!(tactical, [1, 2, 5, 8]);
    assert_eq!(plain.len(), 4);

    // Teammates: a second bot whose path is through 1 and 2 pushes this one the other way once
    // the danger doesn't count.
    let other = p::nav_path_create(&mut a);
    for wp in [8, 5, 2, 1] {
        a.params = vec![other, wp, 1];
        p::nav_path_push(&mut a);
    }
    // Set by hand, the crowded list is sorted and deduplicated.
    let o = a.alloc(vec![7, 3, 7, -1, 6]);
    a.params = vec![other, o, 5];
    assert_eq!(p::nav_path_set_crowded(&mut a), 3);
    assert_eq!(with(|s| s.paths.get(other).unwrap().crowded.clone()).unwrap(), [3, 6, 7]);
    a.params = vec![path, f(0.0), f(0.0), f(4.0)];
    p::nav_path_set_weights(&mut a);
    let others = a.alloc(vec![other, path, 999]);
    a.params = vec![path, others, 3];
    assert_eq!(p::nav_path_crowd_from(&mut a), 4);
    a.params = vec![path, 0, 8, gun, 0, 150, FIND_TACTICS, 0];
    assert_eq!(p::nav_path_find(&mut a), Status::Found as Cell);
    assert_eq!(path_list(&mut a, path), [3, 6, 7, 8]);

    // Threats: an enemy seen near waypoint 6 (which sees 3 and 7) does the same.
    a.params = vec![path, f(0.0), f(4.0), f(0.0)];
    p::nav_path_set_weights(&mut a);
    with(|s| {
        s.waypoints.visibility.clear();
        s.waypoints.visibility.set(6, 3, true);
        s.waypoints.visibility.set(6, 7, true);
    });
    let seen = v3(&mut a, 10.0, 210.0, 0.0);
    a.params = vec![path, seen, f(0.05)];
    assert_eq!(p::nav_path_add_threat(&mut a), 6);
    let (tw, twt) = (a.alloc(vec![0; 4]), a.alloc(vec![0; 4]));
    a.params = vec![path, tw, twt, 4];
    assert_eq!(p::nav_path_threats(&mut a), 1);
    assert_eq!((a.memory[tw as usize][0], fl_(a.memory[twt as usize][0])), (6, 0.1), "weights are at least 0.1");
    let (sw, swt) = (a.alloc(vec![6]), a.alloc_floats(&[1.0]));
    a.params = vec![path, sw, swt, 1];
    assert_eq!(p::nav_path_set_threats(&mut a), 1);
    a.params = vec![path, 0, 8, gun, 0, 150, FIND_TACTICS, 0];
    assert_eq!(p::nav_path_find(&mut a), Status::Found as Cell);
    assert_eq!(path_list(&mut a, path), [1, 2, 5, 8]);
    a.params = vec![path];
    p::nav_path_clear_threats(&mut a);
    a.params = vec![path, tw, twt, 4];
    assert_eq!(p::nav_path_threats(&mut a), 0);
}

#[test]
fn failed_goals_paths_and_roaming() {
    let (d, world) = setup("failed", FakeWorld::default());
    let mut w = grid();
    w.add_origin(Vector::new(1000.0, 0.0, 0.0), 0, None); // 9, no paths
    install(&d, "ts_grid", w, &*world);
    let mut a = FakeArgs::new(&[]);
    let path = p::nav_path_create(&mut a);
    world.time.set(100.0);
    // Upstream: failed, remembered, then toggled off by a second failure.
    assert_eq!(find(&mut a, path, 0, 9, 150, FIND_UPSTREAM, false), Status::Failed as Cell);
    let out = a.alloc(vec![0; 8]);
    a.params = vec![path, out, 8];
    assert_eq!(p::nav_path_failed_goals(&mut a), 1);
    assert_eq!(a.memory[out as usize][0], 9);
    find(&mut a, path, 0, 9, 150, FIND_UPSTREAM, false);
    a.params = vec![path, out, 8];
    assert_eq!(p::nav_path_failed_goals(&mut a), 0);
    // Backed off: stays failed and ignored after the failed goals are cleared.
    find(&mut a, path, 0, 9, 150, FIND_BACK_OFF, false);
    find(&mut a, path, 0, 9, 150, FIND_BACK_OFF, false);
    a.params = vec![path, out, 8];
    assert_eq!(p::nav_path_failed_goals(&mut a), 1);
    a.params = vec![path];
    p::nav_path_clear_failed_goals(&mut a);
    a.params = vec![path, out, 8];
    assert_eq!(p::nav_path_ignored_goals(&mut a), 1);
    world.time.set(200.0);
    a.params = vec![path];
    p::nav_path_think(&mut a);
    a.params = vec![path, out, 8];
    assert_eq!(p::nav_path_ignored_goals(&mut a), 0, "the back-off expired");
    // A goal reached is a failed goal until Think clears it 5-10 s later.
    a.params = vec![path, 4];
    assert_eq!(p::nav_path_goal_reached(&mut a), 1);
    assert_eq!(p::nav_path_goal_reached(&mut a), 0);
    world.time.set(204.0);
    a.params = vec![path];
    p::nav_path_think(&mut a);
    a.params = vec![path, out, 8];
    assert_eq!(p::nav_path_failed_goals(&mut a), 1);
    world.time.set(211.0);
    a.params = vec![path];
    p::nav_path_think(&mut a);
    a.params = vec![path, out, 8];
    assert_eq!(p::nav_path_failed_goals(&mut a), 0);
    // A failed path blocks its node; a search failing on it forgets it.
    a.params = vec![path, 0, 1, -1];
    assert_eq!(p::nav_path_fail(&mut a), 1);
    a.params = vec![path, 0, 4, -1];
    assert_eq!(p::nav_path_fail(&mut a), 0, "no such path");
    a.params = vec![path];
    assert_eq!(p::nav_path_failed_count(&mut a), 1);
    assert_eq!(find(&mut a, path, 0, 2, 150, FIND_UPSTREAM, false), Status::Failed as Cell, "0's only node holds 0-1 and 0-3");
    a.params = vec![path];
    assert_eq!(p::nav_path_failed_count(&mut a), 0);
    a.params = vec![path, 0, 1, -1];
    p::nav_path_fail(&mut a);
    a.params = vec![path];
    p::nav_path_clear_failed_paths(&mut a);
    assert_eq!(p::nav_path_failed_count(&mut a), 0);
    // Roaming: never an ignored goal; a visit makes a waypoint less likely.
    a.params = vec![path, 1];
    p::nav_path_touch(&mut a);
    a.params = vec![path, 1];
    assert_eq!(fl_(p::nav_path_since_visit(&mut a)), 0.0);
    a.params = vec![path, 2];
    assert_eq!(fl_(p::nav_path_since_visit(&mut a)), 120.0);
    let from = v3(&mut a, 5000.0, 0.0, 0.0);
    let mut counts = [0; 10];
    for _ in 0..2000 {
        a.params = vec![path, -1, from, 0, 0];
        let g = p::nav_path_roam_goal(&mut a);
        counts[g as usize] += 1;
    }
    assert!(counts[1] * 5 < counts[0], "{counts:?}");
    // Claimed near 8: less likely.
    let claims = a.alloc(vec![8]);
    let mut claimed = [0; 10];
    for _ in 0..2000 {
        a.params = vec![path, -1, from, claims, 1];
        claimed[p::nav_path_roam_goal(&mut a) as usize] += 1;
    }
    assert!(claimed[8] * 2 < counts[8] && claimed[9] > counts[9], "{claimed:?} {counts:?}");
    a.params = vec![path];
    p::nav_path_reset(&mut a);
    assert_eq!(p::nav_path_status(&mut a), Status::Idle as Cell);
}

fn pop_events(a: &mut FakeArgs) -> Vec<(Cell, String)> {
    let (s, e, prm, txt) = (a.alloc_floats(&[0.0; 3]), a.alloc_floats(&[0.0; 3]), a.alloc(vec![0; 7]), a.alloc(vec![0; 64]));
    let mut out = Vec::new();
    loop {
        a.params = vec![s, e, prm, txt, 63];
        let k = n::nav_event_pop(a);
        if k == 0 {
            break;
        }
        out.push((k, if k == EVENT_BEAM { format!("{:?}", &a.memory[prm as usize][2..5]) } else { a.read_str(txt) }));
    }
    out
}

#[test]
fn editing_and_draw_data() {
    let player = Entity { index: 1, flags: entflags::DUCKING | entflags::CLIENT, origin: Vector::ZERO, health: 100.0, ..Entity::default() };
    let (d, world) = setup("editor", FakeWorld::default());
    world.entities.borrow_mut().push(player);
    let mut a = FakeArgs::new(&[]);
    let m = a.alloc_str("ts_new");
    a.params = vec![m];
    assert_eq!(n::nav_load(&mut a), 0, "no file: a new set for this map");
    // Added for a ducking player: crouch, with the editor's sound and beam.
    let o = v3(&mut a, 0.0, 0.0, 0.0);
    a.params = vec![o, 0, 1, 1, 0, 1];
    assert_eq!(n::nav_add(&mut a), 0);
    a.params = vec![0];
    assert_eq!(n::nav_flags(&mut a), fl::CROUCH as Cell);
    let ev = pop_events(&mut a);
    assert_eq!(ev[0], (EVENT_SOUND, "weapons/xbow_hit1.wav".into()));
    assert_eq!(ev[1].0, EVENT_BEAM);
    let o2 = v3(&mut a, 200.0, 0.0, 0.0);
    a.params = vec![o2, fl::JUMP as Cell, 0, 0, 1, 0];
    assert_eq!(n::nav_add(&mut a), 1);
    a.params = vec![0, 1];
    assert_eq!(n::nav_has_path(&mut a), 1, "auto path");
    a.params = vec![0, 1];
    assert_eq!(n::nav_remove_path(&mut a), 1);
    a.params = vec![0, 1];
    assert_eq!(n::nav_has_path(&mut a), 0);
    // The player's create1/create2.
    a.params = vec![1, o, 1];
    assert_eq!(n::nav_edit_create_path(&mut a), 0);
    a.params = vec![1, o2, 2];
    n::nav_edit_create_path(&mut a);
    a.params = vec![0, 1];
    assert_eq!(n::nav_has_path(&mut a), 1);
    a.params = vec![1, o, 1];
    n::nav_edit_remove_path(&mut a);
    a.params = vec![1, o2, 2];
    n::nav_edit_remove_path(&mut a);
    a.params = vec![0, 1];
    assert_eq!(n::nav_has_path(&mut a), 0);
    a.params = vec![40, o, 1];
    n::nav_edit_create_path(&mut a);
    assert_eq!(a.errors.last().unwrap(), "Player 40 is not valid");
    a.params = vec![0, 1];
    assert_eq!(n::nav_add_path(&mut a), 1);
    a.params = vec![5, 1];
    assert_eq!(n::nav_add_path(&mut a), 0, "no waypoint 5");
    a.params = vec![1];
    n::nav_remove_paths_to(&mut a);
    a.params = vec![0, 1];
    assert_eq!(n::nav_has_path(&mut a), 0);
    // Flags.
    a.params = vec![1, fl::LADDER as Cell];
    assert_eq!(n::nav_set_flags(&mut a), fl::LADDER as Cell);
    a.params = vec![1, fl::DOOR as Cell];
    assert_eq!(n::nav_add_flags(&mut a), (fl::LADDER | fl::DOOR) as Cell);
    a.params = vec![1, fl::LADDER as Cell];
    assert_eq!(n::nav_clear_flags(&mut a), fl::DOOR as Cell);
    // Draw data.
    n::nav_event_clear(&mut a);
    a.params = vec![1];
    assert_eq!(n::nav_draw_index(&mut a), 1);
    a.params = vec![];
    assert_eq!(n::nav_event_count(&mut a), 1);
    assert_eq!(pop_events(&mut a), [(EVENT_BEAM, "[0, 0, 255]".into())], "a door is blue");
    let near = v3(&mut a, 5.0, 0.0, 0.0);
    a.params = vec![1, near, 1];
    assert_eq!(n::nav_draw_around(&mut a), 0, "standing at waypoint 0");
    // Both waypoints' beams, and the path display's (0 has no paths left).
    let ev = pop_events(&mut a);
    assert!(ev.len() >= 2 && ev.iter().all(|e| e.0 == EVENT_BEAM), "{ev:?}");
    let path = p::nav_path_create(&mut a);
    for wp in [1, 0] {
        a.params = vec![path, wp, 1];
        p::nav_path_push(&mut a);
    }
    a.params = vec![path, 0, 255, 0];
    assert_eq!(n::nav_draw_path(&mut a), 1);
    assert_eq!(pop_events(&mut a), [(EVENT_BEAM, "[0, 255, 0]".into())]);
    // Aiming, moving, deleting, standing.
    let ang = v3(&mut a, 0.0, 90.0, 0.0);
    a.params = vec![o, ang];
    assert_eq!(n::nav_add_aiming(&mut a), 2);
    a.params = vec![o];
    assert_eq!(n::nav_nearest_aiming(&mut a), 2);
    let moved = v3(&mut a, 210.0, 0.0, 0.0);
    a.params = vec![1, moved];
    assert_eq!(n::nav_set_origin(&mut a), 1);
    a.params = vec![2];
    assert_eq!(n::nav_delete(&mut a), 1);
    assert_eq!(n::nav_valid(&mut a), 0);
    a.params = vec![near];
    assert_eq!(n::nav_delete_near(&mut a), 0);
    assert_eq!(pop_events(&mut a), [(EVENT_SOUND, "weapons/mine_activate.wav".into())]);
    let floor = a.alloc_floats(&[0.0; 3]);
    a.params = vec![o, floor];
    assert_eq!(n::nav_can_stand(&mut a), 1, "an empty world has room");
    // Saved for the map nav_load named.
    a.params = vec![];
    assert_eq!(n::nav_save(&mut a), 1);
    let (back, _) = Waypoints::from_rcw(&std::fs::read(d.join("waypoints/ts/ts_new.rcw")).unwrap(), "ts_new").unwrap();
    assert_eq!(back.len(), 3, "deleted slots count");
    a.params = vec![];
    n::nav_clear(&mut a);
    assert_eq!(n::nav_count(&mut a), 0);
}

#[test]
fn auto_waypointing_walks() {
    let (_, world) = setup("auto", FakeWorld::default());
    let mut a = FakeArgs::new(&[]);
    let m = a.alloc_str("ts_auto");
    a.params = vec![m];
    n::nav_load(&mut a);
    let walker = |x: f32| Entity {
        index: 3,
        flags: entflags::ONGROUND | entflags::CLIENT,
        origin: Vector::new(x, 0.0, 0.0),
        health: 100.0,
        movetype: rcbot::bot::MOVETYPE_WALK,
        ..Entity::default()
    };
    // Walking 1000 units in 50-unit steps, a think every 0.6 s: a waypoint every 200 units.
    for step in 0..=20 {
        world.time.set(step as f32 * 0.6);
        *world.entities.borrow_mut() = vec![walker(step as f32 * 50.0)];
        a.params = vec![3, 0, 0, 1];
        assert_eq!(n::nav_auto_waypoint(&mut a), 1);
    }
    a.params = vec![];
    let count = n::nav_count(&mut a);
    assert!((3..=6).contains(&count), "{count}");
    a.params = vec![3];
    assert_eq!(n::nav_auto_reset(&mut a), 1);
    world.entities.borrow_mut().clear();
    a.params = vec![3, 0, 0, 1];
    assert_eq!(n::nav_auto_waypoint(&mut a), 0, "no such player");
}

#[test]
fn pvs_asks_the_world_for_the_viewer() {
    let (_, world) = setup("pvs", FakeWorld { pvs_end: Some(500.0), ..FakeWorld::default() });
    let mut a = FakeArgs::new(&[]);
    let near = v3(&mut a, 100.0, 0.0, 0.0);
    let far = v3(&mut a, 900.0, 0.0, 0.0);
    a.params = vec![3, near];
    assert_eq!(n::nav_in_pvs(&mut a), 1);
    assert_eq!(world.pvs_viewer.get(), 3);
    a.params = vec![4, far];
    assert_eq!(n::nav_in_pvs(&mut a), 0);
    assert_eq!(world.pvs_viewer.get(), 4);
    // No viewer: not visible, and the world isn't asked.
    for v in [0, -2] {
        a.params = vec![v, near];
        assert_eq!(n::nav_in_pvs(&mut a), 0);
    }
    assert_eq!(world.pvs_viewer.get(), 4);
}

#[test]
fn invalid_arguments_dont_panic() {
    setup("invalid", FakeWorld::default());
    let mut a = FakeArgs::new(&[]);
    let o = v3(&mut a, 0.0, 0.0, 0.0);
    for w in [-5, 0, 1023, 1024, 99999] {
        a.params = vec![w, o];
        n::nav_origin(&mut a);
        a.params = vec![w];
        n::nav_flags(&mut a);
        n::nav_valid(&mut a);
        n::nav_delete(&mut a);
        n::nav_draw_index(&mut a);
        n::nav_heatmap_danger(&mut a);
        a.params = vec![w, w];
        n::nav_visible(&mut a);
        n::nav_add_path(&mut a);
        n::nav_remove_path(&mut a);
    }
    let path = p::nav_path_create(&mut a);
    for (from, to) in [(-1, 3), (5000, 0), (0, 0)] {
        assert_eq!(find(&mut a, path, from, to, 10, FIND_TACTICS, false), Status::Failed as Cell);
    }
    a.params = vec![path, -3, 1];
    assert_eq!(p::nav_path_push(&mut a), 0);
    a.params = vec![];
    assert_eq!(n::nav_save(&mut a), 0, "no map loaded");
    a.params = vec![0];
    assert_eq!(n::nav_load(&mut a), 0);
    assert_eq!(a.errors.last().unwrap(), "nav_load: no map name (the port's plugin API isn't there)");
}
