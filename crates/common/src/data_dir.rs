// tsmod-amxx-rcbot, GPL-3.0-or-later with the AMX Mod X and RCBot exceptions. The file handling
// follows tsmod-rcbot-rs's host (`State::read` / `State::write`).

//! RCBot's data directory: `<gamedir>/tsmod/rcbot/`, the same folder the standalone
//! `tsmod_rcbot` plugin reads and writes, so both share brains and learned files.

use std::path::{Component, Path, PathBuf};

/// A data directory and the files below it.
#[derive(Clone, Debug)]
pub struct DataDir {
    root: PathBuf,
}

impl DataDir {
    /// The data directory at `root`.
    pub fn new(root: impl Into<PathBuf>) -> DataDir {
        DataDir { root: root.into() }
    }

    /// `<gamedir>/tsmod/rcbot`, from AMX Mod X's `BuildPathname` (the mod directory as the
    /// server sees it). Call after `AMXX_Attach`.
    pub fn from_core() -> DataDir {
        DataDir::new(amxx::core::build_pathname(crate::DATA_DIR))
    }

    /// The directory.
    pub fn root(&self) -> &Path {
        &self.root
    }

    /// `rel` below the directory, or `None` if it would leave it: an absolute path, a drive, or
    /// a `..` component. Plugins name files, so they stay inside the data directory.
    pub fn path(&self, rel: &str) -> Option<PathBuf> {
        let rel = rel.replace('\\', "/");
        let p = Path::new(&rel);
        if rel.is_empty() || p.components().any(|c| !matches!(c, Component::Normal(_) | Component::CurDir)) {
            return None;
        }
        Some(self.root.join(p))
    }

    /// The file's contents, `None` if it is missing, unreadable or outside the directory.
    pub fn read(&self, rel: &str) -> Option<Vec<u8>> {
        std::fs::read(self.path(rel)?).ok()
    }

    /// Writes a file atomically: a temporary file beside it, renamed over it, so a crash or a
    /// full disk mid-save leaves the last good file (as tsmod-rcbot-rs's host does). Creates the
    /// folders on the way. `Err` says why it failed.
    pub fn write(&self, rel: &str, bytes: &[u8]) -> Result<(), String> {
        let p = self.path(rel).ok_or_else(|| format!("{rel}: not a path inside the data directory"))?;
        if let Some(d) = p.parent() {
            let _ = std::fs::create_dir_all(d);
        }
        let mut tmp = p.clone().into_os_string();
        tmp.push(".tmp");
        let tmp = PathBuf::from(tmp);
        let r = std::fs::write(&tmp, bytes).and_then(|()| std::fs::rename(&tmp, &p));
        r.map_err(|e| {
            let _ = std::fs::remove_file(&tmp);
            format!("can't write {}: {e}", p.display())
        })
    }

    /// [`write`](Self::write), logging a failure; returns whether it worked.
    pub fn write_logged(&self, rel: &str, bytes: &[u8]) -> bool {
        match self.write(rel, bytes) {
            Ok(()) => true,
            Err(e) => {
                crate::log(&e);
                false
            }
        }
    }
}

/// A fresh, empty directory for a test (under the system's temp directory).
pub fn temp_dir(name: &str) -> PathBuf {
    let d = std::env::temp_dir().join(format!("rcbot-amxx-{name}-{}-{:?}", std::process::id(), std::thread::current().id()));
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn paths_stay_inside() {
        let d = DataDir::new("/data");
        assert_eq!(d.path("botprofiles/1.rld"), Some(PathBuf::from("/data/botprofiles/1.rld")));
        assert_eq!(d.path("botprofiles\\1.rld"), Some(PathBuf::from("/data/botprofiles/1.rld")));
        assert_eq!(d.path("./team0.rld"), Some(PathBuf::from("/data/team0.rld")));
        for bad in ["", "../x", "a/../../x", "/etc/passwd", "\\\\server\\x"] {
            assert_eq!(d.path(bad), None, "{bad}");
        }
    }

    #[test]
    fn writes_are_atomic_and_make_folders() {
        let d = DataDir::new(temp_dir("write"));
        d.write("botprofiles/x.bin", b"one").unwrap();
        d.write("botprofiles/x.bin", b"two").unwrap();
        assert_eq!(d.read("botprofiles/x.bin").as_deref(), Some(&b"two"[..]));
        assert!(!d.root().join("botprofiles/x.bin.tmp").exists());
        assert!(d.write("../escape", b"x").is_err());
        assert_eq!(d.read("missing"), None);
    }
}
