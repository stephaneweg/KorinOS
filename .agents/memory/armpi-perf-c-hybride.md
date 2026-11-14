---
name: armpi-perf-c-hybride
description: Vitesse du code passé en C : d'abord les réglages du compilateur ; si l'écart reste mauvais à l'usage, parties critiques en asm par cible (ARM et x86-64), comme le noyau
metadata:
  type: feedback
---
Le 12/11/2026, à propos d'AudioKit (MP3 environ 20 % plus lent en C qu'en asm sous QEMU), l'utilisateur a dit : si c'est un paramètre d'optimisation du compilateur, ça vaut la peine de tenter. Si l'écart reste trop mauvais à l'usage, il envisagera une **approche hybride comme pour le noyau** : les parties critiques en asm, selon la cible (ARM ou x86-64).

Ce qui compte le plus pour lui : un démarrage **rapide** et un système **réactif**, comme sur le Pi.

**Why:** il aime Korin pour sa vitesse ; le C ne doit pas la lui faire perdre.

**How to apply:**
- Pour un code passé en C qui ralentit, essayer d'abord les réglages du compilateur : fonctions intégrées plutôt qu'asm en ligne opaque, -O3, vectorisation, fonctions NEON ou AVX.
- Garder l'exactitude au bit près.
- Mesurer sur le vrai matériel, car QEMU mesure mal les vitesses.
- Ne proposer de l'asm par cible que si l'écart reste mauvais.
- Comparer le démarrage et la réactivité à chaque grande étape.
- Sur PC, AVX et FMA sont à activer dans le noyau (12/11).

Voir [[armpi-chantier-perf]], [[armpi-x86-pc]].
