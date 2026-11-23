//! Worktrees : classement et nettoyage sans risque (`pages/worktrees.rs`).
//! À FAIRE : le port de la page de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct Worktrees {}

impl Worktrees {
    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let _ = cx;
        ui.label("Worktrees");
    }
}
