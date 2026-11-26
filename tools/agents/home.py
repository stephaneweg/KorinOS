"""Ce que les agents de code savent de Korin, au système : /res/agents (paquet agent-skills ; décision du 26/11/2026).

    python -I tools/agents/home.py <racine de la carte>

Pose dans <racine>/res/agents :
  skills/<nom>/          les compétences, communes à tous les agents (Vibe, claude-acp, mai-acp, le copilote de
                         KorinStudio) : celles du dépôt (.agents/skills : système, langages, kits, POSIX, port,
                         paquets) et celles de tools/vibe/home/skills (korin et le guide du développeur, korin-app et
                         son squelette et ses exemples, korin-game, korin-widget, korin-csharp...) ; celles des apps
                         restent dans /apps/<app>.app/SKILL.md
  vibe/AGENTS.md         les consignes de Vibe (tools/vibe/home/AGENTS.md)
  claude-acp/AGENTS.md   celles de claude-acp et de mai-acp (tools/claude/home/AGENTS.md)
  mai-acp/AGENTS.md

Dans /res, seul l'uid 0 écrit : un programme du compte ne peut plus glisser une consigne dans une compétence que
l'agent suit avec ses outils (avant : une copie dans le dossier de chaque compte). libacp (src/lib/acp/home.c) copie
AGENTS.md dans le dossier du compte s'il y manque (il est à l'utilisateur une fois posé) ; les compétences sont lues
ici (VIBE_SKILL_PATHS pour Vibe, claude_acp/agent.py pour claude-acp et mai-acp). Claude Code garde les siennes dans
/etc/claude-code/.claude (tools/claude/home.py : l'endroit fixe où il les cherche ; /etc aussi n'est qu'à l'uid 0).
"""
import os
import shutil
import sys

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if len(sys.argv) != 2:
    sys.exit(__doc__)
out = os.path.join(sys.argv[1], "res", "agents")
shutil.rmtree(out, ignore_errors=True)
skills = os.path.join(out, "skills")
os.makedirs(skills)
for agent, src in (("vibe", ("vibe", "home")), ("claude-acp", ("claude", "home")), ("mai-acp", ("claude", "home"))):
    os.makedirs(os.path.join(out, agent))
    shutil.copyfile(os.path.join(root, "tools", *src, "AGENTS.md"), os.path.join(out, agent, "AGENTS.md"))
names = []
for src in (os.path.join(root, ".agents", "skills"), os.path.join(root, "tools", "vibe", "home", "skills")):
    for name in sorted(os.listdir(src)):
        if os.path.isfile(os.path.join(src, name, "SKILL.md")) and name not in names:
            shutil.copytree(os.path.join(src, name), os.path.join(skills, name))
            names.append(name)
# le guide du développeur dans korin, le squelette d'app dans korin-app
api = os.path.join(root, "src", "apps", "help", "data", "api")
ref = os.path.join(skills, "korin", "reference")
os.makedirs(ref, exist_ok=True)
pages = sorted(f for f in os.listdir(api) if f.endswith("_en.md"))
for f in pages:
    shutil.copyfile(os.path.join(api, f), os.path.join(ref, f))
shutil.copytree(os.path.join(api, "skeleton"), os.path.join(skills, "korin-app", "skeleton"))
# des apps de Korin à lire comme exemples (korin-app/examples) : le PC n'a pas /src ; leurs sources C et C++, sans
# l'ancienne version en assembleur
EXAMPLES = ["targets", "2048", "calculator", "snake", "bubbles", "notes"]
IGNORE = shutil.ignore_patterns("*.asm", "*.inc", "*.rgba", "*.png", "*.bin")
ex = os.path.join(skills, "korin-app", "examples")
for a in EXAMPLES:
    shutil.copytree(os.path.join(root, "src", "apps", a), os.path.join(ex, a), ignore=IGNORE)
# korin-game : le cadre des jeux en C (src/inc) et trois jeux à lire
GAME_INC = ["gamefb.h", "gamedraw.h", "gamepause.h", "gamesound.h", "gamepad.h"]
game = os.path.join(skills, "korin-game")
os.makedirs(os.path.join(game, "inc"), exist_ok=True)
for f in GAME_INC:
    shutil.copyfile(os.path.join(root, "src", "inc", f), os.path.join(game, "inc", f))
for a in ["arkanoid", "tetris", "invaders"]:
    shutil.copytree(os.path.join(root, "src", "apps", a), os.path.join(game, "examples", a), ignore=IGNORE)
# korin-widget : les deux widgets du bureau du système, à lire comme exemples
wex = os.path.join(skills, "korin-widget", "examples")
os.makedirs(wex, exist_ok=True)
for w in ("agenda", "stickies"):
    shutil.copyfile(os.path.join(root, "src", "sys", w, w + ".c"), os.path.join(wex, w + ".c"))
print("%s : %d compétences, %d pages du guide, AGENTS.md de vibe, claude-acp, mai-acp" % (out, len(names), len(pages)))
