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
