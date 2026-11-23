//! L'en-tête de la session (`session_header.rs`, `session_title.rs` de ClaudeCodeUI) : le nom (double clic ou menu :
//! Renommer ; Entrée garde, Échap ou un nom vide garde l'ancien), la PR, restreinte ou sans risque, les dossiers
//! ajoutés, branche · dépôt · mode · dossier ; à droite le diff (/diff), Forker, le menu (Renommer, Copier le dossier,
//! Exporter, Remote Control, Terminer la session) et la bascule de l'inspecteur (Ctrl I). Le titre de la fenêtre :
//! « (n) nom · Claude Code » (n décisions en attente), ou « En cours · nom · Claude Code ».

use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;

use ccui_core::live::LiveSession;
use ccui_shared::model::SessionStatus;
use ccui_shared::session::SessionMeta;
use ccui_shared::{i18n, t, text};
use egui::{Align, FontId, Key, Label, Layout, Rect, RichText, Stroke, Ui, UiBuilder, Vec2};

use crate::app::{Cx, Route};
use crate::backend::Task;
use crate::session::tool_row::line_galley;
use crate::theme;
use crate::widgets::{self, icon};

type Act = Box<dyn FnOnce(Arc<LiveSession>) -> Pin<Box<dyn Future<Output = Result<Option<String>, String>> + Send>>>;

#[derive(Default)]
pub struct HeaderState {
    /// Le nom en cours de saisie (Renommer).
    renaming: Option<String>,
    focus_name: bool,
    /// Une action à la fois ; son mot (ou son échec) s'affiche dans l'en-tête.
    busy: Option<Task<Result<Option<String>, String>>>,
    fork: Option<Task<String>>,
    note: Option<String>,
    last_title: String,
}

/// `#42` pour l'adresse d'une PR, sinon le dernier segment de l'adresse.
pub fn pr_label(url: &str) -> String {
    match text::pr_number(url) {
        Some(n) => format!("#{n}"),
        None => url.trim_end_matches('/').rsplit('/').next().unwrap_or(url).to_string(),
    }
}

/// Les faits courts d'abord, pour que les points de suspension mangent le chemin : branche · dépôt · mode · dossier.
pub fn facts(m: &SessionMeta, home: &str, sep: &str) -> String {
    [m.branch.clone(), m.repo.clone(), Some(i18n::name("Mode.Name.", Some(&m.mode))), Some(text::short(&m.cwd, home, sep))]
        .into_iter()
        .flatten()
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join(" · ")
}

/// `{préfixe}{nom} · Claude Code` : « (n) » décisions en attente dans toutes les sessions, sinon « En cours · » pour
/// celle-ci ; `name` None : pas de session.
pub fn title(decisions: usize, status: SessionStatus, name: Option<&str>) -> String {
    let prefix = if decisions > 0 {
        format!("({decisions}) ")
    } else if matches!(status, SessionStatus::Running | SessionStatus::Starting) {
        format!("{} · ", t!("Session.TitleRunning"))
    } else {
        String::new()
    };
    let name = name.map_or_else(|| t!("Session.Session"), str::to_string);
    format!("{prefix}{name} · Claude Code")
}

pub fn show(ui: &mut Ui, cx: &mut Cx, st: &mut HeaderState, id: &str) {
    let t = cx.t;
    let be = cx.be;
    if let Some(r) = st.busy.as_ref().and_then(Task::take) {
        st.busy = None;
        match r {
            Ok(Some(n)) => st.note = Some(n),
            Ok(None) => {}
            Err(e) => st.note = Some(t!("Session.ActionFailed", e.trim_end())),
        }
    }
    if let Some(new) = st.fork.as_ref().and_then(Task::take) {
        st.fork = None;
        cx.open_session(&new);
    }
    let Some(h) = cx.app.session(id) else { return };
    let m = h.meta.clone();
    let has_items = !h.items.is_empty();
    let found = h.found == Some(true);
    let wt = title(cx.app.summary.decision_queue.len(), m.status, found.then_some(m.name.as_str()).filter(|n| !n.is_empty()));
    if wt != st.last_title {
        crate::host::set_title(&wt);
        st.last_title = wt;
    }
    let (home, sep) = (cx.app.boot.home.clone(), cx.app.boot.sep.clone());

    // une action de la session, sur le runtime ; son résultat va dans la note
    let run = |st: &mut HeaderState, f: Act| {
        st.note = None;
        match be.session(id) {
            Ok(s) => st.busy = Some(be.task(f(s))),
            Err(e) => st.note = Some(t!("Session.ActionFailed", e)),
        }
    };

    let full = Rect::from_min_size(ui.max_rect().min, Vec2::new(ui.available_width(), 46.0));
    ui.allocate_rect(full, egui::Sense::hover());
    ui.painter().hline(full.x_range(), full.bottom() - 0.5, Stroke::new(1.0, t.separator));
    let inner = full.shrink2(Vec2::new(16.0, 0.0));

    // à droite
    let mut right = ui.new_child(UiBuilder::new().max_rect(inner).layout(Layout::right_to_left(Align::Center)));
    right.spacing_mut().item_spacing.x = 4.0;
    {
        let ui = &mut right;
        let insp = cx.ui.inspector_open;
        let b = egui::Button::new(RichText::new(t!("Inspector")).font(theme::medium(theme::LABEL)).color(if insp { t.accent_ink } else { t.text_dim })).frame(false);
        if ui.add(b).on_hover_text(format!("{} ({})", t!("Inspector"), i18n::kbd("Ctrl I"))).clicked() {
            cx.ui.inspector_open = !insp;
        }
        let busy = st.busy.is_some();
        ui.menu_button(RichText::new(icon("dots")).color(t.text_dim), |ui| {
            if ui.button(t!("Session.Rename")).on_hover_text(t!("Session.RenameHint")).clicked() {
                st.renaming = Some(m.name.clone());
                st.focus_name = true;
                ui.close();
            }
            if !m.cwd.is_empty() && ui.button(format!("{} {}", icon("copy"), t!("Session.CopyCwd"))).clicked() {
                ui.ctx().copy_text(m.cwd.clone());
                ui.close();
            }
            if ui.add_enabled(!busy && has_items, egui::Button::new(t!("Session.Export"))).on_hover_text(t!("Session.ExportHint")).clicked() {
                let (home, sep) = (home.clone(), sep.clone());
                run(st, Box::new(move |s| Box::pin(async move { s.export().await.map(|p| Some(t!("Session.Exported", text::short(&p, &home, &sep)))) })));
                ui.close();
            }
            if let Some(ru) = &m.remote_url
                && ui.button(t!("Remote.Open")).on_hover_text(ru).clicked()
            {
                crate::host::open(ru);
                ui.close();
            }
            if m.has_process && m.remote_available {
                let on = m.remote_url.is_none();
                let label = format!("{} {}", if on { icon("ring") } else { icon("check") }, t!("Remote.Label"));
                if ui.add_enabled(!busy, egui::Button::new(label)).on_hover_text(t!("Remote.Hint")).clicked() {
                    run(st, Box::new(move |s| Box::pin(async move { s.set_remote(on).await.map(|_| None) })));
                    ui.close();
                }
            }
            if m.status != SessionStatus::Exited {
                ui.separator();
                if ui.button(RichText::new(format!("{} {}", icon("x"), t!("Session.Stop"))).color(t.danger)).on_hover_text(t!("Session.StopHint")).clicked() {
                    let core = be.core.clone();
                    let sid = id.to_string();
                    be.fire(&format!("[{id}]"), async move {
                        core.sessions.stop(&sid).await;
                        core.agent_logs.forget_session(&sid);
                        Ok(())
                    });
                    cx.navigate(Route::Overview);
                    ui.close();
                }
            }
        });
        let fork = ui.add_enabled(m.can_fork && st.fork.is_none(), egui::Button::new(RichText::new(format!("{} {}", icon("fork"), t!("Session.ForkShort"))).color(t.text_dim)).frame(false));
        if fork.on_hover_text(t!("Session.ForkHint")).clicked() {
            match be.session(id) {
                Ok(src) => {
                    let core = be.core.clone();
                    st.fork = Some(be.task(async move { core.sessions.fork(&src, None).await.id().to_string() }));
                }
                Err(e) => st.note = Some(t!("Session.ActionFailed", e)),
            }
        }
        if !m.cwd.is_empty() {
            let on = cx.ui.diff_open;
            let b = egui::Button::new(RichText::new(format!("{} {}", icon("branch"), t!("Diff.Open"))).color(if on { t.accent_ink } else { t.text_dim })).frame(false);
            if ui.add(b).on_hover_text(t!("Diff.OpenHint")).clicked() {
                // (le diff prend la place du contexte dans l'inspecteur : l'outil choisi est lâché, l'inspecteur montré)
                cx.ui.diff_open = !on;
                if !on {
                    cx.ui.selected_tool_id = None;
                    cx.ui.inspector_open = true;
                }
            }
        }
        if busy {
            ui.spinner();
        }
        if let Some(n) = &st.note {
            let w = (ui.available_width() * 0.5).clamp(40.0, 260.0);
            ui.add(Label::new(line_galley(ui, n, FontId::proportional(theme::CAPTION), t.text_dim, w))).on_hover_text(n);
        }
    }
    let used = right.min_rect().width();

    // à gauche
    let left_rect = Rect::from_min_max(inner.min, egui::pos2((inner.right() - used - 12.0).max(inner.left() + 40.0), inner.bottom()));
    let mut left = ui.new_child(UiBuilder::new().max_rect(left_rect).layout(Layout::left_to_right(Align::Center)));
    left.spacing_mut().item_spacing.x = 8.0;
    let ui = &mut left;
    if let Some(draft) = st.renaming.as_mut() {
        let r = ui.add(egui::TextEdit::singleline(draft).font(theme::semibold(theme::BODY)).desired_width(ui.available_width().min(360.0)).char_limit(200));
        if st.focus_name {
            r.request_focus();
            st.focus_name = false;
        }
        if ui.input(|i| i.key_pressed(Key::Escape)) {
            st.renaming = None;
        } else if r.lost_focus() {
            let name = draft.trim().to_string();
            st.renaming = None;
            if !name.is_empty() && name != m.name {
                run(st, Box::new(move |s| Box::pin(async move { s.rename(&name).await.map(|_| None) })));
            }
        }
        return;
    }
    let avail = ui.available_width();
    let name = if m.name.is_empty() { t!("Session.Session") } else { m.name.clone() };
    let title_g = line_galley(ui, &name, theme::semibold(theme::BODY), t.text, (avail * 0.5).max(60.0));
    let r = ui.add(Label::new(title_g).sense(egui::Sense::click())).on_hover_text(format!("{name}\n{}", t!("Session.RenameHint")));
    if r.double_clicked() {
        st.renaming = Some(m.name.clone());
        st.focus_name = true;
    }
    if let Some(pr) = &m.pr_url {
        let b = egui::Button::new(RichText::new(format!("{} {}", icon("branch"), pr_label(pr))).font(theme::mono(theme::CAPTION)).color(t.accent_ink));
        if ui.add(b).on_hover_text(t!("Session.PrOf", pr_label(pr))).clicked() {
            crate::host::open(pr);
        }
    }
    if m.restricted || m.safe_mode {
        let (hint, label) = if m.restricted { ("New.RestrictedHint", "New.Restricted") } else { ("New.SafeModeHint", "New.SafeMode") };
        widgets::tag(ui, &t, &t!(label)).on_hover_text(t!(hint));
    }
    if !m.add_dirs.is_empty() {
        ui.add(Label::new(RichText::new(i18n::plural(m.add_dirs.len() as i64, "Dirs.Count")).font(theme::mono(theme::CAPTION)).color(t.text_faint))).on_hover_text(m.add_dirs.join("\n"));
    }
    let w = ui.available_width();
    if w > 30.0 {
        let f = facts(&m, &home, &sep);
        ui.add(Label::new(line_galley(ui, &f, theme::mono(theme::CAPTION), t.text_faint, w))).on_hover_text(&m.cwd);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn labels() {
        assert_eq!(pr_label("https://github.com/o/r/pull/42"), "#42");
        assert_eq!(pr_label("https://example.com/review/abc/"), "abc");
        let m = SessionMeta { branch: Some("main".into()), mode: "plan".into(), cwd: "/Users/me/src/a".into(), ..Default::default() };
        assert_eq!(facts(&m, "/Users/me", "/"), format!("main · {} · ~/src/a", i18n::name("Mode.Name.", Some("plan"))));
    }

    #[test]
    fn prefixes() {
        assert_eq!(title(2, SessionStatus::Running, Some("fix")), "(2) fix · Claude Code");
        assert_eq!(title(0, SessionStatus::Idle, Some("fix")), "fix · Claude Code");
        assert!(title(0, SessionStatus::Starting, Some("fix")).ends_with(" · fix · Claude Code"));
        assert_eq!(title(0, SessionStatus::Exited, None), format!("{} · Claude Code", t!("Session.Session")));
    }
}
