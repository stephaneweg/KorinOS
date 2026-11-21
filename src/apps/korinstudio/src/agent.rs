//! Le copilote de KorinStudio : un agent de code externe, mené par des fils (pas d'async).
//!
//! Deux familles : les agents ACP (claude-acp, vibe-acp, mai-acp : JSON-RPC 2.0, un message par ligne sur stdio) et
//! Claude Code (`claude -p` en stream-json). `spawn` lance le process ; un fil lit sa sortie, un autre ses erreurs
//! (rendues en `Log`), un troisième (le pilote) tient l'état, écrit vers l'agent et rend des `AgentEvent`.

#![allow(unexpected_cfgs)] // (target_os = "korin" : la cible de Korin)
#![allow(clippy::collapsible_if, clippy::collapsible_match)] // (sans let-chains : pour les Rust plus anciens)

use serde_json::{Value, json};
use std::collections::{HashMap, HashSet, VecDeque};
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::thread;
use std::time::{Duration, Instant};

// --- l'interface ---------------------------------------------------------------------------------------------------

#[derive(Clone, Copy, PartialEq, Eq, Debug, serde::Serialize, serde::Deserialize)]
pub enum AgentKind {
    ClaudeCode,
    ClaudeApi,
    Vibe,
    Mai,
}

impl AgentKind {
    pub fn all() -> &'static [AgentKind] {
        &[AgentKind::ClaudeCode, AgentKind::ClaudeApi, AgentKind::Vibe, AgentKind::Mai]
    }

    pub fn label(self) -> &'static str {
        match self {
            AgentKind::ClaudeCode => "Claude Code",
            AgentKind::ClaudeApi => "Claude (API)",
            AgentKind::Vibe => "Mistral Vibe",
            AgentKind::Mai => "MAI",
        }
    }

    pub fn program(self) -> &'static str {
        match self {
            AgentKind::ClaudeCode => "claude",
            AgentKind::ClaudeApi => "claude-acp",
            AgentKind::Vibe => "vibe-acp",
            AgentKind::Mai => "mai-acp",
        }
    }

    /// Le programme dans le PATH (et sur Korin dans /bin, /devtools/*/bin) ; None s'il manque.
    pub fn find(self) -> Option<PathBuf> {
        find_program(self.program())
    }

    fn package(self) -> &'static str {
        match self {
            AgentKind::ClaudeCode => "claude-code",
            AgentKind::ClaudeApi => "claude",
            AgentKind::Vibe => "vibe",
            AgentKind::Mai => "mai",
        }
    }

    /// Comment se connecter (ou donner la clé), en français.
    fn login_hint(self) -> &'static str {
        match self {
            AgentKind::ClaudeCode => {
                "Claude Code n'est pas connecté : ouvrez le Terminal et lancez claude, puis /login (choisissez « Claude \
                 account with subscription » : la page de connexion s'ouvre dans Jet). Relancez ensuite l'agent."
            }
            AgentKind::ClaudeApi => {
                "Ajoutez votre clé d'API : claude-acp --set-key dans le Terminal (ou l'app Claude), puis relancez l'agent."
            }
            AgentKind::Vibe => {
                "Connectez-vous à Mistral : lancez agent dans le Terminal (la connexion se fait par le navigateur) ou \
                 ouvrez l'app Vibe, puis relancez l'agent."
            }
            AgentKind::Mai => {
                "Donnez le point d'accès et la clé de MAI : mai-acp --set-key dans le Terminal (ou l'app MAI), puis \
                 relancez l'agent."
            }
        }
    }
}

#[derive(Clone, Debug)]
pub struct AgentConfig {
    pub kind: AgentKind,
    pub cwd: PathBuf,
    pub mcp_url: String,
    pub mcp_token: String,
    pub studio_exe: PathBuf,
    pub system_hint: String,
    pub model: Option<String>,
    pub mode: Option<String>,
    pub resume: Option<String>,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ToolStatus {
    Pending,
    Running,
    Done,
    Failed,
}

#[derive(Clone, Debug)]
pub enum AgentEvent {
    Ready {
        session_id: String,
        agent_name: String,
        models: Vec<(String, String)>,
        current_model: Option<String>,
        modes: Vec<(String, String)>,
        current_mode: Option<String>,
    },
    Text(String),
    Thought(String),
    ToolCall { id: String, title: String, kind: String, status: ToolStatus, detail: String },
    ToolUpdate {
        id: String,
        status: Option<ToolStatus>,
        title: Option<String>,
        output: Option<String>,
        diff: Option<(String, String, String)>,
    },
    Plan(Vec<(String, String)>),
    Permission { req: u64, title: String, detail: String, options: Vec<(String, String, String)> },
    TurnEnd { stop_reason: String, info: Option<String> },
    ModeChanged(String),
    NeedsLogin(String),
    Error(String),
    Log(String),
    Exited(Option<i32>),
}

pub enum AgentCmd {
    Prompt(String),
    Cancel,
    Permission { req: u64, option: Option<String> },
    SetMode(String),
    SetModel(String),
    Shutdown,
}

pub struct AgentHandle {
    pub tx: Sender<AgentCmd>,
    pub rx: Receiver<AgentEvent>,
}

/// Lance l'agent et les fils qui le mènent ; chaque événement envoyé appelle aussi ctx.request_repaint().
pub fn spawn(cfg: AgentConfig, ctx: egui::Context) -> Result<AgentHandle, String> {
    let program = cfg.kind.find().ok_or_else(|| missing_message(cfg.kind))?;
    let (ev_tx, ev_rx) = mpsc::channel();
    let (cmd_tx, cmd_rx) = mpsc::channel::<AgentCmd>();
    let (in_tx, in_rx) = mpsc::channel::<Input>();
    let em = Emitter { tx: ev_tx, ctx, gone: Arc::new(AtomicBool::new(false)) };

    if cfg.kind == AgentKind::ClaudeCode {
        let mut b = ClaudeCode::new(cfg, program, em.clone(), in_tx.clone());
        b.start()?;
        start_driver(b, in_rx, em)?;
    } else {
        let mut b = Acp::new(cfg, em.clone());
        b.proc = Some(launch(&program, &[], &b.cfg.cwd, 0, in_tx.clone(), em.clone())?);
        b.start();
        start_driver(b, in_rx, em)?;
    }

    // les commandes de l'interface rejoignent le pilote ; la poignée lâchée vaut Shutdown
    thread::Builder::new()
        .name("agent-cmd".into())
        .spawn(move || {
            loop {
                match cmd_rx.recv() {
                    Ok(c) => {
                        let last = matches!(c, AgentCmd::Shutdown);
                        if in_tx.send(Input::Cmd(c)).is_err() || last {
                            break;
                        }
                    }
                    Err(_) => {
                        let _ = in_tx.send(Input::Cmd(AgentCmd::Shutdown));
                        break;
                    }
                }
            }
        })
        .map_err(|e| e.to_string())?;
    Ok(AgentHandle { tx: cmd_tx, rx: ev_rx })
}

fn missing_message(kind: AgentKind) -> String {
    if cfg!(target_os = "korin") {
        format!(
            "{} introuvable : installez le paquet {} (Logiciels, ou pkg install {}).",
            kind.program(),
            kind.package(),
            kind.package()
        )
    } else {
        format!("{} introuvable dans le PATH.", kind.program())
    }
}

// --- trouver et lancer le programme ----------------------------------------------------------------------------------

fn search_dirs() -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> =
        std::env::var_os("PATH").map(|p| std::env::split_paths(&p).collect()).unwrap_or_default();
    if cfg!(target_os = "korin") {
        v.push(PathBuf::from("/bin"));
        if let Ok(rd) = std::fs::read_dir("/devtools") {
            let mut more: Vec<PathBuf> = rd.flatten().map(|e| e.path().join("bin")).collect();
            more.sort();
            v.extend(more);
        }
    } else if let Some(h) = std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE")) {
        // (une app de bureau n'a pas toujours ~/.local/bin dans son PATH : c'est là que s'installe claude)
        let h = PathBuf::from(h);
        v.push(h.join(".local").join("bin"));
        v.push(h.join(".claude").join("local"));
    }
    v
}

fn find_program(name: &str) -> Option<PathBuf> {
    let exts: &[&str] = if cfg!(windows) { &[".exe", ".cmd", ".bat", ""] } else { &[""] };
    for d in search_dirs() {
        for e in exts {
            let p = d.join(format!("{name}{e}"));
            if is_executable(&p) {
                return Some(p);
            }
        }
    }
    None
}

fn is_executable(p: &Path) -> bool {
    if !p.is_file() {
        return false;
    }
    #[cfg(all(unix, not(target_os = "korin")))]
    {
        use std::os::unix::fs::PermissionsExt;
        if let Ok(m) = p.metadata() {
            return m.permissions().mode() & 0o111 != 0;
        }
    }
    true
}

/// `#!/chemin/interp [arg]` : l'interprète et ses arguments ; None si ce n'est pas un script ou si l'interprète
/// manque (le programme est alors lancé tel quel). `#!/usr/bin/env prog` cherche prog dans le PATH (Korin n'a pas
/// /usr/bin/env).
fn shebang(p: &Path) -> Option<(PathBuf, Vec<String>)> {
    let mut f = std::fs::File::open(p).ok()?;
    let mut buf = [0u8; 256];
    let mut n = 0;
    while n < buf.len() {
        match f.read(&mut buf[n..]) {
            Ok(0) => break,
            Ok(k) => n += k,
            Err(_) => return None,
        }
    }
    let head = &buf[..n];
    if !head.starts_with(b"#!") {
        return None;
    }
    let end = head.iter().position(|&c| c == b'\n').unwrap_or(n);
    let line = String::from_utf8_lossy(&head[2..end]).trim().to_string();
    let (interp, rest) = match line.find(char::is_whitespace) {
        Some(i) => (line[..i].to_string(), line[i..].trim().to_string()),
        None => (line.clone(), String::new()),
    };
    if interp.is_empty() {
        return None;
    }
    let ip = PathBuf::from(&interp);
    if ip.file_name().and_then(|s| s.to_str()) == Some("env") {
        let mut parts = rest.split_whitespace();
        let mut prog = parts.next()?;
        if prog == "-S" {
            prog = parts.next()?;
        }
        let exe = find_program(prog)?;
        return Some((exe, parts.map(str::to_string).collect()));
    }
    let exe = if ip.is_file() { ip } else { find_program(ip.file_name()?.to_str()?)? };
    Some((exe, if rest.is_empty() { vec![] } else { vec![rest] }))
}

/// Une ligne lue de l'agent, la fin de sa sortie, ou une commande de l'interface ; gen : le process qui l'a écrite.
enum Input {
    Cmd(AgentCmd),
    Out(u64, String),
    Eof(u64),
}

#[derive(Clone)]
struct Emitter {
    tx: Sender<AgentEvent>,
    ctx: egui::Context,
    gone: Arc<AtomicBool>,
}

impl Emitter {
    fn send(&self, ev: AgentEvent) {
        if self.tx.send(ev).is_err() {
            self.gone.store(true, Ordering::Relaxed);
        }
        self.ctx.request_repaint();
    }
    fn log(&self, s: impl Into<String>) {
        self.send(AgentEvent::Log(s.into()));
    }
    fn error(&self, s: impl Into<String>) {
        self.send(AgentEvent::Error(s.into()));
    }
    fn gone(&self) -> bool {
        self.gone.load(Ordering::Relaxed)
    }
}

struct Proc {
    child: Child,
    stdin: Option<ChildStdin>,
}

impl Proc {
    fn write_line(&mut self, line: &str) -> bool {
        let Some(w) = self.stdin.as_mut() else { return false };
        let mut s = String::with_capacity(line.len() + 1);
        s.push_str(line);
        s.push('\n');
        w.write_all(s.as_bytes()).and_then(|_| w.flush()).is_ok()
    }

    /// Ferme stdin, laisse `grace` à l'agent pour finir, puis le tue ; rend son code de sortie.
    fn stop(&mut self, grace: Duration) -> Option<i32> {
        self.stdin = None;
        let end = Instant::now() + grace;
        loop {
            match self.child.try_wait() {
                Ok(Some(s)) => return s.code(),
                Ok(None) if Instant::now() < end => thread::sleep(Duration::from_millis(20)),
                _ => break,
            }
        }
        let _ = self.child.kill();
        self.child.wait().ok().and_then(|s| s.code())
    }
}

impl Drop for Proc {
    fn drop(&mut self) {
        if matches!(self.child.try_wait(), Ok(None)) {
            let _ = self.child.kill();
            let _ = self.child.wait();
        }
    }
}

fn launch(
    program: &Path,
    args: &[String],
    cwd: &Path,
    gen_: u64,
    inp: Sender<Input>,
    em: Emitter,
) -> Result<Proc, String> {
    let mut cmd = match shebang(program) {
        Some((interp, iargs)) => {
            let mut c = Command::new(interp);
            c.args(iargs).arg(program);
            c
        }
        None => Command::new(program),
    };
    cmd.args(args)
        .current_dir(cwd)
        .env("KORINSTUDIO", "1")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    let mut child = cmd.spawn().map_err(|e| format!("impossible de lancer {} : {e}", program.display()))?;
    let out = child.stdout.take().ok_or("pas de stdout")?;
    let err = child.stderr.take().ok_or("pas de stderr")?;
    let stdin = child.stdin.take();
    thread::Builder::new()
        .name("agent-out".into())
        .spawn(move || {
            let mut r = BufReader::new(out);
            let mut buf = Vec::new();
            loop {
                buf.clear();
                match r.read_until(b'\n', &mut buf) {
                    Ok(0) | Err(_) => break,
                    Ok(_) => {
                        let s = String::from_utf8_lossy(&buf);
                        let s = s.trim_end_matches(['\n', '\r']);
                        if inp.send(Input::Out(gen_, s.to_string())).is_err() {
                            return;
                        }
                    }
                }
            }
            let _ = inp.send(Input::Eof(gen_));
        })
        .map_err(|e| e.to_string())?;
    thread::Builder::new()
        .name("agent-err".into())
        .spawn(move || {
            let mut r = BufReader::new(err);
            let mut buf = Vec::new();
            loop {
                buf.clear();
                match r.read_until(b'\n', &mut buf) {
                    Ok(0) | Err(_) => break,
                    Ok(_) => {
                        let s = String::from_utf8_lossy(&buf);
                        let s = s.trim_end_matches(['\n', '\r']);
                        if !s.is_empty() {
                            em.log(s.to_string());
                        }
                    }
                }
            }
        })
        .map_err(|e| e.to_string())?;
    Ok(Proc { child, stdin })
}

// --- le pilote -------------------------------------------------------------------------------------------------------

trait Backend: Send + 'static {
    fn gen_(&self) -> u64;
    fn proc_mut(&mut self) -> &mut Option<Proc>;
    fn on_cmd(&mut self, c: AgentCmd);
    fn on_line(&mut self, line: &str);
    /// Le process a fermé sa sortie (avant Exited).
    fn on_eof(&mut self);
    fn deadline(&self) -> Option<Instant> {
        None
    }
    fn on_timeout(&mut self) {}
    fn dead(&self) -> bool {
        false
    }
}

fn start_driver<B: Backend>(b: B, rx: Receiver<Input>, em: Emitter) -> Result<(), String> {
    thread::Builder::new()
        .name("agent-driver".into())
        .spawn(move || drive(b, rx, em))
        .map(|_| ())
        .map_err(|e| e.to_string())
}

fn drive<B: Backend>(mut b: B, rx: Receiver<Input>, em: Emitter) {
    loop {
        let r = match b.deadline() {
            Some(d) => rx.recv_timeout(d.saturating_duration_since(Instant::now())),
            None => rx.recv().map_err(|_| RecvTimeoutError::Disconnected),
        };
        match r {
            Ok(Input::Cmd(AgentCmd::Shutdown)) | Err(RecvTimeoutError::Disconnected) => break,
            Ok(Input::Cmd(c)) => b.on_cmd(c),
            Ok(Input::Out(g, l)) => {
                if g == b.gen_() {
                    b.on_line(&l)
                }
            }
            Ok(Input::Eof(g)) => {
                if g == b.gen_() {
                    b.on_eof();
                    break;
                }
            }
            Err(RecvTimeoutError::Timeout) => b.on_timeout(),
        }
        if em.gone() || b.dead() {
            break;
        }
    }
    let code = b.proc_mut().take().and_then(|mut p| p.stop(Duration::from_millis(1000)));
    em.send(AgentEvent::Exited(code));
}

// --- petits outils -----------------------------------------------------------------------------------------------------

const MAX_OUTPUT: usize = 32 * 1024;

fn st<'a>(v: &'a Value, k: &str) -> &'a str {
    v.get(k).and_then(Value::as_str).unwrap_or("")
}

fn opt_st(v: &Value, k: &str) -> Option<String> {
    v.get(k).and_then(Value::as_str).filter(|s| !s.is_empty()).map(str::to_string)
}

fn clip(s: &str, max: usize) -> String {
    if s.len() <= max {
        return s.to_string();
    }
    let mut i = max;
    while !s.is_char_boundary(i) {
        i -= 1;
    }
    format!("{}…", &s[..i])
}

/// La première ligne non vide, coupée.
fn one_line(s: &str, max: usize) -> String {
    let l = s.lines().map(str::trim).find(|l| !l.is_empty()).unwrap_or("");
    let more = s.trim().lines().count() > 1;
    let c = clip(l, max);
    if more && !c.ends_with('…') { format!("{c} …") } else { c }
}

fn rel_path(p: &str, cwd: &Path) -> String {
    let path = Path::new(p);
    match path.strip_prefix(cwd) {
        Ok(r) if !r.as_os_str().is_empty() => r.to_string_lossy().replace('\\', "/"),
        _ => p.to_string(),
    }
}

/// Un résumé d'une ligne de l'entrée d'un outil (chemin, commande, motif…).
fn summarize_input(input: &Value, cwd: &Path) -> String {
    const KEYS: &[&str] = &[
        "command", "cmd", "file_path", "path", "notebook_path", "filePath", "pattern", "query", "url", "description",
        "prompt", "name",
    ];
    let Some(o) = input.as_object() else {
        return match input {
            Value::String(s) => one_line(s, 200),
            _ => String::new(),
        };
    };
    for k in KEYS {
        let v = match o.get(*k) {
            Some(Value::String(s)) if !s.is_empty() => s.clone(),
            Some(Value::Array(a)) if !a.is_empty() => {
                a.iter().filter_map(Value::as_str).collect::<Vec<_>>().join(" ")
            }
            _ => continue,
        };
        if v.is_empty() {
            continue;
        }
        let v = if k.contains("path") || *k == "filePath" { rel_path(&v, cwd) } else { v };
        return one_line(&v, 200);
    }
    if o.is_empty() { String::new() } else { clip(&input.to_string(), 200) }
}

fn is_login_text(t: &str) -> bool {
    let t = t.to_lowercase();
    [
        "/login", "not logged in", "log in", "login", "invalid api key", "api key", "authentication", "authenticate",
        "oauth", "unauthorized", "401",
    ]
    .iter()
    .any(|k| t.contains(k))
}

// --- ACP (claude-acp, vibe-acp, mai-acp) ------------------------------------------------------------------------------

enum Req {
    Init,
    Open { load: bool },
    Prompt { started: Instant },
    SetMode(String),
    SetModel(String),
}

#[derive(PartialEq)]
enum ModelApi {
    None,
    ConfigOption, // session/set_config_option {configId: "model"} (claude-acp, mai-acp, vibe)
    SetModel,     // session/set_model {modelId} (models / availableModels)
}

struct Acp {
    cfg: AgentConfig,
    em: Emitter,
    proc: Option<Proc>,
    next_id: u64,
    pending: HashMap<u64, Req>,
    session: Option<String>,
    agent_name: String,
    load_session: bool,
    mcp_http: bool,
    models: Vec<(String, String)>,
    current_model: Option<String>,
    model_api: ModelApi,
    modes: Vec<(String, String)>,
    current_mode: Option<String>,
    turn: bool,
    queue: VecDeque<String>,
    hint_sent: bool,
    perms: HashMap<u64, Value>,
    next_perm: u64,
    tools: HashMap<String, (String, String)>,
    loading: bool,
    replayed: usize,
    usage: Option<(u64, u64)>,
}

impl Acp {
    fn new(cfg: AgentConfig, em: Emitter) -> Self {
        let agent_name = cfg.kind.label().to_string();
        Acp {
            cfg,
            em,
            proc: None,
            next_id: 1,
            pending: HashMap::new(),
            session: None,
            agent_name,
            load_session: false,
            mcp_http: false,
            models: vec![],
            current_model: None,
            model_api: ModelApi::None,
            modes: vec![],
            current_mode: None,
            turn: false,
            queue: VecDeque::new(),
            hint_sent: false,
            perms: HashMap::new(),
            next_perm: 1,
            tools: HashMap::new(),
            loading: false,
            replayed: 0,
            usage: None,
        }
    }

    fn start(&mut self) {
        self.request(
            "initialize",
            json!({
                "protocolVersion": 1,
                "clientCapabilities": {"fs": {"readTextFile": false, "writeTextFile": false}, "terminal": false},
                "clientInfo": {"name": "korinstudio", "title": "KorinStudio", "version": env!("CARGO_PKG_VERSION")},
            }),
            Req::Init,
        );
    }

    fn write(&mut self, v: &Value) {
        let line = v.to_string();
        let ok = self.proc.as_mut().is_some_and(|p| p.write_line(&line));
        if !ok {
            self.em.log("écriture vers l'agent impossible (process arrêté ?)");
        }
    }

    fn request(&mut self, method: &str, params: Value, kind: Req) {
        let id = self.next_id;
        self.next_id += 1;
        self.pending.insert(id, kind);
        self.write(&json!({"jsonrpc": "2.0", "id": id, "method": method, "params": params}));
    }

    fn notify(&mut self, method: &str, params: Value) {
        self.write(&json!({"jsonrpc": "2.0", "method": method, "params": params}));
    }

    fn cwd_string(&self) -> String {
        let c = &self.cfg.cwd;
        let abs = if c.is_absolute() {
            c.clone()
        } else {
            std::env::current_dir().map(|d| d.join(c)).unwrap_or_else(|_| c.clone())
        };
        abs.to_string_lossy().into_owned()
    }

    /// Le serveur MCP de l'IDE : en http si l'agent le sait (claude-acp et mai-acp le prennent sans l'annoncer, et
    /// ignorent stdio), sinon le pont stdio `<studio_exe> --mcp-bridge <url> <jeton>`.
    fn mcp_servers(&self) -> Value {
        if self.cfg.mcp_url.is_empty() {
            return json!([]);
        }
        let http = self.mcp_http || matches!(self.cfg.kind, AgentKind::ClaudeApi | AgentKind::Mai);
        if http {
            json!([{
                "type": "http", "name": "korinstudio", "url": self.cfg.mcp_url,
                "headers": [{"name": "Authorization", "value": format!("Bearer {}", self.cfg.mcp_token)}],
            }])
        } else {
            json!([{
                "name": "korinstudio", "command": self.cfg.studio_exe.to_string_lossy(),
                "args": ["--mcp-bridge", self.cfg.mcp_url, self.cfg.mcp_token], "env": [],
            }])
        }
    }

    fn open(&mut self) {
        let mcp = self.mcp_servers();
        let cwd = self.cwd_string();
        match self.cfg.resume.clone() {
            Some(id) if self.load_session => {
                self.loading = true;
                self.replayed = 0;
                self.request("session/load", json!({"sessionId": id, "cwd": cwd, "mcpServers": mcp}), Req::Open {
                    load: true,
                });
            }
            r => {
                if r.is_some() {
                    self.em.log("cet agent ne sait pas reprendre une séance : nouvelle séance");
                }
                self.request("session/new", json!({"cwd": cwd, "mcpServers": mcp}), Req::Open { load: false });
            }
        }
    }

    fn ready(&self) -> AgentEvent {
        AgentEvent::Ready {
            session_id: self.session.clone().unwrap_or_default(),
            agent_name: self.agent_name.clone(),
            models: self.models.clone(),
            current_model: self.current_model.clone(),
            modes: self.modes.clone(),
            current_mode: self.current_mode.clone(),
        }
    }

    fn read_session(&mut self, r: &Value) {
        if let Some(m) = r.get("modes").filter(|m| m.is_object()) {
            self.current_mode = opt_st(m, "currentModeId");
            self.modes = m
                .get("availableModes")
                .and_then(Value::as_array)
                .map(|a| {
                    a.iter()
                        .filter_map(|e| {
                            let id = opt_st(e, "id")?;
                            Some((id.clone(), opt_st(e, "name").unwrap_or(id)))
                        })
                        .collect()
                })
                .unwrap_or_default();
        }
        if let Some(m) = r.get("models").filter(|m| m.is_object()) {
            self.current_model = opt_st(m, "currentModelId");
            self.models = m
                .get("availableModels")
                .and_then(Value::as_array)
                .map(|a| {
                    a.iter()
                        .filter_map(|e| {
                            let id = opt_st(e, "modelId")?;
                            Some((id.clone(), opt_st(e, "name").unwrap_or(id)))
                        })
                        .collect()
                })
                .unwrap_or_default();
            self.model_api = ModelApi::SetModel;
        }
        self.read_options(r.get("configOptions"));
    }

    /// configOptions : l'option « model » (valeurs simples ou en groupes) ; rend vrai si le modèle a changé.
    fn read_options(&mut self, opts: Option<&Value>) -> bool {
        let Some(arr) = opts.and_then(Value::as_array) else { return false };
        let Some(o) = arr.iter().find(|o| st(o, "id") == "model") else { return false };
        let mut list = vec![];
        fn walk(v: &Value, out: &mut Vec<(String, String)>) {
            for e in v.as_array().into_iter().flatten() {
                if let Some(val) = opt_st(e, "value") {
                    out.push((val.clone(), opt_st(e, "name").unwrap_or(val)));
                } else if e.get("options").is_some() {
                    walk(&e["options"], out);
                }
            }
        }
        walk(&o["options"], &mut list);
        let cur = opt_st(o, "currentValue");
        let changed = cur != self.current_model || list != self.models;
        self.models = list;
        self.current_model = cur;
        self.model_api = ModelApi::ConfigOption;
        changed
    }

    /// Le mode de l'agent qui répond à `m` (son id, ou un nom de Claude Code : acceptEdits → accept-edits…).
    fn resolve_mode(&self, m: &str) -> Option<String> {
        if let Some((id, _)) = self.modes.iter().find(|(id, _)| id == m) {
            return Some(id.clone());
        }
        let norm = |s: &str| s.to_lowercase().replace(['-', '_', ' '], "");
        const GROUPS: &[&[&str]] = &[
            &["default", "ask"],
            &["acceptedits", "autoedit", "autoedits"],
            &["bypasspermissions", "autoapprove", "yolo"],
            &["plan"],
        ];
        let g = GROUPS.iter().find(|g| g.contains(&norm(m).as_str()))?;
        self.modes.iter().find(|(id, _)| g.contains(&norm(id).as_str())).map(|(id, _)| id.clone())
    }

    fn set_mode(&mut self, id: String) {
        let sid = self.session.clone().unwrap_or_default();
        self.request("session/set_mode", json!({"sessionId": sid, "modeId": id}), Req::SetMode(id));
    }

    fn set_model(&mut self, m: String) {
        let sid = self.session.clone().unwrap_or_default();
        match self.model_api {
            ModelApi::SetModel => {
                self.request("session/set_model", json!({"sessionId": sid, "modelId": m}), Req::SetModel(m))
            }
            ModelApi::ConfigOption => self.request(
                "session/set_config_option",
                json!({"sessionId": sid, "configId": "model", "value": m}),
                Req::SetModel(m),
            ),
            ModelApi::None => self.em.error("cet agent ne permet pas de choisir le modèle"),
        }
    }

    fn send_prompt(&mut self, text: String) {
        let mut blocks = vec![];
        if !self.hint_sent && !self.cfg.system_hint.trim().is_empty() {
            blocks.push(json!({"type": "text", "text": self.cfg.system_hint}));
        }
        self.hint_sent = true;
        blocks.push(json!({"type": "text", "text": text}));
        let sid = self.session.clone().unwrap_or_default();
        self.turn = true;
        self.usage = None;
        self.request("session/prompt", json!({"sessionId": sid, "prompt": blocks}), Req::Prompt {
            started: Instant::now(),
        });
    }

    fn next_prompt(&mut self) {
        if self.turn || self.session.is_none() || self.loading {
            return;
        }
        if let Some(t) = self.queue.pop_front() {
            self.send_prompt(t);
        }
    }

    fn cancel_permissions(&mut self) {
        let ids: Vec<Value> = self.perms.drain().map(|(_, id)| id).collect();
        for id in ids {
            self.write(&json!({"jsonrpc": "2.0", "id": id, "result": {"outcome": {"outcome": "cancelled"}}}));
        }
    }

    fn rpc_error_text(err: &Value) -> String {
        let mut m = st(err, "message").to_string();
        if let Some(d) = err.get("data") {
            let d = match d {
                Value::String(s) => s.clone(),
                Value::Null => String::new(),
                o => clip(&o.to_string(), 300),
            };
            if !d.is_empty() {
                m = format!("{m} ({d})");
            }
        }
        if m.is_empty() { format!("erreur {}", err.get("code").unwrap_or(&Value::Null)) } else { m }
    }

    fn is_auth_error(err: &Value) -> bool {
        let code = err.get("code").and_then(Value::as_i64).unwrap_or(0);
        if code == -32000 || code == -32002 {
            return true;
        }
        let m = Self::rpc_error_text(err).to_lowercase();
        [
            "auth", "api key", "api_key", "apikey", "clé", "login", "log in", "sign in", "sign-in", "401",
            "unauthorized", "credential",
        ]
        .iter()
        .any(|k| m.contains(k))
    }

    fn on_response(&mut self, id: u64, result: Option<&Value>, err: Option<&Value>) {
        let Some(kind) = self.pending.remove(&id) else {
            self.em.log(format!("réponse inattendue de l'agent (id {id})"));
            return;
        };
        match kind {
            Req::Init => {
                if let Some(e) = err {
                    if Self::is_auth_error(e) {
                        self.em.send(AgentEvent::NeedsLogin(self.cfg.kind.login_hint().into()));
                    } else {
                        self.em.error(format!("initialize : {}", Self::rpc_error_text(e)));
                    }
                    return;
                }
                let r = result.cloned().unwrap_or(Value::Null);
                let caps = &r["agentCapabilities"];
                self.load_session = caps["loadSession"].as_bool().unwrap_or(false);
                self.mcp_http = caps["mcpCapabilities"]["http"].as_bool().unwrap_or(false);
                let info = &r["agentInfo"];
                if let Some(n) = opt_st(info, "title").or_else(|| opt_st(info, "name")) {
                    self.agent_name = n;
                }
                self.em.log(format!(
                    "agent {} {} (protocole {}, reprise {}, MCP http {})",
                    self.agent_name,
                    st(info, "version"),
                    r["protocolVersion"],
                    self.load_session,
                    self.mcp_http
                ));
                self.open();
            }
            Req::Open { load } => {
                if let Some(e) = err {
                    self.loading = false;
                    if Self::is_auth_error(e) {
                        self.em.send(AgentEvent::NeedsLogin(self.cfg.kind.login_hint().into()));
                    } else if load {
                        self.em.log(format!("session/load : {} ; nouvelle séance", Self::rpc_error_text(e)));
                        self.cfg.resume = None;
                        self.open();
                    } else {
                        self.em.error(format!("session/new : {}", Self::rpc_error_text(e)));
                    }
                    return;
                }
                let r = result.cloned().unwrap_or(Value::Null);
                self.session = opt_st(&r, "sessionId").or_else(|| if load { self.cfg.resume.clone() } else { None });
                if self.session.is_none() {
                    self.em.error("l'agent n'a pas donné d'identifiant de séance");
                    return;
                }
                if load {
                    self.loading = false;
                    self.hint_sent = true; // (déjà dans l'historique de la séance reprise)
                    self.em.log(format!("séance reprise ({} mises à jour rejouées, non montrées)", self.replayed));
                }
                self.read_session(&r);
                self.em.send(self.ready());
                // le mode et le modèle voulus
                if let Some(m) = self.cfg.mode.clone() {
                    match self.resolve_mode(&m) {
                        Some(id) if Some(&id) != self.current_mode.as_ref() => self.set_mode(id),
                        Some(_) => {}
                        None => self.em.log(format!("mode « {m} » inconnu de l'agent")),
                    }
                }
                if let Some(m) = self.cfg.model.clone() {
                    if Some(&m) != self.current_model.as_ref() {
                        if self.models.iter().any(|(id, _)| *id == m) {
                            self.set_model(m);
                        } else {
                            self.em.log(format!("modèle « {m} » inconnu de l'agent"));
                        }
                    }
                }
                self.next_prompt();
            }
            Req::Prompt { started } => {
                self.turn = false;
                self.cancel_permissions();
                let mut info = format!("{:.1} s", started.elapsed().as_secs_f64());
                if let Some((u, n)) = self.usage.filter(|(_, n)| *n > 0) {
                    let pct = u * 100 / n;
                    if pct == 0 {
                        info.push_str(" · contexte < 1 %");
                    } else {
                        info.push_str(&format!(" · contexte {pct} %"));
                    }
                }
                match err {
                    Some(e) => {
                        if Self::is_auth_error(e) {
                            self.em.send(AgentEvent::NeedsLogin(self.cfg.kind.login_hint().into()));
                        }
                        self.em.error(Self::rpc_error_text(e));
                        self.em.send(AgentEvent::TurnEnd { stop_reason: "error".into(), info: Some(info) });
                    }
                    None => {
                        let r = result.cloned().unwrap_or(Value::Null);
                        let reason = opt_st(&r, "stopReason").unwrap_or_else(|| "end_turn".into());
                        self.em.send(AgentEvent::TurnEnd { stop_reason: reason, info: Some(info) });
                    }
                }
                self.next_prompt();
            }
            Req::SetMode(m) => match err {
                Some(e) => self.em.error(format!("mode {m} : {}", Self::rpc_error_text(e))),
                None => {
                    if self.current_mode.as_deref() != Some(m.as_str()) {
                        self.current_mode = Some(m.clone());
                        self.em.send(AgentEvent::ModeChanged(m));
                    }
                }
            },
            Req::SetModel(m) => match err {
                Some(e) => self.em.error(format!("modèle {m} : {}", Self::rpc_error_text(e))),
                None => {
                    let opts = result.and_then(|r| r.get("configOptions")).cloned();
                    if !self.read_options(opts.as_ref()) {
                        self.current_model = Some(m);
                    }
                    self.em.send(self.ready());
                }
            },
        }
    }

    fn on_update(&mut self, u: &Value) {
        if self.loading {
            self.replayed += 1;
            return;
        }
        let cwd = self.cfg.cwd.clone();
        match st(u, "sessionUpdate") {
            "agent_message_chunk" | "agent_thought_chunk" => {
                let c = &u["content"];
                if st(c, "type") != "text" {
                    return;
                }
                let t = st(c, "text").to_string();
                if t.is_empty() {
                    return;
                }
                if st(u, "sessionUpdate") == "agent_message_chunk" {
                    self.em.send(AgentEvent::Text(t));
                } else {
                    self.em.send(AgentEvent::Thought(t));
                }
            }
            "tool_call" => {
                let id = st(u, "toolCallId").to_string();
                let title = opt_st(u, "title").unwrap_or_else(|| "outil".into());
                let detail = acp_detail(u, &cwd);
                let status = acp_status(st(u, "status")).unwrap_or(ToolStatus::Pending);
                self.tools.insert(id.clone(), (title.clone(), detail.clone()));
                self.em.send(AgentEvent::ToolCall {
                    id: id.clone(),
                    title,
                    kind: opt_st(u, "kind").unwrap_or_else(|| "other".into()),
                    status,
                    detail,
                });
                let (output, diff) = acp_content(u.get("content"));
                if output.is_some() || diff.is_some() {
                    self.em.send(AgentEvent::ToolUpdate { id, status: None, title: None, output, diff });
                }
            }
            "tool_call_update" => {
                let id = st(u, "toolCallId").to_string();
                let title = opt_st(u, "title");
                if !self.tools.contains_key(&id) {
                    // (une mise à jour sans appel annoncé : on l'annonce)
                    let t = title.clone().unwrap_or_else(|| "outil".into());
                    let detail = acp_detail(u, &cwd);
                    self.tools.insert(id.clone(), (t.clone(), detail.clone()));
                    self.em.send(AgentEvent::ToolCall {
                        id: id.clone(),
                        title: t,
                        kind: opt_st(u, "kind").unwrap_or_else(|| "other".into()),
                        status: ToolStatus::Pending,
                        detail,
                    });
                } else if let Some(t) = &title {
                    if let Some(e) = self.tools.get_mut(&id) {
                        e.0 = t.clone();
                    }
                }
                let (mut output, diff) = acp_content(u.get("content"));
                if output.is_none() {
                    if let Some(Value::String(s)) = u.get("rawOutput") {
                        if !s.is_empty() {
                            output = Some(clip(s, MAX_OUTPUT));
                        }
                    }
                }
                let status = acp_status(st(u, "status"));
                self.em.send(AgentEvent::ToolUpdate { id, status, title, output, diff });
            }
            "plan" => {
                let entries = u["entries"]
                    .as_array()
                    .map(|a| a.iter().map(|e| (st(e, "content").to_string(), st(e, "status").to_string())).collect())
                    .unwrap_or_default();
                self.em.send(AgentEvent::Plan(entries));
            }
            "current_mode_update" => {
                if let Some(m) = opt_st(u, "currentModeId") {
                    if self.current_mode.as_ref() != Some(&m) {
                        self.current_mode = Some(m.clone());
                        self.em.send(AgentEvent::ModeChanged(m));
                    }
                }
            }
            "config_option_update" => {
                if self.read_options(u.get("configOptions")) {
                    self.em.send(self.ready());
                }
            }
            "current_model_update" => {
                if let Some(m) = opt_st(u, "currentModelId").or_else(|| opt_st(u, "modelId")) {
                    if self.current_model.as_ref() != Some(&m) {
                        self.current_model = Some(m);
                        self.em.send(self.ready());
                    }
                }
            }
            "usage_update" => {
                let used = u["used"].as_u64().unwrap_or(0);
                let size = u["size"].as_u64().unwrap_or(0);
                self.usage = Some((used, size));
            }
            _ => {}
        }
    }

    fn on_request(&mut self, id: Value, method: &str, p: &Value) {
        if method != "session/request_permission" {
            self.write(&json!({"jsonrpc": "2.0", "id": id,
                "error": {"code": -32601, "message": format!("méthode non prise en charge : {method}")}}));
            return;
        }
        if self.loading {
            self.write(&json!({"jsonrpc": "2.0", "id": id, "result": {"outcome": {"outcome": "cancelled"}}}));
            return;
        }
        let tc = &p["toolCall"];
        let tid = st(tc, "toolCallId");
        let known = self.tools.get(tid).cloned();
        let title = opt_st(tc, "title")
            .or_else(|| known.as_ref().map(|k| k.0.clone()))
            .unwrap_or_else(|| "Autorisation".into());
        let mut detail = acp_detail(tc, &self.cfg.cwd);
        if detail.is_empty() {
            detail = known.map(|k| k.1).unwrap_or_default();
        }
        let options = p["options"]
            .as_array()
            .map(|a| {
                a.iter()
                    .map(|o| (st(o, "optionId").to_string(), st(o, "name").to_string(), st(o, "kind").to_string()))
                    .collect()
            })
            .unwrap_or_default();
        let req = self.next_perm;
        self.next_perm += 1;
        self.perms.insert(req, id);
        self.em.send(AgentEvent::Permission { req, title, detail, options });
    }
}

fn acp_status(s: &str) -> Option<ToolStatus> {
    match s {
        "pending" => Some(ToolStatus::Pending),
        "in_progress" => Some(ToolStatus::Running),
        "completed" => Some(ToolStatus::Done),
        "failed" => Some(ToolStatus::Failed),
        _ => None,
    }
}

/// Le détail d'un appel ACP : rawInput, sinon le premier emplacement.
fn acp_detail(u: &Value, cwd: &Path) -> String {
    if let Some(r) = u.get("rawInput") {
        let d = summarize_input(r, cwd);
        if !d.is_empty() {
            return d;
        }
    }
    if let Some(l) = u["locations"].as_array().and_then(|a| a.first()) {
        let p = rel_path(st(l, "path"), cwd);
        return match l["line"].as_u64() {
            Some(n) => format!("{p}:{n}"),
            None => p,
        };
    }
    String::new()
}

/// Le contenu d'un appel ACP : le texte des blocs « content », et le dernier « diff ».
fn acp_content(c: Option<&Value>) -> (Option<String>, Option<(String, String, String)>) {
    let mut text = String::new();
    let mut diff = None;
    for b in c.and_then(Value::as_array).into_iter().flatten() {
        match st(b, "type") {
            "content" => {
                let inner = &b["content"];
                if st(inner, "type") == "text" {
                    if !text.is_empty() {
                        text.push('\n');
                    }
                    text.push_str(st(inner, "text"));
                }
            }
            "diff" => {
                diff = Some((st(b, "path").to_string(), st(b, "oldText").to_string(), st(b, "newText").to_string()))
            }
            _ => {}
        }
    }
    ((!text.is_empty()).then(|| clip(&text, MAX_OUTPUT)), diff)
}

impl Backend for Acp {
    fn gen_(&self) -> u64 {
        0
    }

    fn proc_mut(&mut self) -> &mut Option<Proc> {
        &mut self.proc
    }

    fn on_cmd(&mut self, c: AgentCmd) {
        match c {
            AgentCmd::Prompt(t) => {
                if self.turn || self.session.is_none() || self.loading {
                    self.queue.push_back(t);
                    if self.turn {
                        self.em.log("demande mise en attente : un tour est en cours");
                    }
                } else {
                    self.send_prompt(t);
                }
            }
            AgentCmd::Cancel => {
                self.queue.clear();
                self.cancel_permissions();
                if self.turn {
                    if let Some(sid) = self.session.clone() {
                        self.notify("session/cancel", json!({"sessionId": sid}));
                    }
                }
            }
            AgentCmd::Permission { req, option } => match self.perms.remove(&req) {
                Some(id) => {
                    let outcome = match option {
                        Some(o) => json!({"outcome": "selected", "optionId": o}),
                        None => json!({"outcome": "cancelled"}),
                    };
                    self.write(&json!({"jsonrpc": "2.0", "id": id, "result": {"outcome": outcome}}));
                }
                None => self.em.log(format!("autorisation {req} déjà réglée")),
            },
            AgentCmd::SetMode(m) => {
                if self.session.is_none() {
                    self.cfg.mode = Some(m);
                    return;
                }
                match self.resolve_mode(&m) {
                    Some(id) => self.set_mode(id),
                    None => self.em.error(format!("mode inconnu de l'agent : {m}")),
                }
            }
            AgentCmd::SetModel(m) => {
                if self.session.is_none() {
                    self.cfg.model = Some(m);
                } else {
                    self.set_model(m);
                }
            }
            AgentCmd::Shutdown => {}
        }
    }

    fn on_line(&mut self, line: &str) {
        let t = line.trim();
        if t.is_empty() {
            return;
        }
        let msg: Value = match serde_json::from_str(t) {
            Ok(v @ Value::Object(_)) => v,
            _ => {
                self.em.log(format!("(sortie) {}", clip(t, 2000)));
                return;
            }
        };
        let method = msg.get("method").and_then(Value::as_str);
        let id = msg.get("id").filter(|v| !v.is_null()).cloned();
        let params = msg.get("params").cloned().unwrap_or(Value::Null);
        match (method, id) {
            (Some(m), Some(id)) => self.on_request(id, m, &params),
            (Some("session/update"), None) => {
                if let Some(u) = params.get("update") {
                    self.on_update(u);
                }
            }
            (Some(_), None) => {}
            (None, Some(id)) => match id.as_u64() {
                Some(n) => self.on_response(n, msg.get("result"), msg.get("error")),
                None => self.em.log(format!("réponse d'id inconnu : {id}")),
            },
            (None, None) => self.em.log(format!("message JSON-RPC illisible : {}", clip(t, 500))),
        }
    }

    fn on_eof(&mut self) {
        if self.session.is_none() {
            self.em.error(format!("{} s'est arrêté avant d'ouvrir la séance (voir le journal)", self.agent_name));
        } else if self.turn {
            self.em.error(format!("{} s'est arrêté pendant le tour", self.agent_name));
            self.em.send(AgentEvent::TurnEnd { stop_reason: "error".into(), info: None });
        }
    }
}

// --- Claude Code (claude -p, stream-json) ---------------------------------------------------------------------------

/// Les outils de l'IDE qui ne changent rien : permis sans demander.
const IDE_READ_TOOLS: &[&str] = &[
    "get_workspace",
    "list_project_templates",
    "read_file",
    "list_files",
    "search",
    "get_diagnostics",
    "open_file",
];

const CANCEL_GRACE: Duration = Duration::from_secs(5);

enum Ctl {
    Init,
    Interrupt,
    Mode(String),
    Model(String),
}

struct CcTool {
    name: String,
    input: Value,
}

struct ClaudeCode {
    cfg: AgentConfig,
    program: PathBuf,
    em: Emitter,
    inp: Sender<Input>,
    proc: Option<Proc>,
    generation: u64,
    dead: bool,
    session: Option<String>,
    model_raw: Option<String>,
    models: Vec<(String, String)>,
    mode: String,
    last_ready: Option<String>,
    turn: bool,
    queue: VecDeque<String>,
    cancel_at: Option<Instant>,
    cur_msg: String,
    streamed: HashSet<String>,
    thinking_blocks: HashMap<u64, bool>, // index du bloc -> des morceaux reçus
    tools: HashMap<String, CcTool>,
    open_tools: Vec<String>,
    skipped: HashSet<String>,
    next_ctl: u64,
    ctl: HashMap<String, Ctl>,
}

impl ClaudeCode {
    fn new(cfg: AgentConfig, program: PathBuf, em: Emitter, inp: Sender<Input>) -> Self {
        let mode = cfg.mode.clone().unwrap_or_else(|| "default".into());
        let session = cfg.resume.clone();
        ClaudeCode {
            cfg,
            program,
            em,
            inp,
            proc: None,
            generation: 0,
            dead: false,
            session,
            model_raw: None,
            models: vec![
                ("default".into(), "Par défaut".into()),
                ("opus".into(), "Opus".into()),
                ("sonnet".into(), "Sonnet".into()),
                ("haiku".into(), "Haiku".into()),
            ],
            mode,
            last_ready: None,
            turn: false,
            queue: VecDeque::new(),
            cancel_at: None,
            cur_msg: String::new(),
            streamed: HashSet::new(),
            thinking_blocks: HashMap::new(),
            tools: HashMap::new(),
            open_tools: vec![],
            skipped: HashSet::new(),
            next_ctl: 0,
            ctl: HashMap::new(),
        }
    }

    fn args(&self) -> Vec<String> {
        let mut a: Vec<String> = [
            "-p",
            "--input-format",
            "stream-json",
            "--output-format",
            "stream-json",
            "--verbose",
            "--include-partial-messages",
        ]
        .iter()
        .map(|s| s.to_string())
        .collect();
        if !self.cfg.mcp_url.is_empty() {
            let mcp = json!({"mcpServers": {"korinstudio": {
                "type": "http", "url": self.cfg.mcp_url,
                "headers": {"Authorization": format!("Bearer {}", self.cfg.mcp_token)},
            }}});
            a.push("--mcp-config".into());
            a.push(mcp.to_string());
            a.push("--permission-prompt-tool".into());
            a.push("mcp__korinstudio__approve".into());
            a.push("--allowedTools".into());
            a.push(IDE_READ_TOOLS.iter().map(|t| format!("mcp__korinstudio__{t}")).collect::<Vec<_>>().join(","));
        }
        if !self.cfg.system_hint.trim().is_empty() {
            a.push("--append-system-prompt".into());
            a.push(self.cfg.system_hint.clone());
        }
        if let Some(m) = &self.cfg.mode {
            a.push("--permission-mode".into());
            a.push(m.clone());
        }
        if let Some(m) = &self.cfg.model {
            a.push("--model".into());
            a.push(m.clone());
        }
        if let Some(r) = &self.cfg.resume {
            a.push("--resume".into());
            a.push(r.clone());
        }
        a
    }

    fn start(&mut self) -> Result<(), String> {
        let p = launch(&self.program, &self.args(), &self.cfg.cwd, self.generation, self.inp.clone(), self.em.clone())?;
        self.proc = Some(p);
        self.control(json!({"subtype": "initialize"}), Ctl::Init);
        Ok(())
    }

    fn write(&mut self, v: &Value) {
        let line = v.to_string();
        let ok = self.proc.as_mut().is_some_and(|p| p.write_line(&line));
        if !ok {
            self.em.log("écriture vers Claude Code impossible (process arrêté ?)");
        }
    }

    fn control(&mut self, request: Value, kind: Ctl) {
        self.next_ctl += 1;
        let id = format!("ks-{}-{}", self.generation, self.next_ctl);
        self.ctl.insert(id.clone(), kind);
        self.write(&json!({"type": "control_request", "request_id": id, "request": request}));
    }

    fn ready_if_changed(&mut self) {
        let current = self.cfg.model.clone().unwrap_or_else(|| "default".into());
        let mut models = self.models.clone();
        if let (Some(raw), Some(d)) = (&self.model_raw, models.iter_mut().find(|m| m.0 == "default")) {
            if current == "default" {
                d.1 = format!("Par défaut ({raw})");
            }
        }
        let modes: Vec<(String, String)> = [
            ("default", "Demander"),
            ("acceptEdits", "Accepter les modifications"),
            ("plan", "Planifier"),
            ("bypassPermissions", "Tout permettre"),
        ]
        .iter()
        .map(|(a, b)| (a.to_string(), b.to_string()))
        .collect();
        let sid = self.session.clone().unwrap_or_default();
        let key = format!("{sid}|{current}|{}|{:?}", self.mode, models);
        if self.last_ready.as_ref() == Some(&key) {
            return;
        }
        self.last_ready = Some(key);
        self.em.send(AgentEvent::Ready {
            session_id: sid,
            agent_name: "Claude Code".into(),
            models,
            current_model: Some(current),
            modes,
            current_mode: Some(self.mode.clone()),
        });
    }

    fn send_user(&mut self, text: String) {
        self.turn = true;
        self.write(&json!({"type": "user", "message": {"role": "user", "content": [{"type": "text", "text": text}]}}));
    }

    fn end_turn(&mut self, stop_reason: String, info: Option<String>) {
        if stop_reason != "end_turn" {
            // les appels restés ouverts ne finiront pas
            for id in std::mem::take(&mut self.open_tools) {
                self.em.send(AgentEvent::ToolUpdate {
                    id,
                    status: Some(ToolStatus::Failed),
                    title: None,
                    output: Some("interrompu".into()),
                    diff: None,
                });
            }
        }
        self.open_tools.clear();
        self.turn = false;
        self.cancel_at = None;
        self.streamed.clear();
        self.em.send(AgentEvent::TurnEnd { stop_reason, info });
        if let Some(t) = self.queue.pop_front() {
            self.send_user(t);
        }
    }

    fn on_tool_use(&mut self, b: &Value) {
        let id = st(b, "id").to_string();
        if id.is_empty() || self.tools.contains_key(&id) || self.skipped.contains(&id) {
            return;
        }
        let name = st(b, "name").to_string();
        let input = b.get("input").cloned().unwrap_or(Value::Null);
        if name == "TodoWrite" {
            let entries = input["todos"]
                .as_array()
                .map(|a| a.iter().map(|e| (st(e, "content").to_string(), st(e, "status").to_string())).collect())
                .unwrap_or_default();
            self.em.send(AgentEvent::Plan(entries));
            self.skipped.insert(id);
            return;
        }
        let (title, kind) = cc_title(&name, &input, &self.cfg.cwd);
        let detail = summarize_input(&input, &self.cfg.cwd);
        self.em.send(AgentEvent::ToolCall {
            id: id.clone(),
            title,
            kind: kind.into(),
            status: ToolStatus::Running,
            detail,
        });
        self.open_tools.push(id.clone());
        self.tools.insert(id, CcTool { name, input });
    }

    fn on_tool_result(&mut self, b: &Value) {
        let id = st(b, "tool_use_id").to_string();
        let Some(tool) = self.tools.get(&id) else { return };
        let failed = b["is_error"].as_bool().unwrap_or(false);
        let output = match &b["content"] {
            Value::String(s) => s.clone(),
            Value::Array(a) => a
                .iter()
                .map(|c| match st(c, "type") {
                    "text" => st(c, "text").to_string(),
                    "image" => "[image]".to_string(),
                    t => format!("[{t}]"),
                })
                .collect::<Vec<_>>()
                .join("\n"),
            _ => String::new(),
        };
        let diff = if failed { None } else { cc_diff(&tool.name, &tool.input, &self.cfg.cwd) };
        self.open_tools.retain(|t| *t != id);
        self.em.send(AgentEvent::ToolUpdate {
            id,
            status: Some(if failed { ToolStatus::Failed } else { ToolStatus::Done }),
            title: None,
            output: (!output.is_empty()).then(|| clip(&output, MAX_OUTPUT)),
            diff,
        });
    }

    fn on_result(&mut self, v: &Value) {
        if let Some(s) = opt_st(v, "session_id") {
            self.session = Some(s);
        }
        let cancelled = self.cancel_at.is_some();
        let is_error = v["is_error"].as_bool().unwrap_or(false);
        let subtype = st(v, "subtype");
        let mut info = String::new();
        if let Some(ms) = v["duration_ms"].as_f64() {
            info = format!("{:.1} s", ms / 1000.0);
        }
        if let Some(c) = v["total_cost_usd"].as_f64() {
            if !info.is_empty() {
                info.push_str(" · ");
            }
            info.push_str(&format!("{c:.4} $"));
        }
        let reason = if cancelled {
            "cancelled"
        } else if subtype == "success" && !is_error {
            "end_turn"
        } else if subtype == "error_max_turns" {
            "max_turn_requests"
        } else {
            "error"
        };
        if is_error && !cancelled {
            let mut text = st(v, "result").to_string();
            if text.is_empty() {
                text = v["errors"]
                    .as_array()
                    .map(|a| a.iter().filter_map(Value::as_str).collect::<Vec<_>>().join(" ; "))
                    .unwrap_or_default();
            }
            if text.is_empty() {
                text = format!("Claude Code : {subtype}");
            }
            if is_login_text(&text) {
                self.em.send(AgentEvent::NeedsLogin(self.cfg.kind.login_hint().into()));
            }
            self.em.error(text);
        }
        self.end_turn(reason.into(), (!info.is_empty()).then_some(info));
    }

    fn on_stream_event(&mut self, e: &Value) {
        match st(e, "type") {
            "message_start" => {
                self.cur_msg = st(&e["message"], "id").to_string();
                self.thinking_blocks.clear();
            }
            "content_block_start" => {
                if st(&e["content_block"], "type") == "thinking" {
                    self.thinking_blocks.insert(e["index"].as_u64().unwrap_or(0), false);
                }
            }
            "content_block_delta" => {
                let d = &e["delta"];
                match st(d, "type") {
                    "text_delta" => {
                        let t = st(d, "text");
                        if !t.is_empty() {
                            self.streamed.insert(self.cur_msg.clone());
                            self.em.send(AgentEvent::Text(t.to_string()));
                        }
                    }
                    "thinking_delta" => {
                        let t = st(d, "thinking");
                        if !t.is_empty() {
                            if let Some(had) = self.thinking_blocks.get_mut(&e["index"].as_u64().unwrap_or(0)) {
                                *had = true;
                            }
                            self.streamed.insert(self.cur_msg.clone());
                            self.em.send(AgentEvent::Thought(t.to_string()));
                        }
                    }
                    _ => {}
                }
            }
            "content_block_stop" => {
                if self.thinking_blocks.remove(&e["index"].as_u64().unwrap_or(0)) == Some(true) {
                    self.em.send(AgentEvent::Thought("\n".into()));
                }
            }
            _ => {}
        }
    }

    fn on_control_response(&mut self, r: &Value) {
        let id = st(r, "request_id").to_string();
        let ok = st(r, "subtype") == "success";
        let Some(kind) = self.ctl.remove(&id) else { return };
        let err = || {
            let e = st(r, "error");
            if e.is_empty() { "refusé".to_string() } else { e.to_string() }
        };
        match kind {
            Ctl::Init => {
                if !ok {
                    self.em.log(format!("initialize : {}", err()));
                    return;
                }
                let resp = &r["response"];
                if let Some(m) = opt_st(resp, "current_permission_mode") {
                    self.mode = m;
                }
                if let Some(a) = resp["models"].as_array() {
                    if self.model_raw.is_none() {
                        self.model_raw = a.iter().find(|m| st(m, "value") == "default").and_then(|m| opt_st(m, "resolvedModel"));
                    }
                    let list: Vec<(String, String)> = a
                        .iter()
                        .filter_map(|m| {
                            let v = opt_st(m, "value")?;
                            Some((v.clone(), opt_st(m, "displayName").unwrap_or(v)))
                        })
                        .collect();
                    if !list.is_empty() {
                        self.models = list;
                        if !self.models.iter().any(|m| m.0 == "default") {
                            self.models.insert(0, ("default".into(), "Par défaut".into()));
                        }
                    }
                }
                self.ready_if_changed();
            }
            Ctl::Interrupt => {
                if !ok {
                    self.em.log(format!("interruption : {}", err()));
                }
            }
            Ctl::Mode(m) => {
                if ok {
                    self.cfg.mode = Some(m.clone());
                    if self.mode != m {
                        self.mode = m.clone();
                        self.em.send(AgentEvent::ModeChanged(m));
                    }
                    self.ready_if_changed();
                } else {
                    self.em.error(format!("mode {m} : {}", err()));
                }
            }
            Ctl::Model(m) => {
                if ok {
                    if let Some(raw) = opt_st(&r["response"], "model") {
                        self.em.log(format!("modèle : {raw}"));
                    }
                    self.cfg.model = if m == "default" { None } else { Some(m) };
                    self.ready_if_changed();
                } else {
                    self.em.error(format!("modèle {m} : {}", err()));
                }
            }
        }
    }

    /// Pas de réponse à l'interruption : le process est tué et relancé sur la même séance.
    fn respawn(&mut self) {
        self.em.log("Claude Code ne répond pas à l'interruption : relancé sur la même séance");
        if let Some(mut p) = self.proc.take() {
            p.stop(Duration::from_millis(100));
        }
        self.generation += 1;
        self.ctl.clear();
        if let Some(s) = &self.session {
            self.cfg.resume = Some(s.clone());
        }
        let queued = std::mem::take(&mut self.queue);
        self.end_turn("cancelled".into(), None);
        self.queue = queued;
        if let Err(e) = self.start() {
            self.em.error(e);
            self.dead = true;
        }
    }
}

/// Le titre et le genre (ACP) d'un appel d'outil de Claude Code.
fn cc_title(name: &str, input: &Value, cwd: &Path) -> (String, &'static str) {
    let path = || {
        let p = opt_st(input, "file_path").or_else(|| opt_st(input, "notebook_path")).or_else(|| opt_st(input, "path"));
        p.map(|p| rel_path(&p, cwd)).unwrap_or_default()
    };
    let with = |label: &str, what: String| if what.is_empty() { label.to_string() } else { format!("{label} {what}") };
    match name {
        "Bash" => (format!("Bash: {}", one_line(st(input, "command"), 80)), "execute"),
        "Read" => (with("Read", path()), "read"),
        "Edit" | "MultiEdit" | "Write" | "NotebookEdit" => (with(name, path()), "edit"),
        "Glob" | "Grep" => (with(name, one_line(st(input, "pattern"), 80)), "search"),
        "WebFetch" => (with(name, one_line(st(input, "url"), 80)), "fetch"),
        "WebSearch" => (with(name, one_line(st(input, "query"), 80)), "fetch"),
        "Task" | "Agent" => (format!("Task: {}", one_line(st(input, "description"), 80)), "think"),
        "ExitPlanMode" => ("Plan prêt".into(), "switch_mode"),
        _ => {
            if let Some(rest) = name.strip_prefix("mcp__") {
                let (srv, tool) = rest.split_once("__").unwrap_or(("mcp", rest));
                let srv = if srv == "korinstudio" { "IDE" } else { srv };
                (format!("{srv} · {tool}"), if IDE_READ_TOOLS.contains(&tool) { "read" } else { "other" })
            } else {
                (name.to_string(), "other")
            }
        }
    }
}

/// Le diff d'une modification de fichier faite par Claude Code (d'après l'entrée de l'outil).
fn cc_diff(name: &str, input: &Value, cwd: &Path) -> Option<(String, String, String)> {
    let path = rel_path(&opt_st(input, "file_path")?, cwd);
    match name {
        "Edit" => Some((path, st(input, "old_string").into(), st(input, "new_string").into())),
        "Write" => Some((path, String::new(), st(input, "content").into())),
        "MultiEdit" => {
            let edits = input["edits"].as_array()?;
            let old = edits.iter().map(|e| st(e, "old_string")).collect::<Vec<_>>().join("\n");
            let new = edits.iter().map(|e| st(e, "new_string")).collect::<Vec<_>>().join("\n");
            Some((path, old, new))
        }
        _ => None,
    }
}

impl Backend for ClaudeCode {
    fn gen_(&self) -> u64 {
        self.generation
    }

    fn proc_mut(&mut self) -> &mut Option<Proc> {
        &mut self.proc
    }

    fn dead(&self) -> bool {
        self.dead
    }

    fn deadline(&self) -> Option<Instant> {
        self.cancel_at
    }

    fn on_timeout(&mut self) {
        if self.cancel_at.is_some_and(|d| Instant::now() >= d) {
            self.respawn();
        }
    }

    fn on_cmd(&mut self, c: AgentCmd) {
        match c {
            AgentCmd::Prompt(t) => {
                if self.turn {
                    self.queue.push_back(t);
                    self.em.log("demande mise en attente : un tour est en cours");
                } else {
                    self.send_user(t);
                }
            }
            AgentCmd::Cancel => {
                self.queue.clear();
                if self.turn && self.cancel_at.is_none() {
                    self.cancel_at = Some(Instant::now() + CANCEL_GRACE);
                    self.control(json!({"subtype": "interrupt"}), Ctl::Interrupt);
                }
            }
            AgentCmd::Permission { .. } => {
                self.em.log("Claude Code : les autorisations passent par l'outil approve de l'IDE");
            }
            AgentCmd::SetMode(m) => {
                self.control(json!({"subtype": "set_permission_mode", "mode": m}), Ctl::Mode(m));
            }
            AgentCmd::SetModel(m) => {
                self.control(json!({"subtype": "set_model", "model": m}), Ctl::Model(m));
            }
            AgentCmd::Shutdown => {}
        }
    }

    fn on_line(&mut self, line: &str) {
        let t = line.trim();
        if t.is_empty() {
            return;
        }
        let v: Value = match serde_json::from_str(t) {
            Ok(v @ Value::Object(_)) => v,
            _ => {
                self.em.log(format!("(sortie) {}", clip(t, 2000)));
                return;
            }
        };
        let sub = v.get("parent_tool_use_id").is_some_and(|p| !p.is_null());
        if let Some(s) = opt_st(&v, "session_id") {
            self.session = Some(s);
        }
        match st(&v, "type") {
            "system" if st(&v, "subtype") == "status" => {
                // (le mode peut changer de lui-même : ExitPlanMode…)
                if let Some(m) = opt_st(&v, "permissionMode") {
                    if m != self.mode {
                        self.mode = m.clone();
                        self.cfg.mode = Some(m.clone());
                        self.em.send(AgentEvent::ModeChanged(m));
                    }
                }
            }
            "system" => {
                if st(&v, "subtype") != "init" {
                    return;
                }
                if let Some(s) = opt_st(&v, "session_id") {
                    self.session = Some(s);
                }
                self.model_raw = opt_st(&v, "model");
                if let Some(m) = opt_st(&v, "permissionMode") {
                    self.mode = m;
                }
                if !self.cfg.mcp_url.is_empty() {
                    let status = v["mcp_servers"]
                        .as_array()
                        .and_then(|a| a.iter().find(|s| st(s, "name") == "korinstudio"))
                        .map(|s| st(s, "status").to_string());
                    match status.as_deref() {
                        Some("connected") => {}
                        Some(s) => self.em.log(format!("attention : le serveur MCP de l'IDE n'est pas connecté ({s})")),
                        None => self.em.log("attention : Claude Code n'a pas le serveur MCP de l'IDE"),
                    }
                }
                self.ready_if_changed();
            }
            "stream_event" => {
                if !sub {
                    self.on_stream_event(&v["event"]);
                }
            }
            "assistant" => {
                let m = &v["message"];
                let streamed = self.streamed.contains(st(m, "id"));
                if let Some(e) = opt_st(&v, "error") {
                    self.em.log(format!("Claude Code : {e}"));
                }
                for b in m["content"].as_array().into_iter().flatten() {
                    match st(b, "type") {
                        "text" if !streamed && !sub => {
                            let t = st(b, "text");
                            if !t.is_empty() {
                                self.em.send(AgentEvent::Text(t.to_string()));
                            }
                        }
                        "thinking" if !streamed && !sub => {
                            let t = st(b, "thinking");
                            if !t.is_empty() {
                                self.em.send(AgentEvent::Thought(format!("{t}\n")));
                            }
                        }
                        "tool_use" => self.on_tool_use(b),
                        _ => {}
                    }
                }
            }
            "user" => {
                for b in v["message"]["content"].as_array().into_iter().flatten() {
                    if st(b, "type") == "tool_result" {
                        self.on_tool_result(b);
                    }
                }
            }
            "result" => self.on_result(&v),
            "control_response" => self.on_control_response(&v["response"]),
            "control_request" => {
                // (rien n'est demandé à l'IDE par ce canal : les autorisations passent par MCP)
                let id = st(&v, "request_id").to_string();
                self.em.log(format!("Claude Code demande {} : refusé", st(&v["request"], "subtype")));
                self.write(&json!({"type": "control_response", "response": {
                    "subtype": "error", "request_id": id, "error": "non pris en charge par KorinStudio"}}));
            }
            "control_cancel_request" | "keep_alive" | "rate_limit_event" | "tool_progress" | "auth_status"
            | "active_goal" | "autocompact_state" => {}
            other => self.em.log(format!("Claude Code : message « {other} » ignoré")),
        }
    }

    fn on_eof(&mut self) {
        if self.turn {
            self.em.error("Claude Code s'est arrêté pendant le tour (voir le journal)");
            self.end_turn("error".into(), None);
        }
    }
}

// --- essais ----------------------------------------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn titles() {
        let cwd = Path::new("/w");
        assert_eq!(cc_title("Bash", &json!({"command": "ls -la\npwd"}), cwd).0, "Bash: ls -la …");
        assert_eq!(cc_title("Edit", &json!({"file_path": "/w/src/main.rs"}), cwd).0, "Edit src/main.rs");
        assert_eq!(cc_title("mcp__korinstudio__create_project", &json!({}), cwd).0, "IDE · create_project");
        assert_eq!(summarize_input(&json!({"path": "/w/a.txt", "x": 1}), cwd), "a.txt");
        assert_eq!(summarize_input(&json!({"command": ["git", "status"]}), cwd), "git status");
    }

    #[test]
    fn auth_errors() {
        assert!(Acp::is_auth_error(&json!({"code": -32002, "message": "x"})));
        assert!(Acp::is_auth_error(&json!({"code": -32603, "message": "Pas de clé d'API de Claude"})));
        assert!(!Acp::is_auth_error(&json!({"code": -32603, "message": "réseau"})));
        assert!(is_login_text("Invalid API key · Please run /login"));
    }

    #[test]
    fn shebangs() {
        let d = std::env::temp_dir().join(format!("ks-shebang-{}", std::process::id()));
        std::fs::create_dir_all(&d).unwrap();
        let f = d.join("s");
        std::fs::write(&f, "#!/bin/sh -e\necho\n").unwrap();
        let (i, a) = shebang(&f).unwrap();
        assert_eq!(i, PathBuf::from("/bin/sh"));
        assert_eq!(a, vec!["-e".to_string()]);
        std::fs::write(&f, "\x7fELF").unwrap();
        assert!(shebang(&f).is_none());
        let _ = std::fs::remove_dir_all(&d);
    }
}
