---
name: armpi-webkit
description: "Portage de WebKit (Jet d'Onyx) sur ARMPI : état au 30/10/2026, scripts, IPC du noyau (0x70-0x7D), Jet commencé"
metadata:
  node_type: memory
  type: project
  originSessionId: 5720a561-b887-4de4-aad5-6b04aff7366c
  modified: 2026-10-30T00:47:01.910Z
---

- Scripts WSL : tools/webkit (env.sh, build-deps, build-jsc, build-webcore, build-wctest, build-wk2test, build-jet, build-media, build-ui, stage.sh) ; sorties ~/armpi-webkit-build ; build.ps1 recopie build/webkit/sdroot (dans un worktree : copier build/webkit du depot principal).
- Fait et vérifié QEMU : deps, jsc C_LOOP, WebCore (wctest), son -> AudioKit, uikit.h généré, chargeur d'images.
- 30/10/2026 (branche worktree-agent-a12527a311bc7a638) : IPC entre process dans le noyau (src/kernel/ipc.asm, appels 0x70-0x7D : poignees par process, canaux SEQPACKET/STREAM/DGRAM avec anneau 64 Ko, SCM_RIGHTS, HANDLE_WAIT = poll noyau, memfd/shm_open + SHM_MAP en regions VMK_SHARED, heritage HANDLE_INHERIT/HANDLE_LIST) + libposix (ipc.c...). posixtest ipc 27 PASS. WebKit2 tourne : wk2test (UI+web+reseau) peint page1.html. libWebKit.a n'a pas eu besoin d'etre recompile (en-tetes posix inchanges).
- Jet commence : src/extra/onyxports/jet (engine_webkit.cpp d'Onyx, main.cpp habillage UIKit ecrit pour ARMPI, present.c sur la surface attachee ui_surface) ; build-jet.sh -> SD0:/apps/jet.app. Verifie QEMU (pages locales, molette). Reste : menus, redimensionnement, telechargements/console, reseau reel https sur le PocketTerm.
- Adresses WebKit : /chemin (file://SD0:/x invalide). Essai QEMU de Jet : mettre SD0:/apps/jet.app/main dans sdroot/etc/startup.txt (sh n'a pas de &), puis le remettre.
- armpi_remote telnet : "@600 commande" pour une attente > 60 s (preload de 95 Mo).

**21/11 : fluidité de Jet (PC) sur Netflix/Disney+** (l'utilisateur : défilement et agrandissement des vignettes
saccadés ; Spotify, Wikipédia fluides). Analyse dans docs/WEBKIT.md « Fluidité sur Netflix et Disney+ » : le GPU ne
fait que composer ; dessin Skia logiciel, relecture glReadPixels, copies d'Elegant ; tout sur le fil principal du
process web (pas d'async scrolling) ; pas de couche pour les transforms 3D. Bug trouvé : les interrupteurs
SD:/etc/web-gpu-* sont morts depuis l'arbre unique (vfs refuse « NOM: ») ; patch 0012-switch-paths appliqué à
~/korin-webkit-x86_64/webkit-gl (cloud), pas construit. Les variables ONYX_WEB_GPU_* marchent. En attente : les
journaux de l'essai A/B (ONYX_WEB_GPU_3D=1) sur le ZBook.
**22/11 : mesures sur le ZBook** (Claude Code sous Korin, docs/agents/JET-MESURES.md) : la composition GPU attend
(15-45 ms/image, CPU 1 ms, NOCORES 1-8 ms) ; piste intelgpu (un lot à la fois, attente du CSB « repos » jusqu'à 50 ms).
Deuxième série demandée (journal du service, intelgpu status avant/après). Disney+ : 2 × 25 s d'attente dans le cadre
login.disney.com. Symboles : ~/korin-webkit-x86_64/webcore/bin/jet = paquet jet 1.4.3 (cloud). Pièges : lancée du
Terminal, une app écrit sur sa sortie (pas au journal) ; `journal` écrit /journal.txt ; `>` seul : les process de Jet
s'écrasent (position non partagée ?), passer par un tube.
**22/11 (suite) : publié** pc 2026.11.89 (intelgpu : temps des lots dans `intelgpu status` + IG_OP_TIMES, repos
attendu sur place 200 µs, attente finie répondue tout de suite ; hidmanager Ctrl+lettre), jet 1.4.4 (libposix de
pc .88, patch 0012, ligne `web: gpu: gl:` avec ONYX_WEB_GPU_DEBUG ou /etc/web-gpu-debug), claude-code 2.1.330.5
(Ctrl+Q rendu, libposix neuve). Méthode pc sans tout reconstruire : l'initrd publié modifié membre par membre
(tarfile ustar), `pub.py pc --tree <dossier> --boot <dossier avec korin/initrd.tar> --only …` (sans --boot, pub
prend build/pc/esp, périmé ici). Rust : cargo ne relie pas après un libposix neuf (toucher main.rs). En attente :
mesures du ZBook (lignes gl:, intelgpu status avant/après un défilement de Netflix).
**22/11 : mesure du ZBook avec pc .89** : `intelgpu status` après Netflix : attentes des clients 26 ms en moyenne,
50,7 au pire ; « moteur » 7,7 / 50,7 ms ; fin -> repos 0 (le repos suit tout de suite). Cause : le fil du moteur
dormait 50 ms (délai décidé avant le lot) et une fin de lot arrivée pendant le traitement de la précédente est perdue :
**le noyau du PC (src/kernel/core/hw.c) remet `raised` à zéro dans sys_irq_wait et sys_irq_ack** — un défaut qui
peut toucher tous les pilotes (nvme, usbd, hda, iwl…) ; pas corrigé dans le noyau (autre session, risque). Contourné
dans intelgpu (d57ee0f) : 1 s après une activité, sommeil de 1 ms ; publié pc 2026.11.91. À mesurer de nouveau.
Pour recueillir les messages d'une app lancée au Terminal : `2>&1 | cat > fichier` (grep peut garder sa sortie).
**22/11 : pc .91 mesuré sur le ZBook (Netflix)** : attente des clients 0,22 ms en moyenne (3,7 au pire), moteur
0,27 ms (au lieu de 26 / 50 ms et 7,7 ms) : le contournement d'intelgpu marche. Restent des 50 ms pour le premier lot
après plus d'une seconde de repos (le défaut de sys_irq_wait, noyau).
**Régression libposix trouvée (pour la session des tubes, d552ab2)** : après un `execv` (app web Netflix -> Jet), la
sortie d'erreur des process lancés ensuite par posix_spawn (le process web de Jet) ne passe plus par le tube hérité du
Terminal (`/apps/netflix.app/main 2>&1 | cat > f` : seules les lignes du process UI) ; Jet lancé directement : bon.
Reproduit sous QEMU (jet 1.4.4, libposix de pc .88). Avec jet 1.4.3 (libposix d'avant), ça passait.
**22/11 : Disney+ (investigation de l'utilisateur, autre session)** : les 2 × 25 s = le WebSocket du SDK de Disney
dans Jet ; le SDK attend deux fois 15 s avant de se rabattre (puis recommence sur /fr-fr/home). La même poignée de
main en Python reçoit 101 Switching Protocols : suspect, la vérification de la réponse par le client WebSocket du port
curl (Connection: Upgrade en TLS avec ALPN h2) — WebCore/platform/network/curl. Écartés : OneID, worker Datadog,
workers de blob, cookies tiers, en-têtes du navigateur.
**22/11 : Disney+ corrigé (patch 0013)** : WebSocketExtensionParser::consumeToken n'avançait pas m_data (consumeSpan
sur une copie) : toute réponse avec Sec-WebSocket-Extensions échouait. Vérifié sous QEMU (serveur local, contre-essai
1.4.4 en échec). jet 1.4.5 construit dans le cloud, pas publié (accord de l'utilisateur). Détails :
docs/agents/JET-WEBSOCKET-DISNEY.md. Apps web 1.0.1 (fenêtre normale) et claude-code 2.1.330.6 (volet, terminal,
cloud, file) publiés.
**22/11 : SDK de Jet publié** (docs/agents/JET-SDK.md) : jet-sdk-1.4.6-{1,2,3}.zip dans la Release « packages »
(workflow release-upload.yml étendu aux .zip). Installé par tools/webkit/jet-sdk-get.sh dans /devtools/jet-sdk ;
jetbuild (un lot unifié) et jetlink (-> /tmp/jet-main) avec le GCC de Korin. Pièges de sa construction : en-têtes de
libposix retirés (le GCC de Korin a les siens), liens symboliques -> en-têtes d'une ligne, cmake_pch.hxx et
onyx-cc.specs réécrits. Lier sous QEMU vers le disque : délai dépassé du service de fichiers (170 Mo) -> /tmp.
**22/11 : TF1+, résultats de l'agent local** (docs/agents/JET-TF1-ETAPES-RESULTATS.md) : « other » ≈ 400 ms par
mise à jour lourde ; compositing layout, layer flush, observateurs innocents. « other » = surtout le layoutIfNeeded()
du milieu de Page::updateRendering (après les requestAnimationFrame), non chronométré dans 0014. Patch 0015
(style/mise en page séparés, partout) construit dans le cloud.
