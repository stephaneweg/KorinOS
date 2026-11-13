# Réglages (control) : le port en C++

Le panneau de configuration de Korin, passé de l'assembleur (`control.asm`, `applet.inc`, `applets/*.inc`, qui
restent dans le dossier jusqu'à la validation sur le Pi) au C++, un seul source pour le Pi et le PC.

## Ce qui vient d'où

* **Le cadre** (`main.cpp`, `applet.cpp`, `applist.cpp`, `control.h`) vient de l'asm de Korin : une seule fenêtre,
  la liste des volets à gauche, le volet à droite. Le `control` d'Onyx (`user/Apps/control/main.cpp`) lançait chaque
  volet comme une app à part dans une surface partagée ; Korin les réunit, et c'est ce que l'utilisateur a validé.
* **Les volets** (`applets/<nom>.cpp`) suivent chacun leur `.inc` ; leur en-tête dit d'où ils viennent. Les volets
  d'Onyx (`theme`, `displayconf`, `keyconf`, `langconf`…) ont été refaits par Korin (deux thèmes et un accent,
  modes de l'EDID, formats `keyb=`, `lang=`, `fuseau=`…) : l'asm fait foi, Onyx n'a donné que des principes
  (l'aperçu du thème, « Garder cette résolution ? » en 15 s).

## Volets Thème, Région, Écran, .NET

* `theme.cpp` (theme.inc) : thème et accent, aperçu, korin.theme publié (thème à chaud), sans busd la session
  relancée. Définit `theme_topic` et `theme_bus`, partagés avec Écran.
* `keyboard.cpp` (keyboard.inc) : disposition (service `hid`), langue, pays (`wifi_country`), fuseau (région, puis
  ville ; `SD0:/res/tz.txt`, NTP).
* `display.cpp` (display.inc) : résolution à chaud (Elegant, `SYS_WS_CALL`), dialogue de confirmation, section
  Interface (Bureau ou Poche, taille des contrôles, « Appliquer et relancer » par init2). Définit
  `display_density_file`.
* `dotnet.cpp` (dotnet.inc) : les DLL de `SD0:/dotnet`, `SD0:/etc/dllpreload.txt`, `dllpreload` lancé.

Différences avec l'asm :

* Région : la liste des dispositions montre le nom lisible (« Français », « Belge ») comme le dit l'en-tête de
  l'asm ; l'asm montrait le nom du fichier (« FR »), son `cl_libelle` perdant le nom dans l'appel de `str_eq`.
* Région : un libellé de ville par fuseau (l'asm n'avait qu'un tampon, la liste le copie).
* .NET : `dllpreload` est lancé sans argument (l'asm passait une chaîne vide).
* Gardé comme l'asm : la zone de clic des pastilles d'accent commence 8 px sous leur dessin ; les textes du
  dialogue de résolution et de « Normale|Tactile » passent tels quels (UIKit traduit le dialogue).

Vérifié sous QEMU raspi4b (`tools/apps/compare.sh`, TAG `_ctlA`) contre l'asm, en Givre et Onyx Noir : les quatre
volets, l'accent, l'aperçu sombre, le thème appliqué à chaud, les listes de Région, Appliquer, le changement de
résolution et son retour, les cartes Poche et Tactile, les cases .NET et Appliquer : captures identiques au pixel
(hors fond d'écran, heure et barre du compte à rebours), sauf la liste des dispositions (ci-dessus). « Appliquer et
relancer » (redémarrage) n'a pas été essayé.
