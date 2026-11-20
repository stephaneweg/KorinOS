//! Les touches : codes HID USB (EV_CODE de UIKit, la touche physique) vers egui::Key.
use egui::Key;

/// La touche physique, d'après son code HID (page 7 des usages USB).
pub fn physical(hid: i32) -> Option<Key> {
    const LETTERS: [Key; 26] = [
        Key::A, Key::B, Key::C, Key::D, Key::E, Key::F, Key::G, Key::H, Key::I, Key::J, Key::K, Key::L, Key::M,
        Key::N, Key::O, Key::P, Key::Q, Key::R, Key::S, Key::T, Key::U, Key::V, Key::W, Key::X, Key::Y, Key::Z,
    ];
    const DIGITS: [Key; 10] = [
        Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5, Key::Num6, Key::Num7, Key::Num8, Key::Num9, Key::Num0,
    ];
    const FKEYS: [Key; 12] = [
        Key::F1, Key::F2, Key::F3, Key::F4, Key::F5, Key::F6, Key::F7, Key::F8, Key::F9, Key::F10, Key::F11, Key::F12,
    ];
    Some(match hid {
        0x04..=0x1D => LETTERS[(hid - 0x04) as usize],
        0x1E..=0x27 => DIGITS[(hid - 0x1E) as usize],
        0x28 | 0x58 => Key::Enter,
        0x29 => Key::Escape,
        0x2A => Key::Backspace,
        0x2B => Key::Tab,
        0x2C => Key::Space,
        0x2D | 0x56 => Key::Minus,
        0x2E => Key::Equals,
        0x2F => Key::OpenBracket,
        0x30 => Key::CloseBracket,
        0x31 => Key::Backslash,
        0x33 => Key::Semicolon,
        0x34 => Key::Quote,
        0x35 => Key::Backtick,
        0x36 => Key::Comma,
        0x37 => Key::Period,
        0x38 => Key::Slash,
        0x3A..=0x45 => FKEYS[(hid - 0x3A) as usize],
        0x49 => Key::Insert,
        0x4A => Key::Home,
        0x4B => Key::PageUp,
        0x4C => Key::Delete,
        0x4D => Key::End,
        0x4E => Key::PageDown,
        0x4F => Key::ArrowRight,
        0x50 => Key::ArrowLeft,
        0x51 => Key::ArrowDown,
        0x52 => Key::ArrowUp,
        0x57 => Key::Plus,
        _ => return None,
    })
}

/// La touche logique : la lettre ou le chiffre que donne la disposition (AZERTY : la touche A physique est Q),
/// sinon la touche physique. Les raccourcis (Ctrl+Z…) suivent ainsi ce qui est écrit sur le clavier.
pub fn logical(hid: i32, ch: i32) -> Option<Key> {
    if let Some(c) = char::from_u32(ch as u32) {
        let c = c.to_ascii_uppercase();
        if c.is_ascii_uppercase() {
            return Key::from_name(&c.to_string());
        }
        if c.is_ascii_digit() && !(0x1E..=0x27).contains(&hid) {
            return Key::from_name(&c.to_string());
        }
    }
    physical(hid)
}
