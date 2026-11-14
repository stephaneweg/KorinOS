"""Le dossier de claude-acp sous Korin (/sd0/.claude-acp) : ce qu'il faut à l'agent pour connaître le système.

    python -I tools/claude/home.py <racine de la carte>      (tools/claude/build.sh : build/python/sdroot ; une carte : E:\\)

Pose dans <racine>/.claude-acp :
  AGENTS.md          les consignes sur Korin, dans le prompt système de chaque séance (tools/claude/home/AGENTS.md)
  skills/<nom>/      les compétences : celles du dépôt (.agents/skills : système, langages, kits, POSIX, port,
                     paquets) et celles de Vibe (tools/vibe/home/skills : korin et le guide du développeur, korin-app
                     et son squelette, korin-graphics, korin-diag, korin-csharp, korin-freebasic)
Le reste (.env et la clé, config.toml, sessions, logs) est à l'utilisateur et n'est pas touché ; les compétences
sont remplacées en entier.
"""
import os
import shutil
import sys

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if len(sys.argv) != 2:
    sys.exit(__doc__)
home = os.path.join(sys.argv[1], ".claude-acp")
skills = os.path.join(home, "skills")
shutil.rmtree(skills, ignore_errors=True)
os.makedirs(skills)
shutil.copyfile(os.path.join(root, "tools", "claude", "home", "AGENTS.md"), os.path.join(home, "AGENTS.md"))
names = []
for src in (os.path.join(root, ".agents", "skills"), os.path.join(root, "tools", "vibe", "home", "skills")):
    for name in sorted(os.listdir(src)):
        if os.path.isfile(os.path.join(src, name, "SKILL.md")):
            shutil.copytree(os.path.join(src, name), os.path.join(skills, name))
            names.append(name)
# (comme tools/vibe/home.py) le guide du développeur dans korin, le squelette d'app dans korin-app
api = os.path.join(root, "src", "apps", "help", "data", "api")
ref = os.path.join(skills, "korin", "reference")
os.makedirs(ref, exist_ok=True)
pages = sorted(f for f in os.listdir(api) if f.endswith("_en.md"))
for f in pages:
    shutil.copyfile(os.path.join(api, f), os.path.join(ref, f))
shutil.copytree(os.path.join(api, "skeleton"), os.path.join(skills, "korin-app", "skeleton"))
print("%s : AGENTS.md, %d compétences, %d pages du guide" % (home, len(names), len(pages)))
