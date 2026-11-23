//! Nouvelle session : dossier, worktree isolé, mode d'autorisation (`pages/new_session.rs`).
//! À FAIRE : le port de la page de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct NewSession {}

impl NewSession {
    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let _ = cx;
        ui.label("NewSession");
    }
}
