//! Le Markdown des réponses de Claude (ce que `ccui_shared::md` rend en HTML pour ClaudeCodeUI), dessiné en egui :
//! titres (h1 et h2 soulignés), paragraphes, listes (à puces, numérotées, cases à cocher, imbriquées), citations et
//! alertes GitHub (`> [!NOTE]`), tableaux (colonnes mesurées, en-tête, traits entre les lignes), règles, blocs de code
//! (en-tête avec le langage et Copier, coloration simple : mots-clés, chaînes, commentaires, nombres), et dans le
//! texte `code`, **gras**, *italique*, ~~barré~~, [liens](…) et adresses nues (un clic les ouvre). Chaque paragraphe,
//! cellule ou bloc est un label sélectionnable : le texte se sélectionne à la souris et se copie (Ctrl+C).

use std::ops::Range;
use std::sync::Arc;

use ccui_shared::t;
use egui::text::{LayoutJob, TextFormat};
use egui::{Align, Color32, CornerRadius, FontFamily, FontId, Frame, Galley, Label, Layout, Margin, Pos2, Rect, RichText, Sense, Stroke, Ui, Vec2};

use crate::theme::{self, Tokens};
use crate::widgets::icon;

// ------------------------------------------------------------------------------------------- les blocs

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum ColAlign {
    Left,
    Center,
    Right,
}

#[derive(Debug, PartialEq)]
pub(crate) struct ListItem {
    /// La profondeur (0 : en tête).
    depth: usize,
    /// « • » ou « 3. ».
    mark: String,
    /// Une case à cocher : Some(cochée).
    task: Option<bool>,
    text: String,
}

#[derive(Debug, PartialEq)]
pub(crate) enum Block {
    Code { lang: String, text: String },
    Heading(usize, String),
    Para(String),
    Rule,
    List(Vec<ListItem>),
    /// Une citation (son contenu en Markdown) ; Some : une alerte GitHub (NOTE, TIP, IMPORTANT, WARNING, CAUTION).
    Quote { alert: Option<String>, body: String },
    Table { head: Vec<String>, align: Vec<ColAlign>, rows: Vec<Vec<String>> },
}

fn fence(st: &str) -> Option<&str> {
    st.strip_prefix("```").or_else(|| st.strip_prefix("~~~"))
}

fn heading(st: &str) -> Option<(usize, &str)> {
    let n = st.chars().take_while(|&c| c == '#').count();
    ((1..=6).contains(&n) && (st.len() == n || st[n..].starts_with(' '))).then(|| (n, st[n..].trim().trim_end_matches('#').trim_end()))
}

fn is_rule(st: &str) -> bool {
    let s: String = st.chars().filter(|c| !c.is_whitespace()).collect();
    s.len() >= 3 && (s.chars().all(|c| c == '-') || s.chars().all(|c| c == '*') || s.chars().all(|c| c == '_'))
}

/// Un début d'élément de liste : (marque, reste) ; « - », « * », « + », « 12. », « 3) ».
fn list_marker(st: &str) -> Option<(String, &str)> {
    for p in ["- ", "* ", "+ "] {
        if let Some(r) = st.strip_prefix(p) {
            return Some(("•".into(), r));
        }
    }
    let n = st.chars().take_while(|c| c.is_ascii_digit()).count();
    if n > 0 && n < 10 {
        let rest = &st[n..];
        if let Some(r) = rest.strip_prefix(". ").or_else(|| rest.strip_prefix(") ")) {
            return Some((format!("{}.", &st[..n]), r));
        }
    }
    None
}

/// Les cellules d'une ligne de tableau (un `|` dans du `code` ou échappé `\|` est gardé).
pub(crate) fn cells(line: &str) -> Vec<String> {
    let s = line.trim();
    let s = s.strip_prefix('|').unwrap_or(s);
    let s = if s.ends_with('|') && !s.ends_with("\\|") { &s[..s.len() - 1] } else { s };
    let mut out = Vec::new();
    let mut cur = String::new();
    let mut code = false;
    let mut it = s.chars().peekable();
    while let Some(c) = it.next() {
        match c {
            '\\' if it.peek() == Some(&'|') => {
                cur.push('|');
                it.next();
            }
            '`' => {
                code = !code;
                cur.push(c);
            }
            '|' if !code => out.push(std::mem::take(&mut cur).trim().to_string()),
            _ => cur.push(c),
        }
    }
    out.push(cur.trim().to_string());
    out
}

fn delimiter_row(line: &str) -> Option<Vec<ColAlign>> {
    if !line.contains('-') {
        return None;
    }
    let mut out = Vec::new();
    for c in cells(line) {
        let c = c.trim();
        if c.is_empty() || !c.chars().all(|x| x == '-' || x == ':') || !c.contains('-') {
            return None;
        }
        out.push(match (c.starts_with(':'), c.ends_with(':')) {
            (true, true) => ColAlign::Center,
            (false, true) => ColAlign::Right,
            _ => ColAlign::Left,
        });
    }
    Some(out)
}

fn starts_block(l: &str, next: Option<&str>) -> bool {
    let st = l.trim_start();
    fence(st).is_some()
        || heading(st).is_some()
        || st.starts_with('>')
        || is_rule(st)
        || list_marker(st).is_some()
        || (st.contains('|') && next.is_some_and(|n| delimiter_row(n).is_some()))
}

/// Le texte en blocs.
pub(crate) fn blocks(src: &str) -> Vec<Block> {
    let lines: Vec<&str> = src.lines().collect();
    let mut out = Vec::new();
    let mut i = 0;
    while i < lines.len() {
        let l = lines[i];
        let st = l.trim_start();
        let indent = l.len() - st.len();
        if st.is_empty() {
            i += 1;
            continue;
        }
        if let Some(rest) = fence(st) {
            let mark = &st[..3];
            let lang = rest.split_whitespace().next().unwrap_or("").to_string();
            let mut code = Vec::new();
            i += 1;
            while i < lines.len() {
                let c = lines[i];
                let ct = c.trim();
                if ct.starts_with(mark) && ct.chars().all(|x| Some(x) == mark.chars().next()) {
                    i += 1;
                    break;
                }
                // (le retrait de la clôture ouvrante retiré de chaque ligne, comme CommonMark)
                let strip = c.len() - c.trim_start().len();
                code.push(&c[strip.min(indent)..]);
                i += 1;
            }
            out.push(Block::Code { lang, text: code.join("\n") });
            continue;
        }
        if let Some((n, text)) = heading(st) {
            out.push(Block::Heading(n, text.to_string()));
            i += 1;
            continue;
        }
        if is_rule(st) {
            out.push(Block::Rule);
            i += 1;
            continue;
        }
        if st.starts_with('>') {
            let mut body = Vec::new();
            while i < lines.len() && lines[i].trim_start().starts_with('>') {
                let q = &lines[i].trim_start()[1..];
                body.push(q.strip_prefix(' ').unwrap_or(q));
                i += 1;
            }
            let mut alert = None;
            if let Some(first) = body.first() {
                let f = first.trim();
                for k in ["NOTE", "TIP", "IMPORTANT", "WARNING", "CAUTION"] {
                    if f.eq_ignore_ascii_case(&format!("[!{k}]")) {
                        alert = Some(k.to_string());
                    }
                }
            }
            if alert.is_some() {
                body.remove(0);
            }
            out.push(Block::Quote { alert, body: body.join("\n") });
            continue;
        }
        if st.contains('|')
            && let Some(mut align) = lines.get(i + 1).and_then(|n| delimiter_row(n))
        {
            let head = cells(st);
            i += 2;
            let mut rows = Vec::new();
            while i < lines.len() && !lines[i].trim().is_empty() && lines[i].contains('|') {
                let mut r = cells(lines[i]);
                r.resize(head.len(), String::new());
                rows.push(r);
                i += 1;
            }
            align.resize(head.len(), ColAlign::Left);
            out.push(Block::Table { head, align, rows });
            continue;
        }
        if list_marker(st).is_some() {
            let base = indent;
            let mut items: Vec<ListItem> = Vec::new();
            let mut indents: Vec<usize> = Vec::new();
            while i < lines.len() {
                let l = lines[i];
                let s = l.trim_start();
                let ind = l.len() - s.len();
                if s.is_empty() {
                    // (une ligne vide dans une liste : elle continue si un élément, ou la suite d'un élément, suit)
                    let next = lines[i + 1..].iter().find(|x| !x.trim().is_empty());
                    match next {
                        Some(n) if list_marker(n.trim_start()).is_some() && n.len() - n.trim_start().len() >= base => {
                            i += 1;
                            continue;
                        }
                        Some(n) if !items.is_empty() && n.len() - n.trim_start().len() > base + 1 && fence(n.trim_start()).is_none() => {
                            i += 1;
                            continue;
                        }
                        _ => break,
                    }
                }
                if let Some((mark, rest)) = list_marker(s).filter(|_| ind >= base) {
                    while indents.last().is_some_and(|&x| x > ind) {
                        indents.pop();
                    }
                    if indents.last() != Some(&ind) {
                        indents.push(ind);
                    }
                    let (task, text) = if let Some(r) = rest.strip_prefix("[ ] ") {
                        (Some(false), r)
                    } else if let Some(r) = rest.strip_prefix("[x] ").or_else(|| rest.strip_prefix("[X] ")) {
                        (Some(true), r)
                    } else {
                        (None, rest)
                    };
                    items.push(ListItem { depth: indents.len() - 1, mark, task, text: text.trim_end().to_string() });
                    i += 1;
                } else if !items.is_empty() && fence(s).is_none() && (ind > base || (!starts_block(l, lines.get(i + 1).copied()) && !lines[i - 1].trim().is_empty())) {
                    // (la suite d'un élément, ou une continuation paresseuse)
                    if let Some(last) = items.last_mut() {
                        last.text.push(' ');
                        last.text.push_str(s.trim_end());
                    }
                    i += 1;
                } else {
                    break;
                }
            }
            out.push(Block::List(items));
            continue;
        }
        let mut para: Vec<&str> = vec![st];
        i += 1;
        while i < lines.len() && !lines[i].trim().is_empty() && !starts_block(lines[i], lines.get(i + 1).copied()) {
            para.push(lines[i].trim_start());
            i += 1;
        }
        out.push(Block::Para(join_lines(&para)));
    }
    out
}

/// Les lignes d'un paragraphe : un saut doux devient une espace, deux espaces ou `\` en fin de ligne un vrai saut.
fn join_lines(lines: &[&str]) -> String {
    let mut s = String::new();
    for (k, l) in lines.iter().enumerate() {
        if k > 0 {
            let prev = lines[k - 1];
            s.push(if prev.ends_with("  ") || prev.ends_with('\\') { '\n' } else { ' ' });
        }
        s.push_str(l.trim_end().trim_end_matches('\\'));
    }
    s
}

// ------------------------------------------------------------------------------------------- le dessin

/// Dessine le texte Markdown `src`.
pub fn show(ui: &mut Ui, t: &Tokens, src: &str) {
    ui.vertical(|ui| {
        ui.spacing_mut().item_spacing.y = 8.0;
        for b in blocks(src) {
            block(ui, t, &b);
        }
    });
}

fn block(ui: &mut Ui, t: &Tokens, b: &Block) {
    match b {
        Block::Code { lang, text } => code_block(ui, t, lang, text),
        Block::Heading(n, text) => {
            let size = match n {
                1 => theme::TITLE - 2.0,
                2 => theme::SECTION + 1.0,
                3 => theme::SECTION - 1.0,
                _ => theme::BODY,
            };
            ui.add_space(if *n <= 2 { 6.0 } else { 2.0 });
            inline_label(ui, t, text, theme::semibold(size), t.text);
            if *n <= 2 {
                let r = ui.available_rect_before_wrap();
                ui.painter().hline(r.x_range(), r.top() - 3.0, Stroke::new(1.0, t.separator));
                ui.add_space(2.0);
            }
        }
        Block::Para(text) => inline_label(ui, t, text, FontId::proportional(theme::BODY), t.text),
        Block::Rule => {
            ui.separator();
        }
        Block::List(items) => {
            ui.vertical(|ui| {
                ui.spacing_mut().item_spacing.y = 4.0;
                for it in items {
                    ui.horizontal_top(|ui| {
                        ui.spacing_mut().item_spacing.x = 4.0;
                        ui.add_space(2.0 + it.depth as f32 * 18.0);
                        let (mark, color) = match it.task {
                            Some(true) => (icon("check").to_string(), t.success),
                            Some(false) => (icon("ring").to_string(), t.text_faint),
                            None if it.mark == "•" && it.depth > 0 => ("◦".to_string(), t.text_faint),
                            None => (it.mark.clone(), t.text_faint),
                        };
                        let w = if it.mark.len() > 2 { 24.0 } else { 16.0 };
                        ui.add_sized([w, 20.0], Label::new(RichText::new(mark).color(color).font(FontId::proportional(theme::BODY))).selectable(false));
                        ui.vertical(|ui| inline_label(ui, t, &it.text, FontId::proportional(theme::BODY), t.text));
                    });
                }
            });
        }
        Block::Quote { alert, body } => {
            let color = match alert.as_deref() {
                Some("NOTE") | Some("IMPORTANT") => t.accent_ink,
                Some("TIP") => t.success,
                Some("WARNING") => t.warning,
                Some("CAUTION") => t.danger,
                _ => t.card_border,
            };
            let inner = Frame::new().inner_margin(Margin { left: 14, right: 0, top: 2, bottom: 2 }).show(ui, |ui| {
                if let Some(a) = alert {
                    let key = match a.as_str() {
                        "NOTE" => "Md.Alert.Note",
                        "TIP" => "Md.Alert.Tip",
                        "IMPORTANT" => "Md.Alert.Important",
                        "WARNING" => "Md.Alert.Warning",
                        _ => "Md.Alert.Caution",
                    };
                    ui.label(RichText::new(t!(key)).font(theme::semibold(theme::LABEL)).color(color));
                }
                let mut sub = *t;
                sub.text = t.text_dim;
                show(ui, &sub, body);
            });
            let r = inner.response.rect;
            ui.painter().vline(r.left() + 2.0, r.y_range(), Stroke::new(3.0, color));
        }
        Block::Table { head, align, rows } => table(ui, t, head, align, rows),
    }
}

/// Un tableau : colonnes à la taille de leur contenu (les plus larges réduites quand la place manque), en-tête
/// atténué, traits fins entre les lignes, dans un cadre.
fn table(ui: &mut Ui, t: &Tokens, head: &[String], align: &[ColAlign], rows: &[Vec<String>]) {
    let n = head.len().max(1);
    let pad = Vec2::new(12.0, 8.0);
    let avail = ui.available_width() - 2.0;
    let body_font = FontId::proportional(theme::LABEL + 1.0);
    let head_font = theme::semibold(theme::LABEL);
    let measure = |ui: &Ui, text: &str, font: &FontId| ui.ctx().fonts_mut(|f| f.layout_job(inline_job(t, text, font.clone(), t.text, f32::INFINITY).0)).size().x;
    let mut nat = vec![0f32; n];
    for (c, h) in head.iter().enumerate().take(n) {
        nat[c] = nat[c].max(measure(ui, h, &head_font));
    }
    for r in rows {
        for (c, cell) in r.iter().enumerate().take(n) {
            nat[c] = nat[c].max(measure(ui, cell, &body_font).min(420.0));
        }
    }
    let widths = column_widths(&nat, avail - pad.x * 2.0 * n as f32);
    let total = (widths.iter().sum::<f32>() + pad.x * 2.0 * n as f32).min(avail);
    Frame::new().stroke(Stroke::new(1.0, t.card_border)).corner_radius(CornerRadius::same(theme::R)).show(ui, |ui| {
        ui.set_width(total);
        ui.spacing_mut().item_spacing = Vec2::ZERO;
        let row = |ui: &mut Ui, cells: &[String], font: &FontId, color: Color32, first: bool| {
            let laid: Vec<(Arc<Galley>, Vec<(Range<usize>, String)>)> = (0..n)
                .map(|c| {
                    let (job, links) = inline_job(t, cells.get(c).map_or("", String::as_str), font.clone(), color, widths[c] + 0.5);
                    (ui.ctx().fonts_mut(|f| f.layout_job(job)), links)
                })
                .collect();
            let h = laid.iter().map(|(g, _)| g.size().y).fold(0.0, f32::max) + pad.y * 2.0;
            let (rect, _) = ui.allocate_exact_size(Vec2::new(total, h), Sense::hover());
            if !first {
                ui.painter().hline(rect.x_range(), rect.top(), Stroke::new(1.0, t.separator));
            }
            let mut x = rect.left();
            for (c, (g, links)) in laid.into_iter().enumerate() {
                let w = widths[c] + pad.x * 2.0;
                let gx = match align.get(c) {
                    Some(ColAlign::Right) => x + w - pad.x - g.size().x,
                    Some(ColAlign::Center) => x + (w - g.size().x) / 2.0,
                    _ => x + pad.x,
                };
                let cell = Rect::from_min_size(Pos2::new(gx, rect.top() + pad.y), g.size());
                let resp = ui.put(cell, Label::new(g.clone()).selectable(true));
                follow_links(ui, &resp, &g, &links);
                x += w;
            }
        };
        row(ui, head, &head_font, t.text_dim, true);
        for r in rows {
            row(ui, r, &body_font, t.text, false);
        }
    });
}

/// Les largeurs des colonnes : les naturelles quand elles tiennent, sinon un plafond commun pour les plus larges
/// (une colonne étroite garde sa place).
pub(crate) fn column_widths(nat: &[f32], avail: f32) -> Vec<f32> {
    let total: f32 = nat.iter().sum();
    if total <= avail || nat.is_empty() {
        return nat.to_vec();
    }
    let mut sorted = nat.to_vec();
    sorted.sort_by(f32::total_cmp);
    let mut rest = avail.max(40.0 * nat.len() as f32);
    let mut left = nat.len();
    let mut cap = 0.0;
    for w in &sorted {
        let share = rest / left as f32;
        if *w <= share {
            rest -= w;
            left -= 1;
        } else {
            cap = share;
            break;
        }
    }
    nat.iter().map(|&w| if cap > 0.0 { w.min(cap) } else { w }).collect()
}

/// Un bloc de code : l'en-tête (le langage, Copier), le code à chasse fixe, coloré simplement, sélectionnable.
pub fn code_block(ui: &mut Ui, t: &Tokens, lang: &str, text: &str) {
    Frame::new().fill(t.code_bg).stroke(Stroke::new(1.0, t.card_border)).corner_radius(theme::R).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        Frame::new().inner_margin(Margin::symmetric(12, 5)).show(ui, |ui| {
            ui.horizontal(|ui| {
                let label = ccui_shared::md::label((!lang.is_empty()).then_some(lang));
                ui.label(RichText::new(label).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    let b = egui::Button::new(RichText::new(format!("{} {}", icon("copy"), t!("Md.Copy"))).font(FontId::proportional(theme::CAPTION)).color(t.text_dim)).frame(false);
                    if ui.add(b).clicked() {
                        ui.ctx().copy_text(text.to_owned());
                    }
                });
            });
        });
        let r = ui.available_rect_before_wrap();
        ui.painter().hline(r.x_range(), r.top(), Stroke::new(1.0, t.card_border));
        Frame::new().inner_margin(Margin { left: 14, right: 12, top: 10, bottom: 12 }).show(ui, |ui| {
            let mut job = highlight(t, lang, text);
            job.wrap.max_width = ui.available_width();
            ui.add(Label::new(job).selectable(true).wrap());
        });
    });
}

// ------------------------------------------------------------------------------------------- la coloration

const KEYWORDS: &[&str] = &[
    "abstract", "as", "async", "await", "break", "case", "catch", "class", "const", "continue", "def", "default", "do", "done",
    "elif", "else", "enum", "export", "extends", "false", "False", "fi", "finally", "fn", "for", "from", "func", "function", "if",
    "impl", "import", "in", "interface", "is", "let", "local", "loop", "match", "mod", "mut", "namespace", "new", "nil", "None",
    "null", "override", "package", "private", "protected", "pub", "public", "readonly", "record", "return", "sealed", "self", "Self",
    "static", "struct", "super", "switch", "then", "this", "throw", "trait", "true", "True", "try", "type", "typeof", "use", "using",
    "var", "virtual", "void", "where", "while", "with", "yield",
];

fn hash_comments(lang: &str) -> bool {
    matches!(
        lang,
        "sh" | "bash" | "shell" | "zsh" | "console" | "python" | "py" | "yaml" | "yml" | "toml" | "ruby" | "rb" | "ps1" | "powershell" | "dockerfile" | "make" | "makefile" | "conf" | "ini" | "nix"
    )
}

/// Une coloration simple, la même pour tous les langages : mots-clés, chaînes, commentaires, nombres (diff : lignes
/// ajoutées et retirées ; texte : rien).
pub(crate) fn highlight(t: &Tokens, lang: &str, text: &str) -> LayoutJob {
    let font = theme::mono(theme::CODE);
    let fmt = |color: Color32, italics: bool| TextFormat { font_id: font.clone(), color, italics, ..Default::default() };
    let plain = fmt(t.text, false);
    let mut job = LayoutJob::default();
    let lang = lang.to_lowercase();
    if matches!(lang.as_str(), "diff" | "patch") {
        for line in text.split_inclusive('\n') {
            let c = match line.chars().next() {
                Some('+') => t.success,
                Some('-') => t.danger,
                Some('@') => t.accent_ink,
                _ => t.text,
            };
            job.append(line, 0.0, fmt(c, false));
        }
        return job;
    }
    if matches!(lang.as_str(), "" | "text" | "txt" | "plain" | "markdown" | "md" | "log" | "output") {
        job.append(text, 0.0, plain);
        return job;
    }
    let (kw, string, comment, number) = (fmt(t.accent_ink, false), fmt(t.success, false), fmt(t.text_faint, true), fmt(t.warning, false));
    let hash = hash_comments(&lang);
    let sql = matches!(lang.as_str(), "sql" | "lua" | "haskell");
    let b = text.as_bytes();
    let word = |k: usize| b[k].is_ascii_alphanumeric() || b[k] == b'_';
    let mut i = 0;
    let mut plain_from = 0;
    while i < b.len() {
        let c = b[i];
        let rest = &text[i..];
        let tok: Option<(usize, &TextFormat)> = if rest.starts_with("//") || (hash && c == b'#') || (sql && rest.starts_with("--")) {
            Some((rest.find('\n').unwrap_or(rest.len()), &comment))
        } else if rest.starts_with("/*") {
            Some((rest.find("*/").map_or(rest.len(), |e| e + 2), &comment))
        } else if c == b'"' || c == b'\'' || c == b'`' {
            let bs = rest.as_bytes();
            let mut j = 1;
            while j < bs.len() && bs[j] != c && bs[j] != b'\n' {
                j += if bs[j] == b'\\' { 2 } else { 1 };
            }
            // (une apostrophe seule, `'a` de Rust ou l'apostrophe d'un mot : pas une chaîne)
            (j < bs.len() && bs[j] == c && !(c == b'\'' && i > 0 && word(i - 1))).then_some((j + 1, &string))
        } else if c.is_ascii_digit() && (i == 0 || !word(i - 1)) {
            Some((rest.bytes().take_while(|x| x.is_ascii_alphanumeric() || *x == b'.' || *x == b'_').count(), &number))
        } else if (c.is_ascii_alphabetic() || c == b'_') && (i == 0 || !word(i - 1)) {
            let n = rest.bytes().take_while(|x| x.is_ascii_alphanumeric() || *x == b'_').count();
            if KEYWORDS.contains(&&rest[..n]) {
                Some((n, &kw))
            } else {
                i += n;
                continue;
            }
        } else {
            None
        };
        match tok {
            Some((n, f)) if n > 0 => {
                if i > plain_from {
                    job.append(&text[plain_from..i], 0.0, plain.clone());
                }
                let mut end = (i + n).min(text.len());
                while !text.is_char_boundary(end) {
                    end += 1;
                }
                job.append(&text[i..end], 0.0, f.clone());
                i = end;
                plain_from = i;
            }
            _ => {
                i += 1;
                while i < b.len() && !text.is_char_boundary(i) {
                    i += 1;
                }
            }
        }
    }
    if b.len() > plain_from {
        job.append(&text[plain_from..], 0.0, plain);
    }
    job
}

// ------------------------------------------------------------------------------------------- le texte

/// Le texte d'une ligne ou d'un paragraphe avec `code`, **gras**, *italique*, ~~barré~~, [liens](url) et adresses
/// nues : le job, et les liens (plages en caractères, adresse).
pub(crate) fn inline_job(t: &Tokens, text: &str, font: FontId, color: Color32, max_w: f32) -> (LayoutJob, Vec<(Range<usize>, String)>) {
    let mut job = LayoutJob::default();
    job.wrap.max_width = max_w;
    let base = TextFormat { font_id: font.clone(), color, ..Default::default() };
    let bold_family = FontFamily::Name("semibold".into());
    let style = |bold: bool, italic: bool, strike: bool| {
        let mut f = base.clone();
        if bold {
            f.font_id = FontId::new(font.size, bold_family.clone());
        }
        f.italics = italic;
        if strike {
            f.strikethrough = Stroke::new(1.0, color);
        }
        f
    };
    let code_fmt = TextFormat { font_id: theme::mono(font.size - 1.5), color: t.warning.lerp_to_gamma(t.text, 0.3), background: t.code_bg, ..Default::default() };
    let link_fmt = |f: TextFormat| TextFormat { color: t.accent_ink, underline: Stroke::new(1.0, t.accent_ink.gamma_multiply(0.6)), ..f };
    let mut links = Vec::new();
    let mut chars = 0usize;
    let mut buf = String::new();
    let (mut bold, mut italic, mut strike) = (false, false, false);
    let push = |job: &mut LayoutJob, chars: &mut usize, s: &str, f: TextFormat| {
        if !s.is_empty() {
            *chars += s.chars().count();
            job.append(s, 0.0, f);
        }
    };
    let mut rest = text;
    let mut prev: Option<char> = None;
    macro_rules! flush {
        () => {{
            let s = std::mem::take(&mut buf);
            push(&mut job, &mut chars, &s, style(bold, italic, strike));
        }};
    }
    while !rest.is_empty() {
        let word_before = prev.is_some_and(char::is_alphanumeric);
        if let Some(r) = rest.strip_prefix('\\')
            && let Some(c) = r.chars().next()
            && c.is_ascii_punctuation()
        {
            buf.push(c);
            prev = Some(c);
            rest = &r[c.len_utf8()..];
            continue;
        }
        if rest.starts_with('`') {
            let ticks = rest.chars().take_while(|&c| c == '`').count();
            let fence = &rest[..ticks];
            if let Some(end) = rest[ticks..].find(fence) {
                flush!();
                let inner = &rest[ticks..ticks + end];
                let inner = if inner.len() > 2 && inner.starts_with(' ') && inner.ends_with(' ') { &inner[1..inner.len() - 1] } else { inner };
                push(&mut job, &mut chars, &format!("\u{2009}{inner}\u{2009}"), code_fmt.clone());
                rest = &rest[ticks + end + ticks..];
                prev = Some('`');
                continue;
            }
        }
        if rest.starts_with("**") || (rest.starts_with("__") && !word_before) {
            let (mark, r) = rest.split_at(2);
            if bold || (r.contains(mark) && !r.starts_with(' ')) {
                flush!();
                bold = !bold;
                rest = r;
                continue;
            }
        }
        if let Some(r) = rest.strip_prefix("~~")
            && (strike || r.contains("~~"))
        {
            flush!();
            strike = !strike;
            rest = r;
            continue;
        }
        if rest.starts_with('*') || (rest.starts_with('_') && (!word_before || italic)) {
            let (mark, r) = rest.split_at(1);
            let next_word = r.chars().next().is_some_and(char::is_alphanumeric);
            let closes = italic && !buf.ends_with(' ') && (mark == "*" || !next_word);
            let opens = !italic && !r.starts_with(' ') && !r.is_empty() && r.contains(mark);
            if closes || opens {
                flush!();
                italic = !italic;
                rest = r;
                continue;
            }
        }
        if let Some(r) = rest.strip_prefix('[')
            && let Some(close) = r.find("](")
            && !r[..close].contains('[')
            && let Some(end) = r[close + 2..].find(')')
        {
            flush!();
            let label = r[..close].replace('`', "");
            let url = r[close + 2..close + 2 + end].split_whitespace().next().unwrap_or("").to_string();
            let from = chars;
            if ccui_shared::md::external(&url) {
                push(&mut job, &mut chars, &label, link_fmt(style(bold, italic, strike)));
                links.push((from..chars, url));
            } else {
                push(&mut job, &mut chars, &label, style(bold, italic, strike));
            }
            rest = &r[close + 2 + end + 1..];
            prev = Some(')');
            continue;
        }
        if (rest.starts_with("https://") || rest.starts_with("http://")) && !word_before {
            let n = rest.find(|c: char| c.is_whitespace() || matches!(c, '<' | '>' | '`')).unwrap_or(rest.len());
            let url = rest[..n].trim_end_matches(['.', ',', ';', ':', '!', '?', ')', '\'', '"']);
            if url.len() > 8 {
                flush!();
                let from = chars;
                push(&mut job, &mut chars, url, link_fmt(style(bold, italic, strike)));
                links.push((from..chars, url.to_string()));
                rest = &rest[url.len()..];
                prev = Some('/');
                continue;
            }
        }
        let c = rest.chars().next().unwrap_or(' ');
        buf.push(c);
        prev = Some(c);
        rest = &rest[c.len_utf8()..];
    }
    flush!();
    (job, links)
}

/// Les liens d'un label (son galley `g`) : la main au-dessus, un clic ouvre l'adresse.
fn follow_links(ui: &Ui, resp: &egui::Response, g: &Galley, links: &[(Range<usize>, String)]) {
    if links.is_empty() {
        return;
    }
    let Some(p) = resp.hover_pos() else { return };
    let rel = p - resp.rect.min;
    if !g.rect.expand(2.0).contains(rel.to_pos2()) {
        return;
    }
    let idx = g.cursor_from_pos(rel).index.0;
    if let Some((_, url)) = links.iter().find(|(r, _)| r.contains(&idx)) {
        ui.ctx().set_cursor_icon(egui::CursorIcon::PointingHand);
        if resp.clicked() {
            crate::host::open(url);
        }
    }
}

/// Une ligne ou un paragraphe (`inline_job`) dans un seul label sélectionnable ; ses liens s'ouvrent d'un clic.
pub fn inline_label(ui: &mut Ui, t: &Tokens, text: &str, font: FontId, color: Color32) {
    let (job, links) = inline_job(t, text, font, color, ui.available_width());
    let g = ui.ctx().fonts_mut(|f| f.layout_job(job));
    let resp = ui.add(Label::new(g.clone()).selectable(true));
    follow_links(ui, &resp, &g, &links);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splits_blocks() {
        let b = blocks("# Titre\n\nAvant\nsuite\n```rust\nfn main() {}\n```\n- a\n  - b\n- [x] c\n\n> [!NOTE]\n> dit\n\n| A | B |\n|---|--:|\n| `x|y` | 2 |\n\n---\nfin");
        assert_eq!(b[0], Block::Heading(1, "Titre".into()));
        assert_eq!(b[1], Block::Para("Avant suite".into()));
        assert_eq!(b[2], Block::Code { lang: "rust".into(), text: "fn main() {}".into() });
        match &b[3] {
            Block::List(items) => {
                assert_eq!(items.len(), 3);
                assert_eq!(items[1].depth, 1);
                assert_eq!(items[2].task, Some(true));
                assert_eq!(items[2].text, "c");
            }
            other => panic!("{other:?}"),
        }
        assert_eq!(b[4], Block::Quote { alert: Some("NOTE".into()), body: "dit".into() });
        assert_eq!(b[5], Block::Table { head: vec!["A".into(), "B".into()], align: vec![ColAlign::Left, ColAlign::Right], rows: vec![vec!["`x|y`".into(), "2".into()]] });
        assert_eq!(b[6], Block::Rule);
        assert_eq!(b[7], Block::Para("fin".into()));
    }

    #[test]
    fn numbered_and_lazy() {
        let b = blocks("1. un\n2. deux\nsuite\n\nApres");
        match &b[0] {
            Block::List(items) => {
                assert_eq!(items[0].mark, "1.");
                assert_eq!(items[1].text, "deux suite");
            }
            other => panic!("{other:?}"),
        }
        assert_eq!(b[1], Block::Para("Apres".into()));
    }

    #[test]
    fn inline_links() {
        let t = Tokens::studio();
        let (job, links) = inline_job(&t, "voir [doc](https://a.b/c) et https://x.y/z.", FontId::proportional(14.0), t.text, 100.0);
        assert_eq!(job.text, "voir doc et https://x.y/z.");
        assert_eq!(links[0], (5..8, "https://a.b/c".to_string()));
        assert_eq!(links[1].1, "https://x.y/z");
        let (job, _) = inline_job(&t, "**gras** *it* snake_case_name ~~non~~", FontId::proportional(14.0), t.text, 100.0);
        assert_eq!(job.text, "gras it snake_case_name non");
    }

    #[test]
    fn widths() {
        assert_eq!(column_widths(&[10.0, 20.0], 100.0), vec![10.0, 20.0]);
        let w = column_widths(&[50.0, 300.0, 300.0], 350.0);
        assert_eq!(w[0], 50.0);
        assert!((w[1] - 150.0).abs() < 0.01 && (w[2] - 150.0).abs() < 0.01);
    }

    #[test]
    fn highlights() {
        let t = Tokens::studio();
        let job = highlight(&t, "rust", "let s = \"é\"; // fin\nfn x() {} it's");
        assert_eq!(job.text, "let s = \"é\"; // fin\nfn x() {} it's");
        assert!(job.sections.len() > 3);
    }
}
