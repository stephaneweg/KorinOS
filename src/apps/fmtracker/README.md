# FM Tracker (fmtracker)

Le tracker FM d'Onyx : 8 canaux d'instruments FM à 2 opérateurs (la synthèse FM d'AudioKit) sur une grille de
tranches de temps ; lit et écrit les morceaux FM Song (`.FMS`) et les instruments (`.FMI`), exporte en WAV.

Porté en C++ depuis Onyx le 13/11 (un seul source pour le Pi et le PC) ; l'asm (`fmtracker.asm`, `fmt_*.inc`) reste
dans le dossier, assemblé par `build.ps1`, jusqu'à la validation du C sur le vrai Pi.

## Fichiers

| Fichier | Rôle | Vient de |
|---|---|---|
| `fms.h` | le modèle (morceau, motifs, instruments), `.FMS` et `.FMI`, notes | `fms.h` d'Onyx (presque tel quel) |
| `main.cpp` | fenêtre, menus, barres, édition, annulation, lecture, commandes, export WAV | `main.cpp` d'Onyx pour les règles, `fmtracker.asm` et `fmt_edit.inc` pour la fenêtre |
| `view.h` | dessin (grille, en-têtes, liste, piano, état), souris, clavier | `fmt_view.inc` (mêmes coordonnées, mêmes teintes) |
| `ins.h` | éditeur d'instrument, préréglages, infos du morceau | `InsDialog` d'Onyx, calculé en entiers comme `fmt_ins.inc`, `fmt_tables.inc` |
| `ins/`, `songs/`, `lang/` | instruments, morceaux d'exemple, mots en français | inchangés |

## Ce qui vient d'où

* **D'Onyx** : `FmsSong` / `FmsPattern` / `FmsIns` (notes `n[canal * lignes + ligne]`, tableaux alloués par motif),
  lecture et écriture des `.FMS` / `.FMI`, `fms_make_note`, `fms_transpose`, le texte des notes, l'annulation par
  photos de motif (48), le presse-papiers, les règles de lecture (`play_row`, rattrapage, boucle, suivre), les
  ajouts et déplacements de motifs, les formules du dessin de l'enveloppe et de l'onde.
* **De l'asm de Korin** (fait foi pour ce qui se voit) : la fenêtre entière (barres d'outils de UIKit, afficheur
  « motif:ligne temps » sur une ligne, panneau dessiné, petits boutons, nombres, en-têtes, grille, piano, barre
  d'état), l'éditeur d'instrument en entiers (sinus en table, tables de durées), le dialogue des préréglages (une
  liste), les infos du morceau (`ui_dialog_*`), les menus et leurs raccourcis, le glisser-déposer d'un `.FMS`,
  l'argument de la ligne de commande, les chemins (`/apps/fmtracker.app/ins`, `…/lang/<langue>.txt`,
  `HOME:/music/fms`, `/tmp/fmtracker-export.fms`), les couleurs (jetons du thème ; restent fixes celles qui
  l'étaient dans l'asm : couleurs des canaux, pastilles M / S, vumètre, touches du piano, vert de la lecture).
* **Kits** : `ui_*` d'UIKit, `ak_fm_*` (synthèse en direct, `ak_fm_from_fms` lit les 26 octets d'un instrument),
  `ak_dec_open` / `ak_enc_open` (export) d'AudioKit ; la libc et POSIX pour les fichiers et les dossiers ;
  `armpi_syscall (SYS_TIME_MS)` pour l'heure en millisecondes.

## Formats

Les fichiers écrits sont ceux de l'asm, octet pour octet (vérifié : un `.FMS` édité puis enregistré par l'asm et
par le C sous QEMU donne le même fichier). Le WAV exporté : la synthèse n'est **pas** dans l'app, c'est le
décodeur FM Song d'AudioKit qui lit le `.FMS` écrit pour l'occasion ; même `.FMS`, même kit, même WAV.

## Écarts avec l'asm

* Les arguments : `argv[1..]` recollés par des espaces (l'asm prenait la ligne brute de `ui_arg`) ; un chemin
  entre guillemets ou avec deux espaces de suite différerait.
* Les notes d'un motif sont un tableau à sa taille (Onyx) et non une case fixe de 8 x 256 : sans effet visible
  (agrandir un motif ajoute des « --- », comme l'asm).
* Gardé comme l'asm (bizarrerie) : OK, Annuler ou Entrée dans le dialogue des préréglages ferment **aussi**
  l'éditeur d'instrument (OK : l'instrument choisi est gardé ; Annuler : rien ne change) ; Échap ne ferme que les
  préréglages. Le titre du dialogue « Song information » n'est pas traduit (l'asm le passait tel quel).

## Ce qui manque

* Le mode poche (le panneau en tiroir d'Onyx) : ni l'asm ni le C ne l'ont.
* Les glyphes ⊞ ▲ ▼ ✕ des petits boutons du panneau ne sont pas dans la police : ils s'affichent « … » (comme
  l'asm).
* Une aide (`help_fr.md`) : l'app n'en a pas.

## Construire

* Pi : `wsl sh tools/office/build.sh fmtracker` ; `build.ps1` met `ins/` et `lang/` dans `fmtracker.app` et
  `songs/` dans `/home/public/music/fms`, puis recopie `build/office/sdroot` par-dessus (le `main` du C).
* PC : `wsl sh tools/pc/apps.sh fmtracker` ne copie que `app.txt`, `data/` et l'aide : il faut y ajouter `ins/` et
  `lang/` vers `fmtracker.app/` et `songs/*` vers `home/music/fms` (voir le rapport du port).
