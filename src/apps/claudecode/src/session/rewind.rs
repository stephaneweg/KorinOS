//! La boîte « Revenir à ce message ? » (`rewind_dialog.rs` de ClaudeCodeUI) : ouverte par le bouton d'un message
//! du fil, par « Modifier et réessayer » d'un refus, ou par `/rewind` tapé dans le composeur (`cx.ui.rewind`).
//! Elle montre ce qui reviendra sur le disque (`preview_rewind` : fichiers, lignes), puis revient : les fichiers
//! remis, la conversation coupée avant le message, dont le texte attend dans le composeur.

use ccui_shared::model::{RewindPreview, UserItem};
use ccui_shared::{t, tool_kinds};
use egui::{Align, Frame, Label, Layout, Margin, RichText, ScrollArea, Stroke, Ui};

use crate::backend::{Backend, Task};
use crate::layout::rail;
use crate::theme::{self, Tokens};
use crate::widgets::{self, icon};

#[derive(Default)]
pub struct RewindState {
    /// Le message visé (la boîte est ouverte).
    shown: Option<UserItem>,
    preview: Option<RewindPreview>,
    preview_task: Option<Task<Result<RewindPreview, String>>>,
    error: Option<String>,
    rewind_task: Option<Task<Result<(), String>>>,
}

impl RewindState {
    /// Ouvre la boîte sur `u` (chaque demande, le même message deux fois compris) et lit l'aperçu.
    pub fn open(&mut self, be: &Backend, id: &str, u: UserItem) {
        let Some(uuid) = u.uuid.clone() else { return };
        *self = RewindState { shown: Some(u), ..Default::default() };
        match be.session(id) {
            Ok(s) => self.preview_task = Some(be.task(async move { s.preview_rewind(&uuid).await })),
            Err(e) => self.error = Some(e),
        }
    }

    /// La boîte, tant qu'elle est ouverte ; rend true quand le retour a eu lieu (le composeur reprend le texte).
    pub fn show(&mut self, ui: &mut Ui, t: &Tokens, be: &Backend, id: &str, cwd: &str) -> bool {
        // (une réponse arrivée pour une ouverture remplacée depuis : la tâche a été remplacée avec l'état)
        if let Some(r) = self.preview_task.as_ref().and_then(Task::take) {
            self.preview_task = None;
            match r {
                Ok(p) => self.preview = Some(p),
                Err(e) => self.error = Some(e),
            }
        }
        let mut done = false;
        if let Some(r) = self.rewind_task.as_ref().and_then(Task::take) {
            self.rewind_task = None;
            match r {
                Ok(()) => {
                    self.shown = None;
                    done = true;
                }
                Err(e) => self.error = Some(e),
            }
        }
        let Some(u) = self.shown.clone() else { return done };
        let busy = self.rewind_task.is_some();
        let mut confirm = false;
        let out = rail::modal(ui.ctx(), t, "rewind", &t!("Rewind.Title"), |ui| {
            ui.set_max_width(520.0);
            ui.add(Label::new(RichText::new(t!("Rewind.Text")).color(t.text_dim)).wrap());
            ui.add_space(6.0);
            // (le message, en citation)
            let q = Frame::new().inner_margin(Margin { left: 12, right: 4, top: 2, bottom: 2 }).show(ui, |ui| {
                ScrollArea::vertical().id_salt("rw-quote").max_height(120.0).show(ui, |ui| {
                    ui.add(Label::new(RichText::new(&u.text).color(t.text)).wrap());
                });
            });
            let r = q.response.rect;
            ui.painter().vline(r.left() + 2.0, r.y_range(), Stroke::new(3.0, t.card_border));
            ui.add_space(6.0);
            match (&self.preview, self.error.is_some()) {
                (None, false) => {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label(RichText::new(t!("Rewind.Loading")).color(t.text_dim));
                    });
                }
                (None, true) => {}
                (Some(p), _) if p.changes() => {
                    ui.horizontal(|ui| {
                        widgets::cap(ui, t, &t!("Rewind.Files"));
                        ui.label(RichText::new(t!("Rewind.Lines", p.insertions, p.deletions)).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                    });
                    ScrollArea::vertical().id_salt("rw-files").max_height(160.0).show(ui, |ui| {
                        for f in &p.files {
                            ui.add(Label::new(RichText::new(tool_kinds::relative(f, cwd)).font(theme::mono(theme::CODE)).color(t.text)));
                        }
                    });
                }
                (Some(p), _) if p.can_rewind => {
                    ui.label(RichText::new(t!("Rewind.NoFiles")).color(t.text_dim));
                }
                (Some(p), _) => {
                    ui.add(Label::new(RichText::new(t!("Rewind.FilesUnavailable", p.error.clone().unwrap_or_default())).color(t.text_dim)).wrap());
                }
            }
            if let Some(e) = &self.error {
                widgets::error_text(ui, t, e);
            }
            let mut out = None;
            ui.add_space(10.0);
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                let ok = self.preview.is_some() && !busy;
                if ui.add_enabled(ok, egui::Button::new(RichText::new(format!("{} {}", icon("rewind"), t!("Rewind.Confirm"))).color(t.danger))).clicked() {
                    confirm = true;
                }
                if busy {
                    ui.spinner();
                }
                if ui.button(t!("Action.Cancel")).clicked() {
                    out = Some(false);
                }
            });
            out
        });
        if out == Some(false) && !busy {
            self.shown = None;
        }
        if confirm && let (Some(uuid), Some(p)) = (u.uuid.clone(), self.preview.as_ref()) {
            self.error = None;
            let files = p.changes();
            match be.session(id) {
                Ok(s) => self.rewind_task = Some(be.task(async move { s.rewind(&uuid, files).await })),
                Err(e) => self.error = Some(e),
            }
        }
        done
    }
}
