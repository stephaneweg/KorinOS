//! Arrière-plan : les agents d'arrière-plan de Claude Code (`pages/background.rs`).
//! À FAIRE : le port de la page de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct Background {}

impl Background {
    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let _ = cx;
        ui.label("Background");
    }
}
