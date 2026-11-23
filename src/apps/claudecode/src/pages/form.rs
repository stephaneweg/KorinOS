//! Les pièces de formulaire des pages Nouvelle session, Extensions et Réglages (`.toggle`, `.seg`, `.set-row`,
//! `.field` de `app.css` de ClaudeCodeUI), aux couleurs du thème de Korin : interrupteur, choix segmenté, ligne de
//! réglage (titre et aide à gauche, contrôle à droite), champ titré, page défilante centrée.

use egui::{Align, CornerRadius, Frame, Layout, Margin, Response, RichText, ScrollArea, Sense, Stroke, Ui, Vec2};

use crate::theme::{self, Tokens};
use crate::widgets;

/// Un interrupteur (`.toggle` : la glissière puis le libellé) ; cliqué : à l'appelant de basculer.
pub fn switch(ui: &mut Ui, t: &Tokens, on: bool, label: &str, enabled: bool) -> Response {
    let r = ui
        .add_enabled_ui(enabled, |ui| {
            ui.horizontal(|ui| {
                ui.spacing_mut().item_spacing.x = 8.0;
                let (rect, _) = ui.allocate_exact_size(Vec2::new(28.0, 16.0), Sense::hover());
                let (track, knob) = if on { (t.accent, t.on_accent) } else { (t.raised, t.text_dim) };
                let track = if enabled { track } else { track.gamma_multiply(0.5) };
                ui.painter().rect(rect, CornerRadius::same(8), track, Stroke::new(1.0, if on { t.accent } else { t.card_border }), egui::StrokeKind::Inside);
                let x = if on { rect.right() - 8.0 } else { rect.left() + 8.0 };
                ui.painter().circle_filled(egui::pos2(x, rect.center().y), 5.0, knob);
                if !label.is_empty() {
                    ui.label(RichText::new(label).font(theme::medium(theme::SMALL)).color(if enabled { t.text } else { t.text_faint }));
                }
            })
            .response
        })
        .inner;
    let r = ui.interact(r.rect, r.id.with("switch"), if enabled { Sense::click() } else { Sense::hover() });
    if r.hovered() && enabled {
        ui.ctx().set_cursor_icon(egui::CursorIcon::PointingHand);
    }
    r
}

/// Un choix segmenté (`.seg`) : l'indice cliqué.
pub fn seg(ui: &mut Ui, t: &Tokens, options: &[String], selected: Option<usize>) -> Option<usize> {
    let mut out = None;
    Frame::new().stroke(Stroke::new(1.0, t.card_border)).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::same(2)).show(ui, |ui| {
        ui.horizontal(|ui| {
            ui.spacing_mut().item_spacing.x = 2.0;
            for (i, o) in options.iter().enumerate() {
                let on = selected == Some(i);
                let text = RichText::new(o).font(if on { theme::semibold(theme::SMALL) } else { theme::medium(theme::SMALL) }).color(if on { t.accent_ink } else { t.text_dim });
                let b = egui::Button::new(text).fill(if on { t.accent_soft } else { egui::Color32::TRANSPARENT }).stroke(Stroke::NONE).min_size(Vec2::new(40.0, 26.0));
                if ui.add(b).clicked() {
                    out = Some(i);
                }
            }
        });
    });
    out
}

/// Le titre d'un groupe de réglages (`.set-grp h2`).
pub fn group_title(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.add_space(14.0);
    ui.label(RichText::new(text).font(theme::semibold(theme::SECTION)).color(t.text));
    ui.add_space(4.0);
    let y = ui.cursor().top();
    ui.painter().hline(ui.max_rect().x_range(), y, Stroke::new(1.0, t.separator));
    ui.add_space(6.0);
}

/// Une ligne de réglage (`.set-row`) : le titre en gras et l'aide sous lui à gauche, le contrôle à droite.
pub fn set_row(ui: &mut Ui, t: &Tokens, title: &str, hint: &str, control: impl FnOnce(&mut Ui)) {
    let w = ui.available_width();
    let left = (w * 0.34).clamp(180.0, 280.0);
    ui.horizontal_top(|ui| {
        ui.allocate_ui_with_layout(Vec2::new(left, 0.0), Layout::top_down(Align::Min), |ui| {
            ui.set_width(left);
            ui.label(RichText::new(title).font(theme::semibold(theme::SMALL)).color(t.text));
            if !hint.is_empty() {
                ui.add(egui::Label::new(RichText::new(hint).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim)).wrap());
            }
        });
        ui.add_space(24.0);
        ui.allocate_ui_with_layout(Vec2::new(ui.available_width(), 0.0), Layout::top_down(Align::Min), control);
    });
    ui.add_space(14.0);
}

/// Un champ titré (`.field` : l'étiquette au-dessus).
pub fn field_label(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.add_space(6.0);
    ui.label(RichText::new(text).font(theme::semibold(theme::LABEL)).color(t.text));
}

/// Un message sous un champ (`.fmsg`, `.fmsg.risk`).
pub fn fmsg(ui: &mut Ui, t: &Tokens, text: &str, risk: bool) {
    let c = if risk { t.warning } else { t.danger };
    ui.add(egui::Label::new(RichText::new(text).font(egui::FontId::proportional(theme::CAPTION)).color(c)).wrap());
}

/// Une page : l'en-tête (titre, sous-titre facultatif, actions à droite) puis un contenu défilant, centré et limité
/// en largeur.
pub fn page(ui: &mut Ui, t: &Tokens, title: &str, sub: &str, max_w: f32, right: impl FnOnce(&mut Ui), body: impl FnOnce(&mut Ui)) {
    widgets::pane_header(
        ui,
        t,
        |ui| {
            ui.label(RichText::new(title).font(theme::semibold(theme::BODY)).color(t.text));
            if !sub.is_empty() {
                widgets::mono_dim(ui, t, sub);
            }
        },
        right,
    );
    ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
        let avail = ui.available_width();
        let w = (avail - 48.0).min(max_w).max(200.0);
        let pad = ((avail - w) / 2.0).max(0.0);
        ui.add_space(16.0);
        ui.horizontal_top(|ui| {
            ui.add_space(pad);
            ui.vertical(|ui| {
                ui.set_width(w);
                body(ui);
                ui.add_space(32.0);
            });
        });
    });
}
