---
name: armpi-idee-emerge
description: "Idée pour plus tard (16/11/2026) : un « emerge » à la Gentoo, surtout pour Korin PC (x86-64) ; recettes de construction tirées du dépôt KorinOS et exécutées dans l'OS, options d'optimisation dans /etc"
metadata:
  type: project
---

Idée de l'utilisateur, notée le 16/11/2026, **pour plus tard** (pas à faire maintenant) : une commande `emerge`
comme celle de Gentoo. On a la liste du dépôt ; `emerge jet` télécharge depuis le dépôt KorinOS **le script de
construction** de Jet, qui contient tout : téléchargement des sources et des patchs, application des patchs,
construction de l'ensemble, jusqu'à l'installation d'un Jet fraîchement construit. Des options dans `/etc`
(l'équivalent de `make.conf` : `-march`, `-O`, `-j`…) optimisent la compilation pour la machine.

**Cible : surtout Korin PC (x86-64)** (précisé par l'utilisateur le 16/11), pas le Pi. Sur le Pi, techniquement,
les petites apps pourraient aussi se construire, mais ce n'est pas le but.

**Why:** construire les logiciels sur et pour la machine (le PC a la puissance et la RAM pour Jet/WebKit), au lieu
de seulement installer les `.opk` construits dans WSL.

**How to apply:** à proposer quand on revient sur les paquets ou les outils de dev de Korin PC. Pistes à ce moment :
- Réutiliser ce qui existe : l'index signé de `pkg` (une recette = une entrée signée avec SHA-256, comme une archive),
  `needs` pour les dépendances, la base `/var/pkg/db` pour enregistrer ce qui est installé (le résultat d'un
  `emerge` reste un paquet connu de `pkg`, retirable). Les sources téléchargées vérifiées par SHA-256 dans la recette.
  Les recettes portent l'arch (x86_64 d'abord, armpi pour les petites apps).
- Il faut que la recette tourne **dans l'OS** : sur PC, GCC, binutils, make, fbc, fasm, git, rg, bash sont dans
  `/gcc` / `/devtools` depuis le 13/11 ([[armpi-x86-pc]]). Manquent probablement cmake et ninja (WebKit, Mesa, SDL),
  `mkfifo` (jobserver de `make -j`), `awk`, `xargs`, `/usr/bin/env`, les liens symboliques : patcher les
  `configure`/makefiles des projets ou porter ces outils. Vérifier l'état de la couche POSIX du PC à ce moment.
- **`./configure` dans Korin (analyse du 16/11)** : pas encore. Bloquants : pas d'`awk` (config.status en a
  besoin), pas d'`expr`, `egrep`, `od`, `cmp` ; `sed`/`grep` en asm sans `{n,m}` ni options GNU ; pas de
  `regcomp` dans libposix ; position de fichier non partagée entre process pour les programmes asm (config.log) ;
  triplet `x86_64-onyx-elf` vu comme « sans OS » par config.sub. Remède envisagé : porter les vrais outils
  (gawk, GNU sed, grep, coreutils/diffutils, ou BusyBox) en croisé depuis WSL, dans `/devtools`, plus un triplet
  `x86_64-pc-korin` connu de config.sub (ou `--host` forcé + cache `config.site`).
- Les scripts actuels (`tools/pc/*.sh`, `tools/webkit/build-jet.sh`, `fetch.sh`, patchs) sont des **compilations
  croisées dans WSL** : ils sont la base des recettes mais pas exécutables tels quels sur Korin.
- **Variante sans pkg** (proposée par l'utilisateur le 16/11, à préférer) : un dépôt git de recettes cloné dans
  l'OS (comme l'arbre Portage), mis à jour par `git pull` (`emerge --sync`). Pistes : dépôt séparé et léger (pas
  tout KorinOS), un dossier par logiciel (`recipe.sh` : version, sources + SHA-256, dépendances, fonctions
  fetch/prepare/build/install ; `patches/`), installation dans un DESTDIR puis copie, liste des fichiers gardée
  pour `emerge -C`. Confiance = HTTPS de GitHub + SHA-256 des sources (pas de signature, gpg absent). Point à
  trancher : ne pas écraser les fichiers d'un paquet `pkg` (lire `/var/pkg/db` ou y écrire).
- **Dépendances récursives** (demandé le 16/11) : les dépendances se construisent aussi depuis leurs recettes, en
  cascade. Pistes : graphe trié (feuilles d'abord), détection des cycles, saut de ce qui est déjà installé à une
  version suffisante, `-p` (afficher le plan), `--resume` après un échec, distinguer construction et exécution
  (DEPEND/RDEPEND de Gentoo) ; ce que le système fournit déjà (libposix, kits, GCC) déclaré « fourni ».
- `-march=native` dans `/etc` : dans Korin PC, AVX-512 n'est activé que par l'option de démarrage `avx512`
  (`src/kernel/arch/x86_64/cpu.c`), GCC le voit par XCR0 et ne l'utilise pas sinon ; ne pas écrire tigerlake en dur.
- Commencer petit (une lib : zlib, puis une app C), Jet/WebKit en dernier.
- Les bibliothèques tierces livrées avec le GCC de l'OS ([[armpi-idee-libs-gcc]]) en sont le prérequis naturel.
Voir [[armpi-paquets]], [[armpi-gcc-sur-armpi]], [[armpi-idee-build-all]], [[armpi-x86-pc]].
