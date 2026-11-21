//! La fenêtre : à gauche les conversations (recherche, épinglées, dossiers, puis les autres par date ; un clic les
//! ouvre, le clic droit les renomme, épingle, range ou supprime ; on les glisse sur un dossier), à droite le fil de la
//! conversation ouverte, et en bas le champ de la demande. Plusieurs conversations peuvent travailler en même temps :
//! chacune a son process de la CLI.

use std::collections::{HashMap, HashSet};
use std::path::Path;
use std::time::{Duration, Instant};

use egui::{Align, Color32, Frame, Id, Key, KeyboardShortcut, Label, Layout, Margin, Modifiers, RichText, ScrollArea, Sense, Stroke, TextEdit, Ui};

use crate::chat::{self, Action, PermState, Thread};
use crate::cli::{Cli, Event, Launch};
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
}

impl Open {
    fn new(cwd: String) -> Open {
        Open { thread: Thread::default(), cli: None, cwd, draft: String::new(), stderr: Vec::new(), stick: true }
    }
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
    t: Tokens,
    dark: Option<bool>,
    last_scan: Instant,
    counter: u32,
    status: Option<(String, bool)>,
    /// le thème du système (Korin : demandé à l'hôte)
    pub system_dark: Box<dyn Fn() -> bool>,
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
            t: Tokens::studio(),
            dark: None,
            last_scan: Instant::now(),
            counter: 0,
            status: None,
            system_dark,
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

    /// Envoie la demande tapée dans la conversation `key` (lance la CLI s'il le faut).
    fn send(&mut self, key: &str) {
        let (model, mode) = (self.org.model.clone(), self.org.permission_mode.clone());
        let Some(o) = self.open.get_mut(key) else { return };
        let text = o.draft.trim().to_owned();
        if text.is_empty() || o.thread.busy {
            return;
        }
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
        o.draft.clear();
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
        for (key, o) in self.open.iter_mut() {
            let Some(c) = o.cli.as_mut() else { continue };
            active = true;
            let mut closed = false;
            for ev in c.poll() {
                match ev {
                    Event::Json(v) => {
                        let was_busy = o.thread.busy;
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
                    let tail = o.stderr.iter().rev().take(6).rev().cloned().collect::<Vec<_>>().join("\n");
                    o.thread.note(format!("Claude Code s'est arrêté pendant le tour (code {}).\n{tail}", code.map_or("?".into(), |c| c.to_string())), true);
                }
                o.cli = None;
                finished = true;
            }
        }
        for (old, new) in rekey {
            if let Some(o) = self.open.remove(&old) {
                self.open.insert(new.clone(), o);
                if self.current.as_deref() == Some(old.as_str()) {
                    self.current = Some(new);
                }
            }
        }
        if finished || self.last_scan.elapsed() > Duration::from_secs(8) {
            self.rescan();
        }
        active
    }

    fn do_action(&mut self, key: &str, a: Action) {
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
        egui::CentralPanel::default().frame(Frame::new().fill(t.canvas)).show(ui, |ui| self.main(ui));
        self.dialogs(&ctx);
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
        });
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
        Frame::new().fill(t.canvas).inner_margin(Margin { left: 20, right: 120, top: 12, bottom: 10 }).show(ui, |ui| {
            ui.horizontal(|ui| {
                let r = ui.add(Label::new(RichText::new(&title).font(theme::semibold(theme::SECTION)).color(t.text)).sense(Sense::click()).truncate());
                if saved && r.on_hover_text("Renommer").clicked() {
                    self.dialog = Dialog::Rename { id: key.clone(), text: title.clone() };
                }
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
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
                    combo(ui, "mode", &mut self.org.permission_mode, &MODES, "default");
                    combo(ui, "modele", &mut self.org.model, &MODELS, "");
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

        // le champ de la demande, en bas
        let mut send = false;
        let mut stop = false;
        let no_dialog = matches!(self.dialog, Dialog::None);
        egui::Panel::bottom(("composer", &key)).frame(Frame::new().fill(t.canvas).inner_margin(Margin { left: 20, right: 20, top: 8, bottom: 14 })).show(ui, |ui| {
            let o = self.open.get_mut(&key).expect("ouverte");
            let busy = o.thread.busy;
            Frame::new().fill(t.field).stroke(Stroke::new(1.0, t.field_border)).corner_radius(theme::R_LG).inner_margin(Margin::same(10)).show(ui, |ui| {
                let resp = ui.add(
                    TextEdit::multiline(&mut o.draft)
                        .hint_text(if busy { "Claude travaille… (votre prochaine demande peut attendre ici)" } else { "Demandez à Claude…" })
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
                            ui.spinner();
                        } else if chat::accent_button(ui, &t, "Envoyer").clicked() {
                            send = true;
                        }
                    });
                });
            });
        });
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
                let r = modal(ctx, "nouvelle", "Nouvelle conversation", |ui| {
                    ui.label(RichText::new("Le dossier où Claude travaillera (il y lit et modifie les fichiers, avec votre accord) :").color(t.text_dim));
                    let e = ui.add(TextEdit::singleline(cwd).font(theme::mono(theme::CODE)).desired_width(420.0));
                    if ui.memory(|m| m.focused().is_none()) {
                        e.request_focus();
                    }
                    for d in recent.iter().take(5) {
                        if ui.add(Label::new(RichText::new(format!("↺ {d}")).font(egui::FontId::proportional(theme::LABEL)).color(t.accent_ink)).sense(Sense::click())).clicked() {
                            *cwd = d.clone();
                        }
                    }
                    let ok = Path::new(cwd.trim()).is_dir();
                    if !ok {
                        ui.label(RichText::new("Ce dossier n'existe pas.").font(egui::FontId::proportional(theme::LABEL)).color(t.danger));
                    }
                    let r = buttons(ui, &t, "Commencer", ok && ui.input(|i| i.key_pressed(Key::Enter)));
                    if r == Some(true) && !ok { None } else { r }
                });
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
