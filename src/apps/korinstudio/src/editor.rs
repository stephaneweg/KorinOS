// L'éditeur de code : un document (texte, chemin, langage, état), son affichage (TextEdit d'egui avec la
// coloration, la gouttière des numéros, la ligne courante, les soulignés des diagnostics, la recherche) et les
// actions d'édition (indentation, commentaires, lignes).

use crate::highlight::{self, Lang, Tok};
use crate::tasks::{Diagnostic, Severity};
use crate::theme::Palette;
use egui::text::{CCursor, CCursorRange, LayoutJob, TextFormat};
use egui::{Align, Color32, FontId, Id, Pos2, Rect, Sense, Shape, Stroke, Ui, Vec2, pos2, vec2};
use std::ops::Range;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::SystemTime;

pub struct Doc {
    pub id: u64,
    pub path: Option<PathBuf>,
    pub untitled: String,
    pub text: String,
    pub lang: Lang,
    pub dirty: bool,
    pub version: u64,
    pub disk_mtime: Option<SystemTime>,
    pub conflict: bool,
    pub cursor: (usize, usize), // ligne, colonne (à partir de 1)
    pub cursor_char: usize,
    pub sel: Option<(usize, usize)>, // plage de caractères sélectionnée
    pub selection_text: String,
    pending_cursor: Option<(usize, usize)>, // (début, fin) en caractères
    pending_scroll: bool,
    pub pinned: bool,
    hl: Option<(u64, bool, Arc<Vec<(Range<usize>, Tok)>>)>,
    line_starts: Option<(u64, Arc<Vec<usize>>)>,
    pub find: FindState,
    pub flash: Option<(usize, f64)>, // une ligne mise en avant (aller à), avec l'heure
    pub agent_touched: Option<f64>,
}

#[derive(Default)]
pub struct FindState {
    pub open: bool,
    pub replace_open: bool,
    pub query: String,
    pub replace: String,
    pub case: bool,
    pub regex: bool,
    pub whole: bool,
    pub focus: bool,
    pub current: usize,
    matches: Vec<Range<usize>>,
    key: Option<(u64, String, bool, bool, bool)>,
    pub error: Option<String>,
}

pub fn mtime(p: &Path) -> Option<SystemTime> {
    std::fs::metadata(p).ok().and_then(|m| m.modified().ok())
}

impl Doc {
    pub fn new_untitled(id: u64, n: usize) -> Doc {
        Doc::with(id, None, format!("Sans titre-{n}"), String::new(), Lang::Plain)
    }

    fn with(id: u64, path: Option<PathBuf>, untitled: String, text: String, lang: Lang) -> Doc {
        Doc {
            id,
            disk_mtime: path.as_deref().and_then(mtime),
            path,
            untitled,
            text,
            lang,
            dirty: false,
            version: 1,
            conflict: false,
            cursor: (1, 1),
            cursor_char: 0,
            sel: None,
            selection_text: String::new(),
            pending_cursor: None,
            pending_scroll: false,
            pinned: false,
            hl: None,
            line_starts: None,
            find: FindState::default(),
            flash: None,
            agent_touched: None,
        }
    }

    pub fn open(id: u64, path: &Path) -> Result<Doc, String> {
        let bytes = std::fs::read(path).map_err(|e| format!("{} : {e}", path.display()))?;
        if bytes.len() > 16 << 20 {
            return Err(format!("{} : fichier trop grand ({} Mo)", path.display(), bytes.len() >> 20));
        }
        if bytes.iter().take(8000).any(|&b| b == 0) {
            return Err(format!("{} : fichier binaire", path.display()));
        }
        let text = String::from_utf8_lossy(&bytes).replace("\r\n", "\n");
        Ok(Doc::with(id, Some(path.to_path_buf()), String::new(), text, Lang::from_path(path)))
    }

    pub fn title(&self) -> String {
        match &self.path {
            Some(p) => p.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default(),
            None => self.untitled.clone(),
        }
    }

    pub fn set_text(&mut self, text: String) {
        if text != self.text {
            self.text = text;
            self.version += 1;
        }
    }

    pub fn save(&mut self) -> Result<(), String> {
        let Some(p) = self.path.clone() else { return Err("pas de chemin".into()) };
        if let Some(d) = p.parent() {
            let _ = std::fs::create_dir_all(d);
        }
        std::fs::write(&p, self.text.as_bytes()).map_err(|e| format!("{} : {e}", p.display()))?;
        self.dirty = false;
        self.conflict = false;
        self.disk_mtime = mtime(&p);
        Ok(())
    }

    pub fn save_as(&mut self, p: PathBuf) -> Result<(), String> {
        self.lang = Lang::from_path(&p);
        self.path = Some(p);
        self.hl = None;
        self.save()
    }

    /// Relit le fichier s'il a changé sur le disque (un agent l'a modifié) ; true si relu.
    pub fn check_disk(&mut self, now: f64) -> bool {
        let Some(p) = &self.path else { return false };
        let m = mtime(p);
        if m.is_none() || m == self.disk_mtime {
            return false;
        }
        if self.dirty {
            self.conflict = true;
            self.disk_mtime = m;
            return false;
        }
        if let Ok(b) = std::fs::read(p) {
            let t = String::from_utf8_lossy(&b).replace("\r\n", "\n");
            self.disk_mtime = m;
            if t != self.text {
                self.set_text(t);
                self.agent_touched = Some(now);
                return true;
            }
        }
        false
    }

    pub fn reload(&mut self) {
        if let Some(p) = self.path.clone() {
            if let Ok(b) = std::fs::read(&p) {
                self.set_text(String::from_utf8_lossy(&b).replace("\r\n", "\n"));
                self.dirty = false;
                self.conflict = false;
                self.disk_mtime = mtime(&p);
            }
        }
    }

    pub fn line_starts(&mut self) -> Arc<Vec<usize>> {
        if let Some((v, ls)) = &self.line_starts {
            if *v == self.version {
                return ls.clone();
            }
        }
        let mut v = vec![0];
        for (i, b) in self.text.bytes().enumerate() {
            if b == b'\n' {
                v.push(i + 1);
            }
        }
        let a = Arc::new(v);
        self.line_starts = Some((self.version, a.clone()));
        a
    }

    pub fn line_count(&mut self) -> usize {
        self.line_starts().len()
    }

    /// Place le curseur à la ligne et colonne données (à partir de 1), et y fait défiler.
    pub fn goto(&mut self, line: usize, col: usize, now: f64) {
        let ls = self.line_starts();
        let l = line.clamp(1, ls.len()) - 1;
        let start = ls[l];
        let end = ls.get(l + 1).map(|e| e - 1).unwrap_or(self.text.len());
        let line_text = &self.text[start..end];
        let col_b = line_text.char_indices().nth(col.saturating_sub(1)).map(|(b, _)| b).unwrap_or(line_text.len());
        let c = char_index(&self.text, start + col_b);
        self.pending_cursor = Some((c, c));
        self.pending_scroll = true;
        self.flash = Some((l + 1, now));
    }

    /// Sélectionne une plage d'octets.
    pub fn select_bytes(&mut self, r: Range<usize>) {
        let a = char_index(&self.text, r.start);
        let b = char_index(&self.text, r.end);
        self.pending_cursor = Some((a, b));
        self.pending_scroll = true;
    }

    fn spans(&mut self, dark: bool) -> Arc<Vec<(Range<usize>, Tok)>> {
        if let Some((v, d, s)) = &self.hl {
            if *v == self.version && *d == dark {
                return s.clone();
            }
        }
        let s = Arc::new(highlight::highlight(self.lang, &self.text));
        self.hl = Some((self.version, dark, s.clone()));
        s
    }
}

pub fn char_index(text: &str, byte: usize) -> usize {
    let byte = byte.min(text.len());
    text[..byte].chars().count()
}

pub fn byte_index(text: &str, ch: usize) -> usize {
    text.char_indices().nth(ch).map(|(b, _)| b).unwrap_or(text.len())
}

fn uses_spaces(lang: Lang) -> bool {
    matches!(lang, Lang::Rust | Lang::Python | Lang::CSharp | Lang::Toml | Lang::Json | Lang::Markdown | Lang::Xml | Lang::Cpp)
}

pub struct EditorStyle<'a> {
    pub pal: &'a Palette,
    pub code_size: f32,
    pub tab_size: usize,
    pub wrap: bool,
    pub diagnostics: Vec<&'a Diagnostic>,
    pub now: f64,
}

#[derive(Default)]
pub struct EditorOut {
    pub changed: bool,
    pub focused: bool,
}

/// Construit la mise en page colorée d'un texte.
fn layout(text: &str, spans: &[(Range<usize>, Tok)], pal: &Palette, font: &FontId, wrap: f32) -> LayoutJob {
    let mut job = LayoutJob::default();
    job.wrap.max_width = wrap;
    let fmt = |t: Tok| {
        let mut f = TextFormat::simple(font.clone(), pal.tok(t));
        if matches!(t, Tok::Comment | Tok::DocComment | Tok::Emphasis) {
            f.italics = true;
        }
        if t == Tok::Link {
            f.underline = Stroke::new(1.0, pal.tok(t));
        }
        f
    };
    let mut pos = 0;
    for (r, t) in spans {
        if r.start > text.len() || r.end > text.len() || r.start < pos || !text.is_char_boundary(r.start) || !text.is_char_boundary(r.end) {
            continue;
        }
        if r.start > pos {
            job.append(&text[pos..r.start], 0.0, fmt(Tok::Text));
        }
        job.append(&text[r.clone()], 0.0, fmt(*t));
        pos = r.end;
    }
    if pos < text.len() {
        job.append(&text[pos..], 0.0, fmt(Tok::Text));
    }
    job
}

fn find_matches(doc: &mut Doc) {
    let f = &mut doc.find;
    let key = (doc.version, f.query.clone(), f.case, f.regex, f.whole);
    if f.key.as_ref() == Some(&key) {
        return;
    }
    f.key = Some(key);
    f.matches.clear();
    f.error = None;
    if f.query.is_empty() {
        return;
    }
    let mut pat = if f.regex { f.query.clone() } else { regex::escape(&f.query) };
    if f.whole {
        pat = format!(r"\b{pat}\b");
    }
    if !f.case {
        pat = format!("(?i){pat}");
    }
    match regex::Regex::new(&pat) {
        Ok(re) => {
            for m in re.find_iter(&doc.text).take(20_000) {
                if m.start() != m.end() {
                    f.matches.push(m.range());
                }
            }
        }
        Err(e) => f.error = Some(e.to_string().lines().last().unwrap_or("").to_string()),
    }
    if f.current >= f.matches.len() {
        f.current = 0;
    }
}

impl Doc {
    pub fn find_next(&mut self, back: bool) {
        find_matches(self);
        let n = self.find.matches.len();
        if n == 0 {
            return;
        }
        let cur = byte_index(&self.text, self.cursor_char);
        let i = if back {
            self.find.matches.iter().rposition(|m| m.end < cur).unwrap_or(n - 1)
        } else {
            self.find.matches.iter().position(|m| m.start > cur || (m.start == cur && self.sel.is_none())).unwrap_or(0)
        };
        self.find.current = i;
        let r = self.find.matches[i].clone();
        self.select_bytes(r.clone());
        self.cursor_char = char_index(&self.text, r.end);
    }

    pub fn replace_one(&mut self) {
        find_matches(self);
        if self.find.matches.is_empty() {
            return;
        }
        let i = self.find.current.min(self.find.matches.len() - 1);
        let r = self.find.matches[i].clone();
        let rep = self.expand_replacement(&self.text[r.clone()].to_string());
        let mut t = self.text.clone();
        t.replace_range(r.clone(), &rep);
        self.set_text(t);
        self.dirty = true;
        self.cursor_char = char_index(&self.text, r.start + rep.len());
        self.find_next(false);
    }

    pub fn replace_all(&mut self) -> usize {
        find_matches(self);
        let ms = self.find.matches.clone();
        if ms.is_empty() {
            return 0;
        }
        let mut t = String::with_capacity(self.text.len());
        let mut pos = 0;
        for m in &ms {
            t.push_str(&self.text[pos..m.start]);
            t.push_str(&self.expand_replacement(&self.text[m.clone()].to_string()));
            pos = m.end;
        }
        t.push_str(&self.text[pos..]);
        self.set_text(t);
        self.dirty = true;
        ms.len()
    }

    fn expand_replacement(&self, matched: &str) -> String {
        if self.find.regex {
            let pat = if self.find.case { self.find.query.clone() } else { format!("(?i){}", self.find.query) };
            if let Ok(re) = regex::Regex::new(&pat) {
                return re.replace(matched, self.find.replace.as_str()).to_string();
            }
        }
        self.find.replace.clone()
    }

    /// Lignes (0..) couvertes par la sélection, ou celle du curseur.
    fn sel_lines(&mut self) -> (usize, usize) {
        let ls = self.line_starts();
        let (a, b) = self.sel.unwrap_or((self.cursor_char, self.cursor_char));
        let (a, b) = (byte_index(&self.text, a.min(b)), byte_index(&self.text, a.max(b)));
        let la = ls.partition_point(|&s| s <= a).saturating_sub(1);
        let mut lb = ls.partition_point(|&s| s <= b).saturating_sub(1);
        if lb > la && ls[lb] == b {
            lb -= 1;
        }
        (la, lb)
    }

    pub fn toggle_comment(&mut self) {
        let Some(cm) = self.lang.line_comment() else { return };
        let (la, lb) = self.sel_lines();
        let ls = self.line_starts();
        let lines: Vec<(usize, usize)> = (la..=lb).map(|l| (ls[l], ls.get(l + 1).map(|e| e - 1).unwrap_or(self.text.len()))).collect();
        let all = lines.iter().all(|&(s, e)| {
            let t = self.text[s..e].trim_start();
            t.is_empty() || t.starts_with(cm)
        });
        let indent = lines
            .iter()
            .filter(|&&(s, e)| !self.text[s..e].trim().is_empty())
            .map(|&(s, e)| self.text[s..e].len() - self.text[s..e].trim_start().len())
            .min()
            .unwrap_or(0);
        let mut t = String::new();
        let mut pos = 0;
        for &(s, e) in &lines {
            t.push_str(&self.text[pos..s]);
            let line = &self.text[s..e];
            if line.trim().is_empty() {
                t.push_str(line);
            } else if all {
                let i = line.find(cm).unwrap_or(0);
                let mut rest = &line[i + cm.len()..];
                if rest.starts_with(' ') {
                    rest = &rest[1..];
                }
                t.push_str(&line[..i]);
                t.push_str(rest);
            } else {
                let i = indent.min(line.len());
                t.push_str(&line[..i]);
                t.push_str(cm);
                t.push(' ');
                t.push_str(&line[i..]);
            }
            pos = e;
        }
        t.push_str(&self.text[pos..]);
        self.set_text(t);
        self.dirty = true;
    }

    pub fn delete_line(&mut self) {
        let (la, lb) = self.sel_lines();
        let ls = self.line_starts();
        let s = ls[la];
        let e = ls.get(lb + 1).copied().unwrap_or(self.text.len());
        let mut t = self.text.clone();
        t.replace_range(s..e, "");
        self.set_text(t);
        self.dirty = true;
        let c = char_index(&self.text, s.min(self.text.len()));
        self.pending_cursor = Some((c, c));
    }

    pub fn duplicate_line(&mut self) {
        let (la, lb) = self.sel_lines();
        let ls = self.line_starts();
        let s = ls[la];
        let e = ls.get(lb + 1).copied().unwrap_or(self.text.len());
        let mut chunk = self.text[s..e].to_string();
        if !chunk.ends_with('\n') {
            chunk.insert(0, '\n');
        }
        let mut t = self.text.clone();
        t.insert_str(e, &chunk);
        self.set_text(t);
        self.dirty = true;
    }

    /// Après une frappe : Entrée garde l'indentation (et l'augmente après « { », « : »…), Tab met des espaces.
    fn after_typing(&mut self, tab_size: usize) -> bool {
        let c = self.cursor_char;
        if c == 0 {
            return false;
        }
        let b = byte_index(&self.text, c);
        let prev = self.text[..b].chars().last();
        match prev {
            Some('\n') => {
                let before = &self.text[..b - 1];
                let ls = before.rfind('\n').map(|i| i + 1).unwrap_or(0);
                let line = &before[ls..];
                let mut indent: String = line.chars().take_while(|c| *c == ' ' || *c == '\t').collect();
                let trimmed = line.trim_end();
                let unit = if uses_spaces(self.lang) { " ".repeat(tab_size) } else { "\t".into() };
                if trimmed.ends_with(['{', '(', '[']) || (self.lang == Lang::Python && trimmed.ends_with(':')) {
                    indent.push_str(&unit);
                }
                if indent.is_empty() {
                    return false;
                }
                let mut t = self.text.clone();
                t.insert_str(b, &indent);
                // « {|} » : la fermante passe à la ligne suivante
                let after = &self.text[b..];
                if after.starts_with(['}', ')', ']']) && indent.len() >= unit.len() {
                    let base = &indent[..indent.len() - unit.len()];
                    t.insert_str(b + indent.len(), &format!("\n{base}"));
                }
                self.set_text(t);
                let nc = c + indent.chars().count();
                self.pending_cursor = Some((nc, nc));
                true
            }
            Some('\t') if uses_spaces(self.lang) => {
                let ls = self.text[..b - 1].rfind('\n').map(|i| i + 1).unwrap_or(0);
                let col = self.text[ls..b - 1].chars().count();
                let n = tab_size - col % tab_size.max(1);
                let mut t = self.text.clone();
                t.replace_range(b - 1..b, &" ".repeat(n));
                self.set_text(t);
                let nc = c - 1 + n;
                self.pending_cursor = Some((nc, nc));
                true
            }
            Some('}') => {
                // une fermante seule sur sa ligne : désindenter
                let ls = self.text[..b].rfind('\n').map(|i| i + 1).unwrap_or(0);
                let line = &self.text[ls..b - 1];
                if !line.is_empty() && line.chars().all(|c| c == ' ' || c == '\t') {
                    let unit = if uses_spaces(self.lang) { tab_size } else { 1 };
                    let cut = unit.min(line.len());
                    let mut t = self.text.clone();
                    t.replace_range(ls..ls + cut, "");
                    self.set_text(t);
                    let nc = c - cut;
                    self.pending_cursor = Some((nc, nc));
                    return true;
                }
                false
            }
            _ => false,
        }
    }
}

/// Dessine l'éditeur d'un document dans `ui` (toute la place).
pub fn show(ui: &mut Ui, doc: &mut Doc, st: &EditorStyle) -> EditorOut {
    let mut out = EditorOut::default();
    let pal = st.pal;
    let font = FontId::monospace(st.code_size);
    let row_h = ui.fonts_mut(|f| f.row_height(&font));
    let line_count = doc.line_count();
    let digits = (line_count.max(1) as f32).log10().floor() as usize + 1;
    let char_w = ui.fonts_mut(|f| f.glyph_width(&font, '0'));
    let gutter_w = (digits.max(3) as f32) * char_w + 34.0;
    let full = ui.available_rect_before_wrap();
    ui.painter().rect_filled(full, 0.0, pal.editor_bg);
    let gutter = Rect::from_min_max(full.min, pos2(full.min.x + gutter_w, full.max.y));
    let text_rect = Rect::from_min_max(pos2(gutter.max.x, full.min.y), full.max);

    let id = Id::new(("ks-editor", doc.id));
    // le curseur et la sélection voulus (aller à, rechercher, édition par programme)
    if let Some((a, b)) = doc.pending_cursor.take() {
        let mut state = egui::text_edit::TextEditState::load(ui.ctx(), id).unwrap_or_default();
        state.cursor.set_char_range(Some(CCursorRange::two(CCursor::new(a), CCursor::new(b))));
        state.store(ui.ctx(), id);
        ui.ctx().memory_mut(|m| m.request_focus(id));
    }

    let spans = doc.spans(pal.dark);
    let wrap = st.wrap;
    let mut layouter = |ui: &Ui, buf: &dyn egui::TextBuffer, wrap_width: f32| {
        let job = layout(buf.as_str(), &spans, pal, &font, if wrap { wrap_width } else { f32::INFINITY });
        ui.fonts_mut(|f| f.layout_job(job))
    };

    let mut child = ui.new_child(egui::UiBuilder::new().max_rect(text_rect).layout(egui::Layout::top_down(Align::Min)));
    let scroll = egui::ScrollArea::both().id_salt(("ks-scroll", doc.id)).auto_shrink([false, false]);
    let mut text_out = None;
    let mut bg_idx = None;
    let mut fg_idx = None;
    let before_version = doc.version;
    scroll.show(&mut child, |ui| {
        bg_idx = Some(ui.painter().add(Shape::Noop));
        let te = egui::TextEdit::multiline(&mut doc.text)
            .id(id)
            .font(egui::TextStyle::Monospace)
            .code_editor()
            .lock_focus(true)
            .frame(egui::Frame::NONE)
            .desired_width(if wrap { ui.available_width() } else { f32::INFINITY })
            .desired_rows(((text_rect.height() / row_h) as usize).max(1))
            .margin(egui::Margin { left: 4, right: 40, top: 2, bottom: (text_rect.height() * 0.5) as i8 / 2 })
            .layouter(&mut layouter);
        let o = te.show(ui);
        fg_idx = Some(ui.painter().add(Shape::Noop));
        if doc.pending_scroll {
            if let Some(r) = &o.cursor_range {
                let cr = o.galley.pos_from_cursor(r.primary).translate(o.galley_pos.to_vec2());
                ui.scroll_to_rect(cr.expand2(vec2(40.0, row_h * 3.0)), Some(Align::Center));
            }
            doc.pending_scroll = false;
        }
        text_out = Some(o);
    });
    let Some(o) = text_out else { return out };
    if o.response.changed() {
        doc.version += 1;
        doc.dirty = true;
        out.changed = true;
    }
    out.focused = o.response.has_focus();
    // curseur, sélection
    if let Some(r) = o.cursor_range {
        let (a, b) = (r.primary.index.0, r.secondary.index.0);
        doc.cursor_char = a;
        doc.sel = if a != b { Some((a.min(b), a.max(b))) } else { None };
        let bi = byte_index(&doc.text, a);
        let ls = doc.line_starts();
        let l = ls.partition_point(|&s| s <= bi).saturating_sub(1);
        doc.cursor = (l + 1, doc.text[ls[l]..bi].chars().count() + 1);
        doc.selection_text = match doc.sel {
            Some((s, e)) => {
                let (bs, be) = (byte_index(&doc.text, s), byte_index(&doc.text, e));
                doc.text[bs..be].chars().take(20_000).collect()
            }
            None => String::new(),
        };
    }
    if out.changed && before_version + 1 == doc.version && doc.after_typing(st.tab_size) {
        doc.dirty = true;
        ui.ctx().request_repaint();
    }

    let galley = &o.galley;
    let gpos = o.galley_pos;
    let clip = text_rect;
    // la ligne courante, les correspondances de la recherche (sous le texte)
    let mut under = Vec::new();
    let cur_line = doc.cursor.0;
    let mut line_no = 1;
    let mut row_line: Vec<(usize, Rect, bool)> = Vec::with_capacity(galley.rows.len());
    let mut new_line = true;
    for row in &galley.rows {
        let r = Rect::from_min_size(gpos + row.pos.to_vec2(), vec2(row.rect().width(), row_h.max(row.rect().height())));
        row_line.push((line_no, r, new_line));
        new_line = row.ends_with_newline;
        if row.ends_with_newline {
            line_no += 1;
        }
    }
    let flash_line = doc.flash.and_then(|(l, t)| if st.now - t < 1.2 { Some(l) } else { None });
    for (l, r, _) in &row_line {
        if r.max.y < clip.min.y || r.min.y > clip.max.y {
            continue;
        }
        if *l == cur_line && doc.sel.is_none() {
            let rr = Rect::from_min_max(pos2(clip.min.x, r.min.y), pos2(clip.max.x, r.max.y));
            under.push(Shape::rect_stroke(rr, 0.0, Stroke::new(1.0, pal.current_line.gamma_multiply(1.6)), egui::StrokeKind::Inside));
            under.push(Shape::rect_filled(rr, 0.0, pal.current_line));
        }
        if Some(*l) == flash_line {
            let rr = Rect::from_min_max(pos2(clip.min.x, r.min.y), pos2(clip.max.x, r.max.y));
            under.push(Shape::rect_filled(rr, 0.0, pal.find_match.gamma_multiply(0.5)));
        }
    }
    if doc.find.open && !doc.find.query.is_empty() {
        find_matches(doc);
        let vis_a = galley.cursor_from_pos((clip.min - gpos).max(Vec2::ZERO));
        let vis_b = galley.cursor_from_pos(clip.max - gpos);
        let (ba, bb) = (byte_index(&doc.text, vis_a.index.0).saturating_sub(400), byte_index(&doc.text, vis_b.index.0) + 400);
        for (i, m) in doc.find.matches.iter().enumerate() {
            if m.end < ba || m.start > bb {
                continue;
            }
            let a = galley.pos_from_cursor(CCursor::new(char_index(&doc.text, m.start)));
            let b = galley.pos_from_cursor(CCursor::new(char_index(&doc.text, m.end)));
            if (a.min.y - b.min.y).abs() > 1.0 {
                continue;
            }
            let rr = Rect::from_min_max(gpos + a.min.to_vec2(), gpos + vec2(b.min.x, a.max.y));
            let col = if i == doc.find.current { pal.find_match } else { pal.find_match.gamma_multiply(0.45) };
            under.push(Shape::rect_filled(rr, 2.0, col));
        }
    }
    if let Some(i) = bg_idx {
        ui.painter().with_clip_rect(clip).set(i, Shape::Vec(under));
    }
    // les soulignés des diagnostics (ondulés)
    let mut over = Vec::new();
    for d in &st.diagnostics {
        let Some((_, r, _)) = row_line.iter().find(|(l, _, first)| *l == d.line && *first) else { continue };
        if r.max.y < clip.min.y || r.min.y > clip.max.y {
            continue;
        }
        let col = match d.severity {
            Severity::Error => pal.error,
            Severity::Warning => pal.warning,
            Severity::Info => pal.info,
        };
        let ls = doc.line_starts();
        let Some(&s) = ls.get(d.line - 1) else { continue };
        let e = ls.get(d.line).map(|e| e - 1).unwrap_or(doc.text.len());
        let line = &doc.text[s..e];
        let lead = line.len() - line.trim_start().len();
        let x0 = gpos.x + galley.pos_from_cursor(CCursor::new(char_index(&doc.text, s + lead))).min.x;
        let x1 = gpos.x + galley.pos_from_cursor(CCursor::new(char_index(&doc.text, e))).min.x;
        let y = r.max.y - 1.5;
        let mut pts = Vec::new();
        let mut x = x0;
        let mut up = true;
        while x < x1.max(x0 + 8.0) {
            pts.push(pos2(x, if up { y - 1.5 } else { y + 1.0 }));
            x += 2.5;
            up = !up;
        }
        over.push(Shape::line(pts, Stroke::new(1.0, col)));
    }
    if let Some(i) = fg_idx {
        ui.painter().with_clip_rect(clip).set(i, Shape::Vec(over));
    }

    // la gouttière : numéros de ligne, marques des diagnostics
    let gp = ui.painter().with_clip_rect(gutter);
    gp.rect_filled(gutter, 0.0, pal.editor_bg);
    for (l, r, first) in &row_line {
        if !*first || r.max.y < gutter.min.y || r.min.y > gutter.max.y {
            continue;
        }
        let col = if *l == cur_line { pal.gutter_active } else { pal.gutter_fg };
        gp.text(pos2(gutter.max.x - 18.0, r.center().y), egui::Align2::RIGHT_CENTER, l.to_string(), font.clone(), col);
        if let Some(d) = st.diagnostics.iter().filter(|d| d.line == *l).min_by_key(|d| d.severity as u8) {
            let c = match d.severity {
                Severity::Error => pal.error,
                Severity::Warning => pal.warning,
                Severity::Info => pal.info,
            };
            gp.circle_filled(pos2(gutter.min.x + 8.0, r.center().y), 3.0, c);
        }
    }
    // un clic dans la gouttière sélectionne la ligne
    let gresp = ui.interact(gutter, Id::new(("ks-gutter", doc.id)), Sense::click());
    if gresp.clicked() {
        if let Some(p) = gresp.interact_pointer_pos() {
            if let Some((l, _, _)) = row_line.iter().find(|(_, r, f)| *f && p.y >= r.min.y && p.y < r.max.y) {
                let ls = doc.line_starts();
                let s = ls[l - 1];
                let e = ls.get(*l).copied().unwrap_or(doc.text.len());
                doc.select_bytes(s..e);
                doc.pending_scroll = false;
            }
        }
    }
    // la barre de recherche (en haut à droite, comme VS Code)
    if doc.find.open {
        find_bar(ui, doc, text_rect, pal);
    }
    let _ = Pos2::ZERO;
    out
}

fn find_bar(ui: &mut Ui, doc: &mut Doc, area: Rect, pal: &Palette) {
    let w = 420.0f32.min(area.width() - 20.0);
    let pos = pos2(area.max.x - w - 18.0, area.min.y + 2.0);
    egui::Area::new(Id::new(("ks-find", doc.id))).order(egui::Order::Foreground).fixed_pos(pos).show(ui.ctx(), |ui| {
        egui::Frame::popup(ui.style()).inner_margin(egui::Margin::symmetric(6, 4)).corner_radius(4.0).show(ui, |ui| {
            ui.set_width(w);
            let mut go: Option<bool> = None;
            ui.horizontal(|ui| {
                let caret = if doc.find.replace_open { crate::icons::Icon::ChevronDown } else { crate::icons::Icon::ChevronRight };
                if crate::icons::button(ui, caret, 20.0, "Remplacer").clicked() {
                    doc.find.replace_open = !doc.find.replace_open;
                }
                let te = egui::TextEdit::singleline(&mut doc.find.query).hint_text("Rechercher").desired_width(w - 200.0);
                let r = ui.add(te);
                if doc.find.focus {
                    r.request_focus();
                    doc.find.focus = false;
                }
                if r.has_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                    go = Some(ui.input(|i| i.modifiers.shift));
                }
                if r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                    go = Some(ui.input(|i| i.modifiers.shift));
                    r.request_focus();
                }
                toggle(ui, &mut doc.find.case, "Aa", "Respecter la casse", pal);
                toggle(ui, &mut doc.find.whole, "ab", "Mot entier", pal);
                toggle(ui, &mut doc.find.regex, ".*", "Expression régulière", pal);
                find_matches(doc);
                let n = doc.find.matches.len();
                let label = if let Some(e) = &doc.find.error {
                    egui::RichText::new("erreur").color(pal.error)
                } else if n == 0 {
                    egui::RichText::new(if doc.find.query.is_empty() { String::new() } else { "Aucun résultat".into() }).color(pal.fg_dim)
                } else {
                    egui::RichText::new(format!("{} sur {n}", doc.find.current + 1)).color(pal.fg_dim)
                };
                let lr = ui.label(label);
                if let Some(e) = &doc.find.error {
                    lr.on_hover_text(e.clone());
                }
                if crate::icons::button(ui, crate::icons::Icon::ChevronDown, 20.0, "Suivant (Entrée)").clicked() {
                    go = Some(false);
                }
                if crate::icons::button(ui, crate::icons::Icon::Close, 20.0, "Fermer (Échap)").clicked() {
                    doc.find.open = false;
                }
            });
            if doc.find.replace_open {
                ui.horizontal(|ui| {
                    ui.add_space(24.0);
                    ui.add(egui::TextEdit::singleline(&mut doc.find.replace).hint_text("Remplacer").desired_width(w - 200.0));
                    if ui.button("Remplacer").clicked() {
                        doc.replace_one();
                    }
                    if ui.button("Tout").on_hover_text("Tout remplacer").clicked() {
                        doc.replace_all();
                    }
                });
            }
            if let Some(back) = go {
                doc.find_next(back);
            }
        });
    });
}

fn toggle(ui: &mut Ui, v: &mut bool, label: &str, tip: &str, pal: &Palette) {
    let (r, resp) = ui.allocate_exact_size(vec2(22.0, 20.0), Sense::click());
    if *v {
        ui.painter().rect(r, 3.0, pal.accent.gamma_multiply(0.35), Stroke::new(1.0, pal.accent), egui::StrokeKind::Inside);
    } else if resp.hovered() {
        ui.painter().rect_filled(r, 3.0, pal.hover);
    }
    ui.painter().text(r.center(), egui::Align2::CENTER_CENTER, label, FontId::monospace(11.0), pal.fg);
    if resp.on_hover_text(tip).clicked() {
        *v = !*v;
    }
}

#[allow(dead_code)]
pub fn color_of(sev: Severity, pal: &Palette) -> Color32 {
    match sev {
        Severity::Error => pal.error,
        Severity::Warning => pal.warning,
        Severity::Info => pal.info,
    }
}
