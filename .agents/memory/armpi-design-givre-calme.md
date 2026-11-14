---
name: armpi-design-givre-calme
description: "Direction Givre calme (2/11/2026) : revue de l'interface, ce qui est fait sur claude/os-design-review-eae628, ce qui reste"
metadata:
  node_type: memory
  type: project
  originSessionId: aedd59b1-2a71-4cec-9d99-84c2b213828e
  modified: 2026-11-02T20:00:38.112Z
---

Revue dans `docs/DESIGN-PROPOSAL.md`, page https://claude.ai/artifact/HDhNiHDyeA5xKTXtRKryv6. L'utilisateur a demandé de la mettre en œuvre le 2/11/2026 ; direction A recommandée, retenue par défaut, pas confirmée explicitement.

Dans main depuis le 2/11/2026 (fusion demandée par l'utilisateur), QEMU seulement. Les applets Rendez-vous et Notes restent translucides et visibles vides : il a seulement demandé qu'elles descendent sous la barre et ne se déplacent plus, ne pas les transformer. Fait : jetons, fond ton sur ton, Inter + repli DejaVu (`F_FALLBACK`), coins anticrénelés de `ui_rbox` (`uikit_rmask.inc`, `tools/kits/rmask.py`), boutons plats, cadre 12 px (`FRAME_GAP`, `FRAME_HIT`), NEON dans `bb_blit_any`, barre et panel opaques, sept apps sur la macro `jeton`, panel 48 px.

**Why:** les icônes de `tools/icons` restent telles quelles, la couleur vit là, la coquille est neutre ; la police d'icônes a été abandonnée au profit de `ui_glyph`.

Depuis : glyphes d'outils (`uk_tool_map` dans UIKit, les apps ne changent pas), métriques 32/30 (les `ctl_h` en dur des dix apps C++ suivent, à garder alignés avec `uk_mt_tab`), lanceur à rail, Réglages en lignes, Fichiers à fil d'Ariane, applets du bureau à place fixe. Elegant met le fond opaque sous les applets : sans ça, une applet qui démarre avant le fond disparaît.

Le français des apps portées passe par `res/lang/fr.txt` (clés anglaises) : UIKit traduit menus, infobulles, boutons, étiquettes. Sous lang absent, fr.txt est chargé par chaque programme au premier `ui_tr`.

Onyx Remote : WS_OP_WIN_READ partie 0 pose les boutons du cadre sur le contenu (le client peint le cadre dessous), partie 3 = contenu seul. Avant tout déploiement FTP d'`elegant`, comparer le CRC du Pi aux builds : le 2/11 il portait la branche Mesa, pas `main`. Le FTP n'a pas RNFR : envoyer en `.new`, XCRC, puis `cp` par telnet.

**How to apply:** ajouter un texte anglais à fr.txt plutôt que toucher à l'app ; une traduction trop longue tronque un bouton de largeur fixe, à regarder. Reste le détail dans DESIGN-PROPOSAL.md. À essayer sur le Pi : lisibilité d'Inter, gain NEON (F11), RDP. Inter vient d'une installation locale : remplacer par le fichier officiel. Patcher une image privée est le moyen rapide d'essayer (`docs/design-proposal/outils`). Voir [[armpi-chrome-notitle]], [[armpi-panel]], [[armpi-ports-theme]].

Le 3/11 l'utilisateur a demandé « un poil plus grand » : texte 14 px et petit texte 13 (uk_mt_tab grande et moyenne, petite 13/12), barre de menus MB_PX 14, lanceur 14/13, fil d'Ariane de Fichiers 14. Il garde le lanceur à rail (catégories à gauche). Il reste des tailles 13 en dur ailleurs (popups de UIKit, boutons et libellés de Réglages).
