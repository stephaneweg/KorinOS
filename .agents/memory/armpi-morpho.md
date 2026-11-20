---
name: armpi-morpho
description: "Morpho (20/11/2026) : animal de compagnie à la Tamagotchi, créature 3D paramétrique façon Spore (originale), C++ sur gk_draw ; banc d'essai hôte tools/morpho ; PC 1.0.0 publié, code dans main ; essayé sous QEMU PC, GPU jamais vu"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-20T17:00:00.000Z
---

Demandé le 20/11 : « un petit jeu, un animal de compagnie à nourrir, soigner, occuper, qui évolue avec le temps, comme un
tamagotchi ; l'animal en 3D (la GUI en 2D), paramétrique comme ceux de Spore ; le jeu tire au hasard les évolutions aux
différents stades, et les couleurs » ; puis « ça doit être soigné ». Branche `claude/epic-ramanujan-iqm8zn`.

`src/apps/morpho` (README.md du dossier) : génome -> créature refaite à chaque image (colonne balayée + pièces posées par
lancer de rayon + IK), éclairage par sommet, `gk_draw` (GPU ou processeur) ; interface aux jetons du thème ; vie en temps
réel avec rattrapage app fermée ; six stades tirés au hasard d'après les soins ; générations.

**Refonte « studio » (20/11, demandée par l'utilisateur : « une app moderne, vue 3D fond foncé, pas de décor, juste
le personnage, les menus autour »)** : direction de Gemini (prompt écrit ici, maquette « Studio Frame ») ; plus de
prairie ni de ciel ; la vue sombre dans les deux thèmes ; au processeur la 3D tombe de ~25 ms à ~4 ms (hôte). Puis, à sa demande :
fenêtre en portrait par défaut (maquette portrait de Gemini : jauges 2 colonnes, soins 3 × 2 ; la mise en page large
quand la fenêtre est élargie) et un dégradé bien visible dans la vue (ardoise bleutée -> noir, vignettage).

**Plans du corps (20/11, demandés)** : serpent (sans pattes, à plat, vague latérale dans ringXf), carapace (dôme à
écailles par sommet, piquants dessus), une à trois têtes (layoutHead + buildFace réutilisés, posées sur les épaules) ;
tirés à l'enfance puis gardés à 70 % ; `shapePlan` (genome.h) fixe leurs contraintes. Onyx Noir forcé pour l'app
(`ui_theme_text`, réappliqué sur WEV_THEME). Essais : `plan=N heads=N stage=N` ; galerie : MORPHO_PLAN, MORPHO_HEADS.

**How to apply:**
- Voir sans Korin : `sh tools/morpho/run.sh gallery|shot`, `tools/morpho/sheet.sh` (FreeType + polices Inter de
  sdcard/fonts, GpuKit du PC compilé pour l'hôte par host_gk.c). C'est ce qui a servi à régler le rendu.
- Pièges vus : `R_OK` est une macro de unistd.h (d'où RS_OK) ; `Rng` mélange la graine (graines voisines = couleurs
  voisines sinon) ; le regard borné en angle (sinon l'iris passe derrière) ; le temps de la vie suit le temps réel,
  l'animation est bornée à 0,1 s par image ; dans QEMU, laisser le pointeur arriver avant `btn:left 1`.
- Intégré : listes de tools/office/build.sh et tools/pc/apps.sh (`-DMORPHO_GL` quand GlKit du PC est construit),
  `[app.morpho]` des deux packages*.ini, icône dans tools/icons/art.py. Pas essayé sur le Pi ni le ZBook.
- **Publié pour le PC le 20/11** (demande de l'utilisateur) : morpho 1.0.0, `pub.py morpho --new --tree build/pc/sysroot`
  (clé : `OnyxPkgKey="$ONYX_PKG_KEY"`), main 0a7a89f, code fusionné dans main par le même push. La table anglaise
  part avec l'app (data/lang-en.txt, ui_tr_load) : le dictionnaire du système est au paquet pc. Le Pi n'est pas publié.
- Cloud : `tools/sys/build_c.sh` sans argument casse sur linuxhost (aarch64, liblinux) : lui passer la liste sans
  linuxhost ni smbd, puis `PC_QUICK=1 sh tools/pc/sysroot.sh`.
