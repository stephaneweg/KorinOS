"""Le dossier $VIBE_HOME de Vibe sous Korin (/.vibe) : ce qu'il faut a Vibe pour connaitre le systeme.

    python -I tools/vibe/home.py <racine de la carte>      (build.ps1 : build/sdroot ; la carte : E:\\)

Pose dans <racine>/.vibe :
  AGENTS.md                   les consignes de l'utilisateur, lues par Vibe a chaque seance (tools/vibe/home/AGENTS.md)
  skills/korin/SKILL.md       la competence « korin » : le systeme, les kits, la libc
  skills/korin/reference/     les pages anglaises du guide du developpeur (src/apps/help/data/api/*_en.md)
  skills/korin-app/SKILL.md   la competence « korin-app » : ecrire, construire (pibuild), installer une app
  skills/korin-app/skeleton/  le squelette d'app du guide (src/apps/help/data/api/skeleton)
  skills/korin-csharp/, korin-freebasic/, korin-diag/   C# (.NET, WinForms, Avalonia), FreeBASIC, diagnostic
  skills/korin-game/inc/, examples/   le cadre des jeux en C (src/inc/game*.h) et arkanoid, tetris, invaders
Le reste de .vibe (.env et la cle du compte, config.toml, logs, seances) n'est pas touche ; les competences de tools/vibe/home/skills
sont remplacees en entier.
"""
import os
import shutil
import sys

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if len(sys.argv) != 2:
    sys.exit(__doc__)
home = os.path.join(sys.argv[1], ".vibe")
src = os.path.join(root, "tools", "vibe", "home")
api = os.path.join(root, "src", "apps", "help", "data", "api")
skills = os.path.join(home, "skills")
os.makedirs(skills, exist_ok=True)
shutil.copyfile(os.path.join(src, "AGENTS.md"), os.path.join(home, "AGENTS.md"))
for name in os.listdir(os.path.join(src, "skills")):
    d = os.path.join(skills, name)
    shutil.rmtree(d, ignore_errors=True)
    shutil.copytree(os.path.join(src, "skills", name), d)
ref = os.path.join(skills, "korin", "reference")
os.makedirs(ref, exist_ok=True)
pages = sorted(f for f in os.listdir(api) if f.endswith("_en.md"))
for f in pages:
    shutil.copyfile(os.path.join(api, f), os.path.join(ref, f))
shutil.copytree(os.path.join(api, "skeleton"), os.path.join(skills, "korin-app", "skeleton"))
# korin-game : le cadre des jeux en C (src/inc) et trois jeux à lire, sans leur ancienne version en assembleur
GAME_INC = ["gamefb.h", "gamedraw.h", "gamepause.h", "gamesound.h", "gamepad.h"]
GAME_EXAMPLES = ["arkanoid", "tetris", "invaders"]
game = os.path.join(skills, "korin-game")
os.makedirs(os.path.join(game, "inc"), exist_ok=True)
for f in GAME_INC:
    shutil.copyfile(os.path.join(root, "src", "inc", f), os.path.join(game, "inc", f))
for a in GAME_EXAMPLES:
    shutil.copytree(os.path.join(root, "src", "apps", a), os.path.join(game, "examples", a),
                    ignore=shutil.ignore_patterns("*.asm", "*.inc", "*.rgba", "*.png", "*.bin"))
# korin-widget : les deux widgets du bureau du système, à lire comme exemples
wex = os.path.join(skills, "korin-widget", "examples")
os.makedirs(wex, exist_ok=True)
for w in ("agenda", "stickies"):
    shutil.copyfile(os.path.join(root, "src", "sys", w, w + ".c"), os.path.join(wex, w + ".c"))
print(f"{home} : AGENTS.md, competences {', '.join(sorted(os.listdir(os.path.join(src, 'skills'))))}, {len(pages)} pages du guide")
