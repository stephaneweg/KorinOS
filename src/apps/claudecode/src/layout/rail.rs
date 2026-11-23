//! Le rail (`layout/rail.rs`, `rail_session_row.rs`, `mascot.rs` de ClaudeCodeUI), à gauche : la marque, les actions
//! (nouvelle session Alt N, rechercher Ctrl K, vue d'ensemble, worktrees « n à nettoyer », arrière-plan, extensions,
//! réglages), les sessions **actives** groupées par dépôt (point d'état, nom, branche ; en attente : le nombre de
//! décisions ; en cours ; plantée), puis le **rangement de l'app de Korin** à la place des 8 « Récentes » : une
//! recherche, les épinglées, les dossiers (glisser une conversation dessus, menu du clic droit), et toutes les
//! conversations par date (aujourd'hui, hier…). En bas : les quotas (quota.rs) et l'état de l'app (la mascotte de
//! ClaudeCodeUI, sans son canard).

use std::time::{Duration, Instant};

use ccui_shared::model::SessionStatus;
use ccui_shared::session::{SessionSummary, SessionsSummary};
use ccui_shared::{fmt, i18n, t, text};
use egui::{Align, Color32, Frame, Id, Label, Layout, Margin, RichText, ScrollArea, Sense, Stroke, TextEdit, Ui};
use rust_decimal::Decimal;

use crate::app::{Cx, Route};
use crate::backend::Task;
use crate::store::{self, SessionInfo};
use crate::theme::{self, Tokens};
use crate::widgets::{self, Tone, icon, tr};

/// Les sessions actives par dépôt, dans l'ordre où chacun a été vu (`groups`).
pub fn groups(s: &SessionsSummary) -> Vec<(String, Vec<String>)> {
    let mut out: Vec<(String, Vec<String>)> = Vec::new();
    for x in s.sessions.iter().filter(|x| x.status != SessionStatus::Exited) {
        let repo = x.repo.clone().unwrap_or_else(|| text::repo_name(&x.cwd));
        match out.iter_mut().find(|(r, _)| *r == repo) {
            Some((_, ids)) => ids.push(x.id.clone()),
            None => out.push((repo, vec![x.id.clone()])),
        }
    }
    out
}

pub fn is_active(s: &SessionsSummary, id: &str) -> bool {
    s.sessions.iter().any(|x| x.id == id && x.status != SessionStatus::Exited)
}

/// Les serveurs MCP en échec de la dernière session qui a son init (`FailedMcp`).
pub fn failed_mcp(s: &SessionsSummary) -> i32 {
    s.sessions.iter().rev().find(|x| x.has_init).map_or(0, |x| x.failed_mcp)
}

/// Le coût montré à droite d'une ligne (rien sous un demi-centime).
pub fn cost_label(c: Option<Decimal>) -> String {
    match c {
        Some(c) if c >= Decimal::new(5, 3) => fmt::cost(c, 2),
        _ => String::new(),
    }
}

/// « branche · dépôt » (sans HEAD).
pub fn place(branch: Option<&str>, repo: Option<&str>) -> Option<String> {
    let parts: Vec<&str> = [branch.filter(|b| *b != "HEAD"), repo].into_iter().flatten().filter(|s| !s.is_empty()).collect();
    (!parts.is_empty()).then(|| parts.join(" · "))
}

/// La couleur du point d'état d'une session.
pub fn status_color(t: &Tokens, s: SessionStatus) -> (Color32, bool) {
    match s {
        SessionStatus::Waiting => (t.warning, true),
        SessionStatus::Running => (t.success, true),
        SessionStatus::Crashed => (t.danger, true),
        SessionStatus::Exited => (t.text_faint, false),
        _ => (t.text_faint, false),
    }
}

/// L'humeur de l'app (la mascotte) : le titre et le texte de la ligne d'état en bas du rail.
pub fn mood(s: &SessionsSummary, bg_needs: i32, now: ccui_shared::Time) -> (Tone, String, String) {
    let pending = s.decision_queue.len();
    if pending > 0 {
        return (Tone::Wait, i18n::plural(pending as i64, "Mascot.Decision"), i18n::kbd(&t!("Mascot.NextDecision")));
    }
    if bg_needs > 0 {
        return (Tone::Wait, i18n::plural(bg_needs as i64, "Mascot.BgNeeds"), t!("Mascot.BgWhere"));
    }
    let running = s.sessions.iter().filter(|x| x.status == SessionStatus::Running).count();
    if running > 0 {
        return (Tone::Ok, t!("Mascot.Working"), i18n::plural(running as i64, "Mascot.Running"));
    }
    if let Some(c) = s.sessions.iter().rev().find(|x| x.status == SessionStatus::Crashed) {
        return (Tone::Err, t!("Mascot.Crashed"), format!("{} · {}", c.name, c.exit_label));
    }
    let since = now - chrono::Duration::minutes(5);
    let done = s
        .sessions
        .iter()
        .filter(|x| x.last_result_subtype.as_deref() == Some("success") && x.last_result_at.is_some_and(|at| at > since))
        .max_by_key(|x| x.last_result_at);
    if let Some(d) = done {
        let text = match d.last_turn_ms {
            Some(ms) => format!("{} · {}", d.name, fmt::dur(ms)),
            None => d.name.clone(),
        };
        return (Tone::Accent, t!("Mascot.TurnDone"), text);
    }
    (Tone::Neutral, t!("Mascot.Calm"), t!("Mascot.NoDecision"))
}

enum Dialog {
    None,
    Rename { id: String, text: String },
    Delete { id: String },
    NewFolder { text: String, then_move: Option<String> },
    RenameFolder { i: usize, text: String },
    DeleteFolder { i: usize },
}

pub struct Rail {
    sessions: Vec<SessionInfo>,
    scan: Option<Task<Vec<SessionInfo>>>,
    scanned_at: Option<Instant>,
    scanned_gen: u64,
    search: String,
    dialog: Dialog,
    quota: super::quota::Quota,
}

impl Default for Rail {
    fn default() -> Self {
        Rail {
            sessions: Vec::new(),
            scan: None,
            scanned_at: None,
            scanned_gen: u64::MAX,
            search: String::new(),
            dialog: Dialog::None,
            quota: Default::default(),
        }
    }
}

impl Rail {
    /// La barre de menus : « Nouveau dossier… ».
    pub fn new_folder(&mut self) {
        self.dialog = Dialog::NewFolder { text: String::new(), then_move: None };
    }

    /// La barre de menus : « Renommer » la session ouverte.
    pub fn rename(&mut self, id: &str) {
        let text = self.sessions.iter().find(|s| s.id == id).map(|s| s.title.clone()).unwrap_or_default();
        self.dialog = Dialog::Rename { id: id.to_string(), text };
    }

    /// Les conversations sur le disque : relues quand le moteur dit qu'elles ont changé, et toutes les 15 s.
    fn rescan(&mut self, cx: &mut Cx) {
        if let Some(t) = &self.scan
            && let Some(list) = t.take()
        {
            self.sessions = list;
            self.scan = None;
            let alive = self.sessions.iter().map(|s| s.id.clone()).collect();
            if cx.org.prune(&alive) {
                cx.org.save();
            }
        }
        let stale = self.scanned_gen != cx.app.transcripts_generation || self.scanned_at.is_none_or(|a| a.elapsed() > Duration::from_secs(15));
        if self.scan.is_none() && stale {
            self.scanned_gen = cx.app.transcripts_generation;
            self.scanned_at = Some(Instant::now());
            self.scan = Some(cx.be.blocking(store::list_sessions));
        }
    }

    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        self.rescan(cx);
        let t = cx.t;
        // la marque
        Frame::new().inner_margin(Margin { left: 16, right: 12, top: 12, bottom: 10 }).show(ui, |ui| {
            ui.horizontal(|ui| {
                let (r, _) = ui.allocate_exact_size(egui::vec2(10.0, 10.0), Sense::hover());
                ui.painter().rect_filled(r.shrink(1.0), 2.0, Color32::from_rgb(0xd9, 0x77, 0x57));
                ui.label(RichText::new("Claude Code").font(theme::semibold(theme::BODY)).color(t.text));
            });
        });
        ui.painter().hline(ui.max_rect().x_range(), ui.cursor().top(), Stroke::new(1.0, t.separator));
        // les actions
        Frame::new().inner_margin(Margin::symmetric(10, 8)).show(ui, |ui| {
            ui.spacing_mut().item_spacing.y = 4.0;
            let route = cx.route.clone();
            let on = |r: &Route| route == *r;
            if self.action(ui, &t, "plus", &t!("Rail.NewSession"), Some(&i18n::kbd("Alt N")), on(&Route::NewSession), true, |_| {}).clicked() {
                cx.navigate(Route::NewSession);
            }
            if self.action(ui, &t, "search", &t!("Rail.Search"), Some(&i18n::kbd("Ctrl K")), false, true, |_| {}).clicked() {
                cx.ui.palette_open = true;
            }
            if self.action(ui, &t, "grid", &t!("Nav.Overview"), None, on(&Route::Overview), false, |_| {}).clicked() {
                cx.navigate(Route::Overview);
            }
            let wt = cx.app.worktree_count.filter(|n| *n > 0);
            if self
                .action(ui, &t, "branch", &t!("Nav.Worktrees"), None, on(&Route::Worktrees), false, |ui| {
                    if let Some(n) = wt {
                        // (le nombre seul, comme Arrière-plan et Extensions : la phrase entière ne tient pas dans le rail)
                        widgets::pill(ui, &t, &n.to_string(), Tone::Neutral)
                            .on_hover_text(format!("{}\n{}", t!("Rail.ToClean", n), t!("Rail.ToCleanHelp")));
                    }
                })
                .clicked()
            {
                cx.navigate(Route::Worktrees);
            }
            let bg = cx.app.bg_needs;
            if self
                .action(ui, &t, "agents", &t!("Nav.Background"), None, on(&Route::Background), false, |ui| {
                    if bg > 0 {
                        widgets::pill(ui, &t, &bg.to_string(), Tone::Wait).on_hover_text(i18n::plural(bg as i64, "Bg.Needs"));
                    }
                })
                .clicked()
            {
                cx.navigate(Route::Background);
            }
            let failed = failed_mcp(&cx.app.summary);
            if self
                .action(ui, &t, "plug", &t!("Nav.Extensions"), None, on(&Route::Extensions), false, |ui| {
                    if failed > 0 {
                        widgets::pill(ui, &t, &failed.to_string(), Tone::Err).on_hover_text(i18n::plural(failed as i64, "Rail.FailedMcp"));
                    }
                })
                .clicked()
            {
                cx.navigate(Route::Extensions);
            }
            if self.action(ui, &t, "palette", &t!("Settings.Title"), None, on(&Route::Settings), false, |_| {}).clicked() {
                cx.navigate(Route::Settings);
            }
        });

        // en bas : quotas, état
        egui::Panel::bottom("rail-foot").frame(Frame::new().inner_margin(Margin::symmetric(12, 8))).show_inside(ui, |ui| {
            self.quota.ui(ui, cx);
            ui.add_space(4.0);
            let (tone, title, text) = mood(&cx.app.summary, cx.app.bg_needs, chrono::Utc::now());
            ui.horizontal(|ui| {
                widgets::dot(ui, widgets::tone_color(&t, tone), tone != Tone::Neutral);
                ui.vertical(|ui| {
                    ui.label(RichText::new(title).font(theme::medium(theme::LABEL)).color(t.text));
                    ui.add(Label::new(RichText::new(text).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim)).truncate());
                });
            });
        });

        ScrollArea::vertical().id_salt("rail-scroll").auto_shrink([false, false]).show(ui, |ui| {
            Frame::new().inner_margin(Margin::symmetric(10, 4)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                self.live(ui, cx);
                self.organized(ui, cx);
            });
        });
        self.dialogs(ui.ctx(), cx);
    }

    /// Une action du rail (`.btn.block`) : icône, libellé, touche ou pastille à droite.
    #[allow(clippy::too_many_arguments)]
    fn action(&self, ui: &mut Ui, t: &Tokens, ic: &str, label: &str, key: Option<&str>, on: bool, boxed: bool, right: impl FnOnce(&mut Ui)) -> egui::Response {
        let stroke = if boxed { Stroke::new(1.0, t.card_border) } else { Stroke::NONE };
        let fill = if on { t.hover } else { Color32::TRANSPARENT };
        let inner = Frame::new().fill(fill).stroke(stroke).corner_radius(theme::R).inner_margin(Margin::symmetric(10, 6)).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.horizontal(|ui| {
                ui.label(RichText::new(icon(ic)).color(t.text_dim));
                ui.label(RichText::new(label).font(theme::medium(theme::SMALL)).color(if on { t.text } else { t.text_dim }));
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if let Some(k) = key {
                        widgets::kbd(ui, t, k);
                    }
                    right(ui);
                });
            });
        });
        let r = ui.interact(inner.response.rect, Id::new(("rail-action", ic)), Sense::click());
        if r.hovered() && !on {
            ui.painter().rect_filled(inner.response.rect, theme::R, t.hover.gamma_multiply(0.5));
        }
        r.on_hover_cursor(egui::CursorIcon::PointingHand)
    }

    /// Les sessions actives, par dépôt.
    fn live(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let t = cx.t;
        let groups = groups(&cx.app.summary);
        for (repo, ids) in groups {
            ui.add_space(6.0);
            ui.horizontal(|ui| {
                widgets::cap(ui, &t, &repo);
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| widgets::mono_dim(ui, &t, &ids.len().to_string()));
            });
            for id in ids {
                let Some(s) = cx.app.summary.sessions.iter().find(|x| x.id == id).cloned() else { continue };
                let on = matches!(cx.route, Route::Session(c) if *c == id);
                if self.live_row(ui, &t, &s, on).clicked() {
                    cx.open_session(&id);
                }
            }
        }
    }

    fn live_row(&self, ui: &mut Ui, t: &Tokens, s: &SessionSummary, on: bool) -> egui::Response {
        widgets::row(ui, t, on, |ui| {
            ui.horizontal(|ui| {
                let (c, filled) = status_color(t, s.status);
                widgets::dot(ui, c, filled);
                ui.vertical(|ui| {
                    ui.set_max_width(ui.available_width() - 70.0);
                    let name = if s.untitled { t!(if s.items_len > 0 { "Rail.Titling" } else { "Rail.Untitled" }) } else { s.name.clone() };
                    ui.add(Label::new(RichText::new(name).font(theme::medium(theme::SMALL)).color(t.text)).truncate());
                    if let Some(p) = place(s.worktree.as_deref().or(s.branch.as_deref()), None) {
                        ui.add(Label::new(RichText::new(format!("{} {p}", icon("branch"))).font(theme::mono(theme::CAPTION)).color(t.text_faint)).truncate());
                    }
                });
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| match s.status {
                    SessionStatus::Waiting => {
                        widgets::pill(ui, t, &s.pending_count.to_string(), Tone::Wait).on_hover_text(i18n::plural(s.pending_count as i64, "Mascot.Decision"));
                    }
                    SessionStatus::Running => {
                        ui.label(RichText::new(t!("Status.Running")).font(theme::mono(theme::CAPTION)).color(t.success));
                    }
                    SessionStatus::Crashed => {
                        widgets::pill(ui, t, &s.exit_label, Tone::Err);
                    }
                    _ => {
                        widgets::mono_dim(ui, t, &cost_label(Some(s.cost_usd)));
                    }
                });
            });
        })
    }

    /// Le rangement de Korin : recherche, épinglées, dossiers, puis toutes les conversations par date.
    fn organized(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let t = cx.t;
        ui.add_space(10.0);
        ui.add(TextEdit::singleline(&mut self.search).hint_text(tr("Filtrer les conversations", "Filter conversations")).desired_width(f32::INFINITY));
        ui.add_space(4.0);
        let q = self.search.to_lowercase();
        let summary = &cx.app.summary;
        let visible: Vec<SessionInfo> = self
            .sessions
            .iter()
            .filter(|s| !is_active(summary, &s.id))
            .filter(|s| q.is_empty() || s.title.to_lowercase().contains(&q) || s.cwd.to_lowercase().contains(&q))
            .cloned()
            .collect();
        let mut moved: Option<(String, Option<usize>)> = None;
        let mut open: Option<String> = None;

        let pinned: Vec<&SessionInfo> = visible.iter().filter(|s| cx.org.is_pinned(&s.id)).collect();
        if !pinned.is_empty() {
            ui.add_space(6.0);
            widgets::cap(ui, &t, &tr("Épinglées", "Pinned"));
            for s in pinned {
                self.past_row(ui, cx, s, &mut moved, &mut open);
            }
        }
        let nfolders = cx.org.folders.len();
        for fi in 0..nfolders {
            let name = cx.org.folders[fi].name.clone();
            let collapsed = cx.org.folders[fi].collapsed;
            let members: Vec<SessionInfo> =
                visible.iter().filter(|s| cx.org.folders[fi].sessions.contains(&s.id) && !cx.org.is_pinned(&s.id)).cloned().collect();
            let block = ui.scope(|ui| {
                ui.add_space(6.0);
                let h = ui.horizontal(|ui| {
                    let arrow = if collapsed { icon("chev-right") } else { icon("chev-down") };
                    let r = ui.add(Label::new(RichText::new(format!("{arrow} {} {name}", icon("folder"))).font(theme::medium(theme::LABEL)).color(t.text_dim)).sense(Sense::click()));
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| widgets::mono_dim(ui, &t, &members.len().to_string()));
                    r
                });
                let r = h.inner;
                if r.clicked() {
                    cx.org.folders[fi].collapsed = !collapsed;
                    cx.org.save();
                }
                r.context_menu(|ui| {
                    if ui.button(tr("Renommer le dossier…", "Rename folder…")).clicked() {
                        self.dialog = Dialog::RenameFolder { i: fi, text: name.clone() };
                        ui.close();
                    }
                    if fi > 0 && ui.button(tr("Monter", "Move up")).clicked() {
                        cx.org.shift_folder(fi, -1);
                        cx.org.save();
                        ui.close();
                    }
                    if fi + 1 < nfolders && ui.button(tr("Descendre", "Move down")).clicked() {
                        cx.org.shift_folder(fi, 1);
                        cx.org.save();
                        ui.close();
                    }
                    ui.separator();
                    if ui.button(RichText::new(tr("Supprimer le dossier…", "Delete folder…")).color(t.danger)).clicked() {
                        self.dialog = Dialog::DeleteFolder { i: fi };
                        ui.close();
                    }
                });
                if !collapsed {
                    for s in &members {
                        ui.horizontal(|ui| {
                            ui.add_space(8.0);
                            ui.vertical(|ui| self.past_row(ui, cx, s, &mut moved, &mut open));
                        });
                    }
                    if members.is_empty() {
                        widgets::dim(ui, &t, &tr("    Glissez une conversation ici", "    Drag a conversation here"));
                    }
                }
            });
            if let Some(id) = drop_target(ui, &t, block.response.rect, ("dossier", fi)) {
                moved = Some((id, Some(fi)));
            }
        }
        let rest_block = ui.scope(|ui| {
            let rest: Vec<&SessionInfo> = visible.iter().filter(|s| !cx.org.is_pinned(&s.id) && cx.org.folder_of(&s.id).is_none()).collect();
            let mut group = "";
            for s in rest {
                let g = store::age_group(s.modified);
                if g != group {
                    ui.add_space(6.0);
                    widgets::cap(ui, &t, g);
                    group = g;
                }
                self.past_row(ui, cx, s, &mut moved, &mut open);
            }
            if visible.is_empty() {
                ui.add_space(12.0);
                widgets::empty(ui, &t, &if q.is_empty() { tr("Pas encore de conversation.", "No conversation yet.") } else { tr("Aucune conversation ne correspond.", "No conversation matches.") });
            }
            ui.add_space(10.0);
            if ui.small_button(format!("{}  {}", icon("plus"), tr("Nouveau dossier", "New folder"))).clicked() {
                self.new_folder();
            }
            ui.add_space(20.0);
        });
        if let Some(id) = drop_target(ui, &t, rest_block.response.rect, "hors-dossier") {
            moved = Some((id, None));
        }
        if let Some((id, f)) = moved {
            cx.org.move_to(&id, f);
            cx.org.save();
        }
        if let Some(id) = open {
            cx.open_session(&id);
        }
    }

    /// Une conversation passée : clic pour l'ouvrir, glisser sur un dossier, clic droit pour la ranger.
    fn past_row(&mut self, ui: &mut Ui, cx: &mut Cx, s: &SessionInfo, moved: &mut Option<(String, Option<usize>)>, open: &mut Option<String>) {
        let t = cx.t;
        let on = matches!(cx.route, Route::Session(c) if *c == s.id);
        let elsewhere = ccui_shared::session_rules::running_elsewhere(&cx.app.summary, &s.id);
        let r = widgets::row(ui, &t, on, |ui| {
            ui.horizontal(|ui| {
                widgets::dot(ui, t.text_faint, false);
                ui.vertical(|ui| {
                    ui.add(Label::new(RichText::new(&s.title).font(theme::medium(theme::SMALL)).color(t.text)).truncate());
                    let sub = format!("{} · {}", store::short_dir(&s.cwd), store::ago(s.modified));
                    ui.add(Label::new(RichText::new(sub).font(theme::mono(theme::CAPTION)).color(t.text_faint)).truncate());
                    if elsewhere {
                        ui.label(RichText::new(t!("Status.Elsewhere")).font(theme::mono(theme::CAPTION)).color(t.success));
                    }
                });
            });
        });
        let id = s.id.clone();
        r.dnd_set_drag_payload(id.clone());
        if r.dragged() {
            egui::Area::new(Id::new("glisse")).order(egui::Order::Tooltip).fixed_pos(ui.ctx().pointer_latest_pos().unwrap_or_default() + egui::vec2(12.0, 8.0)).show(ui.ctx(), |ui| {
                Frame::new().fill(t.card).stroke(Stroke::new(1.0, t.accent_ink)).corner_radius(theme::R).inner_margin(Margin::symmetric(8, 4)).show(ui, |ui| {
                    ui.label(RichText::new(store::one_line(&s.title, 40)).font(egui::FontId::proportional(theme::SMALL)));
                });
            });
        }
        if r.clicked() {
            *open = Some(id.clone());
        }
        r.context_menu(|ui| {
            if ui.button(tr("Renommer…", "Rename…")).clicked() {
                self.dialog = Dialog::Rename { id: id.clone(), text: s.title.clone() };
                ui.close();
            }
            let pinned = cx.org.is_pinned(&id);
            if ui.button(if pinned { tr("Désépingler", "Unpin") } else { tr("Épingler", "Pin") }).clicked() {
                cx.org.toggle_pin(&id);
                cx.org.save();
                ui.close();
            }
            ui.menu_button(tr("Ranger dans", "Move to"), |ui| {
                let cur = cx.org.folder_of(&id);
                for (i, f) in cx.org.folders.iter().enumerate() {
                    if ui.add_enabled(cur != Some(i), egui::Button::new(format!("{} {}", icon("folder"), f.name))).clicked() {
                        *moved = Some((id.clone(), Some(i)));
                        ui.close();
                    }
                }
                if cur.is_some() && ui.button(tr("Hors des dossiers", "Out of folders")).clicked() {
                    *moved = Some((id.clone(), None));
                    ui.close();
                }
                ui.separator();
                if ui.button(tr("Nouveau dossier…", "New folder…")).clicked() {
                    self.dialog = Dialog::NewFolder { text: String::new(), then_move: Some(id.clone()) };
                    ui.close();
                }
            });
            if ui.button(tr("Copier l'identifiant", "Copy id")).clicked() {
                ui.ctx().copy_text(id.clone());
                ui.close();
            }
            ui.separator();
            if ui.button(RichText::new(tr("Supprimer…", "Delete…")).color(t.danger)).clicked() {
                self.dialog = Dialog::Delete { id: id.clone() };
                ui.close();
            }
        });
    }

    fn dialogs(&mut self, ctx: &egui::Context, cx: &mut Cx) {
        let t = cx.t;
        let mut close = false;
        match &mut self.dialog {
            Dialog::None => {}
            Dialog::Rename { id, text } => {
                if let Some(ok) = modal(ctx, &t, "rename", &t!("Session.Rename"), |ui| {
                    let r = ui.add(TextEdit::singleline(text).desired_width(360.0));
                    r.request_focus();
                    buttons(ui, &t, &t!("Session.Rename"))
                }) {
                    if ok && !text.trim().is_empty() {
                        let title = text.trim().to_string();
                        if let Some(live) = cx.be.core.sessions.get(id) {
                            cx.be.fire("", async move { live.rename(&title).await });
                        } else if let Some(info) = self.sessions.iter().find(|s| s.id == *id) {
                            if let Err(e) = store::rename(info, &title) {
                                cx.be.error(e.to_string());
                            }
                            self.scanned_at = None;
                        }
                    }
                    close = true;
                }
            }
            Dialog::Delete { id } => {
                let title = self.sessions.iter().find(|s| s.id == *id).map(|s| s.title.clone()).unwrap_or_default();
                if let Some(ok) = modal(ctx, &t, "delete", &tr("Supprimer la conversation ?", "Delete the conversation?"), |ui| {
                    ui.label(format!("« {title} »"));
                    widgets::dim(ui, &t, &tr("Son fichier est retiré de ~/.claude/projects (sans retour).", "Its file is removed from ~/.claude/projects (no undo)."));
                    buttons(ui, &t, &tr("Supprimer", "Delete"))
                }) {
                    if ok && let Some(info) = self.sessions.iter().find(|s| s.id == *id) {
                        if let Err(e) = store::delete(info) {
                            cx.be.error(e.to_string());
                        }
                        cx.org.forget(id);
                        cx.org.save();
                        self.scanned_at = None;
                        if matches!(cx.route, Route::Session(c) if c == id) {
                            cx.navigate(Route::NewSession);
                        }
                    }
                    close = true;
                }
            }
            Dialog::NewFolder { text, then_move } => {
                if let Some(ok) = modal(ctx, &t, "newfolder", &tr("Nouveau dossier", "New folder"), |ui| {
                    ui.add(TextEdit::singleline(text).hint_text(tr("Nom du dossier", "Folder name")).desired_width(300.0)).request_focus();
                    buttons(ui, &t, &tr("Créer", "Create"))
                }) {
                    if ok && !text.trim().is_empty() {
                        let i = cx.org.add_folder(text.trim());
                        if let Some(id) = then_move.take() {
                            cx.org.move_to(&id, Some(i));
                        }
                        cx.org.save();
                    }
                    close = true;
                }
            }
            Dialog::RenameFolder { i, text } => {
                if let Some(ok) = modal(ctx, &t, "renamefolder", &tr("Renommer le dossier", "Rename folder"), |ui| {
                    ui.add(TextEdit::singleline(text).desired_width(300.0)).request_focus();
                    buttons(ui, &t, &t!("Session.Rename"))
                }) {
                    if ok && !text.trim().is_empty() && *i < cx.org.folders.len() {
                        cx.org.folders[*i].name = text.trim().to_string();
                        cx.org.save();
                    }
                    close = true;
                }
            }
            Dialog::DeleteFolder { i } => {
                let name = cx.org.folders.get(*i).map(|f| f.name.clone()).unwrap_or_default();
                if let Some(ok) = modal(ctx, &t, "deletefolder", &tr("Supprimer le dossier ?", "Delete the folder?"), |ui| {
                    ui.label(format!("« {name} »"));
                    widgets::dim(ui, &t, &tr("Ses conversations restent, hors des dossiers.", "Its conversations stay, out of folders."));
                    buttons(ui, &t, &tr("Supprimer", "Delete"))
                }) {
                    if ok && *i < cx.org.folders.len() {
                        cx.org.remove_folder(*i);
                        cx.org.save();
                    }
                    close = true;
                }
            }
        }
        if close {
            self.dialog = Dialog::None;
        }
    }
}

/// Une zone où déposer une conversation glissée : son identifiant quand on l'y lâche.
fn drop_target(ui: &mut Ui, t: &Tokens, rect: egui::Rect, salt: impl std::hash::Hash + std::fmt::Debug) -> Option<String> {
    let r = ui.interact(rect, Id::new(("drop", salt)), Sense::hover());
    if r.dnd_hover_payload::<String>().is_some() {
        ui.painter().rect_stroke(rect, theme::R, Stroke::new(1.5, t.accent_ink), egui::StrokeKind::Inside);
    }
    r.dnd_release_payload::<String>().map(|p| (*p).clone())
}

/// Une boîte modale : Some(true) validée, Some(false) annulée, None encore ouverte.
pub fn modal(ctx: &egui::Context, t: &Tokens, id: &str, title: &str, body: impl FnOnce(&mut Ui) -> Option<bool>) -> Option<bool> {
    let mut out = None;
    let r = egui::Modal::new(Id::new(("modal", id))).frame(Frame::new().fill(t.card).stroke(Stroke::new(1.0, t.card_border)).corner_radius(theme::R_LG).inner_margin(Margin::same(16))).show(ctx, |ui| {
        ui.set_min_width(360.0);
        ui.label(RichText::new(title).font(theme::semibold(theme::SECTION)));
        ui.add_space(8.0);
        out = body(ui);
    });
    if r.should_close() && out.is_none() {
        out = Some(false);
    }
    out
}

/// Annuler et le bouton qui valide (aussi Entrée).
pub fn buttons(ui: &mut Ui, t: &Tokens, ok: &str) -> Option<bool> {
    let mut out = None;
    ui.add_space(10.0);
    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
        if widgets::primary(ui, t, ok).clicked() || ui.input(|i| i.key_pressed(egui::Key::Enter)) {
            out = Some(true);
        }
        if ui.button(t!("Action.Cancel")).clicked() {
            out = Some(false);
        }
    });
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn places() {
        assert_eq!(place(Some("main"), Some("repo")).as_deref(), Some("main · repo"));
        assert_eq!(place(Some("HEAD"), Some("repo")).as_deref(), Some("repo"));
        assert_eq!(place(None, None), None);
    }

    #[test]
    fn costs() {
        assert_eq!(cost_label(Some(Decimal::new(4, 3))), "");
        assert_eq!(cost_label(None), "");
        assert!(!cost_label(Some(Decimal::new(42, 2))).is_empty());
    }
}
