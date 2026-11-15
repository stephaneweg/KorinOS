---
name: armpi-paquets
description: "Gestionnaire de paquets d'ARMPI (1/11/2026) : pkg, pkgd, pkgman, depot onyx-packages/armpi, publish = 0, comment publier"
metadata:
  node_type: memory
  type: project
  originSessionId: 7fc97ae2-2e97-4ea4-870b-1c00d2b2f6f2
  modified: 2026-11-01T23:35:40.851Z
---

Port du gestionnaire d'Onyx : `src/lib/pkg/pkglib.h`, `/bin/pkg`, `src/sys/pkgd`, `src/apps/pkgman`, `tools/pkg/`, doc `docs/PAQUETS.md`. Depot : `https://stephaneweg.github.io/onyx-packages/armpi` (sous-dossier `armpi` de stephaneweg/onyx-packages, clone local `C:\Projects\onyx-packages`), board = armpi. Cle privee : `C:\Projects\onyx_pkg-key.pem.txt` (la meme qu'Onyx, verifie) ; ne jamais l'afficher.

Publier (depuis la racine, apres `tools/gcc/kits.sh`, `tools/office/build.sh`, `tools/pkg/build.sh`, `build.ps1`) :
`ONYX_PACKAGES_DIR=/c/Projects/onyx-packages ONYX_PKG_KEY_FILE=/c/Projects/onyx_pkg-key.pem.txt sh tools/pkg/publish.sh` puis pousser ; commiter `tools/pkg/versions.ini`.

Publie le 2/11/2026 : armpi 2026.11.1, 65 paquets. `pkg upgrade` ne met a jour que `armpi` : les apps de la base sont en « manuel » (disponible, code 1), a mettre a jour une par une ou par Logiciels. Essai reel sous QEMU possible : image d'un ancien sdroot, `check = never`, `pkg update armpi` par telnet (`@420 commande` dans armpi_remote.py), redemarrer ; le reseau de QEMU atteint le depot en ligne.

**Why:** demande de l'utilisateur. `publish = 0` dans packages.ini garde hors du depot public : armpi-src (sources : nom de l'OS et licences pas regles, voir [[armpi-projet]]), koton-samples (ses chansons, copiees de son dossier Music par build.ps1), webkit-tests, system-tests.

**How to apply:** avant tout push, relire le contenu des .opk (`home/`, `etc/`, cles). Sans WSL par Git Bash : `wsl -d` par PowerShell (Git Bash deforme les chemins). pkgd contacte GitHub sous QEMU : `check = never` pour un essai hors ligne. Un correctif de fat32.asm (noms `~n` au-dela de 9) doit etre sur la carte avant la premiere mise a jour systeme : refaire une fois la carte par build.ps1 sd.

Depuis le 4/11 : le dépôt de paquets est packages/ de KorinOS (GitHub Pages, site limité à 1 Go). Gros paquets : `split = MOTIF MO` (parties <nom>-data-N) et `release = 1` (archives dans build/pkg-release, à envoyer dans la Release « packages » de KorinOS ; l'index signé donne leur adresse complète, pkglib la suit, tampons de 2 Ko pour la redirection). SuperTux et SuperTuxKart publiés ainsi (15 archives, 982 Mo) ; gh n'est pas installé : envoi par l'utilisateur. mkimages.py retire de l'image full les paquets HORS_FULL d'après var/pkg/db. build/sdl/sdroot/sdroot était un doublon (supprimé, sa version plus récente remise dans apps/). Construire avec `$env:ONYX_DIR = "C:\Projects\Onyx"`, sinon soundfonts manque et sortirait de l'index.

Publier quelques paquets seulement (4/11) : quand le dossier de travail porte le travail non commité d'une autre session, publish.sh republierait armpi (noyau recompilé), korin-srcs, pi-firmware et sortirait soundfonts. On a alors refait les seules sections voulues de packages/index.txt avec les fonctions de mkrepo.py, en gardant l'abi de l'index publié (le dossier de travail était à 579, le système publié à 570), puis signé et vérifié par checkrepo.py.

Publication partielle (8/11) : scratchpad pub_partial.py (à refaire si besoin) : extrait les archives publiées des paquets
visés, remplace seulement les binaires changés par le build d'un worktree, puis mkrepo.plan/manifest_text/write_opk/sign ;
l'examen sans --write liste ce qui diffère (doit être exactement les fichiers changés ; test.img diffère toujours).
Fermer les ZipFile (Windows refuse de supprimer une archive ouverte). checkrepo : « archive absente » normal pour les
paquets release = 1.

PC (décision du 12/11) : index à part dans packages/pc/ (index.txt, index.sig, board = pc, pkgs/), même clé ; Pi inchangé à la racine ; paquets sans code machine une seule fois dans packages/pkgs/, cités par ../pkgs/ avec arch = any.

Clé dans le cloud (14/11) : l'utilisateur a désigné la variable d'environnement **`OnyxPkgKey`** de l'environnement du
cloud comme la clé à utiliser pour signer les paquets de Korin. C'est la clé PEM elle-même (PKCS#8, P-256) sur une
seule ligne, la même que `onyx_pkg-key.pem.txt` (vérifié : sa clé publique = `skel/sd0/etc/pkg/onyx.pub`).
`publish.sh` la remet en PEM dans un temporaire 600 quand `ONYX_PKG_KEY_FILE` manque. Ne jamais l'afficher : un
diagnostic l'a imprimée une fois dans une session du 14/11 (un `print` de la « première ligne » alors qu'elle n'en a
qu'une) ; ne tester que par des booléens (chargée, correspond à onyx.pub). Publier et pousser seulement sur demande.

**Dépôt du PC publié le 14/11** (docs/PAQUETS.md, « Korin pour PC ») : packages/pc/ (board = pc, 90 paquets, ~60 Mo
sur Pages ; pc 2026.11.1) ; 4 cités du Pi par ../pkgs/ (arch = any : soundfonts, fmtracker-samples, koton-demos,
arabesque-samples) ; 11 archives pc-*.opk dans la Release (jet, python, vibe+2, dotnet+2, avalonia, gcc+1, ~220 Mo).
Outils : ARCH=x86_64 pour mkrepo/checkrepo/publish/build.sh, tools/pkg/packages-pc.ini, versions.ini [pc] ; publier
depuis un worktree de main (KORIN_REPO_DIR), seul packages/pc/ commité. Pi inchangé (comparé octet pour octet).
Essai QEMU : image mkkorindisk --layout zbook --korin-mb 700 --cfg 'default = korin\ntimeout = 0' en NVMe (pas en
SATA : pas de pilote), telnetd + etc/remote.txt dans l'image d'essai seulement, index local re-signé ; add, ../pkgs,
Release, update pc + commit + 2 redémarrages : bons. ZBook : il faut un korin.ps1 install avec le nouvel arbre (bin/pkg,
var/pkg/db, etc/pkg) avant que pkg/Logiciels ne marchent ; ensuite pkg update -a.

**pc 2026.11.3 (14/11, main bd76f66)** : renommer dans Fichiers (F2), publication partielle depuis claude/x86-64-pc :
seuls apps/files.app et res/lang/en.txt (clés ajoutées à la fin) remplacés dans l'archive publiée ; le contenu publié
est d'abord recalculé à l'identique (formule de mkrepo), puis index réécrit pour la seule section [pc], signé par
OnyxPkgKey (remise en PEM comme publish.sh), checkrepo. Le Pi n'est pas publié : son armpi publié a encore files.asm,
le Fichiers en C n'y est pas validé. Piège d'essai : l'étape `type:` de qemu_nvme.py tape déjà Entrée.

**pc 2026.11.4 (15/11, main 3746259)** : noyau avec réveil par IPI (2bd6c3d), publication partielle : seul korin/kernel.elf
remplacé. Pièges : prendre mkrepo.py de la branche (celui de main n'écrit pas la ligne boot du manifeste) ; signer
sans `python -I` (cryptography est dans le site-packages de l'utilisateur) ; packages-pc.ini n'est que sur la branche.
