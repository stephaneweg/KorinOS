"""korin_libs : les retouches de craft-libs pour Korin (docs/CRAFT.md), après tools/craft/korin_cfg.py, sur les copies
de travail que tools/craft/build.sh refait (~/korin-craft/craft-libs-<rev>). craft-libs est prise à plusieurs
révisions d'une app à l'autre : un fichier absent est sauté, une retouche qui ne trouve pas son texte le dit, et la
compilation dit s'il manquait. (La suppression des packs de langue sans cap-primitives est dans
tools/craft/patches/craft-libs.patch.)

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
        if b in s:
            continue
        if a not in s:
            print("korin_libs : %s (%s) : texte absent : %r" % (rel, os.path.basename(top), a[:80]))
            continue
        s = s.replace(a, b)
    open(p, "w", encoding="utf-8", newline="\n").write(s)


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
print("korin_libs : %s" % os.path.basename(top))
