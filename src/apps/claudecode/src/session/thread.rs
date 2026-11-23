//! Le fil de la session (`session/thread.rs`, `tool_row.rs`, cartes) : messages, texte en cours, outils, réflexion, compaction, erreurs.
//! À FAIRE : le port de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct ThreadState {}

pub fn show(ui: &mut Ui, cx: &mut Cx, _st: &mut ThreadState, id: &str) {
    let _ = (cx, id);
    ui.label("thread");
}
