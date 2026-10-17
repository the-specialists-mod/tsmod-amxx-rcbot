//! Build, package and test tooling: `cargo xtask <command>`.
//!
//! ```text
//! build [--release]          the modules (rcbot_megahal, rcbot_learn, rcbot_nav) for i686 Linux and Windows
//! dist [--debug-info]        release build -> dist/<platform>/addons/amxmodx/{modules,scripting/include}/ (stripped
//!                            unless --debug-info), with AMX Mod X's file names
//! lint                       cargo fmt --check, clippy -D warnings on both targets
//! test                       unit tests on the host and on i686 Linux, then the Pawn compile check (pawn)
//! pawn                       compiles scripting/**/*.sma with amxxpc against include/ (into target/pawn/)
//! e2e                        not yet: see AGENTS.md
//! ```
//!
//! Environment: `FWGS_DIR` (FWGS headers, default: `xash3d-fwgs` next to the checkout or one of its
//! ancestors, so git worktrees under `.claude/worktrees/` find it too; an `FWGS_DIR` without the
//! headers is ignored), `AMXX_SCRIPTING` (amxxpc's directory, default
//! `~/dev/hl/ref/extracted/amxmodx-1.10.0-git5487-linux/addons/amxmodx/scripting`; `pawn` and
//! `test` skip the compile check with a note when it is missing).

use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};

/// The modules: cargo package, AMX Mod X's base name, and whether it is also a metamod plugin
/// (rcbot_nav: it needs the engine table for traces).
const MODULES: [(&str, &str, bool); 3] =
    [("rcbot_megahal", "rcbot_megahal", false), ("rcbot_learn", "rcbot_learn", false), ("rcbot_nav", "rcbot_nav", true)];

/// The includes shipped in dist.
const INCLUDES: [&str; 3] = ["rcbot_megahal.inc", "rcbot_learn.inc", "rcbot_nav.inc"];

/// A build target and the file names AMX Mod X loads.
struct Target {
    triple: &'static str,
    platform: &'static str,
    strip: &'static str,
    /// cargo's file name and AMX Mod X's, from the module name.
    cargo_file: fn(&str) -> String,
    amxx_file: fn(&str) -> String,
}

const TARGETS: [Target; 2] = [
    Target {
        triple: "i686-unknown-linux-gnu",
        platform: "linux-i686",
        strip: "strip",
        cargo_file: |m| format!("lib{m}.so"),
        amxx_file: |m| format!("{m}_amxx_i386.so"),
    },
    Target {
        triple: "i686-pc-windows-gnu",
        platform: "windows-i686",
        strip: "i686-w64-mingw32-strip",
        cargo_file: |m| format!("{m}.dll"),
        amxx_file: |m| format!("{m}_amxx.dll"),
    },
];

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap().to_path_buf()
}

fn home() -> PathBuf {
    PathBuf::from(std::env::var("HOME").unwrap_or_default())
}

fn env_path(var: &str, default: PathBuf) -> PathBuf {
    std::env::var(var).map(PathBuf::from).unwrap_or(default)
}

fn run(cmd: &mut Command) -> Result<(), String> {
    eprintln!("$ {cmd:?}");
    let st = cmd.status().map_err(|e| format!("{cmd:?}: {e}"))?;
    if st.success() { Ok(()) } else { Err(format!("{cmd:?} failed: {st}")) }
}

fn is_fwgs(dir: &Path) -> bool {
    dir.join("common/xash3d_types.h").is_file()
}

/// `xash3d-fwgs` next to the checkout or the nearest ancestor that has one. `.cargo/config.toml`'s
/// fixed `../xash3d-fwgs` misses it from a worktree nested in the checkout, and cargo hands that
/// value to xtask too, so `FWGS_DIR` only wins when it holds the FWGS headers.
fn fwgs_dir() -> Option<PathBuf> {
    root().ancestors().skip(1).map(|d| d.join("xash3d-fwgs")).find(|p| is_fwgs(p))
}

fn cargo() -> Command {
    let mut c = Command::new(std::env::var("CARGO").unwrap_or_else(|_| "cargo".into()));
    c.current_dir(root());
    if !std::env::var_os("FWGS_DIR").is_some_and(|d| is_fwgs(Path::new(&d)))
        && let Some(dir) = fwgs_dir()
    {
        c.env("FWGS_DIR", dir);
    }
    c
}

fn build(release: bool) -> Result<(), String> {
    for t in &TARGETS {
        let mut c = cargo();
        c.arg("build");
        for (pkg, _, _) in MODULES {
            c.args(["-p", pkg]);
        }
        c.args(["--target", t.triple]);
        if release {
            c.arg("--release");
        }
        run(&mut c)?;
    }
    Ok(())
}

fn built(t: &Target, release: bool, file: &str) -> PathBuf {
    root().join("target").join(t.triple).join(if release { "release" } else { "debug" }).join(file)
}

/// The library's dynamic exports (`nm -D` on Linux, the export table on Windows), lower case.
fn exports(t: &Target, lib: &Path) -> Result<String, String> {
    let out = if t.platform.starts_with("windows") {
        Command::new("i686-w64-mingw32-objdump").arg("-p").arg(lib).output()
    } else {
        Command::new("nm").args(["-D", "--defined-only"]).arg(lib).output()
    }
    .map_err(|e| format!("inspecting {}: {e}", lib.display()))?;
    Ok(String::from_utf8_lossy(&out.stdout).to_lowercase())
}

/// Every module exports the `AMXX_*` entry points. The pure AMX Mod X modules export nothing
/// metamod looks for (AMX Mod X treats a library exporting `Meta_Attach` as a metamod plugin);
/// the metamod ones (rcbot_nav) export `Meta_Query`, `Meta_Attach`, `Meta_Detach` and an
/// undecorated `GiveFnptrsToDll` (metamod looks up the plain stdcall name on Windows). The server
/// loads the DLLs with only system DLLs around, so they import no MinGW runtime.
fn check_library(t: &Target, lib: &Path, metamod: bool) -> Result<(), String> {
    let text = exports(t, lib)?;
    for need in ["amxx_query", "amxx_attach", "amxx_detach", "amxx_pluginsloaded"] {
        if !text.contains(need) {
            return Err(format!("{} doesn't export {need}", lib.display()));
        }
    }
    let meta = ["meta_query", "meta_attach", "meta_detach", "givefnptrstodll"];
    if metamod {
        if let Some(need) = meta.iter().find(|m| !text.contains(*m)) {
            return Err(format!("{} doesn't export the metamod entry point {need}", lib.display()));
        }
        if text.contains("givefnptrstodll@") {
            return Err(format!("{} exports a decorated GiveFnptrsToDll", lib.display()));
        }
    } else if meta.iter().any(|m| text.contains(m)) {
        return Err(format!("{} exports metamod entry points", lib.display()));
    }
    if t.platform.starts_with("windows") {
        for bad in ["libgcc", "libwinpthread", "libstdc"] {
            if text.lines().any(|l| l.contains("dll name:") && l.contains(bad)) {
                return Err(format!("{} imports a MinGW runtime DLL ({bad})", lib.display()));
            }
        }
    }
    Ok(())
}

fn dist(debug_info: bool) -> Result<(), String> {
    build(true)?;
    for t in &TARGETS {
        let base = root().join("dist").join(t.platform).join("addons/amxmodx");
        let dir = base.join("modules");
        std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
        for (_, module, metamod) in MODULES {
            let (from, to) = ((t.cargo_file)(module), (t.amxx_file)(module));
            let dest = dir.join(&to);
            std::fs::copy(built(t, true, &from), &dest).map_err(|e| format!("{from}: {e}"))?;
            if !debug_info {
                run(Command::new(t.strip).arg(&dest))?;
            }
            check_library(t, &dest, metamod)?;
            eprintln!("  {}", dest.display());
        }
        let inc = base.join("scripting/include");
        std::fs::create_dir_all(&inc).map_err(|e| e.to_string())?;
        for f in INCLUDES {
            std::fs::copy(root().join("include").join(f), inc.join(f)).map_err(|e| e.to_string())?;
        }
    }
    Ok(())
}

fn lint() -> Result<(), String> {
    run(cargo().args(["fmt", "--all", "--check"]))?;
    for t in &TARGETS {
        run(cargo().args(["clippy", "--workspace", "--all-targets", "--target", t.triple, "--", "-D", "warnings"]))?;
    }
    Ok(())
}

fn scripting_dir() -> PathBuf {
    env_path("AMXX_SCRIPTING", home().join("dev/hl/ref/extracted/amxmodx-1.10.0-git5487-linux/addons/amxmodx/scripting"))
}

/// Every `.sma` under `dir`, recursively.
fn sources(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    let mut entries: Vec<PathBuf> = rd.filter_map(|e| e.ok().map(|e| e.path())).collect();
    entries.sort();
    for p in entries {
        if p.is_dir() {
            sources(&p, out);
        } else if p.extension().is_some_and(|x| x == "sma") {
            out.push(p);
        }
    }
}

/// Compiles `sma` with amxxpc, `include/` first, into `out`.
fn compile_plugin(scripting: &Path, sma: &Path, out: &Path) -> Result<(), String> {
    std::fs::create_dir_all(out.parent().unwrap()).map_err(|e| e.to_string())?;
    // AMX Mod X 1.8.2's amxxpc exits 0 after errors: only a fresh output file means success.
    let _ = std::fs::remove_file(out);
    let mut c = Command::new(scripting.join("amxxpc"));
    c.env("LD_LIBRARY_PATH", scripting)
        .arg(sma)
        .arg(format!("-i{}", root().join("include").display()))
        .arg(format!("-i{}", scripting.join("include").display()))
        .arg(format!("-o{}", out.display()));
    // amxxpc writes scratch files into the current directory.
    c.current_dir(out.parent().unwrap());
    let o = c.output().map_err(|e| format!("amxxpc: {e}"))?;
    let text = format!("{}{}", String::from_utf8_lossy(&o.stdout), String::from_utf8_lossy(&o.stderr));
    if !o.status.success() || !out.exists() || text.to_lowercase().contains("warning") {
        return Err(format!("amxxpc on {} ({}):\n{text}", sma.display(), o.status));
    }
    eprintln!("  {}", out.display());
    Ok(())
}

/// Compiles every plugin under `scripting/` (warnings are errors). `Ok(false)` when amxxpc
/// isn't there.
fn pawn(required: bool) -> Result<bool, String> {
    let scripting = scripting_dir();
    if !scripting.join("amxxpc").is_file() {
        let msg = format!("no amxxpc in {} (set AMXX_SCRIPTING)", scripting.display());
        if required {
            return Err(msg);
        }
        eprintln!("note: {msg}; skipping the Pawn compile check");
        return Ok(false);
    }
    let mut files = Vec::new();
    sources(&root().join("scripting"), &mut files);
    for sma in &files {
        let rel = sma.strip_prefix(root().join("scripting")).unwrap();
        let out = root().join("target/pawn").join(rel).with_extension("amxx");
        compile_plugin(&scripting, sma, &out)?;
    }
    Ok(true)
}

fn test() -> Result<(), String> {
    run(cargo().args(["test", "--workspace"]))?;
    run(cargo().args(["test", "--workspace", "--target", "i686-unknown-linux-gnu"]))?;
    pawn(false).map(|_| ())
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let flag = |f: &str| args.iter().any(|a| a == f);
    let r = match args.first().map(String::as_str) {
        Some("build") => build(flag("--release")),
        Some("dist") => dist(flag("--debug-info")),
        Some("lint") => lint(),
        Some("test") => test(),
        Some("pawn") => pawn(true).map(|_| ()),
        // TODO: end-to-end on the amxx-testenv roots (../ref/amxx-testenv), as tsmod-amxx-rs's
        // xtask does, once the Pawn bot plugin (scripting/rcbot_amxx.sma) exists.
        Some("e2e") => Err("e2e is not implemented yet (see AGENTS.md)".into()),
        _ => Err("usage: cargo xtask <build [--release] | dist [--debug-info] | lint | test | pawn | e2e>".into()),
    };
    match r {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}
