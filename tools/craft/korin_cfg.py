"""korin_cfg : dans une copie de craft-libs ou d'une app Craft, les `cfg` des « Unix autres que macOS » (X11,
Wayland, D-Bus, portails de freedesktop) excluent Korin (docs/CRAFT.md). Korin est `unix` sans être Linux : sans
cela, il prend ces branches et leurs caisses (zbus, x11rb, wayland-*, rustix).

    python3 -I tools/craft/korin_cfg.py <dossier>...

La même réécriture partout, branches complémentaires comprises (`not(any(all(unix, not(target_os = "macos")…`) :
Korin passe dans la branche « aucun des systèmes connus ». Restent pour Korin : `cfg(all(unix, not(target_arch =
"wasm32")))` (fichiers, processus), `cfg(any(unix, windows))` (même chose ; une app dont le repli
`not(any(unix, windows))` sert à Korin parce que sa branche Unix est celle du bureau est retouchée par korin_app.py) et `not(any(target_vendor = "apple", target_os = "android"))` (la langue et la région
lues dans LANG, LC_ALL…) : c'est du POSIX, que Korin a.
"""
import os
import sys

RULES = [
    ('unix, not(target_os = "macos")', 'unix, not(target_os = "macos"), not(target_os = "korin")'),
    ('not(any(target_os = "macos", target_os = "android", target_os = "emscripten"))',
     'not(any(target_os = "macos", target_os = "android", target_os = "emscripten", target_os = "korin"))'),
]

FALLBACK = "#[cfg(not(any(unix, windows)))]"
FALLBACK_KORIN = '#[cfg(any(target_os = "korin", not(any(unix, windows))))]'

total = 0
for top in sys.argv[1:]:
    changed = []
    for d, dirs, files in os.walk(top):
        dirs[:] = [x for x in dirs if x not in ("target", ".git")]
        for f in files:
            if not (f.endswith(".rs") or f == "Cargo.toml"):
                continue
            p = os.path.join(d, f)
            try:
                s = open(p, encoding="utf-8").read()
            except UnicodeDecodeError:
                continue
            t = s
            for a, b in RULES:
                if b not in t:			# (une seconde passe ne double rien)
                    t = t.replace(a, b)
            # un fichier dont la branche Unix était celle du bureau (réécrite à l'instant) : son repli « aucun Unix ni
            # Windows » sert aussi à Korin (sinon la fonction lui manque)
            if t != s and FALLBACK in t:
                t = t.replace(FALLBACK, FALLBACK_KORIN)
            if t != s:
                open(p, "w", encoding="utf-8", newline="\n").write(t)
                total += 1
                changed.append(os.path.relpath(p, top))
    # les fichiers changés, que tools/craft/craftlibs_patch.py lit (les révisions de craft-libs à rediriger)
    with open(os.path.join(top, ".korin-changed"), "a", encoding="utf-8") as f:
        f.writelines(c + "\n" for c in changed)
print("korin_cfg : %d fichiers" % total)
