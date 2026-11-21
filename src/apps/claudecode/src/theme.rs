//! L'apparence des apps Craft de storytold (Craft Anything : « Studio » sombre et « Studio Light ») : surfaces
//! graphite ou papier, barre latérale un ton plus sombre, cartes à peine arrondies, Figtree pour le texte et
//! JetBrains Mono pour le code (polices OFL, assets/fonts), le bleu de getartcraft.com pour l'accent. Le thème suit
//! celui de Korin (clair ou sombre), sauf choix dans le menu.

use std::sync::Arc;

use egui::epaint::text::VariationCoords;
use egui::{Color32, CornerRadius, FontData, FontDefinitions, FontFamily, FontId, FontTweak, Margin, Stroke, TextStyle, Visuals};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Tokens {
    pub dark: bool,
    pub chrome: Color32,
    pub canvas: Color32,
    pub card: Color32,
    pub card_border: Color32,
    pub raised: Color32,
    pub field: Color32,
    pub field_border: Color32,
    pub hover: Color32,
    pub pressed: Color32,
    pub text: Color32,
    pub text_dim: Color32,
    pub text_faint: Color32,
    pub accent: Color32,
    pub accent_hover: Color32,
    pub accent_ink: Color32,
    pub accent_soft: Color32,
    pub on_accent: Color32,
    pub separator: Color32,
    pub danger: Color32,
    pub warning: Color32,
    pub success: Color32,
    pub code_bg: Color32,
    /// le fond des messages de l'utilisateur
    pub user_bubble: Color32,
}

/// L'accent de getartcraft.com (#1659c4).
pub const BRAND: Color32 = Color32::from_rgb(0x16, 0x59, 0xc4);

impl Tokens {
    pub fn studio() -> Tokens {
        Tokens {
            dark: true,
            chrome: Color32::from_rgb(0x10, 0x10, 0x14),
            canvas: Color32::from_rgb(0x12, 0x13, 0x16),
            card: Color32::from_rgb(0x1b, 0x1c, 0x1f),
            card_border: Color32::from_rgb(0x34, 0x34, 0x36),
            raised: Color32::from_rgb(0x22, 0x23, 0x25),
            field: Color32::from_rgb(0x22, 0x23, 0x26),
            field_border: Color32::from_rgb(0x6c, 0x6c, 0x6c),
            hover: Color32::from_rgb(0x2a, 0x2b, 0x2e),
            pressed: Color32::from_rgb(0x33, 0x34, 0x37),
            text: Color32::from_rgb(0xf2, 0xf1, 0xee),
            text_dim: Color32::from_rgb(0xb3, 0xb3, 0xb2),
            text_faint: Color32::from_rgb(0x8d, 0x8d, 0x8d),
            accent: BRAND,
            accent_hover: Color32::from_rgb(0x2d, 0x81, 0xff),
            accent_ink: Color32::from_rgb(0x74, 0xaa, 0xff),
            accent_soft: Color32::from_rgba_unmultiplied(0x2d, 0x81, 0xff, 44),
            on_accent: Color32::WHITE,
            separator: Color32::from_rgb(0x24, 0x25, 0x27),
            danger: Color32::from_rgb(0xff, 0x62, 0x59),
            warning: Color32::from_rgb(0xe0, 0xa2, 0x3a),
            success: Color32::from_rgb(0x5b, 0xbf, 0x8f),
            code_bg: Color32::from_rgb(0x06, 0x06, 0x07),
            user_bubble: Color32::from_rgb(0x26, 0x2a, 0x33),
        }
    }

    pub fn studio_light() -> Tokens {
        Tokens {
            dark: false,
            chrome: Color32::from_rgb(0xe9, 0xe8, 0xe4),
            canvas: Color32::from_rgb(0xf2, 0xf1, 0xee),
            card: Color32::from_rgb(0xfa, 0xf9, 0xf7),
            card_border: Color32::from_rgb(0xce, 0xcd, 0xcb),
            raised: Color32::from_rgb(0xe9, 0xe8, 0xe4),
            field: Color32::WHITE,
            field_border: Color32::from_rgb(0x86, 0x85, 0x85),
            hover: Color32::from_rgb(0xe4, 0xe4, 0xe1),
            pressed: Color32::from_rgb(0xdb, 0xda, 0xd8),
            text: Color32::from_rgb(0x10, 0x10, 0x14),
            text_dim: Color32::from_rgb(0x3d, 0x3d, 0x40),
            text_faint: Color32::from_rgb(0x5d, 0x5c, 0x5e),
            accent: BRAND,
            accent_hover: Color32::from_rgb(0x2d, 0x81, 0xff),
            accent_ink: BRAND,
            accent_soft: Color32::from_rgba_unmultiplied(0x2d, 0x81, 0xff, 28),
            on_accent: Color32::WHITE,
            separator: Color32::from_rgb(0xdb, 0xda, 0xd8),
            danger: Color32::from_rgb(0xb3, 0x26, 0x1e),
            warning: Color32::from_rgb(0x9a, 0x67, 0x00),
            success: Color32::from_rgb(0x12, 0x75, 0x36),
            code_bg: Color32::from_rgb(0xe9, 0xe8, 0xe4),
            user_bubble: Color32::from_rgb(0xe3, 0xe8, 0xf2),
        }
    }

    pub fn for_dark(dark: bool) -> Tokens {
        if dark { Tokens::studio() } else { Tokens::studio_light() }
    }
}

/// Les arrondis : contrôles, cartes.
pub const R: u8 = 6;
pub const R_LG: u8 = 8;

/// L'échelle des tailles de texte (celle de Craft Anything).
pub const TITLE: f32 = 22.0;
pub const SECTION: f32 = 17.0;
pub const BODY: f32 = 15.0;
pub const SMALL: f32 = 14.0;
pub const LABEL: f32 = 13.0;
pub const CAPTION: f32 = 12.0;
pub const CODE: f32 = 13.0;

pub fn medium(size: f32) -> FontId {
    FontId::new(size, FontFamily::Name("medium".into()))
}
pub fn semibold(size: f32) -> FontId {
    FontId::new(size, FontFamily::Name("semibold".into()))
}
pub fn mono(size: f32) -> FontId {
    FontId::new(size, FontFamily::Monospace)
}

static FIGTREE: &[u8] = include_bytes!("../assets/fonts/Figtree-Variable.ttf");
static JETBRAINS_MONO: &[u8] = include_bytes!("../assets/fonts/JetBrainsMono-Regular.ttf");

/// Figtree (trois graisses tirées de la police variable : texte, moyen, demi-gras), JetBrains Mono pour le code ;
/// les polices d'egui derrière (émojis, symboles).
pub fn install_fonts(ctx: &egui::Context) {
    let mut fonts = FontDefinitions::default();
    for (name, weight) in [("Figtree", 400.0), ("Figtree-Medium", 500.0), ("Figtree-SemiBold", 620.0)] {
        let tweak = FontTweak { coords: VariationCoords::new([(b"wght", weight)]), ..Default::default() };
        fonts.font_data.insert(name.to_owned(), Arc::new(FontData::from_static(FIGTREE).tweak(tweak)));
    }
    fonts.font_data.insert("JetBrainsMono".to_owned(), Arc::new(FontData::from_static(JETBRAINS_MONO)));
    fonts.families.entry(FontFamily::Proportional).or_default().insert(0, "Figtree".to_owned());
    fonts.families.entry(FontFamily::Monospace).or_default().insert(0, "JetBrainsMono".to_owned());
    // (les symboles que Figtree et les polices d'egui n'ont pas, ▸ ▾ ● ⋯ ✕ : ceux de JetBrains Mono)
    fonts.families.entry(FontFamily::Proportional).or_default().push("JetBrainsMono".to_owned());
    let base = fonts.families.get(&FontFamily::Proportional).cloned().unwrap_or_default();
    for (fam, first) in [("medium", "Figtree-Medium"), ("semibold", "Figtree-SemiBold")] {
        let mut stack = vec![first.to_owned()];
        stack.extend(base.iter().cloned());
        fonts.families.insert(FontFamily::Name(fam.into()), stack);
    }
    ctx.set_fonts(fonts);
}

/// Le style d'egui aux couleurs du thème.
pub fn apply(ctx: &egui::Context, t: &Tokens) {
    let mut v = if t.dark { Visuals::dark() } else { Visuals::light() };
    v.panel_fill = t.canvas;
    v.window_fill = t.card;
    v.window_stroke = Stroke::new(1.0, t.card_border);
    v.window_corner_radius = CornerRadius::same(R_LG);
    v.menu_corner_radius = CornerRadius::same(R);
    v.extreme_bg_color = t.field;
    v.faint_bg_color = t.raised;
    v.code_bg_color = t.code_bg;
    v.hyperlink_color = t.accent_ink;
    v.selection.bg_fill = t.accent_soft.gamma_multiply(2.0);
    v.selection.stroke = Stroke::new(1.0, t.accent_ink);
    v.override_text_color = Some(t.text);
    v.error_fg_color = t.danger;
    v.warn_fg_color = t.warning;
    for (w, fill, stroke) in [
        (&mut v.widgets.noninteractive, t.card, t.separator),
        (&mut v.widgets.inactive, t.raised, t.card_border),
        (&mut v.widgets.hovered, t.hover, t.field_border),
        (&mut v.widgets.active, t.pressed, t.accent),
        (&mut v.widgets.open, t.hover, t.card_border),
    ] {
        w.bg_fill = fill;
        w.weak_bg_fill = fill;
        w.bg_stroke = Stroke::new(1.0, stroke);
        w.corner_radius = CornerRadius::same(R);
        w.fg_stroke.color = t.text;
    }
    v.widgets.noninteractive.bg_stroke = Stroke::new(1.0, t.separator);
    v.widgets.noninteractive.fg_stroke.color = t.text;
    // (le thème d'egui suit le nôtre : l'hôte de Korin le change quand le système change, on le refait ensuite)
    let theme = if t.dark { egui::Theme::Dark } else { egui::Theme::Light };
    ctx.set_theme(theme);
    ctx.set_visuals_of(theme, v);
    ctx.all_styles_mut(|s| {
        s.text_styles = [
            (TextStyle::Heading, FontId::new(TITLE, FontFamily::Name("semibold".into()))),
            (TextStyle::Body, FontId::proportional(BODY)),
            (TextStyle::Button, FontId::new(SMALL, FontFamily::Name("medium".into()))),
            (TextStyle::Small, FontId::proportional(CAPTION)),
            (TextStyle::Monospace, FontId::monospace(CODE)),
        ]
        .into();
        s.spacing.item_spacing = egui::vec2(8.0, 6.0);
        s.spacing.button_padding = egui::vec2(10.0, 5.0);
        s.spacing.menu_margin = Margin::same(4);
        s.spacing.scroll.bar_width = 8.0;
        s.interaction.selectable_labels = true;
    });
}
