//! L'inspecteur (`session/inspector.rs`) : la demande d'autorisation en attente (diff, autoriser, refuser, toujours autoriser), les tâches, l'outil ou l'agent choisi, le diff (/diff), le contexte.
//! À FAIRE : le port de ClaudeCodeUI.

use egui::Ui;

use crate::app::Cx;

#[derive(Default)]
pub struct InspectorState {}

pub fn show(ui: &mut Ui, cx: &mut Cx, _view: &mut super::SessionView, id: &str) {
    let _ = (cx, id);
    ui.label("inspector");
}
