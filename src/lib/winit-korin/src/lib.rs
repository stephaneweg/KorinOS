//! winit pour Korin OS (docs/CRAFT.md) : les types de winit 0.30 que nomment les apps, sans fenêtre. Sur Korin, la
//! fenêtre d'une app egui est celle d'egui-korin ; il n'existe jamais de `Window` de winit (le type n'a pas de valeur :
//! `winit_window()` d'eframe-korin rend None), si bien que le code qui en reçoit une compile et ne s'exécute pas.

pub use dpi;
pub use raw_window_handle;

/// Ce qui n'a pas de valeur : une fenêtre ou un écran de winit, qui n'existent pas sur Korin.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
enum Never {}

pub mod error {
    use std::fmt;

    /// (jamais sur Korin)
    #[derive(Debug)]
    pub enum ExternalError {
        NotSupported(NotSupportedError),
        Ignored,
    }

    #[derive(Clone, Debug, Default)]
    pub struct NotSupportedError {
        _private: (),
    }

    #[derive(Debug)]
    pub struct OsError {
        _private: (),
    }

    impl fmt::Display for ExternalError {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.write_str("not supported on Korin")
        }
    }
    impl fmt::Display for NotSupportedError {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.write_str("the requested operation is not supported by Winit")
        }
    }
    impl fmt::Display for OsError {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.write_str("os error")
        }
    }
    impl std::error::Error for ExternalError {}
    impl std::error::Error for NotSupportedError {}
    impl std::error::Error for OsError {}
}

pub mod monitor {
    use crate::Never;
    use dpi::{PhysicalPosition, PhysicalSize};

    /// Un écran (aucun sur Korin : on n'en obtient que d'une fenêtre de winit).
    #[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
    pub struct MonitorHandle(Never);

    impl MonitorHandle {
        pub fn name(&self) -> Option<String> {
            match self.0 {}
        }
        pub fn size(&self) -> PhysicalSize<u32> {
            match self.0 {}
        }
        pub fn position(&self) -> PhysicalPosition<i32> {
            match self.0 {}
        }
        pub fn refresh_rate_millihertz(&self) -> Option<u32> {
            match self.0 {}
        }
        pub fn scale_factor(&self) -> f64 {
            match self.0 {}
        }
        pub fn video_modes(&self) -> std::vec::IntoIter<VideoModeHandle> {
            match self.0 {}
        }
    }

    #[derive(Clone, Debug, PartialEq, Eq, Hash)]
    pub struct VideoModeHandle(Never);

    impl VideoModeHandle {
        pub fn size(&self) -> PhysicalSize<u32> {
            match self.0 {}
        }
        pub fn bit_depth(&self) -> u16 {
            match self.0 {}
        }
        pub fn refresh_rate_millihertz(&self) -> u32 {
            match self.0 {}
        }
        pub fn monitor(&self) -> MonitorHandle {
            match self.0 {}
        }
    }
}

pub mod window {
    use crate::Never;
    use crate::error::{ExternalError, NotSupportedError};
    use crate::monitor::MonitorHandle;
    use dpi::{PhysicalPosition, PhysicalSize, Position, Size};
    use raw_window_handle::{DisplayHandle, HandleError, HasDisplayHandle, HasWindowHandle, WindowHandle};
    use std::fmt;

    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
    pub struct WindowId(u64);

    impl WindowId {
        pub const fn dummy() -> Self {
            WindowId(0)
        }
    }

    impl From<WindowId> for u64 {
        fn from(id: WindowId) -> u64 {
            id.0
        }
    }

    impl From<u64> for WindowId {
        fn from(raw: u64) -> Self {
            Self(raw)
        }
    }

    #[derive(Clone, Debug, PartialEq, Eq)]
    pub enum Fullscreen {
        Exclusive(crate::monitor::VideoModeHandle),
        Borderless(Option<MonitorHandle>),
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub enum Theme {
        Light,
        Dark,
    }

    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
    pub enum WindowLevel {
        AlwaysOnBottom,
        #[default]
        Normal,
        AlwaysOnTop,
    }

    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
    pub enum UserAttentionType {
        Critical,
        #[default]
        Informational,
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub enum CursorGrabMode {
        None,
        Confined,
        Locked,
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub enum ResizeDirection {
        East,
        North,
        NorthEast,
        NorthWest,
        South,
        SouthEast,
        SouthWest,
        West,
    }

    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
    pub enum ImePurpose {
        #[default]
        Normal,
        Password,
        Terminal,
    }

    /// Les boutons de la barre de titre (un champ de bits, comme winit).
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub struct WindowButtons(u32);

    impl WindowButtons {
        pub const CLOSE: Self = Self(1);
        pub const MINIMIZE: Self = Self(2);
        pub const MAXIMIZE: Self = Self(4);
        pub const fn all() -> Self {
            Self(7)
        }
        pub const fn empty() -> Self {
            Self(0)
        }
        pub const fn contains(&self, other: Self) -> bool {
            self.0 & other.0 == other.0
        }
    }

    impl std::ops::BitOr for WindowButtons {
        type Output = Self;
        fn bitor(self, o: Self) -> Self {
            Self(self.0 | o.0)
        }
    }

    impl Default for WindowButtons {
        fn default() -> Self {
            Self::all()
        }
    }

    /// Une icône RGBA (gardée, sans effet : la fenêtre de Korin prend celle de l'app).
    #[derive(Clone, Debug, PartialEq, Eq)]
    pub struct Icon {
        rgba: Vec<u8>,
        width: u32,
        height: u32,
    }

    #[derive(Debug)]
    pub enum BadIcon {
        ByteCountNotDivisibleBy4 { byte_count: usize },
        DimensionsVsPixelCount { width: u32, height: u32, width_x_height: usize, pixel_count: usize },
    }

    impl fmt::Display for BadIcon {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "{self:?}")
        }
    }

    impl std::error::Error for BadIcon {}

    impl Icon {
        pub fn from_rgba(rgba: Vec<u8>, width: u32, height: u32) -> Result<Self, BadIcon> {
            if rgba.len() % 4 != 0 {
                return Err(BadIcon::ByteCountNotDivisibleBy4 { byte_count: rgba.len() });
            }
            let pixel_count = rgba.len() / 4;
            if pixel_count != width as usize * height as usize {
                return Err(BadIcon::DimensionsVsPixelCount {
                    width,
                    height,
                    width_x_height: width as usize * height as usize,
                    pixel_count,
                });
            }
            Ok(Icon { rgba, width, height })
        }
    }

    /// Une fenêtre de winit : jamais sur Korin (pas de valeur ; ses méthodes ne s'exécutent pas).
    pub struct Window(Never);

    impl fmt::Debug for Window {
        fn fmt(&self, _f: &mut fmt::Formatter<'_>) -> fmt::Result {
            match self.0 {}
        }
    }

    impl Window {
        pub fn id(&self) -> WindowId {
            match self.0 {}
        }
        pub fn scale_factor(&self) -> f64 {
            match self.0 {}
        }
        pub fn request_redraw(&self) {
            match self.0 {}
        }
        pub fn pre_present_notify(&self) {
            match self.0 {}
        }
        pub fn reset_dead_keys(&self) {
            match self.0 {}
        }
        pub fn inner_position(&self) -> Result<PhysicalPosition<i32>, NotSupportedError> {
            match self.0 {}
        }
        pub fn outer_position(&self) -> Result<PhysicalPosition<i32>, NotSupportedError> {
            match self.0 {}
        }
        pub fn set_outer_position<P: Into<Position>>(&self, _position: P) {
            match self.0 {}
        }
        pub fn inner_size(&self) -> PhysicalSize<u32> {
            match self.0 {}
        }
        pub fn request_inner_size<S: Into<Size>>(&self, _size: S) -> Option<PhysicalSize<u32>> {
            match self.0 {}
        }
        pub fn outer_size(&self) -> PhysicalSize<u32> {
            match self.0 {}
        }
        pub fn set_min_inner_size<S: Into<Size>>(&self, _min_size: Option<S>) {
            match self.0 {}
        }
        pub fn set_max_inner_size<S: Into<Size>>(&self, _max_size: Option<S>) {
            match self.0 {}
        }
        pub fn resize_increments(&self) -> Option<PhysicalSize<u32>> {
            match self.0 {}
        }
        pub fn set_resize_increments<S: Into<Size>>(&self, _increments: Option<S>) {
            match self.0 {}
        }
        pub fn set_title(&self, _title: &str) {
            match self.0 {}
        }
        pub fn set_transparent(&self, _transparent: bool) {
            match self.0 {}
        }
        pub fn set_blur(&self, _blur: bool) {
            match self.0 {}
        }
        pub fn set_visible(&self, _visible: bool) {
            match self.0 {}
        }
        pub fn is_visible(&self) -> Option<bool> {
            match self.0 {}
        }
        pub fn set_resizable(&self, _resizable: bool) {
            match self.0 {}
        }
        pub fn is_resizable(&self) -> bool {
            match self.0 {}
        }
        pub fn set_enabled_buttons(&self, _buttons: WindowButtons) {
            match self.0 {}
        }
        pub fn enabled_buttons(&self) -> WindowButtons {
            match self.0 {}
        }
        pub fn set_minimized(&self, _minimized: bool) {
            match self.0 {}
        }
        pub fn is_minimized(&self) -> Option<bool> {
            match self.0 {}
        }
        pub fn set_maximized(&self, _maximized: bool) {
            match self.0 {}
        }
        pub fn is_maximized(&self) -> bool {
            match self.0 {}
        }
        pub fn set_fullscreen(&self, _fullscreen: Option<Fullscreen>) {
            match self.0 {}
        }
        pub fn fullscreen(&self) -> Option<Fullscreen> {
            match self.0 {}
        }
        pub fn set_decorations(&self, _decorations: bool) {
            match self.0 {}
        }
        pub fn is_decorated(&self) -> bool {
            match self.0 {}
        }
        pub fn set_window_level(&self, _level: WindowLevel) {
            match self.0 {}
        }
        pub fn set_window_icon(&self, _window_icon: Option<Icon>) {
            match self.0 {}
        }
        pub fn set_ime_cursor_area<P: Into<Position>, S: Into<Size>>(&self, _position: P, _size: S) {
            match self.0 {}
        }
        pub fn set_ime_allowed(&self, _allowed: bool) {
            match self.0 {}
        }
        pub fn set_ime_purpose(&self, _purpose: ImePurpose) {
            match self.0 {}
        }
        pub fn focus_window(&self) {
            match self.0 {}
        }
        pub fn has_focus(&self) -> bool {
            match self.0 {}
        }
        pub fn request_user_attention(&self, _request_type: Option<UserAttentionType>) {
            match self.0 {}
        }
        pub fn set_theme(&self, _theme: Option<Theme>) {
            match self.0 {}
        }
        pub fn theme(&self) -> Option<Theme> {
            match self.0 {}
        }
        pub fn set_content_protected(&self, _protected: bool) {
            match self.0 {}
        }
        pub fn title(&self) -> String {
            match self.0 {}
        }
        pub fn set_cursor_position<P: Into<Position>>(&self, _position: P) -> Result<(), ExternalError> {
            match self.0 {}
        }
        pub fn set_cursor_grab(&self, _mode: CursorGrabMode) -> Result<(), ExternalError> {
            match self.0 {}
        }
        pub fn set_cursor_visible(&self, _visible: bool) {
            match self.0 {}
        }
        pub fn drag_window(&self) -> Result<(), ExternalError> {
            match self.0 {}
        }
        pub fn drag_resize_window(&self, _direction: ResizeDirection) -> Result<(), ExternalError> {
            match self.0 {}
        }
        pub fn show_window_menu(&self, _position: impl Into<Position>) {
            match self.0 {}
        }
        pub fn set_cursor_hittest(&self, _hittest: bool) -> Result<(), ExternalError> {
            match self.0 {}
        }
        pub fn current_monitor(&self) -> Option<MonitorHandle> {
            match self.0 {}
        }
        pub fn available_monitors(&self) -> std::vec::IntoIter<MonitorHandle> {
            match self.0 {}
        }
        pub fn primary_monitor(&self) -> Option<MonitorHandle> {
            match self.0 {}
        }
    }

    impl HasWindowHandle for Window {
        fn window_handle(&self) -> Result<WindowHandle<'_>, HandleError> {
            match self.0 {}
        }
    }

    impl HasDisplayHandle for Window {
        fn display_handle(&self) -> Result<DisplayHandle<'_>, HandleError> {
            match self.0 {}
        }
    }
}

pub mod event_loop {
    use crate::Never;
    use std::marker::PhantomData;

    /// Le constructeur de la boucle d'événements de winit : jamais sur Korin (pas de valeur ; les crochets
    /// `event_loop_builder` d'eframe-korin ne sont pas appelés).
    pub struct EventLoopBuilder<T: 'static>(Never, PhantomData<T>);

    impl<T> std::fmt::Debug for EventLoopBuilder<T> {
        fn fmt(&self, _f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            match self.0 {}
        }
    }
}

pub mod event {
    /// Un bouton de la souris (les apps traduisent ceux des tablettes).
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
    pub enum MouseButton {
        Left,
        Right,
        Middle,
        Back,
        Forward,
        Other(u16),
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub enum ElementState {
        Pressed,
        Released,
    }

    impl ElementState {
        pub fn is_pressed(self) -> bool {
            self == ElementState::Pressed
        }
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
    pub struct DeviceId(u64);

    impl DeviceId {
        pub const fn dummy() -> Self {
            DeviceId(0)
        }
    }
}
