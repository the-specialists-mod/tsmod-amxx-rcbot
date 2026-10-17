//! `cargo xtask compat [linux] [win]`: the data files are shared with the standalone tsmod_rcbot
//! (backend `rcbot`, `../tsmod-rcbot-rs`): what one writes, the other loads, on the same root.
//!
//! The amxx-testenv roots run the port, which loads `tsmod_*` libraries from `ts/dlls/`, so the
//! standalone plugin runs there too (its own e2e uses the port's roots, `~/games/hlds_*` /
//! `xash*`, through the port's `cargo xtask install`; this uses the AMX Mod X roots as they are).
//! Per root, with `ts/tsmod/rcbot/` backed up whole (in memory) and restored afterwards:
//!
//! 1. The e2e's install (modules, plugin, fixtures: four profiles, chat, bot_config.ini) with
//!    `config ts_kungfu 1` added (kung fu fights, so the `.rkf` files learn something), and the
//!    learned files removed: every one is written afresh by the first run.
//! 2. **rcbot_amxx** on ts_dojo, four `rcbot addbot`, `COMPAT_SECS` (default 120) of play, `quit`:
//!    the Pawn bot writes `botprofiles/<id>_hal.brn/.rld/.rkf/.rwr/.rlo/.rcr`, `players.rop`,
//!    `team<n>.rld`, `tsweaps.rld`, `weaponstats.rwr`, `loadouts.rlo`, `waypoints/ts/ts_dojo.rhm`.
//!    Copied to `target/compat/<os>/pawn/`.
//! 3. **tsmod_rcbot** (`../tsmod-rcbot-rs/dist/<platform>/dlls/`, its `cargo xtask dist` when
//!    missing) in `ts/dlls/`, rcbot_amxx disabled, `ts_bot_backend rcbot`, the same four bots and
//!    time: its log must show the waypoints loaded, the four profiles used, no damaged brain and no
//!    crash, and every file of step 2 rewritten (it loads each before it saves; a `.rcr` that has
//!    every player's record is left as it is). Copied to
//!    `target/compat/<os>/rcbot/`; the library is removed.
//! 4. **rcbot_amxx** again on what tsmod_rcbot wrote: no damaged brain, the Pawn bot's own readers
//!    (`.rkf`, `.rcr`, report.inc's `file` lines) parsed every profile's files, no runtime errors.
//!
//! The `.rcw`/`.rcv` waypoint files are written only by the editor (`waypoint save`, a client
//! command): both load the shipped ts_dojo.rcw here, and crates/nav's tests compare what
//! rcbot_nav writes with `rcbot::nav`'s own bytes. crates/learn's `compat` tests load the files of
//! step 2 (checked in as fixtures) with `rcbot`'s loaders.
//!
//! Environment: e2e's, plus `TSMOD_RCBOT_DIR` (default `~/dev/hl/tsmod-rcbot-rs`) and `COMPAT_SECS`.

use super::e2e::{self, Backup, Root};
use super::{dist, env_path, home, pawn, root, run};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, SystemTime};

/// The learned files the first run must write (relative to `tsmod/rcbot/`), per profile and global.
fn learned_files() -> Vec<String> {
    let mut v = Vec::new();
    for id in 1..=4 {
        for ext in ["_hal.brn", ".rld", ".rkf", ".rwr", ".rlo", ".rcr"] {
            v.push(format!("botprofiles/{id}{ext}"));
        }
    }
    for f in ["botprofiles/players.rop", "tsweaps.rld", "weaponstats.rwr", "loadouts.rlo", "waypoints/ts/ts_dojo.rhm"] {
        v.push(f.to_owned());
    }
    for t in 0..4 {
        v.push(format!("team{t}.rld"));
    }
    v
}

/// A directory's files and subdirectories held in memory (RCBot's data directory is small), so a
/// restore doesn't depend on a copy under `target/` that a `cargo clean` may remove mid-run.
pub(super) struct Snapshot {
    live: PathBuf,
    dirs: Vec<PathBuf>,
    files: Vec<(PathBuf, Vec<u8>)>,
}

impl Snapshot {
    pub(super) fn take(live: &Path) -> Result<Snapshot, String> {
        fn walk(base: &Path, dir: &Path, s: &mut Snapshot) -> Result<(), String> {
            for e in std::fs::read_dir(dir).map_err(|e| format!("{}: {e}", dir.display()))? {
                let p = e.map_err(|e| e.to_string())?.path();
                let rel = p.strip_prefix(base).unwrap().to_path_buf();
                if p.is_dir() {
                    s.dirs.push(rel);
                    walk(base, &p, s)?;
                } else {
                    s.files.push((rel, std::fs::read(&p).map_err(|e| format!("{}: {e}", p.display()))?));
                }
            }
            Ok(())
        }
        let mut s = Snapshot { live: live.to_owned(), dirs: Vec::new(), files: Vec::new() };
        walk(live, live, &mut s)?;
        Ok(s)
    }

    /// Replaces the directory with the snapshot.
    pub(super) fn restore(&self) -> Result<(), String> {
        let _ = std::fs::remove_dir_all(&self.live);
        std::fs::create_dir_all(&self.live).map_err(|e| format!("{}: {e}", self.live.display()))?;
        for d in &self.dirs {
            std::fs::create_dir_all(self.live.join(d)).map_err(|e| e.to_string())?;
        }
        for (rel, bytes) in &self.files {
            std::fs::write(self.live.join(rel), bytes).map_err(|e| format!("{}: {e}", rel.display()))?;
        }
        Ok(())
    }
}

/// `ts/tsmod/rcbot/` saved and put back whatever happens.
pub(super) struct DataBackup(Snapshot);

impl DataBackup {
    pub(super) fn take(live: &Path) -> Result<DataBackup, String> {
        Snapshot::take(live).map(DataBackup)
    }
}

impl Drop for DataBackup {
    fn drop(&mut self) {
        if let Err(e) = self.0.restore() {
            eprintln!("warning: restoring {}: {e}", self.0.live.display());
        }
    }
}

/// The standalone library in `ts/dlls/` for one run, removed afterwards whatever happens.
pub(super) struct Library(pub(super) PathBuf);

impl Drop for Library {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.0);
    }
}

/// tsmod_rcbot as its dist has it, building it when it is missing.
pub(super) fn standalone(r: &Root) -> Result<PathBuf, String> {
    let dir = env_path("TSMOD_RCBOT_DIR", home().join("dev/hl/tsmod-rcbot-rs"));
    let file = if r.os == "win" { "tsmod_rcbot.dll" } else { "tsmod_rcbot_i386.so" };
    let lib = dir.join("dist").join(r.target.platform).join("dlls").join(file);
    if !lib.is_file() {
        run(Command::new(std::env::var("CARGO").unwrap_or_else(|_| "cargo".into())).args(["xtask", "dist"]).current_dir(&dir))?;
    }
    if lib.is_file() { Ok(lib) } else { Err(format!("{} is missing after tsmod-rcbot-rs's dist", lib.display())) }
}

/// The server's console output: run.sh's server-stdout.log (hlds_linux) or qconsole.log (`-condebug`;
/// Wine's hlds.exe writes its console only there).
fn console_file(r: &Root) -> PathBuf {
    r.dir.join(if r.os == "win" { "qconsole.log" } else { "server-stdout.log" })
}

/// One server run: start on ts_dojo, wait for `ready` in the console, four `rcbot addbot`,
/// `secs` of play, quit. Returns the run's console output.
fn play(r: &Root, testenv: &Path, ready: &str, secs: u64) -> Result<String, String> {
    run(Command::new(testenv.join("run.sh")).args([r.os, "--map", e2e::MAP1, "--bots", "0"]))?;
    let up = e2e::wait_for(&console_file(r), Duration::from_secs(120), |t| t.contains(ready));
    // What a command prints while it runs goes back over rcon (the engine's redirect), and only
    // there on hlds.exe: the answers are part of the run's output.
    let mut answers = String::new();
    if up {
        for _ in 0..4 {
            answers += &e2e::rcon(testenv, r.port, "rcbot addbot");
            std::thread::sleep(Duration::from_secs(2));
        }
        std::thread::sleep(Duration::from_secs(secs));
    }
    e2e::quit(r, testenv);
    let text = e2e::read(&console_file(r)).replace('\r', "") + &answers.replace('\r', "");
    if up { Ok(text) } else { Err(format!("{}: {ready:?} not in the console within 120 s", r.os)) }
}

/// The server.cfg lines for a backend.
fn server_cfg(base: &str, backend: &str) -> String {
    format!("{base}\n// tsmod-amxx-rcbot compat\nts_bot_backend {backend}\nlocalinfo rcbot_e2e 1\n")
}

/// Copies the learned files that exist to `to` (fresh), returning their names.
fn snapshot(data: &Path, to: &Path) -> Result<Vec<String>, String> {
    let _ = std::fs::remove_dir_all(to);
    let mut got = Vec::new();
    for rel in learned_files() {
        let p = data.join(&rel);
        if p.is_file() {
            let dest = to.join(&rel);
            std::fs::create_dir_all(dest.parent().unwrap()).map_err(|e| e.to_string())?;
            std::fs::copy(&p, &dest).map_err(|e| format!("{}: {e}", p.display()))?;
            got.push(rel);
        }
    }
    Ok(got)
}

/// Lines of a console output that show a load went wrong (either bot's wording).
fn load_errors(text: &str) -> Vec<String> {
    text.lines()
        .filter(|l| {
            l.contains("HAL brain damaged")
                || l.contains("refuses to wake up")
                || l.contains("panicked")
                || l.contains("Segmentation")
                || l.contains("FATAL ERROR")
                || l.contains("not parsed")
        })
        .map(str::to_owned)
        .collect()
}

fn compat_root(r: &Root, testenv: &Path, tsmod: &[PathBuf], secs: u64) -> Result<Vec<String>, String> {
    if e2e::running(r) {
        return Err(format!("a server is already running in {} (amxx-testenv/stop.sh {})", r.dir.display(), r.os));
    }
    let lib = standalone(r)?;
    let amxx = r.dir.join("ts/addons/amxmodx");
    let data = r.dir.join("ts/tsmod/rcbot");
    let out = root().join("target/compat").join(r.os);
    let _data_backup = DataBackup::take(&data)?;
    let cfg = r.dir.join("ts/server.cfg");
    let plugins = amxx.join("configs/plugins.ini");
    let _backup = Backup::take(&[amxx.join("configs/modules.ini"), plugins.clone(), cfg.clone()]);
    let base_cfg = e2e::read(&cfg);
    let base_plugins = e2e::read(&plugins);
    e2e::install(r, tsmod, false)?;
    let bot_config = data.join("bot_config.ini");
    e2e::replace(&bot_config, format!("{}config ts_kungfu 1\n", e2e::read(&bot_config)).as_bytes())?;
    for rel in learned_files() {
        let _ = std::fs::remove_file(data.join(rel));
    }
    let log = amxx.join(format!("logs/{}", e2e::LOG));
    let mut fails = Vec::new();

    // 2. rcbot_amxx writes.
    e2e::replace(&cfg, server_cfg(&base_cfg, "rcbot_amxx").as_bytes())?;
    let _ = std::fs::remove_file(&log);
    let started = SystemTime::now();
    let a = play(r, testenv, "[rcbot_amxx] bot backend active", secs)?;
    fails.extend(load_errors(&a).into_iter().filter(|l| !l.contains("HAL brain damaged")).map(|l| format!("{} rcbot_amxx (1): {l}", r.os)));
    let fresh = e2e::fresh_files(&data, started);
    let pawn_files = snapshot(&data, &out.join("pawn"))?;
    for rel in learned_files() {
        if !fresh.contains(&rel) {
            fails.push(format!("{}: rcbot_amxx didn't write {rel}", r.os));
        }
    }
    println!("--- {}: rcbot_amxx wrote {} files ({})", r.os, pawn_files.len(), out.join("pawn").display());

    // 3. tsmod_rcbot loads them, plays and saves.
    copy_fresh_lib(&lib, &r.dir.join("ts/dlls"))?;
    let _lib = Library(r.dir.join("ts/dlls").join(lib.file_name().unwrap()));
    e2e::replace(&plugins, without_plugin(&base_plugins).as_bytes())?;
    e2e::replace(&cfg, server_cfg(&base_cfg, "rcbot").as_bytes())?;
    let started = SystemTime::now();
    let b = play(r, testenv, "[tsmod_rcbot] bot backend active", secs)?;
    let rc_lines: Vec<&str> = b.lines().filter(|l| l.contains("[tsmod_rcbot]") && !l.contains("] bot #")).collect();
    for l in rc_lines.iter().take(30) {
        println!("   | {l}");
    }
    if !b.lines().any(|l| l.contains("[tsmod_rcbot] loaded") && l.contains("waypoints for ts_dojo")) {
        fails.push(format!("{}: tsmod_rcbot didn't load ts_dojo's waypoints", r.os));
    }
    for id in 1..=4 {
        if !b.lines().any(|l| l.contains("[tsmod_rcbot] added") && l.contains(&format!(", profile {id})"))) {
            fails.push(format!("{}: tsmod_rcbot added no bot with profile {id}", r.os));
        }
    }
    fails.extend(load_errors(&b).into_iter().map(|l| format!("{} tsmod_rcbot: {l}", r.os)));
    let fresh = e2e::fresh_files(&data, started);
    for rel in &pawn_files {
        // A `.rcr` is written only when a player has no record in it yet (AddLoadRep): left as
        // it was, every bot found its record of every other in what rcbot_amxx wrote.
        let unchanged = || std::fs::read(data.join(rel)).ok() == std::fs::read(out.join("pawn").join(rel)).ok();
        if !fresh.contains(rel) && !(rel.ends_with(".rcr") && unchanged()) {
            fails.push(format!("{}: tsmod_rcbot didn't rewrite {rel}", r.os));
        }
    }
    let rcbot_files = snapshot(&data, &out.join("rcbot"))?;
    println!("--- {}: tsmod_rcbot rewrote {} files ({})", r.os, rcbot_files.len(), out.join("rcbot").display());
    drop(_lib);

    // 4. rcbot_amxx loads what tsmod_rcbot wrote.
    e2e::replace(&plugins, e2e::plugins_ini(&base_plugins, false).as_bytes())?;
    e2e::replace(&cfg, server_cfg(&base_cfg, "rcbot_amxx").as_bytes())?;
    let _ = std::fs::remove_file(&log);
    let started = SystemTime::now();
    let c = play(r, testenv, "[rcbot_amxx] bot backend active", secs.min(30))?;
    fails.extend(load_errors(&c).into_iter().map(|l| format!("{} rcbot_amxx (2): {l}", r.os)));
    let text = e2e::read(&log);
    let file_lines: Vec<&str> = text.lines().filter_map(|l| l.split_once(": file ").map(|(_, m)| m)).collect();
    for l in &file_lines {
        println!("   | file {l}");
    }
    for id in 1..=4 {
        for ext in ["rkf", "rcr"] {
            let name = format!("botprofiles/{id}.{ext}");
            if !file_lines.iter().any(|l| l.contains(&name) && l.contains("loaded:")) {
                fails.push(format!("{}: rcbot_amxx didn't load {name} as tsmod_rcbot wrote it", r.os));
            }
        }
    }
    fails.extend(text.lines().filter(|l| l.contains("not parsed")).map(|l| format!("{} rcbot_amxx (2): {l}", r.os)));
    if let Ok(dir) = std::fs::read_dir(amxx.join("logs")) {
        for e in dir.flatten() {
            let name = e.file_name().to_string_lossy().into_owned();
            if name.starts_with("error_") && e.metadata().and_then(|m| m.modified()).is_ok_and(|m| m >= started) {
                for l in e2e::read(&e.path()).lines().filter(|l| l.contains("rcbot") || l.contains("Run time error")) {
                    fails.push(format!("{}: {name}: {l}", r.os));
                }
            }
        }
    }
    Ok(fails)
}

pub(super) fn copy_fresh_lib(lib: &Path, dlls: &Path) -> Result<(), String> {
    let to = dlls.join(lib.file_name().unwrap());
    let _ = std::fs::remove_file(&to);
    std::fs::copy(lib, &to).map(|_| ()).map_err(|e| format!("{} -> {}: {e}", lib.display(), to.display()))
}

/// plugins.ini without rcbot_amxx (and the other e2e plugins), for the standalone bot's run.
pub(super) fn without_plugin(text: &str) -> String {
    e2e::plugins_ini(text, false).lines().filter(|l| !l.starts_with(&format!("{}.amxx", e2e::PLUGIN))).collect::<Vec<_>>().join("\n") + "\n"
}

/// `cargo xtask compat [linux] [win]`.
pub fn compat(names: &[String]) -> Result<(), String> {
    let testenv = env_path("AMXX_TESTENV", home().join("dev/hl/ref/amxx-testenv"));
    if !testenv.join("run.sh").exists() {
        return Err(format!("amxx-testenv not found ({}): see its README", testenv.display()));
    }
    let roots = e2e::roots(names)?;
    if let Some(r) = roots.iter().find(|r| e2e::running(r)) {
        return Err(format!("a server is already running in {} (amxx-testenv/stop.sh {})", r.dir.display(), r.os));
    }
    let secs = std::env::var("COMPAT_SECS").ok().and_then(|v| v.parse().ok()).unwrap_or(120);
    dist(false)?;
    pawn(true)?;
    let mut fails = Vec::new();
    for r in &roots {
        let tsmod = e2e::tsmod_amxx_modules(r.target)?;
        fails.extend(compat_root(r, &testenv, &tsmod, secs)?);
    }
    if fails.is_empty() {
        println!("compat: rcbot_amxx and tsmod_rcbot load each other's files");
        Ok(())
    } else {
        Err(format!("compat failures:\n  {}", fails.join("\n  ")))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_standalone_run_has_no_rcbot_amxx() {
        let out = without_plugin("admin.amxx\nrcbot_amxx.amxx debug\n");
        assert!(!out.contains("rcbot_amxx.amxx") || out.lines().all(|l| l.starts_with(';') || !l.contains("rcbot_amxx")));
        assert!(out.contains("admin.amxx"));
    }

    #[test]
    fn a_snapshot_puts_the_directory_back() {
        let dir = root().join("target/xtask-tests/snapshot");
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(dir.join("botprofiles/empty")).unwrap();
        std::fs::write(dir.join("botprofiles/1.rld"), b"learned").unwrap();
        std::fs::write(dir.join("bot_weapons.ini"), b"w").unwrap();
        let s = Snapshot::take(&dir).unwrap();
        std::fs::remove_file(dir.join("bot_weapons.ini")).unwrap();
        std::fs::write(dir.join("botprofiles/1.rld"), b"changed").unwrap();
        std::fs::write(dir.join("new.rhm"), b"x").unwrap();
        s.restore().unwrap();
        assert_eq!(std::fs::read(dir.join("botprofiles/1.rld")).unwrap(), b"learned");
        assert_eq!(std::fs::read(dir.join("bot_weapons.ini")).unwrap(), b"w");
        assert!(!dir.join("new.rhm").exists() && dir.join("botprofiles/empty").is_dir());
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn errors_are_found_in_either_wording() {
        let text = "[tsmod_rcbot] bot profile (1) HAL brain damaged!\nok\nfile botprofiles/1.rkf not parsed: the defaults kept\n";
        assert_eq!(load_errors(text).len(), 2);
    }
}
