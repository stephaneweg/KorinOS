//! Paths shared by every area — spec 03 §0, 04 §1.4. Owner: core-store-sessions.
//! KEEP the C# locations so existing users keep their state: app data = `dirs::data_local_dir()/ClaudeCodeUI`
//! (`%LOCALAPPDATA%`, `~/Library/Application Support`, `$XDG_DATA_HOME` or `~/.local/share`) — never Tauri's
//! bundle-id `app_data_dir()`.
//!
//! Korin (src/apps/claudecode) : les données de l'app vont dans `~/.config/claudecode` (plus bas, `app_data`).

use std::path::PathBuf;

#[cfg(test)]
thread_local! {
    /// Tests redirect app data / config dir per thread (the C# Check() swapped static paths the same way).
    static TEST_DIRS: std::cell::RefCell<Option<(PathBuf, PathBuf)>> = const { std::cell::RefCell::new(None) };
}

/// Test only: app data → `root/app`, config dir → `root/config` for this thread (None restores).
#[cfg(test)]
pub(crate) fn set_test_root(root: Option<&std::path::Path>) {
    TEST_DIRS.with(|d| *d.borrow_mut() = root.map(|r| (r.join("app"), r.join("config"))));
}

#[cfg(test)]
fn test_dir(config: bool) -> Option<PathBuf> {
    TEST_DIRS.with(|d| d.borrow().as_ref().map(|(a, c)| if config { c.clone() } else { a.clone() }))
}
#[cfg(not(test))]
fn test_dir(_config: bool) -> Option<PathBuf> {
    None
}

/// `$CLAUDE_CONFIG_DIR` if set and non-empty, else `~/.claude` (re-evaluated on each call).
pub fn config_dir() -> PathBuf {
    if let Some(d) = test_dir(true) {
        return d;
    }
    std::env::var_os("CLAUDE_CONFIG_DIR").filter(|d| !d.is_empty()).map(PathBuf::from).unwrap_or_else(|| home().join(".claude"))
}

/// `config_dir()/projects` (`TranscriptStore.Root`).
pub fn projects_root() -> PathBuf {
    config_dir().join("projects")
}

/// Korin : `$CLAUDECODE_DATA`, sinon `~/.config/claudecode` (le dossier de l'app Claude Code de Korin, celui de son
/// `organize.json`), partout, l'hôte de développement Linux compris ; l'original prenait
/// `dirs::data_local_dir()/ClaudeCodeUI` (`AppFlag` / ledgers / repos.json / bookmarks / limits).
pub fn app_data() -> PathBuf {
    if let Some(d) = test_dir(false) {
        return d;
    }
    if let Some(d) = std::env::var_os("CLAUDECODE_DATA").filter(|d| !d.is_empty()) {
        return PathBuf::from(d);
    }
    home().join(".config").join("claudecode")
}

/// User home (Windows: USERPROFILE). Korin : `$HOME`, sinon `/home` (le dossier de l'utilisateur tant qu'il n'y a pas
/// de comptes), sans passer par `getpwuid`.
pub fn home() -> PathBuf {
    #[cfg(target_os = "korin")]
    {
        return std::env::var_os("HOME").filter(|h| !h.is_empty()).map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/home"));
    }
    #[allow(unreachable_code)]
    dirs::home_dir().unwrap_or_default()
}

/// `~/.claude/jobs` — NB the C# used the profile folder, ignoring CLAUDE_CONFIG_DIR (kept for parity).
pub fn jobs_dir() -> PathBuf {
    home().join(".claude").join("jobs")
}

/// `~/Downloads` if it exists, else home (export).
pub fn downloads() -> PathBuf {
    let d = home().join("Downloads");
    if d.is_dir() {
        d
    } else {
        home()
    }
}
