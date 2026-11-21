//! Ce qu'il faut d'UIKit pour une app en Rust (repris de uikit.h et uikit_abi.h de /devtools/gcc/include/kits).
//! Toutes les fonctions du kit prennent et rendent des `long` (i64) : une chaîne passe par l'adresse d'un
//! `CString`, une fonction de rappel par son adresse. Ajouter ici les fonctions dont l'app a besoin, avec le
//! nombre d'arguments de leur prototype dans uikit_abi.h.
#![allow(dead_code)]

use std::ffi::CString;

pub const UI_FONT_SIZE: usize = 320;

/// `struct ui_vars` de uikit.h, champ pour champ.
#[repr(C)]
pub struct UiVars {
    pub win: i64,
    pub surf: *mut u32,
    pub w: i64,
    pub h: i64,
    pub ev: [u8; 32],
    pub font: [u8; UI_FONT_SIZE],
    pub font_b: [u8; UI_FONT_SIZE],
    pub font_m: [u8; UI_FONT_SIZE],
}

pub static mut UI: UiVars = UiVars {
    win: 0,
    surf: std::ptr::null_mut(),
    w: 0,
    h: 0,
    ev: [0; 32],
    font: [0; UI_FONT_SIZE],
    font_b: [0; UI_FONT_SIZE],
    font_m: [0; UI_FONT_SIZE],
};

extern "C" {
    fn uikit_kit_open() -> i64;
    fn ui_init(vars: i64) -> i64;
    pub fn ui_window_ex(x: i64, y: i64, w: i64, h: i64, title: i64, flags: i64) -> i64;
    pub fn ui_min_size(w: i64, h: i64) -> i64;
    pub fn ui_event(ms: i64) -> i64;
    pub fn ui_w_event(w: i64) -> i64;
    pub fn ui_on_resize(f: i64, data: i64) -> i64;
    pub fn ui_fill(x: i64, y: i64, w: i64, h: i64, color: i64) -> i64;
    pub fn ui_rbox(x: i64, y: i64, w: i64, h: i64, r: i64, fill: i64, border: i64) -> i64;
    pub fn ui_text(s: i64, x: i64, y: i64, color: i64, size: i64, font: i64, flags: i64) -> i64;
    pub fn ui_text_width(s: i64, size: i64, font: i64) -> i64;
    pub fn ui_update_all() -> i64;
    pub fn ui_tokens() -> i64;
}

// uikit_abi.h
pub const WEV_KEY: i64 = 1;
pub const WEV_MOUSE: i64 = 2;
pub const WEV_CLOSE: i64 = 3;
pub const WEV_RESIZE: i64 = 8;
pub const WEV_THEME: i64 = 14;
pub const EV_KIND: usize = 4;
pub const EV_X: usize = 8;
pub const EV_Y: usize = 12;
pub const EV_CODE: usize = 16;
pub const EV_CHAR: usize = 20;
pub const EV_MODS: usize = 24;
pub const WF_RESIZABLE: i64 = 512;
pub const KEY_ENTER: i32 = 0x28;
pub const KEY_ESC: i32 = 0x29;
pub const KEY_SPACE: i32 = 0x2C; // (pas dans uikit_abi.h)
pub const TJ_FACE: usize = 0;
pub const TJ_SURFACE: usize = 4;
pub const TJ_LINE: usize = 20;
pub const TJ_TEXT1: usize = 28;
pub const TJ_TEXT2: usize = 32;
pub const TJ_ACCENT: usize = 40;
pub const TJ_ACCENT_INK: usize = 96;
pub const TJ_HOVER: usize = 100;

fn ui() -> &'static mut UiVars {
    unsafe { &mut *std::ptr::addr_of_mut!(UI) }
}

/// `uikit_open` de uikit.h (une fonction inline en C) : le kit ouvert, `UI` remplie ; false sans UIKit.
pub fn open() -> bool {
    unsafe { uikit_kit_open() != 0 && ui_init(std::ptr::addr_of_mut!(UI) as i64) != 0 }
}

/// La taille de la fenêtre courante.
pub fn size() -> (i64, i64) {
    (ui().w, ui().h)
}

/// `ui_ev_field` de uikit.h : un champ (EV_*) du dernier évènement.
pub fn ev(off: usize) -> i32 {
    let e = &ui().ev;
    i32::from_ne_bytes([e[off], e[off + 1], e[off + 2], e[off + 3]])
}

/// Une couleur du thème (TJ_*), 0x00RRGGBB : jamais de couleur en dur.
pub fn token(tj: usize) -> i64 {
    unsafe { (*(ui_tokens() as *const u32).add(tj / 4) & 0xFF_FFFF) as i64 }
}

pub enum Font {
    Regular,
    Bold,
    Mono, // (font_m : la chasse fixe)
}

fn font(f: Font) -> i64 {
    match f {
        Font::Regular => ui().font.as_ptr() as i64,
        Font::Bold => ui().font_b.as_ptr() as i64,
        Font::Mono => ui().font_m.as_ptr() as i64,
    }
}

/// Un texte UTF-8 ; `y` est la ligne de base.
pub fn text(s: &str, x: i64, y: i64, color: i64, size: i64, f: Font) {
    let c = CString::new(s).unwrap_or_default();
    unsafe { ui_text(c.as_ptr() as i64, x, y, color, size, font(f), 0) };
}

pub fn text_width(s: &str, size: i64, f: Font) -> i64 {
    let c = CString::new(s).unwrap_or_default();
    unsafe { ui_text_width(c.as_ptr() as i64, size, font(f)) }
}

/// Une fenêtre redimensionnable au centre ; false si elle n'a pas pu s'ouvrir.
pub fn window(w: i64, h: i64, title: &str) -> bool {
    let c = CString::new(title).unwrap_or_default();
    unsafe { ui_window_ex(-1, -1, w, h, c.as_ptr() as i64, WF_RESIZABLE) > 0 }
}
