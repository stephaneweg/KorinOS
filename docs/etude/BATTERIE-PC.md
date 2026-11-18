# Batterie et énergie du PC : acpid (17/11/2026)

Demande de l'utilisateur : connaître le niveau de la batterie (pourcentage et temps restant estimé), être
averti quand elle arrive à la fin, et passer en économie d'énergie.

## Pourquoi ACPICA

Sur le ZBook, `\_SB.BAT0._BST` n'est pas un registre : c'est `BTST (0)`, qui appelle
`\_SB.PC00.LPCB.EC0.BTST` : mutex `BTMX` et `ECMX`, champs de l'EC (`BST`, `BPR`, `BRC`, `BPV`), le drapeau
`ECRG` posé par le `_REG` de l'EC, `GACS ()` pour le secteur ; `_STA` passe par `BSTA` et un SMI
(`SSMI (0xEA3A…)`, port `0xB2`). Décoder cela à la main ne vaudrait que pour ce BIOS. Le noyau prévoyait déjà
« l'AML d'ACPICA dans un service » (`src/kernel/arch/pc/acpi.c`) : c'est `acpid`, qui servira aussi plus tard
au couvercle, au bouton d'alimentation, à S3 et au thermique.

ACPICA 20260930 (Intel, BSD-3-Clause ou GPL-2.0 : prise sous BSD) n'est pas dans le dépôt :
`tools/acpica/build.sh` télécharge l'archive (empreinte SHA-256 fixée) dans `~/.cache/korin-acpica` et
construit `build/svc/x86_64/obj/libacpica.a` (interprète seul : ni iasl, ni débogueur, ni désassembleur).
Dans le dépôt : `tools/acpica/ackorin.h` (la plateforme : 64 bits, un seul fil, verrou global, bibliothèque C
d'ACPICA renommée `AcpiK*` pour ne pas heurter celle de korinrt) et `acenv.patch` (`acenv.h` choisit
`ackorin.h` avant `aclinux.h`, car gcc définit `__linux__` ; `acgccex.h` ne défait plus le renommage de
`strchr`). Compilé avec `-nostdinc` : aucun en-tête de glibc.

`tools/svc/build_c.sh` a trois nouvelles clés de `svc.conf` : `prebuild` (un script lancé avant),
`cflags` et `libs` (`@OUT@`, `@ARCH@` remplacés).

## Le noyau (src/kernel, PC)

* `IO_PORT` (0x4F) : 8, 16 ou 32 bits (`IO_PORT_READ8`…`WRITE32`, alignés), et tous les ports sauf ceux du
  noyau (PIC, PIT, CMOS, `0xCF8`-`0xCFF`, COM1, `0xE9`) ; l'i8042 reste à ps2kbd, en octets.
* `DEV_MAP` (0x40) : aussi la mémoire du micrologiciel (ACPI récupérable, ACPI NVS, services d'exécution
  de l'UEFI), **en cache** (comme Linux) : tables, GNVS et les autres régions `SystemMemory` de l'AML, en
  lecture et écriture. Jamais la mémoire de Korin.
* `ACPI_TABLE` (0x4D) : signature `RPTR`, l'adresse physique de la RSDP (8 octets), pour
  `AcpiOsGetRootPointer`.
* `POWER` (0x7F) : `POWER_PROFILE` (3), le profil d'énergie des cœurs à chaud : performance (EPP 0),
  équilibré (EPP 0x80), économie (EPP 0xC0 et plafond au niveau garanti de HWP : pas de turbo) ; sans HWP,
  `IA32_PERF_CTL` au turbo ou au garanti. Chaque cœur l'applique à sa minuterie suivante (pas d'IPI).
  `-1` le lit ; `E_NOSYS` avec `pstate=off` ou sans HWP ni EIST (QEMU). `src/kernel/arch/pc/pstate.c`.

## Le service acpid (src/svc/acpid)

| Fichier | Rôle |
|---|---|
| `osl.c` | la couche système d'ACPICA : `DEV_MAP` gardés (ACPICA mappe et démappe sans cesse ; pas de démappage dans le noyau), ports, ECAM de la MCFG pour la configuration PCI, TSC étalonné pour `AcpiOsStall`/`GetTimer`, sémaphores-compteurs (un seul fil), travaux différés dans la boucle, journal ligne par ligne |
| `acpi.c` | démarrage d'ACPICA, l'EC (ACPI 12.2 : ports du `_CRS` de PNP0C09, ou de l'ECDT, 0x62 / 0x66 sinon ; lecture 0x80, écriture 0x81), batteries (PNP0C0A : `_STA` bit 4, `_BIX` ou `_BIF`, `_BST`) et secteur (ACPI0003 : `_PSR`) ; mA convertis en mW par la tension |
| `acpid.c` | la boucle (lecture toutes les `poll` s), le pourcentage, le temps restant, les alertes, le profil, le bus, les requêtes |

**Démarrage d'ACPICA** : tables, espace de noms, objets ; mode ACPI, SCI et GPE (les événements, plus bas ;
`events = 0` : rien de cela, le mode n'est pas changé, tout est sondé). Méthodes d'initialisation : par défaut seulement `\_SB._INI` et les `_INI`
de l'EC, des batteries et des adaptateurs (les autres périphériques ont des pilotes Korin déjà lancés) ;
`init = all` dans `/etc/power.ini` : tous les `_STA`/`_INI`/`_REG`, comme Linux. Puis le gestionnaire de
l'EC, dont l'installation lance le `_REG` de l'EC (`ECRG = 1` sur le ZBook). **Mode souple**
(`AcpiGbl_EnableInterpreterSlack`) : une variable locale lue avant d'être écrite vaut 0, comme sous Windows ;
sur le ZBook, le `_REG` de l'EC passe par `\_SB.PC00.TM03`, qui le fait (sans le mode souple : l'erreur
`AE_AML_UNINITIALIZED_LOCAL` arrête `ECRI`, après `ECRG` heureusement).

**Pourcentage** : capacité restante / capacité à la dernière charge complète (somme des batteries).
**Temps restant** : puissance de `_BST` lissée (moyenne glissante d'environ une minute), ou, si le
micrologiciel ne la donne pas, la pente de la capacité après 2 min ; en charge, le temps jusqu'à la charge
complète. Une nouvelle phase (branché, débranché) oublie la moyenne.

**Alertes** (notifyd, une fois par décharge ; réarmées au secteur ou 3 % au-dessus du seuil) : `warn` (20 %)
« Batterie faible », `critical` (10 %) « Batterie très faible » ; à `shutdown` (5 %, 0 : jamais) « Extinction
imminente », puis extinction propre par init2 (`INIT_OP_REBOOT`, éteindre) après `delay` (60 s) si le
secteur n'est toujours pas branché ; brancher le secteur l'annule. acpid démarre avant la session : une
alerte que notifyd n'a pas prise attend la lecture suivante.

**Profil d'énergie** en automatique : `ac` sur secteur (`boot` : celui du démarrage, `pstate=` de
`boot.cfg`), `battery` sur batterie (équilibré), `low` sous `saver` % (économie, avec une notification) ;
ou un profil fixe (`profile = performance|balanced|saver`, gardé dans `/etc/power.ini`).

**Bus** : `korin.power.battery` (docs/BUS.md), « `<pct> <min> <secteur> <état> <profil> <auto>` ».

**Requêtes** (service `acpid`) : `1` l'état (lignes `clé=valeur`), `2` le profil (`a` : -1 automatique,
0..2), `3` relire `/etc/power.ini`.

Lancé par init2 (`skel/pc/etc/services.txt`) ; sans batterie ni adaptateur décrits (ordinateur de bureau),
il publie « pas de batterie » et reste à répondre.

## Les événements : SCI (17/11, le soir)

* **SCI** : ACPICA installe son gestionnaire sur l'IRQ ISA de la FADT (`SCI_INT`, 9 sur le ZBook et QEMU) ;
  `acpid` la traduit en GSI par les remplacements de la MADT, la lie au canal de son service (`IRQ_BIND`,
  partagée) : chaque SCI arrive dans sa boucle comme `SVC_OP_SIGNAL` (bit 2) ; le gestionnaire d'ACPICA, les
  travaux différés (Notify, requêtes de l'EC), puis `IRQ_ACK`. Le noyau programme la ligne du SCI sur
  niveau, active basse, quand la MADT la dit « conforme » (ACPI 5.2.9 ; `src/kernel/arch/pc/apic.c`).
  Garde : plus de 500 SCI par seconde deux secondes de suite, toutes les GPE sont coupées sauf celle de l'EC.
* **Mode ACPI** (`ACPI_ENABLE` par `SMI_CMD` s'il ne l'est pas), événements fixes, GPE : `AcpiUpdateAllGpes`
  active celles qui ont une méthode `_Lxx` / `_Exx` (comme Linux).
* **EC** : sa GPE (`_GPE`, 0x6E sur le ZBook) a un gestionnaire : tant que `SCI_EVT` (bit 5 de l'état) est
  levé, `QR_EC` (0x84) donne le numéro de la requête et la méthode `_Qxx` de l'EC est lancée (secteur
  branché, batterie, boutons, touches de fonction de HP…).
* **Notify** (gestionnaire global) : sur une batterie ou un adaptateur, tout est relu aussitôt (le secteur
  branché se voit en une fraction de seconde, plus en 10 s) ; sur le couvercle (PNP0C0D), `_LID` relu ; sur
  le bouton d'alimentation (PNP0C0C) ou de mise en veille (PNP0C0E), 0x80 = appui ; les autres au journal
  (les 24 premiers).
* **Bouton d'alimentation** (fixe, `PWRBTN_STS`, ou PNP0C0C) : `power_button = dialog` (défaut) publie
  `korin.power.button` (« power <n> », non gardé) : le panel (`src/sys/panel`) ouvre `/sys/shutdown`, le
  même dialogue que son bouton ; sans bureau (pas de notifyd), extinction propre ; `shutdown` : extinction
  propre ; `nothing`. Appui long de 4 s : le matériel coupe, comme toujours.
* **Couvercle** : `korin.power.lid` (`open` / `closed`, gardé) ; aussi sondé à chaque lecture. Rien d'autre
  tant qu'il n'y a pas de veille.
* **Bouton de mise en veille** : `sleep_button` (`nothing` par défaut : pas encore de veille).

## Découvert, rien de propre à une machine (18/11)

Demande de l'utilisateur : la gestion de la batterie la plus générique possible, sans rien supposer du
ZBook ; chaque fonction s'allume parce que le matériel est découvert :

| Fonction | Découverte | Sans elle |
|---|---|---|
| Batteries | `PNP0C0A` (ACPICA), présente si `_STA` bit 4 ; `_BIX`, sinon `_BIF` ; plusieurs : additionnées | pas d'icône, « Pas de batterie (secteur) » |
| Secteur | `ACPI0003`, `_PSR` | déduit de l'état de la batterie |
| Unités | `_BIX` / `_BIF` : mWh ou mAh ; mAh convertis par la tension de conception (la même pour la capacité pleine et restante), une tension nominale de 11,1 V si elle est inconnue (le % et le temps restent justes, les Wh sont estimés, dit au journal) | — |
| EC | ECDT, sinon `PNP0C09` et ses ports du `_CRS` ; sa GPE par `_GPE` | l'AML qui lit l'EC échoue (journal) |
| Événements | `SCI_INT` de la FADT, MADT ; boutons fixes (drapeaux de la FADT) ou `PNP0C0C` / `PNP0C0E` ; couvercles `PNP0C0D` ; GPE avec `_Lxx` / `_Exx` | tout est sondé (`poll`) |
| Profils | CPUID : HWP, sinon EIST (noyau, `POWER_PROFILE`) | pas de ligne Profil (menu de la barre, Réglages) |

`acpid` dit ce qu'il a trouvé dans la réponse à `ACPID_OP_GET` (`profiles`, `lids`, `power_button`,
`sleep_button`, `ec`, `events`, avec `batteries` et `adapters`) ; la barre et Réglages s'en servent.
Restent propres à un constructeur et non gérés : les batteries « Smart Battery » (`ACPI0002`, SBS par le
SMBus de l'EC : d'anciens portables) et les interfaces WMI (seuils de charge…).

## Ce que voit l'utilisateur

* **Barre du haut** (`src/sys/menubar`) : à gauche de l'heure, le pourcentage et une batterie dessinée
  (niveau aux couleurs du thème : alerte sous 20 %, danger sous 10 % sans secteur ; un éclair en charge) ;
  pas d'icône sans batterie (le Pi, un PC de bureau). Un clic : l'état (« Batterie : 28 %, 1 h 02
  restantes », « Sur batterie »), le profil d'énergie (Automatique, Performance, Équilibré, Économie,
  coché, choisi chez acpid ; seulement si le processeur a des profils) et « Réglages d'énergie… ».
* **Réglages, volet Énergie** (`src/apps/control/applets/power.cpp`, montré si `/sys/svc/acpid` existe) :
  une carte avec ce qui est découvert : le profil (avec HWP ou EIST), « Économie automatique sous »
  (`saver`, avec batterie et profils), « Extinction automatique à » (`shutdown`, avec batterie), « Bouton
  d'alimentation » (`power_button` : Demander, Éteindre, Ne rien faire ; s'il y en a un) ; dessous l'état,
  relu toutes les 5 s (jauge, temps, secteur, usure, cycles, modèle). Le profil passe par acpid ; le reste
  est écrit dans `/etc/power.ini` après l'élévation, puis `ACPID_OP_RELOAD`.
* **Commande `battery`** : niveau, temps restant, secteur, puissance, capacité et usure, modèle, cycles,
  seuils, profil ; `battery -r` (valeurs brutes), `battery profile auto|performance|balanced|saver`,
  `battery reload`.
* **`/etc/power.ini`** (`skel/pc/etc/power.ini`) : les seuils, les profils, `poll`, `init`.

## Essais

Tout ce qu'il faut pour les refaire ailleurs est dans le dépôt (`tools/acpica/`, dans WSL ou le cloud après
`tools/cloud/setup.sh`) :

| Outil | Rôle |
|---|---|
| `build.sh` | télécharge ACPICA 20260930 (somme SHA-256 vérifiée) dans `~/.cache/korin-acpica`, construit `libacpica.a` (lancé par la construction d'acpid : `prebuild` de `src/svc/acpid/svc.conf`) |
| `fakebat.sh` | compile `fakebat.asl` : `build/pc/fakebat.aml` (en décharge) et `fakebat_ac.aml` (en charge) |
| `qemu_test.sh [bat\|ac\|none]` | refait l'image `build/pc/acpitest.img` depuis `build/pc/sysroot` et lance QEMU avec ou sans fausse batterie ; étapes de `qemu_nvme.py` (`--step shot:nom`, `hmp:system_powerdown`, clics) ; `APP=…` ajoute un programme à la session de l'image |
| `acpiexec.sh` | exécute des méthodes de l'AML dans acpiexec, sur les tables de `zbook/` (défaut) ou d'un autre dossier (`-d`) ; `-es` : le mode souple ; `DSL=1` : le DSDT désassemblé |
| `zbook/` | les tables du ZBook Fury 17 G8 (DSDT, 25 SSDT, FADT, RSDT ; copiées le 17/11) |
| `dump_tables.ps1` | copie celles d'une autre machine sous Windows (registre, `HKLM\HARDWARE\ACPI`) |

* **QEMU q35** (sans batterie) : ACPICA charge les tables (mode ACPI déjà actif), « ni batterie ni adaptateur
  décrits ».
* **QEMU avec une fausse batterie** : `tools/acpica/fakebat.asl` (SSDT : PNP0C0A, ACPI0003 ; 21 %, 15 W,
  chaque `_BST` retire 1,5 Wh ; `FAKE = 1` : branché, en charge), compilé par `sh tools/acpica/fakebat.sh`
  (iasl construit dans le cache), donné à QEMU par `--qemu=-acpitable --qemu=file=build/pc/fakebat.aml`
  (`qemu_test.sh bat`). Vérifié le 17/11 : lecture (19 %, 1 h 02), icône et menu de la barre, commande
  `battery` (en charge : « pleine dans 3 h 48 », 15 W, usure 11 %), alerte à 4 %, puis **extinction propre**
  par init2 60 s plus tard. Le profil n'est pas essayé (QEMU n'a ni HWP ni EIST : `E_NOSYS`).
* **Tables du ZBook dans acpiexec** (DSDT et 25 SSDT copiés du registre de Windows,
  `tools/acpica/zbook`, `sh tools/acpica/acpiexec.sh -es`) : `_STA`, `_BIX`, `_BST`, `_PSR` s'exécutent ; l'EC y est simulé
  (zéros), d'où « absente » et des valeurs inconnues ; en mode souple, plus aucune erreur dans le `_REG` de
  l'EC.
* **Bouton d'alimentation sous QEMU** (`system_powerdown` du moniteur : événement fixe) : « SCI : ligne 9 »,
  « bouton : power », le dialogue d'extinction s'ouvre (17/11 le soir). Le GPE de l'EC, les Notify et le
  couvercle n'ont pas d'équivalent sous QEMU : pas essayés.
* **Volet Énergie sous QEMU** (18/11) : avec la fausse batterie, la carte montre l'extinction et le bouton
  (pas de profil : QEMU n'a ni HWP ni EIST), l'état se met à jour ; sans batterie, le bouton seul et « Pas
  de batterie (secteur) », pas d'icône dans la barre ; menu de la barre : « Réglages d'énergie… » sans les
  profils ; extinction à 10 % appliquée (carte d'élévation, « Enregistré. ») : acpid relit le fichier et
  annonce l'extinction à 8 %.
* **Pas encore essayé sur le ZBook.**

## Marche à suivre sur le ZBook

1. Construire et installer comme d'habitude (le **noyau** aussi : `IO_PORT`, `DEV_MAP`, `RPTR`,
   `POWER_PROFILE`).
2. Au démarrage, `journal` doit dire : « acpid : ACPICA 20260930 : espace de noms chargé, mode …, EC
   \_SB.PC00.LPCB.EC0 (ports 62 / 66), 1 batterie(s), 1 adaptateur(s) », puis « batterie 0 : … mWh neuve … »
   et « prêt : batterie NN %, … ». Le pourcentage doit être proche de celui de Windows.
3. `battery` : niveau, temps restant, puissance ; débrancher le secteur : en moins de 10 s, « décharge » et un
   temps restant (la première minute est moins juste) ; le profil passe à « équilibré » (journal : « CPU :
   profil d'énergie »).
4. L'icône de la barre ; le menu (clic) ; choisir « Économie », puis `cpufreq charge` : pas de turbo.
5. Si la batterie reste inconnue : `battery -r` et le journal (lignes « acpid : … », « EC : pas de réponse »,
   « port … refusé », « DEV_MAP … refusé ») ; essayer `init = all` dans `/etc/power.ini` puis redémarrer.
6. Événements : le journal doit dire « événements : bouton d'alimentation …, 1 couvercle(s), GPE de l'EC
   servie » et « SCI : ligne 9 ». Brancher, débrancher le secteur : l'icône change aussitôt (sinon : les
   lignes « EC : requête … », « Notify (…) : non servi »). Appuyer brièvement sur le bouton d'alimentation :
   le dialogue d'extinction. Fermer, rouvrir le couvercle : « couvercle : fermé / ouvert ».
7. Si quelque chose se dérègle après le passage en mode ACPI (ventilateur, touches, interruptions sans fin :
   « SCI : trop d'interruptions ») : `events = 0` dans `/etc/power.ini`, redémarrer, et le journal.

## Reste à faire

* ~~S3 (veille)~~ : **remplacé le 18/11/2026** par le plan ci-dessous (décision de l'utilisateur : le système
  démarre en 0,46 s ; S3 demanderait de rendormir et réveiller chaque pilote, et l'écran GOP ne revient pas).
  S3 reste une idée pour plus tard si ce plan ne suffit pas.
* Le thermique (`_TZ` : refroidissement passif) : l'EC du ZBook gère seul ses ventilateurs.
* La luminosité de l'écran en économie (`_BCM` de l'ACPI ou le rétroéclairage du GPU).
* Les seuils des alertes (`warn`, `critical`) et les profils sur secteur ou batterie dans le volet Énergie
  (aujourd'hui dans `/etc/power.ini`).
* Le mode poche de la barre (l'icône y est dessinée comme en mode normal).

## Plan : couvercle, arrêt automatique, session (décidé le 18/11/2026)

Pas de vraie veille. Trois choses, dans cet ordre :

1. **Couvercle fermé = économie d'énergie** (pas de veille) : `korin.power.lid = closed` fait éteindre l'écran
   (rétroéclairage : `_BCM` de l'ACPI ou registre du GPU Intel ; à défaut l'écran noir et la peinture arrêtée par
   Elegant), passer le profil d'énergie en économie (HWP), ralentir les sondes et les minuteries des services, et
   mettre le Wi-Fi en économie (power save d'iwl). Le son est coupé. À l'ouverture, tout revient tel quel
   (écran de verrouillage si un mot de passe est réglé, avec le multi-utilisateur). Réglage `lid = save | nothing |
   shutdown` dans `/etc/power.ini` et le volet Énergie ; `save` par défaut.
2. **Arrêt automatique** dans deux cas : après **30 minutes couvercle fermé** (`lid_shutdown = 30`, en minutes,
   0 = jamais) et quand **la batterie passe sous un seuil** (`battery_shutdown = 5`, en %, réglable ; remplace
   l'extinction à 5 % d'aujourd'hui, qui coupe sans enregistrer). Les deux réglables dans le volet Énergie ; un
   avis prévient une minute avant (annulable si l'utilisateur est là).

   **Un seul chemin d'arrêt** (décision du 18/11) : l'arrêt manuel (menu, bouton d'alimentation), l'arrêt après
   30 minutes couvercle fermé et l'arrêt sur batterie faible passent tous par `/sys/shutdown` avec une raison
   (`manual`, `lid`, `battery`, notée au journal) ; ce chemin enregistre toujours la session (point 3) avant
   d'arrêter les services, et le démarrage suivant la rouvre de la même façon, quelle que soit la raison.

   **Disque qui écrit (décision du 18/11)** : l'arrêt après 30 minutes couvercle fermé **attend tant que les disques
   écrivent** (construction, copie, téléchargement, installation de `pkgd`). acpid lit un compteur d'octets écrits
   par périphérique bloc (à ajouter à `blkserv`, lu par une op d'information) : s'il a bougé dans les 2 dernières
   minutes, le délai repart pour 5 minutes, au journal (« arrêt retardé : écritures sur blk.nvme0 »). L'arrêt sur
   batterie faible, lui, n'attend pas (une coupure de courant serait pire) : il laisse seulement finir les écritures
   en cours et vide les caches avant d'arrêter.
3. **Session enregistrée à l'arrêt, rouverte au démarrage** (comme la reprise de macOS) :
   * à l'arrêt (`/sys/shutdown`), Elegant envoie à chaque app un événement « enregistrer l'état » ; l'app répond
     par quelques lignes (documents ouverts, onglets de Jet, dossier de Fichiers, dossier du terminal), gardées
     avec la place et la taille de ses fenêtres dans `HOME:/etc/session.ini` ;
   * au démarrage (après la connexion), la session relance les apps de ce fichier avec leur état
     (`--restore <état>`) ; une app qui ne sait pas le faire est rouverte vide, à sa place ;
   * API dans UIKit/AppKit (un événement et deux fonctions : écrire l'état, le relire), ajoutée à la fin des
     tables `.abi` ; les apps C++/C#/BASIC l'ont par leurs liaisons ;
   * documents non enregistrés : enregistrés à part (copie de travail dans `HOME:/etc/session/`), jamais perdus,
     rouverts « modifiés » ;
   * réglage « Rouvrir les apps au démarrage » (oui par défaut) dans le volet Démarrage ou Énergie.

Ordre : 1 et 2 dans acpid, Elegant et le volet Énergie (petit) ; 3 dans UIKit, Elegant et la session, puis app par
app (Jet, Fichiers, Terminal, les éditeurs d'abord).

