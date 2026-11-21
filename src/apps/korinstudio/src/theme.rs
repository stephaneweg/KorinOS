// Le thème de KorinStudio : les palettes « Modern » de VS Code (sombre et claire), les polices (Inter pour
// l'interface, DejaVu Sans Mono pour le code), les couleurs de la coloration syntaxique.

use egui::{Color32, FontData, FontDefinitions, FontFamily, FontId, Stroke, TextStyle};

use crate::highlight::Tok;

#[derive(Clone)]
pub struct Palette {
    pub dark: bool,
    pub title_bg: Color32,
    pub activity_bg: Color32,
    pub activity_fg: Color32,
    pub activity_inactive: Color32,
    pub sidebar_bg: Color32,
    pub editor_bg: Color32,
    pub panel_bg: Color32,
    pub tab_bg: Color32,
    pub tab_active_bg: Color32,
    pub tab_border: Color32,
    pub border: Color32,
    pub fg: Color32,
    pub fg_dim: Color32,
    pub fg_faint: Color32,
    pub accent: Color32,
    pub accent_fg: Color32,
    pub status_bg: Color32,
    pub status_fg: Color32,
    pub hover: Color32,
    pub selection: Color32,
    pub list_active: Color32,
    pub input_bg: Color32,
    pub input_border: Color32,
    pub current_line: Color32,
    pub gutter_fg: Color32,
    pub gutter_active: Color32,
    pub error: Color32,
    pub warning: Color32,
    pub info: Color32,
    pub success: Color32,
    pub added: Color32,
    pub modified: Color32,
    pub deleted: Color32,
    pub chat_user_bg: Color32,
    pub code_bg: Color32,
    pub badge_bg: Color32,
    pub find_match: Color32,
}

const fn c(rgb: u32) -> Color32 {
    Color32::from_rgb((rgb >> 16) as u8, (rgb >> 8) as u8, rgb as u8)
}

impl Palette {
    pub fn dark() -> Self {
        Palette {
            dark: true,
            title_bg: c(0x181818),
            activity_bg: c(0x181818),
            activity_fg: c(0xD7D7D7),
            activity_inactive: c(0x868686),
            sidebar_bg: c(0x181818),
            editor_bg: c(0x1F1F1F),
            panel_bg: c(0x181818),
            tab_bg: c(0x181818),
            tab_active_bg: c(0x1F1F1F),
            tab_border: c(0x2B2B2B),
            border: c(0x2B2B2B),
            fg: c(0xCCCCCC),
            fg_dim: c(0x9D9D9D),
            fg_faint: c(0x6E7681),
            accent: c(0x0078D4),
            accent_fg: c(0xFFFFFF),
            status_bg: c(0x0078D4),
            status_fg: c(0xFFFFFF),
            hover: c(0x2A2D2E),
            selection: Color32::from_rgba_unmultiplied(0x26, 0x4F, 0x78, 0xFF),
            list_active: c(0x04395E),
            input_bg: c(0x313131),
            input_border: c(0x3C3C3C),
            current_line: c(0x282828),
            gutter_fg: c(0x6E7681),
            gutter_active: c(0xCCCCCC),
            error: c(0xF14C4C),
            warning: c(0xCCA700),
            info: c(0x3794FF),
            success: c(0x89D185),
            added: c(0x2EA043),
            modified: c(0x0078D4),
            deleted: c(0xF85149),
            chat_user_bg: c(0x2B2B2B),
            code_bg: c(0x181818),
            badge_bg: c(0x616161),
            find_match: Color32::from_rgba_unmultiplied(0x9E, 0x6A, 0x03, 0x90),
        }
    }

    pub fn light() -> Self {
        Palette {
            dark: false,
            title_bg: c(0xF8F8F8),
            activity_bg: c(0xF8F8F8),
            activity_fg: c(0x1F1F1F),
            activity_inactive: c(0x616161),
            sidebar_bg: c(0xF8F8F8),
            editor_bg: c(0xFFFFFF),
            panel_bg: c(0xF8F8F8),
            tab_bg: c(0xF8F8F8),
            tab_active_bg: c(0xFFFFFF),
            tab_border: c(0xE5E5E5),
            border: c(0xE5E5E5),
            fg: c(0x3B3B3B),
            fg_dim: c(0x616161),
            fg_faint: c(0x8B949E),
            accent: c(0x005FB8),
            accent_fg: c(0xFFFFFF),
            status_bg: c(0x005FB8),
            status_fg: c(0xFFFFFF),
            hover: c(0xF2F2F2),
            selection: c(0xADD6FF),
            list_active: c(0xE4E6F1),
            input_bg: c(0xFFFFFF),
            input_border: c(0xCECECE),
            current_line: c(0xF5F5F5),
            gutter_fg: c(0x6E7681),
            gutter_active: c(0x171184),
            error: c(0xE51400),
            warning: c(0xBF8803),
            info: c(0x1A85FF),
            success: c(0x388A34),
            added: c(0x2EA043),
            modified: c(0x005FB8),
            deleted: c(0xF85149),
            chat_user_bg: c(0xEDEDED),
            code_bg: c(0xF3F3F3),
            badge_bg: c(0xC4C4C4),
            find_match: Color32::from_rgba_unmultiplied(0xF6, 0xB9, 0x4D, 0xA0),
        }
    }

    /// La couleur d'un jeton (Dark+ et Light+ de VS Code).
    pub fn tok(&self, t: Tok) -> Color32 {
        if self.dark {
            match t {
                Tok::Text | Tok::Punct | Tok::Operator => c(0xD4D4D4),
                Tok::Keyword => c(0x569CD6),
                Tok::Type => c(0x4EC9B0),
                Tok::Function => c(0xDCDCAA),
                Tok::String | Tok::Char => c(0xCE9178),
                Tok::Number => c(0xB5CEA8),
                Tok::Comment => c(0x6A9955),
                Tok::DocComment => c(0x6A9955),
                Tok::Preproc => c(0xC586C0),
                Tok::Macro => c(0x4FC1FF),
                Tok::Attribute => c(0x9CDCFE),
                Tok::Label => c(0xDCDCAA),
                Tok::Register => c(0x9CDCFE),
                Tok::Constant => c(0x4FC1FF),
                Tok::Lifetime => c(0x569CD6),
                Tok::Escape => c(0xD7BA7D),
                Tok::Heading => c(0x569CD6),
                Tok::Link => c(0x3794FF),
                Tok::Emphasis => c(0xD4D4D4),
                Tok::Tag => c(0x569CD6),
                Tok::AttrName => c(0x9CDCFE),
            }
        } else {
            match t {
                Tok::Text | Tok::Punct | Tok::Operator => c(0x1F1F1F),
                Tok::Keyword => c(0x0000FF),
                Tok::Type => c(0x267F99),
                Tok::Function => c(0x795E26),
                Tok::String | Tok::Char => c(0xA31515),
                Tok::Number => c(0x098658),
                Tok::Comment | Tok::DocComment => c(0x008000),
                Tok::Preproc => c(0xAF00DB),
                Tok::Macro => c(0x0070C1),
                Tok::Attribute => c(0x001080),
                Tok::Label => c(0x795E26),
                Tok::Register => c(0x001080),
                Tok::Constant => c(0x0070C1),
                Tok::Lifetime => c(0x0000FF),
                Tok::Escape => c(0xEE0000),
                Tok::Heading => c(0x800000),
                Tok::Link => c(0x0451A5),
                Tok::Emphasis => c(0x1F1F1F),
                Tok::Tag => c(0x800000),
                Tok::AttrName => c(0xE50000),
            }
        }
    }
}

/// Les polices : Inter (interface), DejaVu Sans Mono (code), avec celles d'egui en secours (émojis, symboles).
pub fn install_fonts(ctx: &egui::Context) {
    let mut fonts = FontDefinitions::default();
    fonts.font_data.insert("inter".into(), FontData::from_static(include_bytes!("../../../../ref/fonts/Inter-Regular.ttf")).into());
    fonts.font_data.insert("inter-semibold".into(), FontData::from_static(include_bytes!("../../../../ref/fonts/Inter-SemiBold.ttf")).into());
    fonts.font_data.insert("dejavu-mono".into(), FontData::from_static(include_bytes!("../../../../ref/fonts/DejaVuSansMono.ttf")).into());
    fonts.families.get_mut(&FontFamily::Proportional).unwrap().insert(0, "inter".into());
    fonts.families.get_mut(&FontFamily::Monospace).unwrap().insert(0, "dejavu-mono".into());
    let mut bold = vec!["inter-semibold".to_string()];
    bold.extend(fonts.families[&FontFamily::Proportional].iter().skip(1).cloned());
    fonts.families.insert(FontFamily::Name("bold".into()), bold);
    ctx.set_fonts(fonts);
}

pub fn bold(size: f32) -> FontId {
    FontId::new(size, FontFamily::Name("bold".into()))
}

/// Applique la palette aux visuels d'egui (contrôles, champs, menus, barres de défilement).
pub fn apply(ctx: &egui::Context, p: &Palette, code_size: f32) {
    let mut style = (*ctx.global_style()).clone();
    style.text_styles = [
        (TextStyle::Small, FontId::proportional(11.0)),
        (TextStyle::Body, FontId::proportional(13.0)),
        (TextStyle::Button, FontId::proportional(13.0)),
        (TextStyle::Heading, FontId::new(15.0, FontFamily::Name("bold".into()))),
        (TextStyle::Monospace, FontId::monospace(code_size)),
    ]
    .into();
    style.spacing.item_spacing = egui::vec2(6.0, 4.0);
    style.spacing.button_padding = egui::vec2(8.0, 3.0);
    style.spacing.interact_size.y = 22.0;
    style.spacing.scroll.bar_width = 10.0;
    style.spacing.scroll.floating = true;
    style.spacing.menu_margin = egui::Margin::same(4);
    let mut v = if p.dark { egui::Visuals::dark() } else { egui::Visuals::light() };
    v.override_text_color = Some(p.fg);
    v.panel_fill = p.sidebar_bg;
    v.window_fill = if p.dark { c(0x202020) } else { c(0xFFFFFF) };
    v.window_stroke = Stroke::new(1.0, p.border);
    v.extreme_bg_color = p.input_bg;
    v.faint_bg_color = p.hover;
    v.code_bg_color = p.code_bg;
    v.hyperlink_color = p.info;
    v.selection.bg_fill = p.selection;
    v.selection.stroke = Stroke::new(1.0, p.fg);
    v.error_fg_color = p.error;
    v.warn_fg_color = p.warning;
    v.window_corner_radius = egui::CornerRadius::same(6);
    v.menu_corner_radius = egui::CornerRadius::same(6);
    v.popup_shadow = egui::Shadow { offset: [0, 4], blur: 16, spread: 0, color: Color32::from_black_alpha(if p.dark { 110 } else { 40 }) };
    v.window_shadow = v.popup_shadow;
    let r = egui::CornerRadius::same(3);
    for (w, fill, stroke) in [
        (&mut v.widgets.noninteractive, p.sidebar_bg, p.border),
        (&mut v.widgets.inactive, if p.dark { c(0x313131) } else { c(0xF3F3F3) }, p.input_border),
        (&mut v.widgets.hovered, p.hover, p.input_border),
        (&mut v.widgets.active, p.list_active, p.accent),
        (&mut v.widgets.open, p.list_active, p.input_border),
    ] {
        w.bg_fill = fill;
        w.weak_bg_fill = fill;
        w.bg_stroke = Stroke::new(1.0, stroke);
        w.corner_radius = r;
        w.fg_stroke = Stroke::new(1.0, p.fg);
    }
    v.widgets.inactive.weak_bg_fill = Color32::TRANSPARENT;
    v.widgets.inactive.bg_stroke = Stroke::NONE;
    v.widgets.noninteractive.bg_stroke = Stroke::new(1.0, p.border);
    v.widgets.noninteractive.fg_stroke = Stroke::new(1.0, p.fg);
    v.widgets.hovered.bg_stroke = Stroke::NONE;
    style.visuals = v;
    ctx.set_global_style(style);
}
