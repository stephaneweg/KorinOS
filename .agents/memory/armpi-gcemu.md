---
name: armpi-gcemu
description: "GameCube (gcemu) dans main : coeur C++ d'Onyx, TEV GLSL ES sur Mesa, gx_asm.asm ; tourne sur le Pi par le GPU ; prochain chantier : optimisation, son saccade ; gamelib corrige pas encore sur le Pi"
metadata:
  node_type: memory
  type: project
  originSessionId: 46487943-d293-4f30-9193-6216e1635737
  modified: 2026-11-03T00:13:16.121Z
---

Port dans `src/apps/gcemu`, doc docs/EMULATEURS.md (section GameCube : architecture, preuves, mesures). Dans main le 3/11/2026 (331b20d) ; branche et worktree claude/gc-emu-419fd4 supprimés. Pas de dépôt distant.

**Prochain chantier, demandé par l'utilisateur (3/11) : l'optimisation, d'abord le son saccadé.** Il l'a entendu sur le Pi ; cause non établie, à mesurer. Pistes :
- Onyx étire le son quand la machine est en retard (`EcStretch`, `ec_stretch_for`, `ec_stretch_pop` dans user/Apps/gcemu/main.cpp et user/Emulators/emucore.h) en gardant la hauteur ; **ce n'est pas porté** : gcemu passe par la cadence simple d'EmuKit. Probable cause directe dès que la machine fait moins de 50 trames/s.
- EmuKit : `EMD_SKIP` vaut 0 pour gcemu (pas d'image sautée), seuils `EM_Q_LOW` / `EM_Q_HIGH` ; regarder `em_gflags` (« un jeu vit au temps réel : le son le suit »).
- Le son et les manettes n'ont jamais été mesurés dans l'app : les chiffres ci-dessous sont sans son.

Mesures sur le Pi par `gcgl` (sans EmuKit, sans son, sans cadence), Wind Waker PAL : 53-63 trames/s sur 50 au début du titre, 36-40 au survol de l'île (machine 25-27 ms, GX 19-25 ms, GL 29-44 ms par image), 46-49 fin de survol ; film THP de Mario Kart 18 ; logos 8-19 (machine 53-118 ms, cause non établie : disque lu sur le thread machine ? à mesurer). Le frein est le thread machine (attend le GX ou GL 1-1,5 ms par trame), le GX suit de près. Shaders : 169, 0 refusé, 39 ms au pire.

Autres pistes, chiffrées dans docs/HANDOFF.md d'Onyx (« gcemu's speed: what is left ») : compare + branche FP fusionnés, mtmsr natif, moins de fins de run VI, fastmem, second niveau du JIT. Côté GL, ici : glBufferData recopie tout le VBO à chaque image (Onyx dessine en place) ; enregistreur couleurs et texgens encore en C++, textures, mixage Zelda aussi. Elegant compose pour tout le monde.

Sur le Pi, il reste : le **gamelib corrigé (SYS_N = 8) n'est pas dans SD0:/apps/gamelib.app/main** ; une copie essayée est dans SD0:/gcgl/gamelib (à vérifier par CRC), sinon le reconstruire depuis main. Copier un .app ne demande pas de redémarrer. SD0:/gcgl : gcgl (mesure sans EmuKit), ww/tp/mk.gxs, gamelib, emukit.so.avant. Le Pi a UIKit 204 et l'emukit de la session 3DS.

Décision à faire confirmer : le cœur reste en C++ (98 % de la machine dans le code recompilé, mesure d'Onyx), seules les boucles du GX sont en asm (`gx_asm.asm`).

**Why:** l'utilisateur veut jouer ; le 2 et le 3/11 il a dû éteindre le Pi, qui chauffe trop : sans refroidissement, pas de longue mesure ni de longue partie. SD1 intouchable.

**How to apply:** gcemu n'écrit jamais à côté du disque : carte et `.gxs` dans SD0:/home/gamelib/gc. ISO dans SD1:/Roms du Pi et C:\Temp\ARMPI\SD1\Roms (pas C:\Temp\Roms) ; copies ww, tp, mk dans WSL ~/gc-test. Tout build/ est à refaire dans un nouveau worktree : build.ps1, `tools/emu/build_gcemu.sh`, `build_gcgl.sh`, `build_gxes_host.sh`, `check_gc.sh` ; `gc_testimage.py` pour QEMU (`GC_LIB` : les kits d'une autre carte). Les heredocs de l'outil Bash abîment les antislashs : écrire les scripts avec Write, ou Edit. FASMARM `format ELF64` + `processor cpu64_v8 +cpu64_fp +cpu64_simd` donne des objets reliables au C++. gxes_host garde son propre `dirty`, gxgl efface celui de GTexture.

Piège des kits : poser un kit seul casse tout si sa version de kit_use dépasse celle de la carte (code 127, `kit absent ou trop ancien`), et un kit reste en mémoire jusqu'au redémarrage. Comparer les versions du journal, essayer sous QEMU avec les vrais kits, prévoir un redémarrage, et vérifier que personne d'autre ne touche la carte. Ajouter une console à gamelib : mettre à jour SYS_N. Voir [[armpi-emulateurs]], [[armpi-mesa]].

**14/11 (branche claude/x86-64-pc) : gcemu figé sur le ZBook** (Wind Waker noir, Kirby figé après le choix 50/60 Hz, fenêtre immobile, impossible de fermer). Cause trouvée et reproduite sous QEMU q35 (softpipe) avec un DOL à nous : `Player::pump` suivait l'anneau tant que `rd != wr` ; une machine plus rapide que le GL (le JIT x86-64 du ZBook) y tenait le thread de la fenêtre sans fin, et chaque image était « en retard », jamais montrée. Corrigé : pump s'arrête aux images finies à l'entrée ; le GL sur son propre thread (emu_thread « gc-gl ») ; EmuKit attend la machine 2 s au plus à la fermeture ; `pause` dans gxWait sur x86-64 ; trace `--trace` / `SD0:/etc/gcemu-trace` (journal + SD0:/home/gamelib/gc/gcemu-trace.txt). Pas encore essayé sur le ZBook ni sur le Pi (code commun : gcemu et EmuKit à reposer ensemble sur le Pi). Sous QEMU, l'heure du noyau (SYS_TIME_MS) avance 11 fois moins vite que le TSC de libposix : la trace montre les deux. DOL d'essai : tools/emu/gchb (gcc-powerpc-linux-gnu d'Ubuntu, devkitPro bloque les téléchargements automatiques), pctest_gc.py. Ne jamais lire D:\home (pas même les .gxs) ; ne rien effacer hors d'un dossier créé pour la tâche.
