//! Suite ArtCraft : le lanceur des apps Craft de storytold sur Korin OS (docs/CRAFT.md). Les dix-sept apps (Rust,
//! egui) sont dans /apps/<paquet>.app, dans la catégorie « Craft » que le lanceur et le menu de Korin ne montrent pas
//! (src/sys/common/applist.c, cat_hidden) : on les ouvre d'ici. Chacune a son paquet (qui dépend de celui du lanceur,
//! craft) ; une app pas installée est grisée, et un clic ouvre Logiciels.

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use egui::{Align, Color32, CornerRadius, Frame, Layout, RichText, Sense, Stroke, TextureHandle, Vec2};

/// Une app de la suite : son identifiant (dépôt, icône), son nom, son paquet sur Korin, sa famille, son accroche.
struct App {
    id: &'static str,
    name: &'static str,
    package: &'static str,
    family: usize,
    tagline: &'static str,
}

const FAMILIES: [&str; 4] = ["Documents", "Image et dessin", "Vidéo et son", "Technique"];

const APPS: &[App] = &[
    App { id: "wordcraft", name: "WordCraft", package: "wordcraft", family: 0, tagline: "Traitement de texte : ruban, styles, suivi des modifications, .docx." },
    App { id: "gridcraft", name: "GridCraft", package: "gridcraft", family: 0, tagline: "Tableur et analyse de données, .xlsx." },
    App { id: "deckcraft", name: "DeckCraft", package: "deckcraft", family: 0, tagline: "Présentations et diaporamas, .pptx." },
    App { id: "pdfcraft", name: "PdfCraft", package: "pdfcraft", family: 0, tagline: "Lire, organiser, assembler et protéger des PDF." },
    App { id: "designcraft", name: "DesignCraft", package: "designcraft", family: 0, tagline: "Mise en page et publication." },
    App { id: "photocraft", name: "PhotoCraft", package: "photocraft", family: 1, tagline: "Retouche d'images, calques et outils de peinture." },
    App { id: "vectorcraft", name: "VectorCraft", package: "vectorcraft", family: 1, tagline: "Illustration vectorielle, SVG." },
    App { id: "lightcraft", name: "LightCraft", package: "lightcraft", family: 1, tagline: "Photothèque et développement des fichiers RAW." },
    App { id: "figcraft", name: "FigCraft", package: "figcraft", family: 1, tagline: "Conception d'interfaces et prototypes." },
    App { id: "texturecraft", name: "TextureCraft", package: "texturecraft", family: 1, tagline: "Peinture de textures sur des modèles 3D." },
    App { id: "materialcraft", name: "MaterialCraft", package: "materialcraft", family: 1, tagline: "Matériaux procéduraux par nœuds." },
    App { id: "filmcraft", name: "FilmCraft", package: "filmcraft", family: 2, tagline: "Montage vidéo." },
    App { id: "effectcraft", name: "EffectCraft", package: "effectcraft", family: 2, tagline: "Animation graphique et effets visuels." },
    App { id: "anicraft", name: "AniCraft", package: "anicraft", family: 2, tagline: "Animation vectorielle 2D." },
    App { id: "soundcraft", name: "SoundCraft", package: "soundcraft", family: 2, tagline: "Enregistrement, montage et mixage audio." },
    App { id: "cadcraft", name: "CADCraft", package: "cadcraft", family: 3, tagline: "Conception et dessin assistés par ordinateur." },
    App { id: "craft-anything", name: "Craft Anything", package: "craft-anything", family: 3, tagline: "Comprendre comment marche un logiciel, et faire le sien." },
];

/// Le dossier de l'app (/apps/craft.app) : là où est ce programme.
fn home() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_else(|| PathBuf::from("/apps/craft.app"))
}

/// Logiciels, le gestionnaire de paquets (chaque app de la suite a son paquet, qui dépend de celui du lanceur).
const LOGICIELS: &str = "/apps/pkgman.app/main";

fn program(app: &App) -> PathBuf {
    PathBuf::from(format!("/apps/{}.app/main", app.package))
}

struct Launcher {
    icons: Vec<Option<TextureHandle>>,
    installed: Vec<bool>,
    /// la dernière lecture de `installed` (relue toutes les 3 s : une app ajoutée par Logiciels s'allume)
    checked: Instant,
    family: Option<usize>,
    search: String,
    /// le dernier lancement : son message et son heure
    status: Option<(String, Instant)>,
}

impl Launcher {
    fn new(ctx: &egui::Context) -> Self {
        let dir = home().join("icons");
        let icons = APPS
            .iter()
            .map(|a| {
                let bytes = std::fs::read(dir.join(format!("{}.png", a.id))).ok()?;
                let img = image::load_from_memory(&bytes).ok()?.into_rgba8();
                let size = [img.width() as usize, img.height() as usize];
                let color = egui::ColorImage::from_rgba_unmultiplied(size, img.as_raw());
                Some(ctx.load_texture(a.id, color, egui::TextureOptions::LINEAR))
            })
            .collect();
        let installed = APPS.iter().map(|a| program(a).is_file()).collect();
        Self { icons, installed, checked: Instant::now(), family: None, search: String::new(), status: None }
    }

    fn launch(&mut self, i: usize) {
        let a = &APPS[i];
        // une app pas installée : son paquet est dans Logiciels (catégorie « Suite ArtCraft »)
        if !self.installed[i] {
            let r = std::process::Command::new(LOGICIELS).current_dir("/home").spawn();
            self.status = Some((
                match r {
                    Ok(_) => format!("{} n'est pas installée : ajoutez-la dans Logiciels (catégorie « Suite ArtCraft »)", a.name),
                    Err(_) => format!("{} n'est pas installée : son paquet est {} (pkg add {})", a.name, a.package, a.package),
                },
                Instant::now(),
            ));
            return;
        }
        let r = std::process::Command::new(program(a)).current_dir("/home").spawn();
        self.status = Some((
            match r {
                Ok(_) => format!("{} s'ouvre…", a.name),
                Err(e) => format!("{} ne s'ouvre pas : {e}", a.name),
            },
            Instant::now(),
        ));
    }

    fn shown(&self, a: &App) -> bool {
        let q = self.search.trim().to_lowercase();
        self.family.is_none_or(|f| f == a.family)
            && (q.is_empty() || a.name.to_lowercase().contains(&q) || a.tagline.to_lowercase().contains(&q))
    }

    fn ui(&mut self, ui: &mut egui::Ui) {
        if self.checked.elapsed() >= Duration::from_secs(3) {
            self.installed = APPS.iter().map(|a| program(a).is_file()).collect();
            self.checked = Instant::now();
        }
        ui.ctx().request_repaint_after(Duration::from_secs(3));
        let accent = ui.visuals().selection.bg_fill;
        egui::Panel::top("head").frame(Frame::new().inner_margin(egui::Margin { left: 24, right: 120, top: 18, bottom: 10 })).show(ui, |ui| {
            ui.horizontal(|ui| {
                ui.label(RichText::new("Suite ArtCraft").size(28.0).strong());
                ui.add_space(12.0);
                ui.label(RichText::new("La suite d'apps libres de storytold, sur Korin").size(15.0).weak());
            });
            ui.add_space(10.0);
            ui.horizontal(|ui| {
                if ui.selectable_label(self.family.is_none(), "Toutes").clicked() {
                    self.family = None;
                }
                for (f, name) in FAMILIES.iter().enumerate() {
                    if ui.selectable_label(self.family == Some(f), *name).clicked() {
                        self.family = Some(f);
                    }
                }
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    ui.add(egui::TextEdit::singleline(&mut self.search).hint_text("Chercher").desired_width(200.0));
                });
            });
        });
        egui::Panel::bottom("foot").frame(Frame::new().inner_margin(egui::Margin::symmetric(24, 8))).show(ui, |ui| {
            let n = self.installed.iter().filter(|&&i| i).count();
            ui.horizontal(|ui| {
                ui.label(RichText::new(format!("{n} apps sur {} sont sur ce Korin", APPS.len())).weak());
                if let Some((msg, t)) = &self.status
                    && t.elapsed() < Duration::from_secs(6)
                {
                    ui.separator();
                    ui.label(msg);
                    ui.ctx().request_repaint_after(Duration::from_secs(1));
                }
            });
        });
        egui::CentralPanel::default().frame(Frame::new().inner_margin(egui::Margin::symmetric(24, 8))).show(ui, |ui| {
            egui::ScrollArea::vertical().auto_shrink(false).show(ui, |ui| {
                let card = Vec2::new(300.0, 112.0);
                let cols = ((ui.available_width() + 12.0) / (card.x + 12.0)).floor().max(1.0) as usize;
                let shown: Vec<usize> = (0..APPS.len()).filter(|&i| self.shown(&APPS[i])).collect();
                let mut launch = None;
                egui::Grid::new("apps").spacing([12.0, 12.0]).show(ui, |ui| {
                    for (k, &i) in shown.iter().enumerate() {
                        if self.card(ui, i, card, accent) {
                            launch = Some(i);
                        }
                        if (k + 1) % cols == 0 {
                            ui.end_row();
                        }
                    }
                });
                if shown.is_empty() {
                    ui.add_space(40.0);
                    ui.vertical_centered(|ui| ui.label(RichText::new("Aucune app ne correspond.").weak()));
                }
                if let Some(i) = launch {
                    self.launch(i);
                }
            });
        });
    }

    /// Une carte : icône, nom, accroche ; true si on l'a cliquée (app installée).
    fn card(&self, ui: &mut egui::Ui, i: usize, size: Vec2, accent: Color32) -> bool {
        let a = &APPS[i];
        let installed = self.installed[i];
        let (rect, resp) = ui.allocate_exact_size(size, Sense::click());
        let v = ui.visuals();
        let fill = if resp.hovered() && installed { v.widgets.hovered.weak_bg_fill } else { v.extreme_bg_color };
        let stroke = if resp.hovered() && installed { Stroke::new(1.5, accent) } else { v.widgets.noninteractive.bg_stroke };
        ui.painter().rect(rect, CornerRadius::same(10), fill, stroke, egui::StrokeKind::Inside);
        let icon = egui::Rect::from_min_size(rect.min + Vec2::new(16.0, 16.0), Vec2::splat(64.0));
        match &self.icons[i] {
            Some(t) => {
                let tint = if installed { Color32::WHITE } else { Color32::from_white_alpha(90) };
                ui.painter().image(t.id(), icon, egui::Rect::from_min_max(egui::pos2(0.0, 0.0), egui::pos2(1.0, 1.0)), tint);
            }
            None => {
                ui.painter().rect_filled(icon, CornerRadius::same(12), accent.gamma_multiply(if installed { 1.0 } else { 0.35 }));
            }
        }
        let text = rect.min + Vec2::new(96.0, 18.0);
        let strong = if installed { v.strong_text_color() } else { v.weak_text_color() };
        ui.painter().text(text, egui::Align2::LEFT_TOP, a.name, egui::FontId::proportional(17.0), strong);
        let wrap = rect.width() - 96.0 - 12.0;
        let galley = ui.painter().layout(
            if installed { a.tagline.to_owned() } else { format!("{}\nPas installée : un clic ouvre Logiciels.", a.tagline) },
            egui::FontId::proportional(12.5),
            v.weak_text_color(),
            wrap,
        );
        ui.painter().galley(text + Vec2::new(0.0, 24.0), galley, v.weak_text_color());
        resp.clone().on_hover_text(if installed { format!("Ouvrir {}", a.name) } else { format!("Installer {} (Logiciels)", a.name) });
        if resp.hovered() {
            ui.ctx().set_cursor_icon(egui::CursorIcon::PointingHand);
        }
        resp.clicked()
    }
}

fn main() {
    let ctx = egui::Context::default();
    let mut launcher = Launcher::new(&ctx);
    let options = egui_korin::Options::new("Suite ArtCraft").size(1000, 640);
    let r = egui_korin::run_with(options, &ctx, |ctx, input, _host| ctx.run_ui(input, |ui| launcher.ui(ui)));
    if let Err(e) = r {
        eprintln!("{e}");
        std::process::exit(1);
    }
}
