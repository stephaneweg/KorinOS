//! Les petites pièces des pages Vue d'ensemble, Arrière-plan, Worktrees et de la palette :
//! - les textes à balises des tables i18n et du moteur (`<b>`, `<code>` : « Mergé dans <b>main</b> », `Bg.Text`,
//!   `Wt.NeverForce`…), que l'UI Leptos posait en `inner_html` : ici dessinés en egui (gras, chasse fixe), le reste
//!   des balises retiré ;
//! - la couleur d'un outil (`tool_kinds::color` donnait une variable CSS) ;
//! - les lignes de tableau (`<table>` de app.css) : des colonnes de largeurs fixes, une ligne cliquable.

use egui::text::{LayoutJob, TextFormat};
use egui::{Align, Color32, CornerRadius, FontId, Layout, Rect, Response, RichText, Sense, Stroke, Ui, UiBuilder, pos2, vec2};

use crate::theme::{self, Tokens};

/// Les balises retirées, les entités décodées (le `title` d'une cellule ; `plain` de `pages/worktrees.rs`).
pub fn plain(html: &str) -> String {
    let mut out = String::with_capacity(html.len());
    let mut tag = false;
    for c in html.chars() {
        match c {
            '<' => tag = true,
            '>' if tag => tag = false,
            _ if !tag => out.push(c),
            _ => {}
        }
    }
    decode(&out)
}

fn decode(s: &str) -> String {
    let mut s = s.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&#39;", "'");
    while let Some(i) = s.find("&#x") {
        let Some(end) = s[i..].find(';') else { break };
        let ch = u32::from_str_radix(&s[i + 3..i + end], 16).ok().and_then(char::from_u32);
        let Some(ch) = ch else { break };
        s.replace_range(i..i + end + 1, &ch.to_string());
    }
    s.replace("&amp;", "&")
}

/// Les morceaux d'un texte à balises : (texte, gras, code).
pub fn spans(html: &str) -> Vec<(String, bool, bool)> {
    let mut out: Vec<(String, bool, bool)> = Vec::new();
    let (mut bold, mut code) = (0i32, 0i32);
    let mut rest = html;
    let mut text = String::new();
    let flush = |text: &mut String, out: &mut Vec<(String, bool, bool)>, b: bool, c: bool| {
        if !text.is_empty() {
            out.push((decode(text), b, c));
            text.clear();
        }
    };
    while let Some(i) = rest.find('<') {
        text.push_str(&rest[..i]);
        let Some(j) = rest[i..].find('>') else {
            text.push_str(&rest[i..]);
            rest = "";
            break;
        };
        let tag = rest[i + 1..i + j].trim().to_ascii_lowercase();
        let name = tag.split_whitespace().next().unwrap_or("");
        flush(&mut text, &mut out, bold > 0, code > 0);
        match name {
            "b" | "strong" => bold += 1,
            "/b" | "/strong" => bold = (bold - 1).max(0),
            "code" | "kbd" => code += 1,
            "/code" | "/kbd" => code = (code - 1).max(0),
            _ => {}
        }
        rest = &rest[i + j + 1..];
    }
    text.push_str(rest);
    flush(&mut text, &mut out, bold > 0, code > 0);
    out
}

/// Un texte à balises en une étiquette qui passe à la ligne : `<b>` en demi-gras à la couleur du texte, `<code>` à
/// chasse fixe.
pub fn label(ui: &mut Ui, t: &Tokens, html: &str, size: f32, color: Color32) -> Response {
    let mut job = LayoutJob::default();
    for (s, b, c) in spans(html) {
        let font = if c {
            theme::mono(size - 1.0)
        } else if b {
            theme::semibold(size)
        } else {
            FontId::proportional(size)
        };
        let fg = if b || c { t.text } else { color };
        job.append(&s, 0.0, TextFormat { font_id: font, color: fg, ..Default::default() });
    }
    job.wrap.max_width = ui.available_width();
    ui.add(egui::Label::new(job).wrap())
}

/// La couleur d'un outil (`tool_kinds::color` : lecture, recherche, modification, commande, écriture, web).
pub fn tool_color(t: &Tokens, name: &str) -> Color32 {
    match name {
        "Read" | "NotebookRead" | "Grep" | "Glob" | "WebFetch" | "WebSearch" => t.accent_ink,
        "Edit" | "MultiEdit" | "NotebookEdit" | "Write" => t.success,
        "Bash" | "PowerShell" | "BashOutput" | "KillShell" => t.warning,
        _ => t.text_dim,
    }
}

/// Les largeurs des colonnes : `fixed[i]` > 0 est une largeur en points, 0 une part de ce qui reste (à parts égales
/// entre les colonnes à 0, pondérées par `flex`).
pub fn widths(total: f32, fixed: &[f32], flex: &[f32]) -> Vec<f32> {
    let used: f32 = fixed.iter().sum();
    let parts: f32 = fixed.iter().zip(flex).filter(|(f, _)| **f <= 0.0).map(|(_, w)| *w).sum::<f32>().max(0.001);
    let left = (total - used).max(0.0);
    fixed.iter().zip(flex).map(|(f, w)| if *f > 0.0 { *f } else { left * w / parts }).collect()
}

/// Une ligne de tableau de hauteur `height` : chaque cellule reçoit son `Ui` (de gauche à droite, centré en hauteur,
/// coupé à sa largeur ; `right[i]` : alignée à droite). La ligne est cliquable (survol, `selected` en fond,
/// `mark` : un trait de l'accent à gauche).
pub fn table_row(
    ui: &mut Ui,
    t: &Tokens,
    widths: &[f32],
    right: &[bool],
    height: f32,
    selected: bool,
    mark: Option<Color32>,
    mut cell: impl FnMut(usize, &mut Ui),
) -> Response {
    let (rect, _) = ui.allocate_exact_size(vec2(ui.available_width(), height), Sense::hover());
    let bg = ui.painter().add(egui::Shape::Noop);
    let mut x = rect.left();
    for (i, w) in widths.iter().enumerate() {
        let r = Rect::from_min_size(pos2(x, rect.top()), vec2(*w, height)).shrink2(vec2(10.0, 4.0));
        let layout = if right.get(i).copied().unwrap_or(false) { Layout::right_to_left(Align::Center) } else { Layout::left_to_right(Align::Center) };
        let mut child = ui.new_child(UiBuilder::new().id_salt(("cell", i)).max_rect(r).layout(layout));
        child.set_clip_rect(r.intersect(ui.clip_rect()));
        cell(i, &mut child);
        x += w;
    }
    let resp = ui.interact(rect, ui.scope_id().with(("row", rect.top() as i32)), Sense::click());
    let fill = if selected {
        t.hover
    } else if resp.hovered() {
        t.hover.gamma_multiply(0.6)
    } else {
        Color32::TRANSPARENT
    };
    ui.painter().set(bg, egui::Shape::rect_filled(rect, CornerRadius::ZERO, fill));
    if let Some(c) = mark {
        ui.painter().vline(rect.left() + 1.0, rect.y_range(), Stroke::new(2.0, c));
    }
    ui.painter().hline(rect.x_range(), rect.bottom(), Stroke::new(1.0, t.separator));
    resp
}

/// L'en-tête d'un tableau : les titres des colonnes en petites capitales.
pub fn table_head(ui: &mut Ui, t: &Tokens, widths: &[f32], right: &[bool], titles: &[String]) {
    let (rect, _) = ui.allocate_exact_size(vec2(ui.available_width(), 30.0), Sense::hover());
    let mut x = rect.left();
    for (i, w) in widths.iter().enumerate() {
        let r = Rect::from_min_size(pos2(x, rect.top()), vec2(*w, 30.0)).shrink2(vec2(10.0, 0.0));
        let layout = if right.get(i).copied().unwrap_or(false) { Layout::right_to_left(Align::Center) } else { Layout::left_to_right(Align::Center) };
        let mut child = ui.new_child(UiBuilder::new().id_salt(("head", i)).max_rect(r).layout(layout));
        if let Some(s) = titles.get(i) {
            child.add(egui::Label::new(RichText::new(s.to_uppercase()).font(theme::semibold(theme::CAPTION - 1.0)).color(t.text_faint).extra_letter_spacing(0.6)).truncate());
        }
        x += w;
    }
    ui.painter().hline(rect.x_range(), rect.bottom(), Stroke::new(1.0, t.separator));
}

/// Un texte d'une ligne coupé par « … » à la largeur qui reste.
pub fn one_line(ui: &mut Ui, text: RichText) -> Response {
    ui.add(egui::Label::new(text).truncate())
}

/// Un compteur de quota : une barre fine remplie à `f` (0…1), de la couleur donnée.
pub fn meter(ui: &mut Ui, t: &Tokens, width: f32, f: f64, color: Color32) {
    let (r, _) = ui.allocate_exact_size(vec2(width, 4.0), Sense::hover());
    ui.painter().rect_filled(r, 2.0, t.hover);
    let mut fill = r;
    fill.set_width(r.width() * f.clamp(0.0, 1.0) as f32);
    ui.painter().rect_filled(fill, 2.0, color);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plain_and_spans() {
        assert_eq!(plain("<b>merged</b> &amp; gone &#x2014;"), "merged & gone —");
        let s = spans("Mergé dans <b>main</b>, <code>git &lt;x&gt;</code>");
        assert_eq!(
            s,
            vec![
                ("Mergé dans ".to_string(), false, false),
                ("main".to_string(), true, false),
                (", ".to_string(), false, false),
                ("git <x>".to_string(), false, true),
            ]
        );
        assert_eq!(spans("sans balise"), vec![("sans balise".to_string(), false, false)]);
    }
}
