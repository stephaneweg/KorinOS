//! Le Markdown des réponses de Claude, assez pour les lire : titres, paragraphes, listes, citations, blocs de code
//! (police à chasse fixe, fond, bouton Copier), et dans le texte `code`, **gras**, *italique*, [liens](…). Chaque
//! paragraphe est un label sélectionnable : le texte se sélectionne à la souris et se copie (Ctrl+C).

use egui::text::{LayoutJob, TextFormat};
use egui::{Color32, FontId, Frame, Label, Margin, RichText, Stroke, Ui};

use crate::theme::{self, Tokens};

/// Un morceau du texte : un bloc de code, ou des lignes ordinaires.
enum Block<'a> {
    Code { lang: &'a str, text: String },
    Lines(Vec<&'a str>),
}

fn blocks(src: &str) -> Vec<Block<'_>> {
    let mut out = Vec::new();
    let mut lines = Vec::new();
    let mut it = src.lines();
    while let Some(l) = it.next() {
        let t = l.trim_start();
        if let Some(rest) = t.strip_prefix("```") {
            if !lines.is_empty() {
                out.push(Block::Lines(std::mem::take(&mut lines)));
            }
            let lang = rest.trim();
            let mut code = String::new();
            for c in it.by_ref() {
                if c.trim_start().starts_with("```") {
                    break;
                }
                code.push_str(c);
                code.push('\n');
            }
            if code.ends_with('\n') {
                code.pop();
            }
            out.push(Block::Code { lang, text: code });
        } else {
            lines.push(l);
        }
    }
    if !lines.is_empty() {
        out.push(Block::Lines(lines));
    }
    out
}

/// Dessine le texte Markdown `src` (un bloc de code a son bouton Copier).
pub fn show(ui: &mut Ui, t: &Tokens, src: &str) {
    for b in blocks(src) {
        match b {
            Block::Code { lang, text } => code_block(ui, t, lang, &text),
            Block::Lines(lines) => paragraphs(ui, t, &lines),
        }
    }
}

pub fn code_block(ui: &mut Ui, t: &Tokens, lang: &str, text: &str) {
    Frame::new()
        .fill(t.code_bg)
        .stroke(Stroke::new(1.0, t.card_border))
        .corner_radius(theme::R)
        .inner_margin(Margin::same(10))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.horizontal(|ui| {
                ui.label(RichText::new(if lang.is_empty() { "code" } else { lang }).font(theme::medium(theme::CAPTION)).color(t.text_faint));
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.small_button("Copier").on_hover_text("Copier ce bloc").clicked() {
                        ui.ctx().copy_text(text.to_owned());
                    }
                });
            });
            ui.add(Label::new(RichText::new(text).font(theme::mono(theme::CODE)).color(t.text)).selectable(true).wrap());
        });
}

fn paragraphs(ui: &mut Ui, t: &Tokens, lines: &[&str]) {
    let mut para: Vec<&str> = Vec::new();
    let flush = |ui: &mut Ui, para: &mut Vec<&str>| {
        if !para.is_empty() {
            let text = para.join("\n");
            inline_label(ui, t, &text, FontId::proportional(theme::BODY), t.text);
            para.clear();
        }
    };
    for &l in lines {
        let s = l.trim_end();
        let st = s.trim_start();
        if st.is_empty() {
            flush(ui, &mut para);
            ui.add_space(4.0);
            continue;
        }
        let heading = st.chars().take_while(|&c| c == '#').count();
        if (1..=6).contains(&heading) && st[heading..].starts_with(' ') {
            flush(ui, &mut para);
            let size = match heading {
                1 => theme::TITLE - 2.0,
                2 => theme::SECTION + 1.0,
                _ => theme::SECTION - 1.0,
            };
            ui.add_space(4.0);
            inline_label(ui, t, st[heading..].trim(), theme::semibold(size), t.text);
            continue;
        }
        if st == "---" || st == "***" || st == "___" {
            flush(ui, &mut para);
            ui.separator();
            continue;
        }
        let indent = (s.len() - st.len()) as f32 * 6.0;
        let bullet = st.strip_prefix("- ").or_else(|| st.strip_prefix("* ")).or_else(|| st.strip_prefix("+ "));
        let numbered = {
            let n = st.chars().take_while(|c| c.is_ascii_digit()).count();
            (n > 0 && st[n..].starts_with(". ")).then(|| (&st[..n], &st[n + 2..]))
        };
        if let Some(item) = bullet {
            flush(ui, &mut para);
            list_item(ui, t, indent, "•", item);
        } else if let Some((n, item)) = numbered {
            flush(ui, &mut para);
            list_item(ui, t, indent, &format!("{n}."), item);
        } else if let Some(q) = st.strip_prefix('>') {
            flush(ui, &mut para);
            Frame::new().stroke(Stroke::NONE).inner_margin(Margin { left: 10, right: 0, top: 0, bottom: 0 }).show(ui, |ui| {
                let r = ui.max_rect();
                ui.painter().vline(r.left() + 2.0, r.y_range(), Stroke::new(2.0, t.card_border));
                inline_label(ui, t, q.trim_start(), FontId::proportional(theme::BODY), t.text_dim);
            });
        } else if st.starts_with('|') {
            // (un tableau : en chasse fixe, tel quel)
            flush(ui, &mut para);
            ui.add(Label::new(RichText::new(st).font(theme::mono(theme::CODE)).color(t.text)).selectable(true));
        } else {
            para.push(s);
        }
    }
    flush(ui, &mut para);
}

fn list_item(ui: &mut Ui, t: &Tokens, indent: f32, mark: &str, text: &str) {
    ui.horizontal_top(|ui| {
        ui.add_space(4.0 + indent);
        ui.label(RichText::new(mark).color(t.text_faint));
        ui.vertical(|ui| inline_label(ui, t, text, FontId::proportional(theme::BODY), t.text));
    });
}

/// Une ligne ou un paragraphe, avec `code`, **gras**, *italique* et [liens](url) : un seul label sélectionnable.
pub fn inline_label(ui: &mut Ui, t: &Tokens, text: &str, font: FontId, color: Color32) {
    let mut job = LayoutJob::default();
    job.wrap.max_width = ui.available_width();
    let plain = TextFormat { font_id: font.clone(), color, ..Default::default() };
    let bold = TextFormat { font_id: FontId::new(font.size, egui::FontFamily::Name("semibold".into())), color, ..Default::default() };
    let italic = TextFormat { italics: true, ..plain.clone() };
    let code = TextFormat {
        font_id: theme::mono(font.size - 1.5),
        color: t.text,
        background: t.code_bg,
        ..Default::default()
    };
    let link = TextFormat { color: t.accent_ink, underline: Stroke::new(1.0, t.accent_ink), ..plain.clone() };
    let mut rest = text;
    let mut buf = String::new();
    let push = |job: &mut LayoutJob, s: &str, f: &TextFormat| {
        if !s.is_empty() {
            job.append(s, 0.0, f.clone());
        }
    };
    while !rest.is_empty() {
        if let Some(r) = rest.strip_prefix('`')
            && let Some(end) = r.find('`')
        {
            push(&mut job, &buf, &plain);
            buf.clear();
            push(&mut job, &r[..end], &code);
            rest = &r[end + 1..];
            continue;
        }
        if let Some(r) = rest.strip_prefix("**")
            && let Some(end) = r.find("**")
            && end > 0
        {
            push(&mut job, &buf, &plain);
            buf.clear();
            push(&mut job, &r[..end], &bold);
            rest = &r[end + 2..];
            continue;
        }
        if let Some(r) = rest.strip_prefix('*')
            && !r.starts_with(' ')
            && let Some(end) = r.find('*')
            && end > 0
        {
            push(&mut job, &buf, &plain);
            buf.clear();
            push(&mut job, &r[..end], &italic);
            rest = &r[end + 1..];
            continue;
        }
        if let Some(r) = rest.strip_prefix('[')
            && let Some(close) = r.find("](")
            && let Some(end) = r[close + 2..].find(')')
        {
            push(&mut job, &buf, &plain);
            buf.clear();
            push(&mut job, &r[..close], &link);
            rest = &r[close + 2 + end + 1..];
            continue;
        }
        let c = rest.chars().next().unwrap_or(' ');
        buf.push(c);
        rest = &rest[c.len_utf8()..];
    }
    push(&mut job, &buf, &plain);
    ui.add(Label::new(job).selectable(true).wrap());
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splits_code_blocks() {
        let b = blocks("Avant\n```rust\nfn main() {}\n```\nAprès");
        assert_eq!(b.len(), 3);
        match &b[1] {
            Block::Code { lang, text } => {
                assert_eq!(*lang, "rust");
                assert_eq!(text, "fn main() {}");
            }
            _ => panic!("pas un bloc de code"),
        }
    }
}
