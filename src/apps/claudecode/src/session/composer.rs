//! Le composeur (`session/composer.rs`, popovers `@` et `/`, menus du modèle, du mode, de l'effort et de /config, pièces jointes, file d'attente, ligne d'état).
//! À FAIRE : le port de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct ComposerState {}

pub fn show(ui: &mut Ui, cx: &mut Cx, _st: &mut ComposerState, id: &str) {
    let _ = (cx, id);
    ui.label("composer");
}
