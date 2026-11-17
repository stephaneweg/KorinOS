---
name: armpi-fonds-ecran
description: "Fonds d'écran d'Onyx repris le 17/11/2026 : peintre commun src/sys/common/wallpaper.c, wallpaper.ini personnel, volet Thème, korin.wallpaper"
metadata:
  type: project
---

Demande de l'utilisateur (17/11/2026) : reprendre d'Onyx le choix des fonds (procéduraux ou image, avec une teinte) et
l'ajouter au choix du thème. Fait sur `claude/x86-64-pc` : `src/sys/common/wallpaper.{h,c}` (cellules = l'ancien Voronoï
par défaut, dégradé, bulles, uni, motif de `/res/wallpapers`, image ; teinte aucune / accent / un accent de UIKit),
compilé dans voronoy (C, Pi et PC) et inclus par Réglages (`applets/wallpaper.cpp`, aperçu peint par le même code).
Réglage personnel `wallpaper.ini` (HOME:/etc) ; image : Réglages écrit une copie PNG à la taille de l'écran,
`HOME:/etc/wallpaper.png` (UIKit ne lit que BMP/PNG ; décodeurs de Paint dans `applets/wpimage.cpp`). À chaud :
`korin.theme` si le thème change, sinon `korin.wallpaper` (voronoy abonné). Doc : DESIGN-ELEGANT.md §5, BUS.md.

Sources d'Onyx : le dossier `C:\Projects\Onyx` n'existe plus ; recloné (--depth 1, lecture seule) dans le scratchpad
depuis https://github.com/stephaneweg/onyx (accord du 13/11). Motifs et `tools/wallpapers/gen_wallpapers.py` en viennent.

**Why:** l'utilisateur veut le choix d'Onyx, mais au style Givre calme et en suivant le thème (pas de couleur en dur).
**How to apply:** vérifié sous QEMU Pi (WSL) et PC (`qemu_nvme.py --boot-disk`, arbre d'essai réduit : la partition
KORIN OS de mkkorindisk fait 65 Mo, l'OVMF d'Ubuntu n'a pas de shell → `EFI/BOOT/BOOTX64.EFI` ajouté dans l'arbre
d'essai) ; pas encore sur le Pi ni le ZBook. `voronoy.asm` n'est plus copié (le C le remplace) : il garde l'ancien
fond seul. Voir [[armpi-design-givre-calme]], [[armpi-ports-theme]], [[armpi-busd]].
