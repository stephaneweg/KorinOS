//! La CLI officielle de Claude Code, pilotée comme le fait l'Agent SDK d'Anthropic : un process
//! `claude -p --input-format stream-json --output-format stream-json`, un message JSON par ligne dans les deux sens.
//!
//! - vers la CLI : `{"type":"user","message":{…}}` (une demande ; le process reste ouvert entre les tours),
//!   `control_request` `interrupt` (arrêter le tour), `get_usage` (les limites de l'abonnement), `list_cloud_sessions`
//!   et `get_cloud_session_transcript` (les sessions de Claude Code sur le web), `control_response` (la réponse à une
//!   demande d'autorisation) ;
//! - de la CLI : `system` `init` (l'identifiant de la session, le modèle), `stream_event` (le texte au fil de l'eau,
//!   `--include-partial-messages`), `assistant`, `user` (les résultats des outils), `result` (la fin du tour),
//!   `control_request` `can_use_tool` (`--permission-prompt-tool stdio` : la CLI demande s'il peut se servir d'un
//!   outil ; on répond `allow` ou `deny`).
//!
//! Sur Korin, `claude` est la commande du paquet claude-code (/bin/claude : l'hôte Linux et le binaire officiel).
//! Deux fils lisent la sortie et les erreurs du process ; l'app relève leurs lignes à chaque image (`poll`).

use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::mpsc::{Receiver, Sender, channel};

use serde_json::{Value, json};

/// Ce que lit l'app.
#[derive(Debug)]
pub enum Event {
    /// Une ligne JSON de la sortie de la CLI.
    Json(Value),
    /// Une ligne de sa sortie d'erreur, ou de sa sortie qui n'est pas du JSON.
    Text(String),
    /// La sortie est fermée : le process a fini (ou va finir).
    Closed,
}

/// Les réglages d'un lancement.
#[derive(Clone, Debug, Default)]
pub struct Launch {
    /// La session à reprendre (`--resume`) ; None : une nouvelle.
    pub resume: Option<String>,
    pub cwd: PathBuf,
    /// `--model` (vide : celui de la CLI).
    pub model: String,
    /// `--permission-mode` (vide : default).
    pub permission_mode: String,
}

/// La commande `claude` : celle du paquet, sinon celle du PATH (`CLAUDECODE_CLI` : une autre, pour les essais).
pub fn program() -> String {
    if let Ok(p) = std::env::var("CLAUDECODE_CLI")
        && !p.is_empty()
    {
        return p;
    }
    for p in ["/bin/claude", "/usr/local/bin/claude"] {
        if Path::new(p).exists() {
            return p.to_owned();
        }
    }
    "claude".to_owned()
}

/// Les arguments d'un lancement.
pub fn args(l: &Launch) -> Vec<String> {
    let mut a: Vec<String> = [
        "-p",
        "--input-format",
        "stream-json",
        "--output-format",
        "stream-json",
        "--verbose",
        "--include-partial-messages",
        "--permission-prompt-tool",
        "stdio",
        // (le mode « Tout autoriser » choisi en cours de conversation : Claude Code ne l'accepte que lancé ainsi ; le
        // mode de départ reste celui de --permission-mode)
        "--allow-dangerously-skip-permissions",
    ]
    .iter()
    .map(|s| s.to_string())
    .collect();
    if let Some(id) = &l.resume {
        a.push("--resume".into());
        a.push(id.clone());
    }
    if !l.model.is_empty() {
        a.push("--model".into());
        a.push(l.model.clone());
    }
    if !l.permission_mode.is_empty() && l.permission_mode != "default" {
        a.push("--permission-mode".into());
        a.push(l.permission_mode.clone());
    }
    a
}

/// Un process de la CLI qui tourne.
pub struct Cli {
    child: Child,
    stdin: Option<ChildStdin>,
    rx: Receiver<Event>,
    next_id: u64,
}

impl Cli {
    pub fn start(l: &Launch) -> std::io::Result<Cli> {
        let mut cmd = Command::new(program());
        cmd.args(args(l)).current_dir(&l.cwd).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped());
        // (pas de terminal : la CLI ne demande rien à l'écran ; la page de connexion s'ouvre dans Jet par BROWSER)
        cmd.env("CLAUDE_CODE_ENTRYPOINT", "sdk-korin");
        let mut child = cmd.spawn()?;
        let (tx, rx) = channel();
        if let Some(out) = child.stdout.take() {
            reader(BufReader::new(out), tx.clone(), true);
        }
        if let Some(err) = child.stderr.take() {
            reader(BufReader::new(err), tx, false);
        }
        let stdin = child.stdin.take();
        Ok(Cli { child, stdin, rx, next_id: 1 })
    }

    /// Les évènements arrivés depuis la dernière fois.
    pub fn poll(&mut self) -> Vec<Event> {
        self.rx.try_iter().collect()
    }

    /// Le process a fini : son code.
    pub fn exited(&mut self) -> Option<Option<i32>> {
        match self.child.try_wait() {
            Ok(Some(st)) => Some(st.code()),
            Ok(None) => None,
            Err(_) => Some(None),
        }
    }

    fn send(&mut self, v: &Value) -> bool {
        let Some(w) = self.stdin.as_mut() else { return false };
        let mut line = v.to_string();
        line.push('\n');
        w.write_all(line.as_bytes()).and_then(|_| w.flush()).is_ok()
    }

    /// Une demande de l'utilisateur.
    pub fn send_user(&mut self, text: &str) -> bool {
        self.send(&json!({
            "type": "user",
            "message": { "role": "user", "content": [{ "type": "text", "text": text }] },
            "parent_tool_use_id": null,
        }))
    }

    /// Une demande de contrôle à la CLI (`get_usage`, `list_cloud_sessions`, `get_cloud_session_transcript`…), sous le
    /// numéro `id` donné par l'app : la réponse (`control_response`) porte ce numéro.
    pub fn request(&mut self, id: &str, subtype: &str, extra: Value) -> bool {
        let mut r = json!({ "subtype": subtype });
        if let (Some(o), Some(e)) = (r.as_object_mut(), extra.as_object()) {
            for (k, v) in e {
                o.insert(k.clone(), v.clone());
            }
        }
        self.send(&json!({ "type": "control_request", "request_id": id, "request": r }))
    }

    /// Arrête le tour en cours (la CLI rend un `result` interrompu).
    pub fn interrupt(&mut self) -> bool {
        let id = format!("korin-{}", self.next_id);
        self.next_id += 1;
        self.send(&json!({ "type": "control_request", "request_id": id, "request": { "subtype": "interrupt" } }))
    }

    /// Change le mode d'autorisation de la conversation en cours (`default`, `acceptEdits`, `plan`, `bypassPermissions`).
    pub fn set_permission_mode(&mut self, mode: &str) -> bool {
        let id = format!("korin-{}", self.next_id);
        self.next_id += 1;
        let mode = if mode.is_empty() { "default" } else { mode };
        self.send(&json!({ "type": "control_request", "request_id": id, "request": { "subtype": "set_permission_mode", "mode": mode } }))
    }

    /// Change le modèle de la conversation en cours (vide : celui de la CLI).
    pub fn set_model(&mut self, model: &str) -> bool {
        let id = format!("korin-{}", self.next_id);
        self.next_id += 1;
        let model = if model.is_empty() { Value::Null } else { Value::String(model.to_owned()) };
        self.send(&json!({ "type": "control_request", "request_id": id, "request": { "subtype": "set_model", "model": model } }))
    }

    /// La réponse à un `can_use_tool` : autorisé (avec l'entrée de l'outil, et des règles à retenir), ou refusé.
    pub fn answer_permission(&mut self, request_id: &str, allow: bool, input: &Value, remember: Option<&Value>) -> bool {
        let decision = if allow {
            let mut d = json!({ "behavior": "allow", "updatedInput": input });
            if let Some(r) = remember {
                d["updatedPermissions"] = r.clone();
            }
            d
        } else {
            json!({ "behavior": "deny", "message": "Refusé par l'utilisateur dans Claude Code pour Korin." })
        };
        self.send(&json!({
            "type": "control_response",
            "response": { "subtype": "success", "request_id": request_id, "response": decision },
        }))
    }

    /// Une demande de la CLI que l'app ne sait pas servir : une erreur, pour qu'elle n'attende pas.
    pub fn refuse_request(&mut self, request_id: &str, what: &str) -> bool {
        self.send(&json!({
            "type": "control_response",
            "response": { "subtype": "error", "request_id": request_id, "error": format!("{what} : pas servi par Claude Code pour Korin") },
        }))
    }

    /// Ferme l'entrée (la CLI finit après son tour), puis le process s'il traîne.
    pub fn stop(&mut self) {
        self.stdin = None;
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

impl Drop for Cli {
    fn drop(&mut self) {
        self.stop();
    }
}

fn reader<R: BufRead + Send + 'static>(r: R, tx: Sender<Event>, json_out: bool) {
    std::thread::spawn(move || {
        for line in r.lines() {
            let Ok(line) = line else { break };
            if line.trim().is_empty() {
                continue;
            }
            let ev = if json_out {
                match serde_json::from_str::<Value>(&line) {
                    Ok(v) => Event::Json(v),
                    Err(_) => Event::Text(line),
                }
            } else {
                Event::Text(line)
            };
            if tx.send(ev).is_err() {
                return;
            }
        }
        if json_out {
            let _ = tx.send(Event::Closed);
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launch_args() {
        let a = args(&Launch { resume: Some("abc".into()), model: "opus".into(), permission_mode: "plan".into(), ..Default::default() });
        let s = a.join(" ");
        assert!(s.starts_with("-p --input-format stream-json --output-format stream-json"));
        assert!(s.contains("--permission-prompt-tool stdio --allow-dangerously-skip-permissions"));
        assert!(s.contains("--resume abc --model opus --permission-mode plan"));
        let b = args(&Launch { permission_mode: "default".into(), ..Default::default() }).join(" ");
        assert!(!b.contains("--permission-mode") && !b.contains("--resume"));
    }

    /// Le protocole contre une fausse CLI (un script qui répète ce qu'on lui envoie, comme le ferait la vraie).
    #[cfg(unix)]
    #[test]
    fn fake_cli_roundtrip() {
        let dir = std::env::temp_dir().join(format!("claudecode-cli-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let script = dir.join("fake-claude");
        std::fs::write(
            &script,
            "#!/bin/sh\necho '{\"type\":\"system\",\"subtype\":\"init\",\"session_id\":\"s-1\"}'\n\
             while read -r line; do echo \"$line\"; done\necho 'au revoir' >&2\n",
        )
        .unwrap();
        #[allow(clippy::permissions_set_readonly_false)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(&script, std::fs::Permissions::from_mode(0o755)).unwrap();
        }
        // SAFETY : un seul essai touche à cette variable
        unsafe { std::env::set_var("CLAUDECODE_CLI", &script) };
        let mut c = Cli::start(&Launch { cwd: dir.clone(), ..Default::default() }).unwrap();
        assert!(c.send_user("bonjour"));
        assert!(c.answer_permission("r1", true, &json!({"command":"ls"}), None));
        c.stdin = None;
        let mut got = Vec::new();
        let t0 = std::time::Instant::now();
        while t0.elapsed().as_secs() < 5 {
            got.extend(c.poll());
            if got.iter().any(|e| matches!(e, Event::Closed)) {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        let jsons: Vec<&Value> = got.iter().filter_map(|e| if let Event::Json(v) = e { Some(v) } else { None }).collect();
        assert_eq!(jsons[0]["session_id"], "s-1");
        assert_eq!(jsons[1]["message"]["content"][0]["text"], "bonjour");
        assert_eq!(jsons[2]["response"]["response"]["behavior"], "allow");
        assert_eq!(jsons[2]["response"]["response"]["updatedInput"]["command"], "ls");
    }
}
