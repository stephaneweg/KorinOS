//! eframe pour Korin OS (docs/EGUI.md) : les noms d'eframe 0.37 qu'emploient les apps (le trait `App` à deux
//! temps de la fourche de storytold : `raw_input_hook`, `logic`, `ui`, `on_exit` ; `Frame`, `NativeOptions`,
//! `CreationContext`, `run_native`, `icon_data`), sur l'hôte `egui-korin` : une fenêtre UIKit, le dessin par le CPU.
//!
//! Ce qu'eframe fait ailleurs et pas ici : plusieurs fenêtres (viewports), le stockage (`Storage` : aucun), le GPU
//! (`wgpu`, `glow`), les poignées de fenêtre natives (`Frame` n'en a pas : `HandleError::NotSupported`).

use std::fmt;

pub use egui;
#[cfg(feature = "wgpu")]
pub use egui_wgpu;
#[cfg(feature = "wgpu")]
pub use egui_wgpu::wgpu;
pub use egui::{IconData, ViewportBuilder};
pub use winit;
use raw_window_handle::{DisplayHandle, HandleError, HasDisplayHandle, HasWindowHandle, WindowHandle};

pub type DynError = Box<dyn std::error::Error + Send + Sync>;

/// Les événements de la boucle de winit d'eframe (aucun sur Korin : pas de boucle winit).
#[derive(Debug)]
pub enum UserEvent {}

/// Le crochet sur la boucle d'événements de winit (`NativeOptions::event_loop_builder` : jamais appelé sur Korin).
pub type EventLoopBuilderHook = Box<dyn FnOnce(&mut winit::event_loop::EventLoopBuilder<UserEvent>)>;

/// Le crochet qui retouche la fenêtre avant son ouverture (`NativeOptions::window_builder`).
pub type WindowBuilderHook = Box<dyn FnOnce(egui::ViewportBuilder) -> egui::ViewportBuilder>;

/// Ce qui crée l'app, une fois le contexte d'egui prêt.
pub type AppCreator<'app> = Box<dyn 'app + FnOnce(&CreationContext<'_>) -> std::result::Result<Box<dyn 'app + App>, DynError>>;

/// Une app : la logique et l'interface à chaque passe, comme l'App d'eframe 0.37.
pub trait App {
    fn logic(&mut self, ctx: &egui::Context, frame: &mut Frame) {
        let _ = (ctx, frame);
    }
    fn ui(&mut self, ui: &mut egui::Ui, frame: &mut Frame);
    fn save(&mut self, _storage: &mut dyn Storage) {}
    /// (avec la fonction `glow`, la signature d'eframe : le contexte OpenGL, toujours None ici)
    #[cfg(feature = "glow")]
    fn on_exit(&mut self, _gl: Option<&glow::Context>) {}
    #[cfg(not(feature = "glow"))]
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

/// Lit une valeur du stockage d'eframe (fonction `persistence`). Sur Korin il n'y a pas de stockage
/// (`CreationContext::storage` est None, `App::save` n'est pas appelée) : rien n'est jamais lu.
pub fn get_value<T>(_storage: &dyn Storage, _key: &str) -> Option<T> {
    None
}

/// Écrit une valeur dans le stockage d'eframe (sans effet sur Korin : pas de stockage, voir `get_value`).
pub fn set_value<T: ?Sized>(_storage: &mut dyn Storage, _key: &str, _value: &T) {}

/// La clé sous laquelle eframe garde l'état de l'app (`App::save`).
pub const APP_KEY: &str = "app";

#[derive(Clone, Debug, Default)]
pub struct IntegrationInfo {
    /// le temps CPU de la dernière image, en secondes
    pub cpu_usage: Option<f32>,
}

pub struct CreationContext<'s> {
    pub egui_ctx: egui::Context,
    pub integration_info: IntegrationInfo,
    pub storage: Option<&'s dyn Storage>,
    /// toujours None : pas de GPU dans l'eframe de Korin
    #[cfg(feature = "wgpu")]
    pub wgpu_render_state: Option<egui_wgpu::RenderState>,
}

impl CreationContext<'_> {
    /// La fenêtre de winit : jamais sur Korin (la fenêtre est celle d'egui-korin).
    pub fn winit_window(&self) -> Option<&std::sync::Arc<winit::window::Window>> {
        None
    }
}

/// La fenêtre de l'app, passée à chaque passe.
pub struct Frame {
    info: IntegrationInfo,
    egui_ctx: Option<egui::Context>,
    /// toujours None : pas de GPU dans l'eframe de Korin
    #[cfg(feature = "wgpu")]
    #[doc(hidden)]
    pub wgpu_render_state: Option<egui_wgpu::RenderState>,
}

impl Frame {
    #[doc(hidden)]
    pub fn _new_kittest() -> Self {
        Self {
            info: IntegrationInfo::default(),
            egui_ctx: None,
            #[cfg(feature = "wgpu")]
            wgpu_render_state: None,
        }
    }

    /// La fenêtre de winit : jamais sur Korin (la fenêtre est celle d'egui-korin).
    pub fn winit_window(&self) -> Option<&std::sync::Arc<winit::window::Window>> {
        None
    }

    /// L'état de rendu GPU : jamais sur Korin (l'app prend son chemin CPU).
    #[cfg(feature = "wgpu")]
    pub fn wgpu_render_state(&self) -> Option<&egui_wgpu::RenderState> {
        self.wgpu_render_state.as_ref()
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

/// Le rendu de la fenêtre que demande l'app (sans effet : l'eframe de Korin dessine par le CPU).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum Renderer {
    #[cfg(feature = "glow")]
    Glow,
    #[default]
    Wgpu,
}

/// Le module `glow` d'eframe (fonction `glow`) : le type du contexte OpenGL que nomment les apps, jamais créé ici.
#[cfg(feature = "glow")]
pub mod glow {
    /// le contexte OpenGL (aucun sur Korin : `App::on_exit` reçoit toujours None)
    pub struct Context {
        _private: (),
    }
}

/// Les options de la fenêtre. Seuls `viewport` (titre, taille, taille minimale) et `window_builder` comptent ; les
/// autres champs sont ceux d'eframe que les apps règlent, sans effet ici.
#[derive(Default)]
pub struct NativeOptions {
    pub viewport: egui::ViewportBuilder,
    /// retouche `viewport` juste avant l'ouverture (une copie des options ne le garde pas, comme dans eframe)
    pub window_builder: Option<WindowBuilderHook>,
    /// (sans effet : pas de boucle winit sur Korin)
    pub event_loop_builder: Option<EventLoopBuilderHook>,
    /// le rendu demandé (sans effet)
    pub renderer: Renderer,
    /// les réglages du GPU (sans effet : pas de GPU dans l'eframe de Korin)
    #[cfg(feature = "wgpu")]
    pub wgpu_options: egui_wgpu::WgpuConfiguration,
    pub vsync: bool,
    pub multisampling: u16,
    pub depth_buffer: u8,
    pub stencil_buffer: u8,
    pub centered: bool,
    pub persist_window: bool,
    pub persistence_path: Option<std::path::PathBuf>,
    pub dithering: bool,
    pub run_and_return: bool,
    pub system_font_fallback: bool,
}

impl Clone for NativeOptions {
    fn clone(&self) -> Self {
        Self {
            viewport: self.viewport.clone(),
            window_builder: None,
            event_loop_builder: None,
            renderer: self.renderer,
            #[cfg(feature = "wgpu")]
            wgpu_options: self.wgpu_options.clone(),
            vsync: self.vsync,
            multisampling: self.multisampling,
            depth_buffer: self.depth_buffer,
            stencil_buffer: self.stencil_buffer,
            centered: self.centered,
            persist_window: self.persist_window,
            persistence_path: self.persistence_path.clone(),
            dithering: self.dithering,
            run_and_return: self.run_and_return,
            system_font_fallback: self.system_font_fallback,
        }
    }
}

#[derive(Debug)]
pub enum Error {
    AppCreation(DynError),
    /// pas de fenêtre (UIKit, le bureau)
    Window(String),
    /// (jamais sur Korin : pas de GPU ; la variante d'eframe, que les apps nomment)
    #[cfg(feature = "wgpu")]
    Wgpu(egui_wgpu::WgpuError),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::AppCreation(e) => write!(f, "app creation error: {e}"),
            Error::Window(e) => f.write_str(e),
            #[cfg(feature = "wgpu")]
            Error::Wgpu(e) => write!(f, "wgpu error: {e}"),
        }
    }
}

impl std::error::Error for Error {}

pub type Result<T = (), E = Error> = core::result::Result<T, E>;

/// Le dossier des données d'une app (comme eframe sous Linux : $XDG_DATA_HOME ou ~/.local/share, puis l'identifiant
/// de l'app en minuscules sans espaces).
pub fn storage_dir(app_id: &str) -> Option<std::path::PathBuf> {
    std::env::var_os("XDG_DATA_HOME")
        .map(std::path::PathBuf::from)
        .filter(|p| p.is_absolute())
        .or_else(|| std::env::home_dir().map(|p| p.join(".local").join("share")))
        .map(|p| p.join(app_id.to_lowercase().replace(|c: char| c.is_ascii_whitespace(), "")))
}

pub mod icon_data {
    /// L'icône d'une image PNG.
    pub fn from_png_bytes(png_bytes: &[u8]) -> Result<egui::IconData, image::ImageError> {
        let image = image::load_from_memory(png_bytes)?.into_rgba8();
        let (width, height) = image.dimensions();
        Ok(egui::IconData { rgba: image.into_raw(), width, height })
    }
}

/// Ouvre la fenêtre, crée l'app, puis la fait tourner jusqu'à sa fermeture.
pub fn run_native(app_name: &str, mut native_options: NativeOptions, app_creator: AppCreator<'_>) -> Result {
    if let Some(hook) = native_options.window_builder.take() {
        native_options.viewport = hook(native_options.viewport);
    }
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
    let cc = CreationContext {
        egui_ctx: ctx.clone(),
        integration_info: IntegrationInfo::default(),
        storage: None,
        #[cfg(feature = "wgpu")]
        wgpu_render_state: None,
    };
    let mut app = app_creator(&cc).map_err(Error::AppCreation)?;
    let mut frame = Frame {
        info: IntegrationInfo::default(),
        egui_ctx: Some(ctx.clone()),
        #[cfg(feature = "wgpu")]
        wgpu_render_state: None,
    };
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
    #[cfg(feature = "glow")]
    app.on_exit(None);
    #[cfg(not(feature = "glow"))]
    app.on_exit();
    r.map_err(|e| Error::Window(e.to_string()))
}
