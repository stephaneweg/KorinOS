//! Les conversations de Claude Code sur le disque, telles que la CLI officielle les garde : un fichier JSONL par
//! session, `~/.claude/projects/<dossier de travail, en tirets>/<id>.jsonl` (`CLAUDE_CONFIG_DIR` à la place de
//! `~/.claude`). Une ligne par évènement : les messages (`user`, `assistant`), et des lignes de métadonnées dont la
//! dernière l'emporte : `custom-title` (le titre donné par `/rename`, et ici), `ai-title`, `summary`.
//!
//! Renommer ajoute une ligne `custom-title`, comme `/rename` de la CLI ; supprimer retire le fichier (et le dossier
//! `<id>/` de ses sous-agents). Rien d'autre n'est écrit dans les fichiers de la CLI.

use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use serde_json::Value;

/// Ce que la liste des conversations montre d'une session.
#[derive(Clone, Debug)]
pub struct SessionInfo {
    pub id: String,
    pub path: PathBuf,
    /// Le dossier de travail de la session (`cwd` de ses messages).
    pub cwd: String,
    /// Le titre donné (`custom-title`), sinon celui que Claude a proposé (`ai-title`), sinon le résumé, sinon le
    /// début de la première demande.
    pub title: String,
    #[allow(dead_code)]
    pub custom_title: bool,
    pub modified: SystemTime,
    /// Les messages de l'utilisateur et de Claude (sans les métadonnées).
    pub messages: usize,
}

/// `~/.claude`, ou `CLAUDE_CONFIG_DIR`.
pub fn claude_dir() -> PathBuf {
    if let Some(d) = std::env::var_os("CLAUDE_CONFIG_DIR").filter(|d| !d.is_empty()) {
        return PathBuf::from(d);
    }
    home().join(".claude")
}

pub fn home() -> PathBuf {
    std::env::var_os("HOME").filter(|h| !h.is_empty()).map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/home"))
}

/// Toutes les sessions, la plus récente d'abord.
pub fn list_sessions() -> Vec<SessionInfo> {
    let mut out = Vec::new();
    let Ok(projects) = fs::read_dir(claude_dir().join("projects")) else { return out };
    for p in projects.flatten() {
        let Ok(files) = fs::read_dir(p.path()) else { continue };
        for f in files.flatten() {
            let path = f.path();
            if path.extension().and_then(|e| e.to_str()) != Some("jsonl") {
                continue;
            }
            if let Some(info) = read_info(&path) {
                out.push(info);
            }
        }
    }
    out.sort_by(|a, b| b.modified.cmp(&a.modified));
    out
}

/// La fiche d'une session ; None pour un fichier sans message (un résumé seul, une session vide).
pub fn read_info(path: &Path) -> Option<SessionInfo> {
    let text = fs::read_to_string(path).ok()?;
    let modified = fs::metadata(path).and_then(|m| m.modified()).unwrap_or(SystemTime::UNIX_EPOCH);
    let id = path.file_stem()?.to_string_lossy().into_owned();
    let (mut custom, mut ai, mut summary, mut first, mut cwd) = (None, None, None, None, String::new());
    let mut messages = 0;
    for line in text.lines() {
        // (un tri grossier d'abord : la plupart des lignes ne demandent pas d'être lues en entier)
        // (la CLI écrit du JSON compact ; un fichier refait à la main peut avoir des espaces : on ne compte pas dessus)
        let is_meta = line.contains("\"custom-title\"") || line.contains("\"ai-title\"") || line.contains("\"summary\"");
        let is_msg = line.contains("\"user\"") || line.contains("\"assistant\"");
        if !is_meta && !is_msg {
            continue;
        }
        let Ok(v) = serde_json::from_str::<Value>(line) else { continue };
        match v.get("type").and_then(Value::as_str) {
            Some("custom-title") => custom = v.get("customTitle").and_then(Value::as_str).map(str::to_owned),
            Some("ai-title") => ai = v.get("aiTitle").and_then(Value::as_str).map(str::to_owned),
            Some("summary") => summary = v.get("summary").and_then(Value::as_str).map(str::to_owned),
            Some(t @ ("user" | "assistant")) => {
                if v.get("isSidechain").and_then(Value::as_bool) == Some(true) {
                    continue;
                }
                if cwd.is_empty()
                    && let Some(c) = v.get("cwd").and_then(Value::as_str)
                {
                    cwd = c.to_owned();
                }
                if t == "user" && is_tool_result(&v) {
                    continue;
                }
                messages += 1;
                if t == "user" && first.is_none() && v.get("isMeta").and_then(Value::as_bool) != Some(true) {
                    first = user_text(&v).filter(|s| !s.trim().is_empty() && !s.starts_with('<'));
                }
            }
            _ => {}
        }
    }
    if messages == 0 {
        return None;
    }
    let custom_title = custom.as_deref().is_some_and(|t| !t.trim().is_empty());
    let title = custom
        .filter(|t| !t.trim().is_empty())
        .or(ai)
        .or(summary)
        .or_else(|| first.map(|f| one_line(&f, 80)))
        .unwrap_or_else(|| "Conversation sans titre".to_owned());
    Some(SessionInfo { id, path: path.to_owned(), cwd, title, custom_title, modified, messages })
}

/// Le texte d'un message de l'utilisateur (une chaîne, ou ses blocs de texte).
pub fn user_text(v: &Value) -> Option<String> {
    let c = v.get("message")?.get("content")?;
    if let Some(s) = c.as_str() {
        return Some(s.to_owned());
    }
    let mut out = String::new();
    for b in c.as_array()? {
        if b.get("type").and_then(Value::as_str) == Some("text")
            && let Some(t) = b.get("text").and_then(Value::as_str)
        {
            if !out.is_empty() {
                out.push('\n');
            }
            out.push_str(t);
        }
    }
    (!out.is_empty()).then_some(out)
}

/// Un message « user » qui ne porte que des résultats d'outils (la réponse de la CLI à un appel d'outil).
pub fn is_tool_result(v: &Value) -> bool {
    v.get("message")
        .and_then(|m| m.get("content"))
        .and_then(Value::as_array)
        .is_some_and(|a| !a.is_empty() && a.iter().all(|b| b.get("type").and_then(Value::as_str) == Some("tool_result")))
}

/// Une ligne, au plus `max` caractères.
pub fn one_line(s: &str, max: usize) -> String {
    let flat: String = s.split_whitespace().collect::<Vec<_>>().join(" ");
    if flat.chars().count() <= max {
        flat
    } else {
        let cut: String = flat.chars().take(max.saturating_sub(1)).collect();
        format!("{cut}…")
    }
}

/// Les lignes d'une session (les évènements JSON), pour le chat.
pub fn load_entries(path: &Path) -> Vec<Value> {
    let Ok(text) = fs::read_to_string(path) else { return Vec::new() };
    text.lines().filter_map(|l| serde_json::from_str::<Value>(l).ok()).collect()
}

/// Renomme une session comme `/rename` : une ligne `custom-title` ajoutée à la fin (la dernière l'emporte).
pub fn rename(info: &SessionInfo, title: &str) -> io::Result<()> {
    let line = serde_json::json!({ "type": "custom-title", "customTitle": title.trim(), "sessionId": info.id });
    let mut f = fs::OpenOptions::new().append(true).open(&info.path)?;
    // (une ligne entière : le fichier finit par un saut de ligne, sinon on en met un)
    let ends_nl = fs::read(&info.path).map(|b| b.last().is_none_or(|&c| c == b'\n')).unwrap_or(true);
    let text = format!("{}{}\n", if ends_nl { "" } else { "\n" }, line);
    f.write_all(text.as_bytes())
}

/// Supprime une session : son fichier, et le dossier de ses sous-agents (`<id>/` à côté).
pub fn delete(info: &SessionInfo) -> io::Result<()> {
    fs::remove_file(&info.path)?;
    let side = info.path.with_extension("");
    if side.is_dir() {
        let _ = fs::remove_dir_all(side);
    }
    Ok(())
}

/// Le nom court d'un dossier de travail (le dernier élément).
pub fn short_dir(cwd: &str) -> String {
    let t = cwd.trim_end_matches('/');
    if t.is_empty() {
        return "/".to_owned();
    }
    t.rsplit('/').next().unwrap_or(t).to_owned()
}

/// « à l'instant », « il y a 5 min », « hier »…
pub fn ago(t: SystemTime) -> String {
    let s = SystemTime::now().duration_since(t).map(|d| d.as_secs()).unwrap_or(0);
    match s {
        0..=59 => "à l'instant".to_owned(),
        60..=3599 => format!("il y a {} min", s / 60),
        3600..=86_399 => format!("il y a {} h", s / 3600),
        86_400..=172_799 => "hier".to_owned(),
        _ => format!("il y a {} j", s / 86_400),
    }
}

/// Le groupe de date d'une conversation dans la liste.
pub fn age_group(t: SystemTime) -> &'static str {
    let s = SystemTime::now().duration_since(t).map(|d| d.as_secs()).unwrap_or(0);
    match s {
        0..=86_399 => "Aujourd'hui",
        86_400..=172_799 => "Hier",
        172_800..=604_799 => "Cette semaine",
        604_800..=2_678_399 => "Ce mois-ci",
        _ => "Plus ancien",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str, text: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("claudecode-test-{}-{name}", std::process::id()));
        fs::create_dir_all(&d).unwrap();
        let p = d.join(format!("{name}.jsonl"));
        fs::write(&p, text).unwrap();
        p
    }

    #[test]
    fn titles_and_rename() {
        let p = tmp(
            "s1",
            concat!(
                r#"{"type":"user","message":{"role":"user","content":"Écris un programme qui dit bonjour"},"cwd":"/home/a/projet","isSidechain":false}"#,
                "\n",
                r#"{"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"Voilà."}]},"cwd":"/home/a/projet"}"#,
                "\n",
                r#"{"type":"user","message":{"role":"user","content":[{"type":"tool_result","tool_use_id":"x","content":"ok"}]}}"#,
                "\n",
                r#"{"type":"ai-title","aiTitle":"Programme bonjour","sessionId":"s1"}"#,
                "\n"
            ),
        );
        let i = read_info(&p).unwrap();
        assert_eq!(i.title, "Programme bonjour");
        assert_eq!(i.cwd, "/home/a/projet");
        assert_eq!(i.messages, 2);
        assert!(!i.custom_title);
        rename(&i, "  Mon titre ").unwrap();
        let j = read_info(&p).unwrap();
        assert_eq!(j.title, "Mon titre");
        assert!(j.custom_title);
        delete(&j).unwrap();
        assert!(!p.exists());
    }

    #[test]
    fn first_prompt_when_no_title() {
        let p = tmp("s2", "{\"type\": \"user\",\"message\":{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"Bonjour\\n  le monde\"}]}}");
        assert_eq!(read_info(&p).unwrap().title, "Bonjour le monde");
        let e = tmp("s3", "{\"type\":\"summary\",\"summary\":\"rien\"}\n");
        assert!(read_info(&e).is_none());
    }
}
