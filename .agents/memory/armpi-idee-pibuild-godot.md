---
name: armpi-idee-pibuild-godot
description: "Idée pour plus tard (16/11/2026) : pibuild reconnaît un projet Godot (project.godot), l'exporte et installe le jeu dans /apps"
metadata:
  type: project
---

Idée de l'utilisateur, notée le 16/11/2026, **pour plus tard** : `pibuild` (et la fenêtre PiBuild) reconnaît un
dossier de projet Godot, comme il reconnaît déjà un `.csproj`, des `.cs`, des `.bas`, du C ou de l'assembleur
(docs/GCC.md), construit le jeu et l'installe directement dans `/apps/<nom>.app`.

**Why:** faire un jeu Godot dans Korin et l'avoir dans le lanceur en une commande, comme les autres langages.

**How to apply:** à proposer quand on revient sur pibuild ou Godot. Pistes :
- L'utilisateur a ajouté « ou alors make » : `pibuild` ne fait que lancer `make -j4` avec
  `/devtools/gcc/share/pibuild.mk` (tools/gcc/pibuild.mk), donc la reconnaissance va dans ce Makefile générique, et
  `make` seul (ou un Makefile du projet) marche aussi.
- Reconnaître le dossier par `project.godot`.
- Lancer l'export sans écran : `godot --headless --path <dossier> --export-release "<préréglage>" <sortie>`, avec le
  greffon `tools/godot/addons/korin_export` (il fait déjà `<id>.app` : main, app.txt, icon.rgba) et le modèle
  template_release du paquet `godot-templates`. Si le projet n'a pas de préréglage Korin dans
  `export_presets.cfg`, pibuild en ajoute un (préréglage Linux x86_64 + modèle personnalisé), ou le greffon le fait.
- Puis copier le `.app` dans `/apps` (le binaire exporté sur FAT demandait `chmod +x`, voir [[armpi-godot]]).
- Godot n'existe que sur Korin PC pour l'instant (paquets godot, godot-templates) : sur le Pi, pibuild dit que
  Godot manque. Rendu Compatibility conseillé pour les jeux qui doivent aussi tourner sur le Pi plus tard.
Voir [[armpi-godot]], [[armpi-idee-godot]], [[armpi-gcc-sur-armpi]], [[armpi-idee-emerge]].
