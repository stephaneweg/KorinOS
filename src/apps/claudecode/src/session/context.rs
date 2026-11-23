//! Ce que l'inspecteur montre quand rien n'est choisi (`session/context_panel.rs`, `diff_panel.rs`,
//! `memory_panel.rs`, `status_panel.rs`, `extra_dirs.rs`, `sign_in.rs` de ClaudeCodeUI).
//!
//! - **Les changements non validés** (`/diff`) : `git diff HEAD` du dossier (les fichiers non suivis comme nouveaux),
//!   relu quand un outil qui change des fichiers finit et sur Actualiser. En lecture seule.
//! - **Le contexte** : `get_context_usage` relu après chaque tour (la barre par catégorie, compacter maintenant, la
//!   fenêtre d'auto-compactage, le poids des outils MCP), le cache, le dossier de travail (`/cd`, avec la confiance
//!   demandée pour un dossier nouveau), les dossiers en plus (`--add-dir`), les réponses marquées, la mémoire
//!   (CLAUDE.md : lire, modifier, enregistrer avec le contrôle de conflit ; l'auto-mémoire : l'interrupteur, les
//!   souvenirs, les supprimer), puis `/status` et le compte (se connecter par la session, se déconnecter).

use std::hash::{Hash, Hasher};

use ccui_shared::api::{Bookmark, CliOutput, GitDiff, TrustPrompt};
use ccui_shared::edit_diff::DiffKind;
use ccui_shared::memory::{self, AutoMemory, MemoryFile, MemorySave};
use ccui_shared::model::{Item, SessionStatus, Time, ToolState};
use ccui_shared::{fmt, i18n, json, session_rules, t, text, tool_kinds};
use egui::{Color32, CornerRadius, Frame, Margin, RichText, ScrollArea, Sense, Stroke, Ui};
use serde_json::Value;

use crate::app::{Cx, Route};
use crate::backend::Task;
use crate::md;
use crate::session::inspector::{self as insp, val};
use crate::theme::{self, Tokens};
use crate::widgets;

type Res<T> = Result<T, String>;

// =========================================================================================== /diff

/// Au plus 100 fichiers.
const MAX_FILES: usize = 100;

/// Les outils finis qui ont pu changer des fichiers (Edit/Write/MultiEdit/NotebookEdit, commandes shell).
pub fn changing_done(items: &[Item]) -> usize {
    items
        .iter()
        .filter(|i| {
            i.as_tool().is_some_and(|t| {
                t.state == ToolState::Done && (tool_kinds::is_edit(&t.name) || tool_kinds::is_shell(&t.name) || t.name == "NotebookEdit")
            })
        })
        .count()
}

#[derive(Default)]
pub struct DiffState {
    diff: Option<GitDiff>,
    task: Option<Task<GitDiff>>,
    seen: Option<(usize, String)>,
}

pub fn show_diff(ui: &mut Ui, cx: &mut Cx, st: &mut DiffState, id: &str) {
    let t = cx.t;
    let Some(h) = cx.app.session(id) else { return };
    let key = (changing_done(&h.items), h.meta.cwd.clone());
    if let Some(d) = st.task.as_ref().and_then(Task::take) {
        st.task = None;
        st.diff = Some(d);
    }
    let mut refresh = false;
    let mut close = false;
    let sub = match &st.diff {
        Some(d) if !d.files.is_empty() => {
            let count = |k| d.files.iter().map(|f| f.lines.iter().filter(|l| l.kind == k).count()).sum::<usize>();
            format!("{} · +{} −{}", i18n::plural(d.files.len() as i64, "Diff.Files"), count(DiffKind::Add), count(DiffKind::Del))
        }
        _ => String::new(),
    };
    insp::ph(ui, &t, &t!("Diff.Title"), &sub, |ui| {
        close = insp::close_button(ui, &t);
        if ui.add_enabled(st.task.is_none(), egui::Button::new(widgets::icon("refresh")).frame(false)).on_hover_text(t!("Diff.Refresh")).clicked() {
            refresh = true;
        }
    });
    // (un outil a fini pendant que git tournait : relire ensuite)
    if (refresh || st.seen.as_ref() != Some(&key)) && st.task.is_none() {
        st.seen = Some(key.clone());
        let cwd = key.1.clone();
        st.task = Some(cx.be.task(async move { ccui_core::edit_diff::git_diff(&cwd).await }));
    }
    if close {
        cx.ui.diff_open = false;
    }
    ScrollArea::vertical().id_salt(("diff", id)).auto_shrink(false).show(ui, |ui| {
        Frame::new().inner_margin(Margin::same(16)).show(ui, |ui| {
            ui.set_width(ui.available_width());
            let status = match &st.diff {
                None => t!("Diff.Loading"),
                Some(d) if d.root.is_none() => t!("Diff.NotRepo"),
                Some(d) if d.files.is_empty() => t!("Diff.None"),
                _ => String::new(),
            };
            if !status.is_empty() {
                widgets::dim(ui, &t, &status);
            }
            if let Some(d) = &st.diff {
                for (i, f) in d.files.iter().take(MAX_FILES).enumerate() {
                    insp::diff_view(ui, &t, ("git-diff", i), &f.path, &f.lines);
                    ui.add_space(8.0);
                }
                let more = d.files.len().saturating_sub(MAX_FILES);
                if more > 0 {
                    widgets::dim(ui, &t, &t!("Diff.MoreFiles", more));
                }
            }
        });
    });
}

// =========================================================================================== le contexte

/// La CLI prend de 100k à 1M.
const WINDOWS: [&str; 5] = ["auto", "200000", "400000", "600000", "1000000"];

/// Une catégorie utilisée de la barre du contexte ; `color` : la variable d'app.css de l'original.
#[derive(Clone, Debug, PartialEq)]
pub struct Row {
    pub label: String,
    pub color: &'static str,
    pub tokens: i64,
    pub mcp: bool,
}

/// Un nombre JSON tronqué en i64, sinon 0.
pub fn long(v: &Value, k: &str) -> i64 {
    json::prop(v, k).and_then(Value::as_f64).map_or(0, |d| d as i64)
}

/// Les `mcpTools[]` chargés.
pub fn loaded_mcp(ctx: &Value) -> Vec<&Value> {
    match json::prop(ctx, "mcpTools") {
        Some(Value::Array(a)) => a.iter().filter(|t| json::prop(t, "isLoaded") == Some(&Value::Bool(true))).collect(),
        _ => Vec::new(),
    }
}

/// Les catégories utilisées (les outils « différés » ne sont pas dans le contexte ; « buffer » et « free » sont libres).
pub fn rows(ctx: &Value) -> Vec<Row> {
    let mcp = loaded_mcp(ctx).len();
    let Some(Value::Array(cats)) = json::prop(ctx, "categories") else { return Vec::new() };
    cats.iter()
        .filter(|k| json::str(k, "kind") == Some("used") && long(k, "tokens") > 0)
        .map(|k| {
            let tokens = long(k, "tokens");
            let name = json::string(k, "name");
            let row = |label: String, color, mcp| Row { label, color, tokens, mcp };
            match name.as_str() {
                "System prompt" => row(t!("Ctx.Cat.SystemPrompt"), "var(--fg-4)", false),
                "System tools" => row(t!("Ctx.Cat.SystemTools"), "var(--syn-fn)", false),
                n if n.starts_with("MCP") => row(if mcp > 0 { t!("Ctx.Cat.McpN", mcp) } else { t!("Ctx.Cat.Mcp") }, "var(--syn-kw)", true),
                "Memory files" => row(t!("Ctx.Cat.Memory"), "var(--syn-attr)", false),
                "Custom agents" => row(t!("Ctx.Cat.Agents"), "var(--syn-type)", false),
                "Skills" => row(t!("Ctx.Cat.Skills"), "var(--syn-num)", false),
                "Messages" => row(t!("Ctx.Cat.Messages"), "var(--syn-str)", false),
                _ => row(name.clone(), "var(--fg-3)", false),
            }
        })
        .collect()
}

/// Les catégories groupées par libellé (première couleur, jetons additionnés), dans l'ordre où on les voit.
pub fn legend(rows: &[Row]) -> Vec<(String, &'static str, i64)> {
    let mut out: Vec<(String, &'static str, i64)> = Vec::new();
    for r in rows {
        match out.iter_mut().find(|g| g.0 == r.label) {
            Some(g) => g.2 += r.tokens,
            None => out.push((r.label.clone(), r.color, r.tokens)),
        }
    }
    out
}

/// La couleur d'une catégorie, prise dans le thème.
fn cat_color(t: &Tokens, css: &str) -> Color32 {
    match css {
        "var(--fg-4)" => t.text_faint,
        "var(--syn-fn)" => t.accent_ink,
        "var(--syn-kw)" => t.danger,
        "var(--syn-attr)" => t.warning,
        "var(--syn-type)" => t.accent,
        "var(--syn-num)" => t.success.gamma_multiply(0.7),
        "var(--syn-str)" => t.success,
        _ => t.text_dim,
    }
}

#[derive(Default)]
pub struct ContextState {
    refreshed_for: Option<Option<Time>>,
    send: Option<(&'static str, Task<Res<String>>)>,
    error: Option<String>,
    marks: Vec<Bookmark>,
    marks_seen: Option<Option<u64>>,
    marks_task: Option<Task<Vec<Bookmark>>>,
    cwd_task: Option<Task<Res<Option<TrustPrompt>>>>,
    trust: Option<TrustPrompt>,
    cwd_note: Option<String>,
    dirs: DirsState,
    memory: MemoryState,
    status: StatusState,
}

pub fn show_context(ui: &mut Ui, cx: &mut Cx, st: &mut ContextState, id: &str) {
    let t = cx.t;
    let Some(h) = cx.app.session(id) else { return };
    let meta = h.meta.clone();
    let context = h.heavy.context.clone();
    let (home, sep) = (cx.app.boot.home.clone(), cx.app.boot.sep.clone());
    let short = |p: &str| text::short(p, &home, &sep);
    let busy_status = matches!(meta.status, SessionStatus::Starting | SessionStatus::Running | SessionStatus::Waiting);
    let idle = matches!(meta.status, SessionStatus::Idle | SessionStatus::Exited);

    // get_context_usage une fois par tour fini, au repos avec un process
    if meta.has_process && meta.status == SessionStatus::Idle && st.refreshed_for != Some(meta.last_result_at) {
        st.refreshed_for = Some(meta.last_result_at);
        if let Ok(s) = cx.be.session(id) {
            let what = format!("[{id}] get_context_usage");
            cx.be.fire(&what, async move { s.refresh_context().await });
        }
    }
    // une réponse marquée dans le fil se montre ici aussitôt
    let generation = cx.app.bookmarks.as_ref().map(|b| b.0);
    if st.marks_seen != Some(generation) && st.marks_task.is_none() {
        st.marks_seen = Some(generation);
        let sid = id.to_string();
        st.marks_task = Some(cx.be.blocking(move || ccui_core::bookmarks::of(&sid)));
    }
    if let Some(m) = st.marks_task.as_ref().and_then(Task::take) {
        st.marks_task = None;
        st.marks = m;
    }
    if let Some((fail, tk)) = &st.send
        && let Some(r) = tk.take()
    {
        if let Err(e) = r {
            st.error = Some(t!(fail, e));
        }
        st.send = None;
    }
    if let Some(r) = st.cwd_task.as_ref().and_then(Task::take) {
        st.cwd_task = None;
        match r {
            Ok(tp) => {
                if tp.is_none() {
                    let now = cx.app.session(id).map(|h| h.meta.cwd.clone()).unwrap_or_default();
                    st.cwd_note = Some(t!("Cwd.Moved", short(&now)));
                }
                st.trust = tp;
            }
            Err(e) => {
                st.trust = None;
                st.cwd_note = Some(t!("Session.ActionFailed", e.trim_end()));
            }
        }
    }
    let tag = format!("ctx-cwd-{id}");
    let mut move_to: Option<(String, Option<String>)> = crate::host::picked(&tag).map(|p| (p, None));
    let mut send: Option<(String, &'static str)> = None;

    let max = context.as_ref().map_or(meta.context_window.unwrap_or(0), |c| long(c, "maxTokens"));
    let total = context.as_ref().map_or(meta.context_tokens, |c| long(c, "totalTokens"));
    let sub = if max > 0 { format!("{} / {} · {}", fmt::tokens(total), fmt::tokens(max), fmt::pct(total as f64 / max as f64)) } else { String::new() };
    insp::ph(ui, &t, &t!("Ctx.Title"), &sub, |_| {});

    ScrollArea::vertical().id_salt(("ctx", id)).auto_shrink(false).show(ui, |ui| {
        // ---- l'usage du contexte
        match context.as_ref().filter(|_| max > 0) {
            None => insp::section(ui, &t, false, |ui| insp::para(ui, &t, &t!(if meta.has_process { "Ctx.Unknown" } else { "Ctx.NoProcess" }))),
            Some(ctx) => {
                let rows = rows(ctx);
                let mcp = loaded_mcp(ctx);
                let mcp_tokens: i64 = rows.iter().filter(|r| r.mcp).map(|r| r.tokens).sum();
                insp::section(ui, &t, false, |ui| {
                    // la barre
                    let w = ui.available_width();
                    let (rect, _) = ui.allocate_exact_size(egui::vec2(w, 8.0), Sense::hover());
                    ui.painter().rect_filled(rect, CornerRadius::same(4), t.separator);
                    let mut x = rect.left();
                    for r in &rows {
                        let dw = (r.tokens as f32 / max as f32 * w).min(rect.right() - x);
                        ui.painter().rect_filled(egui::Rect::from_min_size(egui::pos2(x, rect.top()), egui::vec2(dw, 8.0)), CornerRadius::ZERO, cat_color(&t, r.color));
                        x += dw;
                    }
                    egui::Grid::new(("ctx-legend", id)).num_columns(3).spacing([8.0, 4.0]).show(ui, |ui| {
                        let mut line = |ui: &mut Ui, c: Color32, label: &str, n: i64| {
                            let (r, _) = ui.allocate_exact_size(egui::vec2(9.0, 9.0), Sense::hover());
                            ui.painter().rect_filled(r, CornerRadius::same(2), c);
                            ui.label(RichText::new(label).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim));
                            ui.label(RichText::new(fmt::tokens(n)).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                            ui.end_row();
                        };
                        for (label, color, n) in legend(&rows) {
                            line(ui, cat_color(&t, color), &label, n);
                        }
                        line(ui, t.separator, &t!("Ctx.Free"), (max - total).max(0));
                    });
                    ui.horizontal_wrapped(|ui| {
                        let can = st.send.is_none() && idle;
                        if ui.add_enabled(can, egui::Button::new(t!("Ctx.CompactNow"))).clicked() {
                            send = Some(("/compact".into(), "Ctx.CompactFailed"));
                        }
                        let threshold = long(ctx, "autoCompactThreshold");
                        if json::prop(ctx, "isAutoCompactEnabled") == Some(&Value::Bool(true)) && threshold > 0 {
                            ui.label(RichText::new(t!("Ctx.Auto", fmt::pct(threshold as f64 / max as f64))).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint))
                                .on_hover_text(t!("Ctx.AutoTitle"));
                            // /autocompact <jetons|auto> : la CLI garde la fenêtre pour le modèle et répond dans le fil
                            ui.add_enabled_ui(can, |ui| {
                                egui::ComboBox::from_id_salt(("ctx-window", id)).selected_text(t!("Ctx.Window")).show_ui(ui, |ui| {
                                    for w in WINDOWS {
                                        let label = if w == "auto" { t!("Ctx.WindowAuto") } else { fmt::tokens(w.parse().unwrap_or(0)) };
                                        if ui.selectable_label(false, label).clicked() {
                                            send = Some((format!("/autocompact {w}"), "Ctx.WindowFailed"));
                                        }
                                    }
                                })
                                .response
                                .on_hover_text(t!("Ctx.WindowHint"));
                            });
                        }
                    });
                    if let Some(e) = &st.error {
                        insp::errbox(ui, &t, e);
                    }
                });
                if !mcp.is_empty() {
                    let n = mcp.len();
                    let mut servers: Vec<&str> = mcp.iter().map(|x| json::str(x, "serverName").unwrap_or("")).collect();
                    servers.sort_unstable();
                    servers.dedup();
                    let k = if n == 1 { "Ctx.Loaded1" } else { "Ctx.LoadedN" };
                    let line = format!("{} {}", t!(k, i18n::plural(n as i64, "Tools"), i18n::plural(servers.len() as i64, "Ctx.Servers")), t!("Ctx.DisableHint"));
                    insp::section(ui, &t, false, |ui| {
                        widgets::cap(ui, &t, &t!("Ctx.McpWeight", fmt::tokens(mcp_tokens)));
                        insp::para(ui, &t, &line);
                        if ui.button(t!("Ctx.ManageMcp")).clicked() {
                            cx.navigate(Route::Extensions);
                        }
                    });
                }
            }
        }
        // ---- le cache du dernier appel
        if meta.context_tokens > 0 && meta.cache_read + meta.cache_write > 0 {
            let line = t!("Ctx.CacheLine", fmt::pct(meta.cache_read as f64 / meta.context_tokens as f64), fmt::tokens(meta.cache_write));
            insp::section(ui, &t, false, |ui| {
                widgets::cap(ui, &t, &t!("Ctx.Cache"));
                ui.add(egui::Label::new(RichText::new(line).font(egui::FontId::proportional(theme::LABEL)).color(t.text_dim)).wrap()).on_hover_text(t!("Ctx.CacheHint"));
            });
        }
        // ---- le dossier de travail (/cd : session_set_cwd ; un dossier jamais approuvé demande d'abord)
        insp::section(ui, &t, false, |ui| {
            widgets::cap(ui, &t, &t!("Cwd.Label"));
            ui.horizontal(|ui| {
                let can = st.cwd_task.is_none() && !busy_status;
                if ui.add_enabled(can, egui::Button::new(widgets::icon("folder"))).on_hover_text(t!("Cwd.Label")).clicked() {
                    crate::host::pick_folder(&meta.cwd, &tag);
                }
                ui.add(egui::Label::new(RichText::new(short(&meta.cwd)).font(theme::mono(theme::CAPTION)).color(t.text_dim)).truncate()).on_hover_text(&meta.cwd);
            });
            if let Some(tp) = st.trust.clone() {
                let msg = match &tp.root {
                    Some(root) => t!("Cwd.TrustRoot", &tp.directory, root),
                    None => t!("Cwd.Trust", &tp.directory),
                };
                ui.add(egui::Label::new(RichText::new(msg).color(t.warning).font(egui::FontId::proportional(theme::LABEL))).wrap());
                ui.horizontal(|ui| {
                    if ui.add_enabled(st.cwd_task.is_none(), egui::Button::new(RichText::new(t!("New.Trust")).color(t.on_accent)).fill(t.accent)).clicked() {
                        move_to = Some((tp.directory.clone(), Some(tp.directory.clone())));
                    }
                    if widgets::ghost(ui, &t, &t!("Action.Cancel")).clicked() {
                        st.trust = None;
                    }
                });
            }
            if let Some(n) = &st.cwd_note {
                insp::para(ui, &t, n);
            }
        });
        // ---- les dossiers en plus (au repos, ou sans process : le dossier passe alors en --add-dir au lancement)
        insp::section(ui, &t, false, |ui| {
            widgets::cap(ui, &t, &t!("Dirs.Label"));
            insp::para(ui, &t, &t!("Dirs.LiveHint"));
            let confirm = session_rules::risky(&meta.mode).then(|| t!("Dirs.ConfirmRisky", i18n::name("Mode.Name.", Some(&meta.mode))));
            extra_dirs(ui, cx, &mut st.dirs, id, &meta.add_dirs, &meta.cwd, confirm, busy_status);
        });
        // ---- les réponses marquées
        if !st.marks.is_empty() {
            insp::section(ui, &t, false, |ui| {
                widgets::cap(ui, &t, &t!("Bookmark.Title"));
                for b in &st.marks {
                    ui.horizontal(|ui| {
                        ui.label(RichText::new(widgets::icon("bookmark")).color(t.accent_ink));
                        ui.add(egui::Label::new(RichText::new(&b.gist).font(egui::FontId::proportional(theme::LABEL)).color(t.text_dim)).truncate());
                    });
                }
            });
        }
        memory_panel(ui, cx, &mut st.memory, id);
        status_panel(ui, cx, &mut st.status, id);
    });

    if let Some((text, fail)) = send
        && st.send.is_none()
    {
        st.error = None;
        match cx.be.session(id) {
            Ok(s) => st.send = Some((fail, cx.be.task(async move { s.send(text, Vec::new()).await }))),
            Err(e) => st.error = Some(t!(fail, e)),
        }
    }
    if let Some((path, trusted)) = move_to
        && st.cwd_task.is_none()
    {
        st.cwd_note = None;
        match cx.be.session(id) {
            Ok(s) => st.cwd_task = Some(cx.be.task(async move { s.set_cwd(&path, trusted).await })),
            Err(e) => st.cwd_note = Some(t!("Session.ActionFailed", e)),
        }
    }
}

// =========================================================================================== les dossiers en plus

/// L'ajout d'un dossier : le champ, la vérification (`validate_extra_dir`), l'étape de confirmation, l'ajout.
#[derive(Default)]
pub struct DirsState {
    draft: String,
    error: Option<String>,
    confirmed: Option<String>,
    check: Option<Task<Res<String>>>,
    add: Option<Task<Res<()>>>,
}

/// La liste des dossiers en plus et le champ qui en ajoute un (un chemin absolu qui existe) ; `confirm` : le mode
/// de la session change des fichiers sans demander (un dossier en plus n'est jamais isolé), le premier Ajouter le
/// dit, le second ajoute.
#[allow(clippy::too_many_arguments)]
fn extra_dirs(ui: &mut Ui, cx: &Cx, st: &mut DirsState, id: &str, dirs: &[String], cwd: &str, confirm: Option<String>, disabled: bool) {
    let t = cx.t;
    let tag = format!("ctx-dir-{id}");
    if let Some(p) = crate::host::picked(&tag) {
        st.draft = p;
        st.error = None;
        st.confirmed = None;
        start_check(cx, st, cwd, dirs);
    }
    if let Some(r) = st.check.as_ref().and_then(Task::take) {
        st.check = None;
        match r {
            Err(m) => st.error = Some(m),
            Ok(d) if confirm.is_some() && st.confirmed.as_deref() != Some(d.as_str()) => st.confirmed = Some(d),
            Ok(d) => match cx.be.session(id) {
                Ok(s) => st.add = Some(cx.be.task(async move { s.add_dir(&d).await })),
                Err(e) => st.error = Some(t!("Dirs.AddFailed", e)),
            },
        }
    }
    if let Some(r) = st.add.as_ref().and_then(Task::take) {
        st.add = None;
        match r {
            Ok(()) => {
                st.draft.clear();
                st.confirmed = None;
            }
            Err(e) => st.error = Some(t!("Dirs.AddFailed", e)),
        }
    }
    if !dirs.is_empty() {
        let (home, sep) = (cx.app.boot.home.clone(), cx.app.boot.sep.clone());
        ui.horizontal_wrapped(|ui| {
            for d in dirs {
                widgets::tag(ui, &t, &format!("{} {}", widgets::icon("folder"), text::chip(d, &home, &sep))).on_hover_text(d);
            }
        });
    }
    let busy = st.check.is_some() || st.add.is_some();
    let mut go = false;
    ui.add_enabled_ui(!disabled, |ui| {
        ui.horizontal(|ui| {
            if ui.button(widgets::icon("folder")).clicked() {
                let start = if st.draft.trim().is_empty() { cwd.to_string() } else { st.draft.trim().to_string() };
                crate::host::pick_folder(&start, &tag);
            }
            let label = t!(if st.confirmed.is_some() { "Dirs.AddAnyway" } else { "Dirs.Add" });
            let bw = 110.0;
            let r = ui.add(egui::TextEdit::singleline(&mut st.draft).hint_text(t!("Dirs.Placeholder")).desired_width((ui.available_width() - bw).max(80.0)));
            if r.changed() {
                st.error = None;
                st.confirmed = None;
            }
            if r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                go = true;
            }
            if ui.add_enabled(!busy && !st.draft.trim().is_empty(), egui::Button::new(format!("{} {label}", widgets::icon("plus"))).frame(false)).clicked() {
                go = true;
            }
        });
    });
    if go && !busy && !disabled && !st.draft.trim().is_empty() {
        st.error = None;
        start_check(cx, st, cwd, dirs);
    }
    if let Some(e) = &st.error {
        ui.label(RichText::new(e).color(t.danger).font(egui::FontId::proportional(theme::CAPTION)));
    } else if st.confirmed.is_some()
        && let Some(c) = &confirm
    {
        ui.add(egui::Label::new(RichText::new(c).color(t.warning).font(egui::FontId::proportional(theme::CAPTION))).wrap());
    }
}

fn start_check(cx: &Cx, st: &mut DirsState, cwd: &str, dirs: &[String]) {
    let (raw, cwd, dirs) = (st.draft.clone(), cwd.to_string(), dirs.to_vec());
    st.check = Some(cx.be.blocking(move || ccui_core::new_session::validate_extra_dir(&raw, &cwd, &dirs)));
}

// =========================================================================================== la mémoire

/// La colonne de droite d'un fichier de mémoire.
pub fn file_note(f: &MemoryFile, has_context: bool) -> String {
    match f.tokens {
        Some(n) => t!("Workflow.Tokens", fmt::tokens(n)),
        None if !f.exists => t!("Mem.Absent"),
        // (sans get_context_usage, une session passée : rien n'est su de ce que la CLI a chargé)
        None if !has_context => String::new(),
        None => t!("Mem.NotLoaded"),
    }
}

fn file_name(path: &str) -> String {
    path.rsplit(['/', '\\']).next().unwrap_or("").to_string()
}

/// Ce qui suit la lecture du fichier ouvert.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Then {
    Nothing,
    /// Recharger : le brouillon devient ce qui est sur le disque.
    Reload,
    /// Après un enregistrement réussi.
    Saved,
}

#[derive(Default)]
pub struct MemoryState {
    mounted: bool,
    files: Vec<MemoryFile>,
    autos: Vec<AutoMemory>,
    lists_key: Option<u64>,
    lists: Option<Task<(Option<Vec<MemoryFile>>, Option<Vec<AutoMemory>>)>>,
    asked_for: Option<Option<Time>>,
    load: Option<(Then, Task<Res<Option<String>>>)>,
    save: Option<Task<(Option<Option<String>>, Res<MemorySave>)>>,
    delete: Option<Task<Res<bool>>>,
    auto: Option<Task<Res<()>>>,
    error: Option<String>,
    delete_armed: Option<String>,
    saved: bool,
    focus: bool,
}

fn start_load(cx: &Cx, st: &mut MemoryState, id: &str, path: &str, then: Then) {
    st.error = None;
    let Ok(s) = cx.be.session(id) else { return };
    let path = path.to_string();
    st.load = Some((
        then,
        cx.be.blocking(move || {
            let known = ccui_core::memory_files::known(&s);
            if !known.iter().any(|f| ccui_core::memory_files::same(&f.path, &path)) {
                return Err(t!("Mem.NotAllowed"));
            }
            ccui_core::memory_files::read(&path)
        }),
    ));
}

fn memory_panel(ui: &mut Ui, cx: &mut Cx, st: &mut MemoryState, id: &str) {
    let t = cx.t;
    let home = cx.app.boot.home.clone();
    let Some(h) = cx.app.session(id) else { return };
    let (has_context, dialog, cwd) = (h.heavy.context.is_some(), h.heavy.memory_dialog.clone(), h.meta.cwd.clone());
    let (ready, at) = (h.meta.has_process && h.meta.status == SessionStatus::Idle, h.meta.last_result_at);
    let mut m = h.memory.clone();

    // de retour : un fichier seulement lu se relit (il a pu changer), une édition garde sa base et son brouillon
    if !st.mounted {
        st.mounted = true;
        if let Some(open) = m.open.clone().filter(|_| !m.editing) {
            start_load(cx, st, id, &open, Then::Nothing);
        }
    }
    // la liste suit ce que dit la CLI (contexte, boîte de la mémoire) et le dossier
    let key = {
        let mut hs = std::collections::hash_map::DefaultHasher::new();
        (has_context, dialog.as_ref().map(|d| d.to_string()), &cwd).hash(&mut hs);
        hs.finish()
    };
    if st.lists_key != Some(key) && st.lists.is_none() {
        st.lists_key = Some(key);
        reload_lists(cx, st, id);
    }
    if let Some((files, autos)) = st.lists.as_ref().and_then(Task::take) {
        st.lists = None;
        if let Some(f) = files {
            st.files = f;
        }
        if let Some(a) = autos {
            st.autos = a;
        }
    }
    // get_memory_dialog après chaque tour : le modèle enregistre ses souvenirs alors
    if ready && st.asked_for != Some(at) {
        st.asked_for = Some(at);
        if let Ok(s) = cx.be.session(id) {
            let what = format!("[{id}] get_memory_dialog");
            cx.be.fire(&what, async move { s.refresh_memory().await });
        }
    }
    if let Some((then, tk)) = &st.load
        && let Some(r) = tk.take()
    {
        let then = *then;
        st.load = None;
        match r {
            Ok(text) => m.loaded = text,
            Err(e) => {
                m.loaded = None;
                st.error = Some(t!("Mem.ReadFailed", e));
            }
        }
        match then {
            Then::Reload => {
                m.draft = Some(m.loaded.clone().unwrap_or_default());
                m.conflict = false;
                st.focus = true;
            }
            Then::Saved => {
                m.editing = false;
                m.conflict = false;
                m.draft = None;
                st.saved = true;
                reload_lists(cx, st, id);
            }
            Then::Nothing => {}
        }
    }
    if let Some((now, r)) = st.save.as_ref().and_then(Task::take) {
        st.save = None;
        if let Some(now) = now {
            m.loaded = now;
        }
        match r {
            Ok(MemorySave::Saved) => {
                if let Some(open) = m.open.clone() {
                    start_load(cx, st, id, &open, Then::Saved);
                }
            }
            Ok(MemorySave::Conflict) => m.conflict = true,
            Ok(MemorySave::NotAllowed) => st.error = Some(t!("Mem.NotAllowed")),
            Err(e) => st.error = Some(t!("Mem.SaveFailed", e)),
        }
    }
    if let Some(r) = st.delete.as_ref().and_then(Task::take) {
        st.delete = None;
        match r {
            Ok(false) => st.error = Some(t!("Mem.NotAllowed")),
            Ok(true) => {
                m.open = None;
                m.editing = false;
                m.loaded = None;
                if let Ok(s) = cx.be.session(id) {
                    cx.be.fire("get_memory_dialog", async move { s.refresh_memory().await.map_err(|e| t!("Mem.DeleteFailed", e)) });
                }
            }
            Err(e) => st.error = Some(t!("Mem.DeleteFailed", e)),
        }
    }
    if let Some(r) = st.auto.as_ref().and_then(Task::take) {
        st.auto = None;
        if let Err(e) = r {
            st.error = Some(t!("Mem.AutoFailed", e));
        }
    }

    let busy = st.save.is_some() || st.auto.is_some();
    let mut open_path: Option<String> = None;
    let mut action: Option<&'static str> = None;
    let mut delete: Option<String> = None;
    let mut set_auto: Option<bool> = None;
    insp::section(ui, &t, false, |ui| {
        widgets::cap(ui, &t, &t!("Mem.Title"));
        for f in &st.files {
            let on = m.open.as_deref() == Some(f.path.as_str());
            let r = widgets::row(ui, &t, on, |ui| {
                ui.horizontal(|ui| {
                    ui.add(egui::Label::new(RichText::new(memory::display(&f.path, &cwd, &home)).font(theme::mono(theme::CAPTION)).color(t.text)).truncate().selectable(false));
                    widgets::tag(ui, &t, &i18n::name("Mem.Type.", Some(&f.kind)));
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        ui.add(egui::Label::new(RichText::new(file_note(f, has_context)).font(theme::mono(theme::CAPTION)).color(t.text_faint)).selectable(false));
                    });
                });
            });
            if r.on_hover_text(&f.path).clicked() {
                open_path = Some(f.path.clone());
            }
        }
        ui.add(egui::Label::new(RichText::new(t!("Mem.Hint")).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint)).wrap());

        // l'auto-mémoire
        if let Some(am) = dialog.as_ref().and_then(|d| json::prop(d, "auto_memory")).cloned() {
            let on = json::bool(&am, "enabled");
            let toggleable = json::bool(&am, "toggleable");
            let mut v = on;
            if ui.add_enabled(!busy && toggleable, egui::Checkbox::new(&mut v, t!("Mem.Auto"))).on_hover_text(json::string(&am, "status")).changed() {
                set_auto = Some(v);
            }
            if m.open.is_none()
                && let Some(e) = &st.error
            {
                insp::errbox(ui, &t, e);
            }
            if let Some(f) = dialog.as_ref().and_then(memory::auto_folder) {
                ui.add(egui::Label::new(RichText::new(t!("Mem.AutoFolder", fmt::home(&f, &home))).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint)).wrap()).on_hover_text(&f);
            }
            if st.autos.is_empty() {
                if on {
                    widgets::dim(ui, &t, &t!("Mem.AutoNone"));
                }
            } else {
                widgets::cap(ui, &t, &t!("Mem.AutoList", st.autos.len()));
                for a in &st.autos {
                    let sel = m.open.as_deref() == Some(a.path.as_str());
                    let r = widgets::row(ui, &t, sel, |ui| {
                        ui.horizontal(|ui| {
                            ui.add(egui::Label::new(RichText::new(&a.name).font(theme::mono(theme::CAPTION)).color(t.text)).selectable(false));
                            if let Some(k) = &a.kind {
                                widgets::tag(ui, &t, k);
                            }
                            if let Some(d) = &a.description {
                                ui.add(egui::Label::new(RichText::new(d).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint)).truncate().selectable(false));
                            }
                        });
                    });
                    if r.on_hover_text(a.description.clone().unwrap_or_else(|| a.path.clone())).clicked() {
                        open_path = Some(a.path.clone());
                    }
                }
            }
        }

        // le fichier ouvert
        if let Some(open) = m.open.clone() {
            let name = memory::display(&open, &cwd, &home);
            Frame::new().fill(t.card).stroke(Stroke::new(1.0, t.card_border)).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::same(12)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                if m.editing {
                    let draft = m.draft.get_or_insert_with(String::new);
                    let r = ui.add(egui::TextEdit::multiline(draft).font(theme::mono(theme::CODE)).desired_rows(14).desired_width(f32::INFINITY));
                    if std::mem::take(&mut st.focus) {
                        r.request_focus();
                    }
                    ui.horizontal(|ui| {
                        if ui.add_enabled(!busy, egui::Button::new(RichText::new(t!("Mem.Save")).color(t.on_accent)).fill(t.accent)).clicked() {
                            action = Some("save");
                        }
                        if ui.button(t!("Action.Cancel")).clicked() {
                            action = Some("cancel");
                        }
                    });
                    if m.conflict {
                        insp::errbox(ui, &t, &t!("Mem.Conflict", &name));
                        ui.horizontal(|ui| {
                            if ui.button(t!("Mem.Reload")).clicked() {
                                action = Some("reload");
                            }
                            if ui.add_enabled(!busy, egui::Button::new(RichText::new(t!("Mem.Overwrite")).color(t.danger))).clicked() {
                                action = Some("overwrite");
                            }
                        });
                    }
                } else if st.error.is_none() {
                    if st.load.is_some() {
                        ui.spinner();
                    } else {
                        match &m.loaded {
                            None => insp::para(ui, &t, &t!("Mem.Missing")),
                            Some(l) if l.trim().is_empty() => insp::para(ui, &t, &t!("Mem.Empty")),
                            Some(l) => md::show(ui, &t, l),
                        }
                        let deletable = m.loaded.is_some() && st.autos.iter().any(|a| a.path == open);
                        ui.horizontal(|ui| {
                            if ui.button(t!(if m.loaded.is_none() { "Mem.Create" } else { "Mem.Edit" })).clicked() {
                                action = Some("edit");
                            }
                            if deletable {
                                let label = if st.delete_armed.as_deref() == Some(open.as_str()) { t!("Mem.DeleteConfirm", file_name(&open)) } else { t!("Mem.Delete") };
                                if ui.add_enabled(!busy, egui::Button::new(RichText::new(label).color(t.danger))).clicked() {
                                    delete = Some(open.clone());
                                }
                            }
                        });
                    }
                }
                if let Some(e) = &st.error {
                    insp::errbox(ui, &t, e);
                }
                if st.saved {
                    insp::para(ui, &t, &t!("Mem.Saved"));
                }
            });
        }
    });

    // les gestes
    if let Some(path) = open_path {
        // (cliquer de nouveau la ligne ouverte la ferme ; un brouillon non enregistré part avec, comme Annuler)
        m.open = if m.open.as_deref() == Some(path.as_str()) { None } else { Some(path) };
        m.editing = false;
        m.conflict = false;
        st.saved = false;
        st.delete_armed = None;
        if let Some(o) = m.open.clone() {
            start_load(cx, st, id, &o, Then::Nothing);
        }
    }
    match action {
        Some("edit") => {
            m.draft = Some(m.loaded.clone().unwrap_or_default());
            m.editing = true;
            st.saved = false;
            st.focus = true;
        }
        // (retour à ce qui est sur le disque maintenant, pas à la base de l'édition)
        Some("cancel") => {
            m.editing = false;
            m.conflict = false;
            m.draft = None;
            if let Some(o) = m.open.clone() {
                start_load(cx, st, id, &o, Then::Nothing);
            }
        }
        Some("reload") => {
            if let Some(o) = m.open.clone() {
                start_load(cx, st, id, &o, Then::Reload);
            }
        }
        // overwrite : l'utilisateur a vu le conflit et garde sa version ; la base devient ce qui est sur le disque
        Some(k @ ("save" | "overwrite")) if st.save.is_none() => {
            if let (Some(path), Ok(s)) = (m.open.clone(), cx.be.session(id)) {
                st.error = None;
                let overwrite = k == "overwrite";
                let (loaded, draft) = (m.loaded.clone(), m.draft.clone().unwrap_or_default());
                st.save = Some(cx.be.blocking(move || {
                    let known = ccui_core::memory_files::known(&s);
                    let mut base = loaded;
                    let mut now = None;
                    if overwrite {
                        match ccui_core::memory_files::read(&path) {
                            Ok(x) => {
                                base = x.clone();
                                now = Some(x);
                            }
                            Err(e) => return (None, Err(e)),
                        }
                    }
                    (now, ccui_core::memory_files::save(&known, &path, base.as_deref(), &draft))
                }));
            }
        }
        _ => {}
    }
    // deux clics : le premier arme le bouton, qui dit alors ce qu'il va faire
    if let Some(path) = delete {
        if st.delete_armed.as_deref() != Some(path.as_str()) {
            st.delete_armed = Some(path);
        } else if let Ok(s) = cx.be.session(id) {
            st.delete_armed = None;
            st.delete = Some(cx.be.blocking(move || ccui_core::memory_files::delete(&ccui_core::memory_files::autos(&s), &path)));
        }
    }
    if let Some(on) = set_auto
        && let Ok(s) = cx.be.session(id)
    {
        st.error = None;
        st.auto = Some(cx.be.task(async move { s.set_auto_memory(on).await }));
    }
    if let Some(h) = cx.app.session_mut(id)
        && h.memory != m
    {
        h.memory = m;
    }
}

fn reload_lists(cx: &Cx, st: &mut MemoryState, id: &str) {
    let Ok(s) = cx.be.session(id) else { return };
    st.lists = Some(cx.be.blocking(move || (Some(ccui_core::memory_files::list(&s)), Some(ccui_core::memory_files::autos(&s)))));
}

// =========================================================================================== /status et le compte

#[derive(Default)]
pub struct StatusState {
    asked: bool,
    refresh: Option<Task<Res<()>>>,
    logout: Option<Task<Res<CliOutput>>>,
    confirm_out: bool,
    note: Option<String>,
    sign_start: Option<Task<Res<Option<String>>>>,
    sign_finish: Option<Task<Res<()>>>,
    sign_url: Option<String>,
    sign_note: Option<String>,
}

fn status_refresh(cx: &Cx, st: &mut StatusState, id: &str) {
    if let Ok(s) = cx.be.session(id) {
        st.refresh = Some(cx.be.task(async move { s.refresh_status().await }));
    }
}

fn status_panel(ui: &mut Ui, cx: &mut Cx, st: &mut StatusState, id: &str) {
    let t = cx.t;
    let Some(h) = cx.app.session(id) else { return };
    let has_process = h.meta.has_process;
    let ready = has_process && h.meta.status != SessionStatus::Starting;
    let rows = h.heavy.status_rows.clone();
    let cwd = h.meta.cwd.clone();
    // demandé une fois, dès qu'un process a fini de démarrer
    if ready && !st.asked {
        st.asked = true;
        status_refresh(cx, st, id);
    }
    if let Some(r) = st.refresh.as_ref().and_then(Task::take) {
        st.refresh = None;
        if let Err(e) = r {
            st.note = Some(t!("Session.ActionFailed", e.trim_end()));
        }
    }
    if let Some(r) = st.logout.as_ref().and_then(Task::take) {
        st.logout = None;
        st.note = Some(match r {
            Ok(o) if o.code == 0 => t!("Account.SignedOut"),
            Ok(o) => t!("Session.ActionFailed", format!("{}{}", o.stderr, o.stdout).trim()),
            Err(e) => t!("Session.ActionFailed", e.trim_end()),
        });
        status_refresh(cx, st, id);
    }
    // /login par stream-json : la page de connexion, puis la fin quand le navigateur a fini
    if let Some(r) = st.sign_start.as_ref().and_then(Task::take) {
        st.sign_start = None;
        match r.and_then(|u| u.ok_or_else(|| t!("Account.NoUrl"))) {
            Ok(u) => {
                st.sign_url = Some(u);
                if let Ok(s) = cx.be.session(id) {
                    st.sign_finish = Some(cx.be.task(async move { s.finish_sign_in().await }));
                }
            }
            Err(e) => st.sign_note = Some(t!("Session.ActionFailed", e.trim_end())),
        }
    }
    if let Some(r) = st.sign_finish.as_ref().and_then(Task::take) {
        st.sign_finish = None;
        st.sign_url = None;
        st.sign_note = Some(match r {
            Ok(()) => t!("Account.SignedIn"),
            Err(e) => t!("Session.ActionFailed", e.trim_end()),
        });
    }

    let busy = st.refresh.is_some() || st.logout.is_some();
    let mut refresh = false;
    let mut sign_out = false;
    let mut sign_in = false;
    insp::section(ui, &t, true, |ui| {
        widgets::cap(ui, &t, &t!("Status.Title"));
        match &rows {
            Some(sections) => {
                for (i, sec) in sections.iter().enumerate() {
                    let r: Vec<(String, RichText)> = sec.rows.iter().map(|r| (r.label.clone().unwrap_or_default(), val(&t, &r.value))).collect();
                    insp::kv(ui, &t, ("status", i), r);
                }
            }
            None => insp::para(ui, &t, &t!(if has_process { "Status.Loading" } else { "Status.NoProcess" })),
        }
        if has_process {
            ui.add_enabled_ui(!busy, |ui| {
                ui.horizontal(|ui| {
                    if ui.button(format!("{} {}", widgets::icon("refresh"), t!("Wt.Refresh"))).clicked() {
                        refresh = true;
                    }
                    // deux clics : le compte quitte chaque Claude Code de cet ordinateur, terminaux compris
                    if widgets::ghost(ui, &t, &t!(if st.confirm_out { "Account.SignOutConfirm" } else { "Account.SignOut" })).clicked() {
                        sign_out = true;
                    }
                });
            });
            match &st.sign_url {
                Some(u) => {
                    insp::para(ui, &t, &t!("Account.Waiting"));
                    if widgets::primary(ui, &t, &t!("Ext.OpenSignIn")).clicked() {
                        crate::host::open(u);
                    }
                }
                None => {
                    if ui.add_enabled(st.sign_start.is_none() && st.sign_finish.is_none(), egui::Button::new(t!("Account.SignIn"))).clicked() {
                        sign_in = true;
                    }
                }
            }
            if let Some(n) = &st.sign_note {
                insp::para(ui, &t, n);
            }
        }
        if let Some(n) = &st.note {
            insp::para(ui, &t, n);
        }
    });

    if refresh {
        status_refresh(cx, st, id);
    }
    if sign_out {
        if !st.confirm_out {
            st.confirm_out = true;
        } else {
            st.confirm_out = false;
            st.note = None;
            st.logout = Some(cx.be.task(async move { ccui_core::cli::auth_logout(&cwd).await }));
        }
    }
    if sign_in && let Ok(s) = cx.be.session(id) {
        st.sign_note = None;
        st.sign_start = Some(cx.be.task(async move { s.start_sign_in().await }));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn context_rows() {
        i18n::set_thread_lang(Some(i18n::Lang::En));
        let ctx = json!({
            "categories": [
                {"name": "System prompt", "kind": "used", "tokens": 3000},
                {"name": "MCP tools", "kind": "used", "tokens": 1500.0},
                {"name": "MCP tools (deferred)", "kind": "deferred", "tokens": 900},
                {"name": "Messages", "kind": "used", "tokens": 0},
                {"name": "Odd", "kind": "used", "tokens": 10},
                {"name": "Free space", "kind": "free", "tokens": 100000}
            ],
            "mcpTools": [{"serverName": "gh", "isLoaded": true}, {"serverName": "gh", "isLoaded": false}]
        });
        let r = rows(&ctx);
        assert_eq!(r.len(), 3);
        assert_eq!(r[0], Row { label: "system prompt".into(), color: "var(--fg-4)", tokens: 3000, mcp: false });
        assert_eq!(r[1].label, "MCP tools · 1");
        assert!(r[1].mcp);
        assert_eq!(r[2].color, "var(--fg-3)");
        assert_eq!(loaded_mcp(&ctx).len(), 1);
        let g = legend(&[r[0].clone(), r[0].clone(), r[2].clone()]);
        assert_eq!(g, [("system prompt".to_string(), "var(--fg-4)", 6000), ("Odd".to_string(), "var(--fg-3)", 10)]);
        i18n::set_thread_lang(None);
    }

    #[test]
    fn notes() {
        i18n::set_thread_lang(Some(i18n::Lang::En));
        let f = |tokens, exists| MemoryFile { path: "/w/CLAUDE.md".into(), kind: "Project".into(), tokens, exists };
        assert_eq!(file_note(&f(Some(1200), true), true), "1k tokens");
        assert_eq!(file_note(&f(None, false), false), "not created");
        assert_eq!(file_note(&f(None, true), false), "");
        assert_eq!(file_note(&f(None, true), true), "not loaded yet");
        assert_eq!(file_name("/a/b/note.md"), "note.md");
        i18n::set_thread_lang(None);
    }

    #[test]
    fn counts_finished_changers() {
        let tool = |name: &str, state| {
            Item::Tool(ccui_shared::model::ToolItem {
                id: "t".into(),
                name: name.into(),
                input: Value::Null,
                parent_tool_use_id: None,
                state,
                result_text: None,
                structured: None,
                started_at: Default::default(),
                ended_at: None,
                task_id: None,
                task_status: None,
                progress: None,
                tokens: 0,
                sub_tool_uses: 0,
                background: false,
                model: None,
            })
        };
        let items = vec![
            tool("Edit", ToolState::Done),
            tool("Bash", ToolState::Done),
            tool("NotebookEdit", ToolState::Done),
            tool("Write", ToolState::Running),
            tool("Read", ToolState::Done),
        ];
        assert_eq!(changing_done(&items), 3);
    }
}
