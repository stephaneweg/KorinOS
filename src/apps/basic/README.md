# basic

`/bin/basic`, le runtime du BASIC d'Onyx sur ARMPI ([docs/BASIC.md](../../../docs/BASIC.md)). Ce n'est pas FreeBASIC.

    basic [-d dossier] [-i fd] [-c] [-m] [-t] [-g] [-p] programme.bas|.bax [arguments]
    basic -u

`-c` écrit le `.bax` ; `-m` : la VM, sans code machine ; `-i fd` : les erreurs à un éditeur (`ligne\0message\0`) ;
`-t` : le texte sur la sortie même sans terminal ; `-p` : où passe le temps (`SD0:/basprof.txt`) ; `-u` : redonne ce runtime aux
apps autonomes de la carte ; `-g` : essai de la 3D, listes du GPU construites et rien d'envoyé (`GK_DBG_DRY`), compte sur la sortie
d'erreur (l'image reste noire).

Construire : `pibuild bin /src/apps/basic basic`, ou `sh tools/office/build.sh basic`, qui écrit aussi `SD0:/lib/*.bi`.
La VM est dans `src/lib/basic`, `core.cpp` l'inclut. `baskits.h` : ce que `#import` donne (noms d'Onyx).
