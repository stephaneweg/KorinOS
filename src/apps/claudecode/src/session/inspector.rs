//! L'inspecteur de la session (`session/inspector.rs` de ClaudeCodeUI), le panneau de droite : la demande
//! d'autorisation en attente d'abord (montrée même quand Ctrl I a masqué l'inspecteur : elle bloque le tour ;
//! `permission.rs`), puis les tâches d'arrière-plan s'il y en a (`tasks.rs`), puis l'agent, le workflow ou l'outil
//! choisi dans le fil (`agents.rs`, `tool_detail.rs`), sinon les changements non validés (`/diff`) ou le contexte
//! (`context.rs`).
//!
//! Ici aussi les petites pièces que ces panneaux partagent (`.ph`, `.section`, `.kv`, `DiffView`, `pre.cmdlog`,
//! `.keys`, `.tabs`, la couleur d'un outil).

use ccui_shared::edit_diff::{DiffKind, DiffLine};
use ccui_shared::{t, tool_kinds, workflow};
use egui::{Color32, CornerRadius, Frame, Margin, RichText, ScrollArea, Stroke, Ui};

use crate::app::Cx;
use crate::session::{agents, context, permission, tasks, tool_detail};
use crate::theme::{self, Tokens};
use crate::widgets;

/// Le panneau qu'ouvre un outil choisi.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Panel {
    Workflow,
    Agent,
    Tool,
}

pub fn panel_for(name: &str) -> Panel {
    if name == workflow::TOOL {
        Panel::Workflow
    } else if tool_kinds::is_agent(name) {
        Panel::Agent
    } else {
        Panel::Tool
    }
}

/// L'état de l'inspecteur d'une session : celui de chaque panneau (ils gardent leurs lectures en cours et ce qui a
/// été tapé d'une image à l'autre, chacun remis à zéro quand son sujet change).
#[derive(Default)]
pub struct InspectorState {
    pub permission: permission::PermissionState,
    pub tasks: tasks::TasksState,
    pub tool: tool_detail::ToolDetailState,
    pub agent: agents::AgentState,
    pub workflow: agents::WorkflowState,
    pub diff: context::DiffState,
    pub context: context::ContextState,
}

pub fn show(ui: &mut Ui, cx: &mut Cx, view: &mut super::SessionView, id: &str) {
    let st = &mut view.inspector;
    let Some(h) = cx.app.session(id) else {
        ui.centered_and_justified(|ui| ui.spinner());
        return;
    };
    if h.found.is_none() {
        ui.centered_and_justified(|ui| ui.spinner());
        return;
    }
    let pending = h.meta.pending.first().cloned();
    let has_tasks = !h.meta.tasks.is_empty();
    let selected = cx.ui.selected_tool_id.clone().and_then(|sid| h.tool_by_id(&sid).map(|x| (x.id.clone(), panel_for(&x.name))));
    // (le journal d'un agent de workflow n'est gardé par le moteur que tant qu'il est montré)
    if pending.is_some() || !matches!(selected, Some((_, Panel::Workflow))) {
        agents::workflow_leave(cx, &mut st.workflow, id);
    }

    if let Some(p) = pending {
        ScrollArea::vertical().id_salt(("insp-perm", id)).auto_shrink(false).show(ui, |ui| {
            permission::show(ui, cx, &mut st.permission, id, &p);
            if has_tasks {
                tasks::show(ui, cx, &mut st.tasks, id);
            }
        });
        return;
    }
    if has_tasks {
        ScrollArea::vertical().id_salt(("insp-tasks", id)).max_height(ui.available_height() * 0.45).auto_shrink([false, true]).show(ui, |ui| {
            tasks::show(ui, cx, &mut st.tasks, id);
        });
    }
    match selected {
        Some((tid, Panel::Tool)) => tool_detail::show(ui, cx, &mut st.tool, id, &tid),
        Some((tid, Panel::Agent)) => agents::show_agent(ui, cx, &mut st.agent, id, &tid),
        Some((tid, Panel::Workflow)) => agents::show_workflow(ui, cx, &mut st.workflow, id, &tid),
        None if cx.ui.diff_open => context::show_diff(ui, cx, &mut st.diff, id),
        // (sans process, une session passée : le contexte est vide, mais les fichiers de mémoire sont sur le disque)
        None => context::show_context(ui, cx, &mut st.context, id),
    }
}

// ------------------------------------------------------------------------------------------- pièces communes

/// L'en-tête d'un panneau (`.ph` : titre, sous-titre à chasse fixe, actions à droite).
pub fn ph(ui: &mut Ui, t: &Tokens, title: &str, sub: &str, right: impl FnOnce(&mut Ui)) {
    widgets::pane_header(
        ui,
        t,
        |ui| {
            ui.label(RichText::new(title).font(theme::semibold(theme::LABEL)).color(t.text));
            if !sub.is_empty() {
                ui.add(egui::Label::new(RichText::new(sub).font(theme::mono(theme::CAPTION)).color(t.text_faint)).truncate());
            }
        },
        right,
    );
}

/// Une section (`.section` : marges, trait dessous ; `last` : sans trait).
pub fn section<R>(ui: &mut Ui, t: &Tokens, last: bool, add: impl FnOnce(&mut Ui) -> R) -> R {
    let r = Frame::new().inner_margin(Margin::symmetric(20, 16)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 10.0;
        add(ui)
    });
    if !last {
        let rect = r.response.rect;
        ui.painter().hline(rect.x_range(), rect.bottom(), Stroke::new(1.0, t.separator));
    }
    r.inner
}

/// Un titre de section (`.section h3`).
pub fn h3(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.add(egui::Label::new(RichText::new(text).font(theme::semibold(theme::SECTION)).color(t.text)).wrap());
}

/// Un paragraphe de section (`.section p`).
pub fn para(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.add(egui::Label::new(RichText::new(text).font(egui::FontId::proportional(theme::LABEL)).color(t.text_dim)).wrap());
}

/// Des lignes de clés et valeurs (`.kv` : légende atténuée à gauche, valeur à chasse fixe).
pub fn kv(ui: &mut Ui, t: &Tokens, id: impl std::hash::Hash + std::fmt::Debug, rows: Vec<(String, RichText)>) {
    egui::Grid::new(egui::Id::new(id)).num_columns(2).min_col_width(96.0).spacing([12.0, 7.0]).show(ui, |ui| {
        for (k, v) in rows {
            ui.label(RichText::new(k).font(theme::mono(theme::CAPTION)).color(t.text_faint));
            ui.add(egui::Label::new(v.font(theme::mono(theme::CAPTION))).wrap());
            ui.end_row();
        }
    });
}

/// La valeur d'une ligne `.kv` à la couleur ordinaire.
pub fn val(t: &Tokens, s: impl Into<String>) -> RichText {
    RichText::new(s.into()).color(t.text_dim)
}

/// Un message d'erreur encadré (`.errbox`).
pub fn errbox(ui: &mut Ui, t: &Tokens, text: &str) {
    Frame::new()
        .fill(t.danger.gamma_multiply(0.12))
        .stroke(Stroke::new(1.0, t.danger.gamma_multiply(0.5)))
        .corner_radius(CornerRadius::same(theme::R))
        .inner_margin(Margin::symmetric(10, 8))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.add(egui::Label::new(RichText::new(text).color(t.danger).font(egui::FontId::proportional(theme::LABEL))).wrap());
        });
}

/// Un texte à chasse fixe dans un cadre de code (`pre.cmdlog`), sélectionnable, défilement horizontal.
pub fn pre(ui: &mut Ui, t: &Tokens, salt: impl std::hash::Hash + std::fmt::Debug, text: &str) {
    Frame::new().fill(t.code_bg).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::same(10)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ScrollArea::horizontal().id_salt(egui::Id::new(salt)).auto_shrink([false, true]).show(ui, |ui| {
            ui.add(egui::Label::new(RichText::new(text).font(theme::mono(theme::CODE - 1.0)).color(t.text_dim)).extend());
        });
    });
}

/// La ligne des raccourcis (`.keys` : touche, ce qu'elle fait · …).
pub fn keys(ui: &mut Ui, t: &Tokens, parts: &[(String, String)]) {
    ui.horizontal_wrapped(|ui| {
        ui.spacing_mut().item_spacing.x = 4.0;
        for (i, (k, what)) in parts.iter().enumerate() {
            if i > 0 {
                ui.label(RichText::new("·").font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
            }
            widgets::kbd(ui, t, k);
            ui.label(RichText::new(what).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint));
        }
    });
}

/// La couleur du nom d'un outil (`tool_kinds::color` : les variables de app.css, prises dans le thème).
pub fn kind_color(t: &Tokens, name: &str) -> Color32 {
    match tool_kinds::color(name) {
        "var(--read)" | "var(--web)" => t.accent_ink,
        "var(--grep)" => t.warning,
        "var(--edit)" | "var(--write)" => t.success,
        "var(--bash)" => t.danger,
        _ => t.text_dim,
    }
}

/// Le point d'état d'un agent, d'une phase ou d'une tâche (`.dot run|done|wait|err|idle|exited`).
pub fn state_dot(ui: &mut Ui, t: &Tokens, kind: &str) {
    let (c, filled) = match kind {
        "run" => (t.success, true),
        "done" => (t.success, false),
        "wait" => (t.warning, true),
        "err" => (t.danger, true),
        _ => (t.text_faint, false),
    };
    widgets::dot(ui, c, filled);
}

/// `ToolDetail.Pretty(json)` : le JSON indenté.
pub fn pretty(json: &serde_json::Value) -> String {
    serde_json::to_string_pretty(json).unwrap_or_default()
}

const MAX_ROWS: usize = 2000;

/// Le diff d'un fichier (`common/diff_view.rs`) : l'en-tête (chemin, +ajouts −retraits) et jusqu'à 2000 lignes,
/// numéros à gauche, défilement horizontal.
pub fn diff_view(ui: &mut Ui, t: &Tokens, salt: impl std::hash::Hash + std::fmt::Debug, path: &str, lines: &[DiffLine]) {
    let adds = lines.iter().filter(|l| l.kind == DiffKind::Add).count();
    let dels = lines.iter().filter(|l| l.kind == DiffKind::Del).count();
    let more = lines.len().saturating_sub(MAX_ROWS);
    let font = theme::mono(theme::CODE - 1.0);
    Frame::new().fill(t.field).stroke(Stroke::new(1.0, t.separator)).corner_radius(CornerRadius::same(theme::R)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        let head = Frame::new().inner_margin(Margin::symmetric(10, 6)).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.horizontal(|ui| {
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.label(RichText::new(format!("−{dels}")).font(theme::mono(theme::CAPTION)).color(t.danger));
                    ui.label(RichText::new(format!("+{adds}")).font(theme::mono(theme::CAPTION)).color(t.success));
                    ui.with_layout(egui::Layout::left_to_right(egui::Align::Center), |ui| {
                        ui.add(egui::Label::new(RichText::new(path).font(theme::mono(theme::CAPTION)).color(t.text_dim)).truncate()).on_hover_text(path);
                    });
                });
            });
        });
        let r = head.response.rect;
        ui.painter().hline(r.x_range(), r.bottom(), Stroke::new(1.0, t.separator));
        ScrollArea::horizontal().id_salt(egui::Id::new(salt)).auto_shrink([false, true]).show(ui, |ui| {
            ui.spacing_mut().item_spacing = egui::vec2(0.0, 0.0);
            for l in lines.iter().take(MAX_ROWS) {
                diff_row(ui, t, l, &font);
            }
            if more > 0 {
                hunk(ui, t, &t!("Diff.MoreLines", more), &font);
            }
        });
    });
}

fn hunk(ui: &mut Ui, t: &Tokens, text: &str, font: &egui::FontId) {
    Frame::new().fill(t.accent_soft.gamma_multiply(0.5)).inner_margin(Margin::symmetric(10, 1)).show(ui, |ui| {
        ui.set_min_width(ui.available_width());
        ui.add(egui::Label::new(RichText::new(text).font(font.clone()).color(t.accent_ink)).extend());
    });
}

fn diff_row(ui: &mut Ui, t: &Tokens, l: &DiffLine, font: &egui::FontId) {
    match l.kind {
        DiffKind::Hunk => hunk(ui, t, &l.text, font),
        DiffKind::More => hunk(ui, t, &t!("Diff.MoreOccurrences", l.n.unwrap_or(0)), font),
        DiffKind::Ctx | DiffKind::Add | DiffKind::Del => {
            let (bg, fg, sign, sign_c) = match l.kind {
                DiffKind::Add => (t.success.gamma_multiply(0.10), t.text, "+", t.success),
                DiffKind::Del => (t.danger.gamma_multiply(0.11), t.text, "−", t.danger),
                _ => (Color32::TRANSPARENT, t.text_dim, "", t.text_faint),
            };
            Frame::new().fill(bg).show(ui, |ui| {
                ui.set_min_width(ui.available_width());
                ui.horizontal(|ui| {
                    ui.spacing_mut().item_spacing.x = 0.0;
                    let n = l.n.map(|n| n.to_string()).unwrap_or_default();
                    ui.add_sized([34.0, 18.0], egui::Label::new(RichText::new(n).font(font.clone()).color(t.text_faint)));
                    ui.add_space(6.0);
                    ui.add_sized([14.0, 18.0], egui::Label::new(RichText::new(sign).font(font.clone()).color(sign_c)));
                    ui.add(egui::Label::new(RichText::new(&l.text).font(font.clone()).color(fg)).extend());
                    ui.add_space(10.0);
                });
            });
        }
    }
}

/// La barre d'onglets (`.tabs` : le choisi souligné de l'accent).
pub fn tabs(ui: &mut Ui, t: &Tokens, labels: &[String], on: &mut usize) {
    let r = Frame::new().inner_margin(Margin { left: 12, right: 12, top: 8, bottom: 0 }).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.horizontal(|ui| {
            ui.spacing_mut().item_spacing.x = 18.0;
            for (i, label) in labels.iter().enumerate() {
                let sel = *on == i;
                let c = if sel { t.text } else { t.text_faint };
                let b = ui.add(egui::Button::new(RichText::new(label).font(theme::medium(theme::LABEL)).color(c)).frame(false).min_size(egui::vec2(0.0, 30.0)));
                if sel {
                    ui.painter().hline(b.rect.x_range(), b.rect.bottom() + 1.0, Stroke::new(2.0, t.accent));
                }
                if b.clicked() {
                    *on = i;
                }
            }
        });
    });
    let rect = r.response.rect;
    ui.painter().hline(rect.x_range(), rect.bottom() + 2.0, Stroke::new(1.0, t.separator));
    ui.add_space(4.0);
}

/// Le bouton ✕ qui ferme un panneau.
pub fn close_button(ui: &mut Ui, t: &Tokens) -> bool {
    widgets::ghost(ui, t, widgets::icon("x")).on_hover_text(t!("Action.Close")).clicked()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn panels() {
        assert_eq!(panel_for("Workflow"), Panel::Workflow);
        assert_eq!(panel_for("Agent"), Panel::Agent);
        assert_eq!(panel_for("Bash"), Panel::Tool);
    }

    #[test]
    fn pretty_json() {
        assert_eq!(pretty(&serde_json::json!({"a": "<é>", "b": [1]})), "{\n  \"a\": \"<é>\",\n  \"b\": [\n    1\n  ]\n}");
    }
}
