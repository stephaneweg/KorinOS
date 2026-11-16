# Dispositions du clavier

Le service **hidmanager** (`src/svc/hidmanager/hidmanager.c`, la version de la carte ; `hidmanager.asm` reste pour le
selfhost) traduit chaque touche (code HID de la touche physique) en caractère Unicode, selon la disposition choisie :

* `keyb=NOM` dans `/etc/options.txt` (lu au démarrage), ou le panneau de configuration (volet **Région et
  langue**, liste « Disposition du clavier » : les fichiers `.txt` de `/res/keyb`, sous leur nom lisible
  **Belge**, **Français**, **US** ; un autre fichier garde son nom), ou l'assistant de la première mise en route
  (`src/sys/setup` : la Belgique choisit `BE`) ; les deux appellent `HID_OP_KEYMAP` ;
* sans `keyb=` : l'AZERTY de France intégré à hidmanager (sans touche morte).

Dispositions fournies (`skel/sd0/res/keyb`) :

| Fichier | Disposition | Source |
|---|---|---|
| `BE.txt` | AZERTY belge (« Belge (période) » de Windows) | `kbdbe.dll` 10.0.25393.1, fichier KLC de <https://kbdlayout.info/kbdbe> |
| `FR.txt` | AZERTY de France (« Français » de Windows) | `kbdfr.dll` 10.0.25393.1, fichier KLC de <https://kbdlayout.info/kbdfr> |
| `US.txt` | QWERTY des États-Unis | |
| `compose.tab` | compositions des touches mortes, communes | les tables `DEADKEY` des deux KLC (plus Ÿ) |

## `/res/keyb/<NOM>.txt`

Texte UTF-8, une touche par ligne, `#` pour un commentaire :

```
<code HID hexa>  <normal>  [<Maj>  [<AltGr>]]
```

* le code HID est celui de la touche physique (page 7 de la spécification USB HID : `04` = A du QWERTY, `1E` = 1,
  `28` = Entrée, `64` = touche ISO `<>` à droite de Maj gauche) ; la touche à gauche d'Entrée d'un clavier ISO est
  `32`, celle au-dessus d'Entrée d'un clavier ANSI `31` : les fichiers donnent les deux, avec les mêmes caractères ;
* chaque colonne : le caractère tel quel (UTF-8, jusqu'à U+FFFF), ou `U+XXXX`, ou `-` (aucun) ; `#` et `-` comme
  caractères s'écrivent `U+0023` et `U+002D` ;
* **touche morte** : `dead:X` (X : un caractère ou `U+XXXX`), dans n'importe quelle colonne. Exemple, la touche à
  droite du P du belge : `2F  dead:^ dead:¨ [` ;
* écrire les trois colonnes avant un commentaire de fin de ligne (sinon `#` serait lu comme un caractère) ;
* AltGr (Alt droite) : la 3e colonne ; si elle est vide (`-`), la touche donne sa colonne normale ou Maj ;
* Ctrl : aucun caractère (les programmes lisent le code HID) ;
* un ancien fichier, sans `dead:`, se lit comme avant. L'asm (`hidmanager.asm`) lit `dead:X` comme `X` : la touche y
  donne son caractère tout de suite, sans composition.

## Touches mortes et `compose.tab`

Quand une disposition a des touches mortes, hidmanager lit `/res/keyb/compose.tab` (à chaque chargement d'une
disposition). Une composition par ligne, `#` pour un commentaire :

```
<morte>  <touche>  <résultat>        par ex.   ^  e  ê
```

(Extension `.tab`, pas `.txt` : le panneau de configuration et setup prennent chaque `.txt` du dossier pour une
disposition.) Fichier absent : aucune composition (la morte, puis la touche).

L'appui d'une touche morte ne donne aucun caractère (événement avec le caractère 0) et la garde en attente ; puis, à
l'appui suivant :

| Touche suivante | Résultat |
|---|---|
| une composition de `compose.tab` (`^` puis `e`) | le caractère composé (`ê`), sur l'événement de cette touche |
| Espace | la morte seule (`^`) |
| une autre touche morte | la composition des deux si `compose.tab` l'a, sinon les deux (`^^`) |
| un autre caractère (`^` puis `x`) | la morte, sur un événement de plus (copie de l'appui), puis la touche (`^x`) |
| un caractère de commande (Entrée, Échap, Retour arrière, Tab) | la touche seule : la morte est abandonnée |
| Maj, AltGr, Ctrl… seuls (codes `E0`–`E7`) | rien : la morte reste en attente (`^` puis Maj+E : `Ê`) |
| une autre touche sans caractère (Ctrl+touche, flèches, F1…) | la morte est abandonnée |

Le relâchement d'une touche garde son caractère de base (0 pour une morte). Les événements déjà traduits par un pilote
virtuel (vncd, rdpd : bit 31 des modificateurs) ne passent pas par la composition.

Belge : `^ ¨` (à droite du P, normal et Maj), `´` (AltGr sur ù), `` ` `` (AltGr sur µ), `~` (AltGr sur =).
Français : `^ ¨` (à droite du P), `~` (AltGr sur é), `` ` `` (AltGr sur è).

## Terminal

Le Terminal envoie `ESC` devant un caractère tapé avec **Alt gauche** seulement (Meta de bash) : Alt droite est AltGr,
le caractère de la disposition passe tel quel (`# @ { [ | \` des AZERTY).

## Moniteur du noyau du PC

Le moniteur des premiers démarrages du PC lit le clavier PS/2 lui-même (`src/kernel/arch/pc/ps2.c`, jeu 1, Latin-1) :
AZERTY de France par défaut, **AZERTY belge** avec `keyb=BE` dans les options du noyau (`\korin\boot.cfg`,
`cmdline = keyb=BE`, voir `docs/etude/PC-PILOTES.md`, « Menu de démarrage ») ; le belge y a les mêmes touches mortes et
compositions (sans € ni Ÿ, hors de Latin-1).
