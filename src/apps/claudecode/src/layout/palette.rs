//! La palette de commandes, Ctrl K (`layout/command_palette.rs`) : sessions, récentes, recherche dans les
//! transcriptions, actions.
//! À FAIRE : le port de ClaudeCodeUI.

use crate::app::Cx;

#[derive(Default)]
pub struct Palette {}

impl Palette {
    pub fn ui(&mut self, ctx: &egui::Context, cx: &mut Cx) {
        egui::Modal::new(egui::Id::new("palette")).show(ctx, |ui| {
            ui.label("Palette");
            if ui.button("✕").clicked() {
                cx.ui.palette_open = false;
            }
        });
    }
}
