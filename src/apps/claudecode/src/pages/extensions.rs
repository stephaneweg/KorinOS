//! Extensions : MCP, skills, agents, plugins, hooks, règles d'autorisation (`pages/extensions/*`).
//! À FAIRE : le port de la page de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct Extensions {}

impl Extensions {
    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let _ = cx;
        ui.label("Extensions");
    }
}
