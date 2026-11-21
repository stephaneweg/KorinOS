"""egui036 : l'hôte egui de Korin pour les apps Craft encore sur egui et eframe 0.36 (Craft Anything, AniCraft ;
docs/CRAFT.md). Une copie générée d'egui-korin, eframe-korin, winit-korin et du rendu CPU (hdrshot-egui-backend 0.2.0
et son patch), aux versions 0.36 ; tools/craft/build.sh la fait avant de construire une telle app, korin_app.py y
branche eframe. Le source reste celui de src/lib (une seule version à suivre) ; ce qui diffère entre 0.36 et 0.37 est
réécrit ici, et chaque réécriture vérifie qu'elle a trouvé son texte.

    python3 -I tools/craft/egui036.py <racine du dépôt> <dossier de sortie> <registre de cargo (src/index.crates.io-…)>
"""
import json
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request

root, out, reg = sys.argv[1], sys.argv[2], sys.argv[3]
HDR = "hdrshot-egui-backend-0.2.0"


def edit(rel, pairs):
    p = os.path.join(out, rel)
    s = open(p, encoding="utf-8").read()
    for a, b in pairs:
        if a not in s:
            sys.exit("egui036 : %s : texte absent : %r" % (rel, a[:80]))
        s = s.replace(a, b)
    open(p, "w", encoding="utf-8", newline="\n").write(s)


if os.path.isdir(out):
    shutil.rmtree(out)
os.makedirs(out)
for c in ("egui-korin", "eframe-korin", "winit-korin"):
    shutil.copytree(os.path.join(root, "src", "lib", c), os.path.join(out, c),
                    ignore=shutil.ignore_patterns("target", "Cargo.lock"))

# le rendu CPU : la caisse de crates.io (registre, sinon téléchargée) et notre patch
src = os.path.join(reg, HDR)
if os.path.isdir(src):
    shutil.copytree(src, os.path.join(out, "hdrshot"))
else:
    dl = os.path.join(out, "_dl")
    os.makedirs(dl)
    crate = os.path.join(dl, HDR + ".crate")
    urllib.request.urlretrieve("https://static.crates.io/crates/hdrshot-egui-backend/%s.crate" % HDR, crate)
    with tarfile.open(crate) as t:
        t.extractall(dl, filter="data")
    shutil.move(os.path.join(dl, HDR), os.path.join(out, "hdrshot"))
    shutil.rmtree(dl)
with open(os.path.join(root, "tools", "craft", "patches", HDR + ".patch"), "rb") as f:
    subprocess.run(["patch", "-p1", "-s", "--no-backup-if-mismatch"], stdin=f, cwd=os.path.join(out, "hdrshot"),
                   check=True)

# les versions : egui 0.36, eframe 0.36.99 (un [patch] de « eframe 0.36 »), le rendu CPU par chemin
edit("hdrshot/Cargo.toml", [('version = "0.37.0"', 'version = "0.36.0"')])
edit("egui-korin/Cargo.toml", [
    ('egui = { version = "0.37", default-features = false, features = ["default_fonts", "monochrome_emoji_fonts"] }',
     'egui = { version = "0.36", default-features = false, features = ["default_fonts"] }'),
    ('hdrshot-egui-backend = { version = "=0.2.0",', 'hdrshot-egui-backend = { path = "../hdrshot",'),
    ('egui_demo_lib = { version = "0.37"', 'egui_demo_lib = { version = "0.36"'),
])
edit("eframe-korin/Cargo.toml", [
    ('version = "0.37.99"', 'version = "0.36.99"'),
    ('egui = { version = "0.37"', 'egui = { version = "0.36"'),
    ('egui-wgpu = { version = "0.37"', 'egui-wgpu = { version = "0.36"'),
])
# build.rs trouve le dépôt trois dossiers plus haut : ici, son chemin
edit("egui-korin/build.rs", [('let root = man.join("../../..");', 'let root = PathBuf::from(%s);' % json.dumps(root))])
# egui 0.36 : MouseWheel sans `source`
edit("egui-korin/src/lib.rs", [("                    source: egui::MouseWheelSource::Wheel,\n", "")])
# la langue de l'interface par la fenêtre est une API de la fourche de storytold (egui 0.37) : sans objet en 0.36
edit("eframe-korin/src/lib.rs", [
    ("        if let Some(ctx) = &self.egui_ctx\n            && ctx.options(|o| o.language.as_deref() != lang)\n"
     "        {\n            ctx.set_language(lang.map(ToOwned::to_owned));\n        }\n",
     "        let _ = lang;\n"),
])
print("egui036 : %s" % out)
