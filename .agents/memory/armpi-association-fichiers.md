---
name: armpi-association-fichiers
description: Association des fichiers (4/11/2026) — ui_assoc_* et ui_open_with dans UIKit, fileassoc.ini, dans main (bb77c09d)
metadata:
  type: project
---

Fait le 4/11/2026 sur la branche claude/sweet-mahavira-916dec (worktree), fusionné dans main le 4/11 (bb77c09d).
UIKit `uikit_assoc.inc` (abi 209) : `ui_assoc_app`, `ui_assoc_open`, `ui_assoc_set`, `ui_open_with`, `ui_arg`.
Ordre : SD0:/etc/fileassoc.ini (« ext = app », écrit par « Toujours ») > `opens` des app.txt > Éditeur.
Branchés : Fichiers (double-clic, menu Fichier et clic droit « Ouvrir avec… »), Archiver, Mail (pièces jointes), lien file: du PDF.
Chemin avec espaces passé entre guillemets : Fichiers, Éditeur, Visionneuse appellent `ui_arg`.

**Why:** demandé : ouvrir un fichier lançait presque toujours l'Éditeur.

**How to apply:** vérifié sous QEMU (docx Letters, xlsx Sheet, png Photos, pdf, svg Vector, txt avec espace, .xyz Éditeur, Ouvrir avec + Toujours). Archiver, Mail, PDF compilés mais pas essayés ; FASMARM natif pas essayé. Piège : uk_strlen/uk_memcpy de UIKit écrasent x1-x3. Pilote QEMU vivant : scratch fa/live.py (commandes ajoutées à un fichier). Voir [[armpi-uikit-fenetres]].
