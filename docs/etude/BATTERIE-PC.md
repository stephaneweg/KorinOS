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

**Démarrage d'ACPICA** : tables, espace de noms, objets ; **sans changer de mode** (ni `ACPI_ENABLE`, ni SCI,
ni GPE : rien par interruption). Méthodes d'initialisation : par défaut seulement `\_SB._INI` et les `_INI`
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

## Ce que voit l'utilisateur

* **Barre du haut** (`src/sys/menubar`) : à gauche de l'heure, le pourcentage et une batterie dessinée
  (niveau aux couleurs du thème : alerte sous 20 %, danger sous 10 % sans secteur ; un éclair en charge) ;
  pas d'icône sans batterie (le Pi, un PC de bureau). Un clic : l'état (« Batterie : 28 %, 1 h 02
  restantes », « Sur batterie ») et le profil d'énergie (Automatique, Performance, Équilibré, Économie,
  coché), choisi chez acpid.
* **Commande `battery`** : niveau, temps restant, secteur, puissance, capacité et usure, modèle, cycles,
  seuils, profil ; `battery -r` (valeurs brutes), `battery profile auto|performance|balanced|saver`,
  `battery reload`.
* **`/etc/power.ini`** (`skel/pc/etc/power.ini`) : les seuils, les profils, `poll`, `init`.

## Essais

* **QEMU q35** (sans batterie) : ACPICA charge les tables (mode ACPI déjà actif), « ni batterie ni adaptateur
  décrits ».
* **QEMU avec une fausse batterie** : `tools/acpica/fakebat.asl` (SSDT : PNP0C0A, ACPI0003 ; 21 %, 15 W,
  chaque `_BST` retire 1,5 Wh ; `FAKE = 1` : branché, en charge), compilé par `sh tools/acpica/fakebat.sh`
  (iasl construit dans le cache), donné à QEMU par `--qemu=-acpitable --qemu=file=build/pc/fakebat.aml`
  (`qemu_nvme.py`). Vérifié le 17/11 : lecture (19 %, 1 h 02), icône et menu de la barre, commande
  `battery` (en charge : « pleine dans 3 h 48 », 15 W, usure 11 %), alerte à 4 %, puis **extinction propre**
  par init2 60 s plus tard. Le profil n'est pas essayé (QEMU n'a ni HWP ni EIST : `E_NOSYS`).
* **Tables du ZBook dans acpiexec** (DSDT et 25 SSDT copiés du registre de Windows,
  `HKLM\HARDWARE\ACPI`, hors du dépôt) : `_STA`, `_BIX`, `_BST`, `_PSR` s'exécutent ; l'EC y est simulé
  (zéros), d'où « absente » et des valeurs inconnues ; en mode souple, plus aucune erreur dans le `_REG` de
  l'EC.
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

## Reste à faire

* SCI et GPE : les événements de l'EC (`_Qxx` : secteur branché à l'instant, batterie changée), le couvercle,
  le bouton d'alimentation, le thermique ; puis S3 (veille).
* Un volet « Énergie » dans Réglages (seuils, profils) ; la luminosité de l'écran en économie.
* Le mode poche de la barre (l'icône y est dessinée comme en mode normal).
