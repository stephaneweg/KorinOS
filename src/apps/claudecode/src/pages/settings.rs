//! Réglages : langue, taille du code, réflexion, horodatage, notifications ; le thème reste celui de Korin (`pages/appearance.rs`).
//! À FAIRE : le port de la page de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct Settings {}

impl Settings {
    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let _ = cx;
        ui.label("Settings");
    }
}
