---
name: armpi-renommage-francais
description: "Noms français du code (f_ouvrir, V_OUVRIR, ui_jetons…) renommés en anglais au fur et à mesure du portage de chaque kit ou service"
metadata:
  node_type: memory
  type: project
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T00:35:33.876Z
---

Le 12/11/2026, l'utilisateur a demandé de corriger les noms en français : fonctions des kits (f_ouvrir, f_lire, fichier_sys, ui_theme_texte…), ops de vfs (V_OUVRIR, FS_LIRE…) et noms internes.
Il a d'abord accepté de tout renommer d'un coup, puis il a choisi le plus sûr : **renommer au fur et à mesure qu'on porte chaque kit (ou service) en C**.

**Pourquoi :** un renommage global d'environ 1 600 identifiants est risqué ; celui d'un kit se fait pendant que son port est vérifié.

**Comment appliquer :**
- Seuls les noms de fonctions et ce qui est public (exports .abi, constantes partagées, ops de protocole) sont renommés ; les variables ne comptent pas (« osef », 12/11).
- On renomme aussi ses noms exportés (.abi) et les constantes partagées de son domaine, dans tout le dépôt, avec `tools/rename_ids.py CARTE.txt [--apply]`. L'outil ne touche ni aux commentaires ni aux chaînes ; dans les .md, seulement le code entre accents graves.
- Pour les programmes en asm, on vérifie en comparant les binaires avant et après.
- Renommer une ligne d'un .abi ne change pas l'ABI (appel par rang) : la règle « ne jamais renommer » d'AGENTS.md est à préciser.

Voir [[armpi-noms-anglais]], [[armpi-x86-pc]].
