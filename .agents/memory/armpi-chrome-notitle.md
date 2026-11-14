---
name: armpi-chrome-notitle
description: "Chrome unique sans barre de titre et deux thèmes (décision du 1/11/2026) ; ce qui reste à corriger"
metadata:
  type: project
---

Décision de l'utilisateur (1/11/2026) : **toutes** les apps sans barre, c'est le seul chrome ; seuls les thèmes Givre et Onyx Noir restent, les autres (et les styles CDE, Milk) sont supprimés. L'idée d'un choix par app (WF_NOTITLE) est abandonnée.

Choix à garder : FRAME_TITLE_H vaut 4 (le nom reste) ; Elegant dessine coins du cadre, contenu coupé (`nt_inset`), surface, puis boutons (`draw_framed`). Seulement les 3 glyphes, sans fond ni contour ; poignée invisible à leur gauche. Déplacement : poignée, haut vide (`UK_DRAG_H`, via ui_w_event), `ui_window_drag` pour les apps sans widgets. `WF_GLYPH_LIGHT`, `WF_BODY_DARK` : émulateurs (EmuKit), Lecteur multimédia, Terminal, Cibles ; Bienvenue : glyphes clairs seuls. Panneaux de gauche : TJ_CREUX (UL_SIDEBAR dans UIKit). Paint ne bouge pas, il sera remplacé.

**Why:** direction de design choisie par l'utilisateur après revue des captures.

**How to apply:** toute app ou tout port laisse `FRAME_CAP_W` libres en haut à droite. Pour capturer toutes les apps : image par app, en remplaçant une ligne de startup.txt dans le fichier image. Après une fusion dans main : `tools/gcc/kits.sh` puis `tools/office/build.sh` pour les apps C++, `tools/webkit/stage.sh` pour Jet et Koton. Reste : screen.inc (screen_work), coins du bas d'une surface vidéo plein cadre, RDP non essayé. Voir [[armpi-panel]], [[armpi-ports-theme]].

Barre de titre virtuelle (1/11/2026, demande de l'utilisateur : dans le kit, jamais dans les apps) : les 32 px du haut déplacent toute fenêtre à cadre dès 8 px de mouvement. Fenêtres avec widgets : `uk_w_event_in` (`uk_held`), champs de texte, curseurs et listes gardent leur comportement. Fenêtres sans widgets : `ui_event` retient l'appui (`uk_ev_held`) et rend au relâchement appui puis relâchement (`uk_ev_next`) ; il rend 0 pendant l'attente. Essayé sous QEMU sur Bienvenue, Calculatrice, Fichiers, puis essayé par l'utilisateur sur le Pi ; dans main.
