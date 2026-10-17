//! `cargo xtask perf [linux] [win]`: what rcbot_amxx's `ts_frame` costs with 4, 16 and 31 bots,
//! with the plugin loaded as servers run it (plugins.ini without `debug`).
//!
//! Per root and bot count: the e2e's install (fixtures included, the plugin without `debug`, no
//! `rcbot_e2e`), a
//! server on ts_dojo with 32 slots (the testenv's run.sh has 8, so this starts hlds itself, the
//! same way), `rcbot_profile 5` over rcon (report.inc's profiler: `nav_clock_us` around
//! `ts_frame` and its parts, logged to `logs/rcbot_amxx_profile.log` every 5 s), `rcbot addbot`
//! up to the count, `PERF_WARMUP` seconds (default 20) to settle, then `PERF_SECS` (default 60)
//! of samples, and `stats` (the server's fps) every 10 s. The table: frames, the mean and worst
//! `ts_frame` in microseconds, the parts (world refresh, think, act + the rest) and the fps.
//! Configs are restored and the server stopped as in the e2e.
//!
//! Environment: e2e's, plus `PERF_BOTS` (comma-separated counts, default `4,16,31`),
//! `PERF_WARMUP`, `PERF_SECS`, `PERF_PLUGIN` (an .amxx to time instead of target/pawn's).

use super::e2e::{self, Backup, Root};
use super::{dist, env_path, home, pawn};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

pub(super) const PROFILE_LOG: &str = "rcbot_amxx_profile.log";
pub(super) const SLOTS: u32 = 32;

/// One count's result.
#[derive(Default, Debug)]
pub(super) struct Sample {
    pub(super) bots: u32,
    pub(super) frames: u64,
    pub(super) avg_us: f64,
    pub(super) max_us: u64,
    pub(super) refresh_us: f64,
    pub(super) think_us: f64,
    pub(super) act_us: f64,
    pub(super) fps: Vec<f64>,
}

/// The value of `key=` in a profile line.
fn field(line: &str, key: &str) -> Option<f64> {
    let rest = &line[line.find(&format!(" {key}="))? + key.len() + 2..];
    rest.split_whitespace().next()?.parse().ok()
}

/// The profile lines for `bots` bots after `skip` lines: frame-weighted means, the worst frame.
pub(super) fn summarize(text: &str, bots: u32, skip: usize) -> Sample {
    let mut s = Sample { bots, ..Sample::default() };
    let (mut sum, mut refresh, mut think, mut act) = (0.0, 0.0, 0.0, 0.0);
    for l in text.lines().filter(|l| l.contains("profile: ")).skip(skip) {
        if field(l, "bots") != Some(f64::from(bots)) {
            continue;
        }
        let Some(frames) = field(l, "frames") else { continue };
        s.frames += frames as u64;
        sum += frames * field(l, "avg_us").unwrap_or(0.0);
        refresh += frames * field(l, "refresh_us").unwrap_or(0.0);
        think += frames * field(l, "think_us").unwrap_or(0.0);
        act += frames * field(l, "act_us").unwrap_or(0.0);
        s.max_us = s.max_us.max(field(l, "max_us").unwrap_or(0.0) as u64);
    }
    if s.frames > 0 {
        let n = s.frames as f64;
        (s.avg_us, s.refresh_us, s.think_us, s.act_us) = (sum / n, refresh / n, think / n, act / n);
    }
    s
}

/// `stats`' FPS column (the header's sixth: CPU In Out Uptime Users FPS Players).
fn stats_fps(answer: &str) -> Option<f64> {
    stats_row(answer).map(|(_, fps)| fps)
}

/// `stats`' CPU and FPS columns (the header's first and sixth: CPU In Out Uptime Users FPS Players).
pub(super) fn stats_row(answer: &str) -> Option<(f64, f64)> {
    answer.lines().rev().find_map(|l| {
        let v: Vec<&str> = l.split_whitespace().collect();
        if v.len() < 7 {
            return None;
        }
        Some((v[0].parse().ok()?, v[5].parse().ok()?))
    })
}

/// The testenv's run.sh with 32 slots: amxx_server.cfg, then hlds in the background, on `map`;
/// under `taskset -c <cpus>` when `cpus` is given (perf-compare pins the server).
pub(super) fn start(r: &Root, map: &str, cpus: Option<&str>) -> Result<std::process::Child, String> {
    let cfg = "// Written by tsmod-amxx-rcbot cargo xtask perf (as amxx-testenv/run.sh)\nexec server.cfg\nrcon_password amxxtest\nlog on\nmp_logecho 0\nts_bot_quota 0\n";
    e2e::replace(&r.dir.join("ts/amxx_server.cfg"), cfg.as_bytes())?;
    let _ = std::fs::remove_file(r.dir.join("qconsole.log"));
    let args = [
        "-game",
        "ts",
        "-port",
        &r.port.to_string(),
        "+maxplayers",
        &SLOTS.to_string(),
        "+sv_lan",
        "1",
        "+ts_bot_quota",
        "0",
        "+servercfgfile",
        "amxx_server.cfg",
        "+map",
        map,
    ]
    .map(String::from);
    let wrapped = |program: &str| match cpus {
        Some(cpus) => {
            let mut c = Command::new("taskset");
            c.args(["-c", cpus, program]);
            c
        }
        None => Command::new(program),
    };
    let mut c = if r.os == "win" {
        let mut c = wrapped("wine");
        c.env("WINEPREFIX", env_path("AMXX_WINEPREFIX", home().join("games/wineprefix_amxx"))).env("WINEDEBUG", "-all").args([
            "hlds.exe",
            "-console",
            "-condebug",
        ]);
        c
    } else {
        let mut c = wrapped(&r.dir.join("hlds_linux").to_string_lossy());
        c.env("LD_LIBRARY_PATH", &r.dir).arg("-condebug");
        c
    };
    let out = std::fs::File::create(r.dir.join("server-stdout.log")).map_err(|e| e.to_string())?;
    c.current_dir(&r.dir).args(args).stdin(Stdio::null()).stdout(out.try_clone().map_err(|e| e.to_string())?).stderr(out);
    // `quit` (or stop.sh, by name and working directory) ends it; the caller reaps it.
    c.spawn().map_err(|e| format!("{}: {e}", r.os))
}

pub(super) fn console_file(r: &Root) -> PathBuf {
    r.dir.join(if r.os == "win" { "qconsole.log" } else { "server-stdout.log" })
}

fn perf_count(r: &Root, testenv: &Path, bots: u32, warmup: u64, secs: u64) -> Result<Sample, String> {
    let amxx = r.dir.join("ts/addons/amxmodx");
    let log = amxx.join(format!("logs/{PROFILE_LOG}"));
    let _ = std::fs::remove_file(&log);
    let mut child = start(r, e2e::MAP1, None)?;
    let up = e2e::wait_for(&console_file(r), Duration::from_secs(120), |t| t.contains("[rcbot_amxx] bot backend active"));
    let mut fps = Vec::new();
    if up {
        e2e::rcon(testenv, r.port, "rcbot_profile 5");
        for _ in 0..bots {
            e2e::rcon(testenv, r.port, "rcbot addbot");
            std::thread::sleep(Duration::from_millis(700));
        }
        std::thread::sleep(Duration::from_secs(warmup));
        let skip = e2e::read(&log).lines().filter(|l| l.contains("profile: ")).count();
        let t0 = Instant::now();
        while t0.elapsed() < Duration::from_secs(secs) {
            std::thread::sleep(Duration::from_secs(10));
            if let Some(f) = stats_fps(&e2e::rcon(testenv, r.port, "stats")) {
                fps.push(f);
            }
        }
        e2e::quit(r, testenv);
        let _ = child.wait();
        let mut s = summarize(&e2e::read(&log), bots, skip);
        s.fps = fps;
        return Ok(s);
    }
    e2e::quit(r, testenv);
    let _ = child.wait();
    Err(format!("{}: the backend wasn't active within 120 s", r.os))
}

fn perf_root(r: &Root, testenv: &Path, tsmod: &[PathBuf], counts: &[u32]) -> Result<Vec<Sample>, String> {
    if e2e::running(r) {
        return Err(format!("a server is already running in {} (amxx-testenv/stop.sh {})", r.dir.display(), r.os));
    }
    let amxx = r.dir.join("ts/addons/amxmodx");
    let mut backed = vec![amxx.join("configs/modules.ini"), amxx.join("configs/plugins.ini"), r.dir.join("ts/server.cfg")];
    backed.extend(e2e::fixtures().into_iter().map(|(rel, _)| r.dir.join("ts/tsmod/rcbot").join(rel)));
    let _backup = Backup::take(&backed);
    let cfg = r.dir.join("ts/server.cfg");
    let base = e2e::read(&cfg);
    e2e::install(r, tsmod, false)?;
    // Another build of the plugin to time (an instrumented one, say) in place of target/pawn's.
    if let Some(p) = std::env::var_os("PERF_PLUGIN") {
        let to = r.dir.join(format!("ts/addons/amxmodx/plugins/{}.amxx", e2e::PLUGIN));
        std::fs::copy(&p, &to).map_err(|e| format!("PERF_PLUGIN: {e}"))?;
    }
    // Without rcbot_e2e: report.inc's checks would run every frame.
    e2e::replace(&cfg, format!("{base}\n// tsmod-amxx-rcbot perf\nts_bot_backend rcbot_amxx\n").as_bytes())?;
    let warmup = env_u64("PERF_WARMUP", 20);
    let secs = env_u64("PERF_SECS", 60);
    counts.iter().map(|&n| perf_count(r, testenv, n, warmup, secs)).collect()
}

pub(super) fn env_u64(var: &str, default: u64) -> u64 {
    std::env::var(var).ok().and_then(|v| v.parse().ok()).unwrap_or(default)
}

/// `cargo xtask perf [linux] [win]`.
pub fn perf(names: &[String]) -> Result<(), String> {
    let testenv = env_path("AMXX_TESTENV", home().join("dev/hl/ref/amxx-testenv"));
    if !testenv.join("rcon.py").exists() {
        return Err(format!("amxx-testenv not found ({}): see its README", testenv.display()));
    }
    let roots = e2e::roots(names)?;
    let counts: Vec<u32> = std::env::var("PERF_BOTS")
        .unwrap_or_else(|_| "4,16,31".into())
        .split(',')
        .filter_map(|n| n.trim().parse().ok())
        .filter(|&n| (1..SLOTS).contains(&n))
        .collect();
    dist(false)?;
    pawn(true)?;
    let mut table = Vec::new();
    for r in &roots {
        let tsmod = e2e::tsmod_amxx_modules(r.target)?;
        for s in perf_root(r, &testenv, &tsmod, &counts)? {
            table.push((r.os, s));
        }
    }
    println!("root   bots  frames   ts_frame avg us  worst us   refresh  think    act+rest  server fps");
    for (os, s) in &table {
        let fps = if s.fps.is_empty() { "-".to_owned() } else { format!("{:.1}", s.fps.iter().sum::<f64>() / s.fps.len() as f64) };
        println!(
            "{os:<6} {:>4}  {:>6}   {:>14.0}  {:>8}   {:>7.0}  {:>7.0}  {:>8.0}  {fps:>10}",
            s.bots, s.frames, s.avg_us, s.max_us, s.refresh_us, s.think_us, s.act_us
        );
    }
    if table.iter().any(|(_, s)| s.frames == 0) { Err("a run logged no profile lines".into()) } else { Ok(()) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn profile_lines_are_frame_weighted() {
        let text = "L x: profile: map=ts_dojo bots=4 frames=100 avg_us=200 max_us=900 refresh_us=20 think_us=150 act_us=30\n\
                    L x: profile: map=ts_dojo bots=4 frames=300 avg_us=400 max_us=700 refresh_us=40 think_us=300 act_us=60\n\
                    L x: profile: map=ts_dojo bots=3 frames=300 avg_us=1 max_us=5000 refresh_us=1 think_us=1 act_us=1\n";
        let s = summarize(text, 4, 0);
        assert_eq!((s.frames, s.max_us), (400, 900));
        assert!((s.avg_us - 350.0).abs() < 1e-9);
        assert_eq!(summarize(text, 4, 1).frames, 300);
    }

    #[test]
    fn stats_fps_reads_the_sixth_column() {
        let answer = "CPU   In    Out   Uptime  Users   FPS    Players\n 9.60  0.00  0.00       2     7   97.96       0\n";
        assert_eq!(stats_fps(answer), Some(97.96));
    }
}
