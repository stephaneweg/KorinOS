//! Vue d'ensemble de toutes les sessions et file des décisions, Ctrl ⇧ A (`pages/overview.rs`).
//! À FAIRE : le port de la page de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct Overview {}

impl Overview {
    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let _ = cx;
        ui.label("Overview");
    }
}
