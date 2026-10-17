//! `cargo xtask e2e [linux] [win]`: the Pawn bot on the amxx-testenv roots (`../ref/amxx-testenv`,
//! `~/games/amxx_rehlds_{linux,win}`), modelled on tsmod-amxx-rs's e2e.
//!
//! 1. `dist` (our three modules, release) and `pawn` (rcbot_amxx.amxx); tsx and tsfun from
//!    tsmod-amxx-rs's `dist/` (its `cargo xtask dist` runs first when they are missing).
//! 2. Per root: refuse if its server runs; back up `configs/modules.ini`, `configs/plugins.ini`,
//!    `ts/server.cfg` and the RCBot files of [`fixtures`]; install the modules and the plugin;
//!    enable tsx, tsfun, rcbot_megahal, rcbot_learn, rcbot_nav and `rcbot_amxx.amxx debug`,
//!    comment out other `*_e2e` plugins (one sets `ts_bot_backend` to itself); append
//!    `ts_bot_backend rcbot_amxx` and `localinfo rcbot_e2e 1` to server.cfg (read at map start,
//!    before plugin_init: the backend takes over at once); write the fixtures into
//!    `ts/tsmod/rcbot/` (four profiles with a MegaHAL training text, bot_chat.ini, bot_config.ini
//!    turning chat on, MAP2's map config with `ts_kungfu` and `min_bots 4`) and remove the four
//!    profiles' brains (so they are made and pretrained afresh).
//! 3. `run.sh <os> --map ts_dojo --bots 0` (ts_bot_quota 0), wait for the plugin's `CHECK backend
//!    PASS`, then over rcon `addbot` twice (the port's quota path, ts_bot_add) and `rcbot addbot`
//!    twice (ts_bot_create), and wait for `DONE map=ts_dojo` in `logs/rcbot_amxx_e2e.log` (phase 1:
//!    report.inc's checks, waypoints to a kill and a MegaHAL reply).
//! 4. The `rcbot` server command over rcon ([`RCON_CHECKS`]), then `changelevel ts_lobby`: the
//!    plugin saves, reloads, and its phase 2 checks (the bots back, kung fu) end with `DONE
//!    map=ts_lobby`. `stats` (the server's fps) is printed.
//! 5. `quit` over rcon (ServerDeactivate saves), `stop.sh` if it hangs; the data files of
//!    [`saved_files`] must have been written during the run; restore the backed up files.
//! 6. Failures: every `CHECK ... FAIL` line, a missing `DONE` or one with failures, an rcon answer
//!    or a saved file missing, `amxx plugins` not listing the plugin as running, and AMX Mod X
//!    runtime errors (`error_*.log` written during the run, lines naming rcbot_amxx or our
//!    modules).
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
pub(super) const PLUGIN: &str = "rcbot_amxx";
pub(super) const LOG: &str = "rcbot_amxx_e2e.log";
/// What the plugin registers as (register_plugin's title) for `amxx plugins`.
const PLUGIN_TITLE: &str = "RCBot AMXX";
/// The first map and the one `changelevel` goes to: both have shipped waypoints
/// (dist/<platform>/tsmod/rcbot/waypoints/ts/) and are in the testenv's maps.
pub(super) const MAP1: &str = "ts_dojo";
pub(super) const MAP2: &str = "ts_lobby";
/// The lines appended to server.cfg (after a newline: server.cfg may end without one).
const SERVER_CFG_LINES: &str = "\n// tsmod-amxx-rcbot e2e\nts_bot_backend rcbot_amxx\nlocalinfo rcbot_e2e 1\n";

pub(super) struct Root {
    pub(super) os: &'static str,
    pub(super) dir: PathBuf,
    pub(super) port: u16,
    pub(super) target: &'static Target,
    pub(super) exe: &'static str,
}

pub(super) fn roots(names: &[String]) -> Result<Vec<Root>, String> {
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
pub(super) fn running(r: &Root) -> bool {
    let Ok(out) = Command::new("pgrep").args(["-x", r.exe]).output() else { return false };
    String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter_map(|p| std::fs::read_link(format!("/proc/{}/cwd", p.trim())).ok())
        .any(|cwd| cwd == r.dir)
}

pub(super) fn rcon(testenv: &Path, port: u16, cmd: &str) -> String {
    Command::new("python3")
        .arg("-I")
        .arg(testenv.join("rcon.py"))
        .args(["--port", &port.to_string(), cmd])
        .output()
        .map(|o| String::from_utf8_lossy(&o.stdout).into_owned())
        .unwrap_or_default()
}

pub(super) fn read(p: &Path) -> String {
    String::from_utf8_lossy(&std::fs::read(p).unwrap_or_default()).into_owned()
}

/// Replaces a file without writing through a hardlink (the roots hardlink their originals).
pub(super) fn replace(p: &Path, bytes: &[u8]) -> Result<(), String> {
    let _ = std::fs::remove_file(p);
    std::fs::write(p, bytes).map_err(|e| format!("{}: {e}", p.display()))
}

pub(super) fn copy_fresh(from: &Path, to: &Path) -> Result<(), String> {
    let _ = std::fs::remove_file(to);
    std::fs::copy(from, to).map(|_| ()).map_err(|e| format!("{} -> {}: {e}", from.display(), to.display()))
}

pub(super) fn wait_for(file: &Path, timeout: Duration, cond: impl Fn(&str) -> bool) -> bool {
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
pub(super) fn quit(r: &Root, testenv: &Path) {
    rcon(testenv, r.port, "quit");
    let t0 = Instant::now();
    while running(r) && t0.elapsed() < Duration::from_secs(20) {
        std::thread::sleep(Duration::from_millis(500));
    }
    let _ = Command::new(testenv.join("stop.sh")).arg(r.os).status();
}

/// tsx and tsfun as tsmod-amxx-rs's dist has them, building it when they are missing.
pub(super) fn tsmod_amxx_modules(t: &Target) -> Result<Vec<PathBuf>, String> {
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

/// RCBot's files the run puts in `ts/tsmod/rcbot/` (backed up and restored with the configs): four
/// profiles, so the bots have MegaHAL brains to save (`botprofiles/<id>_hal.brn`); chat lines;
/// bot_config.ini turning chatting and replies (to bots too) on; and MAP2's map config turning
/// `ts_kungfu` on (the bots fight with kung fu there), which also runs the map config path.
pub(super) fn fixtures() -> Vec<(String, String)> {
    let mut f = vec![("bot_profiles.ini".to_owned(), "1\n2\n3\n4\n".to_owned())];
    for (id, name, skill) in [(1, "Neo", 80), (2, "Trinity", 70), (3, "Morpheus", 90), (4, "Switch", 60)] {
        f.push((
            format!("botprofiles/{id}.ini"),
            format!("# tsmod-amxx-rcbot e2e\nname=\"{name}\"\nskill={skill}\nhal_pretrain_file=\"e2e_hal.trn\"\n"),
        ));
    }
    // The brains' training text (read when a brain is new: the run starts without the four
    // brains, see `install`), so MegaHAL has words to reply with.
    f.push((
        "botprofiles/e2e_hal.trn".to_owned(),
        [
            "the specialists is a game about kung fu and guns",
            "i like the dojo map because the fights are close",
            "nice shot you got me with the shotgun",
            "the bots run along the waypoints and shoot at each other",
            "who wants to fight me with kung fu in the dojo",
            "",
        ]
        .join("\n"),
    ));
    f.push((
        "bot_chat.ini".to_owned(),
        [
            "# tsmod-amxx-rcbot e2e",
            "[greetings-friendly]",
            "hi %n",
            "[greetings-neutral]",
            "hello %n, welcome to %m",
            "[greetings-unfriendly]",
            "oh, it's %n",
            "[kills-friendly]",
            "sorry %n",
            "[kills-neutral]",
            "got you %n",
            "[kills-unfriendly]",
            "take that %n",
            "[killed-friendly]",
            "nice shot %n",
            "[killed-neutral]",
            "you got lucky %n",
            "[killed-unfriendly]",
            "i will get you %n",
            "[idle]",
            "anyone seen %r",
            "",
        ]
        .join("\n"),
    ));
    f.push((
        "bot_config.ini".to_owned(),
        "config chatting 1\nconfig chat_reply_to_bots 1\nconfig chat_percent 100\nconfig chat_reply_percent 100\n".to_owned(),
    ));
    // ReHLDS drops fake clients at a level change: RCBot's min_bots (the rejoin code) brings
    // four back, one every 5 s.
    f.push((format!("map_configs/ts/{MAP2}.cfg"), "rcbot config ts_kungfu 1\nrcbot config min_bots 4\n".to_owned()));
    // Not fixtures but backed up and removed before the run: the four brains, so they are made
    // and pretrained afresh (empty contents mean "remove" in `install`).
    for id in 1..=4 {
        f.push((format!("botprofiles/{id}_hal.brn"), String::new()));
    }
    f
}

/// Whether a data file (relative to `tsmod/rcbot/`) is one of a kind.
type FileMatch = fn(&str) -> bool;

/// The data files the bots must have saved during the run (newer than its start): the MegaHAL
/// brains, the learned data, the weapon stats and both maps' heatmaps (bots.rs save_bot,
/// save_globals; plugin_end at the level change and at quit).
fn saved_files() -> Vec<(String, FileMatch)> {
    vec![
        ("botprofiles/<id>_hal.brn".into(), |n: &str| n.starts_with("botprofiles/") && n.ends_with("_hal.brn")),
        ("botprofiles/<key>.rld".into(), |n: &str| n.starts_with("botprofiles/") && n.ends_with(".rld")),
        ("botprofiles/<key>.rkf".into(), |n: &str| n.starts_with("botprofiles/") && n.ends_with(".rkf")),
        ("team<n>.rld".into(), |n: &str| n.starts_with("team") && n.ends_with(".rld")),
        ("weaponstats.rwr".into(), |n: &str| n == "weaponstats.rwr"),
        (format!("waypoints/ts/{MAP1}.rhm"), |n: &str| n == "waypoints/ts/ts_dojo.rhm"),
        (format!("waypoints/ts/{MAP2}.rhm"), |n: &str| n == "waypoints/ts/ts_lobby.rhm"),
    ]
}

/// The files under `dir` (relative, `/`-separated) modified at or after `since`.
pub(super) fn fresh_files(dir: &Path, since: SystemTime) -> Vec<String> {
    fn walk(base: &Path, dir: &Path, since: SystemTime, out: &mut Vec<String>) {
        let Ok(rd) = std::fs::read_dir(dir) else { return };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                walk(base, &p, since, out);
            } else if e.metadata().and_then(|m| m.modified()).is_ok_and(|m| m >= since) {
                out.push(p.strip_prefix(base).unwrap_or(&p).to_string_lossy().replace('\\', "/"));
            }
        }
    }
    let mut out = Vec::new();
    walk(dir, dir, since, &mut out);
    out.sort();
    out
}

/// The rcon commands and what their answer must contain: the `rcbot` server command (help, and
/// bot_config.ini's settings read back).
const RCON_CHECKS: [(&str, &str); 3] = [
    ("rcbot help config", "[RCBOT] config command help"),
    ("rcbot config chatting", "\"chatting\" is currently \"1\""),
    ("rcbot config chat_reply_percent", "100"),
];

/// The files the run changes (the configs, RCBot's fixtures), restored afterwards whatever happens;
/// a directory the run made for a file that wasn't there goes again if it is empty.
pub(super) struct Backup {
    files: Vec<(PathBuf, Option<Vec<u8>>)>,
}

impl Backup {
    pub(super) fn take(paths: &[PathBuf]) -> Backup {
        Backup { files: paths.iter().map(|p| (p.clone(), std::fs::read(p).ok())).collect() }
    }
}

impl Drop for Backup {
    fn drop(&mut self) {
        for (p, bytes) in &self.files {
            let _ = std::fs::remove_file(p);
            match bytes {
                Some(b) => {
                    let _ = std::fs::write(p, b);
                }
                None => {
                    let mut dir = p.parent();
                    while let Some(d) = dir.filter(|d| std::fs::remove_dir(d).is_ok()) {
                        dir = d.parent();
                    }
                }
            }
        }
    }
}

/// Adds `line` unless a line with the same first word is there (uncommented).
pub(super) fn with_line(text: &str, line: &str) -> String {
    let key = line.split_whitespace().next().unwrap_or(line);
    if text.lines().any(|l| l.split_whitespace().next() == Some(key)) {
        return text.to_owned();
    }
    let sep = if text.ends_with('\n') || text.is_empty() { "" } else { "\n" };
    format!("{text}{sep}; tsmod-amxx-rcbot e2e\n{line}\n")
}

/// plugins.ini with other e2e plugins commented out and ours enabled (with AMX Mod X's `debug`
/// flag: stack traces, and its own timing warnings).
pub(super) fn plugins_ini(text: &str, debug: bool) -> String {
    let mut out: Vec<String> = text
        .lines()
        .map(|l| {
            let name = l.split_whitespace().next().unwrap_or("");
            if !l.starts_with(';') && name.ends_with("_e2e.amxx") { format!(";{l}") } else { l.to_owned() }
        })
        .filter(|l| l.split_whitespace().next() != Some(&format!("{PLUGIN}.amxx")))
        .collect();
    out.push(format!("; tsmod-amxx-rcbot e2e\n{PLUGIN}.amxx{}", if debug { " debug" } else { "" }));
    out.join("\n") + "\n"
}

pub(super) fn install(r: &Root, tsmod: &[PathBuf], debug: bool) -> Result<(), String> {
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
    // RCBot's data (bot_weapons.ini, the waypoints), as dist ships it: ts/tsmod/rcbot/.
    super::copy_dir(&root().join("dist").join(r.target.platform).join("tsmod/rcbot"), &r.dir.join("ts/tsmod/rcbot"))?;
    let modules_ini = amxx.join("configs/modules.ini");
    let mut text = read(&modules_ini);
    for m in ["tsx", "tsfun", "rcbot_megahal", "rcbot_learn", "rcbot_nav"] {
        text = with_line(&text, m);
    }
    replace(&modules_ini, text.as_bytes())?;
    let plugins = amxx.join("configs/plugins.ini");
    replace(&plugins, plugins_ini(&read(&plugins), debug).as_bytes())?;
    let cfg = r.dir.join("ts/server.cfg");
    replace(&cfg, format!("{}{SERVER_CFG_LINES}", read(&cfg)).as_bytes())?;
    let data = r.dir.join("ts/tsmod/rcbot");
    for (rel, text) in fixtures() {
        let p = data.join(&rel);
        if let Some(d) = p.parent() {
            std::fs::create_dir_all(d).map_err(|e| format!("{}: {e}", d.display()))?;
        }
        if text.is_empty() {
            let _ = std::fs::remove_file(&p);
        } else {
            replace(&p, text.as_bytes())?;
        }
    }
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
    let mut backed = vec![amxx.join("configs/modules.ini"), amxx.join("configs/plugins.ini"), r.dir.join("ts/server.cfg")];
    backed.extend(fixtures().into_iter().map(|(rel, _)| r.dir.join("ts/tsmod/rcbot").join(rel)));
    let _backup = Backup::take(&backed);
    install(r, tsmod, true)?;
    let log = amxx.join(format!("logs/{LOG}"));
    let started = SystemTime::now();
    run(Command::new(testenv.join("run.sh")).args([r.os, "--map", MAP1, "--bots", "0"]))?;
    let mut fails = Vec::new();
    if !wait_for(&log, Duration::from_secs(90), |t| t.contains("CHECK backend PASS")) {
        fails.push(format!("{}: the backend wasn't activated within 90 s (plugin loaded? see {})", r.os, log.display()));
    }
    let plugins = rcon(testenv, r.port, "amxx plugins");
    // Installed with plugins.ini's `debug` flag, which `amxx plugins` lists as "debug", not "running".
    let running = |l: &str| l.split_whitespace().last().is_some_and(|s| s == "running" || s == "debug");
    if !plugins.lines().any(|l| l.contains(PLUGIN_TITLE) && running(l)) {
        fails.push(format!("{}: `amxx plugins` doesn't list {PLUGIN_TITLE} as running or debug", r.os));
    }
    for cmd in ["addbot", "addbot", "rcbot addbot", "rcbot addbot"] {
        rcon(testenv, r.port, cmd);
        std::thread::sleep(Duration::from_secs(1));
    }
    let timeout = Duration::from_secs(std::env::var("E2E_TIMEOUT").ok().and_then(|v| v.parse().ok()).unwrap_or(300));
    let done1 = wait_for(&log, timeout, |t| t.contains(&format!("DONE map={MAP1}")));
    for (cmd, want) in RCON_CHECKS {
        let got = rcon(testenv, r.port, cmd);
        if got.contains(want) {
            println!("  rcon {cmd:?}: PASS ({})", got.lines().find(|l| l.contains(want)).unwrap_or("").trim());
        } else {
            fails.push(format!("{}: rcon {cmd:?} doesn't answer {want:?}: {:?}", r.os, got.trim()));
        }
    }
    // The level change: the plugin saves (plugin_end), reloads on MAP2 and adopts the bots the
    // engine kept; its phase 2 checks end with DONE map=MAP2.
    rcon(testenv, r.port, &format!("changelevel {MAP2}"));
    let done2 = wait_for(&log, timeout, |t| t.contains(&format!("DONE map={MAP2}")));
    let stats = rcon(testenv, r.port, "stats");
    println!("  stats: {}", stats.lines().last().unwrap_or("").trim());
    quit(r, testenv);

    let text = read(&log);
    println!("--- {} {PLUGIN} ({}) ---", r.os, log.display());
    for l in text.lines() {
        // log_to_file lines: `L <date> - <time>: <message>`
        let msg = l.split_once(": ").map_or(l, |(_, m)| m);
        println!("  {msg}");
        if msg.starts_with("CHECK ") && msg.contains(" FAIL") || msg.starts_with("DONE ") && !msg.contains(" fail=0") {
            fails.push(format!("{}: {msg}", r.os));
        }
    }
    for (map, done) in [(MAP1, done1), (MAP2, done2)] {
        if !done {
            fails.push(format!("{}: no DONE map={map} in {} within {timeout:?}", r.os, log.display()));
        }
    }
    let fresh = fresh_files(&r.dir.join("ts/tsmod/rcbot"), started);
    for (what, matches) in saved_files() {
        match fresh.iter().find(|n| matches(n)) {
            Some(n) => println!("  saved {what}: PASS ({n})"),
            None => fails.push(format!("{}: no {what} saved during the run (fresh: {})", r.os, fresh.join(" "))),
        }
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
        let out = plugins_ini(text, true);
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
