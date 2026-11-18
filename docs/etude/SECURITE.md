# Plan : protéger Korin d'une page web piégée

Décidé le 18/11/2026, après une réévaluation de l'écart avec SerenityOS. Rien n'est fait. Le PC d'abord ; le Pi
suivra avec le passage de son noyau au noyau commun en C (fin du plan).

**Pourquoi maintenant.** La décision du 29/10 (« pas de sécurité contre un attaquant, seulement la résistance aux
bugs ») tenait pour un appareil hors ligne. Korin navigue aujourd'hui sur Internet avec Jet (WebKit), avec un vrai
compte Google, du courrier et Telegram : une page piégée est le seul risque réel d'un usage quotidien. Ces étapes
visent ce risque-là ; plusieurs protègent aussi des bugs (un process qui écrit au mauvais endroit).

## 1. Ce qui est déjà en place (18/11/2026)

* Multi-utilisateur sur le PC : uid par process dans le noyau, comptes, connexion, `sudo`, élévation (`authd`).
* Coffre à secrets (TPM) : jetons et mots de passe rangés par programme, reconnus par leur chemin.
* Serveurs réseau (telnetd, ftpd, vncd, rdpd, webd, smbd) éteints par défaut, avec mot de passe une fois allumés.
* Accès au matériel par une capacité (`rights`) : registres, IRQ, kits ; transmise seulement à un enfant ; jamais
  la RAM (`src/kernel/core/hw.c`, `proc.c`).
* Noyau du PC : un espace d'adressage par process, pointeurs vérifiés (`copy_from_user`), pages non exécutables
  (NX).

## 2. Les étapes (de la moins chère à la plus lourde)

| # | Étape | Contenu | Protège aussi des bugs | Taille |
|---|---|---|---|---|
| 1 | **`access = enforce`** | lire le journal de vfs en mode `log` sur le ZBook, corriger ce qui est refusé à tort, puis `enforce` par défaut dans `system.ini` (étape 5 de MULTI-UTILISATEUR.md) | oui : une app ne peut plus écrire dans `/` ni chez un autre compte | petite |
| 2 | **SMEP, SMAP, UMIP** (PC) | bits 20, 21 et 11 de CR4 sur chaque cœur quand le processeur les a (CPUID 7) ; `stac`/`clac` autour de `copy_from_user`, `copy_to_user` et des autres accès du noyau aux pages d'un process ; une faute du noyau sur une page utilisateur devient une panne nommée | oui : un pointeur utilisateur suivi par erreur dans le noyau fait une faute claire | petite (heures) |
| 3 | **`unveil` dans vfs** | une liste de chemins permis par process (lecture, écriture, création, exécution), posée par le process lui-même et seulement réduite ensuite, héritée par ses enfants ; vfs connaît déjà l'uid de chaque pid et applique des règles par chemin. Jet la pose sur son process de rendu (WebContent) : son cache, ses cookies, `/tmp` à lui, les polices, les kits ; un téléchargement passe par le process principal. Appel `unveil()` dans libposix, comme OpenBSD | oui | moyenne |
| 4 | **ASLR** | base aléatoire de la pile, du tas (`brk`), des zones `mmap` et des kits partagés par process ; programmes PIE quand la chaîne le permet (GCC de Korin, ports) ; source d'aléa : `random` du noyau (RDRAND sur PC) ; une option de démarrage pour l'éteindre (débogage) | non | moyenne |
| 5 | **`pledge`** | familles d'appels système permises par process (fichiers, réseau, lancement, IPC, matériel), seulement réduites ; Jet la pose sur WebContent (ni lancement de programmes ni réseau direct si le réseau passe par le process réseau de WebKit) ; une violation tue le process et l'écrit au journal | un peu | moyenne à grande |

Chaque étape s'essaie sous QEMU puis sur le ZBook ; les étapes 3 et 5 se jugent par un programme d'essai qui tente
ce qui lui est interdit (`sectest`), et par Jet sur des sites réels (Gmail, YouTube, Facebook) sans régression.

## 3. Hors de ce plan

* Le Pi : son noyau en asm n'a pas d'uid ; rien de ce plan ne s'y applique avant le noyau commun en C.
* L'intégrité des données (journal ext4 : SYSTEME-FICHIERS.md, étape 4) : au plan à part.
* Signatures des programmes, démarrage sécurisé (Secure Boot) : non demandés.
