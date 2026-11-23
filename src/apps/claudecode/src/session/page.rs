//! La page d'une session (`pages/session_page.rs`) : en cours de résolution, introuvable, ou l'en-tête, le fil, la
//! liste des tâches et le composeur (l'inspecteur est un panneau de app.rs). Échap, quand rien d'autre ne le prend (popover, menu, palette),
//! interrompt le tour en cours.

use ccui_shared::{i18n, t};
use egui::{Frame, Margin, RichText, Ui};

use crate::app::{Cx, Route};
use crate::session::{SessionView, composer, header, thread};
use crate::theme;
use crate::widgets::{self, icon};

/// Ce que la page montre.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Resolved {
    Loading,
    Found,
    NotFound,
}

pub fn resolved(found: Option<bool>) -> Resolved {
    match found {
        Some(true) => Resolved::Found,
        Some(false) => Resolved::NotFound,
        None => Resolved::Loading,
    }
}

pub fn show(ui: &mut Ui, cx: &mut Cx, view: &mut SessionView, id: &str) {
    let t = cx.t;
    let state = resolved(cx.app.session(id).and_then(|h| h.found));
    match state {
        Resolved::Loading => {
            ui.centered_and_justified(|ui| ui.spinner());
        }
        Resolved::NotFound => {
            widgets::pane_header(ui, &t, |ui| {
                ui.label(RichText::new(t!("Session.NotFound")).font(theme::semibold(theme::BODY)));
            }, |_| {});
            Frame::new().inner_margin(Margin::same(24)).show(ui, |ui| {
                ui.label(t!("Session.NotFoundText"));
                ui.add_space(10.0);
                ui.horizontal(|ui| {
                    if widgets::primary(ui, &t, &format!("{} {}", icon("plus"), t!("Rail.NewSession"))).clicked() {
                        cx.navigate(Route::NewSession);
                    }
                    if ui.button(format!("{} {}  {}", icon("search"), t!("Rail.Search"), i18n::kbd("Ctrl K"))).clicked() {
                        cx.ui.palette_open = true;
                    }
                });
            });
        }
        Resolved::Found => {
            // Échap : interrompt un tour en cours (s'il n'a servi à rien d'autre pendant la passe)
            let busy = cx.app.session(id).is_some_and(|h| h.busy());
            if busy && !cx.ui.palette_open && ui.input(|i| i.key_pressed(egui::Key::Escape)) && !ui.ctx().any_popup_open()
                && let Ok(s) = cx.be.session(id)
            {
                let what = format!("[{id}] {}", t!("Composer.Interrupt"));
                cx.be.fire(&what, async move { s.interrupt().await });
            }
            egui::Panel::top("session-header").frame(Frame::new()).show_inside(ui, |ui| header::show(ui, cx, &mut view.header, id));
            egui::Panel::bottom("session-composer")
                .frame(Frame::new().inner_margin(Margin { left: 24, right: 24, top: 6, bottom: 10 }))
                .show_inside(ui, |ui| composer::show(ui, cx, &mut view.composer, id));
            // la liste des tâches de l'agent, entre le fil et le composeur (`TodoPanel`)
            if cx.app.session(id).is_some_and(|h| !ccui_shared::todo_list::from(&h.items).is_empty()) {
                egui::Panel::bottom("session-todos")
                    .frame(Frame::new().inner_margin(Margin { left: 24, right: 24, top: 6, bottom: 0 }))
                    .show_inside(ui, |ui| thread::todos(ui, cx, &mut view.thread, id));
            }
            egui::CentralPanel::default().frame(Frame::new()).show_inside(ui, |ui| thread::show(ui, cx, &mut view.thread, id));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolution() {
        assert_eq!(resolved(None), Resolved::Loading);
        assert_eq!(resolved(Some(true)), Resolved::Found);
        assert_eq!(resolved(Some(false)), Resolved::NotFound);
    }
}
