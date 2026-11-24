---
name: armpi-securite
description: "Plan du 18/11 contre une page web piégée (PC d'abord) : access=enforce, SMEP/SMAP/UMIP, unveil dans vfs pour WebContent de Jet, ASLR, pledge ; docs/etude/SECURITE.md, rien de fait"
metadata:
  type: project
---
Le 18/11/2026, après la réévaluation de l'écart avec SerenityOS (session « Revue KorinOS vs Serenity » du 17/11),
l'utilisateur a mis au plan, dans cet ordre : 1) `access = enforce` dans vfs ; 2) SMEP, SMAP, UMIP sur le PC ;
3) `unveil` dans vfs (liste de chemins par process, posée par Jet sur son process de rendu) ; 4) ASLR ; 5) `pledge`.
Détail : docs/etude/SECURITE.md. Rien de fait.

**Why:** assouplit [[armpi-pas-de-securite-attaquant]] (29/10) : Korin navigue sur Internet avec un vrai compte
Google ; une page piégée dans Jet est le seul risque réel au quotidien.

**How to apply:** PC d'abord ; le Pi après le noyau commun en C (son noyau asm n'a pas d'uid). Déjà en place :
comptes, coffre ([[armpi-coffre]]), serveurs réseau éteints par défaut, capacité `rights` pour le matériel, NX.
Voir [[armpi-multi-utilisateur]], [[armpi-webkit]].

**24/11, audit de sécurité du PC** : docs/etude/AUDIT-SECURITE-PC.md (lecture seule, rien corrigé). Causes de fond :
avis du noyau forgeables (`sys_svc_call` ne refuse pas `op >= 0xFFF0`), services sans contrôle d'appelant (vfs
PATHMAP/MOUNT, fs.*, blk.*, pcid, usbd, hidmanager, Elegant, oauthd), `/tmp/session.ini` qui décide de l'uid,
failles noyau (SHM_TRUNC, ELF offset+filesz, IMAGE_CREATE ouvert, FMASK sans NT). Plan en 5 vagues au §9 ; `enforce`
n'a de sens qu'après les vagues 1 à 3.

**24/11, seconde passe** (même fichier, §10-§17, plan révisé §17 qui remplace §9) : chaînes C1-C8 (C3 : anneau 0 en
4 appels même en enforce par blkserv op_attach, fenêtre déclarée > tampon -> DMA en RAM) ; Jet : WebContent sans
cloisonnement, cookies.db en clair, /etc/web-probe.js injecté partout, intelgpu CHUNK_FREE sans unbind ; cache
/file-* de libposix empoisonnable entre comptes ; démarrage : partitions visibles de Windows, ESP de Windows en
écriture, CR0.WP jamais posé, console de secours sans mot de passe ; vterm /dev/pts ouvert à tous. L'utilisateur fera
les corrections après le portage de Blender (24/11).

**24/11, plan de correction** : docs/etude/PLAN-CORRECTIONS-SECURITE.md (E00-E75 en 12 phases, chaque étape avec son
analyse de non-régression faite sur le code, règle de l'utilisateur : « avant toute implémentation, vérifier en
profondeur qu'il n'y aura pas de régression »). §2 : 29 propositions du brouillon qui auraient cassé le système (avis
= pid 0 et non REQ_SIGNAL-1, WS_SIGNAL, IMAGE_CREATE par uid, NXE au trampoline, 0xB2 batterie, DEV_MAP, rights de
session et SVC_REGISTER, WF_TOP/GRAB d'UIKit, VFS_BOOT_BEGIN/pkglib, forbidden et paquet pc, NTP qui recule…).
Règle Pi : E_NOSYS = uid 0 permis. Rien d'implémenté ; corrections après Blender ; E62 et E70 pas pendant Blender.
