//! L'arrière-plan (`pages/background.rs` de ClaudeCodeUI) : les sessions `claude --bg` (et les sessions interactives
//! qui tournent), lues par `claude agents --json --all` (`ccui_core::background_agents::list`) toutes les 15 s, dans
//! un ordre stable. Journaux, arrêter, relancer et retirer passent par les sous-commandes de la CLI
//! (`background_agents::run`) ; relancer et retirer se confirment sur place (un premier clic arme, le second lance ;
//! 5 s ou un autre clic désarment). Ouvrir montre la conversation ici. « Besoin de toi » filtre celles qui attendent.

use std::collections::HashSet;
use std::time::Duration;

use ccui_shared::api::{BgList, BgRunResult};
use ccui_shared::bg_agents::Agent;
use ccui_shared::model::{BgTask, Time};
use ccui_shared::{fmt, i18n, t, text};
use egui::{Align, Frame, Key, Layout, Margin, RichText, ScrollArea, Stroke, Ui};

use super::parts::{self, one_line, table_head, table_row};
use crate::app::Cx;
use crate::backend::Task;
use crate::theme;
use crate::widgets::{self, icon, kbd};

/// La relecture des 15 s garde chaque ligne connue à sa place (les nouvelles à la fin) : rien ne bouge sous le pointeur.
pub fn keep_order(fresh: Vec<Agent>, old: &[Agent]) -> Vec<Agent> {
    let mut v = fresh;
    v.sort_by_key(|r| old.iter().position(|o| o.id == r.id).unwrap_or(usize::MAX));
    v
}

/// Le dossier d'une session, et son dépôt quand c'est un worktree (« .claude/worktrees/<dossier> » d'un dépôt).
pub fn place(cwd: &str, home: &str, sep: &str) -> (String, Option<String>) {
    let p = cwd.replace('\\', "/");
    let p = p.trim_end_matches('/');
    match p.find("/.claude/worktrees/") {
        Some(i) if i > 0 => (p[i + 19..].to_string(), Some(p[..i].rsplit('/').next().unwrap_or_default().to_string())),
        _ => {
            let f = p.rsplit('/').next().unwrap_or_default();
            (if f.is_empty() { text::short(p, home, sep) } else { f.to_string() }, None)
        }
    }
}

/// Le mot de la CLI dans la langue de l'interface ; un mot inconnu tel que la CLI le dit.
pub fn state_word(s: &str) -> String {
    let key = format!("Bg.St.{s}");
    if i18n::has(&key) { t!(&key) } else { s.to_string() }
}

/// Figée sans conversation : plus rien à reprendre ni à répondre.
pub fn gone(a: &Agent, has_transcript: bool) -> bool {
    a.stale && a.session_id.is_some() && !has_transcript
}

/// Copier `claude attach` : une session d'arrière-plan sans conversation ici à ouvrir, sauf figée sans elle.
pub fn attachable(a: &Agent, has_transcript: bool) -> bool {
    a.background() && (a.session_id.is_none() || !has_transcript) && !(a.stale && a.session_id.is_some())
}

type Loaded = Result<(BgList, Vec<String>), String>;

/// Une commande lancée : l'agent, la commande, sa place dans la liste, le résultat.
struct Running {
    agent: Agent,
    command: String,
    index: usize,
    task: Task<Result<BgRunResult, String>>,
}

enum Do {
    Open(String),
    Copy(String),
    Act(String, Agent),
    Arm(String, Agent),
    Close,
    Select(String),
}

#[derive(Default)]
pub struct Background {
    frame: u64,
    rows: Option<Vec<Agent>>,
    /// Les sessions qui ont une transcription ici.
    have: HashSet<String>,
    error: Option<String>,
    output: Option<String>,
    shown: Option<String>,
    cursor: Option<String>,
    out_command: String,
    /// (agent, commande, depuis quand) : le premier clic de rm / respawn.
    armed: Option<(String, String, f64)>,
    failed: bool,
    daemon_up: bool,
    needs_only: bool,
    read_at: Option<Time>,
    notice: Option<String>,
    /// Le prochain chargement trie (à l'arrivée, Rafraîchir) au lieu de garder l'ordre.
    sort: bool,
    last_load: f64,
    t_load: Option<Task<Loaded>>,
    run: Option<Running>,
    copied: Option<(String, f64)>,
}

impl Background {
    fn load(&mut self, cx: &Cx, sort: bool, now: f64) {
        if self.t_load.is_some() {
            return;
        }
        self.sort = sort;
        self.last_load = now;
        self.error = None;
        let core = cx.be.core.clone();
        self.t_load = Some(cx.be.task(async move {
            let list = ccui_core::background_agents::list(&core.sink).await?;
            let ids: Vec<String> = list.rows.iter().filter_map(|r| r.session_id.clone()).collect();
            let exist = tokio::task::spawn_blocking(move || ids.into_iter().filter(|id| ccui_core::transcript_store::find(id).is_some()).collect::<Vec<_>>())
                .await
                .unwrap_or_default();
            Ok((list, exist))
        }));
    }

    fn poll(&mut self, cx: &Cx, now: f64) {
        if let Some(r) = self.t_load.as_ref().and_then(Task::take) {
            self.t_load = None;
            match r {
                Ok((list, exist)) => {
                    let fresh = match &self.rows {
                        Some(old) if !self.sort => keep_order(list.rows, old),
                        _ => list.rows,
                    };
                    if !fresh.iter().any(Agent::needs_you) {
                        self.needs_only = false;
                    }
                    self.have = exist.into_iter().collect();
                    self.rows = Some(fresh);
                    self.daemon_up = list.daemon_up;
                    self.read_at = Some(chrono::Utc::now());
                }
                Err(e) => self.error = Some(t!("Session.ActionFailed", e.trim_end().to_string())),
            }
        }
        let done = self.run.as_ref().and_then(|r| r.task.take());
        if let Some(got) = done {
            let Running { agent, command, index, .. } = self.run.take().expect("commande en cours");
            let ok = match got {
                Ok(r) => {
                    self.failed = !r.ok;
                    // les journaux sont une sortie de terminal : échappements retirés, lignes réécrites par \r gardées
                    // dans leur dernier état
                    self.output = Some(if command == "logs" {
                        BgTask::tail(&r.text, 200)
                    } else if !r.text.is_empty() {
                        r.text
                    } else {
                        t!(if r.ok { "Bg.Done" } else { "Bg.Failed" })
                    });
                    r.ok
                }
                Err(e) => {
                    self.failed = true;
                    self.output = Some(t!("Session.ActionFailed", e.trim_end().to_string()));
                    false
                }
            };
            if command != "logs" {
                // relu ; la ligne partie (rm) : sa sortie part avec elle, on le dit, le curseur va à sa voisine
                let core = cx.be.core.clone();
                let list = cx.be.block(async move { ccui_core::background_agents::list(&core.sink).await });
                if let Ok(list) = list {
                    let old = self.rows.clone().unwrap_or_default();
                    let rows = keep_order(list.rows, &old);
                    self.daemon_up = list.daemon_up;
                    self.read_at = Some(chrono::Utc::now());
                    if !rows.iter().any(|r| r.id == agent.id) {
                        self.notice = Some(t!(if ok { "Bg.Removed" } else { "Bg.Gone2" }, agent.label()));
                        self.output = None;
                        self.shown = None;
                        self.cursor = rows.get(index).or(rows.last()).map(|n| n.id.clone());
                    }
                    self.rows = Some(rows);
                }
            }
        }
        if self.armed.as_ref().is_some_and(|(_, _, at)| now - at > 5.0) {
            self.armed = None;
        }
    }

    fn act(&mut self, cx: &Cx, command: &str, a: Agent) {
        if self.run.is_some() {
            return;
        }
        let index = self.rows.as_ref().and_then(|r| r.iter().position(|x| x.id == a.id)).unwrap_or(0);
        self.notice = None;
        self.armed = None;
        self.shown = Some(a.id.clone());
        self.out_command = command.to_string();
        let (c, id) = (command.to_string(), a.id.clone());
        let task = cx.be.task(async move { ccui_core::background_agents::run(&c, &id).await });
        self.run = Some(Running { agent: a, command: command.to_string(), index, task });
    }

    /// rm et respawn se confirment sur place : le premier clic arme, le second lance.
    fn confirm(&mut self, cx: &Cx, a: Agent, command: &str, now: f64) {
        if self.run.is_some() {
            return;
        }
        match &self.armed {
            Some((i, c, at)) if *i == a.id && c == command => {
                if now - at >= 0.4 {
                    self.act(cx, command, a); // un double-clic arme, ne confirme jamais
                }
            }
            _ => self.armed = Some((a.id.clone(), command.to_string(), now)),
        }
    }

    fn is_running(&self, id: &str, cmd: &str) -> bool {
        self.run.as_ref().is_some_and(|r| r.agent.id == id && r.command == cmd)
    }

    fn is_armed(&self, id: &str, cmd: &str) -> bool {
        self.armed.as_ref().is_some_and(|(i, c, _)| i == id && c == cmd)
    }

    fn has_t(&self, a: &Agent) -> bool {
        a.session_id.as_ref().is_some_and(|s| self.have.contains(s))
    }

    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let now = ui.ctx().input(|i| i.time);
        let nr = ui.ctx().cumulative_frame_nr();
        if self.frame == 0 || nr > self.frame + 1 {
            self.load(cx, true, now);
        }
        self.frame = nr;
        // relu toutes les 15 s (pas pendant une commande)
        if self.run.is_none() && now - self.last_load > 15.0 {
            self.load(cx, false, now);
        }
        self.poll(cx, now);
        ui.ctx().request_repaint_after(Duration::from_secs(if self.run.is_some() || self.t_load.is_some() { 0 } else { 5 }).max(Duration::from_millis(150)));
        let t = cx.t;

        let rows = self.rows.clone();
        let needs = rows.as_ref().map_or(0, |r| r.iter().filter(|x| x.needs_you()).count());
        let mut refresh = false;
        widgets::pane_header(
            ui,
            &t,
            |ui| {
                ui.label(RichText::new(t!("Nav.Background")).font(theme::semibold(theme::BODY)));
                if let (Some(r), Some(at)) = (&rows, self.read_at) {
                    let nb = r.iter().filter(|x| x.background()).count();
                    let counts = format!("{} · {} · {}", i18n::plural(r.len() as i64, "Bg.Sessions"), t!("Bg.Split", nb, r.len() - nb), t!("Bg.ReadShort", fmt::hms(at)));
                    widgets::mono_dim(ui, &t, &counts).on_hover_text(t!("Bg.ReadAt", fmt::long_time(at)));
                    if needs > 0 {
                        let mut txt = i18n::plural(needs as i64, "Bg.Needs");
                        if self.needs_only {
                            txt = format!("{txt} · {}", t!("Bg.Showing", needs, r.len()));
                        }
                        let b = ui.add(egui::Button::new(RichText::new(txt).color(t.warning).font(theme::medium(theme::CAPTION))).selected(self.needs_only));
                        if b.on_hover_text(t!(if self.needs_only { "Bg.ShowAll" } else { "Bg.ShowNeeds" })).clicked() {
                            self.needs_only = !self.needs_only;
                        }
                    }
                }
            },
            |ui| {
                refresh = ui.add_enabled(self.t_load.is_none(), egui::Button::new(format!("{} {}", icon("refresh"), t!("Wt.Refresh")))).on_hover_text(t!("Bg.RefreshHint")).clicked();
            },
        );
        if refresh {
            self.load(cx, true, now);
        }

        let shown_rows: Vec<Agent> = {
            let r = rows.clone().unwrap_or_default();
            if self.needs_only && r.iter().any(Agent::needs_you) { r.into_iter().filter(Agent::needs_you).collect() } else { r }
        };
        let cur = self.cursor.clone().filter(|c| shown_rows.iter().any(|x| &x.id == c)).or_else(|| shown_rows.first().map(|x| x.id.clone()));
        let mut todo: Vec<Do> = Vec::new();
        self.keys(ui, cx, &shown_rows, cur.as_deref(), &mut todo);

        ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
            Frame::new().inner_margin(Margin::symmetric(28, 24)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                ui.spacing_mut().item_spacing.y = 8.0;
                parts::label(ui, &t, &t!("Bg.Text"), theme::SMALL, t.text_dim);
                if let Some(n) = &self.notice {
                    widgets::dim(ui, &t, n);
                }
                if let Some(e) = &self.error {
                    widgets::error_text(ui, &t, e);
                }
                match &rows {
                    // une première lecture ratée le dit, jamais « aucune session ne tourne »
                    None => {
                        if self.error.is_none() {
                            widgets::dim(ui, &t, &t!("Bg.Loading"));
                        }
                    }
                    Some(r) if r.is_empty() => {
                        ui.add_space(8.0);
                        parts::label(ui, &t, &t!("Bg.None"), theme::SMALL, t.text);
                    }
                    Some(r) => {
                        if !self.daemon_up {
                            // une session figée pourrait reprendre
                            let can_run = r.iter().any(|x| x.stale && x.session_id.as_ref().is_none_or(|s| self.have.contains(s)));
                            Frame::new().fill(t.warning.gamma_multiply(0.10)).corner_radius(theme::R).inner_margin(Margin::symmetric(12, 8)).show(ui, |ui| {
                                ui.set_width(ui.available_width());
                                ui.horizontal_wrapped(|ui| {
                                    ui.label(RichText::new(t!("Bg.DaemonDown")).strong().color(t.text));
                                    parts::label(ui, &t, &t!("Bg.DaemonDownText"), theme::LABEL, t.text_dim);
                                    if can_run && ui.link(t!("Bg.DaemonRun")).on_hover_text(t!("Bg.DaemonRunHint")).clicked() {
                                        todo.push(Do::Copy("claude daemon run".into()));
                                    }
                                });
                            });
                        }
                        self.table(ui, cx, &shown_rows, cur.as_deref(), now, &mut todo);
                        let any_attach = shown_rows.iter().any(|x| attachable(x, self.has_t(x)));
                        ui.horizontal_wrapped(|ui| {
                            ui.spacing_mut().item_spacing.x = 4.0;
                            kbd(ui, &t, "J");
                            kbd(ui, &t, "K");
                            widgets::dim(ui, &t, &format!("{} · ", t!("Bg.KeyMove")));
                            kbd(ui, &t, "↵");
                            widgets::dim(ui, &t, &format!("{} · ", t!(if any_attach { "Bg.KeyOpen" } else { "Bg.KeyOpenOnly" })));
                            kbd(ui, &t, "R");
                            widgets::dim(ui, &t, &format!("{} · ", t!("Bg.KeyRefresh")));
                            kbd(ui, &t, "Esc");
                            widgets::dim(ui, &t, &t!("Bg.KeyBack"));
                        });
                        // les verbes sont à l'écran : ce que chacun fait, pas seulement dans son info-bulle
                        if shown_rows.iter().any(|x| x.background() && !x.stale) {
                            widgets::dim(ui, &t, &t!("Bg.Legend"));
                        }
                    }
                }
            });
        });

        for d in todo {
            match d {
                Do::Open(s) => cx.open_session(&s),
                Do::Copy(s) => {
                    ui.ctx().copy_text(s.clone());
                    self.copied = Some((s, now));
                }
                Do::Act(cmd, a) => self.act(cx, &cmd, a),
                Do::Arm(cmd, a) => self.confirm(cx, a, &cmd, now),
                Do::Close => {
                    self.output = None;
                    self.shown = None;
                }
                Do::Select(id) => self.cursor = Some(id),
            }
        }
    }

    /// J K bouger, Entrée ouvrir (ou copier `claude attach`), R relire, Échap la première ligne.
    fn keys(&mut self, ui: &mut Ui, cx: &mut Cx, rows: &[Agent], cur: Option<&str>, todo: &mut Vec<Do>) {
        if rows.is_empty() || cx.ui.palette_open || ui.ctx().memory(|m| m.focused().is_some()) {
            return;
        }
        let plain = egui::Modifiers::NONE;
        let (j, k, enter, r, esc) = ui.ctx().input_mut(|i| {
            (
                i.consume_key(plain, Key::J) || i.consume_key(plain, Key::ArrowDown),
                i.consume_key(plain, Key::K) || i.consume_key(plain, Key::ArrowUp),
                i.consume_key(plain, Key::Enter),
                i.consume_key(plain, Key::R),
                i.key_pressed(Key::Escape),
            )
        });
        let i = cur.and_then(|c| rows.iter().position(|x| x.id == c)).unwrap_or(0);
        if j {
            self.cursor = Some(rows[(i + 1).min(rows.len() - 1)].id.clone());
        }
        if k {
            self.cursor = Some(rows[i.saturating_sub(1)].id.clone());
        }
        if esc {
            self.cursor = rows.first().map(|x| x.id.clone());
        }
        if enter {
            let a = &rows[i];
            if let Some(s) = a.session_id.clone().filter(|_| self.has_t(a)) {
                todo.push(Do::Open(s));
            } else if a.background() && attachable(a, self.has_t(a)) {
                todo.push(Do::Copy(format!("claude attach {}", a.id)));
            }
        }
        if r {
            let now = ui.ctx().input(|i| i.time);
            self.load(cx, true, now);
        }
    }

    fn table(&self, ui: &mut Ui, cx: &Cx, rows: &[Agent], cur: Option<&str>, now: f64, todo: &mut Vec<Do>) {
        let t = cx.t;
        let (home, sep) = (cx.app.boot.home.clone(), cx.app.boot.sep.clone());
        let w = parts::widths(ui.available_width(), &[0.0, 0.0, 90.0, 380.0], &[1.0, 1.0, 0.0, 0.0]);
        let right = [false, false, true, true];
        table_head(ui, &t, &w, &right, &[t!("Bg.Col.Session"), t!("Col.State"), t!("Bg.Col.Started"), String::new()]);
        let utc = chrono::Utc::now();
        let busy = self.run.is_some();
        for a in rows {
            let has_t = self.has_t(a);
            let is_gone = gone(a, has_t);
            let bg = a.background();
            let sel = cur == Some(a.id.as_str()) || self.shown.as_deref() == Some(a.id.as_str());
            let mark = a.needs_you().then_some(t.warning);
            let r = table_row(ui, &t, &w, &right, 62.0, sel, mark, |c, ui| match c {
                0 => {
                    let (folder, repo) = place(&a.cwd, &home, &sep);
                    ui.vertical(|ui| {
                        ui.spacing_mut().item_spacing.y = 2.0;
                        let n = one_line(ui, RichText::new(a.label()).font(theme::medium(theme::LABEL)).color(t.text));
                        if bg {
                            n.on_hover_text(&a.id);
                        }
                        ui.horizontal(|ui| {
                            ui.spacing_mut().item_spacing.x = 4.0;
                            widgets::tag(ui, &t, &t!(if bg { "Bg.Kind.Background" } else { "Bg.Kind.Interactive" }));
                            let place = match repo {
                                Some(r) => format!("{r} · {folder}"),
                                None => folder,
                            };
                            let title = match a.started_at.map(fmt::short_date_time) {
                                Some(g) => format!("{} · {}", a.cwd, t!("Bg.StartedAt", g)),
                                None => a.cwd.clone(),
                            };
                            one_line(ui, RichText::new(place).font(theme::mono(theme::CAPTION)).color(t.text_faint)).on_hover_text(title);
                        });
                    });
                }
                1 => {
                    let color = if a.needs_you() {
                        t.warning
                    } else if a.stale {
                        t.text_faint
                    } else if a.working() {
                        t.success
                    } else {
                        t.accent_ink
                    };
                    let word = if a.stale { t!("Bg.FrozenState", state_word(&a.state)) } else { state_word(&a.state) };
                    let asks = matches!(a.state.as_str(), "blocked" | "waiting").then(|| a.needs.clone().or(a.waiting_for.clone())).flatten();
                    ui.vertical(|ui| {
                        ui.spacing_mut().item_spacing.y = 2.0;
                        ui.horizontal(|ui| {
                            widgets::dot(ui, color, !a.stale);
                            ui.label(RichText::new(word).font(egui::FontId::proportional(theme::LABEL)).color(color));
                        });
                        if let Some(q) = asks {
                            let s = t!(if is_gone { "Bg.Asked" } else { "Bg.Asks" }, q);
                            one_line(ui, RichText::new(&s).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim)).on_hover_text(&s);
                        }
                        if let Some(at) = a.last_at.filter(|_| bg) {
                            let mut s = t!("Bg.LastStep", fmt::age(at, utc));
                            if is_gone {
                                s = format!("{s} · {}", t!("Bg.GoneShort"));
                            }
                            one_line(ui, RichText::new(s).font(theme::mono(theme::CAPTION - 1.0)).color(t.text_faint))
                                .on_hover_text(if is_gone { t!("Bg.GoneVerdict") } else { t!("Bg.LastStepAt", fmt::short_date_time(at)) });
                        }
                    });
                }
                2 => {
                    let s = a.started_at.map_or_else(|| "—".to_string(), |s| fmt::age(s, utc));
                    let l = ui.label(RichText::new(s).font(theme::mono(theme::CAPTION)).color(t.text_dim));
                    if let Some(g) = a.started_at.map(fmt::short_date_time) {
                        l.on_hover_text(g);
                    }
                }
                _ => {
                    ui.spacing_mut().item_spacing.x = 4.0;
                    let dots = |cmd: &str| if self.is_running(&a.id, cmd) { "…" } else { "" };
                    let bypass = a.mode.as_deref() == Some("bypassPermissions");
                    let respawn_hint = if bypass { format!("{} {}", t!("Bg.RespawnHint"), t!("Bg.RespawnBypass")) } else { t!("Bg.RespawnHint") };
                    // (de droite à gauche : retirer, arrêter, relancer, journaux, attacher, ouvrir)
                    if bg {
                        let armed = self.is_armed(&a.id, "rm");
                        let label = if armed { t!("Bg.RemoveConfirm") } else { format!("{}{}", t!("Bg.Remove"), dots("rm")) };
                        let b = ui.add_enabled(!busy, egui::Button::new(RichText::new(label).color(t.danger)).stroke(Stroke::new(1.0, t.danger.gamma_multiply(if armed { 1.0 } else { 0.5 }))));
                        if b.on_hover_text(t!("Bg.RemoveHint")).clicked() {
                            todo.push(Do::Arm("rm".into(), a.clone()));
                        }
                    }
                    // « sa conversation est gardée » : pas quand il n'y en a pas
                    if bg && self.daemon_up && !is_gone && !matches!(a.state.as_str(), "done" | "stopped" | "failed") {
                        let b = ui.add_enabled(!busy, egui::Button::new(format!("{}{}", t!("Bg.Stop"), dots("stop"))).frame(false));
                        if b.on_hover_text(t!("Bg.StopHint")).clicked() {
                            todo.push(Do::Act("stop".into(), a.clone()));
                        }
                    }
                    // elle reprendrait une conversation qui n'est plus sur le disque : pas proposé
                    if bg && !is_gone {
                        let armed = self.is_armed(&a.id, "respawn");
                        let label = if armed { t!("Bg.RespawnConfirm") } else { format!("{}{}", t!("Bg.Respawn"), dots("respawn")) };
                        let b = ui.add_enabled(!busy, egui::Button::new(label).frame(armed));
                        if b.on_hover_text(&respawn_hint).clicked() {
                            todo.push(Do::Arm("respawn".into(), a.clone()));
                        }
                    }
                    if bg && self.daemon_up && ui.add_enabled(!busy, egui::Button::new(format!("{}{}", t!("Bg.Logs"), dots("logs"))).frame(false)).clicked() {
                        todo.push(Do::Act("logs".into(), a.clone()));
                    }
                    if bg && attachable(a, has_t) {
                        let cmd = format!("claude attach {}", a.id);
                        let copied = self.copied.as_ref().is_some_and(|(c, at)| *c == cmd && now - at < 2.0);
                        let label = if copied { format!("{} {}", icon("check"), t!("Bg.Attach")) } else { t!("Bg.Attach") };
                        if widgets::ghost(ui, &t, &label).on_hover_text(t!("Bg.AttachHint", a.id.clone())).clicked() {
                            todo.push(Do::Copy(cmd));
                        }
                    }
                    if has_t && ui.button(t!("Action.Open")).on_hover_text(t!(if bg { "Bg.OpenHint" } else { "Bg.OpenLive" })).clicked()
                        && let Some(s) = a.session_id.clone()
                    {
                        todo.push(Do::Open(s));
                    }
                }
            });
            if r.clicked() {
                todo.push(Do::Select(a.id.clone()));
            }
            // l'avertissement d'une commande armée, sous sa ligne
            if let Some((_, cmd, _)) = self.armed.as_ref().filter(|(i, _, _)| *i == a.id) {
                let bypass = a.mode.as_deref() == Some("bypassPermissions");
                let hint = if cmd == "rm" {
                    t!("Bg.RemoveHint")
                } else if bypass {
                    format!("{} {}", t!("Bg.RespawnHint"), t!("Bg.RespawnBypass"))
                } else {
                    t!("Bg.RespawnHint")
                };
                Frame::new().fill(t.warning.gamma_multiply(0.10)).inner_margin(Margin::symmetric(12, 6)).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.label(RichText::new(hint).font(egui::FontId::proportional(theme::CAPTION)).color(t.text));
                });
            }
            // la sortie de la commande, sous sa ligne
            if self.shown.as_deref() == Some(a.id.as_str())
                && let Some(out) = &self.output
            {
                let border = if self.failed { t.danger } else { t.card_border };
                Frame::new().fill(t.code_bg).stroke(Stroke::new(1.0, border)).corner_radius(theme::R).inner_margin(Margin::same(10)).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.horizontal(|ui| {
                        let title = t!(if self.failed { "Bg.FailedOf" } else { "Bg.OutputOf" }, self.out_command.clone(), a.label());
                        ui.label(RichText::new(title).strong().color(if self.failed { t.danger } else { t.text }));
                        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                            if widgets::ghost(ui, &t, &t!("Action.Close")).clicked() {
                                todo.push(Do::Close);
                            }
                        });
                    });
                    ScrollArea::vertical().id_salt(("bg-out", &a.id)).max_height(320.0).show(ui, |ui| {
                        ui.add(egui::Label::new(RichText::new(out).font(theme::mono(theme::CODE - 1.0)).color(t.text_dim)).wrap().selectable(true));
                    });
                });
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn agent(id: &str) -> Agent {
        Agent { id: id.into(), kind: "background".into(), ..Agent::default() }
    }

    #[test]
    fn order_is_kept_new_last() {
        let old = vec![agent("a"), agent("b"), agent("c")];
        let fresh = vec![agent("n"), agent("c"), agent("a")];
        let ids: Vec<String> = keep_order(fresh, &old).into_iter().map(|a| a.id).collect();
        assert_eq!(ids, ["a", "c", "n"]);
    }

    #[test]
    fn places() {
        assert_eq!(place("/r/api/.claude/worktrees/brave-x/", "/h", "/"), ("brave-x".into(), Some("api".into())));
        assert_eq!(place("C:\\r\\api", "C:\\h", "\\"), ("api".into(), None));
        assert_eq!(place("/", "/h", "/"), ("".into(), None));
    }

    #[test]
    fn verbs_offered() {
        let mut a = agent("x");
        a.session_id = Some("s".into());
        assert!(!attachable(&a, true));
        assert!(attachable(&a, false));
        a.stale = true;
        assert!(gone(&a, false) && !attachable(&a, false));
        assert!(!gone(&a, true));
        a.session_id = None;
        assert!(attachable(&a, false) && !gone(&a, false));
    }
}
