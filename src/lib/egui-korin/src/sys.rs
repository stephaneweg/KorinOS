//! Le relais C (csrc/host.c) et les constantes de UIKit qu'il rend.
//!
//! Les valeurs viennent des .inc de l'assembleur (src/inc/armpi.inc, src/inc/ws_ext.inc, src/kits/uikit/uikit.inc),
//! les mêmes que build/pc/gcc/include/kits/uikit_abi.h ; elles ne changent pas (ABI des programmes).
#![allow(dead_code)]

use std::ffi::{c_char, c_int, c_long, c_uint};

/// Un évènement : les champs EV_* de UIKit, dans leur ordre (struct kh_event de host.c).
#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct Event {
    pub ty: i32,
    pub kind: i32,
    pub x: i32,
    pub y: i32,
    pub code: i32,
    pub ch: i32,
    pub mods: i32,
    pub win: i32,
}

unsafe extern "C" {
    pub fn kh_open(title: *const c_char, w: c_int, h: c_int, min_w: c_int, min_h: c_int) -> c_int;
    pub fn kh_pixels(w: *mut c_int, h: *mut c_int) -> *mut u32;
    pub fn kh_event(timeout_ms: c_int, e: *mut Event) -> c_int;
    pub fn kh_update(x: c_int, y: c_int, w: c_int, h: c_int);
    pub fn kh_title(title: *const c_char);
    pub fn kh_screen(w: *mut c_int, h: *mut c_int, bottom: *mut c_int, top: *mut c_int);
    pub fn kh_minimize();
    pub fn kh_maximize();
    pub fn kh_drag();
    pub fn kh_resize(w: c_int, h: c_int);
    pub fn kh_cursor(shape: c_int);
    pub fn kh_min_size(w: c_int, h: c_int);
    pub fn kh_dark() -> c_int;
    pub fn kh_token(tj: c_int) -> c_uint;
    pub fn kh_clip_set(s: *const c_char, n: c_long);
    pub fn kh_clip_get(buf: *mut c_char, cap: c_long) -> c_long;
    pub fn kh_file_open(dir: *const c_char, filter: *const c_char, buf: *mut c_char, cap: c_long) -> c_int;
    pub fn kh_file_save(dir: *const c_char, name: *const c_char, filter: *const c_char, buf: *mut c_char, cap: c_long) -> c_int;
    pub fn kh_message(title: *const c_char, text: *const c_char);
    pub fn kh_message_ex(title: *const c_char, text: *const c_char, buttons: c_int) -> c_int;
    pub fn kh_folder_choose(dir: *const c_char, buf: *mut c_char, cap: c_long) -> c_int;
    pub fn kh_open_with(path: *const c_char);
}

// les types d'évènements (WEV_*)
pub const WEV_KEY: i32 = 1;
pub const WEV_MOUSE: i32 = 2;
pub const WEV_CLOSE: i32 = 3;
pub const WEV_WHEEL: i32 = 4;
pub const WEV_FOCUS: i32 = 5;
pub const WEV_MENU: i32 = 7;
pub const WEV_RESIZE: i32 = 8;
pub const WEV_DROP: i32 = 9;
pub const WEV_FRAME: i32 = 10;
pub const WEV_SCREEN: i32 = 12;
pub const WEV_THEME: i32 = 14;

// EV_MODS : les deux touches de chaque modificateur (uikit.h)
pub const MOD_CTRL: i32 = 0x11;
pub const MOD_SHIFT: i32 = 0x22;
pub const MOD_ALT: i32 = 0x44;
pub const MOD_GUI: i32 = 0x88;

// curseurs (UC_*)
pub const UC_ARROW: i32 = 0;
pub const UC_TEXT: i32 = 1;
pub const UC_HAND: i32 = 2;
pub const UC_HRESIZE: i32 = 3;
pub const UC_VRESIZE: i32 = 4;
pub const UC_WAIT: i32 = 5;
pub const UC_CROSS: i32 = 6;
pub const UC_DRAG: i32 = 7;
pub const UC_MOVE: i32 = 8;
pub const UC_NWSE: i32 = 9;
pub const UC_NESW: i32 = 10;
pub const UC_NO: i32 = 11;

// jetons du thème (TJ_*, décalages en octets)
pub const TJ_FACE: i32 = 0;
