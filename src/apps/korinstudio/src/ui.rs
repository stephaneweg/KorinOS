// L'affichage de KorinStudio, à la manière de VS Code : barre du haut (menus, centre de commandes), barre
// d'activité, barre latérale (explorateur, recherche, Git, exécuter), onglets et éditeur, panneau du bas
// (problèmes, sortie, terminal), copilote à droite, barre d'état, palette de commandes, dialogues.

use crate::app::*;
use crate::copilot::{self, CopilotAction, CopilotUi};
use crate::editor::{self, EditorStyle};
use crate::highlight::Lang;
use crate::icons::{self, Icon};
use crate::tasks::{LineKind, Severity, TaskKind};
use crate::theme::{self, Palette};
use crate::workspace::{self, ProjectKind};
use egui::{Align, Align2, Color32, FontId, Frame, Id, Key, Layout, Margin, Modifiers, Pos2, Rect, RichText, Sense, Stroke, Ui, Vec2, pos2, vec2};
use std::path::{Path, PathBuf};

const KORIN: bool = workspace::KORIN;

fn lang_color(l: Lang) -> Color32 {
    match l {
        Lang::Rust => Color32::from_rgb(0xDE, 0xA5, 0x84),
        Lang::C | Lang::Cpp => Color32::from_rgb(0x55, 0x9C, 0xD6),
        Lang::CSharp => Color32::from_rgb(0x9B, 0x4F, 0x96),
        Lang::AsmGnu | Lang::AsmFasm => Color32::from_rgb(0x6E, 0x4C, 0x13).gamma_multiply(1.8),
        Lang::FreeBasic => Color32::from_rgb(0x3C, 0x9F, 0xD8),
        Lang::Python => Color32::from_rgb(0xF5, 0xC5, 0x18),
        Lang::Toml | Lang::Ini => Color32::from_rgb(0x9C, 0x9C, 0x9C),
        Lang::Json => Color32::from_rgb(0xCB, 0xCB, 0x41),
        Lang::Markdown => Color32::from_rgb(0x42, 0xA5, 0xF5),
        Lang::Shell => Color32::from_rgb(0x89, 0xE0, 0x51),
        Lang::Make => Color32::from_rgb(0xE3, 0x7C, 0x3C),
        Lang::Xml => Color32::from_rgb(0xE3, 0x7C, 0x3C),
        Lang::Plain => Color32::from_rgb(0x8B, 0x94, 0x9E),
    }
}

fn file_badge(ui: &mut Ui, path: &Path, size: f32) {
    let l = Lang::from_path(path);
    let (r, _) = ui.allocate_exact_size(vec2(size, size), Sense::hover());
    let label = match l {
        Lang::Rust => "rs",
        Lang::C => "C",
        Lang::Cpp => "C+",
        Lang::CSharp => "C#",
        Lang::AsmGnu | Lang::AsmFasm => "as",
        Lang::FreeBasic => "fb",
        Lang::Python => "py",
        Lang::Toml => "⚙",
        Lang::Json => "{}",
        Lang::Markdown => "M↓",
        Lang::Shell => "$",
        Lang::Make => "M",
        Lang::Xml => "<>",
        Lang::Ini => "≡",
        Lang::Plain => "",
    };
    if label.is_empty() {
        icons::paint(ui.painter(), r.shrink(1.0), Icon::File, Color32::from_gray(140));
    } else {
        ui.painter().text(r.center(), Align2::CENTER_CENTER, label, theme::bold(size * 0.62), lang_color(l));
    }
}

/// Un en-tête de section (MAJUSCULES, petites, comme VS Code).
fn section_title(ui: &mut Ui, pal: &Palette, t: &str) {
    ui.label(RichText::new(t).font(theme::bold(11.0)).color(pal.fg_dim));
}

/// Une rangée de liste cliquable (survol, sélection), qui rend sa réponse.
fn row(ui: &mut Ui, pal: &Palette, h: f32, selected: bool, id: Id, add: impl FnOnce(&mut Ui)) -> egui::Response {
    let w = ui.available_width();
    let (rect, _) = ui.allocate_exact_size(vec2(w, h), Sense::hover());
    let resp = ui.interact(rect, id, Sense::click());
    if selected {
        ui.painter().rect_filled(rect, 0.0, pal.list_active);
    } else if resp.hovered() {
        ui.painter().rect_filled(rect, 0.0, pal.hover);
    }
    let mut child = ui.new_child(egui::UiBuilder::new().max_rect(rect).layout(Layout::left_to_right(Align::Center)));
    add(&mut child);
    resp
}

impl Studio {
    pub fn ui(&mut self, root_ui: &mut Ui) {
        let ctx = root_ui.ctx().clone();
        self.now = ctx.input(|i| i.time);
        self.sync_theme(&ctx);
        // le travail de fond
        self.poll_mcp(&ctx);
        self.copilot.poll();
        self.poll_tasks(&ctx);
        self.poll_search();
        self.poll_git();
        self.check_disk();
        self.korin_menu(&ctx);
        self.shortcuts(&ctx);
        let pal = self.pal.clone();

        // fermer la fenêtre : demander pour les fichiers modifiés
        if ctx.input(|i| i.viewport().close_requested()) && !self.allow_close && self.dirty_count() > 0 {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.dialog = Some(Dialog::Confirm {
                title: "Quitter KorinStudio".into(),
                text: format!("{} fichier(s) modifié(s) ne sont pas enregistrés. Quitter sans enregistrer ?", self.dirty_count()),
                action: ConfirmAction::Quit,
            });
        }

        // barre du haut
        egui::Panel::top("ks-top").exact_size(34.0).frame(Frame::new().fill(pal.title_bg).stroke(Stroke::new(1.0, pal.border))).show(root_ui, |ui| self.top_bar(ui, &pal));
        // barre d'état
        egui::Panel::bottom("ks-status").exact_size(22.0).frame(Frame::new().fill(pal.status_bg)).show(root_ui, |ui| self.status_bar(ui, &pal));
        // barre d'activité
        egui::Panel::left("ks-activity").exact_size(48.0).resizable(false).frame(Frame::new().fill(pal.activity_bg).stroke(Stroke::new(1.0, pal.border))).show(root_ui, |ui| self.activity_bar(ui, &pal));
        // barre latérale
        if self.sidebar_open {
            egui::Panel::left("ks-sidebar")
                .default_size(self.settings.sidebar_width)
                .size_range(170.0..=600.0)
                .resizable(true)
                .frame(Frame::new().fill(pal.sidebar_bg).stroke(Stroke::new(1.0, pal.border)))
                .show(root_ui, |ui| self.sidebar(ui, &pal));
        }
        // copilote
        if self.settings.copilot_open {
            egui::Panel::right("ks-copilot")
                .default_size(self.settings.copilot_width)
                .size_range(280.0..=900.0)
                .resizable(true)
                .frame(Frame::new().fill(pal.sidebar_bg).stroke(Stroke::new(1.0, pal.border)))
                .show(root_ui, |ui| self.copilot_panel(ui, &pal));
        }
        // panneau du bas
        if self.panel_open {
            egui::Panel::bottom("ks-panel")
                .default_size(240.0)
                .size_range(90.0..=900.0)
                .resizable(true)
                .frame(Frame::new().fill(pal.panel_bg).stroke(Stroke::new(1.0, pal.border)))
                .show(root_ui, |ui| self.bottom_panel(ui, &pal));
        }
        // l'éditeur
        egui::CentralPanel::default().frame(Frame::new().fill(pal.editor_bg)).show(root_ui, |ui| self.editor_area(ui, &pal));

        self.palette_ui(&ctx, &pal);
        self.dialogs(&ctx, &pal);
        self.toasts_ui(&ctx, &pal);

        // l'hôte de Korin n'est réveillé que par ses évènements : une image de temps en temps tant que quelque chose tourne
        let active = self.tasks.iter().any(|t| t.running()) || self.copilot.running() || !self.pending.is_empty() || self.search.rx.is_some() || !self.toasts.is_empty();
        ctx.request_repaint_after(std::time::Duration::from_millis(if active { 120 } else { 1000 }));
    }

    // -----------------------------------------------------------------------------------------------------------

    fn shortcuts(&mut self, ctx: &egui::Context) {
        use Command as C;
        let ctrl = Modifiers::COMMAND;
        let cs = Modifiers::COMMAND | Modifiers::SHIFT;
        let table: &[(Modifiers, Key, Command)] = &[
            (ctrl, Key::N, C::NewFile),
            (cs, Key::N, C::NewProject),
            (ctrl, Key::O, C::OpenFolder),
            (cs, Key::S, C::SaveAs),
            (ctrl, Key::S, C::Save),
            (ctrl, Key::W, C::CloseTab),
            (ctrl, Key::Q, C::Quit),
            (ctrl, Key::H, C::Replace),
            (cs, Key::F, C::FindInFiles),
            (ctrl, Key::F, C::Find),
            (ctrl, Key::Slash, C::ToggleComment),
            (cs, Key::K, C::DeleteLine),
            (Modifiers::SHIFT | Modifiers::ALT, Key::ArrowDown, C::DuplicateLine),
            (cs, Key::P, C::CommandPalette),
            (ctrl, Key::P, C::QuickOpen),
            (ctrl, Key::G, C::GotoLine),
            (Modifiers::NONE, Key::F8, C::NextProblem),
            (cs, Key::Tab, C::PrevTab),
            (ctrl, Key::Tab, C::NextTab),
            (ctrl, Key::B, C::ToggleSidebar),
            (ctrl, Key::J, C::TogglePanel),
            (Modifiers::COMMAND | Modifiers::ALT, Key::I, C::ToggleCopilot),
            (ctrl, Key::I, C::AskCopilot),
            (cs, Key::E, C::ShowExplorer),
            (cs, Key::G, C::ShowGit),
            (cs, Key::D, C::ShowRun),
            (cs, Key::M, C::ShowProblems),
            (cs, Key::U, C::ShowOutput),
            (ctrl, Key::Equals, C::ZoomIn),
            (ctrl, Key::Plus, C::ZoomIn),
            (ctrl, Key::Minus, C::ZoomOut),
            (Modifiers::ALT, Key::Z, C::ToggleWrap),
            (cs, Key::B, C::Build),
            (Modifiers::SHIFT, Key::F5, C::Stop),
            (Modifiers::NONE, Key::F5, C::Run),
            (ctrl, Key::Backtick, C::NewTerminal),
        ];
        let mut fired = Vec::new();
        ctx.input_mut(|i| {
            for (m, k, c) in table {
                if i.consume_key(*m, *k) {
                    fired.push(*c);
                }
            }
        });
        for c in fired {
            self.exec(c, ctx);
        }
        if ctx.input(|i| i.key_pressed(Key::Escape)) {
            if self.palette.is_some() {
                self.palette = None;
            } else if let Some(d) = self.active_doc() {
                if d.find.open {
                    d.find.open = false;
                }
            }
            if self.copilot.busy() && ctx.memory(|m| m.has_focus(Id::new("ks-chat-input"))) {
                self.copilot.cancel();
            }
        }
    }

    /// La barre de menus de Korin (les menus de la fenêtre y vont, comme ceux de toutes les apps de Korin).
    fn korin_menu(&mut self, ctx: &egui::Context) {
        #[cfg(target_os = "korin")]
        {
            let mut text = String::from("M*\n");
            text.push_str(&format!("I{}\t{}\t\n", 1000, "À propos de KorinStudio"));
            text.push_str("-\n");
            text.push_str(&format!("I{}\t{}\t{}\n", 1001, "Quitter", "Ctrl+Q"));
            let mut last = "";
            for (i, c) in COMMANDS.iter().enumerate() {
                if matches!(c.cmd, Command::About | Command::Quit | Command::Undo | Command::Redo) {
                    continue;
                }
                if c.menu != last {
                    text.push_str(&format!("M{}\n", c.menu));
                    last = c.menu;
                }
                let keys = c.keys.replace("Maj", "Shift");
                text.push_str(&format!("I{}\t{}\t{}\n", i + 1, c.label, keys));
            }
            if text != self.menu_sent {
                egui_korin::menu::set(&text);
                self.menu_sent = text;
            }
            for id in egui_korin::menu::take_chosen() {
                match id {
                    1000 => self.exec(Command::About, ctx),
                    1001 => self.exec(Command::Quit, ctx),
                    i if i >= 1 && (i as usize) <= COMMANDS.len() => self.exec(COMMANDS[i as usize - 1].cmd, ctx),
                    _ => {}
                }
            }
        }
        #[cfg(not(target_os = "korin"))]
        let _ = ctx;
    }

    // -----------------------------------------------------------------------------------------------------------
    // La barre du haut

    fn top_bar(&mut self, ui: &mut Ui, pal: &Palette) {
        let rect = ui.max_rect();
        let ctx = ui.ctx().clone();
        ui.horizontal_centered(|ui| {
            ui.add_space(10.0);
            logo(ui, 18.0, pal);
            ui.add_space(4.0);
            if !KORIN {
                // les menus dans la fenêtre (sous Korin, ils sont dans la barre de menus du système)
                let mut last = "";
                let menus: Vec<&str> = COMMANDS.iter().map(|c| c.menu).filter(|m| { let n = *m != last; last = m; n }).collect();
                for m in menus {
                    let r = ui.menu_button(RichText::new(m).size(13.0), |ui| {
                        ui.set_min_width(260.0);
                        for c in COMMANDS.iter().filter(|c| c.menu == m) {
                            if menu_item(ui, c.label, c.keys) {
                                self.exec(c.cmd, &ctx);
                                ui.close();
                            }
                        }
                        if m == "Fichier" && !self.settings.recent.is_empty() {
                            ui.separator();
                            ui.menu_button("Récents", |ui| {
                                for r in self.settings.recent.clone() {
                                    if ui.button(r.display().to_string()).clicked() {
                                        self.open_project(&r);
                                        ui.close();
                                    }
                                }
                            });
                        }
                    });
                    let _ = r;
                }
            } else {
                ui.label(RichText::new("KorinStudio").font(theme::bold(13.0)));
            }
        });
        // le centre de commandes
        let title = match &self.root {
            Some(r) => format!("{} — KorinStudio", workspace::project_name(r)),
            None => "KorinStudio".into(),
        };
        let cw = (rect.width() * 0.34).clamp(220.0, 560.0);
        let cr = Rect::from_center_size(pos2(rect.center().x, rect.center().y), vec2(cw, 24.0));
        let resp = ui.interact(cr, Id::new("ks-command-center"), Sense::click());
        let fill = if resp.hovered() { pal.hover } else { pal.input_bg };
        ui.painter().rect(cr, 6.0, fill, Stroke::new(1.0, pal.input_border), egui::StrokeKind::Inside);
        icons::paint(ui.painter(), Rect::from_center_size(pos2(cr.min.x + 16.0 + (cw / 2.0 - 16.0) - ui.fonts_mut(|f| f.layout_no_wrap(title.clone(), FontId::proportional(12.5), pal.fg).size().x) / 2.0 - 14.0, cr.center().y), vec2(13.0, 13.0)), Icon::Search, pal.fg_dim);
        ui.painter().text(cr.center(), Align2::CENTER_CENTER, &title, FontId::proportional(12.5), pal.fg_dim);
        if resp.on_hover_text("Aller au fichier (Ctrl+P) · Commandes (Ctrl+Maj+P)").clicked() {
            self.exec(Command::QuickOpen, &ctx);
        }
        // à droite : la mise en page (et la place des boutons d'Elegant sous Korin)
        let mut right = ui.new_child(egui::UiBuilder::new().max_rect(rect).layout(Layout::right_to_left(Align::Center)));
        right.add_space(if KORIN { 112.0 } else { 8.0 }); // (sous Korin : les boutons du cadre d'Elegant, dessinés sur le contenu)
        let toggles = [
            (Icon::Sparkle, self.settings.copilot_open, Command::ToggleCopilot, "Copilote (Ctrl+Alt+I)"),
            (Icon::Layout, self.panel_open, Command::TogglePanel, "Panneau du bas (Ctrl+J)"),
            (Icon::Split, self.sidebar_open, Command::ToggleSidebar, "Barre latérale (Ctrl+B)"),
        ];
        for (ic, on, cmd, tip) in toggles {
            let r = icons::button(&mut right, ic, 24.0, tip);
            if on {
                right.painter().line_segment([pos2(r.rect.min.x + 6.0, r.rect.max.y - 1.0), pos2(r.rect.max.x - 6.0, r.rect.max.y - 1.0)], Stroke::new(1.5, pal.accent));
            }
            if r.clicked() {
                self.exec(cmd, &ctx);
            }
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // La barre d'activité

    fn activity_bar(&mut self, ui: &mut Ui, pal: &Palette) {
        let ctx = ui.ctx().clone();
        let items = [
            (Icon::Files, Some(SideView::Explorer), "Explorateur (Ctrl+Maj+E)"),
            (Icon::Search, Some(SideView::Search), "Rechercher (Ctrl+Maj+F)"),
            (Icon::Git, Some(SideView::Git), "Gestion de versions (Ctrl+Maj+G)"),
            (Icon::Run, Some(SideView::Run), "Exécuter et construire (Ctrl+Maj+D)"),
            (Icon::Sparkle, None, "Copilote (Ctrl+Alt+I)"),
        ];
        ui.add_space(4.0);
        for (ic, view, tip) in items {
            let (r, resp) = ui.allocate_exact_size(vec2(48.0, 46.0), Sense::click());
            let active = match view {
                Some(v) => self.sidebar_open && self.side == v,
                None => self.settings.copilot_open,
            };
            let col = if active || resp.hovered() { pal.activity_fg } else { pal.activity_inactive };
            if active && view.is_some() {
                ui.painter().line_segment([pos2(r.min.x + 1.0, r.min.y + 4.0), pos2(r.min.x + 1.0, r.max.y - 4.0)], Stroke::new(2.0, pal.accent));
            }
            icons::paint(ui.painter(), Rect::from_center_size(r.center(), vec2(24.0, 24.0)), ic, if ic == Icon::Sparkle && active { pal.accent } else { col });
            // pastilles : changements Git, problèmes
            let badge = match view {
                Some(SideView::Git) => self.git.changes.len(),
                _ => 0,
            };
            if badge > 0 {
                let c = pos2(r.max.x - 14.0, r.max.y - 13.0);
                ui.painter().circle_filled(c, 7.5, pal.accent);
                ui.painter().text(c, Align2::CENTER_CENTER, badge.min(99).to_string(), theme::bold(9.0), pal.accent_fg);
            }
            if resp.on_hover_text(tip).clicked() {
                match view {
                    Some(v) => {
                        self.show_side(v);
                        if v == SideView::Git {
                            self.refresh_git();
                        }
                    }
                    None => self.exec(Command::ToggleCopilot, &ctx),
                }
            }
        }
        ui.with_layout(Layout::bottom_up(Align::Center), |ui| {
            ui.add_space(6.0);
            let r = egui::containers::menu::MenuButton::from_button(egui::Button::new("").frame(false).min_size(vec2(34.0, 34.0))).ui(ui, |ui| {
                ui.set_min_width(220.0);
                for c in [Command::CommandPalette, Command::ThemeSystem, Command::ThemeDark, Command::ThemeLight, Command::ZoomIn, Command::ZoomOut, Command::ToggleWrap, Command::About] {
                    let i = info(c);
                    if menu_item(ui, i.label, i.keys) {
                        self.exec(c, &ctx);
                        ui.close();
                    }
                }
            });
            icons::paint(ui.painter(), Rect::from_center_size(r.0.rect.center(), vec2(22.0, 22.0)), Icon::Gear, if r.0.hovered() { pal.activity_fg } else { pal.activity_inactive });
            r.0.on_hover_text("Gérer");
        });
    }

    // -----------------------------------------------------------------------------------------------------------
    // La barre latérale

    fn sidebar(&mut self, ui: &mut Ui, pal: &Palette) {
        self.settings.sidebar_width = ui.max_rect().width();
        let title = match self.side {
            SideView::Explorer => "EXPLORATEUR",
            SideView::Search => "RECHERCHE",
            SideView::Git => "GESTION DE VERSIONS",
            SideView::Run => "EXÉCUTER ET CONSTRUIRE",
        };
        ui.horizontal(|ui| {
            ui.set_height(34.0);
            ui.add_space(14.0);
            section_title(ui, pal, title);
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                ui.add_space(6.0);
                let ctx = ui.ctx().clone();
                match self.side {
                    SideView::Explorer if self.root.is_some() => {
                        let root = self.root.clone().unwrap();
                        if icons::button(ui, Icon::Collapse, 22.0, "Tout replier").clicked() {
                            self.tree.expanded.clear();
                            self.tree.expanded.insert(root.clone());
                        }
                        if icons::button(ui, Icon::Refresh, 22.0, "Actualiser").clicked() {
                            self.tree.invalidate();
                        }
                        if icons::button(ui, Icon::NewFolder, 22.0, "Nouveau dossier").clicked() {
                            self.dialog = Some(Dialog::NewEntry { dir: root.clone(), folder: true, name: String::new(), error: None });
                        }
                        if icons::button(ui, Icon::NewFile, 22.0, "Nouveau fichier").clicked() {
                            self.dialog = Some(Dialog::NewEntry { dir: root, folder: false, name: String::new(), error: None });
                        }
                    }
                    SideView::Search => {
                        if icons::button(ui, Icon::Clear, 22.0, "Effacer les résultats").clicked() {
                            self.search.results.clear();
                            self.search.query.clear();
                        }
                        if icons::button(ui, Icon::Refresh, 22.0, "Relancer").clicked() {
                            self.start_search(&ctx);
                        }
                    }
                    SideView::Git => {
                        if icons::button(ui, Icon::Refresh, 22.0, "Actualiser").clicked() {
                            self.refresh_git();
                        }
                    }
                    _ => {}
                }
            });
        });
        match self.side {
            SideView::Explorer => self.explorer(ui, pal),
            SideView::Search => self.search_view(ui, pal),
            SideView::Git => self.git_view(ui, pal),
            SideView::Run => self.run_view(ui, pal),
        }
    }

    fn explorer(&mut self, ui: &mut Ui, pal: &Palette) {
        let ctx = ui.ctx().clone();
        let Some(root) = self.root.clone() else {
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                ui.add_space(14.0);
                ui.vertical(|ui| {
                    ui.set_width(ui.available_width() - 14.0);
                    ui.label(RichText::new("Aucun dossier ouvert.").color(pal.fg_dim));
                    ui.add_space(8.0);
                    if primary_button(ui, pal, "Ouvrir un dossier").clicked() {
                        self.exec(Command::OpenFolder, &ctx);
                    }
                    ui.add_space(4.0);
                    if primary_button(ui, pal, "Nouveau projet").clicked() {
                        self.exec(Command::NewProject, &ctx);
                    }
                    ui.add_space(10.0);
                    ui.label(RichText::new("Ou demandez au copilote de créer un projet pour vous.").color(pal.fg_dim).size(12.0));
                });
            });
            return;
        };
        // le titre du projet
        let open = self.tree.expanded.contains(&root);
        let r = row(ui, pal, 22.0, false, Id::new("ks-proj-head"), |ui| {
            ui.add_space(4.0);
            icons::inline(ui, if open { Icon::ChevronDown } else { Icon::ChevronRight }, 14.0, pal.fg);
            ui.label(RichText::new(workspace::project_name(&root).to_uppercase()).font(theme::bold(11.0)));
            ui.label(RichText::new(self.kind.label()).size(10.5).color(pal.fg_faint));
        });
        if r.clicked() {
            if open {
                self.tree.expanded.remove(&root);
            } else {
                self.tree.expanded.insert(root.clone());
            }
        }
        if open {
            egui::ScrollArea::vertical().id_salt("ks-tree").auto_shrink([false, false]).show(ui, |ui| {
                let active = self.active.and_then(|i| self.docs.get(i)).and_then(|d| d.path.clone());
                let mut actions = Vec::new();
                self.tree_dir(ui, pal, &root, 1, active.as_deref(), &mut actions);
                for a in actions {
                    match a {
                        TreeAction::Open(p) => {
                            if let Err(e) = self.open_path(&p, None) {
                                self.toast(e, true);
                            }
                        }
                        TreeAction::NewFile(d) => self.dialog = Some(Dialog::NewEntry { dir: d, folder: false, name: String::new(), error: None }),
                        TreeAction::NewFolder(d) => self.dialog = Some(Dialog::NewEntry { dir: d, folder: true, name: String::new(), error: None }),
                        TreeAction::Rename(p) => {
                            let name = p.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
                            self.dialog = Some(Dialog::Rename { path: p, name, error: None });
                        }
                        TreeAction::Delete(p) => {
                            self.dialog = Some(Dialog::Confirm { title: "Supprimer".into(), text: format!("Supprimer « {} » ?", p.display()), action: ConfirmAction::Delete(p) })
                        }
                        TreeAction::Terminal(d) => {
                            self.panel_open = true;
                            self.panel_tab = PanelTab::Terminal;
                            let line = if cfg!(windows) { "cd".to_string() } else { "pwd".to_string() };
                            self.shell(&line, Some(d), &ctx);
                        }
                        TreeAction::Ask(p) => {
                            self.settings.copilot_open = true;
                            self.copilot.input = format!("{} ", p.display());
                            self.copilot.focus_input = true;
                        }
                    }
                }
                ui.add_space(30.0);
            });
        }
    }

    fn tree_dir(&mut self, ui: &mut Ui, pal: &Palette, dir: &Path, depth: usize, active: Option<&Path>, acts: &mut Vec<TreeAction>) {
        for e in self.tree.children(dir) {
            let indent = 8.0 + depth as f32 * 12.0;
            let open = e.is_dir && self.tree.expanded.contains(&e.path);
            let selected = active == Some(e.path.as_path());
            let dim = workspace::dimmed(&e.name);
            let modified = self.docs.iter().any(|d| d.dirty && d.path.as_deref() == Some(e.path.as_path()));
            let git_mark = self.git_status_of(&e.path);
            let resp = row(ui, pal, 22.0, selected, Id::new(("ks-tree", &e.path)), |ui| {
                ui.add_space(indent);
                if e.is_dir {
                    icons::inline(ui, if open { Icon::ChevronDown } else { Icon::ChevronRight }, 14.0, pal.fg_dim);
                    icons::inline(ui, if open { Icon::FolderOpen } else { Icon::Folder }, 15.0, if dim { pal.fg_faint } else { Color32::from_rgb(0xDC, 0xB6, 0x7A) });
                } else {
                    ui.add_space(14.0);
                    file_badge(ui, &e.path, 15.0);
                }
                let mut t = RichText::new(&e.name).size(13.0);
                if dim {
                    t = t.color(pal.fg_faint);
                }
                if let Some((_, c)) = &git_mark {
                    t = t.color(*c);
                }
                ui.add(egui::Label::new(t).truncate().selectable(false));
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    ui.add_space(10.0);
                    if let Some((m, c)) = &git_mark {
                        ui.label(RichText::new(m.as_str()).size(11.0).color(*c));
                    }
                    if modified {
                        icons::inline(ui, Icon::Dot, 10.0, pal.fg);
                    }
                });
            });
            if resp.clicked() {
                if e.is_dir {
                    if open {
                        self.tree.expanded.remove(&e.path);
                    } else {
                        self.tree.expanded.insert(e.path.clone());
                    }
                } else {
                    acts.push(TreeAction::Open(e.path.clone()));
                }
            }
            if resp.double_clicked() && !e.is_dir {
                if let Some(i) = self.doc_index(&e.path) {
                    self.docs[i].pinned = true;
                }
            }
            resp.context_menu(|ui| {
                let dir = if e.is_dir { e.path.clone() } else { e.path.parent().map(|p| p.to_path_buf()).unwrap_or_default() };
                if ui.button("Nouveau fichier…").clicked() {
                    acts.push(TreeAction::NewFile(dir.clone()));
                    ui.close();
                }
                if ui.button("Nouveau dossier…").clicked() {
                    acts.push(TreeAction::NewFolder(dir.clone()));
                    ui.close();
                }
                ui.separator();
                if ui.button("Renommer…").clicked() {
                    acts.push(TreeAction::Rename(e.path.clone()));
                    ui.close();
                }
                if ui.button("Supprimer").clicked() {
                    acts.push(TreeAction::Delete(e.path.clone()));
                    ui.close();
                }
                ui.separator();
                if ui.button("Copier le chemin").clicked() {
                    ui.ctx().copy_text(e.path.display().to_string());
                    ui.close();
                }
                if ui.button("Ouvrir dans le terminal").clicked() {
                    acts.push(TreeAction::Terminal(dir.clone()));
                    ui.close();
                }
                if ui.button("Demander au copilote…").clicked() {
                    acts.push(TreeAction::Ask(e.path.clone()));
                    ui.close();
                }
            });
            if open {
                self.tree_dir(ui, pal, &e.path, depth + 1, active, acts);
            }
        }
    }

    fn git_status_of(&self, p: &Path) -> Option<(String, Color32)> {
        let root = self.root.as_ref()?;
        let rel = p.strip_prefix(root).ok()?.to_string_lossy().replace('\\', "/");
        for (st, f) in &self.git.changes {
            if *f == rel || (p.is_dir() && f.starts_with(&format!("{rel}/"))) {
                let (m, c) = match st.as_str() {
                    "??" | "A" => ("U", self.pal.added),
                    "D" => ("D", self.pal.deleted),
                    _ => ("M", Color32::from_rgb(0xE2, 0xC0, 0x8D)),
                };
                return Some((if p.is_dir() { "•".into() } else { m.into() }, c));
            }
        }
        None
    }

    fn search_view(&mut self, ui: &mut Ui, pal: &Palette) {
        let ctx = ui.ctx().clone();
        ui.horizontal(|ui| {
            ui.add_space(12.0);
            ui.vertical(|ui| {
                ui.set_width(ui.available_width() - 10.0);
                let w = ui.available_width();
                let r = ui.add(egui::TextEdit::singleline(&mut self.search.query).hint_text("Rechercher").desired_width(w - 80.0));
                if self.search.focus {
                    r.request_focus();
                    self.search.focus = false;
                }
                let go = r.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter));
                ui.horizontal(|ui| {
                    let mut ch = false;
                    ch |= ui.toggle_value(&mut self.search.case, "Aa").on_hover_text("Respecter la casse").changed();
                    ch |= ui.toggle_value(&mut self.search.whole, "ab").on_hover_text("Mot entier").changed();
                    ch |= ui.toggle_value(&mut self.search.regex, ".*").on_hover_text("Expression régulière").changed();
                    if ch && !self.search.query.is_empty() {
                        self.start_search(&ctx);
                    }
                });
                if go {
                    self.start_search(&ctx);
                }
                if self.root.is_none() {
                    ui.label(RichText::new("Ouvrez un dossier pour y chercher.").color(pal.fg_dim));
                }
                if let Some(e) = &self.search.error {
                    ui.label(RichText::new(e.as_str()).color(pal.error));
                }
                if self.search.rx.is_some() {
                    ui.horizontal(|ui| {
                        icons::inline(ui, Icon::Spinner(icons::spinner_phase(ui)), 14.0, pal.accent);
                        ui.label("Recherche…");
                    });
                } else if !self.search.results.is_empty() {
                    let n: usize = self.search.results.iter().map(|(_, h)| h.len()).sum();
                    ui.label(RichText::new(format!("{n} résultat(s) dans {} fichier(s)", self.search.results.len())).color(pal.fg_dim).size(12.0));
                }
            });
        });
        ui.add_space(4.0);
        let mut open: Option<(PathBuf, usize, usize)> = None;
        egui::ScrollArea::vertical().id_salt("ks-search").auto_shrink([false, false]).show(ui, |ui| {
            let results = std::mem::take(&mut self.search.results);
            for (f, hits) in &results {
                let collapsed = self.search.collapsed.contains(f);
                let r = row(ui, pal, 22.0, false, Id::new(("ks-sr", f)), |ui| {
                    ui.add_space(6.0);
                    icons::inline(ui, if collapsed { Icon::ChevronRight } else { Icon::ChevronDown }, 14.0, pal.fg_dim);
                    file_badge(ui, f, 14.0);
                    ui.label(RichText::new(f.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default()).size(13.0));
                    let dir = f.parent().map(|p| workspace::rel(self.root.as_deref(), p)).unwrap_or_default();
                    ui.add(egui::Label::new(RichText::new(dir).size(11.0).color(pal.fg_faint)).truncate());
                });
                if r.clicked() {
                    if collapsed {
                        self.search.collapsed.remove(f);
                    } else {
                        self.search.collapsed.insert(f.clone());
                    }
                }
                if collapsed {
                    continue;
                }
                for (l, c, t) in hits {
                    let r = row(ui, pal, 20.0, false, Id::new(("ks-sh", f, *l)), |ui| {
                        ui.add_space(34.0);
                        ui.add(egui::Label::new(RichText::new(t.trim()).size(12.0).family(egui::FontFamily::Monospace)).truncate().selectable(false));
                    });
                    if r.clicked() {
                        open = Some((f.clone(), *l, *c));
                    }
                }
            }
            self.search.results = results;
        });
        if let Some((f, l, c)) = open {
            if let Err(e) = self.open_path(&f, Some((l, c))) {
                self.toast(e, true);
            }
        }
    }

    fn git_view(&mut self, ui: &mut Ui, pal: &Palette) {
        let ctx = ui.ctx().clone();
        ui.horizontal(|ui| {
            ui.add_space(12.0);
            ui.vertical(|ui| {
                ui.set_width(ui.available_width() - 10.0);
                if self.root.is_none() {
                    ui.label(RichText::new("Ouvrez un dossier.").color(pal.fg_dim));
                    return;
                }
                if !self.git.is_repo {
                    ui.label(RichText::new("Ce dossier n'est pas un dépôt Git.").color(pal.fg_dim));
                    ui.add_space(6.0);
                    if primary_button(ui, pal, "Initialiser le dépôt").clicked() {
                        self.shell("git init", None, &ctx);
                        self.git.last = None;
                    }
                    return;
                }
                if let Some(b) = &self.git.branch {
                    ui.horizontal(|ui| {
                        icons::inline(ui, Icon::Branch, 14.0, pal.fg_dim);
                        ui.label(RichText::new(b.as_str()).color(pal.fg_dim));
                    });
                }
                ui.add(egui::TextEdit::multiline(&mut self.git.message).hint_text("Message (Ctrl+Entrée pour valider)").desired_rows(2).desired_width(f32::INFINITY));
                let commit = primary_button(ui, pal, "✔ Valider").clicked() || ui.input(|i| i.modifiers.command && i.key_pressed(Key::Enter));
                if commit && !self.git.message.trim().is_empty() {
                    let msg = self.git.message.replace('"', "'");
                    self.shell(&format!("git add -A && git commit -m \"{msg}\""), None, &ctx);
                    self.git.message.clear();
                    self.git.last = None;
                }
                ui.add_space(6.0);
                ui.label(RichText::new(format!("MODIFICATIONS ({})", self.git.changes.len())).font(theme::bold(11.0)).color(pal.fg_dim));
            });
        });
        let mut open = None;
        egui::ScrollArea::vertical().id_salt("ks-git").auto_shrink([false, false]).show(ui, |ui| {
            for (st, f) in self.git.changes.clone() {
                let p = self.root.as_ref().map(|r| r.join(&f)).unwrap_or_default();
                let r = row(ui, pal, 22.0, false, Id::new(("ks-git", &f)), |ui| {
                    ui.add_space(14.0);
                    file_badge(ui, &p, 14.0);
                    ui.label(RichText::new(Path::new(&f).file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default()).size(13.0));
                    ui.add(egui::Label::new(RichText::new(f.as_str()).size(11.0).color(pal.fg_faint)).truncate());
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        ui.add_space(10.0);
                        let (m, c) = match st.as_str() {
                            "??" => ("U", pal.added),
                            "A" => ("A", pal.added),
                            "D" => ("D", pal.deleted),
                            _ => ("M", Color32::from_rgb(0xE2, 0xC0, 0x8D)),
                        };
                        ui.label(RichText::new(m).color(c).font(theme::bold(12.0)));
                    });
                });
                if r.clicked() && p.is_file() {
                    open = Some(p);
                }
            }
        });
        if let Some(p) = open {
            let _ = self.open_path(&p, None);
        }
    }

    fn run_view(&mut self, ui: &mut Ui, pal: &Palette) {
        let ctx = ui.ctx().clone();
        ui.horizontal(|ui| {
            ui.add_space(12.0);
            ui.vertical(|ui| {
                ui.set_width(ui.available_width() - 10.0);
                let Some(root) = self.root.clone() else {
                    ui.label(RichText::new("Ouvrez un projet pour le construire et le lancer.").color(pal.fg_dim));
                    return;
                };
                let kind = ProjectKind::detect(&root);
                ui.label(RichText::new(format!("{} · {}", workspace::project_name(&root), kind.label())).font(theme::bold(13.0)));
                if let Ok(Some(c)) = workspace::build_cmd(kind, &root) {
                    ui.label(RichText::new(format!("Construire : {}", c.display())).size(11.5).color(pal.fg_dim).family(egui::FontFamily::Monospace));
                }
                if let Ok(c) = workspace::run_cmd(kind, &root, &[]) {
                    ui.label(RichText::new(format!("Lancer : {}", c.display())).size(11.5).color(pal.fg_dim).family(egui::FontFamily::Monospace));
                }
                ui.add_space(8.0);
                let running = self.tasks.iter().any(|t| t.running() && t.kind != TaskKind::Shell);
                ui.horizontal(|ui| {
                    if primary_button(ui, pal, "▶  Exécuter").on_hover_text("F5").clicked() {
                        self.exec(Command::Run, &ctx);
                    }
                    if ui.button("Construire").on_hover_text("Ctrl+Maj+B").clicked() {
                        self.exec(Command::Build, &ctx);
                    }
                    if running && ui.button("■ Arrêter").clicked() {
                        self.exec(Command::Stop, &ctx);
                    }
                });
                ui.add_space(4.0);
                ui.horizontal(|ui| {
                    ui.label("Arguments");
                    ui.add(egui::TextEdit::singleline(&mut self.run_args).desired_width(f32::INFINITY));
                });
                if workspace::clean_cmd(kind, &root).is_some() && ui.button("Nettoyer").clicked() {
                    self.exec(Command::Clean, &ctx);
                }
                ui.add_space(10.0);
                ui.label(RichText::new("TÂCHES").font(theme::bold(11.0)).color(pal.fg_dim));
            });
        });
        let mut show = None;
        egui::ScrollArea::vertical().id_salt("ks-tasks").auto_shrink([false, false]).show(ui, |ui| {
            for t in self.tasks.iter().rev().filter(|t| t.kind != TaskKind::Shell) {
                let r = row(ui, pal, 22.0, self.output_task == Some(t.id), Id::new(("ks-task", t.id)), |ui| {
                    ui.add_space(14.0);
                    let (ic, c) = match t.exit() {
                        None => (Icon::Spinner(icons::spinner_phase(ui)), pal.accent),
                        Some(Some(0)) => (Icon::Check, pal.success),
                        _ => (Icon::Error, pal.error),
                    };
                    icons::inline(ui, ic, 14.0, c);
                    ui.label(RichText::new(&t.title).size(12.5));
                    let secs = t.ended.unwrap_or_else(std::time::Instant::now).duration_since(t.started).as_secs_f32();
                    ui.label(RichText::new(format!("{secs:.1} s")).size(11.0).color(pal.fg_faint));
                });
                if r.clicked() {
                    show = Some(t.id);
                }
            }
        });
        if let Some(id) = show {
            self.output_task = Some(id);
            self.panel_open = true;
            self.panel_tab = PanelTab::Output;
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // L'éditeur : onglets, fil d'Ariane, page d'accueil

    fn editor_area(&mut self, ui: &mut Ui, pal: &Palette) {
        if self.docs.is_empty() {
            self.welcome(ui, pal);
            return;
        }
        let ctx = ui.ctx().clone();
        // les onglets
        let tabs_h = 35.0;
        let full = ui.max_rect();
        let tabs_rect = Rect::from_min_max(full.min, pos2(full.max.x, full.min.y + tabs_h));
        ui.painter().rect_filled(tabs_rect, 0.0, pal.tab_bg);
        ui.painter().hline(tabs_rect.x_range(), tabs_rect.max.y - 0.5, Stroke::new(1.0, pal.tab_border));
        let mut tabs_ui = ui.new_child(egui::UiBuilder::new().max_rect(tabs_rect).layout(Layout::left_to_right(Align::Min)));
        let mut close = None;
        let mut activate = None;
        egui::ScrollArea::horizontal().id_salt("ks-tabs").scroll_bar_visibility(egui::scroll_area::ScrollBarVisibility::AlwaysHidden).show(&mut tabs_ui, |ui| {
            ui.spacing_mut().item_spacing.x = 0.0;
            for (i, d) in self.docs.iter().enumerate() {
                let active = self.active == Some(i);
                let title = d.title();
                let font = FontId::proportional(13.0);
                let tw = ui.fonts_mut(|f| f.layout_no_wrap(title.clone(), font.clone(), pal.fg).size().x);
                let w = tw + 64.0;
                let (r, resp) = ui.allocate_exact_size(vec2(w, tabs_h), Sense::click());
                let fill = if active { pal.tab_active_bg } else { pal.tab_bg };
                ui.painter().rect_filled(r, 0.0, fill);
                if active {
                    ui.painter().hline(r.x_range(), r.min.y + 0.5, Stroke::new(2.0, pal.accent));
                    ui.painter().vline(r.max.x - 0.5, r.y_range(), Stroke::new(1.0, pal.tab_border));
                } else {
                    ui.painter().vline(r.max.x - 0.5, r.y_range(), Stroke::new(1.0, pal.tab_border));
                }
                // la pastille du langage
                let ir = Rect::from_center_size(pos2(r.min.x + 18.0, r.center().y), vec2(15.0, 15.0));
                let mut iu = ui.new_child(egui::UiBuilder::new().max_rect(ir));
                if let Some(p) = &d.path {
                    file_badge(&mut iu, p, 15.0);
                } else {
                    icons::paint(ui.painter(), ir, Icon::File, pal.fg_dim);
                }
                let col = if active { pal.fg } else { pal.fg_dim };
                let mut tcol = col;
                if d.agent_touched.is_some_and(|t| self.now - t < 3.0) {
                    tcol = pal.accent;
                }
                let tp = pos2(r.min.x + 30.0, r.center().y);
                ui.painter().text(tp, Align2::LEFT_CENTER, &title, if d.pinned || active { font.clone() } else { font.clone() }, tcol);
                if !d.pinned && !d.dirty && false {
                    let _ = ();
                }
                // fermer, ou le point des modifications
                let cr = Rect::from_center_size(pos2(r.max.x - 17.0, r.center().y), vec2(18.0, 18.0));
                let hover_close = ui.rect_contains_pointer(cr);
                if hover_close {
                    ui.painter().rect_filled(cr, 3.0, pal.hover);
                }
                if d.dirty && !hover_close {
                    icons::paint(ui.painter(), cr.shrink(4.0), Icon::Dot, pal.fg);
                } else if active || resp.hovered() || hover_close {
                    icons::paint(ui.painter(), cr.shrink(3.0), Icon::Close, col);
                }
                if resp.clicked() {
                    if hover_close {
                        close = Some(d.id);
                    } else {
                        activate = Some(i);
                    }
                }
                if resp.middle_clicked() {
                    close = Some(d.id);
                }
                let id = d.id;
                resp.context_menu(|ui| {
                    if ui.button("Fermer").clicked() {
                        close = Some(id);
                        ui.close();
                    }
                    if let Some(p) = &d.path {
                        if ui.button("Copier le chemin").clicked() {
                            ui.ctx().copy_text(p.display().to_string());
                            ui.close();
                        }
                    }
                });
            }
        });
        if let Some(i) = activate {
            self.active = Some(i);
            self.focus_editor = true;
        }
        if let Some(id) = close {
            self.close_doc(id, false);
        }
        let Some(ai) = self.active.filter(|&i| i < self.docs.len()) else { return };
        // le fil d'Ariane, et la barre d'un conflit
        let mut y = tabs_rect.max.y;
        let crumbs_h = 22.0;
        let cr = Rect::from_min_max(pos2(full.min.x, y), pos2(full.max.x, y + crumbs_h));
        {
            let d = &self.docs[ai];
            let mut cu = ui.new_child(egui::UiBuilder::new().max_rect(cr.shrink2(vec2(14.0, 0.0))).layout(Layout::left_to_right(Align::Center)));
            let parts: Vec<String> = match &d.path {
                Some(p) => workspace::rel(self.root.as_deref(), p).split('/').map(|s| s.to_string()).collect(),
                None => vec![d.title()],
            };
            for (k, part) in parts.iter().enumerate() {
                if k > 0 {
                    icons::inline(&mut cu, Icon::ChevronRight, 12.0, pal.fg_faint);
                }
                cu.label(RichText::new(part.as_str()).size(12.0).color(pal.fg_dim));
            }
        }
        y += crumbs_h;
        if self.docs[ai].conflict {
            let br = Rect::from_min_max(pos2(full.min.x, y), pos2(full.max.x, y + 30.0));
            ui.painter().rect_filled(br, 0.0, pal.warning.gamma_multiply(0.25));
            let mut bu = ui.new_child(egui::UiBuilder::new().max_rect(br.shrink2(vec2(12.0, 0.0))).layout(Layout::left_to_right(Align::Center)));
            icons::inline(&mut bu, Icon::Warning, 14.0, pal.warning);
            bu.label("Le fichier a changé sur le disque (le copilote ?) et vos modifications ne sont pas enregistrées.");
            if bu.button("Recharger").clicked() {
                self.docs[ai].reload();
            }
            if bu.button("Garder les miennes").clicked() {
                self.docs[ai].conflict = false;
            }
            y += 30.0;
        }
        let er = Rect::from_min_max(pos2(full.min.x, y), full.max);
        let mut eu = ui.new_child(egui::UiBuilder::new().max_rect(er).layout(Layout::top_down(Align::Min)));
        let path = self.docs[ai].path.clone();
        let diags: Vec<crate::tasks::Diagnostic> = self.diagnostics.iter().filter(|d| path.as_deref() == Some(d.path.as_path())).cloned().collect();
        let st = EditorStyle { pal, code_size: self.settings.code_size, tab_size: self.settings.tab_size, wrap: self.settings.word_wrap, diagnostics: diags.iter().collect(), now: self.now };
        if self.focus_editor {
            ctx.memory_mut(|m| m.request_focus(Id::new(("ks-editor", self.docs[ai].id))));
            self.focus_editor = false;
        }
        let out = editor::show(&mut eu, &mut self.docs[ai], &st);
        if out.changed && self.settings.autosave {
            self.save_doc(ai);
        }
    }

    fn welcome(&mut self, ui: &mut Ui, pal: &Palette) {
        let ctx = ui.ctx().clone();
        let r = ui.max_rect();
        let w = (r.width() - 80.0).clamp(300.0, 860.0);
        let col_rect = Rect::from_min_size(pos2(r.center().x - w / 2.0, r.min.y + (r.height() * 0.12).min(90.0)), vec2(w, r.height()));
        let mut u = ui.new_child(egui::UiBuilder::new().max_rect(col_rect).layout(Layout::top_down(Align::Min)));
        egui::ScrollArea::vertical().id_salt("ks-welcome").show(&mut u, |ui| {
            ui.horizontal(|ui| {
                logo(ui, 46.0, pal);
                ui.add_space(8.0);
                ui.vertical(|ui| {
                    ui.label(RichText::new("KorinStudio").font(theme::bold(30.0)));
                    ui.label(RichText::new("L'environnement de développement de Korin OS").size(15.0).color(pal.fg_dim));
                });
            });
            ui.add_space(26.0);
            ui.columns(2, |cols| {
                let ui = &mut cols[0];
                ui.label(RichText::new("Démarrer").font(theme::bold(16.0)));
                ui.add_space(6.0);
                for (ic, label, cmd) in [
                    (Icon::Plus, "Nouveau projet…", Command::NewProject),
                    (Icon::NewFile, "Nouveau fichier", Command::NewFile),
                    (Icon::FolderOpen, "Ouvrir un dossier…", Command::OpenFolder),
                    (Icon::File, "Ouvrir un fichier…", Command::OpenFile),
                    (Icon::Sparkle, "Demander au copilote", Command::AskCopilot),
                ] {
                    if link_row(ui, pal, ic, label).clicked() {
                        self.exec(cmd, &ctx);
                    }
                }
                ui.add_space(18.0);
                ui.label(RichText::new("Récents").font(theme::bold(16.0)));
                ui.add_space(6.0);
                if self.settings.recent.is_empty() {
                    ui.label(RichText::new("Aucun projet récent.").color(pal.fg_dim));
                }
                for p in self.settings.recent.clone().into_iter().take(8) {
                    ui.horizontal(|ui| {
                        let name = workspace::project_name(&p);
                        if ui.add(egui::Label::new(RichText::new(name).color(pal.info)).sense(Sense::click())).on_hover_cursor(egui::CursorIcon::PointingHand).clicked() {
                            self.open_project(&p);
                        }
                        ui.add(egui::Label::new(RichText::new(p.parent().map(|x| x.display().to_string()).unwrap_or_default()).color(pal.fg_faint).size(12.0)).truncate());
                    });
                }
                let ui = &mut cols[1];
                ui.label(RichText::new("Le copilote").font(theme::bold(16.0)));
                ui.add_space(6.0);
                card(ui, pal, Icon::Sparkle, "Il travaille dans l'IDE", "Claude Code, Claude par l'API, Mistral Vibe ou MAI : il crée des projets, ouvre et modifie les fichiers sous vos yeux, construit, lance et corrige.");
                card(ui, pal, Icon::Build, "Tous les langages de Korin", "Rust (Cargo), C et C++ (gcc, make), apps de Korin (pibuild), C# (csbuild, dotnet), FreeBASIC (fbc), assembleur, Python.");
                card(ui, pal, Icon::Terminal, "Raccourcis de VS Code", "Ctrl+P fichier, Ctrl+Maj+P commandes, Ctrl+Maj+B construire, F5 exécuter, Ctrl+Alt+I copilote.");
            });
        });
    }

    // -----------------------------------------------------------------------------------------------------------
    // Le panneau du bas

    fn bottom_panel(&mut self, ui: &mut Ui, pal: &Palette) {
        let ctx = ui.ctx().clone();
        let errors = self.diagnostics.iter().filter(|d| d.severity == Severity::Error).count();
        let warnings = self.diagnostics.iter().filter(|d| d.severity == Severity::Warning).count();
        ui.horizontal(|ui| {
            ui.set_height(32.0);
            ui.add_space(12.0);
            for (tab, label) in [
                (PanelTab::Problems, format!("PROBLÈMES{}", if errors + warnings > 0 { format!("  {}", errors + warnings) } else { String::new() })),
                (PanelTab::Output, "SORTIE".to_string()),
                (PanelTab::Terminal, "TERMINAL".to_string()),
                (PanelTab::AgentLog, "JOURNAL DE L'AGENT".to_string()),
            ] {
                let active = self.panel_tab == tab;
                let font = FontId::proportional(11.5);
                let tw = ui.fonts_mut(|f| f.layout_no_wrap(label.clone(), font.clone(), pal.fg).size().x);
                let (r, resp) = ui.allocate_exact_size(vec2(tw + 16.0, 32.0), Sense::click());
                ui.painter().text(r.center(), Align2::CENTER_CENTER, &label, font, if active || resp.hovered() { pal.fg } else { pal.fg_dim });
                if active {
                    ui.painter().hline(r.shrink2(vec2(6.0, 0.0)).x_range(), r.max.y - 3.0, Stroke::new(1.0, pal.accent));
                }
                if resp.clicked() {
                    self.panel_tab = tab;
                }
            }
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                ui.add_space(8.0);
                if icons::button(ui, Icon::Close, 22.0, "Fermer le panneau").clicked() {
                    self.panel_open = false;
                }
                match self.panel_tab {
                    PanelTab::Terminal => {
                        if icons::button(ui, Icon::Trash, 22.0, "Effacer le terminal").clicked() {
                            self.tasks.retain(|t| t.kind != TaskKind::Shell || t.running());
                        }
                        if icons::button(ui, Icon::Stop, 22.0, "Arrêter la commande").clicked() {
                            for t in &mut self.tasks {
                                if t.kind == TaskKind::Shell && t.running() {
                                    t.kill();
                                }
                            }
                        }
                    }
                    PanelTab::Output => {
                        let label = self.output_task.and_then(|id| self.task(id)).map(|t| t.title.clone()).unwrap_or_else(|| "—".into());
                        let mut sel = self.output_task;
                        egui::ComboBox::from_id_salt("ks-out-sel").selected_text(label).width(180.0).show_ui(ui, |ui| {
                            for t in self.tasks.iter().rev().filter(|t| t.kind != TaskKind::Shell) {
                                ui.selectable_value(&mut sel, Some(t.id), format!("{} #{}", t.title, t.id));
                            }
                        });
                        self.output_task = sel;
                    }
                    PanelTab::Problems => {
                        if errors + warnings > 0 && ui.button(RichText::new("✨ Corriger avec le copilote").size(11.5)).clicked() {
                            self.exec(Command::FixProblems, &ctx);
                        }
                    }
                    PanelTab::AgentLog => {}
                }
            });
        });
        ui.painter().hline(ui.max_rect().x_range(), ui.cursor().min.y, Stroke::new(1.0, pal.border));
        match self.panel_tab {
            PanelTab::Problems => self.problems(ui, pal),
            PanelTab::Output => {
                let t = self.output_task.and_then(|id| self.tasks.iter().find(|t| t.id == id));
                match t {
                    Some(t) => {
                        let lines = t.out.lock().unwrap().lines.clone();
                        self.console(ui, pal, "ks-out", &lines, Some(t.cmd.cwd.clone()));
                    }
                    None => {
                        ui.add_space(10.0);
                        ui.horizontal(|ui| {
                            ui.add_space(14.0);
                            ui.label(RichText::new("Rien à montrer : construisez (Ctrl+Maj+B) ou exécutez (F5) le projet.").color(pal.fg_dim));
                        });
                    }
                }
            }
            PanelTab::Terminal => self.terminal(ui, pal),
            PanelTab::AgentLog => {
                let lines: Vec<(LineKind, String)> = self.copilot.log.iter().map(|l| (LineKind::Out, l.clone())).collect();
                self.console(ui, pal, "ks-agentlog", &lines, None);
            }
        }
    }

    fn problems(&mut self, ui: &mut Ui, pal: &Palette) {
        if self.diagnostics.is_empty() {
            ui.add_space(10.0);
            ui.horizontal(|ui| {
                ui.add_space(14.0);
                ui.label(RichText::new("Aucun problème détecté dans l'espace de travail.").color(pal.fg_dim));
            });
            return;
        }
        let mut open = None;
        egui::ScrollArea::vertical().id_salt("ks-problems").auto_shrink([false, false]).show(ui, |ui| {
            let mut files: Vec<PathBuf> = Vec::new();
            for d in &self.diagnostics {
                if !files.contains(&d.path) {
                    files.push(d.path.clone());
                }
            }
            for f in files {
                let ds: Vec<_> = self.diagnostics.iter().filter(|d| d.path == f).collect();
                row(ui, pal, 22.0, false, Id::new(("ks-pf", &f)), |ui| {
                    ui.add_space(10.0);
                    icons::inline(ui, Icon::ChevronDown, 14.0, pal.fg_dim);
                    file_badge(ui, &f, 14.0);
                    ui.label(RichText::new(f.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default()).size(13.0));
                    ui.label(RichText::new(workspace::rel(self.root.as_deref(), f.parent().unwrap_or(Path::new("")))).size(11.5).color(pal.fg_faint));
                    egui::Frame::new().fill(pal.badge_bg).corner_radius(8.0).inner_margin(Margin::symmetric(5, 0)).show(ui, |ui| {
                        ui.label(RichText::new(ds.len().to_string()).size(10.5).color(Color32::WHITE));
                    });
                });
                for d in ds {
                    let r = row(ui, pal, 22.0, false, Id::new(("ks-pd", &d.path, d.line, d.column, &d.message)), |ui| {
                        ui.add_space(34.0);
                        let (ic, c) = match d.severity {
                            Severity::Error => (Icon::Error, pal.error),
                            Severity::Warning => (Icon::Warning, pal.warning),
                            Severity::Info => (Icon::Info, pal.info),
                        };
                        icons::inline(ui, ic, 14.0, c);
                        ui.add(egui::Label::new(RichText::new(d.message.as_str()).size(12.5)).truncate().selectable(false));
                        ui.label(RichText::new(format!("{}  [Ln {}, Col {}]", d.source, d.line, d.column)).size(11.5).color(pal.fg_faint));
                    });
                    if r.clicked() {
                        open = Some((d.path.clone(), d.line, d.column));
                    }
                }
            }
        });
        if let Some((p, l, c)) = open {
            if let Err(e) = self.open_path(&p, Some((l, c))) {
                self.toast(e, true);
            }
        }
    }

    /// Une console : les lignes, avec les emplacements « fichier:ligne:col » cliquables.
    fn console(&mut self, ui: &mut Ui, pal: &Palette, id: &str, lines: &[(LineKind, String)], cwd: Option<PathBuf>) {
        let font = FontId::monospace(self.settings.code_size - 1.0);
        let row_h = ui.fonts_mut(|f| f.row_height(&font)) + 1.0;
        let mut open = None;
        egui::ScrollArea::both().id_salt(id).auto_shrink([false, false]).stick_to_bottom(true).show_rows(ui, row_h, lines.len(), |ui, range| {
            ui.spacing_mut().item_spacing.y = 0.0;
            for i in range {
                let (k, l) = &lines[i];
                let col = match k {
                    LineKind::Out => pal.fg,
                    LineKind::Err => {
                        if l.contains("error") || l.contains("erreur") || l.contains("Error") {
                            pal.error
                        } else if l.contains("warning") {
                            pal.warning
                        } else {
                            pal.fg
                        }
                    }
                    LineKind::Info => pal.info,
                };
                let r = ui.horizontal(|ui| {
                    ui.add_space(14.0);
                    ui.add(egui::Label::new(RichText::new(l.as_str()).font(font.clone()).color(col)).extend())
                });
                if let Some(cwd) = &cwd {
                    let resp = r.inner;
                    if resp.hovered() {
                        let ds = crate::tasks::parse_diagnostics(l, cwd);
                        if let Some(d) = ds.first() {
                            ui.ctx().set_cursor_icon(egui::CursorIcon::PointingHand);
                            if resp.interact(Sense::click()).clicked() {
                                open = Some((d.path.clone(), d.line, d.column));
                            }
                        }
                    }
                }
            }
        });
        if let Some((p, l, c)) = open {
            let _ = self.open_path(&p, Some((l, c)));
        }
    }

    fn terminal(&mut self, ui: &mut Ui, pal: &Palette) {
        let ctx = ui.ctx().clone();
        let input_h = 28.0;
        let avail = ui.available_rect_before_wrap();
        let out_rect = Rect::from_min_max(avail.min, pos2(avail.max.x, avail.max.y - input_h));
        let in_rect = Rect::from_min_max(pos2(avail.min.x, avail.max.y - input_h), avail.max);
        let mut lines = Vec::new();
        for t in self.tasks.iter().filter(|t| t.kind == TaskKind::Shell) {
            let o = t.out.lock().unwrap();
            lines.extend(o.lines.iter().cloned());
            if let Some(code) = o.exit {
                if code != Some(0) {
                    lines.push((LineKind::Err, format!("[code {}]", code.map(|c| c.to_string()).unwrap_or_else(|| "?".into()))));
                }
            }
        }
        let cwd = self.root.clone();
        let mut ou = ui.new_child(egui::UiBuilder::new().max_rect(out_rect).layout(Layout::top_down(Align::Min)));
        self.console(&mut ou, pal, "ks-term", &lines, cwd.clone());
        let mut iu = ui.new_child(egui::UiBuilder::new().max_rect(in_rect.shrink2(vec2(10.0, 2.0))).layout(Layout::left_to_right(Align::Center)));
        let dir = cwd.as_ref().map(|c| workspace::project_name(c)).unwrap_or_else(|| "~".into());
        iu.label(RichText::new(format!("{dir} ❯")).color(pal.success).family(egui::FontFamily::Monospace));
        let running = self.tasks.iter().rev().find(|t| t.kind == TaskKind::Shell && t.running()).map(|t| t.id);
        let te = egui::TextEdit::singleline(&mut self.term_input)
            .id(Id::new("ks-term-input"))
            .font(egui::TextStyle::Monospace)
            .frame(Frame::NONE)
            .hint_text(if running.is_some() { "entrée du programme…" } else { "commande (Entrée)" })
            .desired_width(f32::INFINITY);
        let r = iu.add(te);
        if r.has_focus() {
            let up = iu.input(|i| i.key_pressed(Key::ArrowUp));
            let down = iu.input(|i| i.key_pressed(Key::ArrowDown));
            if (up || down) && !self.term_history.is_empty() {
                let n = self.term_history.len();
                let p = match (self.term_hist_pos, up) {
                    (None, true) => n - 1,
                    (Some(p), true) => p.saturating_sub(1),
                    (Some(p), false) => (p + 1).min(n - 1),
                    (None, false) => n - 1,
                };
                self.term_hist_pos = Some(p);
                self.term_input = self.term_history[p].clone();
            }
        }
        if r.lost_focus() && iu.input(|i| i.key_pressed(Key::Enter)) {
            let line = std::mem::take(&mut self.term_input);
            if let Some(id) = running {
                if let Some(t) = self.tasks.iter_mut().find(|t| t.id == id) {
                    t.write_stdin(&format!("{line}\n"));
                }
            } else if !line.trim().is_empty() {
                if let Some(d) = line.trim().strip_prefix("cd ") {
                    let p = self.resolve(d);
                    if p.is_dir() {
                        self.toast(format!("Le terminal suit le projet ouvert ; ouvrez {} comme dossier pour y travailler", p.display()), false);
                    }
                }
                self.term_history.push(line.clone());
                self.term_hist_pos = None;
                if line.trim() == "clear" || line.trim() == "cls" {
                    self.tasks.retain(|t| t.kind != TaskKind::Shell);
                } else {
                    self.shell(&line, None, &ctx);
                }
            }
            r.request_focus();
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // Le copilote

    fn copilot_panel(&mut self, ui: &mut Ui, pal: &Palette) {
        self.settings.copilot_width = ui.max_rect().width();
        let ctx = ui.ctx().clone();
        let cu = CopilotUi { pal, code_size: self.settings.code_size, context_label: self.context_label() };
        let acts = copilot::show(ui, &mut self.copilot, &cu);
        for a in acts {
            match a {
                CopilotAction::Send(t) => self.ask(t, &ctx),
                CopilotAction::OpenFile(p, l) => {
                    let p = self.resolve(&p);
                    let _ = self.open_path(&p, Some((l, 1)));
                }
                CopilotAction::Insert(code) => {
                    if let Some(d) = self.active_doc() {
                        let b = editor::byte_index(&d.text, d.cursor_char);
                        let mut t = d.text.clone();
                        t.insert_str(b, &code);
                        d.set_text(t);
                        d.dirty = true;
                    }
                }
                CopilotAction::NewChat => self.copilot.reset(),
                CopilotAction::ChangeKind(k) => self.set_agent(k),
                CopilotAction::ShowLog => {
                    self.panel_open = true;
                    self.panel_tab = PanelTab::AgentLog;
                }
            }
        }
        if let Some(m) = &self.copilot.current_mode {
            let key = format!("{:?}", self.copilot.kind);
            if self.settings.agent_modes.get(&key) != Some(m) {
                self.settings.agent_modes.insert(key, m.clone());
                self.settings.save();
            }
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // La barre d'état

    fn status_bar(&mut self, ui: &mut Ui, pal: &Palette) {
        let ctx = ui.ctx().clone();
        let fg = pal.status_fg;
        let font = FontId::proportional(12.0);
        ui.horizontal_centered(|ui| {
            ui.spacing_mut().item_spacing.x = 2.0;
            // le bloc de gauche (comme l'indicateur distant de VS Code)
            let (r, _) = ui.allocate_exact_size(vec2(34.0, 22.0), Sense::hover());
            ui.painter().rect_filled(r, 0.0, pal.status_bg.gamma_multiply(0.75));
            logo_at(ui.painter(), r.center(), 12.0, fg);
            if let Some(b) = self.git.branch.clone() {
                if status_item(ui, pal, Some(Icon::Branch), &b, "Gestion de versions").clicked() {
                    self.exec(Command::ShowGit, &ctx);
                }
            }
            let errors = self.diagnostics.iter().filter(|d| d.severity == Severity::Error).count();
            let warnings = self.diagnostics.iter().filter(|d| d.severity == Severity::Warning).count();
            let r = ui.horizontal(|ui| {
                ui.add_space(6.0);
                icons::inline(ui, Icon::Error, 13.0, fg);
                ui.label(RichText::new(errors.to_string()).font(font.clone()).color(fg));
                ui.add_space(4.0);
                icons::inline(ui, Icon::Warning, 13.0, fg);
                ui.label(RichText::new(warnings.to_string()).font(font.clone()).color(fg));
                ui.add_space(6.0);
            });
            if ui.interact(r.response.rect, Id::new("ks-st-problems"), Sense::click()).on_hover_text("Problèmes (Ctrl+Maj+M)").clicked() {
                self.exec(Command::ShowProblems, &ctx);
            }
            if let Some(t) = self.tasks.iter().rev().find(|t| t.running() && t.kind != TaskKind::Shell) {
                let title = t.title.clone();
                ui.add_space(6.0);
                icons::inline(ui, Icon::Spinner(icons::spinner_phase(ui)), 13.0, fg);
                ui.label(RichText::new(title).font(font.clone()).color(fg));
            }
            if let Some((tool, t)) = &self.agent_activity {
                if self.now - t < 4.0 {
                    ui.add_space(8.0);
                    icons::inline(ui, Icon::Sparkle, 12.0, fg);
                    ui.label(RichText::new(format!("copilote · {tool}")).font(font.clone()).color(fg));
                }
            }
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                ui.add_space(if KORIN { 8.0 } else { 6.0 });
                let cp = format!("{}{}", self.copilot.kind.label(), match self.copilot.status {
                    crate::copilot::Status::Busy => " · au travail",
                    crate::copilot::Status::Starting => " · démarre",
                    _ => "",
                });
                if status_item(ui, pal, Some(Icon::Sparkle), &cp, "Copilote (Ctrl+Alt+I)").clicked() {
                    self.exec(Command::ToggleCopilot, &ctx);
                }
                if let Some(m) = &self.mcp {
                    status_item(ui, pal, None, &format!("MCP :{}", m.port), "Le serveur MCP de l'IDE (pour les agents)");
                }
                if let Some(i) = self.active.filter(|&i| i < self.docs.len()) {
                    let d = &self.docs[i];
                    let lang = d.lang.name().to_string();
                    let pos = match d.sel {
                        Some((a, b)) => format!("Ln {}, Col {} ({} sélectionnés)", d.cursor.0, d.cursor.1, b - a),
                        None => format!("Ln {}, Col {}", d.cursor.0, d.cursor.1),
                    };
                    status_item(ui, pal, None, &lang, "Langage");
                    status_item(ui, pal, None, "LF", "Fins de ligne");
                    status_item(ui, pal, None, "UTF-8", "Encodage");
                    let indent = if matches!(d.lang, Lang::Rust | Lang::Python | Lang::CSharp | Lang::Toml | Lang::Json | Lang::Markdown | Lang::Xml | Lang::Cpp) { format!("Espaces : {}", self.settings.tab_size) } else { "Tabulations".into() };
                    status_item(ui, pal, None, &indent, "Indentation");
                    if status_item(ui, pal, None, &pos, "Aller à la ligne (Ctrl+G)").clicked() {
                        self.exec(Command::GotoLine, &ctx);
                    }
                }
            });
        });
    }

    // -----------------------------------------------------------------------------------------------------------
    // La palette de commandes, Ctrl+P, Ctrl+G

    fn palette_ui(&mut self, ctx: &egui::Context, pal: &Palette) {
        let Some(mut p) = self.palette.take() else { return };
        let screen = ctx.content_rect();
        let w = (screen.width() * 0.5).clamp(360.0, 640.0);
        let pos = pos2(screen.center().x - w / 2.0, screen.min.y + 40.0);
        let mut close = false;
        let mut chosen: Option<PaletteChoice> = None;
        egui::Area::new(Id::new("ks-palette")).order(egui::Order::Foreground).fixed_pos(pos).show(ctx, |ui| {
            Frame::popup(ui.style()).inner_margin(6).corner_radius(6.0).show(ui, |ui| {
                ui.set_width(w);
                let hint = match p.mode {
                    PaletteMode::Files => "Rechercher des fichiers par nom (« > » pour les commandes, « : » pour une ligne)",
                    PaletteMode::Commands => "> Commande",
                    PaletteMode::Line => ": Numéro de ligne",
                };
                let r = ui.add(egui::TextEdit::singleline(&mut p.query).hint_text(hint).desired_width(f32::INFINITY));
                if p.focus {
                    r.request_focus();
                    p.focus = false;
                }
                // le préfixe change de mode
                if p.mode == PaletteMode::Files {
                    if let Some(q) = p.query.strip_prefix('>') {
                        p.mode = PaletteMode::Commands;
                        p.query = q.trim_start().to_string();
                    } else if let Some(q) = p.query.strip_prefix(':') {
                        p.mode = PaletteMode::Line;
                        p.query = q.to_string();
                    }
                }
                let items: Vec<(String, String, PaletteChoice)> = match p.mode {
                    PaletteMode::Files => {
                        let q = p.query.to_lowercase();
                        let mut v: Vec<(i64, &PathBuf)> = p.files.iter().filter_map(|f| fuzzy(&q, &workspace::rel(self.root.as_deref(), f).to_lowercase()).map(|s| (s, f))).collect();
                        v.sort_by(|a, b| b.0.cmp(&a.0));
                        v.into_iter()
                            .take(60)
                            .map(|(_, f)| {
                                let name = f.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
                                let dir = f.parent().map(|d| workspace::rel(self.root.as_deref(), d)).unwrap_or_default();
                                (name, dir, PaletteChoice::File(f.clone()))
                            })
                            .collect()
                    }
                    PaletteMode::Commands => {
                        let q = p.query.to_lowercase();
                        COMMANDS
                            .iter()
                            .filter_map(|c| {
                                let full = format!("{}: {}", c.menu, c.label);
                                fuzzy(&q, &full.to_lowercase()).map(|s| (s, full, c))
                            })
                            .collect::<Vec<_>>()
                            .into_iter()
                            .map(|(_, full, c)| (full, c.keys.to_string(), PaletteChoice::Cmd(c.cmd)))
                            .collect()
                    }
                    PaletteMode::Line => {
                        let n = self.active.and_then(|i| self.docs.get_mut(i)).map(|d| d.line_count()).unwrap_or(0);
                        let mut it = p.query.split([':', ',']);
                        let l = it.next().and_then(|s| s.trim().parse::<usize>().ok());
                        let c = it.next().and_then(|s| s.trim().parse::<usize>().ok()).unwrap_or(1);
                        match l {
                            Some(l) if n > 0 => vec![(format!("Aller à la ligne {l}, colonne {c}"), format!("{n} lignes"), PaletteChoice::Line(l, c))],
                            _ => vec![(format!("Tapez un numéro de ligne entre 1 et {n}"), String::new(), PaletteChoice::None)],
                        }
                    }
                };
                let n = items.len();
                if ui.input(|i| i.key_pressed(Key::ArrowDown)) && n > 0 {
                    p.selected = (p.selected + 1) % n;
                }
                if ui.input(|i| i.key_pressed(Key::ArrowUp)) && n > 0 {
                    p.selected = (p.selected + n - 1) % n;
                }
                if p.selected >= n {
                    p.selected = 0;
                }
                if ui.input(|i| i.key_pressed(Key::Enter)) && n > 0 {
                    chosen = Some(items[p.selected].2.clone());
                }
                if ui.input(|i| i.key_pressed(Key::Escape)) {
                    close = true;
                }
                ui.add_space(4.0);
                egui::ScrollArea::vertical().max_height(380.0).show(ui, |ui| {
                    for (k, (a, b, choice)) in items.iter().enumerate() {
                        let sel = k == p.selected;
                        let r = row(ui, pal, 24.0, sel, Id::new(("ks-pal", k)), |ui| {
                            ui.add_space(8.0);
                            if let PaletteChoice::File(f) = choice {
                                file_badge(ui, f, 14.0);
                            }
                            ui.label(RichText::new(a.as_str()).size(13.0));
                            if matches!(choice, PaletteChoice::File(_)) {
                                ui.add(egui::Label::new(RichText::new(b.as_str()).size(11.5).color(pal.fg_faint)).truncate());
                            } else {
                                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                                    ui.add_space(8.0);
                                    ui.label(RichText::new(b.as_str()).size(11.5).color(pal.fg_dim));
                                });
                            }
                        });
                        if sel {
                            r.scroll_to_me(None);
                        }
                        if r.clicked() {
                            chosen = Some(choice.clone());
                        }
                    }
                    if n == 0 {
                        ui.label(RichText::new("Aucun résultat").color(pal.fg_dim));
                    }
                });
            });
        });
        // un clic hors de la palette la ferme
        if ctx.input(|i| i.pointer.any_pressed()) {
            if let Some(pp) = ctx.input(|i| i.pointer.interact_pos()) {
                let area = ctx.memory(|m| m.area_rect(Id::new("ks-palette")));
                if area.is_some_and(|a| !a.contains(pp)) {
                    close = true;
                }
            }
        }
        match chosen {
            Some(PaletteChoice::File(f)) => {
                if let Err(e) = self.open_path(&f, None) {
                    self.toast(e, true);
                }
            }
            Some(PaletteChoice::Cmd(c)) => {
                self.exec(c, ctx);
                if self.palette.is_some() {
                    return;
                }
            }
            Some(PaletteChoice::Line(l, c)) => {
                let now = self.now;
                if let Some(d) = self.active_doc() {
                    d.goto(l, c, now);
                }
                self.focus_editor = true;
            }
            Some(PaletteChoice::None) => {}
            None => {
                if !close {
                    self.palette = Some(p);
                }
            }
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // Les dialogues

    fn dialogs(&mut self, ctx: &egui::Context, pal: &Palette) {
        let Some(mut d) = self.dialog.take() else { return };
        let mut keep = true;
        let modal = egui::Modal::new(Id::new("ks-dialog")).frame(Frame::popup(&ctx.global_style()).inner_margin(18).corner_radius(8.0));
        let resp = modal.show(ctx, |ui| match &mut d {
            Dialog::NewProject { template, name, location, error } => {
                ui.set_width(620.0);
                ui.label(RichText::new("Nouveau projet").font(theme::bold(18.0)));
                ui.add_space(10.0);
                let avail: Vec<(usize, &workspace::Template)> = workspace::TEMPLATES.iter().enumerate().collect();
                egui::Grid::new("ks-tpl").num_columns(2).spacing(vec2(10.0, 10.0)).show(ui, |ui| {
                    for (k, (i, t)) in avail.iter().enumerate() {
                        let sel = *template == *i;
                        let disabled = t.korin_only && !KORIN;
                        let (r, resp) = ui.allocate_exact_size(vec2(296.0, 58.0), Sense::click());
                        let fill = if sel { pal.list_active } else if resp.hovered() { pal.hover } else { Color32::TRANSPARENT };
                        ui.painter().rect(r, 6.0, fill, Stroke::new(1.0, if sel { pal.accent } else { pal.border }), egui::StrokeKind::Inside);
                        let tc = if disabled { pal.fg_faint } else { pal.fg };
                        ui.painter().text(r.min + vec2(12.0, 10.0), Align2::LEFT_TOP, t.label, theme::bold(13.5), tc);
                        let desc = ui.fonts_mut(|f| {
                            let mut job = egui::text::LayoutJob::simple(t.description.to_string(), FontId::proportional(11.0), pal.fg_dim, 272.0);
                            job.wrap.max_rows = 2;
                            f.layout_job(job)
                        });
                        ui.painter().galley(r.min + vec2(12.0, 28.0), desc, pal.fg_dim);
                        if resp.clicked() {
                            *template = *i;
                        }
                        if k % 2 == 1 {
                            ui.end_row();
                        }
                    }
                });
                ui.add_space(12.0);
                egui::Grid::new("ks-np").num_columns(2).spacing(vec2(10.0, 8.0)).show(ui, |ui| {
                    ui.label("Nom");
                    let r = ui.add(egui::TextEdit::singleline(name).hint_text("mon_projet").desired_width(460.0));
                    if name.is_empty() && !r.has_focus() {
                        r.request_focus();
                    }
                    ui.end_row();
                    ui.label("Dossier");
                    ui.horizontal(|ui| {
                        ui.add(egui::TextEdit::singleline(location).desired_width(370.0));
                        if ui.button("Parcourir…").clicked() {
                            keep = false;
                        }
                    });
                    ui.end_row();
                });
                if !name.is_empty() {
                    ui.label(RichText::new(format!("→ {}", PathBuf::from(location.as_str()).join(name.as_str()).display())).size(11.5).color(pal.fg_dim));
                }
                if let Some(e) = error {
                    ui.label(RichText::new(e.as_str()).color(pal.error));
                }
                if workspace::TEMPLATES[*template].korin_only && !KORIN {
                    ui.label(RichText::new("Ce modèle se construit dans Korin (pibuild) ; vous pouvez le créer ici et l'ouvrir dans Korin.").size(11.5).color(pal.warning));
                }
                ui.add_space(8.0);
                ui.horizontal(|ui| {
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        let create = primary_button(ui, pal, "Créer").clicked() || ui.input(|i| i.key_pressed(Key::Enter));
                        if ui.button("Annuler").clicked() {
                            keep = false;
                            *error = Some("__cancel".into());
                        }
                        if create {
                            let loc = PathBuf::from(location.trim());
                            let _ = std::fs::create_dir_all(&loc);
                            match workspace::create_project(workspace::TEMPLATES[*template].id, name, &loc) {
                                Ok(root) => {
                                    *error = Some(format!("__ok{}", root.display()));
                                    keep = false;
                                }
                                Err(e) => *error = Some(e),
                            }
                        }
                    });
                });
            }
            Dialog::Browse { purpose, dir, name, error } => {
                ui.set_width(560.0);
                let title = match purpose {
                    BrowsePurpose::OpenFolder => "Ouvrir un dossier",
                    BrowsePurpose::OpenFile => "Ouvrir un fichier",
                    BrowsePurpose::SaveAs(_) => "Enregistrer sous",
                    BrowsePurpose::Location => "Choisir un dossier",
                };
                ui.label(RichText::new(title).font(theme::bold(17.0)));
                ui.add_space(8.0);
                let mut dir_s = dir.display().to_string();
                ui.horizontal(|ui| {
                    if icons::button(ui, Icon::ChevronRight, 22.0, "Dossier parent").clicked() {
                        if let Some(p) = dir.parent() {
                            *dir = p.to_path_buf();
                        }
                    }
                    let r = ui.add(egui::TextEdit::singleline(&mut dir_s).desired_width(480.0));
                    if r.changed() {
                        let p = PathBuf::from(&dir_s);
                        if p.is_dir() {
                            *dir = p;
                        }
                    }
                });
                ui.add_space(4.0);
                let mut entries: Vec<(String, bool)> = std::fs::read_dir(&*dir)
                    .map(|rd| rd.flatten().map(|e| (e.file_name().to_string_lossy().to_string(), e.path().is_dir())).filter(|(n, d)| !n.starts_with('.') && (*d || !matches!(purpose, BrowsePurpose::OpenFolder | BrowsePurpose::Location))).collect())
                    .unwrap_or_default();
                entries.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.to_lowercase().cmp(&b.0.to_lowercase())));
                let mut enter = None;
                let mut pick_file = None;
                Frame::new().stroke(Stroke::new(1.0, pal.border)).corner_radius(4.0).show(ui, |ui| {
                    egui::ScrollArea::vertical().max_height(300.0).min_scrolled_height(300.0).auto_shrink([false, false]).show(ui, |ui| {
                        if let Some(par) = dir.parent() {
                            let r = row(ui, pal, 22.0, false, Id::new("ks-br-up"), |ui| {
                                ui.add_space(8.0);
                                icons::inline(ui, Icon::Folder, 15.0, pal.fg_dim);
                                ui.label("..");
                            });
                            if r.clicked() {
                                enter = Some(par.to_path_buf());
                            }
                        }
                        for (n, is_dir) in &entries {
                            let sel = !*is_dir && name == n;
                            let r = row(ui, pal, 22.0, sel, Id::new(("ks-br", n)), |ui| {
                                ui.add_space(8.0);
                                if *is_dir {
                                    icons::inline(ui, Icon::Folder, 15.0, Color32::from_rgb(0xDC, 0xB6, 0x7A));
                                } else {
                                    file_badge(ui, Path::new(n), 15.0);
                                }
                                ui.label(n.as_str());
                            });
                            if r.clicked() {
                                if *is_dir {
                                    enter = Some(dir.join(n));
                                } else {
                                    *name = n.clone();
                                    if r.double_clicked() {
                                        pick_file = Some(dir.join(n));
                                    }
                                }
                            }
                            if r.double_clicked() && !*is_dir {
                                pick_file = Some(dir.join(n));
                            }
                        }
                    });
                });
                if let Some(e) = enter {
                    *dir = e;
                }
                if matches!(purpose, BrowsePurpose::SaveAs(_) | BrowsePurpose::OpenFile) {
                    ui.horizontal(|ui| {
                        ui.label("Nom");
                        ui.add(egui::TextEdit::singleline(name).desired_width(460.0));
                    });
                }
                if let Some(e) = error {
                    ui.label(RichText::new(e.as_str()).color(pal.error));
                }
                ui.add_space(6.0);
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    let label = match purpose {
                        BrowsePurpose::SaveAs(_) => "Enregistrer",
                        BrowsePurpose::OpenFile => "Ouvrir",
                        _ => "Choisir ce dossier",
                    };
                    let ok = primary_button(ui, pal, label).clicked() || pick_file.is_some();
                    if ui.button("Annuler").clicked() {
                        keep = false;
                        *error = Some("__cancel".into());
                    }
                    if ok {
                        let target = pick_file.clone().unwrap_or_else(|| if name.is_empty() { dir.clone() } else { dir.join(name.as_str()) });
                        *error = Some(format!("__ok{}", target.display()));
                        keep = false;
                    }
                });
            }
            Dialog::Confirm { title, text, .. } => {
                ui.set_width(420.0);
                ui.label(RichText::new(title.as_str()).font(theme::bold(16.0)));
                ui.add_space(6.0);
                ui.label(text.as_str());
                ui.add_space(12.0);
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if primary_button(ui, pal, "Oui").clicked() {
                        *title = "__ok".into();
                        keep = false;
                    }
                    if ui.button("Annuler").clicked() {
                        keep = false;
                    }
                });
            }
            Dialog::NewEntry { dir, folder, name, error } => {
                ui.set_width(420.0);
                ui.label(RichText::new(if *folder { "Nouveau dossier" } else { "Nouveau fichier" }).font(theme::bold(16.0)));
                ui.label(RichText::new(dir.display().to_string()).size(11.5).color(pal.fg_dim));
                ui.add_space(6.0);
                let r = ui.add(egui::TextEdit::singleline(name).desired_width(f32::INFINITY));
                if name.is_empty() {
                    r.request_focus();
                }
                if let Some(e) = error {
                    ui.label(RichText::new(e.as_str()).color(pal.error));
                }
                ui.add_space(8.0);
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if (primary_button(ui, pal, "Créer").clicked() || ui.input(|i| i.key_pressed(Key::Enter))) && !name.is_empty() {
                        let p = dir.join(name.as_str());
                        let res = if *folder { std::fs::create_dir_all(&p) } else { p.parent().map(std::fs::create_dir_all).unwrap_or(Ok(())).and_then(|_| std::fs::write(&p, "")) };
                        match res {
                            Ok(_) => {
                                *error = Some(format!("__ok{}", p.display()));
                                keep = false;
                            }
                            Err(e) => *error = Some(e.to_string()),
                        }
                    }
                    if ui.button("Annuler").clicked() {
                        keep = false;
                    }
                });
            }
            Dialog::Rename { path, name, error } => {
                ui.set_width(420.0);
                ui.label(RichText::new("Renommer").font(theme::bold(16.0)));
                ui.add_space(6.0);
                ui.add(egui::TextEdit::singleline(name).desired_width(f32::INFINITY));
                if let Some(e) = error {
                    ui.label(RichText::new(e.as_str()).color(pal.error));
                }
                ui.add_space(8.0);
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if primary_button(ui, pal, "Renommer").clicked() || ui.input(|i| i.key_pressed(Key::Enter)) {
                        let np = path.with_file_name(name.as_str());
                        match std::fs::rename(&*path, &np) {
                            Ok(_) => {
                                *error = Some(format!("__ok{}", np.display()));
                                keep = false;
                            }
                            Err(e) => *error = Some(e.to_string()),
                        }
                    }
                    if ui.button("Annuler").clicked() {
                        keep = false;
                    }
                });
            }
            Dialog::About => {
                ui.set_width(420.0);
                ui.horizontal(|ui| {
                    logo(ui, 40.0, pal);
                    ui.vertical(|ui| {
                        ui.label(RichText::new("KorinStudio").font(theme::bold(20.0)));
                        ui.label(RichText::new(format!("Version {}", env!("CARGO_PKG_VERSION"))).color(pal.fg_dim));
                    });
                });
                ui.add_space(8.0);
                ui.label("L'environnement de développement de Korin OS, en Rust et egui. Rust, C, C++, C#, FreeBASIC, assembleur, Python ; un copilote (Claude Code, Claude, Mistral Vibe, MAI) qui travaille dans l'IDE par MCP.");
                if let Some(m) = &self.mcp {
                    ui.add_space(6.0);
                    ui.label(RichText::new(format!("Serveur MCP : {}", m.url())).size(11.5).color(pal.fg_dim));
                }
                ui.add_space(10.0);
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if primary_button(ui, pal, "Fermer").clicked() {
                        keep = false;
                    }
                });
            }
        });
        if resp.should_close() {
            keep = false;
        }
        if keep {
            self.dialog = Some(d);
            return;
        }
        // les suites
        match d {
            Dialog::NewProject { error: Some(e), location, template, name } => {
                if let Some(p) = e.strip_prefix("__ok") {
                    let root = PathBuf::from(p);
                    self.open_project(&root);
                    if let Some(m) = workspace::main_file(&root) {
                        let _ = self.open_path(&m, None);
                    }
                    self.toast(format!("Projet créé : {}", root.display()), false);
                } else if e != "__cancel" {
                    self.dialog = Some(Dialog::NewProject { error: Some(e), location, template, name });
                }
            }
            Dialog::NewProject { location, .. } => {
                // « Parcourir… »
                let dir = PathBuf::from(location);
                self.dialog = Some(Dialog::Browse { purpose: BrowsePurpose::Location, dir: if dir.is_dir() { dir } else { workspace::home_dir() }, name: String::new(), error: None });
            }
            Dialog::Browse { purpose, error: Some(e), .. } => {
                if let Some(p) = e.strip_prefix("__ok") {
                    let p = PathBuf::from(p);
                    match purpose {
                        BrowsePurpose::OpenFolder => {
                            if p.is_dir() {
                                self.open_project(&p);
                            }
                        }
                        BrowsePurpose::OpenFile => {
                            if let Err(e) = self.open_path(&p, None) {
                                self.toast(e, true);
                            }
                        }
                        BrowsePurpose::SaveAs(id) => {
                            if let Some(i) = self.docs.iter().position(|d| d.id == id) {
                                if let Err(e) = self.docs[i].save_as(p) {
                                    self.toast(e, true);
                                }
                                self.tree.invalidate();
                            }
                        }
                        BrowsePurpose::Location => {
                            self.dialog = Some(Dialog::NewProject { template: 0, name: String::new(), location: p.display().to_string(), error: None });
                        }
                    }
                }
            }
            Dialog::Confirm { title, action, .. } if title == "__ok" => match action {
                ConfirmAction::CloseDoc(id) => self.close_doc(id, true),
                ConfirmAction::Quit => {
                    self.allow_close = true;
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
                ConfirmAction::Delete(p) => {
                    let r = if p.is_dir() { std::fs::remove_dir_all(&p) } else { std::fs::remove_file(&p) };
                    if let Err(e) = r {
                        self.toast(format!("{} : {e}", p.display()), true);
                    }
                    if let Some(i) = self.doc_index(&p) {
                        let id = self.docs[i].id;
                        self.close_doc(id, true);
                    }
                    self.tree.invalidate();
                }
                ConfirmAction::SwitchProject(p) => self.open_project(&p),
            },
            Dialog::NewEntry { error: Some(e), folder, .. } if e.starts_with("__ok") => {
                let p = PathBuf::from(&e[4..]);
                self.tree.invalidate();
                if let Some(root) = self.root.clone() {
                    self.tree.reveal(&root, &p);
                }
                if !folder && p.is_file() {
                    let _ = self.open_path(&p, None);
                }
            }
            Dialog::Rename { error: Some(e), path, .. } if e.starts_with("__ok") => {
                let np = PathBuf::from(&e[4..]);
                self.tree.invalidate();
                for d in &mut self.docs {
                    if let Some(dp) = &d.path {
                        if let Ok(rest) = dp.strip_prefix(&path) {
                            let new = if rest.as_os_str().is_empty() { np.clone() } else { np.join(rest) };
                            d.disk_mtime = editor::mtime(&new);
                            d.lang = Lang::from_path(&new);
                            d.path = Some(new);
                        }
                    }
                }
            }
            _ => {}
        }
    }

    fn toasts_ui(&mut self, ctx: &egui::Context, pal: &Palette) {
        let now = self.now;
        self.toasts.retain(|t| now - t.at < 5.0);
        if self.toasts.is_empty() {
            return;
        }
        let screen = ctx.content_rect();
        let x = screen.max.x - 380.0 - if self.settings.copilot_open { self.settings.copilot_width } else { 0.0 } - 16.0;
        egui::Area::new(Id::new("ks-toasts")).order(egui::Order::Tooltip).fixed_pos(pos2(x.max(10.0), screen.max.y - 40.0 - 56.0 * self.toasts.len() as f32)).show(ctx, |ui| {
            for t in &self.toasts {
                Frame::popup(ui.style()).inner_margin(10).corner_radius(6.0).show(ui, |ui| {
                    ui.set_width(360.0);
                    ui.horizontal(|ui| {
                        icons::inline(ui, if t.error { Icon::Error } else { Icon::Info }, 16.0, if t.error { pal.error } else { pal.info });
                        ui.add(egui::Label::new(t.text.as_str()).wrap());
                    });
                });
                ui.add_space(6.0);
            }
        });
    }
}

#[derive(Clone)]
enum PaletteChoice {
    File(PathBuf),
    Cmd(Command),
    Line(usize, usize),
    None,
}

enum TreeAction {
    Open(PathBuf),
    NewFile(PathBuf),
    NewFolder(PathBuf),
    Rename(PathBuf),
    Delete(PathBuf),
    Terminal(PathBuf),
    Ask(PathBuf),
}

/// Recherche floue : toutes les lettres de `q` dans l'ordre ; le score favorise les suites et le nom du fichier.
fn fuzzy(q: &str, s: &str) -> Option<i64> {
    if q.is_empty() {
        return Some(0);
    }
    let name_start = s.rfind('/').map(|i| i + 1).unwrap_or(0);
    let mut score = 0i64;
    let mut si = s.char_indices();
    let mut last: Option<usize> = None;
    for qc in q.chars() {
        if qc == ' ' {
            continue;
        }
        loop {
            let (i, c) = si.next()?;
            if c == qc {
                score += 1;
                if last.is_some_and(|l| l + 1 == i) {
                    score += 5;
                }
                if i >= name_start {
                    score += 3;
                }
                last = Some(i);
                break;
            }
        }
    }
    Some(score * 100 - s.len() as i64)
}

fn menu_item(ui: &mut Ui, label: &str, keys: &str) -> bool {
    let w = ui.available_width().max(240.0);
    let (r, resp) = ui.allocate_exact_size(vec2(w, 24.0), Sense::click());
    if resp.hovered() {
        ui.painter().rect_filled(r, 4.0, ui.visuals().selection.bg_fill);
    }
    let fg = ui.visuals().text_color();
    ui.painter().text(pos2(r.min.x + 10.0, r.center().y), Align2::LEFT_CENTER, label, FontId::proportional(13.0), fg);
    if !keys.is_empty() {
        ui.painter().text(pos2(r.max.x - 10.0, r.center().y), Align2::RIGHT_CENTER, keys, FontId::proportional(12.0), ui.visuals().weak_text_color());
    }
    resp.clicked()
}

fn primary_button(ui: &mut Ui, pal: &Palette, label: &str) -> egui::Response {
    ui.add(egui::Button::new(RichText::new(label).color(pal.accent_fg)).fill(pal.accent).corner_radius(3.0).min_size(vec2(0.0, 26.0)))
}

fn link_row(ui: &mut Ui, pal: &Palette, ic: Icon, label: &str) -> egui::Response {
    let r = ui.horizontal(|ui| {
        icons::inline(ui, ic, 16.0, pal.info);
        ui.add(egui::Label::new(RichText::new(label).color(pal.info).size(14.0)).sense(Sense::click()))
    });
    r.inner.on_hover_cursor(egui::CursorIcon::PointingHand)
}

fn card(ui: &mut Ui, pal: &Palette, ic: Icon, title: &str, text: &str) {
    Frame::new().fill(pal.hover).corner_radius(6.0).inner_margin(12).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.horizontal(|ui| {
            icons::inline(ui, ic, 18.0, pal.accent);
            ui.label(RichText::new(title).font(theme::bold(13.5)));
        });
        ui.add(egui::Label::new(RichText::new(text).color(pal.fg_dim).size(12.5)).wrap());
    });
    ui.add_space(8.0);
}

fn status_item(ui: &mut Ui, pal: &Palette, ic: Option<Icon>, text: &str, tip: &str) -> egui::Response {
    let font = FontId::proportional(12.0);
    let tw = ui.fonts_mut(|f| f.layout_no_wrap(text.to_string(), font.clone(), pal.status_fg).size().x);
    let w = tw + 14.0 + if ic.is_some() { 16.0 } else { 0.0 };
    let (r, resp) = ui.allocate_exact_size(vec2(w, 22.0), Sense::click());
    if resp.hovered() {
        ui.painter().rect_filled(r, 0.0, Color32::from_white_alpha(30));
    }
    let mut x = r.min.x + 7.0;
    if let Some(i) = ic {
        icons::paint(ui.painter(), Rect::from_center_size(pos2(x + 6.0, r.center().y), vec2(12.0, 12.0)), i, pal.status_fg);
        x += 16.0;
    }
    ui.painter().text(pos2(x, r.center().y), Align2::LEFT_CENTER, text, font, pal.status_fg);
    resp.on_hover_text(tip)
}

/// Le logo de KorinStudio : un losange (celui de Korin) avec des chevrons de code.
pub fn logo(ui: &mut Ui, size: f32, pal: &Palette) {
    let (r, _) = ui.allocate_exact_size(vec2(size, size), Sense::hover());
    let _ = pal;
    logo_at(ui.painter(), r.center(), size, Color32::WHITE);
}

pub fn logo_at(p: &egui::Painter, c: Pos2, size: f32, fg: Color32) {
    let h = size / 2.0;
    let blue = Color32::from_rgb(0x1E, 0x6F, 0xE8);
    let pts = vec![c + vec2(0.0, -h), c + vec2(h, 0.0), c + vec2(0.0, h), c + vec2(-h, 0.0)];
    if fg == Color32::WHITE {
        p.add(egui::Shape::convex_polygon(pts, blue, Stroke::NONE));
    } else {
        p.add(egui::Shape::closed_line(pts, Stroke::new(1.2, fg)));
    }
    let k = size / 16.0;
    let st = Stroke::new((1.4 * k).max(1.0), if fg == Color32::WHITE { Color32::WHITE } else { fg });
    p.line_segment([c + vec2(-1.5 * k, -3.0 * k), c + vec2(-4.5 * k, 0.0)], st);
    p.line_segment([c + vec2(-4.5 * k, 0.0), c + vec2(-1.5 * k, 3.0 * k)], st);
    p.line_segment([c + vec2(1.5 * k, -3.0 * k), c + vec2(4.5 * k, 0.0)], st);
    p.line_segment([c + vec2(4.5 * k, 0.0), c + vec2(1.5 * k, 3.0 * k)], st);
}

#[allow(dead_code)]
fn vec_zero() -> Vec2 {
    Vec2::ZERO
}
