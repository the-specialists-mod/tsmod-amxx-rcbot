//! `cargo xtask perf-compare [linux] [win]`: what the three TS bot backends cost the server on the
//! same root, measured the same way: the port's `native` bots (the baseline), the standalone
//! `rcbot` (tsmod_rcbot, `../tsmod-rcbot-rs/dist`, in `ts/dlls/` as `compat` puts it) and
//! `rcbot_amxx` (this repository's Pawn plugin and modules).
//!
//! Per root, with every config, module, plugin and library it touches backed up and restored
//! (`ts/tsmod/rcbot/` held in memory and put back):
//!
//! * The e2e's install without `debug` (modules, the plugin, the four profiles) minus its chat and
//!   ts_lobby fixtures (the bots run RCBot's defaults: chat off), snapshotted (in memory): every server starts
//!   from these same data files, so learned data doesn't grow from run to run.
//! * AMX Mod X loads the same plugins in every run: AMX Mod X's defaults, the testenv's test
//!   plugins commented out (amxx_test sets every player's health), and `perf_alive`
//!   (scripting/test/perf_alive.sma: bots and alive bots, deaths and server frames over rcon).
//!   `rcbot_amxx.amxx` is commented out for the other backends, so its cost isn't counted; the
//!   rcbot_* modules stay loaded (unused without the plugin). tsmod_rcbot is in `ts/dlls/` only
//!   for its own runs.
//! * One server per (map, backend, repetition), in a shuffled order (`PC_SEED`), pinned with
//!   `taskset -c PC_CPUS`; 32 slots, `mp_timelimit 9999`, the root's default `sys_ticrate`
//!   (recorded). Per bot count (shuffled within the server): `ts_bot_quota N` (the same path for
//!   every backend: the port asks the active backend for each bot), until `perf_alive` sees N
//!   bots, `PC_WARMUP` s, then `PC_SECS` s measured: the server process' utime+stime from
//!   `/proc/<pid>/stat` (hlds_linux, or Wine's hlds.exe process; the prefix's wineserver is
//!   measured on its own), the frames `perf_alive` counted, `stats` and `perf_alive` every
//!   `PC_SAMPLE` s, and for rcbot_amxx its `rcbot_profile` lines (ts_frame and its parts).
//! * Headroom: 31 bots with `sys_ticrate PC_HEADROOM_TICRATE` (default 1000, 0 skips), measured
//!   the same way after `PC_HEADROOM_WARMUP` s.
//!
//! Output: `target/perf-compare/<os>.csv` (one row per measured window) and
//! `target/perf-compare/perf-compare.md` (medians with min-max over the repetitions, the
//! marginal cost per bot, the headroom and rcbot_amxx's ts_frame breakdown) from every `<os>.csv`
//! there.
//!
//! Environment: e2e's and compat's, plus `PC_BACKENDS` (default `native,rcbot,rcbot_amxx`),
//! `PC_MAPS` (`ts_dojo,ts_lobby`), `PC_BOTS` (`0,4,16,31`), `PC_REPS` (3), `PC_WARMUP` (30),
//! `PC_SECS` (60), `PC_SAMPLE` (5), `PC_HEADROOM_TICRATE` (1000), `PC_HEADROOM_WARMUP` (15),
//! `PC_HEADROOM_SECS` (30), `PC_CPUS` (`2,3`; empty: not pinned), `PC_SEED` (1).

use super::compat::{self, DataBackup, Library, Snapshot};
use super::e2e::{self, Backup, Root};
use super::perf::{self, env_u64};
use super::{MODULES, dist, env_path, home, pawn, root};
use std::collections::BTreeMap;
use std::fmt::Write as _;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant};

const HELPER: &str = "perf_alive";
const BACKENDS: [&str; 3] = ["native", "rcbot", "rcbot_amxx"];

/// One measured window.
#[derive(Clone, Debug, Default, PartialEq)]
pub(super) struct Row {
    os: String,
    map: String,
    backend: String,
    rep: u32,
    /// The server's place in the run order (from 1).
    order: u32,
    /// `steady` (the default tick rate) or `headroom`.
    phase: String,
    ticrate: f64,
    bots: u32,
    wall_s: f64,
    cpu_s: f64,
    /// The prefix's wineserver (Wine only; NaN on Linux).
    wineserver_s: f64,
    frames: u64,
    stats_fps: f64,
    stats_cpu: f64,
    bots_min: u32,
    alive_mean: f64,
    alive_min: u32,
    deaths: u64,
    tsf_frames: u64,
    tsf_avg_us: f64,
    tsf_max_us: u64,
    tsf_refresh_us: f64,
    tsf_think_us: f64,
    tsf_act_us: f64,
}

const HEADER: &str = "os,map,backend,rep,order,phase,ticrate,bots,wall_s,cpu_s,wineserver_s,frames,stats_fps,stats_cpu,bots_min,alive_mean,alive_min,deaths,tsf_frames,tsf_avg_us,tsf_max_us,tsf_refresh_us,tsf_think_us,tsf_act_us,cpu_pct,fps,cpu_us_frame";

impl Row {
    /// The server's CPU time per wall second, in percent of one core.
    fn cpu_pct(&self) -> f64 {
        100.0 * self.cpu_s / self.wall_s
    }

    /// Frames per wall second, from perf_alive's frame counter.
    fn fps(&self) -> f64 {
        self.frames as f64 / self.wall_s
    }

    /// The server process' CPU time per frame in microseconds.
    fn cpu_us_frame(&self) -> f64 {
        if self.frames == 0 { f64::NAN } else { self.cpu_s * 1e6 / self.frames as f64 }
    }

    /// As [`Row::cpu_us_frame`] with the wineserver's time added (Wine).
    fn total_us_frame(&self) -> f64 {
        if self.frames == 0 { f64::NAN } else { (self.cpu_s + self.wineserver_s) * 1e6 / self.frames as f64 }
    }

    fn deaths_per_min(&self) -> f64 {
        self.deaths as f64 * 60.0 / self.wall_s
    }

    fn csv(&self) -> String {
        let f = |v: f64| if v.is_nan() { String::new() } else { format!("{v:.4}") };
        [
            self.os.clone(),
            self.map.clone(),
            self.backend.clone(),
            self.rep.to_string(),
            self.order.to_string(),
            self.phase.clone(),
            f(self.ticrate),
            self.bots.to_string(),
            f(self.wall_s),
            f(self.cpu_s),
            f(self.wineserver_s),
            self.frames.to_string(),
            f(self.stats_fps),
            f(self.stats_cpu),
            self.bots_min.to_string(),
            f(self.alive_mean),
            self.alive_min.to_string(),
            self.deaths.to_string(),
            self.tsf_frames.to_string(),
            f(self.tsf_avg_us),
            self.tsf_max_us.to_string(),
            f(self.tsf_refresh_us),
            f(self.tsf_think_us),
            f(self.tsf_act_us),
            // Derived, for reading the CSV on its own.
            f(self.cpu_pct()),
            f(self.fps()),
            f(self.cpu_us_frame()),
        ]
        .join(",")
    }

    fn parse(line: &str) -> Option<Row> {
        let v: Vec<&str> = line.split(',').collect();
        if v.len() < 24 {
            return None;
        }
        let f = |i: usize| if v[i].is_empty() { Some(f64::NAN) } else { v[i].parse::<f64>().ok() };
        let u = |i: usize| v[i].parse::<u64>().ok();
        Some(Row {
            os: v[0].into(),
            map: v[1].into(),
            backend: v[2].into(),
            rep: u(3)? as u32,
            order: u(4)? as u32,
            phase: v[5].into(),
            ticrate: f(6)?,
            bots: u(7)? as u32,
            wall_s: f(8)?,
            cpu_s: f(9)?,
            wineserver_s: f(10)?,
            frames: u(11)?,
            stats_fps: f(12)?,
            stats_cpu: f(13)?,
            bots_min: u(14)? as u32,
            alive_mean: f(15)?,
            alive_min: u(16)? as u32,
            deaths: u(17)?,
            tsf_frames: u(18)?,
            tsf_avg_us: f(19)?,
            tsf_max_us: u(20)?,
            tsf_refresh_us: f(21)?,
            tsf_think_us: f(22)?,
            tsf_act_us: f(23)?,
        })
    }
}

/// A `perf_alive` answer (running totals).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
struct Alive {
    bots: u32,
    alive: u32,
    deaths: u64,
    frames: u64,
    time: f64,
}

fn alive_line(answer: &str) -> Option<Alive> {
    let l = answer.lines().rev().find(|l| l.contains("perf_alive: "))?;
    let get = |k: &str| -> Option<f64> {
        let rest = &l[l.find(&format!(" {k}="))? + k.len() + 2..];
        rest.split_whitespace().next()?.parse().ok()
    };
    Some(Alive {
        bots: get("bots")? as u32,
        alive: get("alive")? as u32,
        deaths: get("deaths")? as u64,
        frames: get("frames")? as u64,
        time: get("time")?,
    })
}

/// `"sys_ticrate" is "100"` (ReHLDS adds `( def. "100" )`): the second quoted string.
fn cvar_value(answer: &str) -> Option<f64> {
    answer.lines().find_map(|l| l.split('"').nth(3)?.trim().parse::<f64>().ok())
}

/// utime + stime of a process (all its threads), in clock ticks: `/proc/<pid>/stat`'s 14th and
/// 15th fields (counted after the parenthesised name, which may contain spaces).
fn proc_ticks(pid: u32) -> Option<u64> {
    let s = std::fs::read_to_string(format!("/proc/{pid}/stat")).ok()?;
    stat_ticks(&s)
}

fn stat_ticks(stat: &str) -> Option<u64> {
    let rest = &stat[stat.rfind(')')? + 2..];
    let v: Vec<&str> = rest.split_whitespace().collect();
    Some(v.get(11)?.parse::<u64>().ok()? + v.get(12)?.parse::<u64>().ok()?)
}

fn clk_tck() -> f64 {
    Command::new("getconf")
        .arg("CLK_TCK")
        .output()
        .ok()
        .and_then(|o| String::from_utf8_lossy(&o.stdout).trim().parse().ok())
        .unwrap_or(100.0)
}

/// The root's server process (the testenv's check: exact name and working directory).
fn server_pid(r: &Root) -> Option<u32> {
    let out = Command::new("pgrep").args(["-x", r.exe]).output().ok()?;
    String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter_map(|p| p.trim().parse::<u32>().ok())
        .find(|p| std::fs::read_link(format!("/proc/{p}/cwd")).ok().is_some_and(|cwd| cwd == r.dir))
}

/// The wineserver of the testenv's prefix (by its environment).
fn wineserver_pid() -> Option<u32> {
    let prefix = env_path("AMXX_WINEPREFIX", home().join("games/wineprefix_amxx"));
    let want = format!("WINEPREFIX={}", prefix.display());
    let out = Command::new("pgrep").args(["-x", "wineserver"]).output().ok()?;
    String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter_map(|p| p.trim().parse::<u32>().ok())
        .find(|p| std::fs::read(format!("/proc/{p}/environ")).is_ok_and(|e| e.split(|&b| b == 0).any(|kv| kv == want.as_bytes())))
}

fn alive(testenv: &Path, r: &Root) -> Option<Alive> {
    (0..3).find_map(|_| alive_line(&e2e::rcon(testenv, r.port, HELPER)))
}

/// plugins.ini for a backend: the e2e's (other `*_e2e` plugins off, rcbot_amxx without `debug`),
/// the testenv's amxx_test off, perf_alive on, rcbot_amxx off unless it is the backend.
fn plugins_for(base: &str, backend: &str) -> String {
    // Our own lines (commented or not) go first, so applying this twice gives the same file.
    let ours = |l: &str| {
        let name = l.trim_start_matches(';').split_whitespace().next().unwrap_or("");
        name == format!("{HELPER}.amxx") || name == format!("{}.amxx", e2e::PLUGIN)
    };
    let base: String = base.lines().filter(|l| !ours(l)).map(|l| format!("{l}\n")).collect();
    let mut out: Vec<String> = e2e::plugins_ini(&base, false)
        .lines()
        .map(|l| {
            let name = l.split_whitespace().next().unwrap_or("");
            let off = name == "amxx_test.amxx" || (name == format!("{}.amxx", e2e::PLUGIN) && backend != e2e::PLUGIN);
            if off { format!(";{l}") } else { l.to_owned() }
        })
        .collect();
    out.push(format!("; tsmod-amxx-rcbot perf-compare\n{HELPER}.amxx"));
    out.join("\n") + "\n"
}

fn server_cfg(base: &str, backend: &str) -> String {
    format!("{base}\n// tsmod-amxx-rcbot perf-compare\nts_bot_backend {backend}\nmp_timelimit 9999\n")
}

/// The line that shows a plugin backend took over (none for native).
fn ready_line(backend: &str) -> Option<&'static str> {
    match backend {
        "rcbot" => Some("[tsmod_rcbot] bot backend active"),
        "rcbot_amxx" => Some("[rcbot_amxx] bot backend active"),
        _ => None,
    }
}

/// A seeded xorshift64 shuffle (the run order is reproducible from `PC_SEED`).
fn shuffle<T>(v: &mut [T], state: &mut u64) {
    for i in (1..v.len()).rev() {
        *state ^= *state << 13;
        *state ^= *state >> 7;
        *state ^= *state << 17;
        v.swap(i, (*state % (i as u64 + 1)) as usize);
    }
}

struct Settings {
    backends: Vec<String>,
    maps: Vec<String>,
    bots: Vec<u32>,
    reps: u32,
    warmup: u64,
    secs: u64,
    sample: u64,
    headroom_ticrate: u64,
    headroom_warmup: u64,
    headroom_secs: u64,
    cpus: Option<String>,
    seed: u64,
}

impl Settings {
    fn from_env() -> Settings {
        let list = |var: &str, default: &str| -> Vec<String> {
            std::env::var(var)
                .unwrap_or_else(|_| default.into())
                .split(',')
                .map(|s| s.trim().to_owned())
                .filter(|s| !s.is_empty())
                .collect()
        };
        let cpus = std::env::var("PC_CPUS").unwrap_or_else(|_| "2,3".into());
        Settings {
            backends: list("PC_BACKENDS", &BACKENDS.join(",")),
            maps: list("PC_MAPS", &format!("{},{}", e2e::MAP1, e2e::MAP2)),
            bots: list("PC_BOTS", "0,4,16,31").iter().filter_map(|n| n.parse().ok()).filter(|&n| n < perf::SLOTS).collect(),
            reps: env_u64("PC_REPS", 3) as u32,
            warmup: env_u64("PC_WARMUP", 30),
            secs: env_u64("PC_SECS", 60),
            sample: env_u64("PC_SAMPLE", 5).max(1),
            headroom_ticrate: env_u64("PC_HEADROOM_TICRATE", 1000),
            headroom_warmup: env_u64("PC_HEADROOM_WARMUP", 15),
            headroom_secs: env_u64("PC_HEADROOM_SECS", 30),
            cpus: (!cpus.trim().is_empty()).then_some(cpus),
            seed: env_u64("PC_SEED", 1).max(1),
        }
    }
}

/// Stops the root's server whatever happens (quit over rcon, then stop.sh).
struct Server<'a> {
    r: &'a Root,
    testenv: &'a Path,
    child: Option<std::process::Child>,
}

impl Drop for Server<'_> {
    fn drop(&mut self) {
        e2e::quit(self.r, self.testenv);
        if let Some(mut c) = self.child.take() {
            let _ = c.wait();
        }
    }
}

/// What one server run needs besides the root.
struct RunCtx<'a> {
    r: &'a Root,
    testenv: &'a Path,
    set: &'a Settings,
    tck: f64,
    profile_log: PathBuf,
}

/// Waits until perf_alive sees `n` bots (the quota adds or kicks one every half second).
fn wait_bots(c: &RunCtx, n: u32) -> bool {
    let t0 = Instant::now();
    while t0.elapsed() < Duration::from_secs(30 + u64::from(n) * 2) {
        if alive(c.testenv, c.r).is_some_and(|a| a.bots == n) {
            return true;
        }
        std::thread::sleep(Duration::from_secs(1));
    }
    false
}

fn profile_lines(text: &str) -> Vec<&str> {
    text.lines().filter(|l| l.contains("profile: ")).collect()
}

/// One measured window of `secs` on the running server.
fn window(c: &RunCtx, base: &Row, bots: u32, secs: u64) -> Result<Row, String> {
    let pid = server_pid(c.r).ok_or_else(|| format!("{}: the server process is gone", c.r.os))?;
    let wsrv = if c.r.os == "win" { wineserver_pid() } else { None };
    let skip = profile_lines(&e2e::read(&c.profile_log)).len();
    let ticks0 = proc_ticks(pid).ok_or("no /proc stat")?;
    let ws0 = wsrv.and_then(proc_ticks);
    let t0 = Instant::now();
    let a0 = alive(c.testenv, c.r).ok_or_else(|| format!("{}: perf_alive didn't answer", c.r.os))?;
    let (mut fps, mut cpu, mut seen) = (Vec::new(), Vec::new(), Vec::new());
    let mut next = Duration::from_secs(c.set.sample);
    while t0.elapsed() < Duration::from_secs(secs) {
        let wait = next.saturating_sub(t0.elapsed());
        std::thread::sleep(wait.min(Duration::from_secs(secs).saturating_sub(t0.elapsed())));
        if t0.elapsed() >= Duration::from_secs(secs) {
            break;
        }
        next += Duration::from_secs(c.set.sample);
        if let Some((c_, f)) = perf::stats_row(&e2e::rcon(c.testenv, c.r.port, "stats")) {
            cpu.push(c_);
            fps.push(f);
        }
        if let Some(a) = alive(c.testenv, c.r) {
            seen.push(a);
        }
    }
    let ticks1 = proc_ticks(pid).ok_or_else(|| format!("{}: the server died during the window", c.r.os))?;
    let ws1 = wsrv.and_then(proc_ticks);
    let wall = t0.elapsed().as_secs_f64();
    let a1 = alive(c.testenv, c.r).ok_or_else(|| format!("{}: perf_alive didn't answer", c.r.os))?;
    seen.push(a1);
    let mean = |v: &[f64]| if v.is_empty() { f64::NAN } else { v.iter().sum::<f64>() / v.len() as f64 };
    let mut row = Row {
        bots,
        wall_s: wall,
        cpu_s: (ticks1 - ticks0) as f64 / c.tck,
        wineserver_s: match (ws0, ws1) {
            (Some(a), Some(b)) => b.saturating_sub(a) as f64 / c.tck,
            _ => f64::NAN,
        },
        frames: a1.frames.saturating_sub(a0.frames),
        stats_fps: mean(&fps),
        stats_cpu: mean(&cpu),
        bots_min: seen.iter().map(|a| a.bots).min().unwrap_or(0),
        alive_mean: mean(&seen.iter().map(|a| f64::from(a.alive)).collect::<Vec<_>>()),
        alive_min: seen.iter().map(|a| a.alive).min().unwrap_or(0),
        deaths: a1.deaths.saturating_sub(a0.deaths),
        tsf_avg_us: f64::NAN,
        tsf_refresh_us: f64::NAN,
        tsf_think_us: f64::NAN,
        tsf_act_us: f64::NAN,
        ..base.clone()
    };
    if base.backend == e2e::PLUGIN {
        let text = e2e::read(&c.profile_log);
        let lines = profile_lines(&text);
        let s = perf::summarize(&lines[skip.min(lines.len())..].join("\n"), bots, 0);
        if s.frames > 0 || bots == 0 {
            row.tsf_frames = s.frames;
            row.tsf_avg_us = s.avg_us;
            row.tsf_max_us = s.max_us;
            row.tsf_refresh_us = s.refresh_us;
            row.tsf_think_us = s.think_us;
            row.tsf_act_us = s.act_us;
        }
    }
    Ok(row)
}

/// One server: `map` with `backend`, every bot count in `counts`' order, then the headroom.
fn run_server(c: &RunCtx, base: &Row, counts: &[u32], out: &mut dyn FnMut(Row)) -> Result<(), String> {
    let _ = std::fs::remove_file(&c.profile_log);
    let child = perf::start(c.r, &base.map, c.set.cpus.as_deref())?;
    let _server = Server { r: c.r, testenv: c.testenv, child: Some(child) };
    let console = perf::console_file(c.r);
    let up = e2e::wait_for(&console, Duration::from_secs(120), |t| ready_line(&base.backend).is_none_or(|l| t.contains(l))) && {
        let t0 = Instant::now();
        loop {
            if alive(c.testenv, c.r).is_some() {
                break true;
            }
            if t0.elapsed() > Duration::from_secs(120) {
                break false;
            }
            std::thread::sleep(Duration::from_secs(1));
        }
    };
    if !up {
        return Err(format!("{} {} {}: the server or backend wasn't up within 120 s", c.r.os, base.map, base.backend));
    }
    let backend = e2e::rcon(c.testenv, c.r.port, "ts_bot_backend");
    if !backend.contains(&format!("\"{}\"", base.backend)) {
        return Err(format!("{}: ts_bot_backend answered {:?}", c.r.os, backend.trim()));
    }
    let ticrate = cvar_value(&e2e::rcon(c.testenv, c.r.port, "sys_ticrate")).unwrap_or(f64::NAN);
    if base.backend == e2e::PLUGIN {
        e2e::rcon(c.testenv, c.r.port, "rcbot_profile 5");
    }
    let base = Row { ticrate, phase: "steady".into(), ..base.clone() };
    for &n in counts {
        e2e::rcon(c.testenv, c.r.port, &format!("ts_bot_quota {n}"));
        if !wait_bots(c, n) {
            return Err(format!("{} {} {}: {n} bots weren't in within the time", c.r.os, base.map, base.backend));
        }
        std::thread::sleep(Duration::from_secs(c.set.warmup));
        let row = window(c, &base, n, c.set.secs)?;
        print_row(&row);
        out(row);
    }
    if c.set.headroom_ticrate > 0 {
        let n = counts.iter().copied().max().unwrap_or(31);
        e2e::rcon(c.testenv, c.r.port, &format!("ts_bot_quota {n}"));
        if !wait_bots(c, n) {
            return Err(format!("{} {} {}: {n} bots weren't in within the time", c.r.os, base.map, base.backend));
        }
        e2e::rcon(c.testenv, c.r.port, &format!("sys_ticrate {}", c.set.headroom_ticrate));
        std::thread::sleep(Duration::from_secs(c.set.headroom_warmup));
        let got = cvar_value(&e2e::rcon(c.testenv, c.r.port, "sys_ticrate")).unwrap_or(f64::NAN);
        let hb = Row { phase: "headroom".into(), ticrate: got, ..base.clone() };
        let row = window(c, &hb, n, c.set.headroom_secs)?;
        print_row(&row);
        out(row);
    }
    Ok(())
}

fn print_row(r: &Row) {
    println!(
        "  {} {} {:<10} rep {} {:<8} bots {:>2}: cpu {:5.1}% fps {:6.1} ({:6.1} stats) {:7.0} us/frame, alive {:4.1} (min {}), {} deaths{}",
        r.os,
        r.map,
        r.backend,
        r.rep,
        r.phase,
        r.bots,
        r.cpu_pct(),
        r.fps(),
        r.stats_fps,
        r.cpu_us_frame(),
        r.alive_mean,
        r.alive_min,
        r.deaths,
        if r.tsf_avg_us.is_nan() { String::new() } else { format!(", ts_frame {:.0} us", r.tsf_avg_us) }
    );
}

fn compare_root(r: &Root, testenv: &Path, tsmod: &[PathBuf], set: &Settings) -> Result<Vec<String>, String> {
    if e2e::running(r) {
        return Err(format!("a server is already running in {} (amxx-testenv/stop.sh {})", r.dir.display(), r.os));
    }
    for m in &set.maps {
        if !r.dir.join(format!("ts/maps/{m}.bsp")).is_file() {
            return Err(format!("{}: no map {m}", r.dir.display()));
        }
    }
    let lib = if set.backends.iter().any(|b| b == "rcbot") { Some(compat::standalone(r)?) } else { None };
    let amxx = r.dir.join("ts/addons/amxmodx");
    let data = r.dir.join("ts/tsmod/rcbot");
    let out_dir = root().join("target/perf-compare");
    let _data_backup = DataBackup::take(&data)?;
    let cfg = r.dir.join("ts/server.cfg");
    let plugins = amxx.join("configs/plugins.ini");
    let mut backed = vec![
        amxx.join("configs/modules.ini"),
        plugins.clone(),
        cfg.clone(),
        r.dir.join("ts/amxx_server.cfg"),
        amxx.join(format!("plugins/{}.amxx", e2e::PLUGIN)),
        amxx.join(format!("plugins/{HELPER}.amxx")),
    ];
    backed.extend(tsmod.iter().map(|f| amxx.join("modules").join(f.file_name().unwrap())));
    backed.extend(MODULES.iter().map(|(_, m, _)| amxx.join("modules").join((r.target.amxx_file)(m))));
    if let Some(l) = &lib {
        backed.push(r.dir.join("ts/dlls").join(l.file_name().unwrap()));
    }
    let _backup = Backup::take(&backed);
    let base_cfg = e2e::read(&cfg);
    let base_plugins = e2e::read(&plugins);
    e2e::install(r, tsmod, false)?;
    // RCBot's defaults (chat off, no ts_lobby map config): the e2e's chat and kung fu fixtures out.
    let _ = std::fs::remove_file(data.join("bot_config.ini"));
    let _ = std::fs::remove_file(data.join(format!("map_configs/ts/{}.cfg", e2e::MAP2)));
    e2e::copy_fresh(&root().join(format!("target/pawn/test/{HELPER}.amxx")), &amxx.join(format!("plugins/{HELPER}.amxx")))?;
    let data0 = Snapshot::take(&data)?;
    for m in &set.maps {
        if !data.join(format!("waypoints/ts/{m}.rcw")).is_file() {
            println!("note: {m} has no RCBot waypoints in {}", data.display());
        }
    }

    let mut plan: Vec<(String, String, u32)> = Vec::new();
    for m in &set.maps {
        for b in &set.backends {
            for rep in 1..=set.reps {
                plan.push((m.clone(), b.clone(), rep));
            }
        }
    }
    let mut rng = set.seed ^ if r.os == "win" { 0x9e37_79b9_7f4a_7c15 } else { 0 };
    shuffle(&mut plan, &mut rng);
    let csv_path = out_dir.join(format!("{}.csv", r.os));
    let mut rows: Vec<Row> = Vec::new();
    // The whole file after every window (and its directory again: a `cargo clean` mid-run
    // mustn't lose what was measured).
    let save = |rows: &[Row]| {
        let text: String = std::iter::once(HEADER.to_owned()).chain(rows.iter().map(Row::csv)).map(|l| l + "\n").collect();
        if let Err(e) = std::fs::create_dir_all(&out_dir).and_then(|_| std::fs::write(&csv_path, text)) {
            eprintln!("warning: {}: {e}", csv_path.display());
        }
    };
    save(&rows);
    let ctx = RunCtx { r, testenv, set, tck: clk_tck(), profile_log: amxx.join(format!("logs/{}", perf::PROFILE_LOG)) };
    let mut fails = Vec::new();
    let total = plan.len();
    for (i, (map, backend, rep)) in plan.into_iter().enumerate() {
        let mut counts = set.bots.clone();
        shuffle(&mut counts, &mut rng);
        println!("--- {} [{}/{total}] {map} {backend} rep {rep}: bots {counts:?}", r.os, i + 1);
        // The data every server starts from, and the configs for this backend.
        data0.restore()?;
        e2e::replace(&plugins, plugins_for(&base_plugins, &backend).as_bytes())?;
        e2e::replace(&cfg, server_cfg(&base_cfg, &backend).as_bytes())?;
        let _lib = match (&lib, backend.as_str()) {
            (Some(l), "rcbot") => {
                compat::copy_fresh_lib(l, &r.dir.join("ts/dlls"))?;
                Some(Library(r.dir.join("ts/dlls").join(l.file_name().unwrap())))
            }
            _ => None,
        };
        let base = Row { os: r.os.into(), map, backend, rep, order: i as u32 + 1, ..Row::default() };
        let mut append = |row: Row| {
            rows.push(row);
            save(&rows);
        };
        if let Err(e) = run_server(&ctx, &base, &counts, &mut append) {
            println!("  FAIL {e}");
            fails.push(e);
        }
    }
    Ok(fails)
}

// ---- the report ------------------------------------------------------------------------------

fn median(v: &[f64]) -> f64 {
    let mut v: Vec<f64> = v.iter().copied().filter(|x| !x.is_nan()).collect();
    if v.is_empty() {
        return f64::NAN;
    }
    v.sort_by(|a, b| a.total_cmp(b));
    let n = v.len();
    if n % 2 == 1 { v[n / 2] } else { (v[n / 2 - 1] + v[n / 2]) / 2.0 }
}

/// `median [min-max]` with `prec` decimals; `-` when there is nothing.
fn spread(v: &[f64], prec: usize) -> String {
    let v: Vec<f64> = v.iter().copied().filter(|x| !x.is_nan()).collect();
    if v.is_empty() {
        return "-".into();
    }
    let (lo, hi) = v.iter().fold((f64::INFINITY, f64::NEG_INFINITY), |(lo, hi), &x| (lo.min(x), hi.max(x)));
    if v.len() == 1 { format!("{:.*}", prec, v[0]) } else { format!("{:.*} [{:.*}–{:.*}]", prec, median(&v), prec, lo, prec, hi) }
}

/// Least-squares slope of y over x.
fn slope(points: &[(f64, f64)]) -> f64 {
    let p: Vec<&(f64, f64)> = points.iter().filter(|(x, y)| !x.is_nan() && !y.is_nan()).collect();
    let n = p.len() as f64;
    if p.len() < 2 {
        return f64::NAN;
    }
    let (mx, my) = (p.iter().map(|q| q.0).sum::<f64>() / n, p.iter().map(|q| q.1).sum::<f64>() / n);
    let (num, den) = p.iter().fold((0.0, 0.0), |(a, b), q| (a + (q.0 - mx) * (q.1 - my), b + (q.0 - mx) * (q.0 - mx)));
    if den == 0.0 { f64::NAN } else { num / den }
}

fn fmt(v: f64, prec: usize) -> String {
    if v.is_nan() { "-".into() } else { format!("{v:.prec$}") }
}

type Key = (String, String, String, u32);

/// The markdown report for `rows` (every OS, map, backend).
pub(super) fn report(rows: &[Row]) -> String {
    let mut md = String::from("# Bot backend performance (cargo xtask perf-compare)\n\n");
    md += "Values are the median over the repetitions with [min–max]. CPU % is the server process' utime+stime per wall second \
           (100 = one core); µs/frame is that CPU time divided by the frames the server ran (perf_alive's server_frame count); \
           fps is frames per wall second; `stats` fps is the engine's own figure. Deaths/min shows the bots fight.\n\n";
    let oses: Vec<String> = ["linux", "win"].iter().filter(|o| rows.iter().any(|r| r.os == **o)).map(|o| o.to_string()).collect();
    let order = |b: &str| BACKENDS.iter().position(|x| *x == b).unwrap_or(9);
    for os in &oses {
        let mut maps: Vec<String> = rows.iter().filter(|r| &r.os == os).map(|r| r.map.clone()).collect();
        maps.dedup();
        maps.sort();
        maps.dedup();
        let win = os == "win";
        let ticrates: Vec<String> = {
            let mut t: Vec<String> = rows.iter().filter(|r| &r.os == os && r.phase == "steady").map(|r| fmt(r.ticrate, 0)).collect();
            t.sort();
            t.dedup();
            t
        };
        let _ = writeln!(md, "## {os} (sys_ticrate {})\n", ticrates.join("/"));
        for map in &maps {
            let mut g: BTreeMap<Key, Vec<&Row>> = BTreeMap::new();
            for r in rows.iter().filter(|r| &r.os == os && &r.map == map && r.phase == "steady") {
                g.entry((format!("{}", order(&r.backend)), r.backend.clone(), String::new(), r.bots)).or_default().push(r);
            }
            let _ = writeln!(md, "### {os} / {map}\n");
            md += "| backend | bots | CPU % | µs/frame | fps | stats fps | alive (mean, min) | deaths/min | ts_frame µs |";
            md += if win {
                " +wineserver µs/frame |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n"
            } else {
                "\n|---|---:|---:|---:|---:|---:|---:|---:|---:|\n"
            };
            for ((_, b, _, bots), rs) in &g {
                let col = |f: fn(&Row) -> f64| rs.iter().map(|r| f(r)).collect::<Vec<f64>>();
                let _ = write!(
                    md,
                    "| {b} | {bots} | {} | {} | {} | {} | {} ({}) | {} | {} |",
                    spread(&col(Row::cpu_pct), 1),
                    spread(&col(Row::cpu_us_frame), 0),
                    spread(&col(Row::fps), 1),
                    spread(&col(|r| r.stats_fps), 1),
                    fmt(median(&col(|r| r.alive_mean)), 1),
                    rs.iter().map(|r| r.alive_min).min().unwrap_or(0),
                    spread(&col(Row::deaths_per_min), 0),
                    spread(&col(|r| r.tsf_avg_us), 0),
                );
                if win {
                    let _ = write!(md, " {} |", spread(&col(Row::total_us_frame), 0));
                }
                md += "\n";
            }
            md += "\n";
        }

        // The marginal cost: µs of server CPU per bot per frame.
        let _ = writeln!(md, "### {os}: marginal cost per bot\n");
        md += "Slope of the median µs/frame over the bot counts (least squares, 0 bots included), and (C(max) − C(0)) / max. \
               The ts_frame column is the same slope for rcbot_amxx's own ts_frame.\n\n";
        md += "| map | backend | C(0) µs/frame | µs per bot per frame (slope) | (C(max)−C(0))/max | ts_frame µs per bot |";
        md += if win { " with wineserver (slope) |\n|---|---|---:|---:|---:|---:|---:|\n" } else { "\n|---|---|---:|---:|---:|---:|\n" };
        for map in &maps {
            let mut backends: Vec<String> = rows.iter().filter(|r| &r.os == os && &r.map == map).map(|r| r.backend.clone()).collect();
            backends.sort_by_key(|b| order(b));
            backends.dedup();
            for b in &backends {
                let mut per: BTreeMap<u32, Vec<&Row>> = BTreeMap::new();
                for r in rows.iter().filter(|r| &r.os == os && &r.map == map && &r.backend == b && r.phase == "steady") {
                    per.entry(r.bots).or_default().push(r);
                }
                let pts = |f: fn(&Row) -> f64| -> Vec<(f64, f64)> {
                    per.iter().map(|(n, rs)| (f64::from(*n), median(&rs.iter().map(|r| f(r)).collect::<Vec<_>>()))).collect()
                };
                let c = pts(Row::cpu_us_frame);
                let c0 = c.iter().find(|p| p.0 == 0.0).map_or(f64::NAN, |p| p.1);
                let cmax = c.last().copied().unwrap_or((0.0, f64::NAN));
                let delta = if cmax.0 > 0.0 { (cmax.1 - c0) / cmax.0 } else { f64::NAN };
                let tsf: Vec<(f64, f64)> = pts(|r| r.tsf_avg_us).into_iter().filter(|p| p.0 > 0.0).collect();
                let _ =
                    write!(md, "| {map} | {b} | {} | {} | {} | {} |", fmt(c0, 0), fmt(slope(&c), 1), fmt(delta, 1), fmt(slope(&tsf), 1));
                if win {
                    let _ = write!(md, " {} |", fmt(slope(&pts(Row::total_us_frame)), 1));
                }
                md += "\n";
            }
        }
        md += "\n";

        // Headroom.
        let head: Vec<&Row> = rows.iter().filter(|r| &r.os == os && r.phase == "headroom").collect();
        if !head.is_empty() {
            let _ =
                writeln!(md, "### {os}: headroom (sys_ticrate {})\n", fmt(median(&head.iter().map(|r| r.ticrate).collect::<Vec<_>>()), 0));
            md += "| map | backend | bots | fps | stats fps | CPU % | µs/frame |\n|---|---|---:|---:|---:|---:|---:|\n";
            let mut g: BTreeMap<Key, Vec<&Row>> = BTreeMap::new();
            for r in head {
                g.entry((r.map.clone(), format!("{}", order(&r.backend)), r.backend.clone(), r.bots)).or_default().push(r);
            }
            for ((map, _, b, bots), rs) in &g {
                let col = |f: fn(&Row) -> f64| rs.iter().map(|r| f(r)).collect::<Vec<f64>>();
                let _ = writeln!(
                    md,
                    "| {map} | {b} | {bots} | {} | {} | {} | {} |",
                    spread(&col(Row::fps), 0),
                    spread(&col(|r| r.stats_fps), 0),
                    spread(&col(Row::cpu_pct), 1),
                    spread(&col(Row::cpu_us_frame), 0)
                );
            }
            md += "\n";
        }

        // rcbot_amxx's ts_frame against the whole server frame.
        let amxx: Vec<&Row> =
            rows.iter().filter(|r| &r.os == os && r.backend == e2e::PLUGIN && r.phase == "steady" && r.bots > 0).collect();
        if !amxx.is_empty() {
            let _ = writeln!(md, "### {os}: rcbot_amxx ts_frame breakdown\n");
            md += "ts_frame (rcbot_profile, nav_clock_us around the Pawn manager frame) and its parts, against the server's whole CPU per \
                   frame: the rest is the engine, the game, AMX Mod X's forwards and the bots' movement.\n\n";
            md += "| map | bots | server µs/frame | ts_frame µs | refresh | think | act + rest | worst ts_frame | ts_frame share |\n";
            md += "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
            let mut g: BTreeMap<(String, u32), Vec<&Row>> = BTreeMap::new();
            for r in amxx {
                g.entry((r.map.clone(), r.bots)).or_default().push(r);
            }
            for ((map, bots), rs) in &g {
                let m = |f: fn(&Row) -> f64| median(&rs.iter().map(|r| f(r)).collect::<Vec<f64>>());
                let _ = writeln!(
                    md,
                    "| {map} | {bots} | {} | {} | {} | {} | {} | {} | {}% |",
                    fmt(m(Row::cpu_us_frame), 0),
                    fmt(m(|r| r.tsf_avg_us), 0),
                    fmt(m(|r| r.tsf_refresh_us), 0),
                    fmt(m(|r| r.tsf_think_us), 0),
                    fmt(m(|r| r.tsf_act_us), 0),
                    fmt(m(|r| r.tsf_max_us as f64), 0),
                    fmt(100.0 * m(|r| r.tsf_avg_us) / m(Row::cpu_us_frame), 0)
                );
            }
            md += "\n";
        }
    }
    md
}

fn write_report(out_dir: &Path) -> Result<String, String> {
    let mut rows = Vec::new();
    for os in ["linux", "win"] {
        for l in e2e::read(&out_dir.join(format!("{os}.csv"))).lines().skip(1) {
            rows.extend(Row::parse(l));
        }
    }
    let md = report(&rows);
    std::fs::create_dir_all(out_dir).and_then(|_| std::fs::write(out_dir.join("perf-compare.md"), &md)).map_err(|e| e.to_string())?;
    Ok(md)
}

/// `cargo xtask perf-compare [linux] [win]`.
pub fn perf_compare(names: &[String]) -> Result<(), String> {
    let testenv = env_path("AMXX_TESTENV", home().join("dev/hl/ref/amxx-testenv"));
    if !testenv.join("rcon.py").exists() {
        return Err(format!("amxx-testenv not found ({}): see its README", testenv.display()));
    }
    let roots = e2e::roots(names)?;
    if let Some(r) = roots.iter().find(|r| e2e::running(r)) {
        return Err(format!("a server is already running in {} (amxx-testenv/stop.sh {})", r.dir.display(), r.os));
    }
    let set = Settings::from_env();
    if let Some(b) = set.backends.iter().find(|b| !BACKENDS.contains(&b.as_str())) {
        return Err(format!("unknown backend {b} ({})", BACKENDS.join("|")));
    }
    dist(false)?;
    pawn(true)?;
    let out_dir = root().join("target/perf-compare");
    let mut fails = Vec::new();
    for r in &roots {
        let tsmod = e2e::tsmod_amxx_modules(r.target)?;
        fails.extend(compare_root(r, &testenv, &tsmod, &set)?);
        write_report(&out_dir)?;
    }
    println!("{}", write_report(&out_dir)?);
    println!("written: {} and {}/<os>.csv", out_dir.join("perf-compare.md").display(), out_dir.display());
    if fails.is_empty() { Ok(()) } else { Err(format!("perf-compare failures:\n  {}", fails.join("\n  "))) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn perf_alive_lines_parse() {
        let a = alive_line("junk\nperf_alive: bots=16 alive=12 deaths=40 frames=12345 time=301.250\n").unwrap();
        assert_eq!((a.bots, a.alive, a.deaths, a.frames), (16, 12, 40, 12345));
        assert!((a.time - 301.25).abs() < 1e-9);
        assert_eq!(alive_line("Unknown command"), None);
    }

    #[test]
    fn proc_stat_fields_after_the_name() {
        // pid (comm with spaces) state ppid ... utime=14th stime=15th field.
        let stat = "1234 (hlds linux) S 1 2 3 4 5 6 7 8 9 10 250 50 0 0 20 0 1 0 100 0 0";
        assert_eq!(stat_ticks(stat), Some(300));
    }

    #[test]
    fn cvar_answers_parse() {
        assert_eq!(cvar_value("\"sys_ticrate\" is \"100\"\n"), Some(100.0));
        assert_eq!(cvar_value("\"sys_ticrate\" is \"1000.000000\" ( def. \"100\" )\n"), Some(1000.0));
    }

    #[test]
    fn plugins_ini_per_backend() {
        let base = "admin.amxx\namxx_test.amxx debug\ntsx_e2e.amxx debug\n";
        let native = plugins_for(base, "native");
        assert!(native.contains(";amxx_test.amxx debug") && native.contains(";tsx_e2e.amxx"));
        assert!(native.lines().any(|l| l == ";rcbot_amxx.amxx") && native.lines().any(|l| l == "perf_alive.amxx"));
        let amxx = plugins_for(&native, "rcbot_amxx");
        assert!(amxx.lines().any(|l| l == "rcbot_amxx.amxx"));
        assert_eq!(amxx.matches("perf_alive.amxx").count(), 1);
        assert_eq!(amxx.lines().filter(|l| l.trim_start_matches(';') == "rcbot_amxx.amxx").count(), 1);
    }

    #[test]
    fn rows_round_trip_through_csv() {
        let r = Row {
            os: "linux".into(),
            map: "ts_dojo".into(),
            backend: "rcbot".into(),
            rep: 2,
            order: 5,
            phase: "steady".into(),
            ticrate: 100.0,
            bots: 16,
            wall_s: 60.0,
            cpu_s: 6.0,
            wineserver_s: f64::NAN,
            frames: 6000,
            tsf_avg_us: f64::NAN,
            ..Row::default()
        };
        let back = Row::parse(&r.csv()).unwrap();
        assert_eq!((back.bots, back.frames, back.backend.as_str()), (16, 6000, "rcbot"));
        assert!(back.wineserver_s.is_nan() && (back.cpu_us_frame() - 1000.0).abs() < 1e-6);
    }

    #[test]
    fn statistics() {
        assert_eq!(median(&[3.0, 1.0, 2.0]), 2.0);
        assert_eq!(spread(&[3.0, 1.0, 2.0], 1), "2.0 [1.0–3.0]");
        assert!((slope(&[(0.0, 100.0), (4.0, 140.0), (16.0, 260.0)]) - 10.0).abs() < 1e-9);
        let mut v = vec![1, 2, 3, 4, 5];
        shuffle(&mut v, &mut 1);
        v.sort();
        assert_eq!(v, [1, 2, 3, 4, 5]);
    }

    #[test]
    fn the_report_has_every_section() {
        let mk = |backend: &str, bots: u32, cpu: f64, phase: &str| Row {
            os: "linux".into(),
            map: "ts_dojo".into(),
            backend: backend.into(),
            phase: phase.into(),
            ticrate: 100.0,
            bots,
            wall_s: 60.0,
            cpu_s: cpu,
            wineserver_s: f64::NAN,
            frames: 6000,
            tsf_avg_us: if backend == "rcbot_amxx" { 10.0 * f64::from(bots) } else { f64::NAN },
            tsf_refresh_us: f64::NAN,
            tsf_think_us: f64::NAN,
            tsf_act_us: f64::NAN,
            ..Row::default()
        };
        let rows = vec![
            mk("native", 0, 1.0, "steady"),
            mk("native", 4, 2.0, "steady"),
            mk("rcbot_amxx", 4, 3.0, "steady"),
            mk("native", 4, 9.0, "headroom"),
        ];
        let md = report(&rows);
        for s in ["### linux / ts_dojo", "marginal cost per bot", "headroom", "ts_frame breakdown", "| native | 4 |"] {
            assert!(md.contains(s), "{s} missing:\n{md}");
        }
    }
}
