//! `cargo xtask e2e [linux] [win]`: the Pawn bot on the amxx-testenv roots (`../ref/amxx-testenv`,
//! `~/games/amxx_rehlds_{linux,win}`), modelled on tsmod-amxx-rs's e2e.
//!
//! 1. `dist` (our three modules, release) and `pawn` (rcbot_amxx.amxx); tsx and tsfun from
//!    tsmod-amxx-rs's `dist/` (its `cargo xtask dist` runs first when they are missing).
//! 2. Per root: refuse if its server runs; back up `configs/modules.ini`, `configs/plugins.ini` and
//!    `ts/server.cfg`; install the modules and the plugin; enable tsx, tsfun, rcbot_megahal,
//!    rcbot_learn, rcbot_nav and `rcbot_amxx.amxx debug`, comment out other `*_e2e` plugins (one
//!    sets `ts_bot_backend` to itself); append `ts_bot_backend rcbot_amxx` and `localinfo rcbot_e2e
//!    1` to server.cfg (read at map start, before plugin_init: the backend takes over at once).
//! 3. `run.sh <os> --map ts_dojo --bots 0` (ts_bot_quota 0), wait for the plugin's `CHECK backend
//!    PASS`, then over rcon `addbot` twice (the port's quota path, ts_bot_add) and `rcbot addbot`
//!    twice (ts_bot_create), and wait for `DONE` in `logs/rcbot_amxx_e2e.log`.
//! 4. `quit` over rcon (ServerDeactivate saves), `stop.sh` if it hangs; restore the three files.
//! 5. Failures: every `CHECK ... FAIL` line, no `DONE`, `amxx plugins` not listing the plugin as
//!    running, and AMX Mod X runtime errors (`error_*.log` written during the run, lines naming
//!    rcbot_amxx or our modules).
//!
//! Environment: `AMXX_TESTENV` (default `~/dev/hl/ref/amxx-testenv`), `AMXX_LINUX_ROOT` /
//! `AMXX_WIN_ROOT` (default `~/games/amxx_rehlds_{linux,win}`), `TSMOD_AMXX_DIR` (default
//! `~/dev/hl/tsmod-amxx-rs`), `E2E_TIMEOUT` (seconds to wait for DONE, default 300).
//!
//! The roots are shared: this never starts a server outside its run and always stops it.

use super::{MODULES, TARGETS, Target, dist, env_path, home, pawn, root, run};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant, SystemTime};

/// The plugin under test and its log (report.inc).
const PLUGIN: &str = "rcbot_amxx";
const LOG: &str = "rcbot_amxx_e2e.log";
/// What the plugin registers as (register_plugin's title) for `amxx plugins`.
const PLUGIN_TITLE: &str = "RCBot AMXX";
/// The lines appended to server.cfg (after a newline: server.cfg may end without one).
const SERVER_CFG_LINES: &str = "\n// tsmod-amxx-rcbot e2e\nts_bot_backend rcbot_amxx\nlocalinfo rcbot_e2e 1\n";

struct Root {
    os: &'static str,
    dir: PathBuf,
    port: u16,
    target: &'static Target,
    exe: &'static str,
}

fn roots(names: &[String]) -> Result<Vec<Root>, String> {
    let names: Vec<&str> = if names.is_empty() { vec!["linux"] } else { names.iter().map(String::as_str).collect() };
    names
        .into_iter()
        .map(|n| match n {
            "linux" => Ok(Root {
                os: "linux",
                dir: env_path("AMXX_LINUX_ROOT", home().join("games/amxx_rehlds_linux")),
                port: 27315,
                target: &TARGETS[0],
                exe: "hlds_linux",
            }),
            "win" => Ok(Root {
                os: "win",
                dir: env_path("AMXX_WIN_ROOT", home().join("games/amxx_rehlds_win")),
                port: 27316,
                target: &TARGETS[1],
                exe: "hlds.exe",
            }),
            other => Err(format!("unknown e2e root {other} (linux|win)")),
        })
        .collect()
}

/// A server of this root is running (the testenv's stop.sh check: name and cwd).
fn running(r: &Root) -> bool {
    let Ok(out) = Command::new("pgrep").args(["-x", r.exe]).output() else { return false };
    String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter_map(|p| std::fs::read_link(format!("/proc/{}/cwd", p.trim())).ok())
        .any(|cwd| cwd == r.dir)
}

fn rcon(testenv: &Path, port: u16, cmd: &str) -> String {
    Command::new("python3")
        .arg("-I")
        .arg(testenv.join("rcon.py"))
        .args(["--port", &port.to_string(), cmd])
        .output()
        .map(|o| String::from_utf8_lossy(&o.stdout).into_owned())
        .unwrap_or_default()
}

fn read(p: &Path) -> String {
    String::from_utf8_lossy(&std::fs::read(p).unwrap_or_default()).into_owned()
}

/// Replaces a file without writing through a hardlink (the roots hardlink their originals).
fn replace(p: &Path, bytes: &[u8]) -> Result<(), String> {
    let _ = std::fs::remove_file(p);
    std::fs::write(p, bytes).map_err(|e| format!("{}: {e}", p.display()))
}

fn copy_fresh(from: &Path, to: &Path) -> Result<(), String> {
    let _ = std::fs::remove_file(to);
    std::fs::copy(from, to).map(|_| ()).map_err(|e| format!("{} -> {}: {e}", from.display(), to.display()))
}

fn wait_for(file: &Path, timeout: Duration, cond: impl Fn(&str) -> bool) -> bool {
    let t0 = Instant::now();
    while t0.elapsed() < timeout {
        if cond(&read(file)) {
            return true;
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    false
}

/// `quit` over rcon so the engine runs ServerDeactivate (the plugin saves); `stop.sh` if the
/// server is still up after 20 s.
fn quit(r: &Root, testenv: &Path) {
    rcon(testenv, r.port, "quit");
    let t0 = Instant::now();
    while running(r) && t0.elapsed() < Duration::from_secs(20) {
        std::thread::sleep(Duration::from_millis(500));
    }
    let _ = Command::new(testenv.join("stop.sh")).arg(r.os).status();
}

/// tsx and tsfun as tsmod-amxx-rs's dist has them, building it when they are missing.
fn tsmod_amxx_modules(t: &Target) -> Result<Vec<PathBuf>, String> {
    let dir = env_path("TSMOD_AMXX_DIR", home().join("dev/hl/tsmod-amxx-rs"));
    let modules = dir.join("dist").join(t.platform).join("addons/amxmodx/modules");
    let files: Vec<PathBuf> = ["tsx", "tsfun"].iter().map(|m| modules.join((t.amxx_file)(m))).collect();
    if files.iter().any(|f| !f.is_file()) {
        run(Command::new(std::env::var("CARGO").unwrap_or_else(|_| "cargo".into())).args(["xtask", "dist"]).current_dir(&dir))?;
    }
    match files.iter().find(|f| !f.is_file()) {
        Some(f) => Err(format!("{} is missing after tsmod-amxx-rs's dist", f.display())),
        None => Ok(files),
    }
}

/// The three files the run changes, restored afterwards whatever happens.
struct Backup {
    files: Vec<(PathBuf, Option<Vec<u8>>)>,
}

impl Backup {
    fn take(paths: &[PathBuf]) -> Backup {
        Backup { files: paths.iter().map(|p| (p.clone(), std::fs::read(p).ok())).collect() }
    }
}

impl Drop for Backup {
    fn drop(&mut self) {
        for (p, bytes) in &self.files {
            let _ = std::fs::remove_file(p);
            if let Some(b) = bytes {
                let _ = std::fs::write(p, b);
            }
        }
    }
}

/// Adds `line` unless a line with the same first word is there (uncommented).
fn with_line(text: &str, line: &str) -> String {
    let key = line.split_whitespace().next().unwrap_or(line);
    if text.lines().any(|l| l.split_whitespace().next() == Some(key)) {
        return text.to_owned();
    }
    let sep = if text.ends_with('\n') || text.is_empty() { "" } else { "\n" };
    format!("{text}{sep}; tsmod-amxx-rcbot e2e\n{line}\n")
}

/// plugins.ini with other e2e plugins commented out and ours enabled.
fn plugins_ini(text: &str) -> String {
    let mut out: Vec<String> = text
        .lines()
        .map(|l| {
            let name = l.split_whitespace().next().unwrap_or("");
            if !l.starts_with(';') && name.ends_with("_e2e.amxx") { format!(";{l}") } else { l.to_owned() }
        })
        .filter(|l| l.split_whitespace().next() != Some(&format!("{PLUGIN}.amxx")))
        .collect();
    out.push(format!("; tsmod-amxx-rcbot e2e\n{PLUGIN}.amxx debug"));
    out.join("\n") + "\n"
}

fn install(r: &Root, tsmod: &[PathBuf]) -> Result<(), String> {
    let amxx = r.dir.join("ts/addons/amxmodx");
    for f in tsmod {
        copy_fresh(f, &amxx.join("modules").join(f.file_name().unwrap()))?;
    }
    for (_, module, _) in MODULES {
        let file = (r.target.amxx_file)(module);
        copy_fresh(
            &root().join("dist").join(r.target.platform).join("addons/amxmodx/modules").join(&file),
            &amxx.join("modules").join(&file),
        )?;
    }
    copy_fresh(&root().join(format!("target/pawn/{PLUGIN}.amxx")), &amxx.join(format!("plugins/{PLUGIN}.amxx")))?;
    let modules_ini = amxx.join("configs/modules.ini");
    let mut text = read(&modules_ini);
    for m in ["tsx", "tsfun", "rcbot_megahal", "rcbot_learn", "rcbot_nav"] {
        text = with_line(&text, m);
    }
    replace(&modules_ini, text.as_bytes())?;
    let plugins = amxx.join("configs/plugins.ini");
    replace(&plugins, plugins_ini(&read(&plugins)).as_bytes())?;
    let cfg = r.dir.join("ts/server.cfg");
    replace(&cfg, format!("{}{SERVER_CFG_LINES}", read(&cfg)).as_bytes())?;
    let _ = std::fs::remove_file(amxx.join(format!("logs/{LOG}")));
    Ok(())
}

/// One root; returns the failures.
fn e2e_root(r: &Root, testenv: &Path, tsmod: &[PathBuf]) -> Result<Vec<String>, String> {
    if running(r) {
        return Err(format!("a server is already running in {} (amxx-testenv/stop.sh {})", r.dir.display(), r.os));
    }
    let amxx = r.dir.join("ts/addons/amxmodx");
    if !amxx.join("configs/modules.ini").exists() {
        return Err(format!("{} has no AMX Mod X: run amxx-testenv/setup.sh {}", r.dir.display(), r.os));
    }
    let _backup = Backup::take(&[amxx.join("configs/modules.ini"), amxx.join("configs/plugins.ini"), r.dir.join("ts/server.cfg")]);
    install(r, tsmod)?;
    let log = amxx.join(format!("logs/{LOG}"));
    let started = SystemTime::now();
    run(Command::new(testenv.join("run.sh")).args([r.os, "--map", "ts_dojo", "--bots", "0"]))?;
    let mut fails = Vec::new();
    if !wait_for(&log, Duration::from_secs(90), |t| t.contains("CHECK backend PASS")) {
        fails.push(format!("{}: the backend wasn't activated within 90 s (plugin loaded? see {})", r.os, log.display()));
    }
    let plugins = rcon(testenv, r.port, "amxx plugins");
    if !plugins.lines().any(|l| l.contains(PLUGIN_TITLE) && l.to_lowercase().contains("running")) {
        fails.push(format!("{}: `amxx plugins` doesn't list {PLUGIN_TITLE} as running", r.os));
    }
    for cmd in ["addbot", "addbot", "rcbot addbot", "rcbot addbot"] {
        rcon(testenv, r.port, cmd);
        std::thread::sleep(Duration::from_secs(1));
    }
    let timeout = Duration::from_secs(std::env::var("E2E_TIMEOUT").ok().and_then(|v| v.parse().ok()).unwrap_or(300));
    let done = wait_for(&log, timeout, |t| t.contains("DONE"));
    quit(r, testenv);

    let text = read(&log);
    println!("--- {} {PLUGIN} ({}) ---", r.os, log.display());
    for l in text.lines() {
        // log_to_file lines: `L <date> - <time>: <message>`
        let msg = l.split_once(": ").map_or(l, |(_, m)| m);
        println!("  {msg}");
        if msg.starts_with("CHECK ") && msg.contains(" FAIL") {
            fails.push(format!("{}: {msg}", r.os));
        }
    }
    if !done {
        fails.push(format!("{}: no DONE in {} within {timeout:?}", r.os, log.display()));
    }
    // AMX Mod X runtime errors of this run that name the plugin or our modules.
    let ours = |l: &str| l.contains(PLUGIN) || l.contains("rcbot_megahal") || l.contains("rcbot_learn") || l.contains("rcbot_nav");
    if let Ok(dir) = std::fs::read_dir(amxx.join("logs")) {
        for e in dir.flatten() {
            let name = e.file_name().to_string_lossy().into_owned();
            let fresh = e.metadata().and_then(|m| m.modified()).is_ok_and(|m| m >= started);
            if !fresh {
                continue;
            }
            let t = read(&e.path());
            if name.starts_with("error_") {
                for l in t.lines().filter(|l| ours(l) || l.contains("Run time error")) {
                    fails.push(format!("{}: {name}: {l}", r.os));
                }
            } else if name.starts_with('L') && name.ends_with(".log") {
                for l in t.lines().filter(|l| ours(l)) {
                    println!("  amxx log: {}", l.split_once(": ").map_or(l, |(_, m)| m));
                }
            }
        }
    }
    Ok(fails)
}

/// `cargo xtask e2e [linux] [win]`.
pub fn e2e(names: &[String]) -> Result<(), String> {
    let testenv = env_path("AMXX_TESTENV", home().join("dev/hl/ref/amxx-testenv"));
    if !testenv.join("run.sh").exists() {
        return Err(format!("amxx-testenv not found ({}): see its README", testenv.display()));
    }
    let roots = roots(names)?;
    if let Some(r) = roots.iter().find(|r| running(r)) {
        return Err(format!("a server is already running in {} (amxx-testenv/stop.sh {})", r.dir.display(), r.os));
    }
    dist(false)?;
    pawn(true)?;
    let mut fails = Vec::new();
    for r in &roots {
        let tsmod = tsmod_amxx_modules(r.target)?;
        fails.extend(e2e_root(r, &testenv, &tsmod)?);
    }
    if fails.is_empty() {
        println!("e2e: all checks passed");
        Ok(())
    } else {
        Err(format!("e2e failures:\n  {}", fails.join("\n  ")))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plugins_ini_disables_other_e2e_plugins_and_adds_ours_once() {
        let text = "admin.amxx\ntsbot_e2e.amxx debug\n;ham_e2e.amxx debug\nrcbot_amxx.amxx\n";
        let out = plugins_ini(text);
        assert!(out.contains(";tsbot_e2e.amxx debug"));
        assert!(out.contains(";ham_e2e.amxx debug") && !out.contains(";;ham"));
        assert_eq!(out.matches("rcbot_amxx.amxx").count(), 1);
        assert!(out.ends_with("rcbot_amxx.amxx debug\n"));
    }

    #[test]
    fn with_line_adds_a_module_once() {
        let once = with_line("fun\nengine", "tsx");
        assert_eq!(with_line(&once, "tsx"), once);
        assert!(once.ends_with("tsx\n"));
    }
}
