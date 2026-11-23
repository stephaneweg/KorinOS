//! Les petites pièces communes de l'interface, aux couleurs du thème de Korin (theme.rs) : ce que `app.css` et
//! `ui/src/common` de ClaudeCodeUI donnaient (icônes, touches, pastilles, boutons, en-têtes de section, diffs,
//! durées). Les écrans les prennent ici plutôt que de refaire leurs couleurs.

use egui::{Align, Color32, CornerRadius, FontId, Frame, Layout, Margin, Response, RichText, Sense, Stroke, Ui, Vec2};

use crate::theme::{self, Tokens};

/// Les icônes de ClaudeCodeUI (sprite SVG) en glyphes des polices de l'app (Figtree, JetBrains Mono, les polices
/// d'egui : symboles et émojis monochromes) ; seulement des glyphes vérifiés en image (beaucoup manquent : ▦ ⎇ ⧉ ➤ ↶ ✎ ⑂ ◐ ⨯).
/// Un nom inconnu donne « • ».
pub fn icon(name: &str) -> &'static str {
    match name {
        "plus" => "+",
        "search" => "🔍",
        "grid" => "⊞",
        "branch" => "⌥",
        "agents" => "◎",
        "plug" => "🔌",
        "palette" | "settings" => "⚙",
        "x" | "close" => "✕",
        "refresh" => "⟳",
        "check" => "✔",
        "stop" => "■",
        "play" => "▶",
        "chev-right" => "▸",
        "chev-down" => "▾",
        "chev-up" => "▴",
        "folder" => "🗀",
        "file" => "🗋",
        "copy" => "📋",
        "bookmark" => "🔖",
        "warn" => "⚠",
        "info" => "ℹ",
        "clock" => "⏱",
        "send" => "⏎",
        "dots" => "⋯",
        "trash" => "🗑",
        "pin" => "📌",
        "link" => "🔗",
        "eye" => "👁",
        "terminal" => "⌘",
        "rewind" => "⌫",
        "fork" => "🔀",
        "paperclip" | "attach" => "📎",
        "edit" => "🖹",
        "dot" => "●",
        "ring" => "○",
        "arrow-right" => "→",
        "arrow-up" => "↑",
        "arrow-down" => "↓",
        _ => "•",
    }
}

/// Une touche (`<kbd>`), en petit et à chasse fixe, dans un cadre.
pub fn kbd(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    Frame::new()
        .fill(t.raised)
        .stroke(Stroke::new(1.0, t.card_border))
        .corner_radius(CornerRadius::same(4))
        .inner_margin(Margin::symmetric(5, 1))
        .show(ui, |ui| ui.label(RichText::new(text).font(theme::mono(theme::CAPTION - 1.0)).color(t.text_dim)))
        .response
}

/// Le genre d'une pastille (`.pill`, `.tag` de app.css).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tone {
    Neutral,
    Accent,
    /// en attente d'une décision
    Wait,
    Ok,
    Err,
    Warn,
}

pub fn tone_color(t: &Tokens, tone: Tone) -> Color32 {
    match tone {
        Tone::Neutral => t.text_dim,
        Tone::Accent => t.accent_ink,
        Tone::Wait => t.warning,
        Tone::Ok => t.success,
        Tone::Err => t.danger,
        Tone::Warn => t.warning,
    }
}

/// Une pastille arrondie (compteur, état).
pub fn pill(ui: &mut Ui, t: &Tokens, text: &str, tone: Tone) -> Response {
    let c = tone_color(t, tone);
    Frame::new()
        .fill(c.gamma_multiply(0.18))
        .corner_radius(CornerRadius::same(8))
        .inner_margin(Margin::symmetric(6, 1))
        .show(ui, |ui| ui.label(RichText::new(text).font(theme::medium(theme::CAPTION)).color(c)))
        .response
}

/// Une étiquette à chasse fixe encadrée (`.tag` : mode, branche, « 4 à nettoyer »).
pub fn tag(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    Frame::new()
        .stroke(Stroke::new(1.0, t.card_border))
        .corner_radius(CornerRadius::same(4))
        .inner_margin(Margin::symmetric(5, 1))
        .show(ui, |ui| ui.label(RichText::new(text).font(theme::mono(theme::CAPTION)).color(t.text_dim)))
        .response
}

/// Un point d'état (session en cours, en attente, arrêtée…).
pub fn dot(ui: &mut Ui, color: Color32, filled: bool) -> Response {
    let (rect, r) = ui.allocate_exact_size(Vec2::splat(10.0), Sense::hover());
    if filled {
        ui.painter().circle_filled(rect.center(), 3.5, color);
    } else {
        ui.painter().circle_stroke(rect.center(), 3.0, Stroke::new(1.2, color));
    }
    r
}

/// L'en-tête d'une section en petites capitales (`.cap` : « ACTIVES », « RÉCENTES », « FILE DES DÉCISIONS »).
pub fn cap(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    ui.label(RichText::new(text.to_uppercase()).font(theme::semibold(theme::CAPTION - 1.0)).color(t.text_faint).extra_letter_spacing(0.6))
}

/// Un texte secondaire (petit, atténué).
pub fn dim(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    ui.label(RichText::new(text).font(FontId::proportional(theme::CAPTION)).color(t.text_dim))
}

/// Un texte à chasse fixe atténué (chemins, branches, durées).
pub fn mono_dim(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    ui.label(RichText::new(text).font(theme::mono(theme::CAPTION)).color(t.text_faint))
}

/// Le bouton principal (`.btn.primary`) : fond de l'accent.
pub fn primary(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    ui.add(egui::Button::new(RichText::new(text).color(t.on_accent).font(theme::medium(theme::SMALL))).fill(t.accent).stroke(Stroke::NONE))
}

/// Un bouton ordinaire (`.btn`).
pub fn button(ui: &mut Ui, text: &str) -> Response {
    ui.button(text)
}

/// Un bouton sans cadre (`.btn.ghost`).
pub fn ghost(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    ui.add(egui::Button::new(RichText::new(text).color(t.text_dim)).frame(false))
}

/// Un bouton de danger (Refuser, Supprimer).
pub fn danger(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    ui.add(egui::Button::new(RichText::new(text).color(t.danger)).stroke(Stroke::new(1.0, t.danger.gamma_multiply(0.6))))
}

/// Une ligne du rail ou d'une liste : toute la largeur, survol et sélection en fond ; `add` dessine son contenu.
pub fn row(ui: &mut Ui, t: &Tokens, selected: bool, add: impl FnOnce(&mut Ui)) -> Response {
    let fill = if selected { t.hover } else { Color32::TRANSPARENT };
    let inner = Frame::new().fill(fill).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::symmetric(8, 5)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        add(ui);
    });
    let r = ui.interact(inner.response.rect, inner.response.id.with("row"), Sense::click());
    if r.hovered() && !selected {
        ui.painter().rect_filled(inner.response.rect, CornerRadius::same(theme::R), t.hover.gamma_multiply(0.6));
    }
    r
}

/// Une carte (`.card`) : fond, bordure, arrondi, marges.
pub fn card<R>(ui: &mut Ui, t: &Tokens, add: impl FnOnce(&mut Ui) -> R) -> egui::InnerResponse<R> {
    Frame::new()
        .fill(t.card)
        .stroke(Stroke::new(1.0, t.card_border))
        .corner_radius(CornerRadius::same(theme::R_LG))
        .inner_margin(Margin::same(12))
        .show(ui, add)
}

/// L'en-tête d'un panneau (`.ph` : titre à gauche, actions à droite), hauteur fixe et trait dessous.
pub fn pane_header(ui: &mut Ui, t: &Tokens, left: impl FnOnce(&mut Ui), right: impl FnOnce(&mut Ui)) {
    Frame::new().inner_margin(Margin::symmetric(16, 10)).show(ui, |ui| {
        ui.set_min_height(26.0);
        ui.horizontal(|ui| {
            left(ui);
            ui.with_layout(Layout::right_to_left(Align::Center), right);
        });
    });
    let r = ui.max_rect();
    let y = ui.cursor().top();
    ui.painter().hline(r.x_range(), y, Stroke::new(1.0, t.separator));
}

/// Une valeur et sa légende sous elle (les chiffres de la vue d'ensemble : « 2 en attente »).
pub fn stat(ui: &mut Ui, t: &Tokens, value: &str, label: &str, color: Color32) {
    ui.vertical(|ui| {
        ui.label(RichText::new(value).font(theme::semibold(theme::SECTION + 3.0)).color(color));
        ui.label(RichText::new(label).font(FontId::proportional(theme::CAPTION)).color(t.text_dim));
    });
}

/// Une ligne de diff (`ccui_shared::edit_diff::DiffLine`) : ajoutée en vert, retirée en rouge, numéros à gauche.
pub fn diff_lines(ui: &mut Ui, t: &Tokens, lines: &[ccui_shared::edit_diff::DiffLine]) {
    use ccui_shared::edit_diff::DiffKind;
    Frame::new().fill(t.code_bg).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::same(6)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        for l in lines {
            let (sign, fg, bg) = match l.kind {
                DiffKind::Add => ("+", t.success, t.success.gamma_multiply(0.12)),
                DiffKind::Del => ("-", t.danger, t.danger.gamma_multiply(0.12)),
                DiffKind::Ctx => (" ", t.text_dim, Color32::TRANSPARENT),
                DiffKind::Hunk | DiffKind::More => ("", t.accent_ink, t.accent_soft),
            };
            Frame::new().fill(bg).show(ui, |ui| {
                ui.set_width(ui.available_width());
                ui.horizontal(|ui| {
                    let n = l.n.map(|n| n.to_string()).unwrap_or_default();
                    ui.add_sized([34.0, 16.0], egui::Label::new(RichText::new(n).font(theme::mono(theme::CAPTION - 1.0)).color(t.text_faint)));
                    ui.add(egui::Label::new(RichText::new(format!("{sign} {}", l.text)).font(theme::mono(theme::CODE)).color(fg)).wrap());
                });
            });
        }
    });
}

/// Un message d'erreur en ligne (`.err`).
pub fn error_text(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    ui.label(RichText::new(format!("{} {text}", icon("warn"))).color(t.danger))
}

/// Une durée courte (« 12,8 s », « 4 min ») par ccui_shared::fmt.
pub fn duration_ms(ms: i64) -> String {
    ccui_shared::fmt::dur(ms)
}

/// Un champ de saisie d'une ligne à la largeur donnée (avec une indication).
pub fn field(ui: &mut Ui, text: &mut String, hint: &str, width: f32) -> Response {
    ui.add(egui::TextEdit::singleline(text).hint_text(hint).desired_width(width))
}

/// Le texte à la couleur atténuée qui dit « rien ici » (`.empty`).
pub fn empty(ui: &mut Ui, t: &Tokens, text: &str) -> Response {
    ui.label(RichText::new(text).color(t.text_faint).italics())
}

/// Un texte propre à l'app de Korin (pas dans les tables de ClaudeCodeUI, `ccui_shared::t!`) : le français ou
/// l'anglais selon la langue de l'interface.
pub fn tr(fr: &str, en: &str) -> String {
    match ccui_shared::i18n::lang() {
        ccui_shared::i18n::Lang::Fr => fr.to_string(),
        ccui_shared::i18n::Lang::En => en.to_string(),
    }
}
