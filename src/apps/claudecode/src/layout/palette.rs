//! La palette de commandes, Ctrl K (`layout/command_palette.rs` de ClaudeCodeUI) : une boîte par-dessus tout, un
//! champ qui filtre ensemble les **sessions** (les vivantes d'abord — celles qui t'attendent, puis en cours —, puis
//! les récentes), les **actions** (décision suivante, bifurquer, nouvelle session dans le dossier courant, vue
//! d'ensemble, arrêter, thème, worktrees, extensions, réglages) et, à partir de 3 lettres, les **messages** des
//! transcriptions (`transcript_store::search`, 250 ms après la dernière touche, annulée par la suivante ou à la
//! fermeture : `core.searches`). ↑ ↓ choisir, Entrée ouvrir, Échap fermer (`cx.ui.palette_open = false`).
//!
//! Différences avec ClaudeCodeUI : le thème parcourt ceux de Korin (système, sombre, clair) plutôt que les cinq de
//! ClaudeCodeUI ; pas d'action « Rechercher une mise à jour » (les paquets de Korin s'en chargent).

use std::sync::atomic::Ordering;
use std::time::Duration;

use ccui_shared::api::{PastSession, SearchResult, TranscriptHit};
use ccui_shared::model::{SessionStatus, Time};
use ccui_shared::session::{SessionSummary, SessionsSummary};
use ccui_shared::{fmt, i18n, session_rules, t, text};
use egui::text::{LayoutJob, TextFormat};
use egui::{Align, Align2, Frame, Key, Layout, Margin, Modifiers, RichText, ScrollArea, Sense, Stroke, Ui};

use crate::app::{Cx, Route};
use crate::backend::Task;
use crate::layout::rail::{cost_label, is_active, status_color};
use crate::theme::{self, Tokens};
use crate::widgets::{self, Tone, icon, kbd, tr};

// La recherche dans les messages : dès MIN_CHARS, après une pause, annulée par la touche suivante ou la fermeture.
const MIN_CHARS: usize = 3;
const MAX_HITS: usize = 20;
const DEBOUNCE: f64 = 0.25;
const RECENT_TAKE: usize = 30;

/// Ce que fait une ligne.
#[derive(Clone, Debug, PartialEq)]
pub enum Run {
    /// Fermer et aller à cette page.
    Go(Route),
    /// Fermer et ouvrir Nouvelle session dans ce dossier (`?cwd=`).
    NewIn(String),
    /// Bifurquer cette session, puis ouvrir la bifurcation.
    Fork(String),
    /// Fermer, arrêter cette session, puis la vue d'ensemble.
    Stop(String),
    /// Le thème suivant (la palette reste ouverte).
    Theme,
}

/// Ce qui précède le nom : le point d'état d'une session (None : passée), ou l'icône d'une action.
#[derive(Clone, Debug, PartialEq)]
pub enum Lead {
    Status(Option<SessionStatus>),
    Icon(&'static str),
}

/// Une ligne de la palette ; `msg` : un message trouvé (son nom est l'extrait).
#[derive(Clone, Debug, PartialEq)]
pub struct Row {
    pub session: bool,
    pub name: String,
    pub lead: Lead,
    pub detail: Option<String>,
    pub pill: usize,
    pub right: Option<String>,
    pub kbd: Option<String>,
    pub run: Run,
    pub msg: bool,
}

impl Row {
    /// L'ordre d'affichage = l'ordre des lignes : ↑ ↓ suivent l'écran. Les messages arrivent tard et vont à la fin :
    /// ajouter ne déplace jamais la ligne choisie.
    pub fn kind(&self) -> u8 {
        if self.msg {
            2
        } else if self.session {
            0
        } else {
            1
        }
    }

    fn action(name: String, icon: &'static str, detail: Option<String>, pill: usize, right: Option<String>, kbd: Option<String>, run: Run) -> Row {
        Row { session: false, name, lead: Lead::Icon(icon), detail, pill, right, kbd, run, msg: false }
    }
}

/// Tout ce que `rows` lit.
pub struct Input<'a> {
    pub summary: &'a SessionsSummary,
    pub recent: &'a [PastSession],
    pub hits: &'a [TranscriptHit],
    pub q: &'a str,
    /// Le thème de Korin choisi : "" (système), "dark", "light".
    pub theme: &'a str,
    /// La session à l'écran.
    pub current: Option<&'a str>,
    pub home: &'a str,
    pub sep: &'a str,
    pub now: Time,
    pub elsewhere: &'a dyn Fn(&str) -> bool,
}

fn join(parts: &[Option<&str>]) -> String {
    parts.iter().flatten().filter(|p| !p.is_empty()).copied().collect::<Vec<_>>().join(" · ")
}

fn some(s: String) -> Option<String> {
    (!s.is_empty()).then_some(s)
}

/// Une requête vide (sans espaces) trouve tout ; sinon « contient », sans la casse.
pub fn hit(q: &str, s: Option<&str>) -> bool {
    let t = q.trim();
    t.is_empty() || s.is_some_and(|s| text::find_ci(s, t).is_some())
}

/// Le nom d'un thème de Korin.
pub fn theme_name(theme: &str) -> String {
    match theme {
        "dark" => tr("Sombre", "Dark"),
        "light" => tr("Clair", "Light"),
        _ => tr("Système", "System"),
    }
}

/// Le thème après `theme` : système → sombre → clair → système.
pub fn next_theme(theme: &str) -> &'static str {
    match theme {
        "dark" => "light",
        "light" => "system",
        _ => "dark",
    }
}

fn session_row(s: &SessionSummary) -> Row {
    let state = match s.status {
        SessionStatus::Waiting => s.first_pending_tool.as_ref().map(|p| t!("Status.WaitingOn", p)),
        SessionStatus::Idle | SessionStatus::Exited => some(cost_label(Some(s.cost_usd))),
        _ => None,
    };
    Row {
        session: true,
        name: s.name.clone(),
        lead: Lead::Status(Some(s.status)),
        detail: some(join(&[s.branch.as_deref(), s.repo.as_deref(), state.as_deref()])),
        pill: if s.status == SessionStatus::Waiting { s.pending_count } else { 0 },
        right: match s.status {
            SessionStatus::Running => Some(t!("Status.Running")),
            SessionStatus::Crashed => Some(s.exit_label.clone()),
            SessionStatus::Waiting => None,
            _ => Some(t!("Status.Done")),
        },
        kbd: None,
        run: Run::Go(Route::Session(s.id.clone())),
        msg: false,
    }
}

fn past_row(p: &PastSession, now: Time, elsewhere: &dyn Fn(&str) -> bool) -> Row {
    let repo = text::repo_name(&p.cwd);
    let day = fmt::day(p.last_write, now);
    let cost = cost_label(p.cost_usd);
    Row {
        session: true,
        name: p.title.clone(),
        lead: Lead::Status(None),
        detail: some(join(&[p.branch.as_deref(), Some(&repo), Some(&day), Some(&cost)])),
        pill: 0,
        right: Some(if elsewhere(&p.id) { t!("Status.Elsewhere") } else { t!("Status.History") }),
        kbd: None,
        // la page de la session ouvre sa transcription
        run: Run::Go(Route::Session(p.id.clone())),
        msg: false,
    }
}

/// La session à l'écran, sinon la plus récente ; un dossier de worktree donne son dépôt.
fn current_cwd(i: &Input) -> Option<String> {
    let here = i.current.and_then(|id| i.summary.sessions.iter().find(|s| s.id == id)).map(|s| s.cwd.clone());
    let cwd = here.or_else(|| i.summary.sessions.last().map(|s| s.cwd.clone())).or_else(|| i.recent.first().map(|p| p.cwd.clone()))?;
    Some(text::root_of(&cwd))
}

/// Les lignes : les sessions (celles qui t'attendent d'abord, les plus récentes d'abord), les actions, puis les
/// messages trouvés.
pub fn rows(i: &Input) -> Vec<Row> {
    let s = i.summary;
    let mut active: Vec<&SessionSummary> = s.sessions.iter().filter(|x| x.status != SessionStatus::Exited).rev().collect();
    active.sort_by_key(|x| match x.status {
        SessionStatus::Waiting => 0,
        SessionStatus::Running => 1,
        _ => 2,
    });
    let live = active.into_iter().map(|x| (x.name.clone(), x.branch.clone(), x.repo.clone(), session_row(x)));
    let past = i
        .recent
        .iter()
        .filter(|p| !is_active(s, &p.id))
        .map(|p| (p.title.clone(), p.branch.clone(), Some(text::repo_name(&p.cwd)), past_row(p, i.now, i.elsewhere)));
    let take = if i.q.is_empty() { 5 } else { 8 };
    let mut out: Vec<Row> = live
        .chain(past)
        .filter(|(n, b, r, _)| hit(i.q, Some(n)) || hit(i.q, b.as_deref()) || hit(i.q, r.as_deref()))
        .take(take)
        .map(|(.., row)| row)
        .collect();

    let next = s.decision_queue.first();
    let here = i.current.and_then(|id| s.sessions.iter().find(|x| x.id == id));
    let cwd = current_cwd(i);
    let mut actions = Vec::new();
    if let Some(n) = next {
        actions.push(Row::action(
            t!("Palette.NextDecision"),
            "warn",
            Some(n.session_name.clone()),
            s.decision_queue.len(),
            None,
            Some(i18n::kbd("Ctrl ⇧ A")),
            Run::Go(Route::Session(n.session_id.clone())),
        ));
    }
    if let Some(h) = here.filter(|h| h.can_fork) {
        actions.push(Row::action(t!("Session.Fork"), "fork", Some(h.name.clone()), 0, None, None, Run::Fork(h.id.clone())));
    }
    actions.push(match &cwd {
        None => Row::action(t!("Rail.NewSession"), "plus", None, 0, None, Some(i18n::kbd("Alt N")), Run::Go(Route::NewSession)),
        Some(c) => Row::action(t!("Palette.NewSessionIn", text::chip(c, i.home, i.sep)), "plus", None, 0, None, Some(i18n::kbd("Alt N")), Run::NewIn(c.clone())),
    });
    actions.push(Row::action(t!("Nav.Overview"), "grid", None, 0, None, Some(i18n::kbd("Ctrl ⇧ O")), Run::Go(Route::Overview)));
    if let Some(h) = here.filter(|h| h.status != SessionStatus::Exited) {
        actions.push(Row::action(t!("Session.Stop"), "x", Some(h.name.clone()), 0, None, None, Run::Stop(h.id.clone())));
    }
    actions.push(Row::action(t!("Palette.Theme", theme_name(i.theme)), "palette", None, 0, Some(t!("Palette.ChangeTheme")), None, Run::Theme));
    actions.push(Row::action(t!("Nav.Worktrees"), "branch", None, 0, None, None, Run::Go(Route::Worktrees)));
    actions.push(Row::action(t!("Nav.Background"), "agents", None, 0, None, None, Run::Go(Route::Background)));
    actions.push(Row::action(t!("Nav.Extensions"), "plug", None, 0, None, None, Run::Go(Route::Extensions)));
    actions.push(Row::action(t!("Settings.Title"), "settings", None, 0, None, None, Run::Go(Route::Settings)));
    out.extend(actions.into_iter().filter(|a| hit(i.q, Some(&a.name))));

    out.extend(i.hits.iter().map(|h| {
        let live = s.sessions.iter().find(|x| x.id == h.session.id);
        Row {
            session: true,
            name: h.snippet.clone(),
            lead: Lead::Status(live.map(|x| x.status)),
            detail: some(join(&[Some(&h.session.title), Some(&text::repo_name(&h.session.cwd))])),
            pill: 0,
            right: Some(fmt::day(h.at, i.now)),
            kbd: None,
            run: Run::Go(Route::Session(h.session.id.clone())),
            msg: true,
        }
    }));
    out
}

/// (avant, trouvé, après) de la première occurrence de la requête, sans la casse (espaces resserrés en second essai).
pub fn mark(s: &str, q: &str) -> (String, Option<String>, String) {
    let mut t = q.trim().to_string();
    // un extrait de message a ses espaces resserrés : la requête aussi, quand elle n'est pas trouvée telle quelle
    if !t.is_empty() && text::find_ci(s, &t).is_none() {
        t = t.split_whitespace().collect::<Vec<_>>().join(" ");
    }
    let found = if t.is_empty() { None } else { text::find_ci(s, &t) };
    let Some(i) = found else { return (s.to_string(), None, String::new()) };
    let chars: Vec<char> = s.chars().collect();
    let n = t.chars().count();
    (chars[..i].iter().collect(), Some(chars[i..i + n].iter().collect()), chars[i + n..].iter().collect())
}

pub struct Palette {
    /// L'image de la dernière passe : un trou = la palette vient de s'ouvrir.
    frame: u64,
    q: String,
    sel: usize,
    recent: Vec<PastSession>,
    hits: Vec<TranscriptHit>,
    searching: bool,
    partial: bool,
    failed: bool,
    /// Le numéro de la dernière recherche (une touche plus récente, ou la fermeture, gagne).
    seq: u64,
    /// La recherche à lancer et quand (après la pause).
    due: Option<(String, f64)>,
    search: Option<(u64, Task<Result<SearchResult, String>>)>,
    t_recent: Option<Task<Vec<PastSession>>>,
    t_fork: Option<Task<Result<String, String>>>,
    /// Faire défiler jusqu'à la ligne choisie (après une flèche).
    reveal: bool,
}

impl Default for Palette {
    fn default() -> Self {
        Palette {
            frame: 0,
            q: String::new(),
            sel: 0,
            recent: Vec::new(),
            hits: Vec::new(),
            searching: false,
            partial: false,
            failed: false,
            seq: 1_000,
            due: None,
            search: None,
            t_recent: None,
            t_fork: None,
            reveal: false,
        }
    }
}

impl Palette {
    /// Annule la recherche en cours (le moteur la voit changée et s'arrête).
    fn cancel(&mut self, cx: &Cx) {
        self.seq += 1;
        self.due = None;
        if let Some((id, _)) = self.search.take() {
            let _ = cx.be.core.searches.current.compare_exchange(id, 0, Ordering::SeqCst, Ordering::SeqCst);
        }
    }

    fn find_messages(&mut self, cx: &Cx, q: String, now: f64) {
        self.cancel(cx);
        self.hits.clear();
        self.partial = false;
        self.failed = false;
        let go = q.chars().count() >= MIN_CHARS;
        self.searching = go;
        if go {
            self.due = Some((q, now + DEBOUNCE));
        }
    }

    fn start_search(&mut self, cx: &Cx, q: String) {
        let id = self.seq;
        let core = cx.be.core.clone();
        core.searches.current.store(id, Ordering::SeqCst);
        let task = cx.be.task(async move {
            tokio::task::spawn_blocking(move || {
                let cancelled = || core.searches.current.load(Ordering::SeqCst) != id;
                ccui_core::transcript_store::search(&q, MAX_HITS, &cancelled, ccui_core::transcript_store::SEARCH_BUDGET)
            })
            .await
            .map_err(|e| e.to_string())
        });
        self.search = Some((id, task));
    }

    /// L'ouverture : la requête vidée, les récentes relues.
    fn open(&mut self, cx: &Cx) {
        // (fermée par Échap dans app.rs : la recherche d'avant est annulée ici)
        self.cancel(cx);
        self.q.clear();
        self.sel = 0;
        self.hits.clear();
        self.searching = false;
        self.partial = false;
        self.failed = false;
        self.t_recent = Some(cx.be.blocking(|| ccui_core::transcript_store::recent(RECENT_TAKE)));
    }

    fn close(&mut self, cx: &mut Cx) {
        self.cancel(cx);
        self.searching = false;
        cx.ui.palette_open = false;
    }

    fn run(&mut self, cx: &mut Cx, r: Run) {
        match r {
            Run::Go(route) => {
                self.close(cx);
                cx.navigate(route);
            }
            Run::NewIn(cwd) => {
                self.close(cx);
                cx.ui.new_session_cwd = Some(cwd);
                cx.navigate(Route::NewSession);
            }
            Run::Fork(id) => {
                if self.t_fork.is_none() {
                    match cx.be.session(&id) {
                        Ok(src) => {
                            let core = cx.be.core.clone();
                            self.t_fork = Some(cx.be.task(async move { Ok(core.sessions.fork(&src, None).await.id().to_string()) }));
                        }
                        Err(e) => cx.be.error(e),
                    }
                }
            }
            Run::Stop(id) => {
                self.close(cx);
                let core = cx.be.core.clone();
                cx.be.fire("", async move {
                    core.sessions.stop(&id).await;
                    Ok(())
                });
                cx.navigate(Route::Overview);
            }
            Run::Theme => {
                let cur = if cx.org.theme.is_empty() { "system" } else { cx.org.theme.as_str() };
                cx.org.theme = next_theme(cur).to_string();
                cx.org.save();
            }
        }
    }

    fn poll(&mut self, cx: &mut Cx, now: f64) {
        if let Some(r) = self.t_recent.as_ref().and_then(Task::take) {
            self.t_recent = None;
            self.recent = r;
        }
        if let Some((q, at)) = self.due.clone()
            && now >= at
        {
            self.due = None;
            self.start_search(cx, q);
        }
        let got = match &self.search {
            Some((id, task)) if *id == self.seq => task.take(),
            _ => None,
        };
        if let Some(got) = got {
            self.search = None;
            match got {
                Ok(r) => {
                    self.hits = r.hits;
                    self.partial = !r.complete;
                }
                // jamais « Recherche dans les messages… » laissé à l'écran
                Err(_) => self.failed = true,
            }
            self.searching = false;
        }
        if let Some(r) = self.t_fork.as_ref().and_then(Task::take) {
            self.t_fork = None;
            match r {
                Ok(fork) => self.run(cx, Run::Go(Route::Session(fork))),
                Err(e) => cx.be.error(e),
            }
        }
    }

    pub fn ui(&mut self, ctx: &egui::Context, cx: &mut Cx) {
        let now = ctx.input(|i| i.time);
        let nr = ctx.cumulative_frame_nr();
        let opened = self.frame == 0 || nr > self.frame + 1;
        self.frame = nr;
        if opened {
            self.open(cx);
        }
        self.poll(cx, now);
        if !cx.ui.palette_open {
            return;
        }
        if self.due.is_some() || self.search.is_some() || self.t_recent.is_some() {
            ctx.request_repaint_after(Duration::from_millis(60));
        }
        let t = cx.t;
        let current = match cx.route {
            Route::Session(id) => Some(id.clone()),
            _ => None,
        };
        let theme = if cx.org.theme.is_empty() { "system".to_string() } else { cx.org.theme.clone() };
        let all = {
            let summary = &cx.app.summary;
            let elsewhere = |id: &str| session_rules::running_elsewhere(summary, id);
            rows(&Input {
                summary,
                recent: &self.recent,
                hits: &self.hits,
                q: &self.q,
                theme: &theme,
                current: current.as_deref(),
                home: &cx.app.boot.home,
                sep: &cx.app.boot.sep,
                now: chrono::Utc::now(),
                elsewhere: &elsewhere,
            })
        };
        let n = all.len();
        // les flèches et Entrée, avant le champ (qui les prendrait)
        let (down, up, enter) = ctx.input_mut(|i| (i.consume_key(Modifiers::NONE, Key::ArrowDown), i.consume_key(Modifiers::NONE, Key::ArrowUp), i.consume_key(Modifiers::NONE, Key::Enter)));
        if n > 0 {
            let cur = self.sel.min(n - 1);
            if down {
                self.sel = (cur + 1) % n;
                self.reveal = true;
            }
            if up {
                self.sel = (cur + n - 1) % n;
                self.reveal = true;
            }
            if enter {
                let r = all[cur].run.clone();
                self.run(cx, r);
                return;
            }
        }
        let shown = self.sel.min(n.saturating_sub(1));

        let id = egui::Id::NULL.with("palette");
        let mut clicked: Option<Run> = None;
        let mut typed: Option<String> = None;
        let resp = egui::Modal::new(id)
            .area(egui::Modal::default_area(id).anchor(Align2::CENTER_TOP, [0.0, 110.0]))
            .backdrop_color(egui::Color32::from_black_alpha(140))
            .frame(Frame::new().fill(t.card).stroke(Stroke::new(1.0, t.card_border)).corner_radius(theme::R_LG).inner_margin(Margin::ZERO))
            .show(ctx, |ui| {
                ui.set_width(640.0);
                ui.spacing_mut().item_spacing.y = 0.0;
                // le champ
                Frame::new().inner_margin(Margin::symmetric(16, 12)).show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.label(RichText::new(icon("search")).color(t.text_dim));
                        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                            kbd(ui, &t, &t!("Key.Esc"));
                            let edit = egui::TextEdit::singleline(&mut self.q)
                                .hint_text(t!("Palette.Placeholder"))
                                .frame(egui::Frame::NONE)
                                .font(egui::FontId::proportional(theme::BODY))
                                .desired_width(ui.available_width());
                            let r = ui.add(edit);
                            if opened || !r.has_focus() {
                                r.request_focus();
                            }
                            if r.changed() {
                                typed = Some(self.q.clone());
                            }
                        });
                    });
                });
                sep(ui, &t);
                // les lignes, par groupe
                ScrollArea::vertical().max_height(440.0).auto_shrink([false, true]).show(ui, |ui| {
                    Frame::new().inner_margin(Margin::symmetric(8, 6)).show(ui, |ui| {
                        ui.set_width(ui.available_width());
                        for (k, title) in [(0u8, "Palette.Sessions"), (1, "Palette.Actions"), (2, "Palette.Messages")] {
                            if !all.iter().any(|r| r.kind() == k) {
                                continue;
                            }
                            ui.add_space(6.0);
                            ui.horizontal(|ui| {
                                ui.add_space(8.0);
                                widgets::cap(ui, &t, &t!(title));
                            });
                            ui.add_space(4.0);
                            for (i, r) in all.iter().enumerate().filter(|(_, r)| r.kind() == k) {
                                let on = i == shown;
                                let resp = row(ui, &t, r, on, &self.q);
                                if on && self.reveal {
                                    resp.scroll_to_me(None);
                                }
                                if resp.hovered() && ui.input(|i| i.pointer.delta() != egui::Vec2::ZERO) {
                                    self.sel = i;
                                }
                                if resp.clicked() {
                                    clicked = Some(r.run.clone());
                                }
                            }
                        }
                        // l'état de la recherche (un parcours arrêté n'est jamais un « aucun résultat » définitif)
                        let status = if self.searching {
                            Some(t!("Palette.SearchingMessages"))
                        } else if self.failed {
                            Some(t!("Palette.SearchFailed"))
                        } else if self.partial {
                            Some(t!("Palette.OlderNotSearched"))
                        } else {
                            None
                        };
                        if n == 0 && !self.searching {
                            pad(ui, &t, &t!("Palette.NoResults"));
                        }
                        if let Some(s) = status {
                            pad(ui, &t, &s);
                        }
                    });
                });
                self.reveal = false;
                sep(ui, &t);
                Frame::new().inner_margin(Margin::symmetric(16, 9)).show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.spacing_mut().item_spacing.x = 4.0;
                        kbd(ui, &t, "↑");
                        kbd(ui, &t, "↓");
                        widgets::dim(ui, &t, &t!("Palette.Navigate"));
                        ui.add_space(10.0);
                        kbd(ui, &t, "⏎");
                        widgets::dim(ui, &t, &t!("Palette.Open"));
                    });
                });
            });
        if let Some(q) = typed {
            self.sel = 0;
            self.find_messages(cx, q.trim().to_string(), now);
        }
        if let Some(r) = clicked {
            self.run(cx, r);
        } else if resp.should_close() {
            self.close(cx);
        }
    }
}

fn sep(ui: &mut Ui, t: &Tokens) {
    let r = ui.max_rect();
    let y = ui.cursor().top();
    ui.painter().hline(r.x_range(), y, Stroke::new(1.0, t.separator));
}

fn pad(ui: &mut Ui, t: &Tokens, s: &str) {
    Frame::new().inner_margin(Margin::symmetric(8, 6)).show(ui, |ui| {
        widgets::dim(ui, t, s);
    });
}

/// Une ligne : le point d'état ou l'icône, le nom (la requête trouvée en couleur), le détail, puis à droite la
/// pastille, l'état ou la touche.
fn row(ui: &mut Ui, t: &Tokens, r: &Row, on: bool, q: &str) -> egui::Response {
    let fill = if on { t.hover } else { egui::Color32::TRANSPARENT };
    let inner = Frame::new().fill(fill).corner_radius(theme::R).inner_margin(Margin::symmetric(8, 6)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.horizontal(|ui| {
            match &r.lead {
                Lead::Status(s) => {
                    let (c, filled) = match s {
                        Some(s) => status_color(t, *s),
                        None => (t.text_faint, false),
                    };
                    widgets::dot(ui, c, filled);
                }
                Lead::Icon(name) => {
                    ui.add_sized([14.0, 14.0], egui::Label::new(RichText::new(icon(name)).color(t.text_dim)));
                }
            }
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                ui.spacing_mut().item_spacing.x = 6.0;
                if on && r.session {
                    kbd(ui, t, "⏎");
                } else if let Some(k) = &r.kbd {
                    kbd(ui, t, k);
                }
                if r.kbd.is_none()
                    && let Some(right) = r.right.as_ref().filter(|x| !x.is_empty())
                {
                    ui.label(RichText::new(right).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                }
                if r.pill > 0 {
                    widgets::pill(ui, t, &r.pill.to_string(), Tone::Wait);
                }
                ui.with_layout(Layout::top_down(Align::Min), |ui| {
                    ui.spacing_mut().item_spacing.y = 1.0;
                    let (before, m, after) = mark(&r.name, q);
                    let mut job = LayoutJob::default();
                    let base = TextFormat { font_id: theme::medium(theme::SMALL), color: t.text, ..Default::default() };
                    job.append(&before, 0.0, base.clone());
                    if let Some(m) = m {
                        job.append(&m, 0.0, TextFormat { color: t.accent_ink, ..base.clone() });
                    }
                    job.append(&after, 0.0, base);
                    ui.add(egui::Label::new(job).truncate());
                    if let Some(d) = r.detail.as_ref().filter(|d| !d.is_empty()) {
                        ui.add(egui::Label::new(RichText::new(d).font(theme::mono(theme::CAPTION)).color(t.text_faint)).truncate());
                    }
                });
            });
        });
    });
    ui.interact(inner.response.rect, inner.response.id.with("pal-row"), Sense::click())
}

#[cfg(test)]
mod tests {
    use super::*;
    use rust_decimal::Decimal;

    fn summary(id: &str, cwd: &str, branch: Option<&str>, status: SessionStatus) -> SessionSummary {
        let now = chrono::Utc::now();
        SessionSummary {
            id: id.into(),
            version: 1,
            name: id.into(),
            cwd: cwd.into(),
            repo: Some(text::repo_name(cwd)),
            branch: branch.map(Into::into),
            worktree: None,
            mode: "default".into(),
            status,
            untitled: false,
            items_len: 0,
            pending_count: 0,
            first_pending_tool: None,
            exit_label: String::new(),
            exit_code: None,
            start_failed: false,
            cost_usd: Decimal::ZERO,
            cost_at_open: Decimal::ZERO,
            tool_count: 0,
            started_at: now,
            last_event_at: now,
            last_result_subtype: None,
            last_result_at: None,
            last_turn_ms: None,
            has_process: status != SessionStatus::Exited,
            can_fork: false,
            has_init: false,
            failed_mcp: 0,
            last_tool: None,
            todos: None,
        }
    }

    #[test]
    fn marks() {
        assert_eq!(mark("Fix the Parser", "parser"), ("Fix the ".into(), Some("Parser".into()), "".into()));
        assert_eq!(mark("abc", ""), ("abc".into(), None, "".into()));
        assert_eq!(mark("abc", "zz"), ("abc".into(), None, "".into()));
        assert_eq!(mark("run the tests now", " the   tests "), ("run ".into(), Some("the tests".into()), " now".into()));
        assert_eq!(mark("Été chaud", "été"), ("".into(), Some("Été".into()), " chaud".into()));
    }

    #[test]
    fn theme_cycle() {
        assert_eq!(next_theme("system"), "dark");
        assert_eq!(next_theme("dark"), "light");
        assert_eq!(next_theme("light"), "system");
        assert_eq!(next_theme(""), "dark");
    }

    fn input<'a>(s: &'a SessionsSummary, recent: &'a [PastSession], q: &'a str, current: Option<&'a str>) -> Input<'a> {
        Input { summary: s, recent, hits: &[], q, theme: "system", current, home: "/Users/me", sep: "/", now: "2026-11-07T12:00:00Z".parse().unwrap(), elsewhere: &|_| false }
    }

    #[test]
    fn rows_order_and_actions() {
        let mut waiting = summary("w", "/r/alpha", Some("alpha"), SessionStatus::Waiting);
        waiting.pending_count = 2;
        waiting.first_pending_tool = Some("Bash".into());
        let mut idle = summary("i", "/r/beta", None, SessionStatus::Idle);
        idle.cost_usd = Decimal::new(150, 2);
        idle.can_fork = true;
        let s = SessionsSummary { sessions: vec![waiting, idle, summary("x", "/r/gone", None, SessionStatus::Exited)], ..Default::default() };
        let past = vec![PastSession {
            id: "p".into(),
            cwd: "/r/gamma".into(),
            branch: Some("main".into()),
            title: "Old talk".into(),
            cost_usd: None,
            last_write: "2026-11-07T10:00:00Z".parse().unwrap(),
            worktree_path: None,
            mode: None,
            pr_url: None,
        }];
        let r = rows(&input(&s, &past, "", Some("i")));
        let names: Vec<&str> = r.iter().filter(|x| x.kind() == 0).map(|x| x.name.as_str()).collect();
        assert_eq!(names, ["w", "i", "Old talk"]);
        assert_eq!(r[0].pill, 2);
        assert_eq!(r[0].detail.as_deref(), Some(format!("alpha · alpha · {}", t!("Status.WaitingOn", "Bash")).as_str()));
        assert_eq!(r[0].right, None);
        assert_eq!(r[1].right.as_deref(), Some(t!("Status.Done").as_str()));
        assert_eq!(r[2].right.as_deref(), Some(t!("Status.History").as_str()));
        let runs: Vec<&Run> = r.iter().filter(|x| x.kind() == 1).map(|x| &x.run).collect();
        assert!(runs.contains(&&Run::Fork("i".into())));
        assert!(runs.contains(&&Run::Stop("i".into())));
        assert!(runs.contains(&&Run::NewIn("/r/beta".into())));
        assert!(runs.contains(&&Run::Theme));
        let r = rows(&input(&s, &past, "gamma", None));
        assert_eq!(r.iter().map(|x| x.name.as_str()).collect::<Vec<_>>(), ["Old talk"]);
        let r = rows(&input(&s, &past, "overv", None));
        assert_eq!(r.len(), 1);
        assert_eq!(r[0].run, Run::Go(Route::Overview));
    }
}
