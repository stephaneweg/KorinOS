//! counter -- une app de Korin en Rust : une fenêtre aux couleurs du thème, un bouton dessiné, la souris et le
//! clavier (Espace ou Entrée : +1, Échap : remise à zéro). Modèle de la compétence korin-rust.
mod uikit;

use std::sync::atomic::{AtomicI64, Ordering};
use uikit::*;

static COUNT: AtomicI64 = AtomicI64::new(0);
static HOVER: AtomicI64 = AtomicI64::new(0);

/// Le bouton : (x, y, largeur, hauteur) pour une fenêtre de taille w × h.
fn button(w: i64, h: i64) -> (i64, i64, i64, i64) {
    (w / 2 - 90, h / 2 + 10, 180, 44)
}

fn paint(w: i64, h: i64) {
    unsafe { ui_fill(0, 0, w, h, token(TJ_FACE)) };
    let n = COUNT.load(Ordering::Relaxed).to_string();
    let tw = text_width(&n, 48, Font::Bold);
    text(&n, (w - tw) / 2, h / 2 - 24, token(TJ_TEXT1), 48, Font::Bold);
    let (bx, by, bw, bh) = button(w, h);
    let fill = if HOVER.load(Ordering::Relaxed) != 0 { token(TJ_HOVER) } else { token(TJ_ACCENT) };
    let ink = if HOVER.load(Ordering::Relaxed) != 0 { token(TJ_TEXT1) } else { token(TJ_ACCENT_INK) };
    unsafe { ui_rbox(bx, by, bw, bh, 8, fill, fill) };
    let label = "Compter";
    let lw = text_width(label, 16, Font::Bold);
    text(label, bx + (bw - lw) / 2, by + 28, ink, 16, Font::Bold);
    text("Espace : +1    Échap : zéro", 16, h - 16, token(TJ_TEXT2), 13, Font::Regular);
    unsafe { ui_update_all() };
}

/// Appelée par ui_w_event après un WEV_RESIZE, à la nouvelle taille.
extern "C" fn on_resize(w: i64, h: i64) -> i64 {
    paint(w, h);
    0
}

fn main() {
    if !open() {
        eprintln!("counter : UIKit absent");
        std::process::exit(1);
    }
    if !window(420, 300, "Compteur") {
        std::process::exit(1);
    }
    unsafe {
        ui_min_size(300, 220);
        ui_on_resize(on_resize as *const () as usize as i64, 0);
    }
    let (w, h) = size();
    paint(w, h);
    loop {
        let t = unsafe { ui_event(1000) };
        let (w, h) = size();
        match t {
            WEV_CLOSE => break,
            WEV_MOUSE => {
                let (bx, by, bw, bh) = button(w, h);
                let (x, y) = (ev(EV_X) as i64, ev(EV_Y) as i64);
                let over = (x >= bx && x < bx + bw && y >= by && y < by + bh) as i64;
                let mut redraw = HOVER.swap(over, Ordering::Relaxed) != over;
                if ev(EV_KIND) == 1 && ev(EV_CODE) & 1 != 0 && over != 0 {
                    COUNT.fetch_add(1, Ordering::Relaxed);
                    redraw = true;
                }
                if redraw {
                    paint(w, h);
                }
            }
            WEV_KEY if ev(EV_KIND) == 1 => match ev(EV_CODE) {
                KEY_SPACE | KEY_ENTER => {
                    COUNT.fetch_add(1, Ordering::Relaxed);
                    paint(w, h);
                }
                KEY_ESC => {
                    COUNT.store(0, Ordering::Relaxed);
                    paint(w, h);
                }
                _ => {}
            },
            WEV_THEME => paint(w, h),
            t if t > 0 => unsafe {
                ui_w_event(0); // (WEV_RESIZE : la nouvelle taille, puis on_resize)
            },
            _ => {}
        }
    }
}
