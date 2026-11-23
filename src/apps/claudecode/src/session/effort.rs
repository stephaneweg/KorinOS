//! Le curseur de l'effort (`common/effort_slider.rs` de ClaudeCodeUI) : les niveaux du modèle, dans l'ordre (plafonnés
//! par les réglages), en segments cliquables ; le niveau en vigueur est surligné. Le choix part au clic.

use egui::{CornerRadius, Frame, Margin, RichText, Sense, Stroke, Ui};

use crate::theme::{self, Tokens};

/// L'indice de `value` parmi les `reach` premiers niveaux, -1 s'il n'y est pas (effort inconnu : aucun surligné).
pub fn index(stops: &[String], value: &str, reach: usize) -> i32 {
    stops.iter().take(reach).position(|s| s == value).map_or(-1, |i| i as i32)
}

/// Les segments ; rend le niveau cliqué s'il diffère de `value`.
pub fn show(ui: &mut Ui, t: &Tokens, levels: &[String], value: &str, label: &str, name: impl Fn(&str) -> String) -> Option<String> {
    let at = index(levels, value, levels.len());
    let mut picked = None;
    Frame::new()
        .stroke(Stroke::new(1.0, t.card_border))
        .corner_radius(CornerRadius::same(theme::R))
        .inner_margin(Margin::same(1))
        .show(ui, |ui| {
            ui.spacing_mut().item_spacing.x = 0.0;
            ui.horizontal(|ui| {
                for (i, l) in levels.iter().enumerate() {
                    let on = i as i32 == at;
                    let text = RichText::new(name(l)).font(theme::mono(theme::CAPTION)).color(if on { t.text } else { t.text_dim });
                    let fill = if on { t.hover } else { egui::Color32::TRANSPARENT };
                    let r = Frame::new()
                        .fill(fill)
                        .corner_radius(CornerRadius::same(theme::R - 1))
                        .inner_margin(Margin::symmetric(7, 3))
                        .show(ui, |ui| ui.label(text))
                        .response;
                    let r = ui.interact(r.rect, ui.scope_id().with(("effort", i)), Sense::click()).on_hover_text(format!("{label} : {}", name(l)));
                    if r.hovered() && !on {
                        ui.painter().rect_filled(r.rect, CornerRadius::same(theme::R - 1), t.hover.gamma_multiply(0.5));
                    }
                    if r.clicked() && l != value {
                        picked = Some(l.clone());
                    }
                }
            });
        });
    picked
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn index_of_level() {
        let l: Vec<String> = ["low", "medium", "high", "max"].map(String::from).to_vec();
        assert_eq!(index(&l, "high", 4), 2);
        assert_eq!(index(&l, "max", 3), -1);
        assert_eq!(index(&l, "xhigh", 4), -1);
    }
}
