//! Réglages (`pages/appearance.rs` de ClaudeCodeUI) : la langue (fr / en, tout de suite), le thème — celui de
//! Korin, pas les cinq de ClaudeCodeUI : Thème de Korin, Sombre, Clair (`organize.json`, comme le menu Affichage) —,
//! la taille du code (préférence `code-size`, montrée sur l'aperçu), les notifications, les résumés de réflexion et
//! l'heure des messages (drapeaux du moteur), et « À propos » : la version, l'origine (ClaudeCodeUI, licence MIT).

use ccui_shared::api::{FLAG_NOTIFY, FLAG_THINKING, FLAG_TIMESTAMPS, PREF_CODE_SIZE};
use ccui_shared::i18n::{self, Lang};
use ccui_shared::t;
use egui::{CornerRadius, Frame, Margin, RichText, Stroke, Ui};

use crate::app::Cx;
use crate::pages::form;
use crate::theme;
use crate::widgets::{self, tr};

/// Chaque langue est nommée en elle-même.
const LANGUAGES: [(&str, &str); 2] = [("en", "English"), ("fr", "Français")];
const SIZES: [u32; 4] = [12, 13, 14, 15];
/// Les thèmes de l'app (`Organize::theme`).
const THEMES: [&str; 3] = ["system", "dark", "light"];

/// L'aperçu de la taille du code (le même extrait que ClaudeCodeUI).
pub fn sample() -> String {
    format!(
        "// {}\nMood MoodFor(Session s) => s.State switch\n{{\n    State.WaitingPermission => Mood.Shocked,\n    State.Running           => Mood.Excited,\n    State.Crashed           => Mood.Ko,\n    _                       => Mood.Happy,\n}};",
        t!("Appearance.SampleComment")
    )
}

/// La taille du code choisie, en points (13 par défaut ; « 14px » → 14).
pub fn code_size(pref: Option<&str>) -> u32 {
    pref.and_then(|p| p.trim_end_matches("px").parse().ok()).filter(|n| SIZES.contains(n)).unwrap_or(13)
}

fn theme_name(k: &str) -> String {
    match k {
        "dark" => tr("Sombre", "Dark"),
        "light" => tr("Clair", "Light"),
        _ => tr("Thème de Korin", "Korin theme"),
    }
}

#[derive(Default)]
pub struct Settings {}

impl Settings {
    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let t = cx.t;
        form::page(ui, &t, &t!("Settings.Title"), "", 860.0, |_| {}, |ui| {
            // ---- apparence
            form::group_title(ui, &t, &t!("Settings.Look"));
            form::set_row(ui, &t, &t!("Appearance.Language"), &tr("Langue de l’interface, appliquée tout de suite.", "Interface language, applied at once."), |ui| {
                let cur = i18n::lang().code();
                let names: Vec<String> = LANGUAGES.iter().map(|(_, n)| n.to_string()).collect();
                let sel = LANGUAGES.iter().position(|(c, _)| *c == cur);
                if let Some(i) = form::seg(ui, &t, &names, sel)
                    && Some(i) != sel
                {
                    let code = LANGUAGES[i].0;
                    match ccui_core::app_flag::set_culture(Lang::from_code(code)) {
                        Ok(()) => {
                            i18n::set_lang(Lang::from_code(code));
                            cx.app.boot.culture = code.to_string();
                        }
                        Err(e) => cx.be.error(e),
                    }
                }
            });
            form::set_row(
                ui,
                &t,
                &t!("Appearance.Theme"),
                &tr("Celui de Korin, ou toujours sombre, ou toujours clair. Aussi dans le menu Affichage.", "Korin’s, or always dark, or always light. Also in the View menu."),
                |ui| {
                    let names: Vec<String> = THEMES.iter().map(|k| theme_name(k)).collect();
                    let cur = if cx.org.theme.is_empty() { "system" } else { cx.org.theme.as_str() };
                    let sel = THEMES.iter().position(|k| *k == cur);
                    if let Some(i) = form::seg(ui, &t, &names, sel) {
                        cx.org.theme = THEMES[i].to_string();
                        cx.org.save();
                        ui.ctx().request_repaint();
                    }
                },
            );
            let size = code_size(cx.app.prefs.code_size.as_deref());
            form::set_row(ui, &t, &t!("Appearance.CodeSize"), &t!("Appearance.CodeSizeHint"), |ui| {
                ui.horizontal(|ui| {
                    let names: Vec<String> = SIZES.iter().map(|s| s.to_string()).collect();
                    if let Some(i) = form::seg(ui, &t, &names, SIZES.iter().position(|s| *s == size)) {
                        let v = format!("{}px", SIZES[i]);
                        cx.app.prefs.code_size = Some(v.clone());
                        cx.be.set_pref(PREF_CODE_SIZE, Some(&v));
                    }
                    widgets::dim(ui, &t, "px");
                });
            });
            form::set_row(ui, &t, &t!("Appearance.Preview"), &t!("Appearance.PreviewHint"), |ui| {
                Frame::new()
                    .fill(t.code_bg)
                    .stroke(Stroke::new(1.0, t.card_border))
                    .corner_radius(CornerRadius::same(theme::R_LG))
                    .inner_margin(Margin::symmetric(14, 10))
                    .show(ui, |ui| {
                        ui.set_width(ui.available_width());
                        widgets::mono_dim(ui, &t, "C#");
                        ui.add_space(4.0);
                        ui.spacing_mut().item_spacing.y = 2.0;
                        for (i, line) in sample().lines().enumerate() {
                            let c = if i == 0 { t.text_faint } else { t.text };
                            let mut r = RichText::new(line).font(theme::mono(size as f32)).color(c);
                            if i == 0 {
                                r = r.italics();
                            }
                            ui.label(r);
                        }
                    });
            });

            // ---- sessions
            form::group_title(ui, &t, &t!("Settings.Sessions"));
            let (notify, thinking, stamps) = (cx.app.flags.notify, cx.app.flags.thinking, cx.app.flags.timestamps);
            let rows = [
                (FLAG_NOTIFY, notify, "Appearance.Notify", "Appearance.NotifyHintDesktop", "Appearance.NotifyLabel"),
                (FLAG_THINKING, thinking, "Appearance.Thinking", "Appearance.ThinkingHint", "Appearance.ThinkingLabel"),
                (FLAG_TIMESTAMPS, stamps, "Appearance.Stamps", "Appearance.StampsHint", "Appearance.StampsLabel"),
            ];
            for (name, on, title, hint, label) in rows {
                form::set_row(ui, &t, &t!(title), &t!(hint), |ui| {
                    if form::switch(ui, &t, on, &t!(label), true).clicked() {
                        cx.be.set_flag(name, !on);
                        // (le push PrefChanged suit ; la copie bascule tout de suite)
                        crate::state::apply_pref(&mut cx.app.prefs, &mut cx.app.flags, name, (!on).then(String::new));
                    }
                });
            }

            // ---- à propos
            form::group_title(ui, &t, &t!("Settings.About"));
            form::set_row(ui, &t, &t!("Appearance.Version"), &tr("Livrée avec Korin, mise à jour avec lui.", "Shipped with Korin, updated with it."), |ui| {
                ui.label(RichText::new(format!("Claude Code {}", cx.be.version)).font(theme::mono(theme::CODE)).color(t.text));
            });
            form::set_row(ui, &t, &tr("Origine", "Origin"), "", |ui| {
                ui.add(
                    egui::Label::new(RichText::new(tr(
                        "L’interface et le moteur viennent de ClaudeCodeUI v0.6.1 (« Claude Code UI », © 2026 Atypical Consulting), sous licence MIT ; portés en egui pour Korin. Cette app pilote la CLI officielle claude d’Anthropic.",
                        "The interface and engine come from ClaudeCodeUI v0.6.1 (“Claude Code UI”, © 2026 Atypical Consulting), under the MIT license; ported to egui for Korin. This app drives Anthropic’s official claude CLI.",
                    )))
                    .wrap(),
                );
                ui.horizontal(|ui| {
                    if ui.link("github.com/Atypical-Consulting/ClaudeCodeUI").clicked() {
                        crate::backend::open_external("https://github.com/Atypical-Consulting/ClaudeCodeUI");
                    }
                    widgets::tag(ui, &t, "MIT");
                });
            });
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sample_has_comment() {
        let s = sample();
        assert!(s.starts_with("// ") && s.ends_with("};"));
    }

    #[test]
    fn sizes() {
        assert_eq!(code_size(Some("14px")), 14);
        assert_eq!(code_size(Some("16px")), 13);
        assert_eq!(code_size(None), 13);
    }
}
