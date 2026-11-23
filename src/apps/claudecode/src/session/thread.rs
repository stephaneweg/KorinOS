//! Le fil de la session (`session/thread.rs` de ClaudeCodeUI) : les éléments groupés en blocs
//! (`ccui_shared::thread_blocks`) : messages de l'utilisateur (Revenir ici), réponses de Claude en Markdown (Marquer,
//! Copier, Forker à partir d'ici), réflexion et compaction repliées, registres d'outils (« 3 outils · 2,4 s », le
//! dernier ouvert), sous-agents en arbre, Workflows, fichiers envoyés, hooks, notices, erreurs de l'API, reprise
//! après plantage ; les séparations des jours (avec les heures, réglage d'apparence) ; puis le texte en cours et le
//! bandeau de plantage ; la boîte du rewind.
//!
//! Le fil défile et suit le bas tant qu'on n'est pas remonté (un nouveau message y ramène ; remonté, « Nouvelle
//! activité » y ramène). Seuls les blocs proches de la vue sont dessinés : les autres gardent la hauteur mesurée (ou
//! estimée) ; le défilement est corrigé quand un bloc au-dessus de la vue change de hauteur. Tout le texte se
//! sélectionne et se copie.

use std::collections::{HashMap, HashSet};

use ccui_shared::model::{Item, SessionStatus, Time, UserItem};
use ccui_shared::push::Push;
use ccui_shared::thread_blocks::{self, Block};
use ccui_shared::{fmt, i18n, session_rules, t, tool_kinds, workflow as wf};
use chrono::NaiveDate;
use egui::{Align, CornerRadius, FontId, Frame, Label, Margin, Rect, RichText, ScrollArea, Stroke, Ui, Vec2};

use super::cards::{self, SignIn};
use super::rewind::RewindState;
use super::workflow::{self, Tree, WfPoll};
use crate::app::Cx;
use crate::backend::{Backend, Task};
use crate::md;
use crate::state::{SessionHandle, UiState};
use crate::theme::{self, Tokens};
use crate::widgets::{self, Tone, icon};

// ------------------------------------------------------------------------------------------- les blocs

/// Un bloc dessiné : sa clé (stable tant qu'il vit : un registre garde l'id de son premier outil), le bloc, la
/// séparation du jour au-dessus (partie de la clé).
#[derive(Clone, Debug, PartialEq)]
pub struct Entry {
    pub key: String,
    pub block: Block,
    pub divider: Option<NaiveDate>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Blocks {
    pub list: Vec<Entry>,
    pub at: HashMap<String, usize>,
    /// La clé du dernier registre d'outils (hors agents) : celui qui est ouvert.
    pub last_run: Option<String>,
}

fn kind(item: &Item) -> (&'static str, &str) {
    match item {
        Item::User(u) => ("u", u.uuid.as_deref().unwrap_or("")),
        Item::Text(t) => ("t", t.uuid.as_deref().unwrap_or("")),
        Item::Thought(_) => ("th", ""),
        Item::Compact(_) => ("c", ""),
        Item::Fallback(_) => ("f", ""),
        Item::ApiError(_) => ("e", ""),
        Item::Reset => ("x", ""),
        Item::Resumed => ("r", ""),
        Item::Hook(_) => ("h", ""),
        Item::Notice(_) => ("n", ""),
        Item::Scheduled(_) => ("s", ""),
        Item::Tool(t) => ("tool", &t.id),
    }
}

/// `ThreadBlocks.Of` (une réflexion cachée ne coupe pas un registre), les clés, les séparations des jours (`stamps`) :
/// quand le jour local change, mais aucune au-dessus d'un fil qui commence aujourd'hui.
pub fn blocks(items: &[&Item], hide_thoughts: bool, stamps: bool, day_of: impl Fn(Time) -> NaiveDate, today: NaiveDate) -> Blocks {
    let mut out = Blocks::default();
    let mut day: Option<NaiveDate> = None;
    for block in thread_blocks::of(items, hide_thoughts) {
        let (mut key, dated) = match &block {
            Block::Run { tools, .. } => {
                let first = tools.first().copied().unwrap_or_default();
                (format!("run:{}", items.get(first).and_then(|i| i.as_tool()).map_or("", |t| t.id.as_str())), None)
            }
            Block::Hooks(ix) => (format!("hooks:{}", ix.first().copied().unwrap_or_default()), None),
            Block::Item(i) => {
                let item = items[*i];
                let (k, id) = kind(item);
                let dated = match item {
                    Item::User(u) => Some(u.at),
                    Item::Text(t) => t.at,
                    _ => None,
                };
                (format!("{i}:{k}:{id}"), dated)
            }
        };
        let mut divider = None;
        if let Some(d) = dated.filter(|_| stamps).map(&day_of)
            && Some(d) != day
        {
            if day.is_some() || d != today {
                divider = Some(d);
                key.push_str(&format!("|{d}"));
            }
            day = Some(d);
        }
        if matches!(block, Block::Run { agents: false, .. }) {
            out.last_run = Some(key.clone());
        }
        out.at.insert(key.clone(), out.list.len());
        out.list.push(Entry { key, block, divider });
    }
    out
}

/// Le début d'une réponse (pour nommer son signet et ses boutons) : sans marques Markdown, espaces repliées, 60
/// caractères et « … ».
pub fn gist(md: &str) -> String {
    let s = md.chars().filter(|c| !matches!(c, '*' | '_' | '`' | '#' | '>')).collect::<String>();
    let s = s.split_whitespace().collect::<Vec<_>>().join(" ");
    if s.chars().count() > 60 { format!("{}…", s.chars().take(60).collect::<String>()) } else { s }
}

fn local_day(t: Time) -> NaiveDate {
    t.with_timezone(&chrono::Local).date_naive()
}

// ------------------------------------------------------------------------------------------- l'état

/// L'état du fil d'une session, d'une image à l'autre.
#[derive(Default)]
pub struct ThreadState {
    /// Les blocs, refaits quand la session change (version, nombre d'éléments, réglages, jour).
    view: Blocks,
    children: HashMap<String, Vec<usize>>,
    view_key: Option<(u64, usize, bool, bool, NaiveDate)>,
    /// Ouvert ou fermé par un clic (registres, replis, hooks), par clé de bloc.
    open: HashMap<String, bool>,
    /// Les agents ouverts sur leurs outils.
    agents_open: HashSet<String>,
    workflows: HashMap<String, WfPoll>,
    file_errors: HashMap<String, Option<String>>,
    /// Les réponses marquées (signets) de cette session, et leur lecture.
    marked: HashSet<String>,
    marks_task: Option<Task<Vec<String>>>,
    marks_seen: Option<u64>,
    mark_tasks: Vec<Task<(String, Result<bool, String>)>>,
    fork: Option<Task<String>>,
    restart: Option<Task<Result<(), String>>>,
    sign_in: SignIn,
    rewind: RewindState,
    /// La hauteur mesurée de chaque bloc (à la largeur `width`).
    heights: HashMap<String, f32>,
    width: f32,
    /// Le nombre de messages de l'utilisateur (un nouveau ramène en bas).
    prompts: usize,
    hover: Option<String>,
    offset: f32,
    /// La correction du défilement à appliquer (blocs au-dessus de la vue qui ont changé de hauteur).
    fix: f32,
    at_bottom: bool,
    force_bottom: bool,
    /// Du nouveau est arrivé pendant qu'on était remonté.
    unseen: bool,
    seen_version: (u64, usize),
    /// La liste des tâches ouverte ou fermée par un clic (None : ouverte tant qu'il reste du travail).
    pub todo_open: Option<bool>,
}

/// Ce que partagent les blocs d'une passe.
struct R<'a> {
    t: Tokens,
    be: &'a Backend,
    h: &'a SessionHandle,
    uis: &'a mut UiState,
    stamps: bool,
    /// `CanRewind` sans le message : process, inactive, aucun agent d'arrière-plan en cours.
    rewind_ok: bool,
    now: Time,
}

impl R<'_> {
    fn can_rewind(&self, uuid: Option<&str>) -> bool {
        self.rewind_ok && uuid.is_some_and(|u| self.h.meta.sent.iter().any(|s| s == u))
    }

    fn stamp(&self, ui: &mut Ui, at: Option<Time>) {
        if let Some(a) = at.filter(|_| self.stamps) {
            ui.add(Label::new(RichText::new(fmt::time(a)).font(theme::mono(theme::CAPTION)).color(self.t.text_faint))).on_hover_text(fmt::long_time(a));
        }
    }
}

// ------------------------------------------------------------------------------------------- la page

pub fn show(ui: &mut Ui, cx: &mut Cx, st: &mut ThreadState, id: &str) {
    let t = cx.t;
    let be = cx.be;
    // (les tâches finies : fork → la nouvelle session ; relance ; signets)
    if let Some(new) = st.fork.as_ref().and_then(Task::take) {
        st.fork = None;
        cx.open_session(&new);
    }
    if let Some(r) = st.restart.as_ref().and_then(Task::take) {
        st.restart = None;
        if let Err(e) = r {
            be.error(format!("[{id}] {e}"));
        }
    }
    poll_marks(st, cx, id);
    // `/rewind` tapé dans le composeur : la boîte sur ce message
    if let Some(u) = cx.ui.rewind.take() {
        st.rewind.open(be, id, u);
    }
    let Some(h) = cx.app.session(id) else { return };
    let (stamps, thoughts) = (cx.app.flags.timestamps, cx.app.flags.thinking);
    let today = chrono::Local::now().date_naive();
    let key = (h.version, h.items.len(), stamps, thoughts, today);
    if st.view_key != Some(key) {
        let refs = h.item_refs();
        st.view = blocks(&refs, !thoughts, stamps, local_day, today);
        st.children = session_rules::children(&refs);
        st.view_key = Some(key);
        let n = h.items.iter().filter(|i| matches!(i, Item::User(_))).count();
        if n > st.prompts && st.prompts > 0 {
            st.force_bottom = true;
        }
        st.prompts = n;
    }
    let rewind_ok = h.meta.has_process && h.meta.status == SessionStatus::Idle && !session_rules::background_running(&h.items);
    let mut r = R { t, be, h, uis: &mut *cx.ui, stamps, rewind_ok, now: chrono::Utc::now() };

    // du nouveau pendant qu'on est remonté : le bouton pour redescendre
    let seen = (h.version, h.stream.len());
    if seen != st.seen_version {
        if !st.at_bottom {
            st.unseen = true;
        }
        st.seen_version = seen;
    }

    let outer = ui.max_rect();
    let mut area = ScrollArea::vertical().id_salt(("thread", id)).auto_shrink([false, false]).stick_to_bottom(true);
    if st.fix.abs() > 0.5 && !st.at_bottom {
        area = area.vertical_scroll_offset((st.offset + st.fix).max(0.0));
    }
    st.fix = 0.0;
    let out = area.show(ui, |ui| {
        let avail = ui.available_width();
        let col = (avail - 64.0).clamp(200.0, 760.0);
        let pad = ((avail - col) / 2.0).max(0.0);
        if (st.width - col).abs() > 0.5 {
            st.heights.clear();
            st.width = col;
        }
        ui.add_space(24.0);
        ui.horizontal_top(|ui| {
            ui.add_space(pad);
            ui.vertical(|ui| {
                ui.set_width(col);
                ui.spacing_mut().item_spacing.y = 14.0;
                body(ui, &mut r, st, id);
            });
        });
        ui.add_space(12.0);
        if st.force_bottom {
            ui.scroll_to_cursor(Some(Align::BOTTOM));
            st.force_bottom = false;
            st.unseen = false;
        }
    });
    st.offset = out.state.clamped_offset().y;
    st.at_bottom = st.offset + out.inner_rect.height() >= out.content_size.y - 24.0;
    if st.at_bottom {
        st.unseen = false;
    }
    // « Nouvelle activité, aller en bas »
    if st.unseen && !st.at_bottom {
        let label = format!("{} {}", icon("arrow-down"), t!("Thread.JumpLatest"));
        let rect = Rect::from_center_size(egui::pos2(outer.center().x, outer.bottom() - 28.0), Vec2::new(280.0, 30.0));
        let b = egui::Button::new(RichText::new(label).font(theme::medium(theme::LABEL)).color(t.text))
            .fill(t.raised)
            .stroke(Stroke::new(1.0, t.card_border))
            .corner_radius(CornerRadius::same(15));
        if ui.put(rect, b).clicked() {
            st.force_bottom = true;
            ui.ctx().request_repaint();
        }
    }
    let cwd = cx.app.session(id).map(|h| h.meta.cwd.clone()).unwrap_or_default();
    st.rewind.show(ui, &t, be, id, &cwd);
}

/// Les signets de la session : lus à l'ouverture, relus quand ils changent ailleurs (`Push::Bookmarks`).
fn poll_marks(st: &mut ThreadState, cx: &mut Cx, id: &str) {
    let n = cx.app.bookmarks.as_ref().map(|(n, s)| (*n, s.clone()));
    let changed = match (st.marks_seen, &n) {
        (None, _) => true,
        (Some(seen), Some((n, sid))) => *n > seen && sid == id,
        _ => false,
    };
    st.marks_seen = Some(n.as_ref().map_or(0, |(n, _)| *n));
    if changed && st.marks_task.is_none() {
        let sid = id.to_string();
        st.marks_task = Some(cx.be.blocking(move || ccui_core::bookmarks::of(&sid).into_iter().map(|b| b.uuid).collect()));
    }
    if let Some(list) = st.marks_task.as_ref().and_then(Task::take) {
        st.marks_task = None;
        st.marked = list.into_iter().collect();
    }
    let mut keep = Vec::new();
    for task in std::mem::take(&mut st.mark_tasks) {
        match task.take() {
            Some((uuid, Ok(on))) => {
                if on {
                    st.marked.insert(uuid);
                } else {
                    st.marked.remove(&uuid);
                }
            }
            Some((_, Err(e))) => cx.be.error(format!("[{id}] {e}")),
            None => keep.push(task),
        }
    }
    st.mark_tasks = keep;
}

/// Une hauteur estimée pour un bloc jamais mesuré (hors de la vue).
fn estimate(h: &SessionHandle, e: &Entry, col: f32, open: bool) -> f32 {
    let lines = |s: &str| (s.len() as f32 * 7.5 / col).ceil() + s.matches('\n').count() as f32;
    let base = if e.divider.is_some() { 30.0 } else { 0.0 };
    base + match &e.block {
        Block::Run { tools, agents } => 34.0 + if open || *agents { 34.0 * tools.len() as f32 + 6.0 } else { 0.0 },
        Block::Hooks(_) => 34.0,
        Block::Item(i) => match h.items.get(*i) {
            Some(Item::User(u)) => 56.0 + 22.0 * lines(&u.text),
            Some(Item::Text(t)) => 34.0 + 23.0 * lines(&t.markdown),
            _ => 40.0,
        },
    }
}

/// Les blocs (seuls ceux près de la vue sont dessinés), le texte en cours, le bandeau de plantage.
fn body(ui: &mut Ui, r: &mut R<'_>, st: &mut ThreadState, id: &str) {
    let view = std::mem::take(&mut st.view);
    let children = std::mem::take(&mut st.children);
    let clip = ui.clip_rect();
    let near = clip.expand2(Vec2::new(0.0, 700.0));
    let col = ui.available_width();
    let mut hover = None;
    for e in &view.list {
        let open = st.open.get(&e.key).copied().unwrap_or(view.last_run.as_ref() == Some(&e.key));
        let top = ui.cursor().top();
        let guess = st.heights.get(&e.key).copied().unwrap_or_else(|| estimate(r.h, e, col, open));
        if top > near.bottom() || top + guess < near.top() {
            ui.allocate_space(Vec2::new(col, guess));
            continue;
        }
        let resp = ui
            .scope(|ui| {
                ui.spacing_mut().item_spacing.y = 8.0;
                if let Some(d) = e.divider {
                    cards::day(ui, &r.t, &fmt::long_date(d));
                }
                entry(ui, r, st, e, open, &children, id);
            })
            .response;
        let hgt = resp.rect.height();
        if top < clip.top() && (hgt - guess).abs() > 0.5 {
            st.fix += hgt - guess;
        }
        st.heights.insert(e.key.clone(), hgt);
        if ui.rect_contains_pointer(resp.rect) {
            hover = Some(e.key.clone());
        }
    }
    st.hover = hover;
    st.view = view;
    st.children = children;
    if !r.h.stream.is_empty() {
        cards::streaming(ui, &r.t, &r.h.stream);
    }
    if r.h.meta.status == SessionStatus::Crashed && cards::crash(ui, &r.t, &r.h.meta, st.restart.is_some()) {
        match r.be.session(id) {
            Ok(s) => st.restart = Some(r.be.task(async move { s.restart().await })),
            Err(e) => r.be.error(e),
        }
    }
}

fn entry(ui: &mut Ui, r: &mut R<'_>, st: &mut ThreadState, e: &Entry, open: bool, children: &HashMap<String, Vec<usize>>, id: &str) {
    let t = r.t;
    match &e.block {
        Block::Run { tools, agents: true } => {
            let mut tree = Tree { t: &t, h: r.h, children, open: &mut st.agents_open, uis: r.uis, now: r.now };
            workflow::agents(ui, &mut tree, tools);
        }
        Block::Run { tools, agents: false } => {
            if cards::ledger(ui, &t, r.uis, r.h, tools, open) {
                st.open.insert(e.key.clone(), !open);
            }
        }
        Block::Hooks(ix) => {
            let hooks: Vec<_> = ix
                .iter()
                .filter_map(|&i| match r.h.items.get(i) {
                    Some(Item::Hook(h)) => Some(&h.hook),
                    _ => None,
                })
                .collect();
            let mut o = st.open.get(&e.key).copied();
            cards::hooks(ui, &t, &hooks, &mut o);
            if let Some(o) = o {
                st.open.insert(e.key.clone(), o);
            }
        }
        Block::Item(i) => {
            let Some(item) = r.h.items.get(*i) else { return };
            match item {
                Item::Tool(tool) if tool.name == tool_kinds::SEND_FILE => {
                    let err = st.file_errors.entry(tool.id.clone()).or_default();
                    cards::file_card(ui, &t, r.uis, tool, &r.h.meta.cwd, err);
                }
                Item::Tool(tool) if tool.name == wf::TOOL => {
                    let poll = st.workflows.entry(tool.id.clone()).or_default();
                    poll.poll(r.be, ui, tool);
                    workflow::wf_card(ui, &t, r.uis, tool, poll.run.as_ref());
                }
                Item::Tool(_) | Item::Hook(_) | Item::Reset => {}
                Item::User(u) => user(ui, r, st, u, id),
                Item::Text(tx) => {
                    r.stamp(ui, tx.at);
                    md::show(ui, &t, &tx.markdown);
                    if let Some(at) = &tx.uuid {
                        let hovered = st.hover.as_ref() == Some(&e.key);
                        said_actions(ui, r, st, at, &tx.markdown, hovered, id);
                    }
                }
                Item::Thought(th) => {
                    let mut o = st.open.get(&e.key).copied().unwrap_or(false);
                    cards::fold(ui, &t, &mut o, |ui| cards::fold_small(ui, &t, &t!("Thread.Thinking")), |ui| md::show(ui, &t, &th.text));
                    st.open.insert(e.key.clone(), o);
                }
                Item::Compact(c) => {
                    let small = [
                        i18n::name("Thread.Compact.", Some(&c.trigger)),
                        if c.pre_tokens > 0 { t!("Thread.CompactTokens", fmt::tokens(c.pre_tokens), fmt::tokens(c.post_tokens)) } else { String::new() },
                        if c.duration_ms > 0 { fmt::dur(c.duration_ms as i64) } else { String::new() },
                    ]
                    .into_iter()
                    .filter(|s| !s.is_empty())
                    .collect::<Vec<_>>()
                    .join(" · ");
                    let mut o = st.open.get(&e.key).copied().unwrap_or(false);
                    cards::fold(
                        ui,
                        &t,
                        &mut o,
                        |ui| {
                            cards::fold_small(ui, &t, &t!("Thread.Compacted"));
                            ui.add(Label::new(RichText::new(&small).font(theme::mono(theme::CAPTION)).color(t.text_faint)).selectable(false));
                        },
                        |ui| match &c.summary {
                            Some(s) => md::show(ui, &t, s),
                            None => {
                                ui.label(RichText::new(t!("Thread.CompactNoSummary")).color(t.text_dim));
                            }
                        },
                    );
                    st.open.insert(e.key.clone(), o);
                }
                Item::Fallback(f) => {
                    // (le message refusé revient dans le composeur, conversation et fichiers remis avant lui :
                    // « Modifier et réessayer » de la CLI)
                    let target = f.retry.as_deref().and_then(|rt| {
                        r.h.items.iter().find_map(|i| match i {
                            Item::User(u) if u.uuid.as_deref() == Some(rt) => Some(u.clone()),
                            _ => None,
                        })
                    });
                    let target = target.filter(|u| r.can_rewind(u.uuid.as_deref()));
                    let mut retry = None;
                    cards::notice(ui, &t, false, |ui| {
                        ui.add(Label::new(cards::notice_text(&t, &f.text)).wrap());
                        if let Some(u) = target
                            && widgets::ghost(ui, &t, &format!("{} {}", icon("rewind"), t!("Thread.EditRetry"))).clicked()
                        {
                            retry = Some(u);
                        }
                    });
                    if let Some(u) = retry {
                        st.rewind.open(r.be, id, u);
                    }
                }
                Item::ApiError(err) => cards::api_error(ui, &t, r.be, r.h, &mut st.sign_in, &err.error),
                Item::Resumed => cards::notice(ui, &t, false, |ui| {
                    ui.label(cards::notice_text(&t, &t!("Thread.Resumed")));
                }),
                Item::Scheduled(sc) => cards::notice(ui, &t, false, |ui| {
                    ui.label(cards::notice_text(&t, &format!("{} {}", icon("refresh"), t!("Thread.Scheduled", fmt::time(sc.at)))));
                    ui.add(Label::new(RichText::new(format!("« {} »", sc.prompt)).font(FontId::proportional(theme::CAPTION + 0.5)).color(t.text)).wrap());
                }),
                Item::Notice(n) => {
                    let bad = matches!(n.notice.color.as_deref(), Some("error" | "warning"));
                    cards::notice(ui, &t, bad, |ui| {
                        ui.add(Label::new(cards::notice_text(&t, &n.notice.text)).wrap());
                    });
                }
            }
        }
    }
}

/// Un message de l'utilisateur (`.you`) : « toi · 14:31 » et Revenir ici, la pastille ultracode, le texte, les
/// images et documents joints.
fn user(ui: &mut Ui, r: &mut R<'_>, st: &mut ThreadState, u: &UserItem, id: &str) {
    let t = r.t;
    let mut rewind = false;
    Frame::new()
        .fill(t.user_bubble)
        .stroke(Stroke::new(1.0, t.card_border))
        .corner_radius(CornerRadius::same(theme::R_LG))
        .inner_margin(Margin::symmetric(14, 11))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.spacing_mut().item_spacing.y = 4.0;
            ui.horizontal(|ui| {
                ui.add(Label::new(RichText::new(t!("Thread.You", fmt::time(u.at))).font(theme::mono(theme::CAPTION)).color(t.text_faint)));
                if r.can_rewind(u.uuid.as_deref()) {
                    let b = egui::Button::new(RichText::new(format!("{} {}", icon("rewind"), t!("Rewind.Action"))).font(FontId::proportional(theme::CAPTION)).color(t.text_dim)).frame(false);
                    if ui.add(b).on_hover_text(t!("Rewind.ActionLabel", fmt::time(u.at))).clicked() {
                        rewind = true;
                    }
                }
            });
            if u.ultracode {
                widgets::pill(ui, &t, &format!("{} ULTRACODE", icon("agents")), Tone::Accent);
            }
            ui.add(Label::new(RichText::new(&u.text).font(FontId::proportional(theme::BODY)).color(t.text)).selectable(true).wrap());
            if let Some(imgs) = u.images.as_ref().filter(|i| !i.is_empty()) {
                ui.horizontal_wrapped(|ui| {
                    for (k, img) in imgs.iter().enumerate() {
                        let n = k + 1;
                        let text = if img.document() {
                            img.name.clone().unwrap_or_else(|| t!("Composer.Document", n))
                        } else if img.url.is_some() {
                            t!("Composer.ImageAlt", n)
                        } else {
                            t!("Thread.ImageMissing", n)
                        };
                        widgets::tag(ui, &t, &format!("{} {text}", icon("paperclip")));
                    }
                });
            }
        });
    if rewind {
        st.rewind.open(r.be, id, u.clone());
    }
}

/// Les actions d'une réponse (`.said-acts`, au survol ; toujours là si elle est marquée) : Marquer, Copier, Forker à
/// partir d'ici.
fn said_actions(ui: &mut Ui, r: &mut R<'_>, st: &mut ThreadState, at: &str, md_text: &str, hovered: bool, id: &str) {
    let t = r.t;
    let marked = st.marked.contains(at);
    let can_fork = r.h.meta.can_fork;
    let g = gist(md_text);
    ui.scope(|ui| {
        if !(hovered || marked) {
            ui.set_invisible();
        }
        ui.horizontal(|ui| {
            ui.spacing_mut().item_spacing.x = 4.0;
            let act = |text: String, on: bool| {
                egui::Button::new(RichText::new(text).font(FontId::proportional(theme::CAPTION)).color(if on { t.accent_ink } else { t.text_faint })).frame(false)
            };
            if ui.add(act(format!("{} {}", icon("bookmark"), t!("Bookmark.Add")), marked)).on_hover_text(t!("Bookmark.Of", &g)).clicked() {
                let (core, sid, uuid, gist) = (r.be.core.clone(), id.to_string(), at.to_string(), g.clone());
                st.mark_tasks.push(r.be.task(async move {
                    let res = ccui_core::bookmarks::toggle(&sid, &uuid, &gist);
                    if res.is_ok() {
                        core.sink.emit(Push::Bookmarks { session_id: sid });
                    }
                    (uuid, res)
                }));
            }
            if ui.add(act(format!("{} {}", icon("copy"), t!("Md.Copy")), false)).on_hover_text(t!("Thread.CopyOf", &g)).clicked() {
                ui.ctx().copy_text(md_text.to_string());
            }
            let fork = ui.add_enabled(can_fork && st.fork.is_none(), act(format!("{} {}", icon("fork"), t!("Session.ForkHere")), false)).on_hover_text(t!("Session.ForkHint"));
            if fork.clicked() {
                match r.be.session(id) {
                    Ok(src) => {
                        let (core, at) = (r.be.core.clone(), at.to_string());
                        st.fork = Some(r.be.task(async move { core.sessions.fork(&src, Some(at)).await.id().to_string() }));
                    }
                    Err(e) => r.be.error(e),
                }
            }
        });
    });
}

/// La liste des tâches de la session (entre le fil et le composeur) ; rien quand il n'y en a pas.
pub fn todos(ui: &mut Ui, cx: &mut Cx, st: &mut ThreadState, id: &str) {
    if let Some(h) = cx.app.session(id) {
        cards::todos(ui, &cx.t, &h.items, &mut st.todo_open);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ccui_shared::model::{ToolItem, ToolState, UserItem};
    use serde_json::json;

    fn user(day: i64) -> Item {
        Item::User(UserItem { text: "q".into(), at: Time::default() + chrono::Duration::days(day), ultracode: false, images: None, uuid: Some(format!("u{day}")) })
    }

    fn tool(id: &str) -> Item {
        Item::Tool(ToolItem {
            id: id.into(),
            name: "Bash".into(),
            input: json!({}),
            parent_tool_use_id: None,
            state: ToolState::Done,
            result_text: None,
            structured: None,
            started_at: Time::default(),
            ended_at: None,
            task_id: None,
            task_status: None,
            progress: None,
            tokens: 0,
            sub_tool_uses: 0,
            background: false,
            model: None,
        })
    }

    #[test]
    fn keys_runs_and_days() {
        let items = [user(0), tool("a"), tool("b"), user(1), tool("c")];
        let refs: Vec<&Item> = items.iter().collect();
        let day = |t: Time| t.date_naive();
        let today = Time::default().date_naive();
        let b = blocks(&refs, true, true, day, today);
        let keys: Vec<&str> = b.list.iter().map(|e| e.key.as_str()).collect();
        let d1 = today + chrono::Duration::days(1);
        assert_eq!(keys, ["0:u:u0".to_string(), "run:a".into(), format!("3:u:u1|{d1}"), "run:c".into()]);
        assert_eq!(b.list[0].divider, None, "pas de séparation au-dessus d'un fil qui commence aujourd'hui");
        assert_eq!(b.list[2].divider, Some(d1));
        assert_eq!(b.last_run.as_deref(), Some("run:c"));
        assert_eq!(b.at["run:a"], 1);
        let b = blocks(&refs, true, true, day, today - chrono::Duration::days(5));
        assert_eq!(b.list[0].divider, Some(today));
        assert!(blocks(&refs, true, false, day, today).list.iter().all(|e| e.divider.is_none()));
    }

    #[test]
    fn gists() {
        assert_eq!(gist("## Hello **world**\n\n> quoted `code`"), "Hello world quoted code");
        let long = "a".repeat(70);
        assert_eq!(gist(&long), format!("{}…", "a".repeat(60)));
    }
}
