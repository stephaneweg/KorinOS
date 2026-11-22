//! Le volet « Environnement » : le dossier de la conversation et son dépôt git (branche, dépôt distant, avance et
//! retard, fichiers changés, dernier commit), l'utilisation de l'abonnement (fenêtre de 5 h, semaine : la réponse de
//! `get_usage` de la CLI), et les sessions de Claude Code sur le web (`list_cloud_sessions`).
//!
//! git tourne dans un fil (une commande prend quelques dizaines de ms sous Korin) : l'app demande (`Git::want`) et
//! relève (`Git::poll`) sans attendre.

use std::collections::HashMap;
use std::path::Path;
use std::process::Command;
use std::sync::mpsc::{Receiver, Sender, channel};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde_json::Value;

/// Ce que git dit d'un dossier.
#[derive(Clone, Debug, Default)]
pub struct GitInfo {
    /// Pas un dépôt (ou pas de git) : le reste est vide.
    pub repo: bool,
    pub root: String,
    pub branch: String,
    /// `origin` : son adresse, et « propriétaire/nom » quand elle est de GitHub ou d'un autre hôte.
    pub remote: String,
    pub remote_name: String,
    /// La branche suivie (`origin/main`), et l'avance et le retard sur elle.
    pub upstream: String,
    pub ahead: u32,
    pub behind: u32,
    /// Les fichiers changés : (état sur deux lettres de `git status --porcelain`, chemin).
    pub changed: Vec<(String, String)>,
    pub last_commit: String,
    pub error: Option<String>,
}

pub struct Git {
    cache: HashMap<String, (GitInfo, Instant)>,
    asked: HashMap<String, Instant>,
    tx: Sender<(String, GitInfo)>,
    rx: Receiver<(String, GitInfo)>,
}

impl Default for Git {
    fn default() -> Self {
        let (tx, rx) = channel();
        Git { cache: HashMap::new(), asked: HashMap::new(), tx, rx }
    }
}

impl Git {
    /// Ce qu'on sait du dossier (None : pas encore lu).
    pub fn get(&self, dir: &str) -> Option<&GitInfo> {
        self.cache.get(dir).map(|(g, _)| g)
    }

    /// Relit le dossier s'il n'a pas été lu depuis `max_age` (une lecture à la fois par dossier).
    pub fn want(&mut self, dir: &str, max_age: Duration) {
        if self.cache.get(dir).is_some_and(|(_, t)| t.elapsed() < max_age) {
            return;
        }
        if self.asked.get(dir).is_some_and(|t| t.elapsed() < Duration::from_secs(10)) {
            return;
        }
        self.asked.insert(dir.to_owned(), Instant::now());
        let tx = self.tx.clone();
        let d = dir.to_owned();
        std::thread::spawn(move || {
            let g = read(&d);
            let _ = tx.send((d, g));
        });
    }

    /// Oublie la lecture d'un dossier : la prochaine `want` le relit (après un tour de Claude).
    pub fn stale(&mut self, dir: &str) {
        if let Some((_, t)) = self.cache.get_mut(dir) {
            *t = Instant::now() - Duration::from_secs(3600);
        }
    }

    /// Les lectures finies -> true s'il y en a eu.
    pub fn poll(&mut self) -> bool {
        let mut any = false;
        for (d, g) in self.rx.try_iter() {
            self.asked.remove(&d);
            self.cache.insert(d, (g, Instant::now()));
            any = true;
        }
        any
    }
}

fn git(dir: &str, args: &[&str]) -> Option<String> {
    let out = Command::new("git").args(args).current_dir(dir).output().ok()?;
    if !out.status.success() {
        return None;
    }
    Some(String::from_utf8_lossy(&out.stdout).trim_end().to_owned())
}

fn read(dir: &str) -> GitInfo {
    if !Path::new(dir).is_dir() {
        return GitInfo { error: Some("Ce dossier n'existe pas.".into()), ..Default::default() };
    }
    let Some(root) = git(dir, &["rev-parse", "--show-toplevel"]) else {
        return GitInfo::default();
    };
    let mut g = GitInfo { repo: true, root, ..Default::default() };
    if let Some(st) = git(dir, &["status", "--porcelain=v1", "--branch"]) {
        parse_status(&st, &mut g);
    }
    if g.branch.is_empty() {
        g.branch = git(dir, &["rev-parse", "--abbrev-ref", "HEAD"]).unwrap_or_default();
    }
    if let Some(url) = git(dir, &["remote", "get-url", "origin"]) {
        g.remote_name = repo_name(&url);
        g.remote = url;
    }
    g.last_commit = git(dir, &["log", "-1", "--format=%h %s (%cr)"]).unwrap_or_default();
    g
}

/// `git status --porcelain=v1 --branch` : la première ligne (`## main...origin/main [ahead 1, behind 2]`), puis un
/// fichier par ligne.
pub fn parse_status(st: &str, g: &mut GitInfo) {
    for (i, line) in st.lines().enumerate() {
        if i == 0
            && let Some(b) = line.strip_prefix("## ")
        {
            let (head, rest) = b.split_once(' ').unwrap_or((b, ""));
            let (branch, up) = head.split_once("...").unwrap_or((head, ""));
            g.branch = branch.strip_prefix("No commits yet on ").unwrap_or(branch).to_owned();
            g.upstream = up.to_owned();
            for part in rest.trim_matches(|c| c == '[' || c == ']').split(", ") {
                if let Some(n) = part.strip_prefix("ahead ") {
                    g.ahead = n.parse().unwrap_or(0);
                } else if let Some(n) = part.strip_prefix("behind ") {
                    g.behind = n.parse().unwrap_or(0);
                }
            }
            continue;
        }
        if line.len() > 3 {
            g.changed.push((line[..2].to_owned(), line[3..].to_owned()));
        }
    }
}

/// L'adresse d'un dépôt -> « propriétaire/nom » (git@github.com:a/b.git, https://github.com/a/b) ; sinon elle-même.
pub fn repo_name(url: &str) -> String {
    let u = url.trim().trim_end_matches('/').trim_end_matches(".git");
    let path = if let Some((_, p)) = u.split_once("://") {
        p.split_once('/').map(|(_, p)| p).unwrap_or(p)
    } else if let Some((_, p)) = u.split_once(':') {
        p
    } else {
        u
    };
    let parts: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();
    if parts.len() >= 2 { format!("{}/{}", parts[parts.len() - 2], parts[parts.len() - 1]) } else { u.to_owned() }
}

/// Une fenêtre de l'abonnement : la part utilisée (0 à 100) et sa remise à zéro (secondes UNIX).
#[derive(Clone, Debug, PartialEq)]
pub struct Window {
    pub name: String,
    pub pct: f64,
    pub resets: Option<i64>,
}

/// La réponse de `get_usage`.
#[derive(Clone, Debug, Default)]
pub struct Usage {
    pub plan: String,
    /// False : pas de limites d'abonnement (clé d'API…).
    pub available: bool,
    pub windows: Vec<Window>,
}

/// `get_usage` -> Usage (la réponse de la CLI : `response.response`).
pub fn parse_usage(r: &Value) -> Usage {
    let mut u = Usage {
        plan: r.get("subscription_type").and_then(Value::as_str).unwrap_or("").to_owned(),
        available: r.get("rate_limits_available").and_then(Value::as_bool).unwrap_or(false),
        windows: Vec::new(),
    };
    let rl = r.get("rate_limits").unwrap_or(&Value::Null);
    for (k, name) in [
        ("five_hour", "Session (5 h)"),
        ("seven_day", "Semaine"),
        ("seven_day_opus", "Semaine (Opus)"),
        ("seven_day_sonnet", "Semaine (Sonnet)"),
    ] {
        if let Some(w) = rl.get(k)
            && let Some(p) = w.get("utilization").and_then(Value::as_f64)
        {
            u.windows.push(Window { name: name.into(), pct: p, resets: w.get("resets_at").and_then(Value::as_str).and_then(iso_secs) });
        }
    }
    if let Some(arr) = rl.get("model_scoped").and_then(Value::as_array) {
        for m in arr {
            if let Some(p) = m.get("utilization").and_then(Value::as_f64) {
                let n = m.get("display_name").and_then(Value::as_str).unwrap_or("modèle");
                u.windows.push(Window {
                    name: format!("Semaine ({n})"),
                    pct: p,
                    resets: m.get("resets_at").and_then(Value::as_str).and_then(iso_secs),
                });
            }
        }
    }
    u
}

/// « 2026-11-22T18:40:00Z », « …+01:00 », « ….123Z » -> secondes UNIX.
pub fn iso_secs(s: &str) -> Option<i64> {
    let b = s.as_bytes();
    if b.len() < 19 {
        return None;
    }
    let num = |a: usize, n: usize| -> Option<i64> { s.get(a..a + n)?.parse().ok() };
    let (y, mo, d, h, mi, se) = (num(0, 4)?, num(5, 2)?, num(8, 2)?, num(11, 2)?, num(14, 2)?, num(17, 2)?);
    // les jours depuis 1970 (algorithme des jours civils)
    let (y2, m2) = if mo <= 2 { (y - 1, mo + 9) } else { (y, mo - 3) };
    let era = y2.div_euclid(400);
    let yoe = y2 - era * 400;
    let doy = (153 * m2 + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    let days = era * 146097 + doe - 719468;
    let mut t = days * 86400 + h * 3600 + mi * 60 + se;
    // le décalage : « Z », ou « ±HH:MM » après les secondes (et leurs décimales)
    let rest = &s[19..];
    let rest = rest.trim_start_matches(|c: char| c == '.' || c.is_ascii_digit());
    if let Some(sign) = rest.chars().next()
        && (sign == '+' || sign == '-')
        && rest.len() >= 6
    {
        let oh: i64 = rest[1..3].parse().ok()?;
        let om: i64 = rest[4..6].parse().ok()?;
        let off = oh * 3600 + om * 60;
        t -= if sign == '+' { off } else { -off };
    }
    Some(t)
}

pub fn now_secs() -> i64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs() as i64).unwrap_or(0)
}

/// Dans combien de temps : « dans 2 h 13 », « dans 3 j 4 h », « dans 12 min ».
pub fn until(secs: i64, now: i64) -> String {
    let d = (secs - now).max(0);
    if d >= 86400 {
        format!("dans {} j {} h", d / 86400, (d % 86400) / 3600)
    } else if d >= 3600 {
        format!("dans {} h {:02}", d / 3600, (d % 3600) / 60)
    } else {
        format!("dans {} min", (d + 59) / 60)
    }
}

/// Une session de Claude Code sur le web (`list_cloud_sessions`).
#[derive(Clone, Debug)]
pub struct CloudSession {
    pub id: String,
    pub title: String,
    pub updated: String,
    pub status: String,
}

/// La réponse de `list_cloud_sessions` -> les sessions, ou l'erreur que dit la CLI.
pub fn parse_cloud(r: &Value) -> Result<Vec<CloudSession>, String> {
    if r.get("status").and_then(Value::as_str) != Some("ok") {
        let why = r.get("message").or_else(|| r.get("error")).and_then(Value::as_str).unwrap_or("");
        let st = r.get("status").and_then(Value::as_str).unwrap_or("?");
        return Err(if why.is_empty() { st.to_owned() } else { format!("{st} : {why}") });
    }
    let mut v = Vec::new();
    for s in r.get("sessions").and_then(Value::as_array).into_iter().flatten() {
        let id = s.get("id").and_then(Value::as_str).unwrap_or("").to_owned();
        if id.is_empty() {
            continue;
        }
        v.push(CloudSession {
            title: s.get("title").and_then(Value::as_str).filter(|t| !t.is_empty()).unwrap_or("Session sans titre").to_owned(),
            updated: s.get("updated_at").and_then(Value::as_str).unwrap_or("").to_owned(),
            status: s.get("status").and_then(Value::as_str).unwrap_or("idle").to_owned(),
            id,
        });
    }
    Ok(v)
}

/// L'état d'une session du cloud, en français.
pub fn cloud_status(s: &str) -> &'static str {
    match s {
        "running" => "au travail",
        "requires_action" => "attend une réponse",
        "archived" => "archivée",
        "pending" => "en attente",
        "failed" => "en échec",
        _ => "au repos",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn status_and_repo() {
        let mut g = GitInfo::default();
        parse_status("## main...origin/main [ahead 2, behind 1]\n M src/a.rs\n?? b.txt", &mut g);
        assert_eq!((g.branch.as_str(), g.upstream.as_str(), g.ahead, g.behind), ("main", "origin/main", 2, 1));
        assert_eq!(g.changed, vec![(" M".to_owned(), "src/a.rs".to_owned()), ("??".to_owned(), "b.txt".to_owned())]);
        let mut h = GitInfo::default();
        parse_status("## feature", &mut h);
        assert_eq!((h.branch.as_str(), h.upstream.as_str()), ("feature", ""));
        assert_eq!(repo_name("git@github.com:stephaneweg/KorinOS.git"), "stephaneweg/KorinOS");
        assert_eq!(repo_name("https://github.com/stephaneweg/KorinOS"), "stephaneweg/KorinOS");
        assert_eq!(repo_name("http://127.0.0.1:1234/git/a/b/"), "a/b");
    }

    #[test]
    fn usage_and_dates() {
        assert_eq!(iso_secs("1970-01-01T00:00:00Z"), Some(0));
        assert_eq!(iso_secs("2026-11-22T18:40:00Z"), Some(1795372800));
        assert_eq!(iso_secs("2026-11-22T19:40:00.123+01:00"), Some(1795372800));
        let u = parse_usage(&json!({
            "subscription_type": "max", "rate_limits_available": true,
            "rate_limits": { "five_hour": { "utilization": 34.0, "resets_at": "2026-11-22T18:40:00Z" },
                             "seven_day": { "utilization": 12.5, "resets_at": null }, "seven_day_opus": null,
                             "model_scoped": [{ "display_name": "Fable", "utilization": 3.0, "resets_at": null }] }
        }));
        assert_eq!(u.plan, "max");
        assert_eq!(u.windows.len(), 3);
        assert_eq!(u.windows[0], Window { name: "Session (5 h)".into(), pct: 34.0, resets: Some(1795372800) });
        assert_eq!(u.windows[2].name, "Semaine (Fable)");
        assert_eq!(until(1000 + 2 * 3600 + 13 * 60, 1000), "dans 2 h 13");
        assert_eq!(until(1000 + 3 * 86400 + 4 * 3600, 1000), "dans 3 j 4 h");
    }

    #[test]
    fn cloud() {
        let ok = parse_cloud(&json!({ "status": "ok", "sessions": [{ "id": "session_1", "title": "", "updated_at": "x", "status": "running" }] })).unwrap();
        assert_eq!((ok[0].title.as_str(), cloud_status(&ok[0].status)), ("Session sans titre", "au travail"));
        assert!(parse_cloud(&json!({ "status": "not_signed_in" })).is_err());
    }
}
