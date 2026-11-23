//! Les deux formulaires qui prennent la place d'Autoriser / Refuser dans la demande d'autorisation
//! (`session/question_form.rs`, `session/elicit_form.rs` de ClaudeCodeUI).
//!
//! - **Les questions** (`AskUserQuestion`) : un groupe par question (boutons radio, ou cases quand plusieurs choix sont
//!   permis) et une réponse libre « Autre ». Répondre autorise l'outil avec `ask_user::with_answers` ; Passer le
//!   refuse. Touches : ↑↓ d'une option à l'autre, Espace coche, Entrée répond (la première fois hors d'un champ :
//!   va à la première option), Suppr passe. L'aperçu d'une option (Markdown) se montre sous la question tant que la
//!   souris est dessus, sinon celui de l'option choisie.
//! - **Un formulaire MCP** (elicitation) : un champ par propriété de `requested_schema` ; Envoyer accepte avec le
//!   contenu, Refuser décline. En mode URL, comme la boîte de la CLI : l'adresse entière, « Ouvrir dans le
//!   navigateur », puis « J'ai fini, continuer » accepte. Entrée envoie, Suppr décline.

use std::collections::{BTreeSet, HashMap};

use ccui_shared::api::AnswerArgs;
use ccui_shared::ask_user::{self, Question};
use ccui_shared::elicit::{self, Field, FieldKind};
use ccui_shared::model::{Decision, PendingPermission};
use ccui_shared::{json, t};
use egui::{CornerRadius, Frame, Key, Margin, Modifiers, RichText, Stroke, Ui};
use serde_json::Value;

use crate::app::Cx;
use crate::md;
use crate::session::inspector as insp;
use crate::session::permission::{Answer, card_key};
use crate::theme;
use crate::widgets;

// ------------------------------------------------------------------------------------------- les questions

/// L'état d'une question.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Picks {
    pub picked: BTreeSet<usize>,
    pub other_on: bool,
    pub other: String,
}

/// `Pick(q, o, on)` : `o == options.len()` est « Autre ». Un bouton radio ne dit qu'être choisi ; une case, son état.
pub fn pick(q: &Question, p: &mut Picks, o: usize, on: bool) {
    if !q.multi {
        p.picked.clear();
        p.other_on = false;
    }
    if o == q.options.len() {
        p.other_on = on;
    } else if on {
        p.picked.insert(o);
    } else {
        p.picked.remove(&o);
    }
}

/// La réponse à une question ("" = pas de réponse).
pub fn value(q: &Question, p: &Picks) -> String {
    let picked: Vec<usize> = p.picked.iter().copied().collect();
    ask_user::answer(q, &picked, p.other_on.then_some(p.other.as_str()))
}

/// L'aperçu montré : celui de l'option survolée, sinon de la première choisie.
pub fn preview(q: &Question, focused: Option<usize>, p: &Picks) -> Option<String> {
    let o = focused.filter(|f| *f < q.options.len()).or_else(|| p.picked.iter().next().copied())?;
    q.options.get(o)?.preview.clone()
}

/// La place suivante du curseur clavier (question, option) : ↑↓ dans une question, puis à la question voisine.
pub fn step(questions: &[Question], cur: (usize, usize), down: bool) -> (usize, usize) {
    let (q, o) = cur;
    let n = questions.get(q).map_or(1, |x| x.options.len() + 1);
    if down {
        if o + 1 < n {
            (q, o + 1)
        } else if q + 1 < questions.len() {
            (q + 1, 0)
        } else {
            cur
        }
    } else if o > 0 {
        (q, o - 1)
    } else if q > 0 {
        (q - 1, questions[q - 1].options.len())
    } else {
        cur
    }
}

#[derive(Default)]
pub struct QuestionState {
    questions: Vec<Question>,
    picks: Vec<Picks>,
    hovered: Vec<Option<usize>>,
    /// L'option qu'ont atteinte les flèches.
    cursor: Option<(usize, usize)>,
}

impl QuestionState {
    pub fn new(p: &PendingPermission) -> QuestionState {
        let questions = ask_user::parse(&p.input);
        let n = questions.len();
        QuestionState { questions, picks: vec![Picks::default(); n], hovered: vec![None; n], cursor: None }
    }

    fn complete(&self) -> bool {
        !self.questions.is_empty() && self.questions.iter().zip(&self.picks).all(|(q, p)| !value(q, p).is_empty())
    }
}

fn args(rid: &str, d: Decision, input: Option<Value>) -> AnswerArgs {
    AnswerArgs { request_id: rid.to_string(), decision: d, mode: None, message: None, input, destination: None }
}

/// Une option encadrée (`.opt`, `.opt.on`) ; la ligne du curseur clavier est soulignée de l'accent.
fn opt_frame<R>(ui: &mut Ui, t: &theme::Tokens, on: bool, cursor: bool, add: impl FnOnce(&mut Ui) -> R) -> egui::InnerResponse<R> {
    let stroke = if cursor {
        Stroke::new(1.5, t.accent)
    } else if on {
        Stroke::new(1.0, t.accent.gamma_multiply(0.7))
    } else {
        Stroke::new(1.0, t.card_border)
    };
    Frame::new()
        .fill(if on { t.accent_soft } else { t.card })
        .stroke(stroke)
        .corner_radius(CornerRadius::same(theme::R))
        .inner_margin(Margin::symmetric(10, 7))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            add(ui)
        })
}

pub fn question(ui: &mut Ui, cx: &mut Cx, st: &mut QuestionState, ans: &mut Answer, since: f64, id: &str, p: &PendingPermission) {
    let t = cx.t;
    let n = st.questions.len();
    let mut submit = false;
    let mut skip = false;
    let extra = [(Modifiers::NONE, Key::ArrowDown, "ArrowDown"), (Modifiers::NONE, Key::ArrowUp, "ArrowUp"), (Modifiers::NONE, Key::Space, "Space")];
    match card_key(ui, cx, since, &extra) {
        Some("Enter") if n > 0 => match st.cursor {
            None => st.cursor = Some((0, 0)),
            Some(_) => submit = true,
        },
        Some("Delete") => skip = true,
        Some(k @ ("ArrowDown" | "ArrowUp")) if n > 0 => st.cursor = Some(st.cursor.map_or((0, 0), |c| step(&st.questions, c, k == "ArrowDown"))),
        Some("Space") => {
            if let Some((qi, o)) = st.cursor {
                let q = &st.questions[qi];
                let on = if o == q.options.len() { st.picks[qi].other_on } else { st.picks[qi].picked.contains(&o) };
                pick(q, &mut st.picks[qi], o, !q.multi || !on);
            }
        }
        _ => {}
    }

    ui.add_enabled_ui(!ans.answered, |ui| {
        for qi in 0..n {
            let q = st.questions[qi].clone();
            let count = q.options.len();
            ui.add_space(4.0);
            if !q.header.is_empty() {
                widgets::cap(ui, &t, &q.header);
            }
            ui.add(egui::Label::new(RichText::new(&q.text).font(theme::semibold(theme::SMALL)).color(t.text)).wrap());
            let mut hovered = None;
            for o in 0..=count {
                let other = o == count;
                let on = if other { st.picks[qi].other_on } else { st.picks[qi].picked.contains(&o) };
                let cursor = st.cursor == Some((qi, o));
                let r = opt_frame(ui, &t, on, cursor, |ui| {
                    let mut change = None;
                    ui.horizontal(|ui| {
                        let mark = if q.multi {
                            let mut v = on;
                            let r = ui.checkbox(&mut v, "");
                            r.changed().then_some(v)
                        } else {
                            ui.radio(on, "").clicked().then_some(true)
                        };
                        if let Some(v) = mark {
                            change = Some(v);
                        }
                        if other {
                            let r = ui.add(
                                egui::TextEdit::singleline(&mut st.picks[qi].other)
                                    .hint_text(t!("Ask.OtherPlaceholder"))
                                    .desired_width(f32::INFINITY),
                            );
                            // (taper une réponse « Autre » la choisit ; à choix unique, les options se décochent)
                            if r.changed() && !st.picks[qi].other.trim().is_empty() && !st.picks[qi].other_on {
                                change = Some(true);
                            }
                            if r.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter)) {
                                submit = true;
                            }
                        } else {
                            let o = &q.options[o];
                            ui.vertical(|ui| {
                                ui.add(egui::Label::new(RichText::new(&o.label).font(theme::semibold(theme::LABEL)).color(t.text)).wrap().selectable(false));
                                if !o.description.is_empty() {
                                    ui.add(egui::Label::new(RichText::new(&o.description).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim)).wrap().selectable(false));
                                }
                            });
                        }
                    });
                    change
                });
                let area = ui.interact(r.response.rect, ui.id().with(("ask-opt", qi, o)), egui::Sense::click());
                if area.hovered() {
                    hovered = Some(o);
                }
                let mut change = r.inner;
                if area.clicked() && !other {
                    change = Some(!q.multi || !on);
                }
                if let Some(v) = change {
                    pick(&q, &mut st.picks[qi], o, v);
                    st.cursor = Some((qi, o));
                }
            }
            st.hovered[qi] = hovered;
            if let Some(pv) = preview(&q, hovered.or(st.cursor.filter(|c| c.0 == qi).map(|c| c.1)), &st.picks[qi]) {
                Frame::new().fill(t.raised).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::same(10)).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    md::show(ui, &t, &pv);
                });
            }
        }
        ui.add_space(4.0);
        ui.horizontal(|ui| {
            let ok = st.complete();
            if ui.add_enabled(ok, egui::Button::new(RichText::new(format!("{}  ⏎", t!("Ask.Submit"))).color(t.on_accent).font(theme::medium(theme::SMALL))).fill(t.accent)).clicked() {
                submit = true;
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if widgets::danger(ui, &t, &t!("Ask.Skip")).clicked() {
                    skip = true;
                }
            });
        });
    });
    ans.error_box(ui, &t);
    insp::keys(
        ui,
        &t,
        &[
            ("↑↓".into(), t!("Ask.Hint.Move")),
            (t!("Key.Space"), t!("Ask.Hint.Pick")),
            ("⏎".into(), t!("Ask.Hint.Submit")),
            (t!("Key.Del"), t!("Ask.Hint.Skip")),
        ],
    );

    if submit && !ans.answered && st.complete() {
        let answers: Vec<(String, String)> = st.questions.iter().zip(&st.picks).map(|(q, p)| (q.text.clone(), value(q, p))).collect();
        ans.send(cx.be, id, args(&p.request_id, Decision::Allow, Some(ask_user::with_answers(&p.input, &answers))));
    } else if skip {
        ans.send(cx.be, id, args(&p.request_id, Decision::Deny, None));
    }
}

// ------------------------------------------------------------------------------------------- le formulaire MCP

/// Le texte de départ de chaque champ : un booléen "true"/"false" selon son défaut, les autres leur défaut ou "".
pub fn initial(fields: &[Field]) -> HashMap<String, String> {
    fields
        .iter()
        .map(|f| {
            let v = match f.kind {
                FieldKind::Boolean => if f.default.as_deref() == Some("true") { "true" } else { "false" }.to_string(),
                _ => f.default.clone().unwrap_or_default(),
            };
            (f.key.clone(), v)
        })
        .collect()
}

/// `display_name ?? mcp_server_name ?? "MCP"`.
pub fn server(input: &Value) -> String {
    json::str(input, "display_name").or_else(|| json::str(input, "mcp_server_name")).unwrap_or("MCP").to_string()
}

#[derive(Default)]
pub struct ElicitState {
    fields: Vec<Field>,
    text: HashMap<String, String>,
    picks: HashMap<String, BTreeSet<String>>,
    url: bool,
    link: Option<String>,
    opened: bool,
}

impl ElicitState {
    pub fn new(p: &PendingPermission) -> ElicitState {
        let fields = elicit::parse(&p.input);
        ElicitState {
            text: initial(&fields),
            picks: fields.iter().map(|f| (f.key.clone(), BTreeSet::new())).collect(),
            fields,
            url: elicit::is_url(&p.input),
            link: elicit::url(&p.input),
            opened: false,
        }
    }
}

fn label_req(ui: &mut Ui, t: &theme::Tokens, f: &Field) {
    let text = if f.required { format!("{} *", f.label) } else { f.label.clone() };
    ui.label(RichText::new(text).font(theme::medium(theme::CAPTION)).color(t.text_dim));
}

pub fn elicit(ui: &mut Ui, cx: &mut Cx, st: &mut ElicitState, ans: &mut Answer, since: f64, id: &str, p: &PendingPermission) {
    let t = cx.t;
    let mut submit = false;
    let mut decline = false;
    match card_key(ui, cx, since, &[]) {
        Some("Enter") => submit = true,
        Some("Delete") => decline = true,
        _ => {}
    }
    let title = p
        .info
        .as_ref()
        .and_then(|i| i.title.clone())
        .filter(|t| !t.is_empty())
        .unwrap_or_else(|| t!("Elicit.Asks", server(&p.input)));
    insp::h3(ui, &t, &title);
    if let Some(m) = p.description.clone().filter(|m| !m.is_empty()) {
        insp::para(ui, &t, &m);
    }
    ui.add_enabled_ui(!ans.answered, |ui| {
        if st.url {
            match st.link.clone() {
                Some(href) => {
                    insp::pre(ui, &t, ("el-url", &p.request_id), &href);
                    let k = if st.opened { "Elicit.OpenAgain" } else { "Elicit.Open" };
                    if ui.button(format!("{} {}", widgets::icon("arrow-right"), t!(k))).clicked() {
                        crate::host::open(&href);
                        st.opened = true;
                    }
                    if st.opened {
                        insp::para(ui, &t, &t!("Elicit.Waiting"));
                    }
                }
                None => insp::errbox(ui, &t, &t!("Elicit.BadUrl")),
            }
        } else {
            for f in st.fields.clone() {
                match f.kind {
                    FieldKind::Boolean => {
                        let mut on = st.text.get(&f.key).is_some_and(|v| v == "true");
                        if ui.checkbox(&mut on, RichText::new(&f.label).font(theme::semibold(theme::LABEL))).changed() {
                            st.text.insert(f.key.clone(), if on { "true" } else { "false" }.into());
                        }
                        if let Some(d) = &f.description {
                            widgets::dim(ui, &t, d);
                        }
                    }
                    FieldKind::Multi => {
                        label_req(ui, &t, &f);
                        if let Some(d) = &f.description {
                            widgets::dim(ui, &t, d);
                        }
                        for (value, label) in &f.options {
                            let set = st.picks.entry(f.key.clone()).or_default();
                            let mut on = set.contains(value);
                            if ui.checkbox(&mut on, label).changed() {
                                if on {
                                    set.insert(value.clone());
                                } else {
                                    set.remove(value);
                                }
                            }
                        }
                    }
                    FieldKind::Enum => {
                        label_req(ui, &t, &f);
                        let cur = st.text.get(&f.key).cloned().unwrap_or_default();
                        let shown = f.options.iter().find(|o| o.0 == cur).map_or("—".to_string(), |o| o.1.clone());
                        egui::ComboBox::from_id_salt(("el-enum", &p.request_id, &f.key)).selected_text(shown).show_ui(ui, |ui| {
                            let v = st.text.entry(f.key.clone()).or_default();
                            ui.selectable_value(v, String::new(), "—");
                            for (value, label) in &f.options {
                                ui.selectable_value(v, value.clone(), label);
                            }
                        });
                    }
                    _ => {
                        label_req(ui, &t, &f);
                        let v = st.text.entry(f.key.clone()).or_default();
                        let hint = match (f.min, f.max) {
                            (Some(a), Some(b)) => format!("{a} – {b}"),
                            _ => String::new(),
                        };
                        let r = ui.add(egui::TextEdit::singleline(v).hint_text(hint).desired_width(f32::INFINITY));
                        if r.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter)) {
                            submit = true;
                        }
                        if let Some(d) = &f.description {
                            widgets::dim(ui, &t, d);
                        }
                    }
                }
            }
        }
        ui.add_space(4.0);
        ui.horizontal(|ui| {
            let label = if st.url { t!(if st.opened { "Elicit.Done" } else { "Elicit.Accept" }) } else { t!("Ask.Submit") };
            if widgets::primary(ui, &t, &format!("{label}  ⏎")).clicked() {
                submit = true;
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if widgets::danger(ui, &t, &t!("Elicit.Decline")).clicked() {
                    decline = true;
                }
            });
        });
    });
    ans.error_box(ui, &t);
    insp::keys(
        ui,
        &t,
        &[
            ("⏎".into(), t!(if st.url { "Elicit.HintAccept" } else { "Ask.Hint.Submit" })),
            (t!("Key.Del"), t!("Elicit.HintDecline")),
        ],
    );

    if submit && !ans.answered {
        let mut content = None;
        if !st.url {
            match elicit::content(&st.fields, &st.text, &st.picks) {
                (Some(c), _) => content = Some(Value::Object(c)),
                (None, invalid) => {
                    ans.error = Some(t!("Elicit.Invalid", invalid.unwrap_or_default()));
                    return;
                }
            }
        }
        ans.send(cx.be, id, args(&p.request_id, Decision::Allow, content));
    } else if decline {
        ans.send(cx.be, id, args(&p.request_id, Decision::Deny, None));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ccui_shared::ask_user::AskOption;
    use serde_json::json;

    fn q(multi: bool) -> Question {
        let opt = |l: &str, pv: Option<&str>| AskOption { label: l.into(), description: String::new(), preview: pv.map(str::to_string) };
        Question { text: "Which?".into(), header: String::new(), options: vec![opt("A", Some("pa")), opt("B", None)], multi }
    }

    #[test]
    fn picking() {
        let single = q(false);
        let mut p = Picks::default();
        pick(&single, &mut p, 1, true);
        pick(&single, &mut p, 0, true);
        assert_eq!(value(&single, &p), "A");
        pick(&single, &mut p, 2, true); // Autre
        p.other = " mine ".into();
        assert_eq!(value(&single, &p), "mine");
        assert!(p.picked.is_empty());

        let multi = q(true);
        let mut m = Picks::default();
        pick(&multi, &mut m, 1, true);
        pick(&multi, &mut m, 0, true);
        assert_eq!(value(&multi, &m), "A, B");
        pick(&multi, &mut m, 0, false);
        assert_eq!(value(&multi, &m), "B");
        assert_eq!(value(&multi, &Picks::default()), "");
    }

    #[test]
    fn previews() {
        let single = q(false);
        let mut p = Picks::default();
        assert_eq!(preview(&single, None, &p), None);
        assert_eq!(preview(&single, Some(0), &p).as_deref(), Some("pa"));
        p.picked.insert(0);
        assert_eq!(preview(&single, Some(1), &p), None);
        assert_eq!(preview(&single, Some(2), &p).as_deref(), Some("pa")); // « Autre » survolé : celui de l'option choisie
    }

    #[test]
    fn cursor_moves() {
        let qs = vec![q(false), q(true)];
        assert_eq!(step(&qs, (0, 0), true), (0, 1));
        assert_eq!(step(&qs, (0, 2), true), (1, 0));
        assert_eq!(step(&qs, (1, 2), true), (1, 2));
        assert_eq!(step(&qs, (1, 0), false), (0, 2));
        assert_eq!(step(&qs, (0, 0), false), (0, 0));
    }

    #[test]
    fn defaults_and_server() {
        let req = json!({"mcp_server_name":"gh","requested_schema":{"properties":{
            "ok":{"type":"boolean","default":true},"n":{"type":"string","default":"x"},"m":{"type":"string"}}}});
        let t = initial(&elicit::parse(&req));
        assert_eq!(t["ok"], "true");
        assert_eq!(t["n"], "x");
        assert_eq!(t["m"], "");
        assert_eq!(server(&req), "gh");
        assert_eq!(server(&json!({"display_name":"GitHub","mcp_server_name":"gh"})), "GitHub");
        assert_eq!(server(&json!({})), "MCP");
    }
}
