// Les conversations précédentes du copilote : celles que Claude Code garde lui-même, une par fichier
// (~/.claude/projects/<dossier du projet, chaque signe non alphanumérique devenu « - »>/<séance>.jsonl, une ligne JSON
// par évènement). On en fait la liste (date, premier message), on rejoue une conversation dans le panneau, et Claude
// Code la reprend (--resume) avec tout son contexte.

use std::path::{Path, PathBuf};
use std::time::SystemTime;

pub struct PastSession {
    pub id: String,
    pub title: String,
    pub when: SystemTime,
    pub path: PathBuf,
    pub messages: usize,
}

/// Un message rejoué : (rôle, texte) ; rôle « user », « assistant » ou « tool » (le titre d'un outil).
pub type Replayed = Vec<(String, String)>;

fn projects_dir() -> PathBuf {
    crate::workspace::home_dir().join(".claude").join("projects")
}

/// Le nom du dossier que Claude Code donne à un dossier de travail.
fn escaped(cwd: &Path) -> String {
    cwd.to_string_lossy().chars().map(|c| if c.is_ascii_alphanumeric() { c } else { '-' }).collect()
}

/// Le texte d'un message : une chaîne, ou les blocs « text » d'un tableau (sans les résultats d'outils).
fn text_of(content: &serde_json::Value) -> Option<String> {
    match content {
        serde_json::Value::String(s) => Some(s.clone()),
        serde_json::Value::Array(blocks) => {
            if blocks.iter().any(|b| b.get("type").and_then(|t| t.as_str()) == Some("tool_result")) {
                return None;
            }
            let t: Vec<&str> = blocks.iter().filter(|b| b.get("type").and_then(|t| t.as_str()) == Some("text")).filter_map(|b| b.get("text").and_then(|t| t.as_str())).collect();
            if t.is_empty() { None } else { Some(t.join("\n")) }
        }
        _ => None,
    }
}

/// Le message tel que l'utilisateur l'a tapé : sans le contexte que KorinStudio y joint (app.rs, context_text).
pub fn strip_context(t: &str) -> String {
    let t = t.trim();
    if let Some(rest) = t.strip_prefix("[Contexte de l'IDE]") {
        if let Some(i) = rest.find("```\n\n") {
            return rest[i + 5..].trim().to_string();
        }
        if let Some(i) = rest.find("\n\n") {
            return rest[i + 2..].trim().to_string();
        }
    }
    t.to_string()
}

/// Un message de l'utilisateur à montrer (pas une sortie de commande ni un message interne de Claude Code).
fn shown_user(t: &str) -> bool {
    let t = t.trim_start();
    !t.is_empty() && !t.starts_with('<') && !t.starts_with("Caveat:")
}

fn first_user_message(path: &Path) -> Option<(String, usize, Option<String>)> {
    let data = std::fs::read_to_string(path).ok()?;
    let mut title = None;
    let mut count = 0;
    let mut cwd = None;
    for line in data.lines() {
        let Ok(v) = serde_json::from_str::<serde_json::Value>(line) else { continue };
        if cwd.is_none() {
            cwd = v.get("cwd").and_then(|c| c.as_str()).map(|s| s.to_string());
        }
        let kind = v.get("type").and_then(|t| t.as_str()).unwrap_or("");
        if kind == "user" || kind == "assistant" {
            if let Some(t) = v.get("message").and_then(|m| m.get("content")).and_then(text_of) {
                if kind == "user" && shown_user(&t) {
                    count += 1;
                    if title.is_none() {
                        title = Some(strip_context(&t));
                    }
                } else if kind == "assistant" {
                    count += 1;
                }
            }
        }
    }
    Some((title?, count, cwd))
}

/// Les conversations de Claude Code pour ce dossier de travail, les plus récentes d'abord.
pub fn list(cwd: &Path) -> Vec<PastSession> {
    let root = projects_dir();
    let mut files: Vec<PathBuf> = Vec::new();
    let dir = root.join(escaped(cwd));
    let dirs: Vec<PathBuf> = if dir.is_dir() {
        vec![dir]
    } else {
        // (un autre nommage : on cherche le dossier dont les séances ont ce cwd)
        std::fs::read_dir(&root).map(|rd| rd.flatten().map(|e| e.path()).filter(|p| p.is_dir()).collect()).unwrap_or_default()
    };
    for d in dirs {
        if let Ok(rd) = std::fs::read_dir(&d) {
            files.extend(rd.flatten().map(|e| e.path()).filter(|p| p.extension().is_some_and(|x| x == "jsonl")));
        }
    }
    let want = cwd.to_string_lossy().to_string();
    let mut out: Vec<PastSession> = files
        .into_iter()
        .filter_map(|p| {
            let when = std::fs::metadata(&p).and_then(|m| m.modified()).ok()?;
            let (title, messages, scwd) = first_user_message(&p)?;
            if scwd.as_deref().is_some_and(|c| c != want) {
                return None;
            }
            let id = p.file_stem()?.to_string_lossy().to_string();
            Some(PastSession { id, title, when, path: p, messages })
        })
        .collect();
    out.sort_by(|a, b| b.when.cmp(&a.when));
    out.truncate(40);
    out
}

/// La conversation d'une séance : les messages de l'utilisateur, les textes de l'agent, les outils qu'il a lancés.
pub fn replay(path: &Path) -> Replayed {
    let Ok(data) = std::fs::read_to_string(path) else { return Vec::new() };
    let mut out = Vec::new();
    for line in data.lines() {
        let Ok(v) = serde_json::from_str::<serde_json::Value>(line) else { continue };
        let kind = v.get("type").and_then(|t| t.as_str()).unwrap_or("");
        let Some(content) = v.get("message").and_then(|m| m.get("content")) else { continue };
        match kind {
            "user" => {
                if let Some(t) = text_of(content) {
                    if shown_user(&t) {
                        out.push(("user".to_string(), strip_context(&t)));
                    }
                }
            }
            "assistant" => {
                if let serde_json::Value::Array(blocks) = content {
                    for b in blocks {
                        match b.get("type").and_then(|t| t.as_str()) {
                            Some("text") => {
                                if let Some(t) = b.get("text").and_then(|t| t.as_str()) {
                                    // les morceaux d'un même message de l'agent se suivent : un seul bloc
                                    if let Some((r, prev)) = out.last_mut() {
                                        if r == "assistant" {
                                            prev.push_str(t);
                                            continue;
                                        }
                                    }
                                    out.push(("assistant".to_string(), t.to_string()));
                                }
                            }
                            Some("tool_use") => {
                                let name = b.get("name").and_then(|n| n.as_str()).unwrap_or("outil");
                                let name = name.strip_prefix("mcp__korinstudio__").map(|n| format!("IDE · {n}")).unwrap_or_else(|| name.to_string());
                                out.push(("tool".to_string(), name));
                            }
                            _ => {}
                        }
                    }
                }
            }
            _ => {}
        }
    }
    out
}

/// « il y a 5 min », « hier », « 21/11 »…
pub fn ago(when: SystemTime) -> String {
    let s = SystemTime::now().duration_since(when).map(|d| d.as_secs()).unwrap_or(0);
    match s {
        0..=59 => "à l'instant".into(),
        60..=3599 => format!("il y a {} min", s / 60),
        3600..=86_399 => format!("il y a {} h", s / 3600),
        86_400..=172_799 => "hier".into(),
        _ => format!("il y a {} jours", s / 86_400),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn context_is_stripped() {
        assert_eq!(strip_context("[Contexte de l'IDE] Fichier actif : a.rs (ligne 1, colonne 1)\n\nBonjour"), "Bonjour");
        assert_eq!(strip_context("[Contexte de l'IDE] Fichier actif : a.rs\nSélection :\n```\nx\n\ny\n```\n\nExplique"), "Explique");
        assert_eq!(strip_context("Salut"), "Salut");
        assert_eq!(escaped(Path::new("/tmp/claude-0/kshome/Projets/demo")), "-tmp-claude-0-kshome-Projets-demo");
    }
}
