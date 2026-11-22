//! La fenêtre : à gauche les conversations (recherche, épinglées, dossiers, puis les autres par date ; un clic les
//! ouvre, le clic droit les renomme, épingle, range ou supprime ; on les glisse sur un dossier), à droite le fil de la
//! conversation ouverte, et en bas le champ de la demande. Plusieurs conversations peuvent travailler en même temps :
//! chacune a son process de la CLI. Une demande tapée pendant que Claude travaille attend son tour sous le champ
//! (« Envoyer maintenant » arrête le tour et la fait partir). À droite, le volet « Environnement » (env.rs) ; dans la
//! liste, les sessions de Claude Code sur le web, en lecture.

use std::collections::{HashMap, HashSet};
use std::path::Path;
use std::time::{Duration, Instant};

use egui::{Align, Color32, Frame, Id, Key, KeyboardShortcut, Label, Layout, Margin, Modifiers, RichText, ScrollArea, Sense, Stroke, TextEdit, Ui};

use crate::chat::{self, Action, PermState, Thread};
use crate::cli::{Cli, Event, Launch};
use crate::env::{self, CloudSession, Git, Usage};
use crate::term::{Line, Term};
use crate::organize::Organize;
use crate::store::{self, SessionInfo};
use crate::theme::{self, Tokens};

/// Une conversation ouverte (son fil, son process).
struct Open {
    thread: Thread,
    cli: Option<Cli>,
    cwd: String,
    draft: String,
    /// les dernières lignes d'erreur de la CLI (montrées si elle s'arrête en plein tour)
    stderr: Vec<String>,
    stick: bool,
    /// Les demandes tapées pendant un tour : la première part à la fin du tour.
    queue: Vec<String>,
    /// Une session du cloud (Claude Code sur le web) : lue, pas de champ de demande ; ses entrées, page après page.
    cloud: Option<String>,
    cloud_entries: Vec<serde_json::Value>,
}

impl Open {
    fn new(cwd: String) -> Open {
        Open {
            thread: Thread::default(),
            cli: None,
            cwd,
            draft: String::new(),
            stderr: Vec::new(),
            stick: true,
            queue: Vec::new(),
            cloud: None,
            cloud_entries: Vec::new(),
        }
    }
}

/// Une demande de contrôle en cours (son numéro -> ce qu'on attend).
enum Req {
    Usage,
    Cloud,
    Transcript { key: String, pages: u32 },
}

enum Dialog {
    None,
    Rename { id: String, text: String },
    Delete { id: String },
    NewFolder { text: String, then_move: Option<String> },
    RenameFolder { i: usize, text: String },
    DeleteFolder { i: usize },
    NewChat { cwd: String },
    Login(Login),
}

/// `claude auth login` : sa sortie, et un champ pour coller un code s'il en demande un.
struct Login {
    cli: Option<std::process::Child>,
    rx: Option<std::sync::mpsc::Receiver<String>>,
    lines: Vec<String>,
    code: String,
}

pub struct App {
    sessions: Vec<SessionInfo>,
    org: Organize,
    open: HashMap<String, Open>,
    current: Option<String>,
    search: String,
    dialog: Dialog,
    /// Le sélecteur de dossier à ouvrir (main.rs : Host::folder_choose), sur ce dossier.
    pub folder_request: Option<String>,
    t: Tokens,
    dark: Option<bool>,
    last_scan: Instant,
    counter: u32,
    status: Option<(String, bool)>,
    /// le thème du système (Korin : demandé à l'hôte)
    pub system_dark: Box<dyn Fn() -> bool>,
    /// le volet « Environnement » : git, l'utilisation de l'abonnement
    git: Git,
    usage: Option<Usage>,
    usage_err: Option<String>,
    usage_asked: Option<Instant>,
    /// les sessions du cloud
    cloud: Vec<CloudSession>,
    cloud_err: Option<String>,
    cloud_asked: Option<Instant>,
    /// Les demandes de contrôle en cours (numéro -> quoi), et la CLI de service : celle qui répond quand aucune
    /// conversation n'a la sienne (arrêtée après deux minutes sans demande).
    requests: HashMap<String, Req>,
    req_counter: u64,
    aux: Option<Cli>,
    aux_used: Instant,
    /// L'onglet Terminal : un bash par dossier.
    terms: HashMap<String, Term>,
    term_focus: bool,
}

const MODELS: [(&str, &str); 4] = [("", "Modèle par défaut"), ("opus", "Opus"), ("sonnet", "Sonnet"), ("haiku", "Haiku")];
const MODES: [(&str, &str); 4] = [
    ("default", "Demander avant d'agir"),
    ("acceptEdits", "Accepter les modifications"),
    ("plan", "Planifier seulement"),
    ("bypassPermissions", "Tout autoriser"),
];

impl App {
    pub fn new(ctx: &egui::Context, system_dark: Box<dyn Fn() -> bool>) -> App {
        theme::install_fonts(ctx);
        let mut app = App {
            sessions: Vec::new(),
            org: Organize::load(),
            open: HashMap::new(),
            current: None,
            search: String::new(),
            dialog: Dialog::None,
            folder_request: None,
            t: Tokens::studio(),
            dark: None,
            last_scan: Instant::now(),
            counter: 0,
            status: None,
            system_dark,
            git: Git::default(),
            usage: None,
            usage_err: None,
            usage_asked: None,
            cloud: Vec::new(),
            cloud_err: None,
            cloud_asked: None,
            requests: HashMap::new(),
            req_counter: 0,
            aux: None,
            aux_used: Instant::now(),
            terms: HashMap::new(),
            term_focus: false,
        };
        app.rescan();
        app
    }

    fn rescan(&mut self) {
        self.sessions = store::list_sessions();
        let alive: HashSet<String> = self.sessions.iter().map(|s| s.id.clone()).collect();
        if self.org.prune(&alive) {
            self.org.save();
        }
        self.last_scan = Instant::now();
    }

    fn info(&self, id: &str) -> Option<&SessionInfo> {
        self.sessions.iter().find(|s| s.id == id)
    }

    fn title_of(&self, key: &str) -> String {
        self.info(key).map(|i| i.title.clone()).unwrap_or_else(|| "Nouvelle conversation".to_owned())
    }

    /// Le thème : celui choisi, sinon celui du système ; refait le style quand il change.
    fn update_theme(&mut self, ctx: &egui::Context) {
        let dark = match self.org.theme.as_str() {
            "dark" => true,
            "light" => false,
            _ => (self.system_dark)(),
        };
        if self.dark != Some(dark) {
            self.dark = Some(dark);
            self.t = Tokens::for_dark(dark);
            theme::apply(ctx, &self.t);
        }
    }

    fn open_session(&mut self, id: &str) {
        if !self.open.contains_key(id)
            && let Some(info) = self.info(id).cloned()
        {
            let mut o = Open::new(info.cwd.clone());
            o.thread = Thread::from_entries(&store::load_entries(&info.path));
            o.thread.session_id = Some(info.id.clone());
            self.open.insert(id.to_owned(), o);
        }
        self.current = Some(id.to_owned());
    }

    fn new_chat(&mut self, cwd: String) {
        self.counter += 1;
        let key = format!("nouvelle-{}", self.counter);
        self.org.note_dir(&cwd);
        self.org.save();
        self.open.insert(key.clone(), Open::new(cwd));
        self.current = Some(key);
    }

    /// Envoie la demande tapée dans la conversation `key` ; pendant un tour, elle attend dans la file.
    fn send(&mut self, key: &str) {
        let Some(o) = self.open.get_mut(key) else { return };
        let text = o.draft.trim().to_owned();
        if text.is_empty() || o.cloud.is_some() {
            return;
        }
        o.draft.clear();
        if o.thread.busy || !o.queue.is_empty() {
            o.queue.push(text);
            return;
        }
        self.send_text(key, &text);
    }

    /// Envoie une demande à la CLI de la conversation `key` (lancée s'il le faut).
    fn send_text(&mut self, key: &str, text: &str) {
        let (model, mode) = (self.org.model.clone(), self.org.permission_mode.clone());
        let Some(o) = self.open.get_mut(key) else { return };
        let text = text.to_owned();
        if o.cli.is_none() {
            let launch = Launch { resume: o.thread.session_id.clone(), cwd: o.cwd.clone().into(), model, permission_mode: mode };
            match Cli::start(&launch) {
                Ok(c) => o.cli = Some(c),
                Err(e) => {
                    o.thread.note(format!("Claude Code ne démarre pas ({} : {e}). Le paquet claude-code est-il installé ?", crate::cli::program()), true);
                    return;
                }
            }
        }
        o.thread.push_user(&text);
        o.stick = true;
        if let Some(c) = o.cli.as_mut()
            && !c.send_user(&text)
        {
            o.thread.busy = false;
            o.thread.note("La demande n'est pas partie : la CLI s'est arrêtée.", true);
            o.cli = None;
        }
    }

    /// Relève les évènements de toutes les CLI qui tournent.
    fn pump(&mut self) -> bool {
        let mut active = false;
        let mut rekey: Vec<(String, String)> = Vec::new();
        let mut finished = false;
        let mut ended: Vec<String> = Vec::new();
        let mut answers: Vec<(String, serde_json::Value, Option<String>)> = Vec::new();
        for (key, o) in self.open.iter_mut() {
            let Some(c) = o.cli.as_mut() else { continue };
            active = true;
            let mut closed = false;
            for ev in c.poll() {
                match ev {
                    Event::Json(v) => {
                        let was_busy = o.thread.busy;
                        // (la réponse à une demande de l'app : get_usage, les sessions du cloud…)
                        if let Some(a) = answer_of(&v, &self.requests) {
                            answers.push(a);
                            continue;
                        }
                        if v.get("type").and_then(|x| x.as_str()) == Some("control_response")
                            && v["response"]["subtype"].as_str() == Some("error")
                        {
                            // (un changement de mode ou de modèle refusé par la CLI : le dire)
                            let e = v["response"]["error"].as_str().unwrap_or("refusé").to_owned();
                            o.thread.note(format!("Claude Code a refusé le changement : {e}"), true);
                            continue;
                        }
                        if v.get("type").and_then(|x| x.as_str()) == Some("control_request") {
                            let sub = v["request"]["subtype"].as_str().unwrap_or("").to_owned();
                            if sub != "can_use_tool" {
                                let rid = v["request_id"].as_str().unwrap_or("").to_owned();
                                c.refuse_request(&rid, &sub);
                                continue;
                            }
                        }
                        o.thread.apply(&v);
                        if was_busy && !o.thread.busy {
                            finished = true;
                            ended.push(key.clone());
                        }
                        if let Some(id) = &o.thread.session_id
                            && key.starts_with("nouvelle-")
                        {
                            rekey.push((key.clone(), id.clone()));
                        }
                    }
                    Event::Text(l) => {
                        o.stderr.push(l);
                        if o.stderr.len() > 30 {
                            o.stderr.remove(0);
                        }
                    }
                    Event::Closed => closed = true,
                }
            }
            if closed || c.exited().is_some() {
                let code = c.exited().flatten();
                if o.thread.busy {
                    o.thread.busy = false;
                    ended.push(key.clone());
                    let tail = o.stderr.iter().rev().take(6).rev().cloned().collect::<Vec<_>>().join("\n");
                    o.thread.note(format!("Claude Code s'est arrêté pendant le tour (code {}).\n{tail}", code.map_or("?".into(), |c| c.to_string())), true);
                }
                o.cli = None;
                finished = true;
            }
        }
        // la CLI de service (sans conversation) : seulement ses réponses
        if let Some(c) = self.aux.as_mut() {
            active = true;
            let mut gone = false;
            for ev in c.poll() {
                match ev {
                    Event::Json(v) => {
                        if let Some(a) = answer_of(&v, &self.requests) {
                            answers.push(a);
                        } else if v.get("type").and_then(|x| x.as_str()) == Some("control_request") {
                            let rid = v["request_id"].as_str().unwrap_or("").to_owned();
                            let sub = v["request"]["subtype"].as_str().unwrap_or("").to_owned();
                            c.refuse_request(&rid, &sub);
                        }
                    }
                    Event::Text(_) => {}
                    Event::Closed => gone = true,
                }
            }
            let idle = self.requests.is_empty() && self.aux_used.elapsed() > Duration::from_secs(120);
            if gone || idle || c.exited().is_some() {
                self.aux = None;
                if gone && !self.requests.is_empty() {
                    // (ses demandes n'auront pas de réponse ; pas de relance avant le délai suivant : une CLI qui s'arrête
                    // aussitôt serait relancée à chaque image)
                    self.requests.retain(|_, r| matches!(r, Req::Transcript { .. }));
                    let why = "Claude Code s'est arrêté avant de répondre".to_owned();
                    self.usage_err = Some(why.clone());
                    self.cloud_err = Some(why);
                }
            }
        }
        for (old, new) in rekey {
            if let Some(o) = self.open.remove(&old) {
                self.open.insert(new.clone(), o);
                if self.current.as_deref() == Some(old.as_str()) {
                    self.current = Some(new.clone());
                }
            }
            for k in ended.iter_mut() {
                if *k == old {
                    *k = new.clone();
                }
            }
        }
        for (id, r, err) in answers {
            self.answered(&id, r, err);
        }
        // fin d'un tour : la demande suivante de la file part ; le volet relit le dépôt et l'utilisation
        for k in ended {
            if let Some(o) = self.open.get_mut(&k) {
                let cwd = o.cwd.clone();
                self.git.stale(&cwd);
                if !o.queue.is_empty() && !o.thread.busy {
                    let next = o.queue.remove(0);
                    self.send_text(&k, &next);
                }
            }
            self.usage_asked = self.usage_asked.filter(|t| t.elapsed() < Duration::from_secs(20));
        }
        if self.git.poll() {
            active = true;
        }
        for term in self.terms.values_mut() {
            if term.poll() || term.running {
                active = true;
            }
        }
        if finished || self.last_scan.elapsed() > Duration::from_secs(8) {
            self.rescan();
        }
        active
    }

    /// Une demande de contrôle (`get_usage`…) : à la CLI d'une conversation ouverte, sinon à la CLI de service (lancée
    /// dans le dossier personnel, sans conversation).
    fn ask(&mut self, subtype: &str, extra: serde_json::Value, req: Req) -> bool {
        self.req_counter += 1;
        let id = format!("korin-app-{}", self.req_counter);
        let mut sent = false;
        if let Some(c) = self.open.values_mut().filter_map(|o| o.cli.as_mut()).next() {
            sent = c.request(&id, subtype, extra.clone());
        }
        if !sent {
            if self.aux.is_none() {
                let launch = Launch { cwd: store::home(), ..Default::default() };
                match Cli::start(&launch) {
                    Ok(c) => self.aux = Some(c),
                    Err(e) => {
                        self.usage_err = Some(format!("Claude Code ne démarre pas : {e}"));
                        return false;
                    }
                }
            }
            self.aux_used = Instant::now();
            sent = self.aux.as_mut().is_some_and(|c| c.request(&id, subtype, extra));
        }
        if sent {
            self.requests.insert(id, req);
        }
        sent
    }

    /// La réponse d'une demande de contrôle (err : son erreur).
    fn answered(&mut self, id: &str, r: serde_json::Value, err: Option<String>) {
        let Some(req) = self.requests.remove(id) else { return };
        match req {
            Req::Usage => match err {
                Some(e) => self.usage_err = Some(e),
                None => {
                    self.usage = Some(env::parse_usage(&r));
                    self.usage_err = None;
                }
            },
            Req::Cloud => {
                match err.map(Err).unwrap_or_else(|| env::parse_cloud(&r)) {
                    Ok(v) => {
                        self.cloud = v;
                        self.cloud_err = None;
                    }
                    Err(e) => self.cloud_err = Some(e),
                }
            }
            Req::Transcript { key, pages } => {
                let Some(o) = self.open.get_mut(&key) else { return };
                if let Some(e) = err.or_else(|| (r.get("status").and_then(|s| s.as_str()) != Some("ok")).then(|| r["status"].as_str().unwrap_or("?").to_owned())) {
                    o.thread.note(format!("La session du cloud ne se lit pas : {e}"), true);
                    return;
                }
                o.cloud_entries.extend(r.get("entries").and_then(|e| e.as_array()).cloned().unwrap_or_default());
                o.thread = Thread::from_entries(&o.cloud_entries);
                let next = r.get("next_cursor").and_then(|c| c.as_str()).filter(|c| !c.is_empty()).map(str::to_owned);
                let sid = o.cloud.clone().unwrap_or_default();
                if let Some(cur) = next
                    && pages < 40
                {
                    self.ask("get_cloud_session_transcript", serde_json::json!({ "session_id": sid, "cursor": cur }), Req::Transcript { key, pages: pages + 1 });
                }
            }
        }
    }

    /// Ouvre une session du cloud, en lecture.
    fn open_cloud(&mut self, s: &CloudSession) {
        let key = format!("cloud:{}", s.id);
        if !self.open.contains_key(&key) {
            let mut o = Open::new("Claude Code sur le web".into());
            o.cloud = Some(s.id.clone());
            o.thread.note(format!("« {} » — session de Claude Code sur le web ({}), en lecture.", s.title, env::cloud_status(&s.status)), false);
            self.open.insert(key.clone(), o);
            self.ask("get_cloud_session_transcript", serde_json::json!({ "session_id": s.id }), Req::Transcript { key: key.clone(), pages: 1 });
        }
        self.current = Some(key);
    }

    fn do_action(&mut self, key: &str, a: Action) {
        if let Action::ToTerminal(cmd) = &a {
            // (la commande dans le champ de l'onglet Terminal, à relire puis lancer par Entrée)
            let cwd = self.open.get(key).map(|o| o.cwd.clone()).unwrap_or_else(|| store::home().to_string_lossy().into_owned());
            self.org.env_panel = true;
            self.org.env_tab = "term".into();
            self.org.save();
            let term = self.terms.entry(cwd.clone()).or_insert_with(|| Term::new(&cwd));
            term.input = cmd.clone();
            self.term_focus = true;
            return;
        }
        let Some(o) = self.open.get_mut(key) else { return };
        match a {
            Action::Allow { request_id, input, remember } => {
                if let Some(c) = o.cli.as_mut() {
                    c.answer_permission(&request_id, true, &input, remember.as_ref());
                }
                o.thread.set_permission(&request_id, PermState::Allowed);
            }
            Action::Deny { request_id } => {
                if let Some(c) = o.cli.as_mut() {
                    c.answer_permission(&request_id, false, &serde_json::Value::Null, None);
                }
                o.thread.set_permission(&request_id, PermState::Denied);
            }
            Action::Login => self.start_login(),
            Action::ToTerminal(_) => {}
        }
    }

    fn start_login(&mut self) {
        use std::io::BufRead;
        let mut login = Login { cli: None, rx: None, lines: Vec::new(), code: String::new() };
        match std::process::Command::new(crate::cli::program())
            .args(["auth", "login"])
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
        {
            Ok(mut child) => {
                let (tx, rx) = std::sync::mpsc::channel();
                for r in [child.stdout.take().map(|s| Box::new(s) as Box<dyn std::io::Read + Send>), child.stderr.take().map(|s| Box::new(s) as Box<dyn std::io::Read + Send>)]
                    .into_iter()
                    .flatten()
                {
                    let tx = tx.clone();
                    std::thread::spawn(move || {
                        for l in std::io::BufReader::new(r).lines().map_while(Result::ok) {
                            let _ = tx.send(strip_ansi(&l));
                        }
                    });
                }
                login.cli = Some(child);
                login.rx = Some(rx);
            }
            Err(e) => login.lines.push(format!("claude auth login ne démarre pas : {e}")),
        }
        self.dialog = Dialog::Login(login);
    }

    pub fn ui(&mut self, ui: &mut Ui) {
        let ctx = ui.ctx().clone();
        self.update_theme(&ctx);
        self.shortcuts(&ctx);
        let active = self.pump();
        if active || matches!(self.dialog, Dialog::Login(_)) {
            ctx.request_repaint_after(Duration::from_millis(50));
        } else {
            ctx.request_repaint_after(Duration::from_secs(2));
        }
        let t = self.t;
        egui::Panel::left("conversations")
            .resizable(true)
            .default_size(290.0)
            .size_range(220.0..=480.0)
            .frame(Frame::new().fill(t.chrome).inner_margin(Margin::same(10)).stroke(Stroke::new(1.0, t.separator)))
            .show(ui, |ui| self.sidebar(ui));
        if self.org.env_panel {
            egui::Panel::right("environnement")
                .resizable(true)
                .default_size(300.0)
                .size_range(240.0..=520.0)
                .frame(Frame::new().fill(t.chrome).inner_margin(Margin::same(12)).stroke(Stroke::new(1.0, t.separator)))
                .show(ui, |ui| self.env_panel(ui));
        }
        egui::CentralPanel::default().frame(Frame::new().fill(t.canvas)).show(ui, |ui| self.main(ui));
        self.dialogs(&ctx);
    }

    /// L'onglet Terminal : un bash dans le dossier de la conversation (term.rs).
    fn term_panel(&mut self, ui: &mut Ui) {
        let t = self.t;
        let cwd = self
            .current
            .as_ref()
            .and_then(|k| self.open.get(k))
            .filter(|o| o.cloud.is_none())
            .map(|o| o.cwd.clone())
            .unwrap_or_else(|| store::home().to_string_lossy().into_owned());
        let focus = std::mem::take(&mut self.term_focus);
        // (sans conversation ouverte, pas de champ de demande : le clavier va au terminal)
        let no_conv = self.current.as_ref().is_none_or(|k| !self.open.contains_key(k));
        let term = self.terms.entry(cwd.clone()).or_insert_with(|| Term::new(&cwd));
        ui.horizontal(|ui| {
            ui.add(Label::new(RichText::new(format!("🗀 {}", store::short_dir(&cwd))).font(theme::mono(theme::CAPTION)).color(t.text_dim)).truncate());
        });
        ui.horizontal_wrapped(|ui| {
            if term.running && ui.small_button("■ Arrêter").clicked() {
                term.stop();
            }
            if ui.small_button("Effacer").clicked() {
                term.lines.clear();
            }
            if ui.small_button("Tout copier").clicked() {
                ui.ctx().copy_text(term.text());
            }
            if ui.small_button("Terminal de Korin").on_hover_text("Le Terminal complet, dans ce dossier (programmes plein écran)").clicked() {
                let _ = std::process::Command::new("/apps/terminal.app/main").current_dir(&cwd).spawn();
            }
        });
        let out_h = (ui.available_height() - 74.0).max(80.0);
        ScrollArea::vertical().id_salt(("terminal", &cwd)).max_height(out_h).auto_shrink([false, false]).stick_to_bottom(true).show(ui, |ui| {
            Frame::new().fill(t.code_bg).corner_radius(theme::R).inner_margin(Margin::same(8)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                if term.lines.is_empty() {
                    ui.label(RichText::new("Tapez une commande en bas, ou « ▶ Terminal » sous une commande de Claude.").font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                }
                for l in &term.lines {
                    let (text, color) = match l {
                        Line::Cmd(c) => (format!("$ {c}"), t.accent_ink),
                        Line::Out(o) => (o.clone(), t.text),
                        Line::Err(e) => (e.clone(), t.warning),
                        Line::End(0) => continue,
                        Line::End(c) => (format!("(code {c})"), t.danger),
                        Line::Note(n) => (n.clone(), t.text_faint),
                    };
                    ui.add(Label::new(RichText::new(text).font(theme::mono(theme::CAPTION)).color(color)).selectable(true).wrap());
                }
                if term.running {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label(RichText::new("en cours…").font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                    });
                }
            });
        });
        ui.add_space(6.0);
        let mut run = false;
        ui.horizontal(|ui| {
            let w = ui.available_width() - 34.0;
            // (comme le champ de la demande : Entrée lance, Maj+Entrée va à la ligne ; un champ d'une ligne d'egui avale
            // l'Entrée)
            let r = ui.add(
                TextEdit::multiline(&mut term.input)
                    .font(theme::mono(theme::CODE))
                    .hint_text("Commande bash")
                    .desired_rows(1)
                    .desired_width(w)
                    .return_key(Some(KeyboardShortcut::new(Modifiers::SHIFT, Key::Enter))),
            );
            if focus || (no_conv && ui.memory(|m| m.focused().is_none())) {
                r.request_focus();
            }
            if r.has_focus() && !term.input.contains('\n') {
                let (up, down) = ui.input(|i| (i.key_pressed(Key::ArrowUp), i.key_pressed(Key::ArrowDown)));
                if up {
                    term.recall(true);
                } else if down {
                    term.recall(false);
                }
            }
            if r.has_focus() && ui.input(|i| i.key_pressed(Key::Enter) && !i.modifiers.shift) {
                run = true;
            }
            if ui.button("▶").on_hover_text("Lancer (Entrée)").clicked() {
                run = true;
            }
            if run {
                let c = std::mem::take(&mut term.input);
                term.run(&c);
                r.request_focus();
            }
        });
        ui.label(RichText::new("bash sans terminal : copier-coller (Ctrl+C, Ctrl+V), ↑ ↓ l'historique ; pas de programmes plein écran.").font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
    }

    /// Le volet « Environnement » : utilisation de l'abonnement, dossier et dépôt de la conversation, session.
    fn env_panel(&mut self, ui: &mut Ui) {
        let t = self.t;
        let term_tab = self.org.env_tab == "term";
        ui.horizontal(|ui| {
            for (k, l) in [("env", "Environnement"), ("term", "Terminal")] {
                let on = (k == "term") == term_tab;
                if ui.selectable_label(on, RichText::new(l).font(theme::semibold(theme::SMALL))).clicked() && !on {
                    self.org.env_tab = if k == "term" { "term".into() } else { String::new() };
                    self.org.save();
                }
            }
            // (à droite : les boutons de la fenêtre de Korin ; le volet se ferme par ◧ ou F6)
        });
        ui.add_space(4.0);
        if term_tab {
            self.term_panel(ui);
            return;
        }
        // l'utilisation : relue chaque minute tant que l'onglet est ouvert
        let waiting = self.requests.values().any(|r| matches!(r, Req::Usage));
        if !waiting && self.usage_asked.is_none_or(|a| a.elapsed() > Duration::from_secs(60)) {
            self.usage_asked = Some(Instant::now());
            self.ask("get_usage", serde_json::json!({ "skip_behaviors": true }), Req::Usage);
        }
        ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
            // -- l'abonnement
            section(ui, &t, "UTILISATION");
            match (&self.usage, &self.usage_err) {
                (_, Some(e)) => {
                    ui.label(RichText::new(format!("Indisponible : {e}")).font(egui::FontId::proportional(theme::LABEL)).color(t.text_faint));
                }
                (None, None) => {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label(RichText::new("Demandée à Claude Code…").color(t.text_faint));
                    });
                }
                (Some(u), None) => {
                    if !u.plan.is_empty() {
                        ui.label(RichText::new(format!("Abonnement : {}", plan_name(&u.plan))).font(egui::FontId::proportional(theme::LABEL)).color(t.text_dim));
                    }
                    if !u.available || u.windows.is_empty() {
                        ui.label(RichText::new("Pas de limites d'abonnement (clé d'API, ou pas encore connu).").font(egui::FontId::proportional(theme::LABEL)).color(t.text_faint));
                    }
                    let now = env::now_secs();
                    for w in &u.windows {
                        ui.add_space(4.0);
                        ui.horizontal(|ui| {
                            ui.label(RichText::new(&w.name).font(theme::medium(theme::LABEL)).color(t.text));
                            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                                ui.label(RichText::new(format!("{:.0} %", w.pct)).font(theme::medium(theme::LABEL)).color(if w.pct >= 90.0 { t.danger } else if w.pct >= 75.0 { t.warning } else { t.text }));
                            });
                        });
                        let bar = egui::ProgressBar::new((w.pct / 100.0).clamp(0.0, 1.0) as f32).desired_height(6.0).fill(if w.pct >= 90.0 { t.danger } else if w.pct >= 75.0 { t.warning } else { t.accent });
                        ui.add(bar);
                        if let Some(r) = w.resets {
                            ui.label(RichText::new(format!("repart à zéro {}", env::until(r, now))).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                        }
                    }
                }
            }
            if ui.small_button("Actualiser").clicked() {
                self.usage_asked = None;
                self.usage_err = None;
            }

            // -- la conversation
            let Some(key) = self.current.clone().filter(|k| self.open.contains_key(k)) else {
                section(ui, &t, "CONVERSATION");
                ui.label(RichText::new("Ouvrez une conversation pour voir son dossier et son dépôt.").font(egui::FontId::proportional(theme::LABEL)).color(t.text_faint));
                return;
            };
            if let Some(sid) = self.open[&key].cloud.clone() {
                section(ui, &t, "SESSION DU CLOUD");
                ui.add(Label::new(RichText::new(&sid).font(theme::mono(theme::CAPTION)).color(t.text_dim)).selectable(true).wrap());
                if ui.link("Ouvrir dans Jet (claude.ai)").clicked() {
                    let _ = std::process::Command::new("xdg-open").arg(format!("https://claude.ai/code/{sid}")).spawn();
                }
                return;
            }
            let cwd = self.open[&key].cwd.clone();
            section(ui, &t, "DOSSIER");
            ui.add(Label::new(RichText::new(&cwd).font(theme::mono(theme::CAPTION)).color(t.text)).selectable(true).wrap());
            self.git.want(&cwd, Duration::from_secs(20));
            section(ui, &t, "DÉPÔT");
            match self.git.get(&cwd).cloned() {
                None => {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label(RichText::new("git…").color(t.text_faint));
                    });
                }
                Some(g) if !g.repo => {
                    let why = g.error.unwrap_or_else(|| "Pas un dépôt git.".into());
                    ui.label(RichText::new(why).font(egui::FontId::proportional(theme::LABEL)).color(t.text_faint));
                }
                Some(g) => {
                    let line = |ui: &mut Ui, k: &str, v: &str| {
                        ui.horizontal_wrapped(|ui| {
                            ui.label(RichText::new(k).font(egui::FontId::proportional(theme::LABEL)).color(t.text_faint));
                            ui.add(Label::new(RichText::new(v).font(theme::mono(theme::CAPTION)).color(t.text)).selectable(true));
                        });
                    };
                    if !g.remote_name.is_empty() {
                        line(ui, "Dépôt", &g.remote_name);
                    } else {
                        line(ui, "Dépôt", &store::short_dir(&g.root));
                    }
                    let mut b = format!("⎇ {}", g.branch);
                    if g.ahead > 0 {
                        b.push_str(&format!("  ↑{}", g.ahead));
                    }
                    if g.behind > 0 {
                        b.push_str(&format!("  ↓{}", g.behind));
                    }
                    line(ui, "Branche", &b);
                    if !g.upstream.is_empty() {
                        line(ui, "Suit", &g.upstream);
                    }
                    if !g.last_commit.is_empty() {
                        ui.add(Label::new(RichText::new(&g.last_commit).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim)).wrap());
                    }
                    ui.add_space(4.0);
                    if g.changed.is_empty() {
                        ui.label(RichText::new("✓ Rien à commiter").font(egui::FontId::proportional(theme::LABEL)).color(t.success));
                    } else {
                        ui.label(RichText::new(format!("{} fichier{} changé{}", g.changed.len(), if g.changed.len() > 1 { "s" } else { "" }, if g.changed.len() > 1 { "s" } else { "" })).font(theme::medium(theme::LABEL)).color(t.text));
                        for (st, f) in g.changed.iter().take(15) {
                            ui.horizontal(|ui| {
                                let c = match st.trim() {
                                    "??" => t.text_faint,
                                    s if s.contains('D') => t.danger,
                                    s if s.contains('A') => t.success,
                                    _ => t.warning,
                                };
                                ui.label(RichText::new(st.trim()).font(theme::mono(theme::CAPTION)).color(c));
                                ui.add(Label::new(RichText::new(f).font(theme::mono(theme::CAPTION)).color(t.text_dim)).truncate());
                            });
                        }
                        if g.changed.len() > 15 {
                            ui.label(RichText::new(format!("… et {} autres", g.changed.len() - 15)).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                        }
                    }
                    if ui.small_button("Relire").clicked() {
                        self.git.stale(&cwd);
                    }
                }
            }
            // -- la session
            section(ui, &t, "SESSION");
            let o = &self.open[&key];
            let model = if o.thread.model.is_empty() { MODELS.iter().find(|(k, _)| *k == self.org.model).map_or("", |(_, l)| *l).to_owned() } else { o.thread.model.clone() };
            ui.label(RichText::new(format!("Modèle : {model}")).font(egui::FontId::proportional(theme::LABEL)).color(t.text_dim));
            let mode = MODES.iter().find(|(k, _)| *k == self.org.permission_mode || (self.org.permission_mode.is_empty() && *k == "default")).map_or("", |(_, l)| *l);
            ui.label(RichText::new(format!("Autorisations : {mode}")).font(egui::FontId::proportional(theme::LABEL)).color(t.text_dim));
            ui.label(RichText::new(if o.cli.is_some() { "Claude Code tourne pour cette conversation" } else { "Claude Code ne tourne pas (il démarre à la prochaine demande)" }).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
            if let Some(id) = &o.thread.session_id {
                ui.add(Label::new(RichText::new(id).font(theme::mono(theme::CAPTION)).color(t.text_faint)).selectable(true).wrap());
            }
        });
    }

    // ---- la barre de menus de Korin (main.rs la tient : egui_korin::menu) ----

    /// Le texte des menus (le format de UIKit : M titre, I<id> entrée, - séparateur).
    pub fn menu_text(&self) -> &'static str {
        "MFichier\nI1\tNouvelle conversation…\tCtrl+N\nI2\tNouveau dossier…\n-\nI3\tQuitter\tCtrl+Q\n\
         MConversation\nI10\tRenommer…\tF2\nI11\tÉpingler\nI12\tCopier la conversation\nI13\tArrêter Claude\n-\nI14\tSupprimer…\n\
         MAffichage\nI20\tThème de Korin\nI21\tSombre\nI22\tClair\n-\nI24\tVolet Environnement\tF6\nI23\tRelire les conversations\tF5\n"
    }

    /// Les états des entrées : (id, 0 normale | 1 grisée | 2 cochée).
    pub fn menu_states(&self) -> Vec<(i64, i64)> {
        let saved = self.current.as_deref().is_some_and(|k| self.info(k).is_some());
        let running = self.current.as_deref().and_then(|k| self.open.get(k)).is_some_and(|o| o.cli.is_some());
        let pinned = self.current.as_deref().is_some_and(|k| self.org.is_pinned(k));
        let theme = if self.org.theme.is_empty() { "system" } else { self.org.theme.as_str() };
        let off = |b: bool| if b { 0 } else { 1 };
        vec![
            (10, off(saved)),
            (11, if !saved { 1 } else if pinned { 2 } else { 0 }),
            (12, off(self.current.is_some())),
            (13, off(running)),
            (14, off(saved)),
            (20, if theme == "system" { 2 } else { 0 }),
            (21, if theme == "dark" { 2 } else { 0 }),
            (22, if theme == "light" { 2 } else { 0 }),
            (24, if self.org.env_panel { 2 } else { 0 }),
        ]
    }

    /// Le dossier choisi dans le sélecteur de Korin : celui de la nouvelle conversation.
    pub fn folder_chosen(&mut self, dir: String) {
        if let Dialog::NewChat { cwd } = &mut self.dialog {
            let t = dir.trim_end_matches('/');
            *cwd = if t.is_empty() { "/".to_owned() } else { t.to_owned() };
        }
    }

    /// Un choix dans la barre de menus ; rend true pour Quitter.
    pub fn menu_command(&mut self, ctx: &egui::Context, id: i64) -> bool {
        let cur = self.current.clone();
        match id {
            1 => {
                let cwd = self.org.recent_dirs.first().cloned().unwrap_or_else(|| store::home().to_string_lossy().into_owned());
                self.dialog = Dialog::NewChat { cwd };
            }
            2 => self.dialog = Dialog::NewFolder { text: String::new(), then_move: None },
            3 => return true,
            10 => {
                if let Some(k) = cur.filter(|k| self.info(k).is_some()) {
                    let text = self.title_of(&k);
                    self.dialog = Dialog::Rename { id: k, text };
                }
            }
            11 => {
                if let Some(k) = cur.filter(|k| self.info(k).is_some()) {
                    self.org.toggle_pin(&k);
                    self.org.save();
                }
            }
            12 => {
                if let Some(o) = cur.and_then(|k| self.open.get(&k)) {
                    ctx.copy_text(o.thread.as_text());
                }
            }
            13 => {
                if let Some(o) = cur.and_then(|k| self.open.get_mut(&k)) {
                    o.cli = None;
                    o.thread.busy = false;
                }
            }
            14 => {
                if let Some(k) = cur.filter(|k| self.info(k).is_some()) {
                    self.dialog = Dialog::Delete { id: k };
                }
            }
            20..=22 => {
                self.org.theme = ["system", "dark", "light"][(id - 20) as usize].to_owned();
                self.org.save();
                self.dark = None;
            }
            23 => self.rescan(),
            24 => {
                self.org.env_panel = !self.org.env_panel;
                self.org.save();
            }
            _ => {}
        }
        false
    }

    /// Les raccourcis des menus quand la fenêtre a le clavier (Ctrl+N, F2, F5, F6).
    fn shortcuts(&mut self, ctx: &egui::Context) {
        if !matches!(self.dialog, Dialog::None) {
            return;
        }
        let (n, f2, f5, f6) = ctx.input(|i| (i.modifiers.command && i.key_pressed(Key::N), i.key_pressed(Key::F2), i.key_pressed(Key::F5), i.key_pressed(Key::F6)));
        if n {
            self.menu_command(ctx, 1);
        } else if f2 {
            self.menu_command(ctx, 10);
        } else if f5 {
            self.menu_command(ctx, 23);
        } else if f6 {
            self.menu_command(ctx, 24);
        }
    }

    // ---- la liste des conversations ----

    fn sidebar(&mut self, ui: &mut Ui) {
        let t = self.t;
        ui.horizontal(|ui| {
            ui.label(RichText::new("✳").font(theme::semibold(theme::SECTION)).color(Color32::from_rgb(0xd9, 0x77, 0x57)));
            ui.label(RichText::new("Claude Code").font(theme::semibold(theme::SECTION)).color(t.text));
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                theme_menu(ui, &mut self.org, &mut self.dark);
            });
        });
        ui.add_space(6.0);
        let w = ui.available_width();
        if ui.add_sized([w, 32.0], egui::Button::new(RichText::new("+  Nouvelle conversation").color(t.on_accent).font(theme::medium(theme::SMALL))).fill(t.accent).corner_radius(theme::R)).clicked() {
            let cwd = self.org.recent_dirs.first().cloned().unwrap_or_else(|| store::home().to_string_lossy().into_owned());
            self.dialog = Dialog::NewChat { cwd };
        }
        ui.add_space(6.0);
        ui.add(TextEdit::singleline(&mut self.search).hint_text("Rechercher").desired_width(f32::INFINITY).margin(Margin::symmetric(8, 6)));
        ui.add_space(6.0);

        let q = self.search.to_lowercase();
        let visible: Vec<SessionInfo> =
            self.sessions.iter().filter(|s| q.is_empty() || s.title.to_lowercase().contains(&q) || s.cwd.to_lowercase().contains(&q)).cloned().collect();
        let mut moved: Option<(String, Option<usize>)> = None;
        let mut cloud_click: Option<CloudSession> = None;
        ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
            // les conversations neuves, pas encore enregistrées par la CLI
            let fresh: Vec<String> = self.open.keys().filter(|k| k.starts_with("nouvelle-")).cloned().collect();
            for k in fresh {
                let sel = self.current.as_deref() == Some(k.as_str());
                if row(ui, &t, "Nouvelle conversation", &self.open[&k].cwd, sel, self.open[&k].thread.busy).clicked() {
                    self.current = Some(k.clone());
                }
            }
            let pinned: Vec<&SessionInfo> = visible.iter().filter(|s| self.org.is_pinned(&s.id)).collect();
            if !pinned.is_empty() {
                section(ui, &t, "Épinglées");
                for s in pinned {
                    self.session_row(ui, s, &mut moved);
                }
            }
            // les dossiers (on y dépose une conversation glissée)
            let nfolders = self.org.folders.len();
            for fi in 0..nfolders {
                let name = self.org.folders[fi].name.clone();
                let collapsed = self.org.folders[fi].collapsed;
                let members: Vec<SessionInfo> =
                    visible.iter().filter(|s| self.org.folders[fi].sessions.contains(&s.id) && !self.org.is_pinned(&s.id)).cloned().collect();
                let block = ui.scope(|ui| {
                    let h = ui.horizontal(|ui| {
                        let arrow = if collapsed { "▸" } else { "▾" };
                        let r = ui.add(Label::new(RichText::new(format!("{arrow}  🗀  {name}")).font(theme::medium(theme::LABEL)).color(t.text_dim)).sense(Sense::click()));
                        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                            ui.label(RichText::new(members.len().to_string()).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                        });
                        r
                    });
                    let r = h.inner;
                    if r.clicked() {
                        self.org.folders[fi].collapsed = !collapsed;
                        self.org.save();
                    }
                    r.context_menu(|ui| {
                        if ui.button("Renommer le dossier…").clicked() {
                            self.dialog = Dialog::RenameFolder { i: fi, text: name.clone() };
                            ui.close();
                        }
                        if fi > 0 && ui.button("Monter").clicked() {
                            self.org.shift_folder(fi, -1);
                            self.org.save();
                            ui.close();
                        }
                        if fi + 1 < nfolders && ui.button("Descendre").clicked() {
                            self.org.shift_folder(fi, 1);
                            self.org.save();
                            ui.close();
                        }
                        ui.separator();
                        if ui.button(RichText::new("Supprimer le dossier…").color(t.danger)).clicked() {
                            self.dialog = Dialog::DeleteFolder { i: fi };
                            ui.close();
                        }
                    });
                    if !collapsed {
                        for s in &members {
                            ui.horizontal(|ui| {
                                ui.add_space(10.0);
                                ui.vertical(|ui| self.session_row(ui, s, &mut moved));
                            });
                        }
                        if members.is_empty() {
                            ui.label(RichText::new("    Glissez une conversation ici").font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                        }
                    }
                });
                if let Some(id) = drop_target(ui, &t, block.response.rect, ("dossier", fi)) {
                    moved = Some((id, Some(fi)));
                }
            }
            // les autres, par date ; une zone où déposer une conversation pour la sortir de son dossier
            let rest_block = ui.scope(|ui| {
                let rest: Vec<&SessionInfo> = visible.iter().filter(|s| !self.org.is_pinned(&s.id) && self.org.folder_of(&s.id).is_none()).collect();
                let mut group = "";
                for s in rest {
                    let g = store::age_group(s.modified);
                    if g != group {
                        section(ui, &t, g);
                        group = g;
                    }
                    self.session_row(ui, s, &mut moved);
                }
                if visible.is_empty() && self.open.is_empty() {
                    ui.add_space(20.0);
                    ui.label(RichText::new(if q.is_empty() { "Pas encore de conversation." } else { "Aucune conversation ne correspond." }).color(t.text_faint));
                }
                ui.add_space(30.0);
            });
            if let Some(id) = drop_target(ui, &t, rest_block.response.rect, "hors-dossier") {
                moved = Some((id, None));
            }
            // les sessions de Claude Code sur le web (list_cloud_sessions de la CLI), en lecture
            let open_cloud = self.org.cloud_open;
            let r = ui.add(
                Label::new(RichText::new(format!("{}  ☁  Claude Code sur le web", if open_cloud { "▾" } else { "▸" })).font(theme::medium(theme::LABEL)).color(t.text_dim))
                    .sense(Sense::click()),
            );
            if r.clicked() {
                self.org.cloud_open = !open_cloud;
                self.org.save();
                self.cloud_asked = None;
            }
            if open_cloud {
                let waiting = self.requests.values().any(|r| matches!(r, Req::Cloud));
                if !waiting && self.cloud_asked.is_none_or(|a| a.elapsed() > Duration::from_secs(120)) {
                    self.cloud_asked = Some(Instant::now());
                    self.ask("list_cloud_sessions", serde_json::json!({ "user_initiated": true }), Req::Cloud);
                }
                if let Some(e) = &self.cloud_err {
                    ui.label(RichText::new(format!("Indisponible : {e}")).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                } else if self.cloud.is_empty() {
                    ui.label(RichText::new(if waiting { "Demandées à Claude Code…" } else { "Aucune session sur le web." }).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                }
                let list = self.cloud.clone();
                for c in &list {
                    let key = format!("cloud:{}", c.id);
                    let sel = self.current.as_deref() == Some(key.as_str());
                    let sub = format!("{} · {}", env::cloud_status(&c.status), iso_ago(&c.updated));
                    let r = row(ui, &t, &c.title, &sub, sel, c.status == "running");
                    if r.clicked() {
                        cloud_click = Some(c.clone());
                    }
                    r.context_menu(|ui| {
                        if ui.button("Ouvrir dans Jet (claude.ai)").clicked() {
                            let _ = std::process::Command::new("xdg-open").arg(format!("https://claude.ai/code/{}", c.id)).spawn();
                            ui.close();
                        }
                        if ui.button("Copier l'identifiant").clicked() {
                            ui.ctx().copy_text(c.id.clone());
                            ui.close();
                        }
                    });
                }
                if ui.small_button("Actualiser").clicked() {
                    self.cloud_asked = None;
                    self.cloud_err = None;
                }
            }
            ui.add_space(30.0);
        });
        if let Some(c) = cloud_click {
            self.open_cloud(&c);
        }
        if let Some((id, f)) = moved {
            self.org.move_to(&id, f);
            self.org.save();
        }
        ui.with_layout(Layout::bottom_up(Align::Min), |ui| {
            if ui.button("+  Nouveau dossier").clicked() {
                self.dialog = Dialog::NewFolder { text: String::new(), then_move: None };
            }
        });
    }

    fn session_row(&mut self, ui: &mut Ui, s: &SessionInfo, moved: &mut Option<(String, Option<usize>)>) {
        let t = self.t;
        let sel = self.current.as_deref() == Some(s.id.as_str());
        let busy = self.open.get(&s.id).is_some_and(|o| o.thread.busy);
        let id = s.id.clone();
        let sub = format!("{} · {} message{} · {}", store::short_dir(&s.cwd), s.messages, if s.messages > 1 { "s" } else { "" }, store::ago(s.modified));
        let r = row(ui, &t, &s.title, &sub, sel, busy);
        // (glisser la ligne sur un dossier : la charge est l'identifiant ; un clic reste un clic)
        r.dnd_set_drag_payload(id.clone());
        if r.dragged() {
            egui::Area::new(Id::unique("glisse")).order(egui::Order::Tooltip).fixed_pos(ui.ctx().pointer_latest_pos().unwrap_or_default() + egui::vec2(12.0, 8.0)).show(ui.ctx(), |ui| {
                Frame::new().fill(t.card).stroke(Stroke::new(1.0, t.accent_ink)).corner_radius(theme::R).inner_margin(Margin::symmetric(8, 4)).show(ui, |ui| {
                    ui.label(RichText::new(store::one_line(&s.title, 40)).font(egui::FontId::proportional(theme::SMALL)));
                });
            });
        }
        if r.clicked() {
            self.open_session(&id);
        }
        r.context_menu(|ui| {
            if ui.button("Renommer…").clicked() {
                self.dialog = Dialog::Rename { id: id.clone(), text: s.title.clone() };
                ui.close();
            }
            let pinned = self.org.is_pinned(&id);
            if ui.button(if pinned { "Désépingler" } else { "Épingler" }).clicked() {
                self.org.toggle_pin(&id);
                self.org.save();
                ui.close();
            }
            ui.menu_button("Ranger dans", |ui| {
                let cur = self.org.folder_of(&id);
                for (i, f) in self.org.folders.iter().enumerate() {
                    if ui.add_enabled(cur != Some(i), egui::Button::new(format!("📁 {}", f.name))).clicked() {
                        *moved = Some((id.clone(), Some(i)));
                        ui.close();
                    }
                }
                if cur.is_some() && ui.button("Hors des dossiers").clicked() {
                    *moved = Some((id.clone(), None));
                    ui.close();
                }
                ui.separator();
                if ui.button("Nouveau dossier…").clicked() {
                    self.dialog = Dialog::NewFolder { text: String::new(), then_move: Some(id.clone()) };
                    ui.close();
                }
            });
            if ui.button("Copier l'identifiant").clicked() {
                ui.ctx().copy_text(id.clone());
                ui.close();
            }
            ui.separator();
            if ui.button(RichText::new("Supprimer…").color(t.danger)).clicked() {
                self.dialog = Dialog::Delete { id: id.clone() };
                ui.close();
            }
        });
    }

    // ---- la conversation ouverte ----

    fn main(&mut self, ui: &mut Ui) {
        let t = self.t;
        let Some(key) = self.current.clone().filter(|k| self.open.contains_key(k)) else {
            self.welcome(ui);
            return;
        };
        // l'en-tête : titre (un clic le renomme), dossier, modèle, mode
        let title = self.title_of(&key);
        let saved = self.info(&key).is_some();
        // (à droite, la place des boutons de la fenêtre de Korin : réduire, agrandir, fermer)
        // (le volet ouvert porte les boutons de la fenêtre : l'en-tête n'a plus à leur laisser la place)
        let right = if self.org.env_panel { 16 } else { 120 };
        Frame::new().fill(t.canvas).inner_margin(Margin { left: 20, right, top: 12, bottom: 10 }).show(ui, |ui| {
            ui.horizontal(|ui| {
                let r = ui.add(Label::new(RichText::new(&title).font(theme::semibold(theme::SECTION)).color(t.text)).sense(Sense::click()).truncate());
                if saved && r.on_hover_text("Renommer").clicked() {
                    self.dialog = Dialog::Rename { id: key.clone(), text: title.clone() };
                }
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if ui.selectable_label(self.org.env_panel, "◧").on_hover_text("Volet Environnement (F6)").clicked() {
                        self.org.env_panel = !self.org.env_panel;
                        self.org.save();
                    }
                    ui.menu_button("⋯", |ui| {
                        if ui.button("Copier la conversation").clicked() {
                            ui.ctx().copy_text(self.open[&key].thread.as_text());
                            ui.close();
                        }
                        if saved && ui.button("Renommer…").clicked() {
                            self.dialog = Dialog::Rename { id: key.clone(), text: title.clone() };
                            ui.close();
                        }
                        if self.open[&key].cli.is_some() && ui.button("Arrêter Claude Code pour cette conversation").clicked() {
                            if let Some(o) = self.open.get_mut(&key) {
                                o.cli = None;
                                o.thread.busy = false;
                            }
                            ui.close();
                        }
                        if saved {
                            ui.separator();
                            if ui.button(RichText::new("Supprimer…").color(t.danger)).clicked() {
                                self.dialog = Dialog::Delete { id: key.clone() };
                                ui.close();
                            }
                        }
                    });
                    let (mode0, model0) = (self.org.permission_mode.clone(), self.org.model.clone());
                    combo(ui, "mode", &mut self.org.permission_mode, &MODES, "default");
                    combo(ui, "modele", &mut self.org.model, &MODELS, "");
                    // (un changement vaut aussi pour les conversations dont la CLI tourne déjà : elle ne relit pas ses
                    // options de lancement)
                    if self.org.permission_mode != mode0 || self.org.model != model0 {
                        self.org.save();
                        for o in self.open.values_mut() {
                            if let Some(c) = o.cli.as_mut() {
                                if self.org.permission_mode != mode0 {
                                    c.set_permission_mode(&self.org.permission_mode);
                                }
                                if self.org.model != model0 {
                                    c.set_model(&self.org.model);
                                }
                            }
                        }
                    }
                });
            });
            let o = &self.open[&key];
            let mut sub = format!("🗁  {}", o.cwd);
            if !o.thread.model.is_empty() {
                sub.push_str(&format!("   ·   {}", o.thread.model));
            }
            ui.add(Label::new(RichText::new(sub).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint)).truncate());
        });
        ui.painter().hline(ui.max_rect().x_range(), ui.cursor().top(), Stroke::new(1.0, t.separator));

        // le champ de la demande, en bas (une session du cloud : en lecture, pas de champ)
        let mut send = false;
        let mut stop = false;
        let mut now_idx: Option<usize> = None;
        let mut drop_idx: Option<usize> = None;
        let no_dialog = matches!(self.dialog, Dialog::None);
        let cloud = self.open[&key].cloud.clone();
        if let Some(sid) = cloud {
            egui::Panel::bottom(("lecture", &key)).frame(Frame::new().fill(t.canvas).inner_margin(Margin { left: 20, right: 20, top: 8, bottom: 14 })).show(ui, |ui| {
                ui.horizontal(|ui| {
                    ui.label(RichText::new("☁  Session de Claude Code sur le web, en lecture.").color(t.text_dim));
                    if ui.link("La continuer dans Jet (claude.ai)").clicked() {
                        let _ = std::process::Command::new("xdg-open").arg(format!("https://claude.ai/code/{sid}")).spawn();
                    }
                });
            });
        } else {
        egui::Panel::bottom(("composer", &key)).frame(Frame::new().fill(t.canvas).inner_margin(Margin { left: 20, right: 20, top: 8, bottom: 14 })).show(ui, |ui| {
            let o = self.open.get_mut(&key).expect("ouverte");
            let busy = o.thread.busy;
            // les demandes en attente : la première part à la fin du tour ; « Envoyer maintenant » arrête le tour
            for (i, q) in o.queue.iter().enumerate() {
                Frame::new().fill(t.card).stroke(Stroke::new(1.0, t.card_border)).corner_radius(theme::R).inner_margin(Margin::symmetric(10, 6)).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.horizontal(|ui| {
                        ui.label(RichText::new("⏳").color(t.text_faint));
                        ui.add(Label::new(RichText::new(store::one_line(q, 90)).font(egui::FontId::proportional(theme::SMALL)).color(t.text)).truncate());
                    });
                    ui.horizontal(|ui| {
                        ui.label(RichText::new(if i == 0 { "En attente : part à la fin du tour" } else { "En attente" }).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                        if ui.link(RichText::new("Envoyer maintenant").font(egui::FontId::proportional(theme::CAPTION))).on_hover_text("Arrête le tour en cours et envoie cette demande").clicked() {
                            now_idx = Some(i);
                        }
                        if ui.link(RichText::new("Retirer").font(egui::FontId::proportional(theme::CAPTION))).clicked() {
                            drop_idx = Some(i);
                        }
                    });
                });
                ui.add_space(4.0);
            }
            Frame::new().fill(t.field).stroke(Stroke::new(1.0, t.field_border)).corner_radius(theme::R_LG).inner_margin(Margin::same(10)).show(ui, |ui| {
                let resp = ui.add(
                    TextEdit::multiline(&mut o.draft)
                        .hint_text(if busy { "Claude travaille… votre demande attendra la fin du tour" } else { "Demandez à Claude…" })
                        .desired_rows(2)
                        .desired_width(f32::INFINITY)
                        .frame(Frame::NONE)
                        .return_key(Some(KeyboardShortcut::new(Modifiers::SHIFT, Key::Enter))),
                );
                // (le champ prend le clavier quand rien d'autre ne l'a : on tape sa demande sans cliquer)
                if no_dialog && ui.memory(|m| m.focused().is_none()) {
                    resp.request_focus();
                }
                if resp.has_focus() && ui.input(|i| i.key_pressed(Key::Enter) && !i.modifiers.shift) {
                    send = true;
                }
                ui.horizontal(|ui| {
                    ui.label(RichText::new("Entrée : envoyer · Maj+Entrée : à la ligne").font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        if busy {
                            if ui.add(egui::Button::new(RichText::new("■  Arrêter").color(t.text)).corner_radius(theme::R)).clicked() {
                                stop = true;
                            }
                            if !o.draft.trim().is_empty() && ui.button("Mettre en attente").clicked() {
                                send = true;
                            }
                            ui.spinner();
                        } else if chat::accent_button(ui, &t, "Envoyer").clicked() {
                            send = true;
                        }
                    });
                });
            });
        });
        }
        if let Some(i) = drop_idx
            && let Some(o) = self.open.get_mut(&key)
            && i < o.queue.len()
        {
            o.queue.remove(i);
        }
        if let Some(i) = now_idx
            && let Some(o) = self.open.get_mut(&key)
            && i < o.queue.len()
        {
            let q = o.queue.remove(i);
            o.queue.insert(0, q);
            if o.thread.busy {
                // (le tour s'arrête ; sa fin fait partir la première demande de la file : pump)
                if let Some(c) = o.cli.as_mut() {
                    c.interrupt();
                }
            } else {
                let q = o.queue.remove(0);
                self.send_text(&key, &q);
            }
        }
        if stop
            && let Some(o) = self.open.get_mut(&key)
            && let Some(c) = o.cli.as_mut()
        {
            c.interrupt();
        }
        if send {
            self.send(&key);
        }

        // le fil
        let mut actions = Vec::new();
        {
            let o = self.open.get_mut(&key).expect("ouverte");
            let stick = o.stick;
            let out = ScrollArea::vertical().id_salt(("fil", &key)).auto_shrink([false, false]).stick_to_bottom(stick).show(ui, |ui| {
                Frame::new().inner_margin(Margin::symmetric(24, 16)).show(ui, |ui| {
                    if o.thread.items.is_empty() {
                        ui.add_space(40.0);
                        ui.vertical_centered(|ui| {
                            ui.label(RichText::new("Que voulez-vous faire ?").font(theme::semibold(theme::TITLE)).color(t.text));
                            ui.label(RichText::new(format!("Claude travaillera dans {}", o.cwd)).color(t.text_dim));
                        });
                    }
                    actions = o.thread.show(ui, &t);
                    if o.thread.busy && !matches!(o.thread.items.last(), Some(chat::Item::Assistant { live: true, .. })) {
                        ui.horizontal(|ui| {
                            ui.spinner();
                            ui.label(RichText::new("Claude réfléchit…").color(t.text_faint));
                        });
                    }
                });
            });
            // (remonter dans le fil le décroche du bas ; y revenir l'y raccroche)
            let at_bottom = out.state.clamped_offset().y + out.inner_rect.height() >= out.content_size.y - 4.0;
            o.stick = at_bottom || o.thread.busy && o.stick;
        }
        for a in actions {
            self.do_action(&key, a);
        }
    }

    fn welcome(&mut self, ui: &mut Ui) {
        let t = self.t;
        ui.add_space(ui.available_height() * 0.22);
        ui.vertical_centered(|ui| {
            ui.label(RichText::new("✳").font(theme::semibold(44.0)).color(Color32::from_rgb(0xd9, 0x77, 0x57)));
            ui.label(RichText::new("Claude Code").font(theme::semibold(theme::TITLE + 4.0)).color(t.text));
            ui.add_space(4.0);
            ui.label(RichText::new("L'agent de code d'Anthropic, avec votre abonnement Claude.").color(t.text_dim));
            ui.add_space(16.0);
            if chat::accent_button(ui, &t, "+  Nouvelle conversation").clicked() {
                let cwd = self.org.recent_dirs.first().cloned().unwrap_or_else(|| store::home().to_string_lossy().into_owned());
                self.dialog = Dialog::NewChat { cwd };
            }
            ui.add_space(8.0);
            if !self.sessions.is_empty() {
                ui.label(RichText::new(format!("ou reprenez l'une de vos {} conversations, à gauche", self.sessions.len())).font(egui::FontId::proportional(theme::LABEL)).color(t.text_faint));
            }
        });
    }

    // ---- les dialogues ----

    fn dialogs(&mut self, ctx: &egui::Context) {
        let t = self.t;
        let mut close = false;
        let mut dialog = std::mem::replace(&mut self.dialog, Dialog::None);
        match &mut dialog {
            Dialog::None => {}
            Dialog::Rename { id, text } => {
                let r = modal(ctx, "renommer", "Renommer la conversation", |ui| {
                    let e = ui.add(TextEdit::singleline(text).desired_width(360.0));
                    if ui.memory(|m| m.focused().is_none()) {
                        e.request_focus();
                    }
                    buttons(ui, &t, "Renommer", ui.input(|i| i.key_pressed(Key::Enter)))
                });
                match r {
                    Some(true) if !text.trim().is_empty() => {
                        if let Some(info) = self.info(id).cloned() {
                            match store::rename(&info, text) {
                                Ok(()) => self.rescan(),
                                Err(e) => self.status = Some((format!("Renommer : {e}"), true)),
                            }
                        }
                        close = true;
                    }
                    Some(_) => close = true,
                    None => {}
                }
            }
            Dialog::Delete { id } => {
                let title = self.title_of(id);
                let r = modal(ctx, "supprimer", "Supprimer la conversation ?", |ui| {
                    ui.label(RichText::new(format!("« {title} » sera effacée de ce Korin (son fichier dans ~/.claude). On ne peut pas revenir en arrière.")).color(t.text_dim));
                    buttons(ui, &t, "Supprimer", false)
                });
                match r {
                    Some(true) => {
                        if let Some(o) = self.open.remove(id.as_str()) {
                            drop(o);
                        }
                        if let Some(info) = self.info(id).cloned()
                            && let Err(e) = store::delete(&info)
                        {
                            self.status = Some((format!("Supprimer : {e}"), true));
                        }
                        self.org.forget(id);
                        self.org.save();
                        if self.current.as_deref() == Some(id.as_str()) {
                            self.current = None;
                        }
                        self.rescan();
                        close = true;
                    }
                    Some(false) => close = true,
                    None => {}
                }
            }
            Dialog::NewFolder { text, then_move } => {
                let r = modal(ctx, "dossier", "Nouveau dossier", |ui| {
                    let e = ui.add(TextEdit::singleline(text).hint_text("Nom du dossier").desired_width(320.0));
                    if ui.memory(|m| m.focused().is_none()) {
                        e.request_focus();
                    }
                    buttons(ui, &t, "Créer", ui.input(|i| i.key_pressed(Key::Enter)))
                });
                match r {
                    Some(true) if !text.trim().is_empty() => {
                        let i = self.org.add_folder(text);
                        if let Some(id) = then_move {
                            self.org.move_to(id, Some(i));
                        }
                        self.org.save();
                        close = true;
                    }
                    Some(_) => close = true,
                    None => {}
                }
            }
            Dialog::RenameFolder { i, text } => {
                let r = modal(ctx, "rdossier", "Renommer le dossier", |ui| {
                    let e = ui.add(TextEdit::singleline(text).desired_width(320.0));
                    if ui.memory(|m| m.focused().is_none()) {
                        e.request_focus();
                    }
                    buttons(ui, &t, "Renommer", ui.input(|i| i.key_pressed(Key::Enter)))
                });
                match r {
                    Some(true) if !text.trim().is_empty() => {
                        if let Some(f) = self.org.folders.get_mut(*i) {
                            f.name = text.trim().to_owned();
                        }
                        self.org.save();
                        close = true;
                    }
                    Some(_) => close = true,
                    None => {}
                }
            }
            Dialog::DeleteFolder { i } => {
                let name = self.org.folders.get(*i).map(|f| f.name.clone()).unwrap_or_default();
                let r = modal(ctx, "sdossier", "Supprimer le dossier ?", |ui| {
                    ui.label(RichText::new(format!("Le dossier « {name} » disparaît ; ses conversations restent, hors des dossiers.")).color(t.text_dim));
                    buttons(ui, &t, "Supprimer le dossier", false)
                });
                match r {
                    Some(true) => {
                        self.org.remove_folder(*i);
                        self.org.save();
                        close = true;
                    }
                    Some(false) => close = true,
                    None => {}
                }
            }
            Dialog::NewChat { cwd } => {
                let recent = self.org.recent_dirs.clone();
                let mut pick = false;
                let r = modal(ctx, "nouvelle", "Nouvelle conversation", |ui| {
                    ui.label(RichText::new("Le dossier où Claude travaillera (il y lit et modifie les fichiers, avec votre accord) :").color(t.text_dim));
                    ui.add_space(4.0);
                    ui.horizontal(|ui| {
                        egui::Frame::new().fill(t.field).stroke(egui::Stroke::new(1.0, t.card_border)).corner_radius(theme::R).inner_margin(egui::Margin::symmetric(8, 5)).show(ui, |ui| {
                            ui.set_width(330.0);
                            ui.add(Label::new(RichText::new(format!("🗀  {cwd}")).font(theme::mono(theme::CODE)).color(t.text)).truncate());
                        });
                        if ui.button("Choisir…").on_hover_text("Le sélecteur de dossier de Korin").clicked() {
                            pick = true;
                        }
                    });
                    let others: Vec<&String> = recent.iter().filter(|d| *d != cwd).take(5).collect();
                    if !others.is_empty() {
                        ui.add_space(4.0);
                        ui.label(RichText::new("Récents").font(theme::medium(theme::CAPTION)).color(t.text_faint));
                        for d in others {
                            if ui.add(Label::new(RichText::new(format!("↺ {d}")).font(egui::FontId::proportional(theme::LABEL)).color(t.accent_ink)).sense(Sense::click())).clicked() {
                                *cwd = d.clone();
                            }
                        }
                    }
                    let ok = Path::new(cwd.trim()).is_dir();
                    if !ok {
                        ui.label(RichText::new("Ce dossier n'existe pas.").font(egui::FontId::proportional(theme::LABEL)).color(t.danger));
                    }
                    let r = buttons(ui, &t, "Commencer", ok && ui.input(|i| i.key_pressed(Key::Enter)));
                    if r == Some(true) && !ok { None } else { r }
                });
                if pick {
                    // (le sélecteur s'ouvre hors de la passe d'egui : main.rs, par Host::folder_choose)
                    self.folder_request = Some(cwd.clone());
                }
                match r {
                    Some(true) => {
                        let d = cwd.trim().trim_end_matches('/').to_owned();
                        self.new_chat(if d.is_empty() { "/".into() } else { d });
                        close = true;
                    }
                    Some(false) => close = true,
                    None => {}
                }
            }
            Dialog::Login(l) => {
                if let Some(rx) = &l.rx {
                    l.lines.extend(rx.try_iter());
                }
                let done = l.cli.as_mut().is_some_and(|c| c.try_wait().ok().flatten().is_some());
                let r = modal(ctx, "login", "Se connecter à Claude", |ui| {
                    ui.label(RichText::new("La page de connexion d'Anthropic s'ouvre dans Jet. Connectez-vous avec votre compte Claude (abonnement Pro, Max ou Team) ; si elle vous donne un code, collez-le ici.").color(t.text_dim));
                    ScrollArea::vertical().max_height(180.0).stick_to_bottom(true).show(ui, |ui| {
                        for l in &l.lines {
                            ui.add(Label::new(RichText::new(l).font(theme::mono(theme::CAPTION)).color(t.text_dim)).selectable(true).wrap());
                        }
                    });
                    ui.horizontal(|ui| {
                        ui.add(TextEdit::singleline(&mut l.code).hint_text("Code").desired_width(260.0));
                        if ui.button("Envoyer le code").clicked()
                            && let Some(c) = l.cli.as_mut()
                            && let Some(w) = c.stdin.as_mut()
                        {
                            use std::io::Write;
                            let _ = writeln!(w, "{}", l.code.trim());
                            l.code.clear();
                        }
                    });
                    if done {
                        ui.label(RichText::new("La connexion est finie : renvoyez votre demande.").color(t.success));
                    }
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| if ui.button("Fermer").clicked() { Some(false) } else { None }).inner
                });
                if r.is_some() {
                    if let Some(mut c) = l.cli.take() {
                        let _ = c.kill();
                        let _ = c.wait();
                    }
                    close = true;
                }
            }
        }
        if !close {
            self.dialog = dialog;
        }
        if let Some((msg, err)) = self.status.clone() {
            egui::Area::new(Id::unique("statut")).anchor(egui::Align2::CENTER_BOTTOM, [0.0, -90.0]).show(ctx, |ui| {
                Frame::new().fill(t.card).stroke(Stroke::new(1.0, if err { t.danger } else { t.card_border })).corner_radius(theme::R).inner_margin(Margin::symmetric(12, 8)).show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.label(RichText::new(msg).color(if err { t.danger } else { t.text }));
                        if ui.small_button("✕").clicked() {
                            self.status = None;
                        }
                    });
                });
            });
        }
    }
}

/// Une ligne de la CLI qui répond à une demande de l'app : (numéro, réponse, erreur).
fn answer_of(v: &serde_json::Value, requests: &HashMap<String, Req>) -> Option<(String, serde_json::Value, Option<String>)> {
    if v.get("type").and_then(|x| x.as_str()) != Some("control_response") {
        return None;
    }
    let r = &v["response"];
    let id = r["request_id"].as_str()?;
    if !requests.contains_key(id) {
        return None;
    }
    if r["subtype"].as_str() == Some("success") {
        Some((id.to_owned(), r["response"].clone(), None))
    } else {
        Some((id.to_owned(), serde_json::Value::Null, Some(r["error"].as_str().unwrap_or("refusé par Claude Code").to_owned())))
    }
}

fn plan_name(p: &str) -> String {
    match p {
        "pro" => "Pro".into(),
        "max" => "Max".into(),
        "team" => "Team".into(),
        "enterprise" => "Enterprise".into(),
        o => o.to_owned(),
    }
}

/// « il y a 3 h » d'une date ISO du service.
fn iso_ago(s: &str) -> String {
    match env::iso_secs(s) {
        Some(t) if t > 0 => store::ago(std::time::UNIX_EPOCH + Duration::from_secs(t as u64)),
        _ => String::new(),
    }
}

/// Une ligne de la liste : titre, sous-titre, sélectionnée ou non, un point si Claude travaille.
fn row(ui: &mut Ui, t: &Tokens, title: &str, sub: &str, selected: bool, busy: bool) -> egui::Response {
    let fill = if selected { t.accent_soft } else { Color32::TRANSPARENT };
    let r = Frame::new().fill(fill).corner_radius(theme::R).inner_margin(Margin::symmetric(8, 5)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.horizontal(|ui| {
            if busy {
                ui.label(RichText::new("●").font(egui::FontId::proportional(theme::CAPTION)).color(t.accent_ink));
            }
            ui.add(Label::new(RichText::new(title).font(egui::FontId::proportional(theme::SMALL)).color(t.text)).truncate().selectable(false));
        });
        ui.add(Label::new(RichText::new(sub).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint)).truncate().selectable(false));
    });
    let r = ui.interact(r.response.rect, ui.scope_id().with(("row", title, sub)), Sense::click_and_drag());
    if r.hovered() && !selected {
        ui.painter().rect_filled(r.rect, theme::R, t.hover.gamma_multiply(0.5));
    }
    r
}

/// Une zone où déposer une conversation glissée (son identifiant) : soulignée pendant le survol, rend l'identifiant
/// au lâcher.
fn drop_target(ui: &mut Ui, t: &Tokens, rect: egui::Rect, salt: impl std::hash::Hash + std::fmt::Debug) -> Option<String> {
    let r = ui.interact(rect, Id::unique(("depot", salt)), Sense::hover());
    if r.dnd_hover_payload::<String>().is_some() {
        ui.painter().rect_stroke(rect.expand(2.0), theme::R, Stroke::new(1.5, t.accent_ink), egui::StrokeKind::Outside);
    }
    r.dnd_release_payload::<String>().map(|p| (*p).clone())
}

fn section(ui: &mut Ui, t: &Tokens, name: &str) {
    ui.add_space(8.0);
    ui.label(RichText::new(name).font(theme::medium(theme::CAPTION)).color(t.text_faint));
}

fn combo(ui: &mut Ui, id: &str, value: &mut String, choices: &[(&str, &str)], default: &str) {
    let cur = choices.iter().find(|(k, _)| *k == value.as_str() || (value.is_empty() && *k == default)).map(|(_, l)| *l).unwrap_or(choices[0].1);
    egui::ComboBox::from_id_salt(id).selected_text(cur).show_ui(ui, |ui| {
        for (k, l) in choices {
            ui.selectable_value(value, (*k).to_owned(), *l);
        }
    });
}

fn theme_menu(ui: &mut Ui, org: &mut Organize, dark: &mut Option<bool>) {
    ui.menu_button("◑", |ui| {
        for (k, l) in [("system", "Comme Korin"), ("dark", "Sombre"), ("light", "Clair")] {
            let cur = if org.theme.is_empty() { "system" } else { org.theme.as_str() };
            if ui.radio(cur == k, l).clicked() {
                org.theme = k.to_owned();
                org.save();
                *dark = None;
                ui.close();
            }
        }
    })
    .response
    .on_hover_text("Thème");
}

/// Une fenêtre modale : rend Some(true) (valider), Some(false) (annuler, Échap, clic dehors) ou None.
fn modal(ctx: &egui::Context, id: &str, title: &str, body: impl FnOnce(&mut Ui) -> Option<bool>) -> Option<bool> {
    let mut out = None;
    let frame = Frame::popup(&ctx.global_style()).inner_margin(Margin::same(20)).corner_radius(theme::R_LG);
    let r = egui::Modal::new(Id::unique(("modal", id))).frame(frame).show(ctx, |ui| {
        ui.set_min_width(380.0);
        ui.label(RichText::new(title).font(theme::semibold(theme::SECTION)));
        ui.add_space(8.0);
        out = body(ui);
    });
    if out.is_none() && r.should_close() {
        out = Some(false);
    }
    out
}

fn buttons(ui: &mut Ui, t: &Tokens, ok: &str, enter: bool) -> Option<bool> {
    ui.add_space(10.0);
    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
        if chat::accent_button(ui, t, ok).clicked() || enter {
            return Some(true);
        }
        if ui.button("Annuler").clicked() {
            return Some(false);
        }
        None
    })
    .inner
}

/// Les séquences de couleur d'un terminal, retirées.
fn strip_ansi(s: &str) -> String {
    let mut out = String::new();
    let mut it = s.chars().peekable();
    while let Some(c) = it.next() {
        if c == '\u{1b}' {
            if it.peek() == Some(&'[') {
                it.next();
                for d in it.by_ref() {
                    if d.is_ascii_alphabetic() {
                        break;
                    }
                }
            }
            continue;
        }
        out.push(c);
    }
    out
}
