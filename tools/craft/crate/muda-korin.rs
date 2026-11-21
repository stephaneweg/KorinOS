// La plateforme Korin de muda (tools/craft/crate_korin.py, docs/CRAFT.md) : les menus dans la barre de menus de Korin
// (UIKit ui_menu, par l'hôte egui-korin). La couche commune de muda garde l'état des entrées (textes, coches, grisé,
// raccourcis, enfants) ; ici, à chaque changement, l'arbre du menu installé (Menu::init_for_nsapp, comme sur macOS)
// est relu après la passe d'egui et publié en texte (lignes M, I, -, S … E ; « M* » : le menu de l'app, que la barre
// verse dans son menu au nom de l'app), les états par lot (ui_menu_states), et un choix dans la barre rejoue l'action
// de l'entrée (MenuEvent, bascule d'une coche, commande standard donnée à egui).
//
// egui-korin est appelé par trois symboles C (kh_menu_set et kh_menu_states de son relais, egui_korin_menu_hooks et
// egui_korin_menu_inject exportés par sa caisse) : muda ne dépend pas d'egui, ni de sa version.

use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{c_char, c_long};
use std::sync::atomic::{AtomicBool, Ordering};

use crate::{
    accelerator::MenuAccelerator,
    icon::NoIcon,
    items::{IconType, MenuItemAction, PredefinedMenuItemType},
    util::AddOp,
    Menu, MenuEvent, MenuItemKind,
};

pub(crate) type PlatformIcon = NoIcon;

#[cfg(feature = "snapshot")]
pub(crate) fn dispatch_on_main_thread<F>(f: F)
where
    F: FnOnce() + Send + 'static,
{
    f();
}

#[cfg(feature = "snapshot")]
pub(crate) fn run_predefined_action(item_type: &crate::items::PredefinedMenuItemType) {
    if let Some(code) = standard(item_type) {
        unsafe { egui_korin_menu_inject(code) };
    }
}

unsafe extern "C" {
    fn kh_menu_set(text: *const c_char) -> c_long;
    fn kh_menu_states(pairs: *const c_long, n: c_long) -> c_long;
    fn egui_korin_menu_hooks(on_chosen: extern "C" fn(i64), after_pass: extern "C" fn());
    fn egui_korin_menu_inject(code: i32);
    fn egui_korin_menu_key_mods() -> i32;
}

/// Pendant un choix dans la barre (le gestionnaire de MenuEvent) : (Ctrl, Alt, Maj) s'il vient d'un raccourci, None
/// pour un clic ; craft-menu s'en sert comme du `currentEvent` d'AppKit (muda::korin_key_mods).
pub fn key_mods() -> Option<(bool, bool, bool)> {
    let m = unsafe { egui_korin_menu_key_mods() };
    if m < 0 { None } else { Some((m & 1 != 0, m & 2 != 0, m & 4 != 0)) }
}

/// Un changement depuis la dernière publication (texte, état, enfants).
static DIRTY: AtomicBool = AtomicBool::new(false);

fn dirty() {
    DIRTY.store(true, Ordering::Relaxed);
}

/// Le plus long texte que la barre accepte (WS_MENU_MAX, sans le NUL).
const MAX: usize = 16383;

struct Published {
    root: Option<Menu>,
    /// L'action de chaque id du texte publié (son rang).
    actions: Vec<MenuItemAction>,
    text: String,
    /// Deux tampons en alternance : UIKit reconnaît un texte déjà traduit à son adresse.
    bufs: [Vec<u8>; 2],
    which: usize,
    /// Les états envoyés (id -> 1 grisée, 2 cochée).
    states: HashMap<usize, i64>,
}

thread_local! {
    static PUBLISHED: RefCell<Published> = RefCell::new(Published {
        root: None,
        actions: Vec::new(),
        text: String::new(),
        bufs: [Vec::new(), Vec::new()],
        which: 0,
        states: HashMap::new(),
    });
}

/// (Menu::init_for_nsapp) ce menu devient celui de la barre de Korin.
pub(crate) fn install(menu: Option<Menu>) {
    PUBLISHED.with(|p| p.borrow_mut().root = menu);
    unsafe { egui_korin_menu_hooks(on_chosen, after_pass) };
    dirty();
    publish();
}

extern "C" fn after_pass() {
    publish();
}

extern "C" fn on_chosen(id: i64) {
    let action = PUBLISHED.with(|p| p.borrow().actions.get(id as usize).cloned());
    match action {
        Some(MenuItemAction::Emit(id)) => MenuEvent::send(MenuEvent { id }),
        Some(MenuItemAction::Toggle(id, state)) => {
            if let Some(state) = state.upgrade() {
                let c = !state.borrow().checked;
                state.borrow_mut().checked = c;
            }
            dirty();
            MenuEvent::send(MenuEvent { id });
        }
        Some(MenuItemAction::Predefined(state)) => {
            if let Some(state) = state.upgrade() {
                let ty = state.borrow().predefined_item_type.clone();
                if let Some(code) = standard(&ty) {
                    unsafe { egui_korin_menu_inject(code) };
                }
            }
        }
        None => {}
    }
}

/// Une commande standard -> son code pour egui-korin (menu::Standard).
fn standard(t: &PredefinedMenuItemType) -> Option<i32> {
    Some(match t {
        PredefinedMenuItemType::Undo => 0,
        PredefinedMenuItemType::Redo => 1,
        PredefinedMenuItemType::Cut => 2,
        PredefinedMenuItemType::Copy => 3,
        PredefinedMenuItemType::Paste | PredefinedMenuItemType::PasteAndMatchStyle => 4,
        PredefinedMenuItemType::SelectAll => 5,
        _ => return None,
    })
}

/// Le libellé sans ses marques de mnémonique (« &Fichier » -> « Fichier », « && » -> « & »), sur une ligne.
fn label(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut chars = s.chars().peekable();
    while let Some(c) = chars.next() {
        match c {
            '&' if chars.peek() == Some(&'&') => {
                out.push('&');
                chars.next();
            }
            '&' => {}
            '\t' | '\n' | '\r' => out.push(' '),
            c => out.push(c),
        }
    }
    out.trim().to_owned()
}

/// Un raccourci, comme la barre de Korin les montre (« Ctrl+Maj+S ») : Cmd (macOS) et Ctrl font Ctrl.
fn shortcut(a: Option<&MenuAccelerator>) -> String {
    use keyboard_types::{Code, Key, Modifiers, NamedKey};
    let Some(a) = a else { return String::new() };
    let (mods, key) = match a {
        MenuAccelerator::Physical(a) => {
            let k = match a.key {
                Code::Comma => ",".into(),
                Code::Period => ".".into(),
                Code::Slash => "/".into(),
                Code::Backslash => "\\".into(),
                Code::Minus => "-".into(),
                Code::Equal => "=".into(),
                Code::Quote => "'".into(),
                Code::Semicolon => ";".into(),
                Code::BracketLeft => "[".into(),
                Code::BracketRight => "]".into(),
                Code::Backquote => "`".into(),
                Code::Space => "Espace".into(),
                Code::Enter => "Entrée".into(),
                Code::Escape => "Échap".into(),
                Code::Backspace => "Retour arrière".into(),
                Code::Delete => "Suppr".into(),
                Code::Tab => "Tab".into(),
                Code::ArrowLeft => "←".into(),
                Code::ArrowRight => "→".into(),
                Code::ArrowUp => "↑".into(),
                Code::ArrowDown => "↓".into(),
                k => {
                    let s = format!("{k:?}");
                    s.strip_prefix("Key").or(s.strip_prefix("Digit")).map(str::to_owned).unwrap_or(s)
                }
            };
            (a.mods, k)
        }
        MenuAccelerator::Logical(a) => {
            let k = match &a.key {
                Key::Character(c) => c.to_uppercase(),
                Key::Named(NamedKey::Enter) => "Entrée".into(),
                Key::Named(NamedKey::Escape) => "Échap".into(),
                Key::Named(NamedKey::Delete) => "Suppr".into(),
                Key::Named(n) => format!("{n:?}"),
                k => format!("{k:?}"),
            };
            (a.mods, k)
        }
    };
    let mut s = String::new();
    if mods.intersects(Modifiers::CONTROL | Modifiers::META) {
        s.push_str("Ctrl+");
    }
    if mods.contains(Modifiers::SHIFT) {
        s.push_str("Maj+");
    }
    if mods.contains(Modifiers::ALT) {
        s.push_str("Alt+");
    }
    s.push_str(&key);
    s
}

/// (publish) le texte et les états, construits depuis l'arbre
struct Build {
    text: String,
    actions: Vec<MenuItemAction>,
    states: Vec<(usize, i64)>,
}

impl Build {
    fn item(&mut self, prefix: &str, text: &str, accel: Option<&MenuAccelerator>, action: MenuItemAction, state: i64) {
        let id = self.actions.len();
        self.actions.push(action);
        if state != 0 {
            self.states.push((id, state));
        }
        let accel = shortcut(accel);
        self.text.push_str(&format!("I{id}\t{prefix}{}\t{accel}\n", label(text)));
    }

    /// Les entrées d'un menu ; depth 1 : un menu de la barre, 2 : un sous-menu (« S » … « E ») ; au-delà, aplati dans
    /// le sous-menu, les libellés préfixés du titre (« Exporter › PDF »). Un séparateur n'est posé qu'entre deux
    /// entrées montrées (ni en tête, ni deux de suite, ni en fin).
    fn children(&mut self, items: &[MenuItemKind], depth: usize, prefix: &str, app_menu: bool) {
        let mut pending = false;
        let mut any = false;
        for k in items {
            let before = self.text.len();
            let sep_at = self.text.len();
            match k {
                MenuItemKind::Submenu(s) => {
                    let title = label(&s.text());
                    if depth == 1 {
                        self.text.push_str(&format!("S{title}\n"));
                        self.children(&s.items(), 2, "", false);
                        self.text.push_str("E\n");
                    } else {
                        let p = format!("{prefix}{title} › ");
                        self.children(&s.items(), depth + 1, &p, false);
                    }
                }
                MenuItemKind::MenuItem(i) => {
                    // (le menu de l'app : son Quitter est celui de la barre)
                    if app_menu && label(&i.text()).starts_with("Quit") {
                        continue;
                    }
                    let st = if i.is_enabled() { 0 } else { 1 };
                    let accel = i.state.borrow().accelerator.clone();
                    self.item(prefix, &i.text(), accel.as_ref(), k.click_action(), st);
                }
                MenuItemKind::Check(i) => {
                    let st = (if i.is_enabled() { 0 } else { 1 }) | (if i.is_checked() { 2 } else { 0 });
                    let accel = i.state.borrow().accelerator.clone();
                    self.item(prefix, &i.text(), accel.as_ref(), k.click_action(), st);
                }
                MenuItemKind::Icon(i) => {
                    let st = if i.is_enabled() { 0 } else { 1 };
                    let accel = i.state.borrow().accelerator.clone();
                    self.item(prefix, &i.text(), accel.as_ref(), k.click_action(), st);
                }
                MenuItemKind::Predefined(p) => {
                    let ty = p.state.borrow().predefined_item_type.clone();
                    if matches!(ty, PredefinedMenuItemType::Separator) {
                        pending = any;
                        continue;
                    }
                    if standard(&ty).is_none() {
                        continue; // (Services, Masquer, Quitter, Réduire… : la barre et le cadre de Korin les ont)
                    }
                    let text = p.text();
                    self.item(prefix, &text, ty.accelerator().as_ref(), k.click_action(), 0);
                }
            }
            if self.text.len() > before {
                if pending {
                    self.text.insert_str(sep_at, "-\n");
                    pending = false;
                }
                any = true;
            }
        }
    }
}

/// Relit l'arbre installé et publie ce qui a changé (texte, états). Après chaque passe d'egui.
fn publish() {
    if !DIRTY.swap(false, Ordering::Relaxed) {
        return;
    }
    PUBLISHED.with(|p| {
        let mut p = p.borrow_mut();
        let Some(root) = p.root.clone() else {
            if !p.text.is_empty() {
                p.text.clear();
                p.actions.clear();
                p.states.clear();
                unsafe { kh_menu_set(std::ptr::null()) };
            }
            return;
        };
        let mut b = Build { text: String::new(), actions: Vec::new(), states: Vec::new() };
        let top = root.items();
        for (n, k) in top.iter().enumerate() {
            let MenuItemKind::Submenu(s) = k else { continue };
            // le menu de l'app (macOS) : le premier, s'il a Services, Masquer ou Quitter
            let app_menu = n == 0
                && s.items().iter().any(|c| {
                    matches!(c, MenuItemKind::Predefined(p) if matches!(p.state.borrow().predefined_item_type,
                        PredefinedMenuItemType::Services | PredefinedMenuItemType::Hide | PredefinedMenuItemType::Quit))
                });
            let mark = b.text.len();
            if app_menu {
                b.text.push_str("M*\n");
            } else {
                b.text.push_str(&format!("M{}\n", label(&s.text())));
            }
            b.children(&s.items(), 1, "", app_menu);
            if b.text.len() > MAX {
                b.text.truncate(mark); // (trop long : ce menu et les suivants ne sont pas publiés)
                break;
            }
        }
        let text_changed = b.text != p.text;
        let mut send: Vec<c_long> = Vec::new();
        let new: HashMap<usize, i64> = b.states.iter().copied().collect();
        if text_changed {
            p.which ^= 1;
            let w = p.which;
            p.bufs[w].clear();
            p.bufs[w].extend_from_slice(b.text.as_bytes());
            p.bufs[w].push(0);
            // les états : ceux du nouveau texte, et la remise à zéro des ids qui en avaient un
            for (&id, _) in p.states.iter() {
                if !new.contains_key(&id) {
                    send.extend([id as c_long, 0]);
                }
            }
            for (&id, &st) in &new {
                send.extend([id as c_long, st as c_long]);
            }
        } else {
            for (&id, &st) in &new {
                if p.states.get(&id) != Some(&st) {
                    send.extend([id as c_long, st as c_long]);
                }
            }
            for (&id, _) in p.states.iter() {
                if !new.contains_key(&id) {
                    send.extend([id as c_long, 0]);
                }
            }
        }
        if !send.is_empty() {
            unsafe { kh_menu_states(send.as_ptr(), (send.len() / 2) as c_long) };
        }
        if text_changed {
            let w = p.which;
            let ptr = p.bufs[w].as_ptr();
            unsafe { kh_menu_set(if b.text.is_empty() { std::ptr::null() } else { ptr.cast() }) };
            p.text = b.text;
        }
        p.actions = b.actions;
        p.states = new;
    });
}

pub(crate) struct PlatformMenu;

impl PlatformMenu {
    pub(crate) fn new() -> Self {
        Self
    }

    pub(crate) fn attach(&mut self, _child: &MenuItemKind, _op: AddOp) -> crate::Result<()> {
        dirty();
        Ok(())
    }

    pub(crate) fn remove_at(&mut self, _index: usize, _child: &MenuItemKind) {
        dirty();
    }
}

pub(crate) struct PlatformMenuItem;

impl PlatformMenuItem {
    pub(crate) fn new(_click: MenuItemAction) -> Self {
        Self
    }

    pub(crate) fn new_submenu(_click: MenuItemAction) -> Self {
        Self
    }

    pub(crate) fn text(&self) -> Option<String> {
        None
    }

    pub(crate) fn set_text(&mut self, _text: &str, _accelerator: Option<&MenuAccelerator>) {
        dirty();
    }

    pub(crate) fn is_enabled(&self) -> Option<bool> {
        None
    }

    pub(crate) fn set_enabled(&mut self, _enabled: bool) {
        dirty();
    }

    pub(crate) fn set_accelerator(&mut self, _text: &str, _accelerator: Option<&MenuAccelerator>) -> crate::Result<()> {
        dirty();
        Ok(())
    }

    pub(crate) fn is_checked(&self) -> Option<bool> {
        None
    }

    pub(crate) fn set_checked(&mut self, _checked: bool) {
        dirty();
    }

    pub(crate) fn set_icon(&mut self, _icon: Option<&IconType>) {}

    pub(crate) fn attach(&mut self, _child: &MenuItemKind, _op: AddOp) -> crate::Result<()> {
        dirty();
        Ok(())
    }

    pub(crate) fn remove_at(&mut self, _index: usize, _child: &MenuItemKind) {
        dirty();
    }

    /// (Submenu::set_as_windows_menu_for_nsapp, set_as_help_menu_for_nsapp : sans objet ; la barre reconnaît le
    /// menu Aide à son titre)
    pub(crate) fn set_as_windows_menu_for_nsapp(&mut self) {}

    pub(crate) fn set_as_help_menu_for_nsapp(&mut self) {}
}
