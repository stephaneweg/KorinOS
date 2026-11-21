// Le copilote : la conversation avec un agent de code (Claude Code, Claude par l'API, Mistral Vibe, MAI), ses
// outils, ses demandes d'accord ; l'agent agit dans l'IDE par le serveur MCP de KorinStudio (mcp.rs).

use crate::agent::{self, AgentCmd, AgentConfig, AgentEvent, AgentHandle, AgentKind, ToolStatus};
use crate::highlight::{self, Lang};
use crate::icons::{self, Icon};
use crate::mcp::ToolResult;
use crate::theme::Palette;
use egui::text::{LayoutJob, TextFormat};
use egui::{Color32, FontId, RichText, Sense, Stroke, Ui, vec2};
use std::collections::HashSet;
use std::path::PathBuf;
use std::sync::mpsc::Sender;

pub enum PermSource {
    Acp(u64),
    Mcp { reply: Sender<ToolResult>, input: serde_json::Value, tool: String },
}

pub enum Item {
    User(String),
    Assistant(String),
    Thought(String, bool),
    Tool { id: String, title: String, kind: String, status: ToolStatus, detail: String, output: String, diff: Option<(String, String, String)>, open: bool },
    Plan(Vec<(String, String)>),
    Permission { src: Option<PermSource>, title: String, detail: String, options: Vec<(String, String, String)>, answer: Option<String> },
    Notice(String, bool), // texte, erreur ?
    TurnInfo(String),
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Idle,
    Starting,
    Ready,
    Busy,
}

pub struct Copilot {
    pub kind: AgentKind,
    handle: Option<AgentHandle>,
    pub items: Vec<Item>,
    pub input: String,
    pub status: Status,
    pub session_id: Option<String>,
    pub agent_name: String,
    pub models: Vec<(String, String)>,
    pub current_model: Option<String>,
    pub modes: Vec<(String, String)>,
    pub current_mode: Option<String>,
    pub allow_always: HashSet<String>,
    pub log: Vec<String>,
    scroll_bottom: bool,
    pub include_context: bool,
    queued: Vec<String>,
    pub focus_input: bool,
    pub wanted_mode: Option<String>,
    pub last_cwd: Option<PathBuf>,
}

/// Ce que l'app donne au copilote pour lancer un agent.
pub struct Launch {
    pub cwd: PathBuf,
    pub mcp_url: String,
    pub mcp_token: String,
    pub studio_exe: PathBuf,
    pub hint: String,
}

impl Copilot {
    pub fn new(kind: AgentKind) -> Copilot {
        Copilot {
            kind,
            handle: None,
            items: Vec::new(),
            input: String::new(),
            status: Status::Idle,
            session_id: None,
            agent_name: String::new(),
            models: Vec::new(),
            current_model: None,
            modes: Vec::new(),
            current_mode: None,
            allow_always: HashSet::new(),
            log: Vec::new(),
            scroll_bottom: false,
            include_context: true,
            queued: Vec::new(),
            focus_input: false,
            wanted_mode: None,
            last_cwd: None,
        }
    }

    pub fn busy(&self) -> bool {
        matches!(self.status, Status::Busy | Status::Starting)
    }

    pub fn running(&self) -> bool {
        self.handle.is_some()
    }

    /// Une nouvelle conversation (arrête l'agent ; il repart au prochain message).
    pub fn reset(&mut self) {
        self.stop_agent();
        self.items.clear();
        self.session_id = None;
        self.allow_always.clear();
        self.queued.clear();
    }

    pub fn stop_agent(&mut self) {
        if let Some(h) = self.handle.take() {
            let _ = h.tx.send(AgentCmd::Shutdown);
        }
        // les demandes d'accord en attente sont refusées
        for it in &mut self.items {
            if let Item::Permission { src, answer, .. } = it {
                if answer.is_none() {
                    if let Some(PermSource::Mcp { reply, .. }) = src.take() {
                        let _ = reply.send(ToolResult::text(deny_json("session arrêtée")));
                    }
                    *answer = Some("annulée".into());
                }
            }
        }
        self.status = Status::Idle;
    }

    pub fn set_kind(&mut self, k: AgentKind) {
        if k != self.kind {
            self.reset();
            self.kind = k;
            self.models.clear();
            self.modes.clear();
            self.current_mode = None;
            self.current_model = None;
        }
    }

    fn ensure_started(&mut self, l: &Launch, ctx: &egui::Context) -> bool {
        if self.handle.is_some() {
            return true;
        }
        if self.kind.find().is_none() {
            self.items.push(Item::Notice(not_installed(self.kind), true));
            return false;
        }
        let cfg = AgentConfig {
            kind: self.kind,
            cwd: l.cwd.clone(),
            mcp_url: l.mcp_url.clone(),
            mcp_token: l.mcp_token.clone(),
            studio_exe: l.studio_exe.clone(),
            system_hint: l.hint.clone(),
            model: self.current_model.clone(),
            mode: self.wanted_mode.clone().or_else(|| self.current_mode.clone()),
            resume: self.session_id.clone().filter(|s| !s.is_empty()),
        };
        match agent::spawn(cfg, ctx.clone()) {
            Ok(h) => {
                self.handle = Some(h);
                self.status = Status::Starting;
                self.last_cwd = Some(l.cwd.clone());
                true
            }
            Err(e) => {
                self.items.push(Item::Notice(format!("{} ne démarre pas : {e}", self.kind.label()), true));
                false
            }
        }
    }

    pub fn send(&mut self, text: String, context: Option<String>, l: &Launch, ctx: &egui::Context) {
        let text = text.trim().to_string();
        if text.is_empty() {
            return;
        }
        self.items.push(Item::User(text.clone()));
        self.scroll_bottom = true;
        if !self.ensure_started(l, ctx) {
            return;
        }
        let full = match context {
            Some(c) if self.include_context => format!("{c}\n\n{text}"),
            _ => text,
        };
        if self.status == Status::Busy || self.status == Status::Starting {
            self.queued.push(full);
            if self.status == Status::Busy {
                self.items.push(Item::Notice("Message mis en attente : il partira à la fin du tour.".into(), false));
            }
            return;
        }
        self.prompt(full);
    }

    fn prompt(&mut self, text: String) {
        if let Some(h) = &self.handle {
            if h.tx.send(AgentCmd::Prompt(text)).is_ok() {
                self.status = Status::Busy;
            }
        }
    }

    pub fn cancel(&mut self) {
        if let Some(h) = &self.handle {
            let _ = h.tx.send(AgentCmd::Cancel);
        }
        self.queued.clear();
        for it in &mut self.items {
            if let Item::Permission { src, answer, .. } = it {
                if answer.is_none() {
                    if let Some(PermSource::Mcp { reply, .. }) = src.take() {
                        let _ = reply.send(ToolResult::text(deny_json("interrompu par l'utilisateur")));
                    }
                    *answer = Some("annulée".into());
                }
            }
        }
    }

    pub fn set_mode(&mut self, m: String) {
        self.wanted_mode = Some(m.clone());
        if let Some(h) = &self.handle {
            let _ = h.tx.send(AgentCmd::SetMode(m.clone()));
        }
        self.current_mode = Some(m);
    }

    pub fn set_model(&mut self, m: String) {
        if let Some(h) = &self.handle {
            let _ = h.tx.send(AgentCmd::SetModel(m.clone()));
        }
        self.current_model = Some(m);
    }

    /// Une demande d'accord venue du serveur MCP (l'outil « approve » de Claude Code).
    pub fn mcp_permission(&mut self, tool: String, input: serde_json::Value, reply: Sender<ToolResult>) {
        let base = tool.split("__").last().unwrap_or(&tool).to_string();
        if self.allow_always.contains(&tool) || self.current_mode.as_deref() == Some("bypassPermissions") {
            let _ = reply.send(ToolResult::text(allow_json(&input)));
            return;
        }
        let (title, detail) = describe_tool(&tool, &input);
        self.items.push(Item::Permission {
            src: Some(PermSource::Mcp { reply, input, tool: tool.clone() }),
            title,
            detail,
            options: vec![
                ("allow".into(), "Autoriser".into(), "allow_once".into()),
                ("always".into(), format!("Toujours ({base})"), "allow_always".into()),
                ("deny".into(), "Refuser".into(), "reject_once".into()),
            ],
            answer: None,
        });
        self.scroll_bottom = true;
    }

    pub fn pending_permission(&self) -> bool {
        self.items.iter().any(|i| matches!(i, Item::Permission { answer: None, .. }))
    }

    /// Les évènements de l'agent ; rend les chemins que l'agent a touchés (pour recharger les éditeurs).
    pub fn poll(&mut self) {
        let mut evs = Vec::new();
        let mut dead = false;
        if let Some(h) = &self.handle {
            loop {
                match h.rx.try_recv() {
                    Ok(e) => evs.push(e),
                    Err(std::sync::mpsc::TryRecvError::Empty) => break,
                    Err(std::sync::mpsc::TryRecvError::Disconnected) => {
                        dead = true;
                        break;
                    }
                }
            }
        }
        for e in evs {
            self.event(e);
        }
        if dead {
            self.handle = None;
            if self.status != Status::Idle {
                self.status = Status::Idle;
            }
        }
    }

    fn event(&mut self, e: AgentEvent) {
        self.scroll_bottom = true;
        match e {
            AgentEvent::Ready { session_id, agent_name, models, current_model, modes, current_mode } => {
                self.session_id = Some(session_id);
                self.agent_name = agent_name;
                if !models.is_empty() {
                    self.models = models;
                }
                if current_model.is_some() {
                    self.current_model = current_model;
                }
                if !modes.is_empty() {
                    self.modes = modes;
                }
                if let Some(w) = self.wanted_mode.clone() {
                    if current_mode.as_deref() != Some(&w) && self.modes.iter().any(|(id, _)| *id == w) {
                        if let Some(h) = &self.handle {
                            let _ = h.tx.send(AgentCmd::SetMode(w.clone()));
                        }
                    }
                    self.current_mode = Some(w);
                } else {
                    self.current_mode = current_mode;
                }
                if self.status == Status::Starting {
                    self.status = Status::Ready;
                    if !self.queued.is_empty() {
                        let q = self.queued.remove(0);
                        self.prompt(q);
                    }
                }
            }
            AgentEvent::Text(t) => match self.items.last_mut() {
                Some(Item::Assistant(s)) => s.push_str(&t),
                _ => self.items.push(Item::Assistant(t)),
            },
            AgentEvent::Thought(t) => match self.items.last_mut() {
                Some(Item::Thought(s, _)) => s.push_str(&t),
                _ => self.items.push(Item::Thought(t, false)),
            },
            AgentEvent::ToolCall { id, title, kind, status, detail } => {
                if let Some(Item::Tool { title: t, status: s, detail: d, kind: k, .. }) = self.items.iter_mut().rev().find(|i| matches!(i, Item::Tool { id: i2, .. } if *i2 == id)) {
                    *t = title;
                    *s = status;
                    *k = kind;
                    if !detail.is_empty() {
                        *d = detail;
                    }
                } else {
                    self.items.push(Item::Tool { id, title, kind, status, detail, output: String::new(), diff: None, open: false });
                }
            }
            AgentEvent::ToolUpdate { id, status, title, output, diff } => {
                if let Some(Item::Tool { status: s, title: t, output: o, diff: d, .. }) = self.items.iter_mut().rev().find(|i| matches!(i, Item::Tool { id: i2, .. } if *i2 == id)) {
                    if let Some(st) = status {
                        *s = st;
                    }
                    if let Some(tt) = title {
                        *t = tt;
                    }
                    if let Some(oo) = output {
                        if o.len() < 200_000 {
                            o.push_str(&oo);
                        }
                    }
                    if diff.is_some() {
                        *d = diff;
                    }
                }
            }
            AgentEvent::Plan(p) => {
                if let Some(pos) = self.items.iter().rposition(|i| matches!(i, Item::Plan(_))) {
                    if !self.items[pos + 1..].iter().any(|i| matches!(i, Item::User(_))) {
                        self.items[pos] = Item::Plan(p);
                        return;
                    }
                }
                self.items.push(Item::Plan(p));
            }
            AgentEvent::Permission { req, title, detail, options } => {
                self.items.push(Item::Permission { src: Some(PermSource::Acp(req)), title, detail, options, answer: None });
            }
            AgentEvent::TurnEnd { stop_reason, info } => {
                for it in &mut self.items {
                    if let Item::Permission { src, answer, .. } = it {
                        if answer.is_none() && matches!(src, Some(PermSource::Acp(_))) {
                            *src = None;
                            *answer = Some("annulée (fin du tour)".into());
                        }
                    }
                    if let Item::Tool { status, .. } = it {
                        if matches!(status, ToolStatus::Pending | ToolStatus::Running) {
                            *status = if stop_reason == "cancelled" { ToolStatus::Failed } else { ToolStatus::Done };
                        }
                    }
                }
                let mut s = match stop_reason.as_str() {
                    "end_turn" | "success" => String::new(),
                    "cancelled" => "Interrompu".into(),
                    "max_tokens" => "Arrêt : limite de jetons".into(),
                    "refusal" => "Le modèle a refusé".into(),
                    other => other.to_string(),
                };
                if let Some(i) = info {
                    if !s.is_empty() {
                        s.push_str(" · ");
                    }
                    s.push_str(&i);
                }
                if !s.is_empty() {
                    self.items.push(Item::TurnInfo(s));
                }
                self.status = Status::Ready;
                if !self.queued.is_empty() {
                    let q = self.queued.remove(0);
                    self.prompt(q);
                }
            }
            AgentEvent::ModeChanged(m) => self.current_mode = Some(m),
            AgentEvent::NeedsLogin(m) => {
                self.items.push(Item::Notice(m, true));
                if self.status == Status::Starting {
                    // sans connexion, l'agent n'a pas de séance : il repartira au prochain message
                    if let Some(h) = self.handle.take() {
                        let _ = h.tx.send(AgentCmd::Shutdown);
                    }
                    self.session_id = None;
                    self.queued.clear();
                    self.status = Status::Idle;
                }
            }
            AgentEvent::Error(m) => {
                self.items.push(Item::Notice(m, true));
                if self.status == Status::Starting {
                    self.status = Status::Ready;
                }
            }
            AgentEvent::Log(l) => {
                if self.log.len() > 5000 {
                    self.log.drain(..1000);
                }
                self.log.push(l);
            }
            AgentEvent::Exited(code) => {
                if self.status != Status::Idle {
                    self.items.push(Item::Notice(
                        format!("{} s'est arrêté{}. Le prochain message le relance.", self.kind.label(), code.map(|c| format!(" (code {c})")).unwrap_or_default()),
                        code.is_some_and(|c| c != 0),
                    ));
                }
                self.handle = None;
                self.status = Status::Idle;
                self.queued.clear();
            }
        }
    }

    fn answer(&mut self, idx: usize, option: Option<String>) {
        let Some(Item::Permission { src, answer, options, .. }) = self.items.get_mut(idx) else { return };
        let label = option.as_ref().and_then(|o| options.iter().find(|(id, _, _)| id == o)).map(|(_, l, _)| l.clone()).unwrap_or_else(|| "annulée".into());
        *answer = Some(label);
        match src.take() {
            Some(PermSource::Acp(req)) => {
                if let Some(h) = &self.handle {
                    let _ = h.tx.send(AgentCmd::Permission { req, option });
                }
            }
            Some(PermSource::Mcp { reply, input, tool }) => {
                let text = match option.as_deref() {
                    Some("allow") => allow_json(&input),
                    Some("always") => {
                        self.allow_always.insert(tool);
                        allow_json(&input)
                    }
                    _ => deny_json("refusé par l'utilisateur"),
                };
                let _ = reply.send(ToolResult::text(text));
            }
            None => {}
        }
    }
}

fn allow_json(input: &serde_json::Value) -> String {
    serde_json::json!({"behavior": "allow", "updatedInput": input}).to_string()
}

fn deny_json(msg: &str) -> String {
    serde_json::json!({"behavior": "deny", "message": msg}).to_string()
}

fn not_installed(k: AgentKind) -> String {
    match k {
        AgentKind::ClaudeCode => "Claude Code n'est pas installé : installez le paquet claude-code (Logiciels, ou « pkg install claude-code »), puis lancez « claude » une fois dans le Terminal pour vous connecter.".into(),
        AgentKind::ClaudeApi => "claude-acp n'est pas installé (paquet python) : il faut aussi une clé d'API, « claude-acp --set-key ».".into(),
        AgentKind::Vibe => "Mistral Vibe (vibe-acp) n'est pas installé (paquet python).".into(),
        AgentKind::Mai => "mai-acp n'est pas installé (paquet python).".into(),
    }
}

/// Le titre et le détail d'une demande d'accord de Claude Code.
fn describe_tool(tool: &str, input: &serde_json::Value) -> (String, String) {
    let g = |k: &str| input.get(k).and_then(|v| v.as_str()).unwrap_or("").to_string();
    match tool {
        "Bash" => ("Lancer une commande".into(), g("command")),
        "Edit" | "MultiEdit" => (format!("Modifier {}", g("file_path")), g("old_string").lines().take(6).collect::<Vec<_>>().join("\n")),
        "Write" => (format!("Écrire {}", g("file_path")), g("content").lines().take(8).collect::<Vec<_>>().join("\n")),
        "WebFetch" => ("Lire une page web".into(), g("url")),
        "WebSearch" => ("Chercher sur le web".into(), g("query")),
        t if t.starts_with("mcp__korinstudio__") => {
            let name = &t["mcp__korinstudio__".len()..];
            (format!("IDE · {name}"), serde_json::to_string_pretty(input).unwrap_or_default().chars().take(600).collect())
        }
        t => (format!("Utiliser {t}"), serde_json::to_string_pretty(input).unwrap_or_default().chars().take(600).collect()),
    }
}

// ---------------------------------------------------------------------------------------------------------------
// L'affichage

pub struct CopilotUi<'a> {
    pub pal: &'a Palette,
    pub code_size: f32,
    pub context_label: Option<String>,
}

pub enum CopilotAction {
    Send(String),
    OpenFile(String, usize),
    Insert(String),
    NewChat,
    ChangeKind(AgentKind),
    ShowLog,
}

pub fn show(ui: &mut Ui, cp: &mut Copilot, cu: &CopilotUi) -> Vec<CopilotAction> {
    let pal = cu.pal;
    let mut acts = Vec::new();
    // l'en-tête
    ui.horizontal(|ui| {
        ui.set_height(34.0);
        ui.add_space(8.0);
        icons::inline(ui, Icon::Sparkle, 16.0, pal.accent);
        ui.label(RichText::new("COPILOTE").font(crate::theme::bold(11.0)).color(pal.fg_dim));
        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
            ui.add_space(6.0);
            let mb = egui::containers::menu::MenuButton::from_button(egui::Button::new("").frame(false).min_size(vec2(24.0, 24.0))).ui(ui, |ui| {
                if ui.button("Journal de l'agent").clicked() {
                    acts.push(CopilotAction::ShowLog);
                }
                ui.checkbox(&mut cp.include_context, "Joindre le fichier actif et la sélection");
            });
            icons::paint(ui.painter(), egui::Rect::from_center_size(mb.0.rect.center(), vec2(16.0, 16.0)), Icon::Ellipsis, pal.fg);
            if icons::button(ui, Icon::Plus, 24.0, "Nouvelle conversation").clicked() {
                acts.push(CopilotAction::NewChat);
            }
            let mut k = cp.kind;
            egui::ComboBox::from_id_salt("ks-agent-kind").selected_text(k.label()).width(140.0).show_ui(ui, |ui| {
                for a in AgentKind::all() {
                    let installed = a.find().is_some();
                    let txt = if installed { a.label().to_string() } else { format!("{} (absent)", a.label()) };
                    ui.selectable_value(&mut k, *a, txt);
                }
            });
            if k != cp.kind {
                acts.push(CopilotAction::ChangeKind(k));
            }
        });
    });
    let sep = ui.cursor().min.y;
    ui.painter().hline(ui.max_rect().x_range(), sep, Stroke::new(1.0, pal.border));

    // la saisie, en bas
    let input_h = 118.0;
    let avail = ui.available_rect_before_wrap();
    let list_rect = egui::Rect::from_min_max(avail.min, egui::pos2(avail.max.x, avail.max.y - input_h));
    let input_rect = egui::Rect::from_min_max(egui::pos2(avail.min.x, avail.max.y - input_h), avail.max);

    let mut list_ui = ui.new_child(egui::UiBuilder::new().max_rect(list_rect).layout(egui::Layout::top_down(egui::Align::Min)));
    egui::ScrollArea::vertical().id_salt("ks-chat").auto_shrink([false, false]).stick_to_bottom(true).show(&mut list_ui, |ui| {
        ui.add_space(8.0);
        let w = ui.available_width() - 16.0;
        if cp.items.is_empty() {
            welcome(ui, cp, pal, &mut acts);
        }
        let mut answer: Option<(usize, Option<String>)> = None;
        let n = cp.items.len();
        for i in 0..n {
            ui.horizontal(|ui| {
                ui.add_space(8.0);
                ui.vertical(|ui| {
                    ui.set_width(w);
                    match &mut cp.items[i] {
                        Item::User(t) => {
                            egui::Frame::new().fill(pal.chat_user_bg).corner_radius(6.0).inner_margin(egui::Margin::symmetric(10, 8)).show(ui, |ui| {
                                ui.set_width(w - 20.0);
                                ui.horizontal(|ui| {
                                    icons::inline(ui, Icon::Person, 14.0, pal.fg_dim);
                                    ui.label(RichText::new("Vous").font(crate::theme::bold(12.0)));
                                });
                                ui.add(egui::Label::new(t.as_str()).wrap());
                            });
                        }
                        Item::Assistant(t) => {
                            ui.horizontal(|ui| {
                                icons::inline(ui, Icon::Sparkle, 14.0, pal.accent);
                                ui.label(RichText::new(cp.kind.label()).font(crate::theme::bold(12.0)));
                            });
                            markdown(ui, t, pal, cu.code_size, &mut acts);
                        }
                        Item::Thought(t, open) => {
                            let r = ui.add(egui::Label::new(RichText::new(if *open { "▾ Réflexion" } else { "▸ Réflexion" }).color(pal.fg_faint).italics()).sense(Sense::click()));
                            if r.clicked() {
                                *open = !*open;
                            }
                            if *open {
                                ui.add(egui::Label::new(RichText::new(t.as_str()).color(pal.fg_faint).italics().size(12.0)).wrap());
                            }
                        }
                        Item::Tool { title, kind: _, status, detail, output, diff, open, .. } => {
                            tool_card(ui, title, *status, detail, output, diff, open, pal, cu.code_size, &mut acts);
                        }
                        Item::Plan(p) => {
                            egui::Frame::new().stroke(Stroke::new(1.0, pal.border)).corner_radius(6.0).inner_margin(8).show(ui, |ui| {
                                ui.set_width(w - 18.0);
                                ui.label(RichText::new("Plan").font(crate::theme::bold(12.0)));
                                for (e, s) in p.iter() {
                                    let (mark, col) = match s.as_str() {
                                        "completed" => ("✔", pal.success),
                                        "in_progress" => ("◐", pal.accent),
                                        _ => ("○", pal.fg_faint),
                                    };
                                    ui.horizontal_wrapped(|ui| {
                                        ui.label(RichText::new(mark).color(col));
                                        let t = RichText::new(e.as_str());
                                        ui.label(if s == "completed" { t.strikethrough().color(pal.fg_dim) } else { t });
                                    });
                                }
                            });
                        }
                        Item::Permission { title, detail, options, answer: ans, .. } => {
                            let pending = ans.is_none();
                            egui::Frame::new()
                                .stroke(Stroke::new(1.0, if pending { pal.warning } else { pal.border }))
                                .corner_radius(6.0)
                                .inner_margin(10)
                                .show(ui, |ui| {
                                    ui.set_width(w - 22.0);
                                    ui.horizontal(|ui| {
                                        icons::inline(ui, Icon::Warning, 14.0, pal.warning);
                                        ui.label(RichText::new(title.as_str()).font(crate::theme::bold(12.5)));
                                    });
                                    if !detail.is_empty() {
                                        code_block(ui, detail, Lang::Plain, pal, cu.code_size - 1.0, false, &mut acts);
                                    }
                                    match ans {
                                        None => {
                                            ui.horizontal_wrapped(|ui| {
                                                for (id, label, kind) in options.iter() {
                                                    let primary = kind == "allow_once";
                                                    let b = if primary {
                                                        egui::Button::new(RichText::new(label.as_str()).color(pal.accent_fg)).fill(pal.accent)
                                                    } else {
                                                        egui::Button::new(label.as_str())
                                                    };
                                                    if ui.add(b).clicked() {
                                                        answer = Some((i, Some(id.clone())));
                                                    }
                                                }
                                            });
                                        }
                                        Some(a) => {
                                            ui.label(RichText::new(format!("→ {a}")).color(pal.fg_dim));
                                        }
                                    }
                                });
                        }
                        Item::Notice(t, err) => {
                            let col = if *err { pal.error } else { pal.fg_dim };
                            ui.horizontal_wrapped(|ui| {
                                icons::inline(ui, if *err { Icon::Error } else { Icon::Info }, 14.0, col);
                                ui.add(egui::Label::new(RichText::new(t.as_str()).color(col)).wrap());
                            });
                        }
                        Item::TurnInfo(t) => {
                            ui.label(RichText::new(t.as_str()).color(pal.fg_faint).size(11.0));
                        }
                    }
                });
            });
            ui.add_space(8.0);
        }
        if cp.busy() {
            ui.horizontal(|ui| {
                ui.add_space(8.0);
                let ph = icons::spinner_phase(ui);
                icons::inline(ui, Icon::Spinner(ph), 14.0, pal.accent);
                ui.label(RichText::new(if cp.status == Status::Starting { format!("Démarrage de {}…", cp.kind.label()) } else if cp.pending_permission() { "En attente de votre accord…".into() } else { "Au travail…".into() }).color(pal.fg_dim));
            });
        }
        if let Some((i, o)) = answer {
            cp.answer(i, o);
        }
        if cp.scroll_bottom {
            ui.scroll_to_cursor(Some(egui::Align::Max));
            cp.scroll_bottom = false;
        }
        ui.add_space(12.0);
    });

    // la zone de saisie
    let mut iu = ui.new_child(egui::UiBuilder::new().max_rect(input_rect.shrink2(vec2(8.0, 6.0))).layout(egui::Layout::top_down(egui::Align::Min)));
    egui::Frame::new().fill(pal.input_bg).stroke(Stroke::new(1.0, pal.input_border)).corner_radius(6.0).inner_margin(egui::Margin::symmetric(8, 6)).show(&mut iu, |ui| {
        let w = ui.available_width();
        if let Some(c) = &cu.context_label {
            if cp.include_context {
                ui.horizontal(|ui| {
                    egui::Frame::new().fill(pal.chat_user_bg).corner_radius(3.0).inner_margin(egui::Margin::symmetric(5, 1)).show(ui, |ui| {
                        icons::inline(ui, Icon::File, 12.0, pal.fg_dim);
                        ui.label(RichText::new(c.as_str()).size(11.0).color(pal.fg_dim));
                    });
                });
            }
        }
        let id = egui::Id::new("ks-chat-input");
        let te = egui::TextEdit::multiline(&mut cp.input)
            .id(id)
            .hint_text(format!("Demandez à {}… (Entrée : envoyer, Maj+Entrée : à la ligne)", cp.kind.label()))
            .frame(egui::Frame::NONE)
            .desired_rows(2)
            .desired_width(w);
        let r = ui.add(te);
        if cp.focus_input {
            r.request_focus();
            cp.focus_input = false;
        }
        let enter = r.has_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter) && !i.modifiers.shift);
        if enter {
            // la touche Entrée a déjà ajouté une ligne : la retirer
            while cp.input.ends_with('\n') {
                cp.input.pop();
            }
        }
        ui.horizontal(|ui| {
            if !cp.modes.is_empty() {
                let cur = cp.current_mode.clone().unwrap_or_default();
                let label = cp.modes.iter().find(|(id, _)| *id == cur).map(|(_, l)| l.clone()).unwrap_or_else(|| "Mode".into());
                let mut sel = cur.clone();
                egui::ComboBox::from_id_salt("ks-mode").selected_text(RichText::new(label).size(11.5)).show_ui(ui, |ui| {
                    for (id, l) in cp.modes.clone() {
                        ui.selectable_value(&mut sel, id, l);
                    }
                });
                if sel != cur {
                    cp.set_mode(sel);
                }
            }
            if !cp.models.is_empty() {
                let cur = cp.current_model.clone().unwrap_or_default();
                let label = cp.models.iter().find(|(id, _)| *id == cur).map(|(_, l)| l.clone()).unwrap_or_else(|| if cur.is_empty() { "Modèle".into() } else { cur.clone() });
                let mut sel = cur.clone();
                egui::ComboBox::from_id_salt("ks-model").selected_text(RichText::new(label).size(11.5)).show_ui(ui, |ui| {
                    for (id, l) in cp.models.clone() {
                        ui.selectable_value(&mut sel, id, l);
                    }
                });
                if sel != cur {
                    cp.set_model(sel);
                }
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if cp.status == Status::Busy {
                    if icons::button(ui, Icon::Stop, 24.0, "Arrêter (Échap)").clicked() {
                        cp.cancel();
                    }
                } else {
                    let can = !cp.input.trim().is_empty();
                    let b = icons::button(ui, Icon::Send, 24.0, "Envoyer (Entrée)");
                    if (b.clicked() || enter) && can {
                        let t = std::mem::take(&mut cp.input);
                        acts.push(CopilotAction::Send(t));
                    }
                }
            });
        });
    });
    acts
}

fn welcome(ui: &mut Ui, cp: &Copilot, pal: &Palette, acts: &mut Vec<CopilotAction>) {
    ui.add_space(30.0);
    ui.vertical_centered(|ui| {
        let (r, _) = ui.allocate_exact_size(vec2(44.0, 44.0), Sense::hover());
        icons::paint(ui.painter(), r, Icon::Sparkle, pal.accent);
        ui.add_space(6.0);
        ui.label(RichText::new(format!("Copilote · {}", cp.kind.label())).font(crate::theme::bold(16.0)));
        ui.add_space(4.0);
        ui.label(RichText::new("Il agit dans KorinStudio : il crée des projets, ouvre et modifie les fichiers, construit, lance et lit les erreurs.").color(pal.fg_dim));
        if cp.kind.find().is_none() {
            ui.add_space(6.0);
            ui.label(RichText::new(not_installed(cp.kind)).color(pal.warning));
        }
        ui.add_space(14.0);
        for s in [
            "Crée un nouveau projet Rust qui affiche la suite de Fibonacci, construis-le et lance-le",
            "Fais une app Korin en C avec une fenêtre et un compteur de clics",
            "Explique le fichier ouvert",
            "Construis le projet et corrige les erreurs",
        ] {
            let b = egui::Button::new(RichText::new(s).size(12.0)).wrap().min_size(vec2(ui.available_width() - 40.0, 30.0)).stroke(Stroke::new(1.0, pal.border)).corner_radius(6.0);
            if ui.add(b).clicked() {
                acts.push(CopilotAction::Send(s.to_string()));
            }
            ui.add_space(2.0);
        }
    });
    ui.add_space(20.0);
}

#[allow(clippy::too_many_arguments)]
fn tool_card(
    ui: &mut Ui,
    title: &str,
    status: ToolStatus,
    detail: &str,
    output: &str,
    diff: &Option<(String, String, String)>,
    open: &mut bool,
    pal: &Palette,
    code_size: f32,
    acts: &mut Vec<CopilotAction>,
) {
    let w = ui.available_width();
    egui::Frame::new().stroke(Stroke::new(1.0, pal.border)).corner_radius(5.0).inner_margin(egui::Margin::symmetric(8, 5)).show(ui, |ui| {
        ui.set_width(w - 18.0);
        let r = ui
            .horizontal(|ui| {
                let (ic, col) = match status {
                    ToolStatus::Pending | ToolStatus::Running => (Icon::Spinner(icons::spinner_phase(ui)), pal.accent),
                    ToolStatus::Done => (Icon::Check, pal.success),
                    ToolStatus::Failed => (Icon::Error, pal.error),
                };
                icons::inline(ui, ic, 13.0, col);
                let ide = title.starts_with("IDE");
                if ide {
                    icons::inline(ui, Icon::Tool, 12.0, pal.accent);
                }
                ui.add(egui::Label::new(RichText::new(title).size(12.0)).truncate());
            })
            .response;
        let r = ui.interact(r.rect, ui.id().with(("tool", title)), Sense::click());
        if r.clicked() {
            *open = !*open;
        }
        if *open {
            if !detail.is_empty() {
                code_block(ui, detail, Lang::Shell, pal, code_size - 1.5, false, acts);
            }
            if let Some((path, old, new)) = diff {
                ui.label(RichText::new(path.as_str()).size(11.0).color(pal.fg_dim));
                diff_view(ui, old, new, pal, code_size - 1.5);
            }
            if !output.is_empty() {
                let o: String = output.chars().take(6000).collect();
                code_block(ui, &o, Lang::Plain, pal, code_size - 1.5, false, acts);
            }
        }
    });
}

/// Un diff simple, ligne à ligne (préfixe commun, suffixe commun).
fn diff_view(ui: &mut Ui, old: &str, new: &str, pal: &Palette, size: f32) {
    let a: Vec<&str> = old.lines().collect();
    let b: Vec<&str> = new.lines().collect();
    let mut p = 0;
    while p < a.len() && p < b.len() && a[p] == b[p] {
        p += 1;
    }
    let mut s = 0;
    while s < a.len() - p && s < b.len() - p && a[a.len() - 1 - s] == b[b.len() - 1 - s] {
        s += 1;
    }
    let font = FontId::monospace(size);
    egui::Frame::new().fill(pal.code_bg).corner_radius(4.0).inner_margin(6).show(ui, |ui| {
        let mut job = LayoutJob::default();
        let ctx = 2;
        for l in a.iter().take(p).skip(p.saturating_sub(ctx)) {
            job.append(&format!("  {l}\n"), 0.0, TextFormat::simple(font.clone(), pal.fg_dim));
        }
        for l in &a[p..a.len() - s] {
            let mut f = TextFormat::simple(font.clone(), pal.fg);
            f.background = pal.deleted.gamma_multiply(0.25);
            job.append(&format!("- {l}\n"), 0.0, f);
        }
        for l in &b[p..b.len() - s] {
            let mut f = TextFormat::simple(font.clone(), pal.fg);
            f.background = pal.added.gamma_multiply(0.25);
            job.append(&format!("+ {l}\n"), 0.0, f);
        }
        for l in b.iter().skip(b.len() - s).take(ctx) {
            job.append(&format!("  {l}\n"), 0.0, TextFormat::simple(font.clone(), pal.fg_dim));
        }
        job.wrap.max_width = ui.available_width();
        ui.label(job);
    });
}

/// Un bloc de code coloré, avec Copier (et Insérer dans l'éditeur).
pub fn code_block(ui: &mut Ui, code: &str, lang: Lang, pal: &Palette, size: f32, actions: bool, acts: &mut Vec<CopilotAction>) {
    let font = FontId::monospace(size);
    let code = code.trim_end_matches('\n');
    egui::Frame::new().fill(pal.code_bg).stroke(Stroke::new(1.0, pal.border)).corner_radius(4.0).inner_margin(egui::Margin::symmetric(8, 6)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        if actions {
            ui.horizontal(|ui| {
                ui.label(RichText::new(lang.name()).size(10.5).color(pal.fg_faint));
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.small_button("Copier").clicked() {
                        ui.ctx().copy_text(code.to_string());
                    }
                    if ui.small_button("Insérer").on_hover_text("Insérer au curseur de l'éditeur").clicked() {
                        acts.push(CopilotAction::Insert(code.to_string()));
                    }
                });
            });
        }
        let spans = highlight::highlight(lang, code);
        let mut job = LayoutJob::default();
        let mut pos = 0;
        for (r, t) in spans {
            if r.start < pos || r.end > code.len() {
                continue;
            }
            if r.start > pos {
                job.append(&code[pos..r.start], 0.0, TextFormat::simple(font.clone(), pal.tok(highlight::Tok::Text)));
            }
            job.append(&code[r.clone()], 0.0, TextFormat::simple(font.clone(), pal.tok(t)));
            pos = r.end;
        }
        if pos < code.len() {
            job.append(&code[pos..], 0.0, TextFormat::simple(font.clone(), pal.tok(highlight::Tok::Text)));
        }
        job.wrap.max_width = ui.available_width();
        ui.add(egui::Label::new(job).selectable(true));
    });
}

fn lang_of_fence(tag: &str) -> Lang {
    match tag.trim().to_ascii_lowercase().as_str() {
        "rust" | "rs" => Lang::Rust,
        "c" | "h" => Lang::C,
        "cpp" | "c++" | "cc" | "hpp" => Lang::Cpp,
        "cs" | "csharp" | "c#" => Lang::CSharp,
        "asm" | "fasm" | "nasm" => Lang::AsmFasm,
        "s" | "gas" | "x86asm" => Lang::AsmGnu,
        "bas" | "basic" | "freebasic" | "fb" | "vb" => Lang::FreeBasic,
        "py" | "python" => Lang::Python,
        "toml" => Lang::Toml,
        "json" => Lang::Json,
        "md" | "markdown" => Lang::Markdown,
        "sh" | "bash" | "shell" | "console" => Lang::Shell,
        "make" | "makefile" => Lang::Make,
        "xml" | "html" | "csproj" | "axaml" => Lang::Xml,
        "ini" => Lang::Ini,
        _ => Lang::Plain,
    }
}

/// Un Markdown simple : titres, listes, blocs de code, `code`, **gras**, liens vers des fichiers.
pub fn markdown(ui: &mut Ui, text: &str, pal: &Palette, code_size: f32, acts: &mut Vec<CopilotAction>) {
    let mut lines = text.lines().peekable();
    let mut para = String::new();
    let flush = |ui: &mut Ui, para: &mut String| {
        if !para.trim().is_empty() {
            inline(ui, para.trim_end(), pal, 13.0, code_size);
        }
        para.clear();
    };
    while let Some(l) = lines.next() {
        let t = l.trim_start();
        if let Some(tag) = t.strip_prefix("```") {
            flush(ui, &mut para);
            let mut code = String::new();
            for c in lines.by_ref() {
                if c.trim_start().starts_with("```") {
                    break;
                }
                code.push_str(c);
                code.push('\n');
            }
            code_block(ui, &code, lang_of_fence(tag), pal, code_size - 0.5, true, acts);
            continue;
        }
        if t.starts_with('#') {
            flush(ui, &mut para);
            let lvl = t.chars().take_while(|c| *c == '#').count();
            let h = t[lvl..].trim();
            ui.add_space(4.0);
            ui.label(RichText::new(h).font(crate::theme::bold(match lvl { 1 => 16.0, 2 => 14.5, _ => 13.5 })));
            continue;
        }
        let bullet = t.strip_prefix("- ").or_else(|| t.strip_prefix("* ")).or_else(|| t.strip_prefix("+ "));
        let numbered = t.split_once(". ").filter(|(n, _)| !n.is_empty() && n.len() <= 3 && n.chars().all(|c| c.is_ascii_digit()));
        if bullet.is_some() || numbered.is_some() {
            flush(ui, &mut para);
            let indent = (l.len() - t.len()) as f32 * 4.0;
            ui.horizontal_wrapped(|ui| {
                ui.add_space(6.0 + indent);
                let (mark, body) = match (bullet, numbered) {
                    (Some(b), _) => ("•".to_string(), b),
                    (_, Some((n, b))) => (format!("{n}."), b),
                    _ => unreachable!(),
                };
                ui.label(RichText::new(mark).color(pal.fg_dim));
                ui.vertical(|ui| inline(ui, body, pal, 13.0, code_size));
            });
            continue;
        }
        if t.is_empty() {
            flush(ui, &mut para);
            ui.add_space(4.0);
            continue;
        }
        if !para.is_empty() {
            para.push('\n');
        }
        para.push_str(l);
    }
    flush(ui, &mut para);
}

/// Le texte d'un paragraphe : `code`, **gras**, *italique*.
fn inline(ui: &mut Ui, text: &str, pal: &Palette, size: f32, code_size: f32) {
    let mut job = LayoutJob::default();
    let body = FontId::proportional(size);
    let bold = crate::theme::bold(size);
    let mono = FontId::monospace(code_size - 1.0);
    let mut rest = text;
    let mut is_bold = false;
    let mut italic = false;
    while !rest.is_empty() {
        let next = rest.find(['`', '*']).unwrap_or(rest.len());
        if next > 0 {
            let mut f = TextFormat::simple(if is_bold { bold.clone() } else { body.clone() }, pal.fg);
            f.italics = italic;
            job.append(&rest[..next], 0.0, f);
            rest = &rest[next..];
            continue;
        }
        if let Some(r) = rest.strip_prefix('`') {
            if let Some(end) = r.find('`') {
                let mut f = TextFormat::simple(mono.clone(), if pal.dark { Color32::from_rgb(0xD7, 0xBA, 0x7D) } else { Color32::from_rgb(0xA3, 0x15, 0x15) });
                f.background = pal.code_bg;
                job.append(&r[..end], 0.0, f);
                rest = &r[end + 1..];
            } else {
                job.append("`", 0.0, TextFormat::simple(body.clone(), pal.fg));
                rest = r;
            }
        } else if let Some(r) = rest.strip_prefix("**") {
            is_bold = !is_bold;
            rest = r;
        } else if let Some(r) = rest.strip_prefix('*') {
            if r.starts_with(' ') {
                job.append("*", 0.0, TextFormat::simple(body.clone(), pal.fg));
            } else {
                italic = !italic;
            }
            rest = r;
        }
    }
    job.wrap.max_width = ui.available_width();
    ui.add(egui::Label::new(job).selectable(true));
}
