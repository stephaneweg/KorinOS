//! L'en-tête de la session (`session_header.rs`, `session_title.rs`) : nom, branche, dépôt, mode, actions.
//! À FAIRE : le port de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct HeaderState {}

pub fn show(ui: &mut Ui, cx: &mut Cx, _st: &mut HeaderState, id: &str) {
    let _ = (cx, id);
    ui.label("header");
}
