---
name: armpi-fichiers-acces-rapides
description: Accès rapides de Fichiers (24/11/2026) — Accueil, Musiques, Images, Téléchargements, Projets avant Applications, glyphes glyph-place-* à la GNOME
metadata:
  type: project
---

Demande de l'utilisateur du 24/11/2026, branche claude/lucid-wright-6o2o4b, fusionnée dans main le 24/11.

- `skel/sd0/etc/places.ini` : Accueil (HOME:/), Musiques, Images, Téléchargements (HOME:/downloads), Projets
  (HOME:/projects), puis Applications, Documents, Vidéos.
- Icônes : `tools/icons/art.py` (table PLACES) rend `glyph-place-{home,music,pictures,downloads,projects,documents,
  videos,apps,folder}` (projects : équerre et crayon parallèle à l'hypoténuse, demandé le 24/11 ; au trait, comme les icônes symboliques de Nautilus) ; `src/apps/files/main.c` `place_glyph`
  choisit d'après le dossier et dessine par `ui_glyph` (20 px, jeton TEXT2) ; un dossier épinglé quelconque a
  `glyph-place-folder`.
- Fichiers crée au démarrage les dossiers types manquants de HOME:/ (`std_dirs_make` : music, pictures, downloads,
  projects, documents, video), comme xdg-user-dirs : sinon un accès rapide vers un dossier absent n'est pas montré
  (le home/ du PC est vide).
- `PL_MAX` 16, `ID_PLACE` passé de 20 à 100 (20 + 10 touchait ID_EJECT 30).
- Piège : un HOME:/etc/places.ini personnel (créé dès qu'on épingle ou retire un accès) masque celui de /etc : il
  garde l'ancienne liste. Le supprimer (ou l'éditer) pour voir la nouvelle.
- Style Adwaita (24/11, demande de l'utilisateur) : glyph-place-* refaits sur la grille de 16 d'Adwaita, trait de 2
  (fonction adw d'art.py) ; dossiers de la vue = masques glyph-folder-back (onglet aligné à gauche) et
  glyph-folder-front (la face redescend à gauche sous l'onglet, courbe en S), coloriés d'après TJ_ACCENT (tint :
  fond 31/256 vers le blanc, épaisseur 77, face 148) ; dans le dossier personnel (HOME:/ ou $HOME), les dossiers types
  portent leur glyphe en emblème (16 px) dans la vue en icônes, pas dans la liste. Plus de COL_FOLDER (ambre) en dur.
- Publié : pc 2026.11.105 (pub.py depuis le cloud, sur l'archive .104 extraite + les fichiers de Fichiers ; essayé
  sous QEMU PC avec cet arbre comme disque d'essai : mkkorindisk --sysroot <arbre sans EFI/ ni korin/> --esp <ces
  deux-là> --loader <esp>/EFI/Korin/korin.efi, ligne « /apps/files.app/main HOME:/ » ajoutée à etc/session de l'essai).
- 24/11 suite : recherche en tapant (type_ahead, 1 s entre les touches, même lettre répétée = suivant) ; champ
  d'emplacement (ui_textbox loc_w par-dessus le fil d'Ariane : Ctrl+L, clic dans le fil hors d'un segment, « / » et
  « ~ » tapés ; Tab complète, Entrée, Échap ; ~ = HOME:, relatif, . et ..) ; Ctrl+L n'actualise plus (F5, Ctrl+R),
  comme GNOME. vol_of : HOME: rattaché au volume de $HOME (avant : « Volume retiré : retour à / » à chaque changement
  des volumes quand on était dans HOME:). Essayé sous QEMU PC (captures t1..t8).
- 24/11 : Récents (Favoris refusés par l'utilisateur : l'épinglage suffit). UIKit uk_recent_add (uk_assoc.c, sans
  changer l'ABI) tient HOME:/etc/recent-files.txt (50, le plus récent d'abord) depuis ui_assoc_open (documents) et
  ui_file_open/ui_file_save ; Fichiers : dossier virtuel « recent: » (places.ini « Récents = recent: »), noms = chemins
  complets (ent_shown, make_full), pas de tri, Nouveau dossier/Coller/Renommer/Déposer refusés, Suppr = retirer de la
  liste ; clic droit Ouvrir l'emplacement, Retirer des récents ; glyph-place-recent (horloge).
  **Piège du PC** : le noyau prend un kit dans korin/initrd.tar avant lib/, et à version de kit égale initrd_kits.py
  garde celui de l'initrd : une modification d'UIKit sans changement d'ABI doit remplacer uikit.so DANS l'initrd
  aussi (scratchpad initrd_put.py : ordre, noms, dates gardés ; pub.py --boot DOSSIER avec korin/initrd.tar).
- 24/11 : accents de UIKit (uk_theme.c, uikit_theme.inc, applet Thème asm) choisis par l'utilisateur : vert
  0x2E8B3E (Givre) / 0x57B65F (Onyx Noir), « Vert vif » renommé « Vert » ; jaune 0xE69A0F / 0xFFB52E (le « J3 » :
  l'utilisateur l'a préféré aux jaunes moins saturés de même teinte). Fichiers redessine sur WEV_THEME.
- 24/11 : ↑ ne remonte plus au-dessus d'une racine nommée (vol_root : « HOME: », « HOME:/ » ; path_parent rend
  « HOME:/ » et non « H ») ; panneau : volumes amovibles et du réseau seulement (vol_shown), Affichage > Tous les
  volumes (files.ini « volumes = 1 ») ; Affichage > Fichiers cachés, Ctrl+H (files.ini « caches = 1 ») : « . » ou
  attribut FAT 0x02 par korin_readdir_attr (extension de libposix, sys/dirent.h ; libposix statique : relier
  l'app) ; volumes du réseau nommés (vol_title : « Google Drive », « partage sur hôte », clé TR avec une espace
  insécable car « %s sur %s » = « %s of %s » existe), glyphe réseau, chemin en 2e ligne.
- Publié : pc 2026.11.107 (24/11 soir, main b591842) : Récents, accents, volumes, fichiers cachés, ↑ ; UIKit dans lib/ et
  dans korin/initrd.tar ; emporte aussi libposix /proc/self/exe (341c51b, autre session) par Fichiers et Réglages.
- 24/11 soir : alias fixes de vfs PUBLIC:/ (/home/public) et APPS:/ (/apps) (alias_fixes, src/svc/vfs/path.c ;
  statvfs de libposix ; vol_of de Fichiers) ; places.ini « Applications = APPS:/ », « Public = PUBLIC:/ » ; glyphe
  glyph-place-public (le partage de GNOME) ; vfs est dans korin/initrd.tar seulement (pas sys/svc) : le remplacer là.
  **But de l'utilisateur** : à terme, un compte n'atteint /, /tmp et /home que par élévation, ses alias restant ouverts
  (« une sorte d'unveil au niveau user ») ; rien de fait.
- 24/11 soir : Tous les volumes = « / » seul, nommé « Système » (glyph-place-drive, disque d'Adwaita) ; le reste est dans
  son arborescence (demande de l'utilisateur). Lanceur de la vue d'ensemble (src/sys/panel/launcher.c) : la_pill_under
  généralisé en la_box_under (rayon quelconque ; à r = h/2, octet pour octet l'ancienne pilule, vérifié sur l'hôte) ;
  les cases de la grille et les flèches de page tracent l'icône et le texte puis glissent le voile dessous (le texte
  se frangeait de sombre). La vue d'ensemble ne s'ouvre pas sous QEMU (clic sur « Korin OS », Super), même avec le pc
  publié : pas vu en vrai.
- Publié : pc 2026.11.109 (24/11 soir, sur la .108 d'une autre session) : alias PUBLIC:/ APPS:/ (vfs dans l'initrd),
  accès rapide Public, Système, lanceur (sys/panel) ; UIKit inchangé depuis la .107 (vérifié octet pour octet).
- 25/11 : korin.apps.changed (pkglib refresh_desktop : pkg, pkg ui-job de Logiciels, pkgd ; busd rend le nombre
  d'abonnés prévenus : 0 ou pas de busd -> relance comme avant) ; panel, panel classique, menubar relisent le
  catalogue (apprun.c apps_reload : pids recasés par nom). Essayé sous QEMU PC (bureau classique, app factice
  « demo » dans var/pkg/db, pkg remove demo). Pas publié. Construire pkg du PC : ARCH=x86_64 sh tools/openssl/build.sh
  (pas tools/pc/thirdparty.sh, qui n'existe pas), puis tools/pc/apps.sh pkg pkgd pkgman.
- 25/11 : la vraie cause du texte frangé (grille, pilules, toute fenêtre WF_ALPHA) : ttf_blend / ttf_blend8 d'UIKit
  mélangeaient le bord des glyphes avec la couleur du fond SANS sa transparence (noir du vide -> pixel gris opaque).
  ttf_mix_alpha (uk_ttf.c) compose sur un fond en partie transparent ; fond opaque : calcul inchangé. Vérifié sous
  QEMU (pixels de « Démo » et « Toutes »). La vue d'ensemble s'ouvre sous QEMU s'il y a une fenêtre (bureau vide :
  rien) : clic sur « Korin OS » (50,15), grille par (765,750). korin.apps.changed essayé aussi dans la grille.
- 25/11 : le guet (VFS_WATCH 58, src/svc/vfs/watch.c, korinrt.h et fs.inc) : 32 guets, rang par programme, droit de
  lire vérifié, touché par ops_chemins (écritures) et chemin_renommer (deux dossiers), avis korin.fs.watch.<pid>
  (rangs) à 200 ms, envoyé par watch_flush dans servir (le délai de SVC_NEXT suit le prochain dû), rendu à
  SVC_OP_END. Fichiers : watch_set dans load_dir (rang = vue), watch_reload (sélection et défilement gardés), seconde
  relecture à 1,5 s. Essayé sous QEMU PC par un script de session (/bin/sh /etc/watchtest.sh : mkdir, cp, mv, mv dans
  un dossier, rm, rmdir ; une capture toutes les 3 s) : tout suivi. Les fenêtres ne s'ouvrent pas toujours au même
  endroit sous QEMU : éviter les clics pour les essais en plusieurs fenêtres. Pas publié (vfs dans l'initrd).
- Le Pi (armpi) n'est pas publié : son paquet a encore files.asm.
