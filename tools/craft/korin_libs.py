"""korin_libs : les retouches de craft-libs pour Korin (docs/CRAFT.md), après tools/craft/korin_cfg.py, sur les copies
de travail que tools/craft/build.sh refait (~/korin-craft/craft-libs-<rev>). craft-libs est prise à plusieurs
révisions d'une app à l'autre : un fichier absent est sauté, une retouche qui ne trouve pas son texte le dit, et la
compilation dit s'il manquait.

    python3 -I tools/craft/korin_libs.py <dossier de craft-libs>
"""
import os
import sys

top = sys.argv[1]


def edit(rel, pairs):
    p = os.path.join(top, rel)
    if not os.path.isfile(p):
        return
    s = open(p, encoding="utf-8").read()
    for a, b in pairs:
        if b and b in s:
            continue
        if a not in s:
            print("korin_libs : %s (%s) : texte absent : %r" % (rel, os.path.basename(top), a[:80]))
            continue
        s = s.replace(a, b)
    open(p, "w", encoding="utf-8", newline="\n").write(s)
    with open(os.path.join(top, ".korin-changed"), "a", encoding="utf-8") as f:
        f.write(rel + "\n")


# l'apparence du système : sur Korin pas de guetteur (start_active rend None), mais l'énumération des guetteurs
# devient vide et le match de Drop ne compile plus : une variante qui ne sert pas
edit("crates/platform/src/appearance.rs", [
    ('    #[cfg(any(windows, target_os = "macos"))]\n    Native { stop: std::sync::mpsc::Sender<()>',
     '    /// Korin : aucun guetteur (tools/craft/korin_libs.py)\n'
     '    #[cfg(target_os = "korin")]\n    #[allow(dead_code)]\n    Idle,\n'
     '    #[cfg(any(windows, target_os = "macos"))]\n    Native { stop: std::sync::mpsc::Sender<()>'),
    ('                #[cfg(any(windows, target_os = "macos"))]\n                Worker::Native { stop, finished, thread } => {',
     '                #[cfg(target_os = "korin")]\n                Worker::Idle => {}\n'
     '                #[cfg(any(windows, target_os = "macos"))]\n                Worker::Native { stop, finished, thread } => {'),
])
# craft-font-store : la suppression des packs de langue passe par des dossiers ouverts (cap-primitives, donc rustix,
# qui ne connaît pas Korin) ; sur Korin, les mêmes étapes par chemins (Korin n'a pas de liens symboliques à éviter).
# Le code est dans src/language_packs.rs ou src/language_packs/removal.rs selon la révision.
KORIN_REMOVAL = '''
/// Korin : pas de liens symboliques ni d'`openat` complet ; les mêmes étapes par chemins (tools/craft/korin_libs.py).
#[cfg(target_os = "korin")]
pub(crate) struct PackRemovalRoot {
    packs: std::path::PathBuf,
    recovery: std::path::PathBuf,
    stage: Option<std::path::PathBuf>,
}

#[cfg(target_os = "korin")]
#[allow(dead_code)]
impl PackRemovalRoot {
    pub(super) fn open(store: &Path) -> std::io::Result<Self> {
        let packs = store.join(LANGUAGE_PACKS);
        let recovery = packs.join(".removing");
        for dir in [store, packs.as_path(), recovery.as_path()] {
            if !std::fs::metadata(dir)?.is_dir() {
                return Err(std::io::Error::other(format!("{} is not a folder", dir.display())));
            }
        }
        Ok(Self { packs, recovery, stage: None })
    }

    pub(crate) fn create_stage(&self, id: &str) -> std::io::Result<()> {
        std::fs::create_dir(self.recovery.join(id))
    }

    pub(crate) fn with_stage(mut self, id: &str) -> std::io::Result<Self> {
        let dir = self.recovery.join(id);
        match std::fs::metadata(&dir) {
            Ok(meta) if meta.is_dir() => {
                self.stage = Some(dir);
                Ok(self)
            }
            other => {
                let _ = std::fs::remove_dir(&dir);
                Err(other.err().unwrap_or_else(|| std::io::Error::other("recovery stage is not a folder")))
            }
        }
    }

    fn move_pack(&self, locale: &str) -> std::io::Result<()> {
        let stage = self.stage.as_ref().ok_or_else(|| std::io::Error::other("missing recovery stage"))?;
        std::fs::rename(self.packs.join(locale), stage.join(locale))
    }

    fn restore_pack(&self, locale: &str) -> std::io::Result<()> {
        let stage = self.stage.as_ref().ok_or_else(|| std::io::Error::other("missing recovery stage"))?;
        std::fs::rename(stage.join(locale), self.packs.join(locale))
    }

    pub(super) fn retains_receipt(&self, id: &str) -> bool {
        !matches!(
            std::fs::symlink_metadata(self.recovery.join(id)),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound
        )
    }

    pub(crate) fn remove_stage(mut self, id: &str, display_parent: &Path) -> std::io::Result<()> {
        drop(self.stage.take());
        self.remove(id, display_parent)
    }

    pub(crate) fn remove(&self, id: &str, _display_parent: &Path) -> std::io::Result<()> {
        std::fs::remove_dir_all(self.recovery.join(id))
    }
}
'''
for rel in ("crates/font-store/src/language_packs/removal.rs", "crates/font-store/src/language_packs.rs"):
    p = os.path.join(top, rel)
    if os.path.isfile(p) and "pub(crate) struct PackRemovalRoot {" in open(p, encoding="utf-8").read():
        s = open(p, encoding="utf-8").read()
        if 'target_os = "korin"' not in s:
            s = s.replace("pub(crate) struct PackRemovalRoot {", '#[cfg(not(target_os = "korin"))]\npub(crate) struct PackRemovalRoot {', 1)
            s = s.replace("impl PackRemovalRoot {", '#[cfg(not(target_os = "korin"))]\nimpl PackRemovalRoot {', 1)
            open(p, "w", encoding="utf-8", newline="\n").write(s + KORIN_REMOVAL)
            with open(os.path.join(top, ".korin-changed"), "a", encoding="utf-8") as f:
                f.write(rel + "\n")
        edit("crates/font-store/Cargo.toml", [
            ('cap-primitives = "4.0.3"\n', ""),
            ("[dev-dependencies]", "# Korin : PackRemovalRoot par chemins (tools/craft/korin_libs.py)\n"
             "[target.'cfg(not(any(target_arch = \"wasm32\", target_os = \"korin\")))'.dependencies]\n"
             "cap-primitives = \"4.0.3\"\n\n[dev-dependencies]"),
        ])
        break

print("korin_libs : %s" % os.path.basename(top))

# craft-pkcs7 : les opérations RSA à clé privée passent par aws-lc-rs (aws-lc, en C et en assembleur, sans port pour
# Korin) ; Korin prend la branche du navigateur (wasm32) : la vérification marche (RustCrypto), la signature RSA et la
# création de clés RSA sont refusées (P-256, P-384 signent)
NO_AWS = 'any(target_arch = "wasm32", target_os = "korin")'
AWS = 'aws-lc-rs = { version = "1.18", default-features = false, features = ["aws-lc-sys", "prebuilt-nasm"] }\n'
edit("crates/pkcs7/Cargo.toml", [
    ("[target.'cfg(not(target_arch = \"wasm32\"))'.dependencies]\n" + AWS + "getrandom = \"0.4\"\n",
     "[target.'cfg(not(target_arch = \"wasm32\"))'.dependencies]\ngetrandom = \"0.4\"\n\n"
     "# Korin : sans aws-lc (tools/craft/korin_libs.py)\n"
     "[target.'cfg(not(%s))'.dependencies]\n" % NO_AWS + AWS),
])
for rel in ["crates/pkcs7/src/keys.rs"]:
    p = os.path.join(top, rel)
    if os.path.isfile(p):
        t = open(p, encoding="utf-8").read()
        if NO_AWS not in t:
            t = t.replace('#[cfg(not(target_arch = "wasm32"))]', '#[cfg(not(%s))]' % NO_AWS)
            t = t.replace('#[cfg(target_arch = "wasm32")]', '#[cfg(%s)]' % NO_AWS)
            open(p, "w", encoding="utf-8", newline="\n").write(t)
            with open(os.path.join(top, ".korin-changed"), "a", encoding="utf-8") as f:
                f.write(rel + "\n")

# craft-ui-shell, la barre de titre (WordCraft, PhotoCraft) : sur Korin, Elegant dessine les boutons de la fenêtre sur le
# contenu, en haut à droite ; l'extrémité de droite de la barre est une réserve de 112 px (comme les feux de macOS à
# gauche), sans boutons de l'app, et le titre va au début de la place libre (à gauche, après les premiers contrôles)
edit("crates/ui-shell/src/shell/title_bar/measure.rs", [
    ("        if !shown {\n            return Ends::bare();\n        }\n        match frame {",
     "        if !shown {\n            return Ends::bare();\n        }\n"
     "        // Korin : les boutons d'Elegant (tools/craft/korin_libs.py)\n"
     "        if cfg!(target_os = \"korin\") {\n"
     "            let _ = frame;\n"
     "            return if rtl { Ends { start: End::inset(112.0), ..Ends::bare() } } else { Ends { end: End::inset(112.0), ..Ends::bare() } };\n"
     "        }\n        match frame {"),
])
edit("crates/ui-shell/src/shell/title_bar/title.rs", [
    ("        if width >= free {\n            return (0.0, free);\n        }\n        ((centre - width / 2.0).clamp(0.0, free - width), width)",
     "        if width >= free {\n            return (0.0, free);\n        }\n"
     "        // Korin : le titre à gauche, au début de la place libre (tools/craft/korin_libs.py)\n"
     "        if cfg!(target_os = \"korin\") {\n            let _ = centre;\n            return (0.0, width);\n        }\n"
     "        ((centre - width / 2.0).clamp(0.0, free - width), width)"),
])

# craft-menu : le menu natif de macOS sert aussi sur Korin (21/11/2026) : la barre de menus de Korin est faite comme
# celle de macOS (un menu au nom de l'app, les autres ensuite) et muda a une plateforme Korin (crate/muda-korin.rs).
# Ce que mac.rs demande à AppKit est remplacé par un petit module : pas de Masquer (sans objet), pas de menu ouvert à
# suivre, et le « currentEvent » d'AppKit (un choix vient-il d'un raccourci ?) donné par egui-korin, qui prend les
# raccourcis de la barre avant la fenêtre : un raccourci est rendu à egui (un champ de texte garde Ctrl+C), un clic
# lance la commande. Ctrl de Korin est le « cmd » de craft (Ctrl hors de macOS).
edit("crates/menu/Cargo.toml", [
    ('[target.\'cfg(target_os = "macos")\'.dependencies]\nmuda = { version = "0.21.2", default-features = false }',
     '[target.\'cfg(target_os = "korin")\'.dependencies]\nmuda = { version = "0.21.2", default-features = false }\n\n'
     '[target.\'cfg(target_os = "macos")\'.dependencies]\nmuda = { version = "0.21.2", default-features = false }'),
])
edit("crates/menu/src/lib.rs", [
    ('#[cfg(target_os = "macos")]\npub mod mac;', '#[cfg(any(target_os = "macos", target_os = "korin"))]\npub mod mac;'),
])
edit("crates/menu/src/mac.rs", [
    ('use objc2::MainThreadMarker;\nuse objc2_app_kit::{NSApplication, NSEventModifierFlags, NSEventType};',
     '#[cfg(target_os = "macos")]\nuse objc2::MainThreadMarker;\n#[cfg(target_os = "macos")]\n'
     'use objc2_app_kit::{NSApplication, NSEventModifierFlags, NSEventType};\n'
     '#[cfg(target_os = "korin")]\nuse korin_appkit::{MainThreadMarker, NSApplication, NSEventModifierFlags, NSEventType};\n\n'
     '/// Korin : ce que ce fichier demande à AppKit (tools/craft/korin_libs.py).\n'
     '#[cfg(target_os = "korin")]\n#[allow(non_snake_case, non_upper_case_globals, dead_code, clippy::unused_self)]\n'
     'mod korin_appkit {\n'
     '    #[derive(Clone, Copy)]\n    pub struct MainThreadMarker;\n'
     '    impl MainThreadMarker {\n        pub fn new() -> Option<MainThreadMarker> {\n            Some(MainThreadMarker)\n        }\n    }\n'
     '    #[derive(Clone, Copy, PartialEq, Eq)]\n    pub struct NSEventType(u8);\n'
     '    impl NSEventType {\n        pub const KeyDown: NSEventType = NSEventType(1);\n    }\n'
     '    #[derive(Clone, Copy)]\n    pub struct NSEventModifierFlags(u8);\n'
     '    impl NSEventModifierFlags {\n        pub const Command: NSEventModifierFlags = NSEventModifierFlags(1);\n'
     '        pub const Control: NSEventModifierFlags = NSEventModifierFlags(16);\n'
     '        pub const Option: NSEventModifierFlags = NSEventModifierFlags(2);\n'
     '        pub const Shift: NSEventModifierFlags = NSEventModifierFlags(4);\n'
     '        pub fn contains(self, o: NSEventModifierFlags) -> bool {\n            self.0 & o.0 != 0\n        }\n    }\n'
     '    pub struct KeyEvent(NSEventModifierFlags);\n'
     '    impl KeyEvent {\n        pub fn r#type(&self) -> NSEventType {\n            NSEventType::KeyDown\n        }\n'
     '        pub fn modifierFlags(&self) -> NSEventModifierFlags {\n            self.0\n        }\n    }\n'
     '    pub struct MainMenu;\n'
     '    impl MainMenu {\n        pub fn highlightedItem(&self) -> Option<()> {\n            None\n        }\n'
     '        pub fn cancelTracking(&self) {}\n    }\n'
     '    pub struct NSApplication;\n'
     '    impl NSApplication {\n        pub fn sharedApplication(_mtm: MainThreadMarker) -> NSApplication {\n            NSApplication\n        }\n'
     '        /// Ctrl de Korin -> le cmd de craft (⌘), comme hors de macOS\n'
     '        pub fn currentEvent(&self) -> Option<KeyEvent> {\n'
     '            muda::korin_key_mods().map(|(c, a, s)| KeyEvent(NSEventModifierFlags(c as u8 | (a as u8) << 1 | (s as u8) << 2)))\n        }\n'
     '        pub fn hide(&self, _sender: Option<&()>) {}\n'
     '        pub fn hideOtherApplications(&self, _sender: Option<&()>) {}\n'
     '        pub fn mainMenu(&self) -> Option<MainMenu> {\n            None\n        }\n    }\n'
     '}'),
])
edit("crates/menu/src/model.rs", [
    ('#[cfg(any(target_os = "macos", test))]\npub(crate) fn native_id(', '#[cfg(any(target_os = "macos", target_os = "korin", test))]\npub(crate) fn native_id('),
])
# Korin : pas de Masquer ni de Masquer les autres, de Services ni de Tout afficher (la barre n'en a pas)
edit("crates/menu/src/layout.rs", [
    ('    menu.extend([options.standard(Standard::Services), Node::Separator]);\n    let mut hide = Item::new(',
     '    #[cfg(not(target_os = "korin"))]\n    menu.extend([options.standard(Standard::Services), Node::Separator]);\n    let mut hide = Item::new('),
    ('    menu.push(Node::Item(hide));\n    let mut hide_others',
     '    #[cfg(not(target_os = "korin"))]\n    menu.push(Node::Item(hide));\n    #[cfg(target_os = "korin")]\n    let _ = hide;\n    let mut hide_others'),
    ('    menu.extend([Node::Item(hide_others), options.standard(Standard::ShowAll), Node::Separator]);',
     '    #[cfg(not(target_os = "korin"))]\n    menu.extend([Node::Item(hide_others), options.standard(Standard::ShowAll), Node::Separator]);\n'
     '    #[cfg(target_os = "korin")]\n    let _ = hide_others;'),
])
# les touches d'édition d'un champ de texte : celles de Korin (Ctrl+Gauche : un mot), pas celles de macOS
edit("crates/menu/src/driver.rs", [
    ('    CLIPBOARD.contains(c) || craft_keymap::text_editing_chords(craft_keymap::Platform::Mac).contains(c)',
     '    CLIPBOARD.contains(c) || craft_keymap::text_editing_chords(if cfg!(target_os = "korin") { craft_keymap::Platform::current() } else { craft_keymap::Platform::Mac }).contains(c)'),
])
