//! eframe pour Korin OS (docs/EGUI.md) : les noms d'eframe 0.37 qu'emploient les apps (le trait `App` à deux
//! temps de la fourche de storytold : `raw_input_hook`, `logic`, `ui`, `on_exit` ; `Frame`, `NativeOptions`,
//! `CreationContext`, `run_native`, `icon_data`), sur l'hôte `egui-korin` : une fenêtre UIKit, le dessin par le CPU.
//!
//! Ce qu'eframe fait ailleurs et pas ici : plusieurs fenêtres (viewports), le stockage (`Storage` : aucun), le GPU
//! (`wgpu`, `glow`), les poignées de fenêtre natives (`Frame` n'en a pas : `HandleError::NotSupported`).

use std::fmt;

pub use egui;
pub use egui::{IconData, ViewportBuilder};
use raw_window_handle::{DisplayHandle, HandleError, HasDisplayHandle, HasWindowHandle, WindowHandle};

pub type DynError = Box<dyn std::error::Error + Send + Sync>;

/// Ce qui crée l'app, une fois le contexte d'egui prêt.
pub type AppCreator<'app> = Box<dyn 'app + FnOnce(&CreationContext<'_>) -> std::result::Result<Box<dyn 'app + App>, DynError>>;

/// Une app : la logique et l'interface à chaque passe, comme l'App d'eframe 0.37.
pub trait App {
    fn logic(&mut self, ctx: &egui::Context, frame: &mut Frame) {
        let _ = (ctx, frame);
    }
    fn ui(&mut self, ui: &mut egui::Ui, frame: &mut Frame);
    fn save(&mut self, _storage: &mut dyn Storage) {}
    fn on_exit(&mut self) {}
    fn auto_save_interval(&self) -> core::time::Duration {
        core::time::Duration::from_secs(30)
    }
    fn clear_color(&self, _visuals: &egui::Visuals) -> [f32; 4] {
        [0.0; 4]
    }
    fn persist_egui_memory(&self) -> bool {
        true
    }
    fn raw_input_hook(&mut self, _ctx: &egui::Context, _raw_input: &mut egui::RawInput) {}
}

/// Le stockage clé-valeur d'eframe (aucun sur Korin : les apps Craft ont leurs propres réglages).
pub trait Storage {
    fn get_string(&self, key: &str) -> Option<String>;
    fn set_string(&mut self, key: &str, value: String);
    fn remove_string(&mut self, key: &str);
    fn flush(&mut self);
}

#[derive(Clone, Debug, Default)]
pub struct IntegrationInfo {
    /// le temps CPU de la dernière image, en secondes
    pub cpu_usage: Option<f32>,
}

pub struct CreationContext<'s> {
    pub egui_ctx: egui::Context,
    pub integration_info: IntegrationInfo,
    pub storage: Option<&'s dyn Storage>,
}

/// La fenêtre de l'app, passée à chaque passe.
pub struct Frame {
    info: IntegrationInfo,
    egui_ctx: Option<egui::Context>,
}

impl Frame {
    #[doc(hidden)]
    pub fn _new_kittest() -> Self {
        Self { info: IntegrationInfo::default(), egui_ctx: None }
    }

    pub fn is_web(&self) -> bool {
        false
    }

    pub fn info(&self) -> &IntegrationInfo {
        &self.info
    }

    pub fn set_ui_language(&self, lang: Option<&str>) {
        if let Some(ctx) = &self.egui_ctx
            && ctx.options(|o| o.language.as_deref() != lang)
        {
            ctx.set_language(lang.map(ToOwned::to_owned));
        }
    }

    pub fn set_text_input_language(&mut self, _lang: Option<&str>) {}

    pub fn storage(&self) -> Option<&dyn Storage> {
        None
    }

    pub fn storage_mut(&mut self) -> Option<&mut (dyn Storage + 'static)> {
        None
    }
}

// pas de poignée native : les dialogues de fichiers n'ont pas de fenêtre parente
impl HasWindowHandle for Frame {
    fn window_handle(&self) -> std::result::Result<WindowHandle<'_>, HandleError> {
        Err(HandleError::NotSupported)
    }
}

impl HasDisplayHandle for Frame {
    fn display_handle(&self) -> std::result::Result<DisplayHandle<'_>, HandleError> {
        Err(HandleError::NotSupported)
    }
}

/// Les options de la fenêtre. Seul `viewport` compte (titre, taille, taille minimale) ; les autres champs sont ceux
/// d'eframe que les apps règlent, sans effet ici.
#[derive(Clone, Default)]
pub struct NativeOptions {
    pub viewport: egui::ViewportBuilder,
    pub vsync: bool,
    pub multisampling: u16,
    pub depth_buffer: u8,
    pub stencil_buffer: u8,
    pub centered: bool,
    pub persist_window: bool,
    pub persistence_path: Option<std::path::PathBuf>,
    pub dithering: bool,
    pub run_and_return: bool,
}

#[derive(Debug)]
pub enum Error {
    AppCreation(DynError),
    /// pas de fenêtre (UIKit, le bureau)
    Window(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::AppCreation(e) => write!(f, "app creation error: {e}"),
            Error::Window(e) => f.write_str(e),
        }
    }
}

impl std::error::Error for Error {}

pub type Result<T = (), E = Error> = core::result::Result<T, E>;

pub mod icon_data {
    /// L'icône d'une image PNG.
    pub fn from_png_bytes(png_bytes: &[u8]) -> Result<egui::IconData, image::ImageError> {
        let image = image::load_from_memory(png_bytes)?.into_rgba8();
        let (width, height) = image.dimensions();
        Ok(egui::IconData { rgba: image.into_raw(), width, height })
    }
}

/// Ouvre la fenêtre, crée l'app, puis la fait tourner jusqu'à sa fermeture.
pub fn run_native(app_name: &str, native_options: NativeOptions, app_creator: AppCreator<'_>) -> Result {
    let vp = &native_options.viewport;
    let title = vp.title.clone().unwrap_or_else(|| app_name.to_owned());
    let mut options = egui_korin::Options::new(&title);
    if let Some(s) = vp.inner_size {
        options.size = [s.x.max(1.0) as u32, s.y.max(1.0) as u32];
    }
    if let Some(s) = vp.min_inner_size {
        options.min_size = Some([s.x.max(1.0) as u32, s.y.max(1.0) as u32]);
    }
    let ctx = egui::Context::default();
    let cc = CreationContext { egui_ctx: ctx.clone(), integration_info: IntegrationInfo::default(), storage: None };
    let mut app = app_creator(&cc).map_err(Error::AppCreation)?;
    let mut frame = Frame { info: IntegrationInfo::default(), egui_ctx: Some(ctx.clone()) };
    let r = egui_korin::run_with(options, &ctx, |ctx, mut raw, _host| {
        app.raw_input_hook(ctx, &mut raw);
        let t0 = std::time::Instant::now();
        let out = ctx.run_ui(raw, |ui| {
            app.logic(ui.ctx(), &mut frame);
            app.ui(ui, &mut frame);
        });
        frame.info.cpu_usage = Some(t0.elapsed().as_secs_f32());
        out
    });
    app.on_exit();
    r.map_err(|e| Error::Window(e.to_string()))
}
