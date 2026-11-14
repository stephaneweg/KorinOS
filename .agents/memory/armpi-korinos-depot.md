---
name: armpi-korinos-depot
description: "Dépôt public stephaneweg/KorinOS : depuis le 3/11/2026 le dossier principal C:\\Projects\\ARMPI est ce dépôt (origin, main) ; l'historique privé armpi-historique a été supprimé à la demande de l'utilisateur le 4/11"
metadata:
  node_type: memory
  type: project
  originSessionId: 65543494-9bfa-4f4d-875d-14e44f3daaf8
  modified: 2026-11-03T16:09:44.351Z
---

`https://github.com/stephaneweg/KorinOS` (public, voulu par l'utilisateur) : historique neuf, auteur `stephaneweg@users.noreply.github.com`. Site à la racine (Pages, branche main), anglais par défaut, français dans `fr/`.

- Depuis le 3/11 (demande de l'utilisateur) : `C:\Projects\ARMPI` a `origin` = KorinOS et `main` suit `origin/main`. L'ancien `main` local (777 commits, chemins et adresses personnels) était la branche `armpi-historique` ; supprimée le 4/11/2026 à la demande de l'utilisateur (pointe 61d7ca0e, récupérable par le reflog tant que git ne l'a pas nettoyée). Ne jamais pousser de branche ancienne.
- Config du dépôt : `user.email` noreply, `core.autocrlf false` (l'index signé des paquets ne doit pas changer d'un octet).
- `img/` : `korin_lite.img.xz`, `os-list.json` ; `korin_full.img.xz` (ignoré par git) va dans la Release v2026.11.1.
- `packages/` : `tools/pkg/publish.sh` publie maintenant dans ce dépôt lui-même (plus de clone `../KorinOS`) et commite aussi `tools/pkg/versions.ini`.
- Le worktree `claude/github-repo` et `build/KorinOS-site` deviennent inutiles (l'utilisateur supprime le worktree et la branche).

**Why:** l'utilisateur veut travailler directement dans le dépôt public.

**How to apply:** audit avant tout push (aucun chemin de la machine, courriel perso, identifiant Wi-Fi, ROM). Voir [[armpi-nom-korin]], [[armpi-paquets]], [[armpi-setup-disque]].
