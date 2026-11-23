//! Une ligne d'outil (`session/tool_row.rs` de ClaudeCodeUI) : le genre de l'outil en couleur, sa cible (fichier,
//! commande, motif), son état ou ce qu'il a donné (« 89 lignes », « 3 résultats », « +2 −1 », la durée). Un clic la
//! choisit : l'inspecteur la montre (et s'ouvre). Aussi les aides de dessin des lignes à une seule ligne de texte
//! (texte coupé par « … ») que partagent le registre, les sous-agents et les cartes.

use std::sync::Arc;

use ccui_shared::model::{ToolItem, ToolState};
use ccui_shared::{ask_user, fmt, i18n, json, tool_kinds};
use egui::text::{LayoutJob, TextFormat};
use egui::{Align2, Color32, CornerRadius, FontId, Galley, Pos2, Rect, Response, Sense, Ui, Vec2};
use serde_json::Value;

use crate::state::UiState;
use crate::theme::{self, Tokens};
use crate::widgets::{self, Tone, icon};

/// `ToolRow.Class` : Waiting « wait », Denied / NotRun « void », sinon « sel » quand choisie.
pub fn row_class(state: ToolState, selected: bool) -> Option<&'static str> {
    match state {
        ToolState::Waiting => Some("wait"),
        ToolState::Denied | ToolState::NotRun => Some("void"),
        _ => selected.then_some("sel"),
    }
}

/// La case de droite d'une ligne d'outil (aussi les lignes des sous-agents).
#[derive(Clone, Debug, PartialEq)]
pub enum ToolMeta {
    /// Un texte simple (attend ta décision, « … » en cours, ce que l'outil a donné).
    Plain(String),
    /// Une pastille : « no » (refusé, erreur), « dim » (pas lancé, avec son pourquoi), « ok » (Write qui a créé).
    Badge { class: &'static str, text: String, title: Option<String> },
}

/// `ToolRow.Meta(tool)` selon l'état ; None : case vide.
pub fn meta(tool: &ToolItem) -> Option<ToolMeta> {
    let badge = |class, key: &str, title: Option<String>| Some(ToolMeta::Badge { class, text: i18n::t(key), title });
    match tool.state {
        ToolState::Waiting => Some(ToolMeta::Plain(i18n::t("Tool.Waiting"))),
        ToolState::Denied => badge("no", "Tool.Denied", None),
        ToolState::Error => badge("no", "Tool.Error", None),
        ToolState::NotRun => badge("dim", "Tool.NotRun", Some(i18n::t("Tool.NotRunWhy"))),
        ToolState::Running => Some(ToolMeta::Plain("…".into())),
        ToolState::Done => {
            if tool.name == "Write" && tool.structured.as_ref().and_then(|s| json::str(s, "type")) == Some("create") {
                return badge("ok", "Tool.Created", None);
            }
            Some(done(tool)).filter(|m| !m.is_empty()).map(ToolMeta::Plain)
        }
    }
}

fn num(v: &Value, name: &str) -> Option<i64> {
    json::prop(v, name).and_then(Value::as_f64).map(|d| d as i64)
}

/// `ToolRow.Compute` : ce que donne un outil fini.
fn done(t: &ToolItem) -> String {
    let s = t.structured.as_ref().unwrap_or(&json::UNDEFINED);
    match t.name.as_str() {
        ask_user::TOOL => {
            let qa = ask_user::answered(t.structured.as_ref());
            if !qa.is_empty() {
                return qa.into_iter().map(|(_, a)| a).collect::<Vec<_>>().join(" · ");
            }
        }
        "Read" => {
            let n = json::prop(s, "file")
                .and_then(|f| num(f, "numLines"))
                .or_else(|| t.result_text.as_ref().map(|r| r.trim_end_matches('\n').split('\n').count() as i64))
                .unwrap_or(0);
            return i18n::plural(n, "Tool.Lines");
        }
        "Grep" | "Glob" => {
            let r = match json::str(s, "mode") {
                Some("content") => num(s, "numLines"),
                Some("count") => num(s, "numMatches"),
                _ => num(s, "numFiles"),
            }
            .or_else(|| t.result_text.as_ref().map(|r| r.split('\n').filter(|l| !l.is_empty()).count() as i64))
            .unwrap_or(0);
            return i18n::plural(r, "Tool.Results");
        }
        "Edit" | "MultiEdit" | "Write" => {
            if let Some(Value::Array(patch)) = json::prop(s, "structuredPatch") {
                let lines: Vec<&str> = patch
                    .iter()
                    .filter_map(|h| match json::prop(h, "lines") {
                        Some(Value::Array(l)) => Some(l),
                        _ => None,
                    })
                    .flatten()
                    .map(|l| l.as_str().unwrap_or(""))
                    .collect();
                let (add, del) = (lines.iter().filter(|l| l.starts_with('+')).count(), lines.iter().filter(|l| l.starts_with('-')).count());
                return format!("+{add} \u{2212}{del}");
            }
        }
        _ => {}
    }
    match t.ended_at.map(|e| (e - t.started_at).num_milliseconds()) {
        Some(ms) if ms >= 50 => fmt::dur(ms),
        _ => String::new(),
    }
}

/// La couleur du nom d'un outil (`tool_kinds::color` : des variables CSS) aux couleurs du thème.
pub fn kind_color(t: &Tokens, name: &str) -> Color32 {
    match tool_kinds::color(name) {
        "var(--read)" | "var(--web)" => t.accent_ink,
        "var(--grep)" => t.accent_ink.lerp_to_gamma(t.text_dim, 0.35),
        "var(--edit)" | "var(--write)" => t.success,
        "var(--bash)" => t.warning,
        _ => t.text_dim,
    }
}

/// Une ligne de texte coupée par « … » à `max_w`.
pub fn line_galley(ui: &Ui, text: &str, font: FontId, color: Color32, max_w: f32) -> Arc<Galley> {
    let mut job = LayoutJob::single_section(text.replace('\n', " "), TextFormat { font_id: font, color, ..Default::default() });
    job.wrap.max_width = max_w.max(8.0);
    job.wrap.max_rows = 1;
    job.wrap.break_anywhere = true;
    job.wrap.overflow_character = Some('…');
    ui.ctx().fonts_mut(|f| f.layout_job(job))
}

/// Dessine `g` à gauche de `x`, centré en hauteur dans `r`.
pub fn paint_left(ui: &Ui, r: Rect, x: f32, g: Arc<Galley>) -> f32 {
    let w = g.size().x;
    let pos = Pos2::new(x, r.center().y - g.size().y / 2.0);
    ui.painter().galley(pos, g, Color32::PLACEHOLDER);
    x + w
}

/// La taille d'une case de droite.
pub fn meta_width(ui: &Ui, t: &Tokens, m: &Option<ToolMeta>) -> f32 {
    match m {
        None => 0.0,
        Some(ToolMeta::Plain(s)) => line_galley(ui, s, theme::mono(theme::CAPTION), t.text_faint, 400.0).size().x,
        Some(ToolMeta::Badge { text, .. }) => line_galley(ui, text, theme::medium(theme::CAPTION), t.text, 400.0).size().x + 12.0,
    }
}

/// Dessine la case de droite, alignée à droite sur `right` ; rend sa bulle d'aide éventuelle.
pub fn paint_meta(ui: &Ui, t: &Tokens, r: Rect, right: f32, m: &Option<ToolMeta>) -> Option<(Rect, String)> {
    match m {
        None => None,
        Some(ToolMeta::Plain(s)) => {
            let g = line_galley(ui, s, theme::mono(theme::CAPTION), t.text_faint, 400.0);
            let x = right - g.size().x;
            paint_left(ui, r, x, g);
            None
        }
        Some(ToolMeta::Badge { class, text, title }) => {
            let tone = match *class {
                "no" => Tone::Err,
                "ok" => Tone::Ok,
                _ => Tone::Neutral,
            };
            let c = widgets::tone_color(t, tone);
            let g = line_galley(ui, text, theme::medium(theme::CAPTION), c, 400.0);
            let size = g.size() + Vec2::new(12.0, 2.0);
            let rect = Align2::RIGHT_CENTER.align_size_within_rect(size, Rect::from_min_max(Pos2::new(r.left(), r.top()), Pos2::new(right, r.bottom())));
            ui.painter().rect_filled(rect, CornerRadius::same(8), c.gamma_multiply(0.18));
            paint_left(ui, r, rect.left() + 6.0, g);
            title.clone().map(|tt| (rect, tt))
        }
    }
}

/// Ce que montre une ligne d'outil, déjà calculé (le fil le prépare pour chaque ligne visible).
pub struct RowView<'a> {
    pub tool: &'a ToolItem,
    pub cwd: &'a str,
}

/// Hauteur d'une ligne d'outil.
pub const ROW_H: f32 = 32.0;

/// Une ligne d'outil cliquable (toute la largeur) ; un clic la choisit dans l'inspecteur et ouvre celui-ci.
pub fn show(ui: &mut Ui, t: &Tokens, uis: &mut UiState, v: RowView<'_>) -> Response {
    let tool = v.tool;
    let selected = uis.selected_tool_id.as_deref() == Some(tool.id.as_str());
    let class = row_class(tool.state, selected);
    let w = ui.available_width();
    let (rect, resp) = ui.allocate_exact_size(Vec2::new(w, ROW_H), Sense::click());
    if ui.is_rect_visible(rect) {
        let p = ui.painter();
        match class {
            Some("sel") => {
                p.rect_filled(rect, CornerRadius::ZERO, t.raised);
                p.rect_filled(Rect::from_min_size(rect.min, Vec2::new(2.0, rect.height())), CornerRadius::ZERO, t.accent);
            }
            Some("wait") => {
                p.rect_filled(rect, CornerRadius::ZERO, t.warning.gamma_multiply(0.10));
                p.rect_filled(Rect::from_min_size(rect.min, Vec2::new(2.0, rect.height())), CornerRadius::ZERO, t.warning);
            }
            _ if resp.hovered() => {
                p.rect_filled(rect, CornerRadius::ZERO, t.hover.gamma_multiply(0.6));
            }
            _ => {}
        }
        let void = class == Some("void");
        let fade = |c: Color32| if void { c.gamma_multiply(0.55) } else { c };
        let font = theme::mono(theme::CODE);
        let mut x = rect.left() + 12.0;
        let chev = line_galley(ui, icon(if selected { "chev-down" } else { "chev-right" }), FontId::proportional(theme::CAPTION), t.text_faint, 20.0);
        paint_left(ui, rect, x, chev);
        x += 22.0;
        let kind = line_galley(ui, &tool_kinds::label(&tool.name), theme::medium(theme::CODE), fade(kind_color(t, &tool.name)), 110.0);
        paint_left(ui, rect, x, kind);
        x += 120.0;
        let m = meta(tool);
        let mw = meta_width(ui, t, &m);
        let right = rect.right() - 12.0;
        let target = tool_kinds::target(&tool.name, &tool.input, v.cwd);
        let avail = (right - mw - 12.0 - x).max(10.0);
        let g = line_galley(ui, &target, font, fade(t.text), avail);
        paint_left(ui, rect, x, g);
        if let Some((r, tip)) = paint_meta(ui, t, rect, right, &m)
            && ui.rect_contains_pointer(r)
        {
            resp.clone().on_hover_text(tip);
        }
    }
    let resp = resp.on_hover_text(&tool.name);
    if resp.clicked() {
        uis.selected_tool_id = Some(tool.id.clone());
        uis.selected_phase = None;
        // (la ligne n'a d'effet que dans l'inspecteur : fermé, il avalerait le clic)
        uis.inspector_open = true;
    }
    resp
}

#[cfg(test)]
mod tests {
    use super::*;
    use ccui_shared::i18n::{Lang, set_thread_lang};
    use ccui_shared::model::Time;
    use serde_json::json;

    fn tool(name: &str, state: ToolState, structured: Option<Value>, result: Option<&str>) -> ToolItem {
        ToolItem {
            id: "t".into(),
            name: name.into(),
            input: json!({}),
            parent_tool_use_id: None,
            state,
            result_text: result.map(Into::into),
            structured,
            started_at: Time::default(),
            ended_at: Some(Time::default() + chrono::Duration::milliseconds(4500)),
            task_id: None,
            task_status: None,
            progress: None,
            tokens: 0,
            sub_tool_uses: 0,
            background: false,
            model: None,
        }
    }

    fn plain(t: &ToolItem) -> String {
        match meta(t) {
            Some(ToolMeta::Plain(s)) => s,
            other => panic!("{other:?}"),
        }
    }

    #[test]
    fn metas() {
        set_thread_lang(Some(Lang::En));
        assert_eq!(plain(&tool("Read", ToolState::Done, Some(json!({"file":{"numLines":12.0}})), None)), i18n::plural(12, "Tool.Lines"));
        assert_eq!(plain(&tool("Read", ToolState::Done, None, Some("a\nb\n"))), i18n::plural(2, "Tool.Lines"));
        assert_eq!(plain(&tool("Grep", ToolState::Done, Some(json!({"mode":"count","numMatches":3})), None)), i18n::plural(3, "Tool.Results"));
        assert_eq!(plain(&tool("Glob", ToolState::Done, None, Some("a\n\nb"))), i18n::plural(2, "Tool.Results"));
        let edit = json!({"structuredPatch":[{"lines":[" a","-b","+c","+d"]},{"nolines":1}]});
        assert_eq!(plain(&tool("Edit", ToolState::Done, Some(edit), None)), "+2 \u{2212}1");
        assert_eq!(plain(&tool("Bash", ToolState::Done, None, None)), fmt::dur(4500));
        assert!(matches!(meta(&tool("Write", ToolState::Done, Some(json!({"type":"create"})), None)), Some(ToolMeta::Badge { class: "ok", .. })));
        assert!(matches!(meta(&tool("Bash", ToolState::NotRun, None, None)), Some(ToolMeta::Badge { class: "dim", title: Some(_), .. })));
        assert_eq!(plain(&tool("Bash", ToolState::Running, None, None)), "…");
        let mut quick = tool("Bash", ToolState::Done, None, None);
        quick.ended_at = Some(Time::default());
        assert_eq!(meta(&quick), None);
        set_thread_lang(None);
    }

    #[test]
    fn classes() {
        assert_eq!(row_class(ToolState::Waiting, true), Some("wait"));
        assert_eq!(row_class(ToolState::NotRun, true), Some("void"));
        assert_eq!(row_class(ToolState::Done, true), Some("sel"));
        assert_eq!(row_class(ToolState::Done, false), None);
    }
}
