# Gestionnaire de tâches (taskman)

Le Task Manager d'Onyx en C++ (`main.cpp`), un seul source pour le Pi (AArch64) et le PC (x86-64). `taskman.asm`
reste dans le dossier jusqu'à la validation sur le vrai Pi ; `tools/office/build.sh taskman` (Pi) et
`tools/pc/apps.sh taskman` (PC) construisent le C++.

## D'où vient le code

* **Onyx** (`user/Apps/taskman/main.cpp`) : quatre onglets sous un contrôle segmenté (Processus, Mémoire,
  Processeur, Réseau), la grille triée par insertion (`cmp`, `sort_rows` : une colonne, puis le nom sans la casse),
  l'historique d'une minute (`push`), les tuiles et les courbes des vues dessinées, les formats des tailles et des
  débits, Entrée ou double-clic (Au premier plan), Suppr (Terminer), R (relire), la fenêtre redimensionnable.
* **L'asm de Korin** (`taskman.asm`), qui fait foi pour ce qu'on voit, et ce qu'il a de plus qu'Onyx : les colonnes
  Tâche, Genre, État, CPU, Mémoire, Threads, Tas, PID ; le genre (Système : un programme de `SD0:/sys`, par
  `SYS_PROC_RIGHTS` ; Application : un process qui a une fenêtre, par `ui_win_details` ; sinon Programme) ; l'état
  d'après ses threads ; le CPU de chaque process sur la dernière seconde, par les temps de ses threads
  (`SYS_THREAD_LIST`), et le process qui a le plus occupé chaque cœur ; la mémoire à lui (`SYS_VM_STATS`) ; les
  commutations par seconde et le temps de marche (`SYS_SYSINFO`) ; la confirmation avant de terminer un programme
  du système ; l'onglet Réseau sur les compteurs de netd (`net_stats`, `net_ifaces` de NetKit) ; le pied de chaque
  onglet ; les couleurs fixes des cœurs, de la bande mémoire et de l'émission (du contenu, comme dans l'asm), le
  reste aux jetons du thème ; `lang/en.txt`, `app.txt` inchangés.
* Les appels du noyau passent par `<armpi/syscall.h>` : sur le PC, `src/kernel/core` rend les mêmes structures
  (entrées de 64 octets de `SYS_PROCLIST_FROM`, emplacement = `(pid − 1) & 0xFFFF` comme sur le Pi ; `THL_*` de
  `SYS_THREAD_LIST` ; `SYS_VM_STATS` pages à lui en premier mot ; `SYS_SYSINFO` de 80 octets).

## Différences

* **Avec l'asm** : une colonne de tri inconnue (−1) trie par nom (l'asm lisait hors de sa table) ; le nom d'un
  process est coupé à 32 octets (l'asm comptait sur l'octet nul du mot d'état qui suit).
* **L'asm (donc ce port) avec Onyx** : pas d'appels système par seconde ni de réseau par process (le noyau de Korin
  ne les compte pas), pas de ligne adresse / nom d'hôte / sockets ; à la place, threads, tas, CPU par process,
  commutations, temps de marche, interfaces de netd.

## Ce qui manque

Rien par rapport à l'asm. Sur le PC, le noyau ne mesure pas encore la charge des cœurs : `SYS_SYSINFO` met le
nombre de threads prêts à la place de celle du cœur 0, et 0 pour les autres (`src/kernel/core/sys.c`) ; l'onglet
Processeur le montre tel quel. Le CPU par process (temps des threads) y est juste.
