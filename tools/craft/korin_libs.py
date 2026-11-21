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
