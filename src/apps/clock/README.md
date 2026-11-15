# Horloge (clock)

L'Horloge d'Onyx en C++ (`main.cpp`, `alarms.cpp`, `alarms.h`), un seul source pour le Pi (AArch64) et le PC
(x86-64). `clock.asm` reste dans le dossier jusqu'à la validation sur le vrai Pi ; `tools/office/build.sh clock` (Pi)
et `tools/pc/apps.sh clock` (PC) construisent le C++.

## D'où vient le code

* **Onyx** (`user/Apps/clock`) : le modèle des alarmes (`alarms.h` : `Alarm`, `AlarmSet`, `alarm_next`,
  `alarms_next`, `alarm_passed`, `alarm_once_day`, `alarm_repeat_kind`, `alarm_find`, `alarm_remove`, les numéros
  uniques de `from_doc`), les dates de `clocktime.cpp` (H. Hinnant), les trois sons de `sounds.h` (carillon, bips,
  marimba), joués par les voix FM d'AudioKit (`src/inc/gamesound.h`).
* **L'asm de Korin** (`clock.asm`, `src/inc/alarms.inc`), qui fait foi pour ce qu'on voit : deux onglets (Horloge,
  Alarmes) ; le cadran analogique anti-crénelé dessiné au pixel (couverture par distance au segment et au cercle,
  séries de sin et cos, mêmes flottants simples : `#pragma GCC optimize ("fp-contract=off")` et des `fmaf` là où
  l'asm a `fmadd` / `fmsub`, donc mêmes pixels), l'heure en chiffres, les boutons +1 h, +1 min, −1 min ; le
  décalage gardé dans `/etc/clock.cfg` et publié sur `korin.clock.offset` (gardé) ; la page Alarmes (liste
  triée par heure, prochaine alarme, Ajouter, Modifier ou double-clic, Activer / Désactiver, Supprimer ou Suppr,
  20 au plus), le dialogue d'une alarme (heure, nom de 40 caractères, répétition et jours qui se suivent, son,
  Essayer, Active ; Entrée dans le nom : OK) ; le fichier `/etc/alarms.txt` au format de Korin (`snooze =` en
  tête, nom de 87 octets au plus), écrit puis publié sur `korin.clock.alarms` pour clockd, et relu quand clockd le
  publie ; le mode poche (cadran centré, ancres de la page Alarmes) ; le thème à chaud (`WEV_THEME`) ;
  `lang/en.txt`, `help_fr.md`, `help_en.md`, `app.txt` inchangés.
* Le fichier des alarmes est lu et écrit **octet pour octet comme l'asm et clockd** (`src/sys/common/alarms.c`,
  sans le modifier) : vérifié sur le PC (WSL) en lisant puis réécrivant les mêmes fichiers (celui de l'essai, un
  fichier vide, des fins de ligne CRLF, des cas limites : numéros en double ou négatifs, jours en mots entiers,
  noms trop longs en UTF-8, horodatages faux, autres blocs) avec `alarms.c` et `alarms.cpp` : sorties identiques, et
  la sortie de l'app relue par `alarms.c` redonne la même.

## Différences

* **Avec l'asm** :
  * au lancement, l'heure prend aussi la valeur gardée de `korin.clock.offset` (comme la barre de menus depuis le
    13/11) : sur le PC, busd la publie au démarrage d'après l'horloge CMOS, et l'Horloge montre la même heure que
    la barre (l'asm ne lisait que `clock.cfg` au lancement, puis le bus à chaque publication) ;
  * `korin.clock.offset` lu au plus sur 31 octets (l'asm ne bornait pas la longueur rendue par `bus_get`) ;
  * sans BusKit, rien n'est publié (l'asm appelait le kit quand même).
* **L'asm (donc ce port) avec Onyx** : pas d'onglets Monde, Minuteur, Chronomètre, pas de `--ring` ni `--missed`
  (c'est clockd qui sonne, avec sa carte), pas de bloc `[timer]` ni de clés inconnues gardées ; le fichier est
  `/etc/alarms.txt` (Onyx : `/apps/clock.app/alarms.txt`) avec `snooze =` en tête ; les jours se lisent sur
  leurs trois premières lettres ; un nom garde 87 octets du fichier (Onyx : 40 caractères ; le dialogue en permet
  40) ; l'heure vient d'un décalage réglé à la main ou par le bus (Onyx : l'horloge et le fuseau du noyau).

## Vérifié (Pi sous QEMU raspi4b, 13/11)

`tools/apps/compare.sh`, l'asm puis le C, avec un `alarms.txt` d'essai (alarmes répétées, d'une fois, manquée,
cassée) : onglet Horloge, +1 h, onglet Alarmes, choix d'une ligne, Modifier (dialogue), nom changé puis Entrée,
Désactiver, Suppr, Ajouter, en Givre et en Onyx Noir : **identiques au pixel** hors de l'heure (aiguilles, chiffres,
« dans 3 h 50 min »). Le cadran sans les aiguilles (bord, graduations, chiffres, axe) est identique au pixel.

## Ce qui manque

Rien par rapport à l'asm. Le son du PC (pas de pilote) : Essayer y reste muet.
