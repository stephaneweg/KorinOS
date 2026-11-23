//! Les blocs du fil autres que les messages : le registre d'une suite d'outils (`ledger.rs` : « 3 outils · 2,4 s »,
//! replié sauf le dernier), les hooks (`hook_activity.rs`), les replis (réflexion, compaction), la carte d'erreur de
//! l'API (`api_error_card.rs`, avec la reconnexion de `sign_in.rs`), le bandeau de plantage (`crash_banner.rs`), la
//! carte d'un fichier envoyé (`file_card.rs`), le texte en cours (`streaming_tail.rs`) et la liste des tâches de
//! l'agent (`todo_panel.rs`, entre le fil et le composeur).

use std::sync::{Arc, Mutex};

use ccui_shared::api_errors::{ApiError, ApiErrorKind};
use ccui_shared::model::{HookEvt, Item, Time, ToolItem};
use ccui_shared::session::SessionMeta;
use ccui_shared::todo_list::{self, TodoStatus};
use ccui_shared::{fmt, i18n, json, t, tool_kinds};
use egui::text::{LayoutJob, TextFormat};
use egui::{Color32, CornerRadius, FontId, Frame, Label, Margin, Rect, Response, RichText, ScrollArea, Sense, Stroke, Ui, Vec2};

use super::tool_row::{self, line_galley, paint_left};
use crate::backend::{Backend, Task};
use crate::state::{SessionHandle, UiState};
use crate::theme::{self, Tokens};
use crate::widgets::{self, Tone, icon};

// ------------------------------------------------------------------------------------------- commun

/// Une ligne d'en-tête cliquable (chevron, puis ce que `add` dessine) ; rend le clic.
pub fn fold_header(ui: &mut Ui, t: &Tokens, open: bool, fill: Option<Color32>, add: impl FnOnce(&mut Ui)) -> Response {
    let inner = Frame::new().fill(fill.unwrap_or(Color32::TRANSPARENT)).inner_margin(Margin::symmetric(12, 6)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.horizontal(|ui| {
            ui.spacing_mut().item_spacing.x = 6.0;
            ui.add(Label::new(RichText::new(icon(if open { "chev-down" } else { "chev-right" })).color(t.text_faint).font(FontId::proportional(theme::CAPTION))).selectable(false));
            add(ui);
        });
    });
    let r = ui.interact(inner.response.rect, inner.response.id.with("fold"), Sense::click());
    if r.hovered() {
        ui.ctx().set_cursor_icon(egui::CursorIcon::PointingHand);
    }
    r
}

/// Un repli encadré (`.fold` : la réflexion, la compaction) : l'en-tête, et le corps quand il est ouvert.
pub fn fold(ui: &mut Ui, t: &Tokens, open: &mut bool, head: impl FnOnce(&mut Ui), body: impl FnOnce(&mut Ui)) {
    Frame::new().stroke(Stroke::new(1.0, t.card_border)).corner_radius(CornerRadius::same(theme::R)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        if fold_header(ui, t, *open, None, head).clicked() {
            *open = !*open;
        }
        if *open {
            Frame::new().inner_margin(Margin { left: 12, right: 12, top: 4, bottom: 10 }).show(ui, |ui| {
                ui.set_width(ui.available_width());
                body(ui);
            });
        }
    });
}

fn small(text: impl Into<String>, t: &Tokens) -> RichText {
    RichText::new(text.into()).font(theme::medium(theme::CAPTION)).color(t.text_dim)
}

// ------------------------------------------------------------------------------------------- le registre

/// `Ledger.Head` : « n outils », puis « · durée » quand la somme des durées (un outil en cours compte 0) ≥ 50 ms.
pub fn head(tools: &[(Time, Option<Time>)]) -> String {
    let n = i18n::plural(tools.len() as i64, "Tools");
    let ms: i64 = tools.iter().map(|(s, e)| (e.unwrap_or(*s) - *s).num_milliseconds()).sum();
    if ms >= 50 { format!("{n} · {}", fmt::dur(ms)) } else { n }
}

/// Une suite d'outils de premier niveau (une ligne d'outil chacun), repliable : `open` (le dernier registre est ouvert
/// par défaut, le clic sur l'en-tête décide ensuite). Rend true quand l'en-tête a été cliqué.
pub fn ledger(ui: &mut Ui, t: &Tokens, uis: &mut UiState, h: &SessionHandle, tools: &[usize], open: bool) -> bool {
    let rows: Vec<&ToolItem> = tools.iter().filter_map(|&i| h.tool_at(i)).collect();
    let title = head(&rows.iter().map(|t| (t.started_at, t.ended_at)).collect::<Vec<_>>());
    let mut clicked = false;
    Frame::new().stroke(Stroke::new(1.0, t.card_border)).corner_radius(CornerRadius::same(theme::R_LG)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        let fill = t.chrome.lerp_to_gamma(t.canvas, 0.4);
        let r = fold_header(ui, t, open, Some(fill), |ui| {
            ui.add(Label::new(RichText::new(&title).font(theme::mono(theme::CAPTION)).color(t.text_faint)).selectable(false));
        });
        clicked = r.clicked();
        if open {
            for (k, tool) in rows.iter().enumerate() {
                let line = ui.available_rect_before_wrap();
                ui.painter().hline(line.x_range(), line.top(), Stroke::new(1.0, if k == 0 { t.card_border } else { t.separator }));
                tool_row::show(ui, t, uis, tool_row::RowView { tool, cwd: &h.meta.cwd });
            }
        }
    });
    clicked
}

// ------------------------------------------------------------------------------------------- les hooks

/// `HookActivity.Label` : succès → sortie, erreur code 2 → bloquante, erreur → échec (code), sinon tel quel.
pub fn hook_label(h: &HookEvt) -> String {
    match (h.outcome.as_str(), h.exit_code) {
        ("success", _) => t!("Hook.Output"),
        ("error", Some(2)) => t!("Hook.Blocking"),
        ("error", code) => t!("Hook.Failed", code.map_or_else(|| "—".to_string(), |c| c.to_string())),
        (other, _) => other.to_string(),
    }
}

/// Les noms groupés dans l'ordre : « PreToolUse ×2, Stop ».
pub fn hook_names(hooks: &[&HookEvt]) -> String {
    let mut groups: Vec<(&str, usize)> = Vec::new();
    for h in hooks {
        match groups.iter_mut().find(|(n, _)| *n == h.name) {
            Some((_, c)) => *c += 1,
            None => groups.push((&h.name, 1)),
        }
    }
    groups.iter().map(|(n, c)| if *c > 1 { format!("{n} ×{c}") } else { n.to_string() }).collect::<Vec<_>>().join(", ")
}

/// Des hooks qui se suivent (échoués, bloquants ou bavards), repliés ; ouverts d'office quand l'un a échoué.
pub fn hooks(ui: &mut Ui, t: &Tokens, hooks: &[&HookEvt], open: &mut Option<bool>) {
    let first_error = hooks.iter().find(|h| h.outcome == "error");
    let is_open = open.unwrap_or(first_error.is_some());
    let border = if first_error.is_some() { t.danger.gamma_multiply(0.4) } else { t.card_border };
    Frame::new().stroke(Stroke::new(1.0, border)).corner_radius(CornerRadius::same(theme::R)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        let r = fold_header(ui, t, is_open, None, |ui| {
            ui.add(Label::new(RichText::new(t!("Hook.Title").to_uppercase()).font(theme::semibold(theme::CAPTION - 1.0)).color(t.text_dim)).selectable(false));
            let names = hook_names(hooks);
            let w = (ui.available_width() - 140.0).max(40.0);
            ui.add(Label::new(line_galley(ui, &names, theme::mono(theme::CAPTION), t.text_faint, w)).selectable(false));
            if let Some(e) = first_error {
                widgets::pill(ui, t, &hook_label(e), Tone::Err);
            }
        });
        if r.clicked() {
            *open = Some(!is_open);
        }
        if is_open {
            for h in hooks {
                let line = ui.available_rect_before_wrap();
                ui.painter().hline(line.x_range(), line.top(), Stroke::new(1.0, t.separator));
                Frame::new().inner_margin(Margin::symmetric(12, 8)).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.horizontal(|ui| {
                        ui.label(RichText::new(&h.name).font(theme::mono(theme::CAPTION)).color(t.text_dim));
                        ui.label(RichText::new(hook_label(h)).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                    });
                    if !h.output.is_empty() {
                        ui.add_space(4.0);
                        ScrollArea::vertical().id_salt(("hook-out", &h.name, h.output.len())).max_height(240.0).show(ui, |ui| {
                            ui.add(Label::new(RichText::new(&h.output).font(theme::mono(theme::CAPTION)).color(t.text_dim)).selectable(true).wrap());
                        });
                    }
                });
            }
        }
    });
}

// ------------------------------------------------------------------------------------------- notices

/// Une ligne encadrée (`.notice`) : reprise, tâche programmée, notice de la CLI ; `bad` : bord rouge.
pub fn notice(ui: &mut Ui, t: &Tokens, bad: bool, add: impl FnOnce(&mut Ui)) {
    let border = if bad { t.danger.gamma_multiply(0.4) } else { t.card_border };
    Frame::new().stroke(Stroke::new(1.0, border)).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::symmetric(12, 6)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.horizontal_wrapped(|ui| {
            ui.spacing_mut().item_spacing.x = 6.0;
            add(ui);
        });
    });
}

/// Le texte d'une notice.
pub fn notice_text(t: &Tokens, text: &str) -> RichText {
    RichText::new(text).font(FontId::proportional(theme::CAPTION + 0.5)).color(t.text_dim)
}

/// La séparation d'un jour (`.day`) : la date entre deux traits.
pub fn day(ui: &mut Ui, t: &Tokens, text: &str) {
    let g = line_galley(ui, text, theme::medium(theme::CAPTION), t.text_faint, 400.0);
    let (rect, _) = ui.allocate_exact_size(Vec2::new(ui.available_width(), g.size().y + 4.0), Sense::hover());
    let w = g.size().x;
    let cx = rect.center().x;
    let y = rect.center().y;
    ui.painter().hline(rect.left()..=cx - w / 2.0 - 12.0, y, Stroke::new(1.0, t.separator));
    ui.painter().hline(cx + w / 2.0 + 12.0..=rect.right(), y, Stroke::new(1.0, t.separator));
    paint_left(ui, rect, cx - w / 2.0, g);
}

// ------------------------------------------------------------------------------------------- erreurs

/// Le cadre d'une carte d'erreur (`.crash`).
fn crash_frame(ui: &mut Ui, t: &Tokens, add: impl FnOnce(&mut Ui)) {
    Frame::new()
        .fill(t.danger.gamma_multiply(0.06))
        .stroke(Stroke::new(1.0, t.danger.gamma_multiply(0.45)))
        .corner_radius(CornerRadius::same(theme::R_LG))
        .inner_margin(Margin::same(16))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            add(ui);
        });
}

fn raw_box(ui: &mut Ui, t: &Tokens, raw: &str) {
    if raw.is_empty() {
        return;
    }
    Frame::new().fill(t.code_bg).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::same(8)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ScrollArea::vertical().id_salt(("raw", raw.len())).max_height(200.0).show(ui, |ui| {
            ui.add(Label::new(RichText::new(raw).font(theme::mono(theme::CAPTION)).color(t.text_dim)).selectable(true).wrap());
        });
    });
}

/// La reconnexion d'une session (`/login` par stream-json) : le lien de la page de connexion, puis l'attente du
/// retour du navigateur.
#[derive(Default)]
pub struct SignIn {
    url: Arc<Mutex<Option<String>>>,
    task: Option<Task<Result<(), String>>>,
    note: Option<String>,
}

impl SignIn {
    fn busy(&self) -> bool {
        self.task.is_some()
    }

    fn poll(&mut self) {
        if let Some(r) = self.task.as_ref().and_then(Task::take) {
            self.task = None;
            *self.url.lock().unwrap() = None;
            self.note = Some(match r {
                Ok(()) => t!("Account.SignedIn"),
                Err(e) => t!("Session.ActionFailed", e.trim_end()),
            });
        }
    }

    fn start(&mut self, be: &Backend, id: &str) {
        self.note = None;
        let s = match be.session(id) {
            Ok(s) => s,
            Err(e) => {
                self.note = Some(t!("Session.ActionFailed", e));
                return;
            }
        };
        let url = self.url.clone();
        self.task = Some(be.task(async move {
            let u = s.start_sign_in().await?.ok_or_else(|| t!("Account.NoUrl"))?;
            *url.lock().unwrap() = Some(u);
            s.finish_sign_in().await
        }));
    }

    /// Le bouton (ou le lien de la page, pendant l'attente) et le mot de la fin.
    pub fn show(&mut self, ui: &mut Ui, t: &Tokens, be: &Backend, id: &str, primary: bool) {
        self.poll();
        let url = self.url.lock().unwrap().clone();
        match url {
            Some(u) => {
                ui.label(RichText::new(t!("Account.Waiting")).color(t.text_dim));
                if widgets::primary(ui, t, &t!("Ext.OpenSignIn")).clicked() {
                    crate::host::open(&u);
                }
            }
            None => {
                let label = t!("Account.SignIn");
                let r = ui.add_enabled_ui(!self.busy(), |ui| if primary { widgets::primary(ui, t, &label) } else { ui.button(label) }).inner;
                if r.clicked() {
                    self.start(be, id);
                }
            }
        }
        if let Some(n) = &self.note {
            ui.label(RichText::new(n).color(t.text_dim));
        }
    }
}

/// Une erreur de l'API pendant un tour (`ApiErrorCard`) : titre, conseil, texte brut (copiable) ; une erreur
/// d'authentification propose de se reconnecter par la session tant qu'elle a son process.
pub fn api_error(ui: &mut Ui, t: &Tokens, be: &Backend, h: &SessionHandle, sign_in: &mut SignIn, error: &ApiError) {
    crash_frame(ui, t, |ui| {
        ui.label(RichText::new(error.title()).font(theme::semibold(theme::SECTION)).color(t.text));
        ui.add(Label::new(RichText::new(error.hint()).color(t.text_dim)).wrap());
        raw_box(ui, t, &error.raw);
        ui.horizontal(|ui| {
            if ui.button(t!("Error.Copy")).clicked() {
                ui.ctx().copy_text(error.raw.clone());
            }
        });
        if error.kind == ApiErrorKind::Auth && h.meta.has_process {
            ui.horizontal_wrapped(|ui| sign_in.show(ui, t, be, &h.id, true));
        }
    });
}

/// Le bandeau d'une session plantée ou qui n'a pas démarré (`CrashBanner`) : la raison, Relancer (et reprendre le
/// tour coupé), Copier l'erreur. Rend true quand Relancer est cliqué.
pub fn crash(ui: &mut Ui, t: &Tokens, m: &SessionMeta, restarting: bool) -> bool {
    let mut restart = false;
    crash_frame(ui, t, |ui| {
        let failed = m.start_failed;
        ui.label(RichText::new(t!(if failed { "Crash.StartFailed.Title" } else { "Crash.Title" })).font(theme::semibold(theme::SECTION)).color(t.text));
        ui.add(Label::new(RichText::new(t!(if failed { "Crash.StartFailed.Text" } else { "Crash.Text" })).color(t.text_dim)).wrap());
        let exit = m.exit_text.clone().unwrap_or_default();
        raw_box(ui, t, &exit);
        ui.horizontal(|ui| {
            let label = t!(if m.crashed_mid_turn { "Crash.RestartContinue" } else { "Crash.Restart" });
            ui.add_enabled_ui(!restarting, |ui| {
                if restarting {
                    ui.spinner();
                }
                if widgets::primary(ui, t, &format!("{} {label}", icon("refresh"))).clicked() {
                    restart = true;
                }
            });
            if ui.button(t!("Error.Copy")).clicked() {
                ui.ctx().copy_text(exit.clone());
            }
        });
    });
    restart
}

// ------------------------------------------------------------------------------------------- fichier envoyé

/// `Path.GetFileName`.
pub fn file_name(path: &str) -> &str {
    path.rsplit(['/', '\\']).next().unwrap_or(path)
}

fn fully_qualified(p: &str) -> bool {
    let b = p.as_bytes();
    p.starts_with('/') && !p.starts_with("/\\")
        || p.starts_with("\\\\")
        || (b.len() >= 3 && b[0].is_ascii_alphabetic() && b[1] == b':' && (b[2] == b'\\' || b[2] == b'/'))
}

/// Le chemin complet d'un fichier envoyé (relatif au dossier de la session ; `.` et `..` repliés).
pub fn full_path(p: &str, cwd: &str) -> String {
    if fully_qualified(p) {
        return p.to_string();
    }
    let sep = if cwd.contains('\\') && !cwd.contains('/') { '\\' } else { '/' };
    let mut parts: Vec<&str> = Vec::new();
    for seg in cwd.split(['/', '\\']).chain(p.split(['/', '\\'])) {
        match seg {
            "" | "." => {}
            ".." => {
                if parts.len() > usize::from(cwd.as_bytes().get(1) == Some(&b':')) {
                    parts.pop();
                }
            }
            s => parts.push(s),
        }
    }
    let joined = parts.join(&sep.to_string());
    if sep == '/' { format!("/{joined}") } else { joined }
}

/// Les chemins envoyés : `input.files` (chaînes non vides).
pub fn files(input: &serde_json::Value, cwd: &str) -> Vec<String> {
    match json::prop(input, "files") {
        Some(serde_json::Value::Array(a)) => a.iter().filter_map(serde_json::Value::as_str).filter(|x| !x.is_empty()).map(|x| full_path(x, cwd)).collect(),
        _ => Vec::new(),
    }
}

/// Montre `path` dans le gestionnaire de fichiers : son dossier, ouvert par la fenêtre (le fichier n'est jamais ouvert
/// pour toi).
pub fn reveal(path: &str) -> Result<(), String> {
    ccui_core::folder::reveal_check(path)?;
    let dir = std::path::Path::new(path).parent().map(|p| p.to_string_lossy().into_owned()).unwrap_or_else(|| path.to_string());
    crate::host::open(&dir);
    Ok(())
}

/// La carte de `SendUserFile` : les fichiers (Afficher dans le dossier), la légende ; l'en-tête choisit l'appel.
pub fn file_card(ui: &mut Ui, t: &Tokens, uis: &mut UiState, tool: &ToolItem, cwd: &str, error: &mut Option<String>) {
    let selected = uis.selected_tool_id.as_deref() == Some(tool.id.as_str());
    Frame::new().stroke(Stroke::new(1.0, t.card_border)).corner_radius(CornerRadius::same(theme::R_LG)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        let w = ui.available_width();
        let (rect, resp) = ui.allocate_exact_size(Vec2::new(w, tool_row::ROW_H), Sense::click());
        if selected {
            ui.painter().rect_filled(rect, CornerRadius::ZERO, t.raised);
            ui.painter().rect_filled(Rect::from_min_size(rect.min, Vec2::new(2.0, rect.height())), CornerRadius::ZERO, t.accent);
        } else if resp.hovered() {
            ui.painter().rect_filled(rect, CornerRadius::ZERO, t.hover.gamma_multiply(0.6));
        }
        let x = paint_left(ui, rect, rect.left() + 12.0, line_galley(ui, icon("paperclip"), FontId::proportional(theme::CAPTION), t.text_dim, 30.0));
        paint_left(ui, rect, x + 6.0, line_galley(ui, &t!("File.Sent"), theme::semibold(theme::LABEL), t.text, 300.0));
        tool_row::paint_meta(ui, t, rect, rect.right() - 12.0, &tool_row::meta(tool));
        if resp.clicked() {
            uis.selected_tool_id = Some(tool.id.clone());
            uis.inspector_open = true;
        }
        Frame::new().inner_margin(Margin { left: 12, right: 12, top: 4, bottom: 8 }).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.spacing_mut().item_spacing.y = 4.0;
            for f in files(&tool.input, cwd) {
                ui.horizontal(|ui| {
                    ui.add(Label::new(RichText::new(tool_kinds::relative(&f, cwd)).font(theme::mono(theme::CODE)).color(t.text)).selectable(true)).on_hover_text(&f);
                    if widgets::ghost(ui, t, &format!("{} {}", icon("folder"), t!("File.Reveal"))).on_hover_text(t!("File.RevealOf", file_name(&f))).clicked() {
                        *error = reveal(&f).err().map(|e| t!("File.RevealFailed", e));
                    }
                });
            }
            if let Some(c) = json::str(&tool.input, "caption").filter(|c| !c.is_empty()) {
                ui.add(Label::new(RichText::new(c).color(t.text_dim)).wrap());
            }
            if let Some(e) = error {
                widgets::error_text(ui, t, e);
            }
        });
    });
}

// ------------------------------------------------------------------------------------------- le texte en cours

/// Le texte qui arrive (`SessionHandle.stream`), tel quel, et le curseur.
pub fn streaming(ui: &mut Ui, t: &Tokens, text: &str) {
    if text.is_empty() {
        return;
    }
    let mut job = LayoutJob::default();
    job.wrap.max_width = ui.available_width();
    job.append(text, 0.0, TextFormat { font_id: FontId::proportional(theme::BODY), color: t.text, ..Default::default() });
    job.append("▍", 0.0, TextFormat { font_id: FontId::proportional(theme::BODY), color: t.accent_ink, ..Default::default() });
    ui.add(Label::new(job).selectable(true));
}

// ------------------------------------------------------------------------------------------- les tâches

/// InProgress « now », Completed « done ».
pub fn todo_css(s: TodoStatus) -> Option<&'static str> {
    match s {
        TodoStatus::InProgress => Some("now"),
        TodoStatus::Completed => Some("done"),
        TodoStatus::Pending => None,
    }
}

/// La liste des tâches de l'agent (`TodoPanel`), entre le fil et le composeur : ouverte tant qu'il reste du travail
/// (sauf clic sur l'en-tête) ; repliée, une ligne : l'avancement et la tâche en cours.
pub fn todos(ui: &mut Ui, t: &Tokens, items: &[Item], open: &mut Option<bool>) {
    let list = todo_list::from(items);
    if list.is_empty() {
        return;
    }
    let is_open = open.unwrap_or_else(|| list.iter().any(|x| x.status != TodoStatus::Completed));
    Frame::new().stroke(Stroke::new(1.0, t.card_border)).fill(t.card).corner_radius(CornerRadius::same(theme::R_LG)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        let r = fold_header(ui, t, is_open, None, |ui| {
            widgets::cap(ui, t, &t!("Todo.Title"));
            ui.add(Label::new(RichText::new(todo_list::progress(&list)).font(theme::mono(theme::CAPTION)).color(t.text_faint)).selectable(false));
            if !is_open && let Some(now) = list.iter().find(|x| x.status == TodoStatus::InProgress) {
                let w = ui.available_width().max(20.0);
                ui.add(Label::new(line_galley(ui, now.label(), FontId::proportional(theme::LABEL), t.text_dim, w)).selectable(false));
            }
        });
        if r.clicked() {
            *open = Some(!is_open);
        }
        if is_open {
            Frame::new().inner_margin(Margin { left: 14, right: 12, top: 2, bottom: 8 }).show(ui, |ui| {
                ui.set_width(ui.available_width());
                ScrollArea::vertical().id_salt("todos").max_height(180.0).auto_shrink([false, true]).show(ui, |ui| {
                    ui.spacing_mut().item_spacing.y = 3.0;
                    for x in &list {
                        ui.horizontal(|ui| {
                            let (mark, color, text) = match todo_css(x.status) {
                                Some("done") => (icon("check"), t.success, t.text_faint),
                                Some(_) => (icon("dot"), t.accent_ink, t.text),
                                None => (icon("ring"), t.text_faint, t.text_dim),
                            };
                            ui.add(Label::new(RichText::new(mark).color(color).font(FontId::proportional(theme::CAPTION))).selectable(false))
                                .on_hover_text(todo_list::status_label(x.status));
                            let mut rt = RichText::new(x.label()).font(FontId::proportional(theme::LABEL)).color(text);
                            if x.status == TodoStatus::Completed {
                                rt = rt.strikethrough();
                            } else if x.status == TodoStatus::InProgress {
                                rt = rt.font(theme::medium(theme::LABEL));
                            }
                            ui.add(Label::new(rt).wrap());
                        });
                    }
                });
            });
        }
    });
}

/// Le petit texte de la ligne repliée d'un repli : `small`.
pub fn fold_small(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.add(Label::new(small(text, t)).selectable(false));
}

#[cfg(test)]
mod tests {
    use super::*;
    use ccui_shared::model::HookItem;

    #[test]
    fn heads() {
        let t0 = Time::default();
        let ms = |n| t0 + chrono::Duration::milliseconds(n);
        assert_eq!(head(&[(t0, None), (t0, Some(ms(30)))]), i18n::plural(2, "Tools"));
        assert_eq!(head(&[(t0, Some(ms(30))), (t0, Some(ms(4470)))]), format!("{} · {}", i18n::plural(2, "Tools"), fmt::dur(4500)));
    }

    fn hook(name: &str, outcome: &str, code: Option<i32>) -> HookItem {
        HookItem { hook: HookEvt { name: name.into(), outcome: outcome.into(), exit_code: code, output: String::new(), id: None } }
    }

    #[test]
    fn hook_labels_and_names() {
        let hs = [hook("Pre", "success", None), hook("Stop", "error", Some(2)), hook("Pre", "cancelled", None)];
        let refs: Vec<&HookEvt> = hs.iter().map(|h| &h.hook).collect();
        assert_eq!(hook_names(&refs), "Pre ×2, Stop");
        assert_eq!(hook_label(&hs[0].hook), t!("Hook.Output"));
        assert_eq!(hook_label(&hs[1].hook), t!("Hook.Blocking"));
        assert_eq!(hook_label(&hook("x", "error", None).hook), t!("Hook.Failed", "—"));
        assert_eq!(hook_label(&hs[2].hook), "cancelled");
    }

    #[test]
    fn paths() {
        use serde_json::json;
        assert_eq!(full_path("/abs/a.png", "/w"), "/abs/a.png");
        assert_eq!(full_path("out/a.png", "/w/p"), "/w/p/out/a.png");
        assert_eq!(full_path("../a.png", "/w/p"), "/w/a.png");
        assert_eq!(full_path("./x/../a.png", "/w"), "/w/a.png");
        assert_eq!(full_path("a.png", "C:\\w"), "C:\\w\\a.png");
        assert_eq!(full_path("..\\..\\a", "C:\\w"), "C:\\a");
        assert_eq!(full_path("D:/x", "C:\\w"), "D:/x");
        assert_eq!(file_name("/w/out/a.png"), "a.png");
        assert_eq!(files(&json!({"files":["a", "", 3, "/b"]}), "/w"), ["/w/a", "/b"]);
    }

    #[test]
    fn todo_classes() {
        assert_eq!(todo_css(TodoStatus::InProgress), Some("now"));
        assert_eq!(todo_css(TodoStatus::Pending), None);
    }
}
