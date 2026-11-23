//! La vue d'ensemble (`pages/overview.rs` de ClaudeCodeUI, Ctrl ⇧ O), en deux états :
//! - **au travail** (une session active) : « n sessions, n décisions t'attendent », les chiffres (en attente, en cours,
//!   ce passage, outils), le tableau de toutes les sessions (session, état, mode, dernière action, outils, durée,
//!   coût ; un clic l'ouvre), puis la file des décisions (J K naviguer, A autoriser, D refuser, Entrée ouvrir ; les
//!   éditions disent « +3 −1 ») ;
//! - **au repos** : les conversations à reprendre (une en tête, puis des rangées de trois ; 1–9 J K choisir,
//!   Entrée reprendre, R relire), les quotas, les worktrees qui gardent du travail que git ne retrouverait pas
//!   ailleurs (W y aller), l'état du jour et de la semaine.
//!
//! Les lectures (transcriptions, worktrees) passent par `be.blocking` / `be.task` hors de l'image ; la page les relit
//! à chaque arrivée (comme le composant Leptos à son montage).

use std::collections::{HashMap, HashSet};

use ccui_shared::api::{AnswerArgs, Ending, PastSession, SavedLimits, Stop};
use ccui_shared::edit_diff::{DiffKind, DiffLine};
use ccui_shared::model::{Decision, RateLimits, SessionStatus, Time};
use ccui_shared::session::{DecisionEntry, SessionSummary, SessionsSummary};
use ccui_shared::worktree_rules::{WorktreeInfo, WtState, display_name};
use ccui_shared::{ask_user, elicit, fmt, i18n, session_rules, t, text, tool_kinds};
use chrono::{Duration as Span, Local, Utc};
use egui::{Align, Frame, Key, Layout, Margin, RichText, ScrollArea, Sense, Stroke, Ui};
use rust_decimal::Decimal;

use super::parts::{self, one_line, table_head, table_row, tool_color};
use crate::app::{Cx, Route};
use crate::backend::{Backend, Task};
use crate::layout::rail::status_color;
use crate::theme::{self, Tokens};
use crate::widgets::{self, Tone, icon, kbd};

/// Une fenêtre aussi pleine est l'alarme de la page.
const HOT_AT: f64 = 0.8;
const RISKY_AHEAD: i32 = 10;
const WT_SHOWN: usize = 5;
/// Les 30 du rail : assez après les essais écartés.
const RECENT_TAKE: usize = 30;

/// Une HEAD détachée n'est pas un nom de branche à montrer.
pub fn branch(p: &PastSession) -> Option<&str> {
    p.branch.as_deref().filter(|b| !b.is_empty() && *b != "HEAD")
}

fn substantial(e: Option<&Ending>) -> bool {
    e.is_some_and(|e| e.stop != Stop::Done || e.text.as_ref().is_some_and(|t| t.encode_utf16().count() >= 20) || e.tool.is_some())
}

/// Écartées des cartes : les sessions qu'un autre process tient, un coût connu sous 5 centimes, un coût inconnu qui
/// est un essai (sans branche, ou presque rien d'écrit).
pub fn worth(p: &PastSession, end: Option<&Ending>, away: bool) -> bool {
    !away && p.cost_usd.map_or_else(|| branch(p).is_some() && substantial(end), |c| c >= Decimal::new(5, 2))
}

/// Jusqu'à 7 sessions à reprendre (une en tête, puis des rangées de trois), les plus récentes d'abord. La tête : la
/// plus récente, sauf une qui attend une réponse (AskUserQuestion) des dernières 24 h, sinon une interrompue ou
/// laissée sans réponse.
pub fn pick_cards(recent: &[PastSession], endings: &HashMap<String, Ending>, away: &dyn Fn(&PastSession) -> bool, now: Time) -> Vec<PastSession> {
    let mut list: Vec<PastSession> = recent.iter().filter(|p| worth(p, endings.get(&p.id), away(p))).take(7).cloned().collect();
    if list.len() > 4 {
        list.truncate(1 + (list.len() - 1) / 3 * 3); // jamais une carte seule sur sa rangée
    }
    let fresh = |p: &PastSession| now - p.last_write < Span::hours(24);
    let lead = list
        .iter()
        .position(|p| fresh(p) && endings.get(&p.id).is_some_and(|e| e.stop == Stop::Asked))
        .or_else(|| list.iter().position(|p| fresh(p) && endings.get(&p.id).is_some_and(|e| e.interrupted || e.unanswered)));
    if let Some(i) = lead {
        let p = list.remove(i);
        list.insert(0, p);
    }
    list
}

/// Le chemin sans séparateur final (« / » reste « / »).
pub fn trim_sep(p: &str) -> &str {
    let t = p.trim_end_matches(['/', '\\']);
    if t.is_empty() { p } else { t }
}

/// Un `claude` simple ne nomme pas sa session sur sa ligne de commande : une écriture toute fraîche le dit aussi, et
/// une transcription d'un dossier où tourne un `claude` simple, écrite dans les 30 dernières minutes. Le doute compte
/// comme « elle tourne ».
pub fn away(p: &PastSession, elsewhere: bool, live_cwds: &HashSet<String>, now: Time) -> bool {
    let age = now - p.last_write;
    elsewhere || age < Span::seconds(60) || age < Span::minutes(30) && live_cwds.contains(trim_sep(&p.cwd))
}

/// Qui doit le mot suivant, en une étiquette.
pub fn flag_key(end: Option<&Ending>) -> Option<&'static str> {
    let e = end?;
    if e.unanswered {
        return Some("Ov.NoReply");
    }
    if e.interrupted {
        return Some("Ov.Interrupted");
    }
    match e.stop {
        Stop::Asked => Some("Ov.Asked"),
        Stop::Cut => Some("Ov.Cut"),
        Stop::Done if e.text.as_ref().is_some_and(|t| t.trim_end().ends_with('?')) => Some("Ov.EndsAsk"),
        _ => None,
    }
}

/// Un dépôt sous le dossier temporaire est le brouillon d'un agent : jamais du vrai travail.
pub fn scratch(repo: &str) -> bool {
    let r = repo.to_lowercase();
    ["/tmp", "/private/tmp", "/private/var/folders"].iter().any(|t| r.starts_with(&format!("{t}/")))
}

/// `-\d+$` retiré : une série numérotée (worktree-x, -2, -3) est une seule ligne.
pub fn stem(s: &str) -> &str {
    let t = s.trim_end_matches(|c: char| c.is_ascii_digit());
    match t.strip_suffix('-') {
        Some(head) if t.len() < s.len() => head,
        _ => s,
    }
}

/// Les worktrees qui gardent du travail que git ne retrouverait pas ailleurs, les plus risqués d'abord ; une série
/// numérotée : son membre le plus risqué et un compte.
pub fn unsaved_rows(found: Vec<WorktreeInfo>) -> Vec<(WorktreeInfo, usize)> {
    let mut v: Vec<WorktreeInfo> = found
        .into_iter()
        .filter(|w| w.state == WtState::Check && !w.branch_only && (w.dirty > 0 || w.unpushed > 0 && !w.merged && !w.squash_merged))
        .filter(|w| !scratch(&w.repo))
        .collect();
    v.sort_by(|a, b| b.at_risk().cmp(&a.at_risk()).then(b.dirty.cmp(&a.dirty)).then(b.last_activity.cmp(&a.last_activity)));
    let mut out: Vec<(WorktreeInfo, usize, String)> = Vec::new();
    for w in v {
        let key = stem(w.branch.as_deref().unwrap_or(&w.name)).to_string();
        match out.iter_mut().find(|(f, _, k)| f.repo == w.repo && *k == key) {
            Some((_, n, _)) => *n += 1,
            None => out.push((w, 0, key)),
        }
    }
    out.into_iter().map(|(w, n, _)| (w, n)).collect()
}

/// La fenêtre est-elle encore celle de la lecture (pas de remise à zéro connue = encore valable).
pub fn live(reset: Option<Time>, now: Time) -> bool {
    reset.is_none_or(|r| r > now)
}

/// Au-dessus des sessions quand une fenêtre est à 80 % ou plus (elle décide alors de reprendre), dessous sinon.
pub fn quota_hot(l: &RateLimits, now: Time) -> bool {
    live(l.five_hour_reset, now) && l.five_hour >= HOT_AT || live(l.seven_day_reset, now) && l.seven_day >= HOT_AT
}

/// Une remise à zéro dite depuis aujourd'hui : « aujourd'hui 23:00 », « demain 19:59 », puis le jour.
fn soon(t: Time, now: Time) -> String {
    match (t.with_timezone(&Local).date_naive() - now.with_timezone(&Local).date_naive()).num_days() {
        0 => t!("Ov.TodayAt", fmt::time(t)),
        1 => t!("Ov.TomorrowAt", fmt::time(t)),
        _ => fmt::day(t, now),
    }
}

/// L'état et sa couleur d'une session vivante.
fn state_of(t: &Tokens, s: &SessionSummary) -> (String, egui::Color32) {
    match s.status {
        SessionStatus::Waiting => (t!("Status.WaitingOn", tool_kinds::label(s.first_pending_tool.as_deref().unwrap_or(""))), t.warning),
        SessionStatus::Running => (t!("Status.Running"), t.success),
        SessionStatus::Starting => (t!("Status.Starting"), t.text_dim),
        SessionStatus::Crashed => (t!("Session.ProcessStopped"), t.danger),
        _ => (t!("Status.Done"), t.text_dim),
    }
}

fn answerable(tool: &str) -> bool {
    tool != ask_user::TOOL && tool != elicit::TOOL
}

/// Minuit local, six jours en arrière (la semaine que la ligne d'état additionne).
fn week_start() -> Time {
    let d = Local::now().date_naive() - Span::days(6);
    d.and_hms_opt(0, 0, 0).and_then(|t| t.and_local_timezone(Local).earliest()).map_or_else(Utc::now, |t| t.with_timezone(&Utc))
}

/// Ce que l'instantané fixe d'un coup : les cartes, la tête et les comptes.
#[derive(Clone, Default)]
pub struct Snap {
    cards: Vec<PastSession>,
    endings: HashMap<String, Ending>,
    elsewhere: usize,
    elsewhere_list: Vec<PastSession>,
    left_probes: usize,
}

/// L'instantané des conversations à reprendre (hors de l'image : lit les transcriptions).
fn snapshot(summary: &SessionsSummary) -> Snap {
    use ccui_core::transcript_store as ts;
    let recent = ts::recent(RECENT_TAKE);
    let live_cwds: HashSet<String> = ts::plain_claude_cwds().into_iter().collect();
    let endings: HashMap<String, Ending> = recent.iter().filter_map(|p| ts::end_of(&p.id).map(|e| (p.id.clone(), e))).collect();
    let at = Utc::now();
    let is_away = |p: &PastSession| away(p, session_rules::running_elsewhere(summary, &p.id), &live_cwds, at);
    let c = pick_cards(&recent, &endings, &is_away, at);
    let left: Vec<&PastSession> = match c.iter().map(|x| x.last_write).min() {
        Some(min) => recent.iter().filter(|p| p.last_write >= min && !c.iter().any(|x| x.id == p.id)).collect(),
        None => Vec::new(),
    };
    Snap {
        elsewhere: recent.iter().filter(|p| is_away(p)).count(),
        elsewhere_list: recent.iter().filter(|p| is_away(p)).take(3).cloned().collect(),
        left_probes: left.iter().filter(|p| !is_away(p)).count(),
        cards: c,
        endings,
    }
}

type Unsaved = (Option<Vec<(WorktreeInfo, usize)>>, Option<String>);

/// Les worktrees qui gardent du travail (le dernier parcours s'il a moins de 2 min).
fn unsaved_scan(be: &Backend, fresh: bool) -> Task<Unsaved> {
    let core = be.core.clone();
    be.task(async move {
        let cache = core.worktrees.cached();
        let young = cache.cached_at.is_some_and(|at| Utc::now() - at < Span::minutes(2));
        let found = if !fresh && young {
            cache.rows
        } else {
            let repos = match core.worktrees.discover_repos(false).await {
                Ok(r) => r,
                Err(e) => return (None, Some(e)), // git absent : le dire plutôt que ne rien montrer
            };
            let mut found = Vec::new();
            for r in repos {
                // un dépôt illisible : la page Worktrees le nomme
                if let Ok(list) = core.worktrees.scan(&r, None).await {
                    found.extend(list);
                }
            }
            found
        };
        (Some(unsaved_rows(found)), None)
    })
}

#[derive(Default)]
pub struct Overview {
    /// L'image de la dernière passe : un trou = une nouvelle arrivée sur la page, tout est relu.
    frame: u64,
    qi: usize,
    error: Option<String>,
    limits: Option<SavedLimits>,
    week: Option<Vec<PastSession>>,
    snap: Snap,
    loaded: bool,
    snap_at: Option<Time>,
    unsaved: Option<Vec<(WorktreeInfo, usize)>>,
    wt_error: Option<String>,
    ri: usize,
    deltas: HashMap<String, (usize, usize)>,
    asked: HashSet<String>,
    t_snap: Option<Task<Snap>>,
    t_week: Option<Task<Vec<PastSession>>>,
    t_unsaved: Option<Task<Unsaved>>,
    t_deltas: Vec<(String, Task<Vec<DiffLine>>)>,
    t_answers: Vec<Task<Result<(), String>>>,
}

impl Overview {
    /// L'arrivée sur la page : les quotas enregistrés, l'instantané, la semaine, les worktrees.
    fn arrive(&mut self, cx: &Cx) {
        self.limits = ccui_core::session_manager::SessionManager::last_limits();
        self.reread(cx);
        let since = week_start();
        self.t_week = Some(cx.be.blocking(move || ccui_core::transcript_store::since(since)));
        self.t_unsaved = Some(unsaved_scan(cx.be, false));
    }

    /// Un nouvel instantané (R) : les numéros changent, la tête d'abord.
    fn reread(&mut self, cx: &Cx) {
        let summary = cx.app.summary.clone();
        self.t_snap = Some(cx.be.blocking(move || snapshot(&summary)));
    }

    fn poll(&mut self, cx: &Cx) {
        if let Some(s) = self.t_snap.as_ref().and_then(Task::take) {
            self.t_snap = None;
            self.snap = s;
            self.loaded = true;
            self.snap_at = Some(Utc::now());
            self.ri = 0;
        }
        if let Some(w) = self.t_week.as_ref().and_then(Task::take) {
            self.t_week = None;
            self.week = Some(w);
        }
        if let Some((u, e)) = self.t_unsaved.as_ref().and_then(Task::take) {
            self.t_unsaved = None;
            self.unsaved = u;
            self.wt_error = e;
        }
        let deltas = &mut self.deltas;
        self.t_deltas.retain(|(rid, task)| match task.take() {
            Some(lines) => {
                let add = lines.iter().filter(|l| l.kind == DiffKind::Add).count();
                let del = lines.iter().filter(|l| l.kind == DiffKind::Del).count();
                deltas.insert(rid.clone(), (add, del));
                false
            }
            None => true,
        });
        let error = &mut self.error;
        self.t_answers.retain(|task| match task.take() {
            Some(Err(e)) => {
                *error = Some(t!("Perm.AnswerFailed", e));
                false
            }
            Some(Ok(())) => false,
            None => true,
        });
        // « +3 −1 » pour une modification qui attend (le diff est relu sur le disque)
        for d in &cx.app.summary.decision_queue {
            let p = &d.permission;
            if !tool_kinds::is_edit(&p.tool) || self.asked.contains(&p.request_id) {
                continue;
            }
            self.asked.insert(p.request_id.clone());
            let (tool, input) = (p.tool.clone(), p.input.clone());
            let task = cx.be.blocking(move || ccui_core::edit_diff::from_input(&tool, &input));
            self.t_deltas.push((p.request_id.clone(), task));
        }
    }

    fn answer(&mut self, cx: &Cx, d: &DecisionEntry, decision: Decision) {
        // déjà répondue : un second A/D avant le résumé suivant
        if !cx.app.summary.decision_queue.iter().any(|x| x.permission.request_id == d.permission.request_id) || !self.t_answers.is_empty() {
            return;
        }
        self.error = None;
        let args = AnswerArgs { request_id: d.permission.request_id.clone(), decision, mode: None, message: None, input: None, destination: None };
        match cx.be.session(&d.session_id) {
            Ok(s) => self.t_answers.push(cx.be.task(async move { s.answer(args).await })),
            Err(e) => self.error = Some(t!("Perm.AnswerFailed", e)),
        }
    }

    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let nr = ui.ctx().cumulative_frame_nr();
        if self.frame == 0 || nr > self.frame + 1 {
            self.arrive(cx);
        }
        self.frame = nr;
        self.poll(cx);
        let t = cx.t;
        let busy = cx.app.summary.sessions.iter().any(|x| x.status != SessionStatus::Exited);

        let mut new = false;
        widgets::pane_header(
            ui,
            &t,
            |ui| {
                ui.label(RichText::new(t!("Nav.Overview")).font(theme::semibold(theme::BODY)));
            },
            |ui| {
                if busy {
                    let label = format!("{} {}   {}", icon("plus"), t!("Rail.NewSession"), i18n::kbd("Alt N"));
                    new = widgets::primary(ui, &t, &label).clicked();
                }
            },
        );
        if new {
            cx.navigate(Route::NewSession);
        }
        ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
            Frame::new().inner_margin(Margin::symmetric(28, 24)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                ui.spacing_mut().item_spacing.y = 8.0;
                if busy {
                    self.busy_view(ui, cx);
                } else if !self.loaded {
                    // pas de titre avant d'avoir lu les transcriptions : « aucune session » quelques instants serait
                    // un faux état vide
                    widgets::dim(ui, &t, &t!("Ov.Reading"));
                } else {
                    self.at_rest(ui, cx);
                }
            });
        });
        // les âges bougent une fois par minute
        ui.ctx().request_repaint_after(std::time::Duration::from_secs(30));
    }

    // ------------------------------------------------------------------------------------------- au travail

    fn busy_view(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let t = cx.t;
        let s = &cx.app.summary;
        let active: Vec<SessionSummary> = s.sessions.iter().filter(|x| x.status != SessionStatus::Exited).cloned().collect();
        let queue = s.decision_queue.clone();
        let q = queue.len();
        let (running, spent, tools) = active.iter().fold((0usize, Decimal::ZERO, 0i64), |(r, c, n), x| {
            // une session rouverte apporte le coût de toute sa vie : seul ce passage compte
            (r + usize::from(x.status == SessionStatus::Running), c + x.cost_usd - x.cost_at_open, n + i64::from(x.tool_count))
        });

        ui.horizontal(|ui| {
            let title = format!("{}{}", i18n::plural(active.len() as i64, "Ov.Sessions"), if q > 0 { format!(", {}", i18n::plural(q as i64, "Ov.Decision")) } else { String::new() });
            ui.label(RichText::new(title).font(theme::semibold(theme::TITLE)).color(t.text));
            ui.with_layout(Layout::right_to_left(Align::Max), |ui| {
                ui.spacing_mut().item_spacing.x = 28.0;
                let tot = |ui: &mut Ui, v: String, label: String, c: egui::Color32| {
                    ui.vertical(|ui| {
                        ui.spacing_mut().item_spacing.y = 0.0;
                        ui.label(RichText::new(v).font(theme::mono(20.0)).color(c));
                        ui.label(RichText::new(label).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
                    });
                };
                tot(ui, tools.to_string(), t!("Kv.Tools"), t.text);
                tot(ui, fmt::cost(spent, 2), t!("Ov.ThisRun"), t.text);
                tot(ui, running.to_string(), t!("Status.Running"), t.text);
                tot(ui, q.to_string(), t!("Ov.Pending"), if q > 0 { t.accent_ink } else { t.text });
            });
        });
        ui.add_space(10.0);

        // le tableau de toutes les sessions
        let w = parts::widths(ui.available_width(), &[250.0, 170.0, 130.0, 0.0, 70.0, 84.0, 80.0], &[0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0]);
        let right = [false, false, false, false, true, true, true];
        let titles = [t!("Session.Session"), t!("Col.State"), t!("Col.Mode"), t!("Ov.Col.Last"), t!("Col.Tools"), t!("Ov.Col.Duration"), t!("Ov.Col.Cost")];
        table_head(ui, &t, &w, &right, &titles);
        let mut open = None;
        for x in &active {
            let waiting = x.status == SessionStatus::Waiting;
            let r = table_row(ui, &t, &w, &right, 58.0, false, waiting.then_some(t.warning), |i, ui| match i {
                0 => {
                    ui.vertical(|ui| {
                        ui.spacing_mut().item_spacing.y = 1.0;
                        one_line(ui, RichText::new(&x.name).font(theme::medium(theme::LABEL)).color(t.text)).on_hover_text(&x.name);
                        let place = [x.branch.clone(), x.repo.clone()].into_iter().flatten().filter(|p| !p.is_empty()).collect::<Vec<_>>().join(" · ");
                        if !place.is_empty() {
                            one_line(ui, RichText::new(format!("{} {place}", icon("branch"))).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                        }
                        if let Some((d, n)) = x.todos.filter(|(_, n)| *n > 0) {
                            one_line(ui, RichText::new(format!("{} {}", icon("check"), t!("Todo.Progress", d, n))).font(theme::mono(theme::CAPTION)).color(t.text_faint))
                                .on_hover_text(t!("Todo.Title"));
                        }
                    });
                }
                1 => {
                    let (label, c) = state_of(&t, x);
                    let (dc, filled) = status_color(&t, x.status);
                    widgets::dot(ui, dc, filled);
                    one_line(ui, RichText::new(label).font(egui::FontId::proportional(theme::LABEL)).color(c));
                }
                2 => {
                    widgets::tag(ui, &t, &i18n::name("Mode.Name.", Some(&x.mode)));
                }
                3 => {
                    // l'outil en attente d'abord, sinon le dernier outil ; une session tombée : sa ligne de sortie
                    if x.status == SessionStatus::Crashed {
                        let txt = if x.start_failed { t!("Crash.StartFailed.Title") } else { t!("Session.Exited", x.exit_code.unwrap_or(0)) };
                        one_line(ui, RichText::new(txt).font(theme::mono(theme::CAPTION)).color(t.danger));
                    } else if let Some(tl) = &x.last_tool {
                        ui.spacing_mut().item_spacing.x = 6.0;
                        ui.label(RichText::new(tool_kinds::label(&tl.name)).font(theme::mono(theme::CAPTION)).color(tool_color(&t, &tl.name))).on_hover_text(&tl.name);
                        one_line(ui, RichText::new(tool_kinds::target(&tl.name, &tl.input, &x.cwd)).font(theme::mono(theme::CAPTION)).color(t.text_dim));
                    }
                }
                4 => {
                    ui.label(RichText::new(x.tool_count.to_string()).font(theme::mono(theme::LABEL)).color(t.text));
                }
                5 => {
                    ui.label(RichText::new(fmt::dur((x.last_event_at - x.started_at).num_milliseconds())).font(theme::mono(theme::LABEL)).color(t.text));
                }
                _ => {
                    ui.label(RichText::new(fmt::cost(x.cost_usd, 2)).font(theme::mono(theme::LABEL)).color(t.text));
                }
            });
            if r.clicked() {
                open = Some(x.id.clone());
            }
        }
        if let Some(id) = open {
            cx.open_session(&id);
        }
        if !queue.is_empty() {
            ui.add_space(18.0);
            self.queue_view(ui, cx, &queue);
        }
    }

    fn queue_view(&mut self, ui: &mut Ui, cx: &mut Cx, queue: &[DecisionEntry]) {
        let t = cx.t;
        let n = queue.len();
        let i = self.qi.min(n - 1);
        // les touches de la file, quand aucun champ n'a le clavier (Ctrl ⇧ A, Ctrl A… ne sont pas des réponses)
        let free = !cx.ui.palette_open && ui.ctx().memory(|m| m.focused().is_none());
        if free {
            let plain = egui::Modifiers::NONE;
            let (down, up, enter, a, d) = ui.ctx().input_mut(|inp| {
                (
                    inp.consume_key(plain, Key::J) || inp.consume_key(plain, Key::ArrowDown),
                    inp.consume_key(plain, Key::K) || inp.consume_key(plain, Key::ArrowUp),
                    inp.consume_key(plain, Key::Enter),
                    inp.consume_key(plain, Key::A),
                    inp.consume_key(plain, Key::D),
                )
            });
            if down {
                self.qi = (i + 1) % n;
            }
            if up {
                self.qi = (i + n - 1) % n;
            }
            if enter {
                cx.open_session(&queue[i].session_id);
            }
            if a && answerable(&queue[i].permission.tool) {
                self.answer(cx, &queue[i], Decision::Allow);
            }
            if d {
                self.answer(cx, &queue[i], Decision::Deny);
            }
        }
        let i = self.qi.min(n - 1);
        let mut act: Option<(usize, Option<Decision>)> = None;
        Frame::new().fill(t.card).stroke(Stroke::new(1.0, t.card_border)).corner_radius(theme::R_LG).show(ui, |ui| {
            ui.set_width(ui.available_width());
            Frame::new().inner_margin(Margin::symmetric(16, 12)).show(ui, |ui| {
                ui.horizontal(|ui| {
                    widgets::cap(ui, &t, &t!("Ov.Queue"));
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        ui.spacing_mut().item_spacing.x = 4.0;
                        widgets::dim(ui, &t, &t!("Palette.Open"));
                        kbd(ui, &t, &t!("Key.Enter"));
                        widgets::dim(ui, &t, &format!("{} · ", t!("Perm.HintDeny")));
                        kbd(ui, &t, "D");
                        if answerable(&queue[i].permission.tool) {
                            widgets::dim(ui, &t, &format!("{} · ", t!("Perm.HintAllow")));
                            kbd(ui, &t, "A");
                        }
                        widgets::dim(ui, &t, &format!("{} · ", t!("Palette.Navigate")));
                        kbd(ui, &t, "K");
                        kbd(ui, &t, "J");
                    });
                });
            });
            let r = ui.max_rect();
            ui.painter().hline(r.x_range(), ui.cursor().top(), Stroke::new(1.0, t.separator));
            if let Some(e) = &self.error {
                Frame::new().inner_margin(Margin::symmetric(16, 8)).show(ui, |ui| {
                    widgets::error_text(ui, &t, e);
                });
            }
            let w = parts::widths(ui.available_width(), &[176.0, 90.0, 0.0, 250.0], &[0.0, 0.0, 1.0, 0.0]);
            for (k, d) in queue.iter().enumerate() {
                let p = &d.permission;
                let sel = k == i;
                let delta = self.deltas.get(&p.request_id).copied().filter(|(a, d)| a + d > 0);
                let r = table_row(ui, &t, &w, &[false, false, false, true], 50.0, sel, sel.then_some(t.accent), |c, ui| match c {
                    0 => {
                        one_line(ui, RichText::new(&d.session_name).font(theme::medium(theme::LABEL)).color(t.text)).on_hover_text(&d.session_name);
                    }
                    1 => {
                        one_line(ui, RichText::new(tool_kinds::label(&p.tool)).font(theme::mono(theme::LABEL)).color(tool_color(&t, &p.tool))).on_hover_text(&p.tool);
                    }
                    2 => {
                        let target = tool_kinds::target(&p.tool, &p.input, &d.session_cwd);
                        let label = match delta {
                            Some((a, dl)) => {
                                let mut job = egui::text::LayoutJob::default();
                                job.append(&target, 0.0, egui::TextFormat { font_id: theme::mono(theme::LABEL), color: t.text, ..Default::default() });
                                job.append(&format!("  +{a} −{dl}"), 0.0, egui::TextFormat { font_id: theme::mono(theme::CAPTION), color: t.text_faint, ..Default::default() });
                                egui::WidgetText::from(job)
                            }
                            None => RichText::new(target).font(theme::mono(theme::LABEL)).color(t.text).into(),
                        };
                        ui.add(egui::Label::new(label).truncate());
                    }
                    _ => {
                        ui.spacing_mut().item_spacing.x = 6.0;
                        if ui.button(t!("Action.Open")).clicked() {
                            act = Some((k, None));
                        }
                        if widgets::danger(ui, &t, &t!("Perm.Deny")).clicked() {
                            act = Some((k, Some(Decision::Deny)));
                        }
                        // répondue par son formulaire dans la session, jamais par un simple « autoriser »
                        if answerable(&p.tool) && widgets::primary(ui, &t, &t!("Perm.Allow")).clicked() {
                            act = Some((k, Some(Decision::Allow)));
                        }
                    }
                });
                if r.clicked() {
                    self.qi = k;
                }
                if r.double_clicked() {
                    act = Some((k, None));
                }
            }
        });
        match act {
            Some((k, None)) => cx.open_session(&queue[k].session_id),
            Some((k, Some(dec))) => {
                self.qi = k;
                self.answer(cx, &queue[k], dec);
            }
            None => {}
        }
    }

    // ------------------------------------------------------------------------------------------- au repos

    fn state_line(&self) -> String {
        let s = &self.snap;
        let mut parts = vec![t!("Ov.NothingHere")];
        if s.elsewhere > 0 && s.elsewhere_list.is_empty() {
            parts.push(i18n::plural(s.elsewhere as i64, "Ov.Elsewhere"));
        }
        if let Some(w) = &self.week {
            let today = Local::now().date_naive();
            let sum = |it: &mut dyn Iterator<Item = &PastSession>| it.map(|p| p.cost_usd.unwrap_or_default()).sum::<Decimal>();
            parts.push(t!("Ov.Today", fmt::cost(sum(&mut w.iter().filter(|p| p.last_write.with_timezone(&Local).date_naive() == today)), 2)));
            parts.push(t!("Ov.Week", fmt::cost(sum(&mut w.iter()), 0)));
        }
        parts.join(" · ")
    }

    fn at_rest(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let t = cx.t;
        let now = Utc::now();
        let n = self.snap.cards.len();
        let has_unsaved = self.unsaved.as_ref().is_some_and(|u| !u.is_empty());
        let hot = self.limits.as_ref().is_some_and(|l| quota_hot(&l.limits, now));

        ui.label(RichText::new(t!(if n > 0 { "Ov.Resume" } else { "Ov.Empty" })).font(theme::semibold(theme::TITLE)).color(t.text));
        ui.label(RichText::new(self.state_line()).color(t.text_dim));
        if n == 0 {
            ui.label(RichText::new(t!("Ov.EmptyText")).color(t.text_dim));
            let label = format!("{} {}   {}", icon("plus"), t!("Rail.NewSession"), i18n::kbd("Alt N"));
            if widgets::primary(ui, &t, &label).clicked() {
                cx.navigate(Route::NewSession);
            }
        }
        ui.add_space(10.0);
        if hot && let Some(l) = self.limits.clone() {
            quota(ui, &t, &l, now);
            ui.add_space(10.0);
        }
        if n > 0 {
            self.resume_keys(ui, cx, has_unsaved);
            self.cards_section(ui, cx, has_unsaved, now);
            ui.add_space(10.0);
        }
        if !hot && let Some(l) = self.limits.clone() {
            quota(ui, &t, &l, now);
            ui.add_space(10.0);
        }
        if has_unsaved {
            self.worktrees_section(ui, cx, now);
        } else if let Some(e) = self.wt_error.clone() {
            ui.horizontal_wrapped(|ui| {
                widgets::dim(ui, &t, &t!("Ov.WtFailed", e));
                if widgets::ghost(ui, &t, &t!("Wt.Refresh")).clicked() {
                    self.wt_error = None;
                    self.t_unsaved = Some(unsaved_scan(cx.be, false));
                }
            });
        } else if self.unsaved.is_none() {
            widgets::dim(ui, &t, &t!("Ov.WtScanning"));
        }
    }

    /// Les touches des cartes (quand aucun champ n'a le clavier).
    fn resume_keys(&mut self, ui: &mut Ui, cx: &mut Cx, wt: bool) {
        let n = self.snap.cards.len();
        if n == 0 || cx.ui.palette_open || ui.ctx().memory(|m| m.focused().is_some()) {
            return;
        }
        let cur = self.ri.min(n - 1);
        let plain = egui::Modifiers::NONE;
        let digits = [Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5, Key::Num6, Key::Num7, Key::Num8, Key::Num9];
        let (digit, home, end, w, j, k, enter, r) = ui.ctx().input_mut(|inp| {
            let digit = digits.iter().position(|d| inp.consume_key(plain, *d));
            (
                digit,
                inp.consume_key(plain, Key::Home) || inp.consume_key(plain, Key::Escape),
                inp.consume_key(plain, Key::End),
                inp.consume_key(plain, Key::W),
                inp.consume_key(plain, Key::J) || inp.consume_key(plain, Key::ArrowRight) || inp.consume_key(plain, Key::ArrowDown),
                inp.consume_key(plain, Key::K) || inp.consume_key(plain, Key::ArrowLeft) || inp.consume_key(plain, Key::ArrowUp),
                inp.consume_key(plain, Key::Enter),
                inp.consume_key(plain, Key::R),
            )
        });
        if let Some(d) = digit.filter(|d| *d < n) {
            self.ri = d; // choisir seulement : Entrée ouvre
        }
        if home {
            self.ri = 0;
        }
        if end {
            self.ri = n - 1;
        }
        if (w || j && cur == n - 1) && wt {
            cx.navigate(Route::Worktrees);
        } else if j {
            self.ri = (cur + 1).min(n - 1);
        }
        if k {
            self.ri = cur.saturating_sub(1);
        }
        if enter && let Some(p) = self.snap.cards.get(cur) {
            let id = p.id.clone();
            cx.open_session(&id);
        }
        if r {
            self.reread(cx);
        }
    }

    fn cards_section(&mut self, ui: &mut Ui, cx: &mut Cx, has_unsaved: bool, now: Time) {
        let t = cx.t;
        let n = self.snap.cards.len();
        ui.horizontal(|ui| {
            widgets::cap(ui, &t, &t!("Ov.Recent"));
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                ui.spacing_mut().item_spacing.x = 4.0;
                if has_unsaved {
                    widgets::dim(ui, &t, &t!("Ov.WtKey"));
                    kbd(ui, &t, "W");
                    widgets::dim(ui, &t, " · ");
                }
                widgets::dim(ui, &t, &t!("Ov.Reread"));
                kbd(ui, &t, "R");
                widgets::dim(ui, &t, &format!("{} · ", t!("Ov.ResumeKey")));
                kbd(ui, &t, &t!("Key.Enter"));
                widgets::dim(ui, &t, &format!("{} · ", t!("Ov.Select")));
                kbd(ui, &t, "K");
                kbd(ui, &t, "J");
                kbd(ui, &t, &n.to_string());
                widgets::dim(ui, &t, "–");
                kbd(ui, &t, "1");
                if self.ri != 0 {
                    widgets::dim(ui, &t, &format!("{} · ", t!("Ov.First")));
                    kbd(ui, &t, "Esc");
                }
            })
            .response
            .on_hover_text(t!("Ov.MoreKeys"));
        });

        let cards = self.snap.cards.clone();
        let mut open = None;
        let gap = 10.0;
        let full = ui.available_width();
        let mut i = 0;
        while i < cards.len() {
            // la tête seule sur sa rangée ; puis des rangées de trois (deux ou trois cartes : elles se partagent la rangée)
            let per = if i == 0 { 1 } else { (cards.len() - 1).clamp(1, 3) };
            let row: Vec<usize> = (i..(i + per).min(cards.len())).collect();
            let w = (full - gap * (per as f32 - 1.0)) / per as f32;
            ui.horizontal_top(|ui| {
                ui.spacing_mut().item_spacing.x = gap;
                for &k in &row {
                    ui.allocate_ui_with_layout(egui::vec2(w, 0.0), Layout::top_down(Align::Min), |ui| {
                        ui.set_width(w);
                        if self.card(ui, &t, k, &cards[k], now) {
                            open = Some(k);
                        }
                    });
                }
            });
            i += per;
        }
        if let Some(k) = open {
            if self.ri == k {
                cx.open_session(&cards[k].id);
            } else {
                self.ri = k;
            }
        }

        let s = &self.snap;
        if !s.elsewhere_list.is_empty() {
            let more = s.elsewhere.saturating_sub(s.elsewhere_list.len());
            let list = s.elsewhere_list.clone();
            ui.horizontal_wrapped(|ui| {
                ui.spacing_mut().item_spacing.x = 4.0;
                widgets::dim(ui, &t, &t!("Ov.ElsewhereList"));
                for (k, e) in list.iter().enumerate() {
                    if k > 0 {
                        widgets::dim(ui, &t, "·");
                    }
                    let title = if e.title.chars().count() <= 40 { e.title.clone() } else { format!("{}…", e.title.chars().take(39).collect::<String>()) };
                    if ui.link(RichText::new(title).font(egui::FontId::proportional(theme::CAPTION))).on_hover_text(t!("Status.ElsewhereLine")).clicked() {
                        cx.open_session(&e.id);
                    }
                }
                if more > 0 {
                    widgets::dim(ui, &t, &format!("· +{more}"));
                }
            });
        }
        let s = &self.snap;
        if s.left_probes > 0 {
            widgets::dim(ui, &t, &t!("Ov.LeftOut", i18n::plural(s.left_probes as i64, "Ov.LeftProbe"), i18n::kbd("Ctrl K").replace(' ', "")));
        }
        // figées exprès (un chiffre nomme ce qu'il montrait), jamais en silence : leur âge est dit
        if let Some(at) = self.snap_at
            && now - at > Span::minutes(2)
        {
            ui.horizontal(|ui| {
                ui.spacing_mut().item_spacing.x = 4.0;
                widgets::dim(ui, &t, &t!("Ov.SnapAge", fmt::age(at, now)));
                kbd(ui, &t, "R");
                widgets::dim(ui, &t, &t!("Ov.SnapRefresh"));
            });
        }
    }

    /// Une carte ; true : cliquée.
    fn card(&self, ui: &mut Ui, t: &Tokens, i: usize, p: &PastSession, now: Time) -> bool {
        let sel = self.ri == i;
        let end = self.snap.endings.get(&p.id);
        let flag = flag_key(end);
        let lead = i == 0;
        let fill = if sel { t.raised } else { t.card };
        let border = if sel { t.text_faint } else { t.card_border };
        let inner = Frame::new().fill(fill).stroke(Stroke::new(1.0, border)).corner_radius(theme::R_LG).inner_margin(Margin::symmetric(14, 12)).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.spacing_mut().item_spacing.y = 6.0;
            let flag_ui = |ui: &mut Ui| {
                if let Some(k) = flag {
                    widgets::pill(ui, t, &t!(k), if k == "Ov.Asked" { Tone::Accent } else { Tone::Wait });
                }
            };
            let meta = |ui: &mut Ui| {
                let cost = match p.cost_usd.filter(|c| *c >= Decimal::new(5, 3)) {
                    Some(c) => fmt::cost(c, 2),
                    None => "—".into(),
                };
                ui.label(RichText::new(format!("{} · {cost}", fmt::age(p.last_write, now))).font(theme::mono(theme::CAPTION - 1.0)).color(t.text_faint))
                    .on_hover_text(fmt::short_date_time(p.last_write));
            };
            ui.horizontal(|ui| {
                kbd(ui, t, &(i + 1).to_string());
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if lead {
                        flag_ui(ui);
                    } else {
                        meta(ui);
                    }
                    ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
                        one_line(ui, RichText::new(&p.title).font(theme::medium(theme::SMALL)).color(t.text)).on_hover_text(&p.title);
                    });
                });
            });
            // l'extrait : ce qui a été dit en dernier, sinon le dernier outil
            match end {
                Some(Ending { text: Some(said), .. }) => {
                    let before = end.is_some_and(|e| e.interrupted && !e.unanswered);
                    let said = if lead { said.clone() } else { text::tail(said, 110) };
                    let txt = if before { format!("{} {said}", t!("Ov.BeforeStop")) } else { said };
                    let mut job = egui::text::LayoutJob::simple(txt, egui::FontId::proportional(theme::LABEL), t.text_dim, ui.available_width());
                    job.wrap.max_rows = if lead { 4 } else { 2 };
                    ui.add(egui::Label::new(job));
                }
                Some(Ending { tool: Some(tool), target, .. }) => {
                    ui.horizontal(|ui| {
                        ui.spacing_mut().item_spacing.x = 4.0;
                        ui.label(RichText::new(t!("Ov.LastStep")).font(egui::FontId::proportional(theme::LABEL)).color(t.text_faint));
                        ui.label(RichText::new(tool_kinds::label(tool)).font(theme::mono(theme::LABEL)).color(tool_color(t, tool)));
                        one_line(ui, RichText::new(tool_kinds::relative(target.as_deref().unwrap_or(""), &p.cwd)).font(theme::mono(theme::LABEL)).color(t.text_dim));
                    });
                }
                _ => {
                    if end.is_none() {
                        ui.label(RichText::new(t!("Ov.Unread")).font(egui::FontId::proportional(theme::LABEL)).color(t.text_faint).italics());
                    }
                }
            }
            if !lead && flag.is_some() {
                ui.horizontal(flag_ui);
            }
            ui.horizontal(|ui| {
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    // la place de « Reprendre » sur chaque carte, choisie ou non : rien ne bouge quand il arrive
                    let go = t!("Ov.ResumeAction");
                    if sel {
                        widgets::primary(ui, t, &go);
                    } else {
                        ui.add_visible(false, egui::Button::new(go));
                    }
                    if lead {
                        meta(ui);
                    }
                    ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
                        let repo = text::repo_name(&p.cwd);
                        let place = match branch(p) {
                            Some(b) => format!("{} {b} · {repo}", icon("branch")),
                            None => repo,
                        };
                        one_line(ui, RichText::new(place).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                    });
                });
            });
        });
        let rr = inner.response.rect;
        let r = ui.interact(rr, ui.scope_id().with(("card", i)), Sense::click());
        if r.hovered() && !sel {
            ui.painter().rect_stroke(rr, theme::R_LG, Stroke::new(1.0, t.text_faint), egui::StrokeKind::Inside);
        }
        if sel {
            ui.painter().vline(rr.left() + 1.0, rr.y_range().shrink(4.0), Stroke::new(2.0, t.text_dim));
        }
        r.clicked()
    }

    fn worktrees_section(&mut self, ui: &mut Ui, cx: &mut Cx, now: Time) {
        let t = cx.t;
        let list = self.unsaved.clone().unwrap_or_default();
        let count = list.len();
        let mut all = false;
        ui.horizontal(|ui| {
            let mut cap = t!("Ov.Wt");
            if count > WT_SHOWN {
                cap = format!("{cap} · {}", t!("OfN", WT_SHOWN, count));
            }
            widgets::cap(ui, &t, &cap).on_hover_text(t!("Ov.WtHelp"));
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                all = ui.link(format!("{} →", t!("Ov.WtAll"))).clicked();
            });
        });
        if all {
            cx.navigate(Route::Worktrees);
        }
        widgets::dim(ui, &t, &t!("Ov.WtHelp"));
        let mut go = None;
        Frame::new().stroke(Stroke::new(1.0, t.card_border)).corner_radius(theme::R_LG).show(ui, |ui| {
            let w = parts::widths(ui.available_width(), &[0.0, 130.0, 0.0, 90.0], &[1.0, 0.0, 1.4, 0.0]);
            for (wt, more) in list.into_iter().take(WT_SHOWN) {
                let risky = wt.at_risk() >= RISKY_AHEAD;
                let r = table_row(ui, &t, &w, &[false, false, false, true], 40.0, false, risky.then_some(t.danger), |c, ui| match c {
                    0 => {
                        let name = display_name(&wt).to_string();
                        if more > 0 {
                            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                                widgets::pill(ui, &t, &format!("+{more}"), Tone::Neutral);
                                ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
                                    one_line(ui, RichText::new(&name).font(theme::medium(theme::LABEL)).color(t.text)).on_hover_text(&wt.path);
                                });
                            });
                        } else {
                            one_line(ui, RichText::new(&name).font(theme::medium(theme::LABEL)).color(t.text)).on_hover_text(&wt.path);
                        }
                    }
                    1 => {
                        one_line(ui, RichText::new(wt.repo.rsplit(['/', '\\']).next().unwrap_or_default()).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                    }
                    2 => {
                        // la phrase de la page Worktrees : la ligne et la page qu'elle ouvre disent la même chose
                        let mut job = egui::text::LayoutJob::default();
                        for (s, b, _) in parts::spans(&wt.why) {
                            let font_id = if b { theme::semibold(theme::LABEL) } else { egui::FontId::proportional(theme::LABEL) };
                            job.append(&s, 0.0, egui::TextFormat { font_id, color: if b { t.text } else { t.text_dim }, ..Default::default() });
                        }
                        ui.add(egui::Label::new(job).truncate()).on_hover_text(parts::plain(&wt.why));
                    }
                    _ => {
                        ui.label(RichText::new(wt.last_activity.map(|a| fmt::age(a, now)).unwrap_or_default()).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                    }
                });
                if r.clicked() {
                    go = Some(wt.path.clone());
                }
            }
        });
        if let Some(path) = go {
            // la page Worktrees choisit cette ligne (le `?path=` de ClaudeCodeUI)
            super::worktrees::focus_path(ui.ctx(), &path);
            cx.navigate(Route::Worktrees);
        }
    }
}

/// Le bandeau des quotas (et sa ligne d'alarme quand il est chaud).
fn quota(ui: &mut Ui, t: &Tokens, sl: &SavedLimits, now: Time) {
    let l = &sl.limits;
    let hot = quota_hot(l, now);
    let stale = now - sl.at > Span::hours(24);
    Frame::new().stroke(Stroke::new(1.0, t.card_border)).corner_radius(theme::R_LG).inner_margin(Margin::symmetric(14, 10)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.horizontal_wrapped(|ui| {
            ui.spacing_mut().item_spacing.x = 10.0;
            widgets::cap(ui, t, &t!("Quota.Usage")).on_hover_text(t!("Ov.QuotaHelp"));
            ui.add_space(14.0);
            window(ui, t, &t!("Quota.FiveHours"), l.five_hour, l.five_hour_reset, stale || now - sl.at > Span::hours(1), now);
            ui.add_space(14.0);
            window(ui, t, &t!("Quota.SevenDays"), l.seven_day, l.seven_day_reset, stale, now);
            ui.add_space(14.0);
            ui.label(RichText::new(t!(if stale { "Ov.AsOfStale" } else { "Ov.AsOf" }, fmt::age(sl.at, now))).font(theme::mono(theme::CAPTION)).color(if stale { t.text_dim } else { t.text_faint }))
                .on_hover_text(fmt::short_date_time(sl.at));
        });
    });
    // l'alarme dit ce qui reste et jusqu'à quand : ce qui décide de reprendre
    if hot {
        let line = if live(l.seven_day_reset, now) && l.seven_day >= HOT_AT {
            t!("Ov.QuotaLeft", fmt::pct(1.0 - l.seven_day), t!("Ov.ThisWeek"), l.seven_day_reset.map(|r| soon(r, now)).unwrap_or_default())
        } else {
            t!("Ov.QuotaLeft", fmt::pct(1.0 - l.five_hour), t!("Ov.ThisWindow"), l.five_hour_reset.map(|r| soon(r, now)).unwrap_or_default())
        };
        widgets::dim(ui, t, &line);
    }
}

fn window(ui: &mut Ui, t: &Tokens, name: &str, used: f64, reset: Option<Time>, old: bool, now: Time) {
    let is_live = live(reset, now);
    let f = if is_live { used } else { 0.0 }; // la fenêtre est repartie depuis la lecture : elle recommence vide
    let hot = f >= HOT_AT;
    let fg = if old { t.text_faint } else { t.text_dim };
    ui.label(RichText::new(name).font(theme::mono(theme::CAPTION)).color(fg));
    parts::meter(ui, t, 120.0, f, if hot { t.accent } else if old { t.text_faint } else { t.text_dim });
    ui.label(RichText::new(fmt::pct(f)).font(theme::mono(theme::CAPTION)).color(if hot { t.accent_ink } else { t.text }));
    if let Some(r) = reset.filter(|_| is_live) {
        ui.label(RichText::new(t!("Quota.Resets", soon(r, now))).font(theme::mono(theme::CAPTION)).color(if hot { t.text } else { fg }));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::TimeZone;

    fn past(id: &str, mins_ago: i64, cost: Option<&str>, branch: Option<&str>, now: Time) -> PastSession {
        PastSession {
            id: id.into(),
            cwd: "/r/app".into(),
            branch: branch.map(Into::into),
            title: id.into(),
            cost_usd: cost.map(|c| c.parse().unwrap()),
            last_write: now - Span::minutes(mins_ago),
            worktree_path: None,
            mode: None,
            pr_url: None,
        }
    }

    #[test]
    fn cards_lead_and_rows_of_three() {
        let now = Utc.with_ymd_and_hms(2026, 11, 7, 12, 0, 0).unwrap();
        let recent: Vec<PastSession> = (0..9).map(|i| past(&format!("s{i}"), 10 + i * 10, Some("0.10"), None, now)).collect();
        let mut endings = HashMap::new();
        endings.insert("s3".to_string(), Ending { stop: Stop::Asked, ..Ending::default() });
        let none = |_: &PastSession| false;
        let c = pick_cards(&recent, &endings, &none, now);
        assert_eq!(c.len(), 7);
        assert_eq!(c[0].id, "s3");
        let c = pick_cards(&recent[..5], &HashMap::new(), &none, now);
        assert_eq!(c.len(), 4);
        let cheap = past("c", 5, Some("0.01"), None, now);
        let probe = past("p", 5, None, None, now);
        let real = past("r", 5, None, Some("feat"), now);
        let mut e = HashMap::new();
        e.insert("r".to_string(), Ending { tool: Some("Edit".into()), ..Ending::default() });
        let c = pick_cards(&[cheap, probe, real], &e, &none, now);
        assert_eq!(c.iter().map(|p| p.id.as_str()).collect::<Vec<_>>(), ["r"]);
    }

    #[test]
    fn away_rules() {
        let now = Utc.with_ymd_and_hms(2026, 11, 7, 12, 0, 0).unwrap();
        let mut cwds = HashSet::new();
        cwds.insert("/r/app".to_string());
        let mut p = past("a", 0, None, None, now);
        p.last_write = now - Span::seconds(30);
        assert!(away(&p, false, &HashSet::new(), now));
        p.last_write = now - Span::minutes(20);
        assert!(!away(&p, false, &HashSet::new(), now));
        assert!(away(&p, false, &cwds, now));
        p.last_write = now - Span::minutes(40);
        assert!(!away(&p, false, &cwds, now));
        assert!(away(&p, true, &cwds, now));
    }

    #[test]
    fn flags() {
        let e = |stop, text: &str, un, int| Ending { text: Some(text.into()), stop, unanswered: un, interrupted: int, ..Ending::default() };
        assert_eq!(flag_key(None), None);
        assert_eq!(flag_key(Some(&e(Stop::Asked, "x", true, true))), Some("Ov.NoReply"));
        assert_eq!(flag_key(Some(&e(Stop::Asked, "x", false, true))), Some("Ov.Interrupted"));
        assert_eq!(flag_key(Some(&e(Stop::Asked, "x", false, false))), Some("Ov.Asked"));
        assert_eq!(flag_key(Some(&e(Stop::Cut, "x", false, false))), Some("Ov.Cut"));
        assert_eq!(flag_key(Some(&e(Stop::Done, "Shall I? ", false, false))), Some("Ov.EndsAsk"));
        assert_eq!(flag_key(Some(&e(Stop::Done, "Done.", false, false))), None);
    }

    fn wt(path: &str, branch: &str, dirty: i32, unpushed: i32) -> WorktreeInfo {
        WorktreeInfo {
            repo: "/r".into(),
            path: path.into(),
            name: path.rsplit('/').next().unwrap().into(),
            branch: Some(branch.into()),
            head: None,
            state: WtState::Check,
            why: String::new(),
            size_bytes: None,
            last_activity: None,
            locked: false,
            lock_reason: None,
            dirty,
            unpushed,
            merged: false,
            squash_merged: false,
            upstream_gone: false,
            branch_only: false,
            unreadable: None,
            pushed: None,
            recent_min: None,
            kept: None,
        }
    }

    #[test]
    fn unsaved_series_fold() {
        let mut merged = wt("/r/m", "m", 0, 3);
        merged.merged = true;
        let mut tmp = wt("/r/t", "t", 2, 0);
        tmp.repo = "/private/tmp/x".into();
        let rows = unsaved_rows(vec![wt("/r/a", "worktree-x", 1, 0), wt("/r/b", "worktree-x-2", 0, 12), merged, tmp, wt("/r/c", "c", 3, 0)]);
        let got: Vec<(&str, usize)> = rows.iter().map(|(w, n)| (w.path.as_str(), *n)).collect();
        assert_eq!(got, [("/r/b", 1), ("/r/c", 0)]);
        assert_eq!(stem("worktree-x-12"), "worktree-x");
        assert_eq!(stem("v2"), "v2");
        assert_eq!(stem("x-"), "x-");
    }

    #[test]
    fn small_rules() {
        assert!(scratch("/private/var/folders/xy/repo") && !scratch("/tmpx/repo"));
        assert_eq!(trim_sep("/r/app/"), "/r/app");
        assert_eq!(trim_sep("/"), "/");
        let now = Utc.with_ymd_and_hms(2026, 11, 7, 12, 0, 0).unwrap();
        let l = RateLimits { five_hour: 0.9, five_hour_reset: Some(now - Span::minutes(1)), seven_day: 0.5, ..RateLimits::default() };
        assert!(!quota_hot(&l, now));
        assert!(quota_hot(&RateLimits { five_hour_reset: None, ..l }, now));
    }
}
