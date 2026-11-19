---
name: armpi-cao
description: CAO native (successeur de 3DForge) : OCCT 8.0.1 + PlaneGCS portés (tools/cao), caotest passe sous QEMU PC le 14/11, paquets PC occt-dev/planegcs-dev/caotest publiés le 15/11 ; maquettes validées le 15/11, app pas écrite
metadata:
  type: project
---

Chantier lancé le 14/11/2026 (idée : [[armpi-idee-cao]]) ; code poussé sur `claude/x86-64-pc` le 15/11 à la demande de
l'utilisateur (« le code dans la branche x86-64-pc, les paquets dans main »). Document : `docs/DESIGN-CAO.md` (§1-14 étude, §15 moteur).

**Moteur, fait et essayé sous QEMU PC seulement** (pas sur le ZBook ni le Pi) :
- `tools/cao/` : env.sh (OCCT 8.0.1, Eigen 5.0.1, FreeCAD 1.1.4 ; SHA-256 fixés), fetch.sh, korin.cmake (chaîne CMake
  sur libposix), build-occt.sh, build-planegcs.sh, build-test.sh, occt-korin.patch (une ligne : OSD_Path connaît
  `__armpi__`), planegcs/compat (Boost remplacé par un union-find, Base::Console par fprintf), test/caotest.cpp,
  pctest.py (reprend tools/python/pctest.py). Tout est construit sous `~/korin-cao/$ARCH` (WSL), rien de lourd dans le dépôt.
- Rien n'a manqué à libposix. OCCT : 30 .a statiques (268 Mo), STEP/IGES/STL ; TKXCAF tire TKV3d/TKService (sans GL ni FreeType).
- caotest (esquisse PlaneGCS 0 DDL + conflit désigné, boîte, congé droit, union, congé sur arête elliptique, retrait des
  trous, STEP écrit et relu, IGES, STL) : PASSE sous QEMU (TCG, sans KVM) en 6 s ; 119 ms natif Linux sur un i7-11800H.
- aarch64 (Pi) : tout se construit avec ARCH=aarch64, sans changement (OCCT 54 min en -j6) ; **pas lancé** ni sous QEMU
  raspi4b ni sur le Pi ; pas de paquets Pi.
- **Paquets PC publiés le 15/11** (main, packages/pc, 101 paquets) : occt-dev (+ occt-dev-data-1..4 dans la Release,
  pc-occt-dev-*.opk), planegcs-dev, caotest, tous 1.0.0 ; tools/pc/sysroot_cao.sh (après sysroot_gcc.sh) ; publication
  partielle par mkrepo --keep-unbuilt sur un arbre de la seule CAO (build/pc/cao-pkgroot), avec un lanceur qui ne garde
  que ces paquets (sinon [pc] ramasse etc/pkg/abi que mkrepo écrit dans l'arbre). Pas de lib<nom> : statique seulement.

**Étude de design** faite avec un agent designer (demande de l'utilisateur : « l'interface la plus professionnelle
possible ») ; maquettes (13 écrans, Givre/Onyx Noir) : https://claude.ai/artifact/Mudrvp7z3EWPnHAvogwFZc ; générateur
Python resté hors du dépôt (scratchpad de la session). **Maquettes validées par l'utilisateur le 15/11** (« elles me conviennent ») ; l'app n'est pas écrite.
**Décisions du 15/11 (§13, toutes les recommandations retenues)** : nom **Forge** (forge.app, .forge) ; barre à
groupes + réglage « noms des outils » ; souris droit = tourner, milieu = déplacer, molette vers le pointeur (préréglages
Fusion/SolidWorks/3DForge) ; pas de menu radial en v1 ; touches de Fusion, S = esquisse, Ctrl+K = recherche ;
fabrication : fraisage GRBL d'abord, puis résine, puis filament.

**Pièges :** OCCT en -j12 a fait tomber WSL (7,8 Go) → env.sh plafonne à 4 (WSL partagé, setsid nohup pour les longues constructions) ; `-DCMAKE_CXX_FLAGS` écrase les options du
fichier de chaîne (passer par `CMAKE_CXX_FLAGS_RELEASE`) ; l'OVMF d'Ubuntu n'a pas le shell UEFI : `sh tools/cloud/setup.sh qemu`
(edk2 de QEMU) puis `. ~/korin-cloud/env.sh` avant pctest ; `wsl sh -c '…$x…'` perd les `$` (passer par un fichier,
`wsl -e sh fichier`, et `MSYS_NO_PATHCONV=1` dans Git Bash).

**Why:** l'utilisateur veut une CAO libre native, aussi facile que Fusion/Shapr3D, qui dépasse les limites de 3DForge.

**Étape 0 faite le 15/11** (§15.7) : `WS_OP_SURFACE_OVER` (38) dans Elegant C, `ui_surface_overlay` (UIKit rang 215),
`tools/mesa/armgl/glview.h` (vue GL qui suit la fenêtre, pas essayée sur GPU), jetons TJ_HOVER/TJ_AXIS_X/Y/Z (TJ_SIZE 120) ;
`overlaytest` PASS sous QEMU PC ; pas sur le ZBook ni le Pi, paquets pas republiés.

**Étape 1 faite le 15/11** (§15.8) : app `src/apps/forge` (model.cpp, render.cpp, main.cpp), vue rendue par le
processeur (pas encore GlKit : pas reconstructible ici), équerre d'exemple 63,99 cm³ ; `forge --selftest` PASS sous QEMU
PC, gestes réels par tablette USB (survol, clic, rotation, cube) ; piège : un ui_view perd la transparence → pas de
widget sous la vue, souris brute. Pas sur le ZBook ni le Pi, pas de paquet.

**Étape 2 faite le 15/11** (§15.9) : history.cpp (opérations rejouées, références par signature à 3 mm), boîte et
cylindre au geste, pousser/tirer, congé/chanfrein (aperçu réel, refus), union/soustraction, puces de cote dans l'app
(formules, unités), frise modifiable (barre glissée), annuler/rétablir ; selftest 9/9 et gestes réels sous QEMU.
Piège d'essai : un geste de 30 px vaut 10 mm à l'échelle de départ (la valeur par défaut) : prendre un geste ample.

**Publié le 15/11 (main)** : forge 1.0.0 (abi >= 598) et pc 2026.11.7 = archive 11.6 avec seulement uikit.so (initrd + lib),
sys/elegant, bin/overlaytest remplacés (publication partielle : arbre tiré de l'archive publiée, mkrepo --keep-unbuilt
--bump, lanceur WANT="pc forge") ; l'utilisateur veut que le code soit poussé sur claude/x86-64-pc à chaque publication.
Toujours repartir de la DERNIÈRE archive pc publiée (11.9 le 15/11 : glkit.so ; avant 11.8 : intelgpu, glkit.so de la session Godot), sinon un
correctif d'une autre session disparaît ; forge 1.1.0 publié seul le 15/11.

**Étape 3 faite le 15/11** (§15.10) : sketch.cpp (PlaneGCS : DDL, conflits, éléments libres ; contours -> faces), mode
esquisse (L R C A D, accrochage, H/V déduites, cotes tapées, contraintes sur la sélection, refus d'un conflit ou d'un
élément écrasé), extrusion (E) et révolution ; selftest 17/17 et vraies touches/clics sous QEMU. Piège : H sur une
verticale n'est pas un conflit pour PlaneGCS (il écrase la ligne) : refusé à part. Essais QEMU : le selftest dure ~40 s,
les gestes QMP vers 100 s ; qmp_steps (scratchpad) sait key:<qcode> (clavier US, le noyau décode en AZERTY : pas de chiffres).

**Étape 4 faite le 15/11** (§15.11) : chaîne tangente (double-clic), R max proposé (dichotomie), perçage (H), coque,
réseau linéaire/circulaire, symétrie, paramètres nommés (panneau du document ; formules gardées dans Feature::expr,
rejouées). Retours de l'utilisateur sur sa machine (15/11) traités : clavier aux puces dès le début d'une commande
(Tab : la suivante, le clic prend ce qui est tapé), rotation 0,25°/px et une image par lot d'événements, scintillement
(Elegant compose depuis le tampon de la surface à tout moment : rendre à part puis recopier ; ne pas repasser par le
transparent les zones par-dessus restées en place), catégorie Création. Puis (1.2.1) : l'aperçu suit la frappe, une image
par lot de mouvements pendant un geste aussi, accrochage 1/5/10 mm (5 par défaut, Affichage). Piège d'essai : pctest prend forge dans
build/pc/sysroot (recopier depuis build/pc/apps/sdroot) ; replay ne remplit pas Feature::body d'un corps neuf (bodyOf).

**MCP fait le 16/11** (§16, forge 1.3.0) : serveur Streamable HTTP dans Forge (mcp.cpp, mcptools.inc, 29 outils,
port 7311, jeton dans SD0:/home/forge/mcp.txt, Fichier > Piloter par un agent ou forge --mcp), client MCP dans
claude-acp (claude 1.0.1), compétence korin-forge livrée par le paquet forge pour .vibe et .claude-acp (décision :
pas de 3e paquet). Essai : scratchpad mcp_client.py contre QEMU (--hostfwd tcp:127.0.0.1:7311-:7311), 28/28.
Pièges : un perçage doit suivre sa face (faces[0]) ; insérer/supprimer une étape recale les ref (agent.h).
Pas essayé avec le vrai Claude Code ni sur le ZBook.

**How to apply:** l'étape 5 du §14 (STEP/IGES/STL/3MF, palette Ctrl+K, 800 × 600), puis la fabrication.
Voir [[armpi-x86-pc]], [[armpi-idee-libs-gcc]], [[armpi-ports-uikit-os]], [[armpi-ports-theme]].

**18/11, fabrication précisée par l'utilisateur** (DESIGN-CAO.md §14 bis) : trois procédés, CNC, SLA, FDM ; chacun
calcule un **état intermédiaire** indépendant de la machine (trajets d'outil, pile de couches, chemins de buse), puis
un **module d'export par machine** (.so, dlopen) écrit le fichier : Two Trees **6040** en GRBL, Photon Mono 2 en
.pm3n, Kobra X en G-code (fichier d'exemple attendu). Comme 3DForge d'Onyx (fcam.h, fprint.h, ffdm.h). La fraiseuse est la
Two Trees TTC 6040 : zone de travail 600 × 400 × 100 mm (confirmé le 18/11 ; la maquette g1 disait TTC 450).

**18/11, CNC deux faces** : plusieurs mises en position ; chacune : origine parmi 27 points, X machine parmi ±X/±Y du projet, Z machine +Z ou −Z, Y déduit (Y = Z × X, jamais de miroir) ; exemple du violon (setup 1 X=+Y Z=+Z → Y=−X ; setup 2 X=+Y Z=−Z → Y=+X) ; l'origine suit le retournement (27 points lus dans le repère de la machine, donc de l'autre côté du projet au setup 2) ; origine par défaut : milieu de la face du dessus du brut (habitude de l'utilisateur) ; pas de pions dans Forge (l'utilisateur les pose lui-même avant l'usinage, sur l'axe médian en X). DESIGN-CAO.md §14 bis.

**19/11, forge 1.3.5 (pas publié)** : demandes de l'utilisateur faites et essayées sous QEMU PC (autotest 28/28, captures) :
Combiner (une seule opération comme Combine de Fusion : boutons Cible / Outils puis clics dans la vue, Échap annule le
choix, Union / Soustraction / Intersection, « Conserver les outils », Confirmer ; ST_COMBINE, Feature::tools et keep ;
ST_CUT gardé pour le rejeu ; MCP boolean : tool_bodies, keep_tools, intersect) ; boîte coin à coin, la hauteur suit
l'arête du coin opposé ; frise en icônes dessinées dans l'app (iconLines), nom et résumé en infobulle, clic droit
Modifier… / Supprimer / Supprimer à partir d'ici ; suppression avec dépendances (agent.cpp planRemoval/applyRemoval :
une forme jointe à un corps disparu devient un nouveau corps) ; Fichier Nouveau / Ouvrir / Enregistrer (sous), format
.forge = l'historique en JSON (docfile.cpp), rejoué à l'ouverture. Pas essayé sur le ZBook ni le Pi.
