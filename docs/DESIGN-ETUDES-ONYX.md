# Études de design : ce qu'on reprend d'Onyx

Quatre études du 5/11/2026 : Wi-Fi, Fichiers, mode repos, mode poche. Pour chacune, ce qu'Onyx proposait, ce que Korin a aujourd'hui, et comment le faire entrer dans la direction **Givre calme** ([DESIGN-PROPOSAL.md](DESIGN-PROPOSAL.md)).

**Niveau de preuve.** Lecture du code des deux dépôts et des captures d'Onyx (`C:\Projects\Onyx\screenshots`, `docs/compact-shell/real`), et des captures de Korin dans `design-proposal/`. Rien n'a été compilé ni essayé, ni sous QEMU ni sur le PocketTerm. Aucune maquette n'a été dessinée : les croquis sont en texte, à 800x600. Les gains d'énergie de l'étude 3 sont des ordres de grandeur à mesurer, pas des mesures.

**Règles de Givre calme appliquées partout.** La couleur vit dans les icônes ; l'accent marque seulement le choix ou l'action principale ; surfaces opaques ; contrôles de 32 px, lignes de 30 ; texte 14, petit texte 13 ; réponse sous 50 ms, aucun fondu ; `FRAME_CAP_W` libre en haut à droite. Pour le tactile du PocketTerm, la cible est de 32 px au moins.

**Effort**, comme dans DESIGN-PROPOSAL : S moins d'un jour, M quelques jours, L une à deux semaines.

---

## 1. Wi-Fi : wpaconf d'Onyx ou ce que Korin a déjà

### Constat

**Onyx, `wpaconf`** ([capture](../../Onyx/screenshots/wpaconf.png), `user/Apps/wpaconf/main.cpp`) : un formulaire dans le panneau de configuration. SSID dans une liste déroulante avec un bouton Scan, mot de passe, case « Show password », puis trois champs libres : Country, Proto, Key mgmt. Trois boutons : Save, Save & Reboot, Reload. Une ligne d'état en bas, et à droite un texte grisé qui renvoie au menu Wi-Fi de la barre. C'est un éditeur de `wpa_supplicant.conf` présenté en formulaire. Le vrai geste de tous les jours était déjà dans le menu Wi-Fi d'Onyx ([capture](../../Onyx/screenshots/wifimenu.png)).

**Korin a trois surfaces**, toutes branchées sur le bus (`korin.net.wifi`, `korin.net.state`) :

| Surface | Fichier | Ce qu'elle montre |
|---|---|---|
| Menu Wi-Fi de la barre | `src/sys/wifimenu/wifimenu.asm` | 300 px, lignes de 26, 8 réseaux au plus, barres de signal, cadenas, coche. Un clic rejoint aussitôt un réseau connu ou ouvert ; sinon la ligne se déplie avec un champ de clé et « Se connecter ». En bas, « Paramètres Wi-Fi... » |
| Applet Wi-Fi de Réglages | `src/apps/control/applets/wifi.inc` | État (réseau, adresse, passerelle, signal, Déconnecter), réseaux visibles (6, lignes de 24, dépliables), réseaux connus (5, priorité − / +, Oublier), Actualiser en bas. Balayage et connexion dans un thread |
| Étape Wi-Fi de Setup | `src/sys/setup/main.cpp` (l. 363, 740) | `ui_list` de 210 px avec les noms seuls, champ mot de passe, Connecter, Actualiser, case « Afficher le mot de passe », ligne d'état |

Le pays du Wi-Fi est dans l'applet Clavier et région (« Pays (Wi-Fi) », 8 pays, `applets/keyboard.inc` l. 229 à 240) et dans l'étape Région de Setup.

### Comparaison

| Critère | wpaconf (Onyx) | Korin | Le meilleur |
|---|---|---|---|
| Rejoindre un réseau proche | Scan, ouvrir la liste, choisir, taper la clé, Save | un clic, ou un clic et la clé | **Korin** |
| Plusieurs réseaux | ajoutés sans être montrés | liste des connus, priorité, Oublier | **Korin** |
| Retour d'état | une ligne de texte | adresse, passerelle, signal, mis à jour par le bus | **Korin** |
| Vocabulaire | Proto, Key mgmt en champs libres | aucun terme de wpa_supplicant | **Korin** |
| Appliquer | Save & Reboot reste proposé | immédiat | **Korin** |
| Voir la clé tapée | case « Show password » | seulement dans Setup ; ni dans l'applet ni dans le menu | **Onyx** |
| Réseau caché ou absent | SSID libre dans la liste déroulante | aucun champ pour taper un nom | **Onyx** |
| Expliquer le réseau choisi | « WPA2, -61 dBm -- type the password », WEP refusé en clair | cadenas seul | **Onyx**, en partie |
| Pays | sur la même page | dans Clavier et région, sans lien depuis le Wi-Fi | **Onyx**, en partie |

**Verdict : Korin est déjà mieux pour l'essentiel.** Le modèle « liste, clic, clé dans la ligne » est le bon, et wpaconf ne doit pas être porté. Il faut lui prendre quatre petites choses : voir la clé, un réseau tapé à la main, une phrase sur la sécurité, et un lien vers le pays.

### Problèmes concrets de Korin

1. **Le champ de clé de l'applet est dessiné à la main** (`wifi.inc` l. 488 à 519) : des puces, 24 au plus, pas de curseur mobile, pas de collage, et il est en `CREUX` alors que Givre met les champs en `SURFACE`. Une clé de 63 caractères ne se relit pas.
2. **Lignes de 24 px dans l'applet**, 26 dans le menu. Givre fixe 30, et 32 pour le doigt.
3. **6 réseaux visibles au plus, sans défilement** (`WF_VIS`). Dans un immeuble, le réseau voulu peut ne pas apparaître.
4. **Trois rendus pour la même ligne.** Setup n'a ni signal ni cadenas, et son champ de mot de passe reste affiché pour un réseau ouvert.
5. **Messages techniques** : « La puce Wi-Fi ne répond pas (ou QEMU) : voir le journal. »
6. **Réseau caché** : le pilote n'essaie les réseaux connus sans les voir que si le balayage est vide (`wifi.asm` l. 1077, `mark_all`). Un SSID caché ajouté à la main ne serait donc pas rejoint dans un lieu où d'autres réseaux sont visibles.

### Propositions

**A. Retouches de l'applet seulement.** Champ de clé UIKit (`ui_textbox`, `UTF_PASSWORD`) avec un bouton Voir, lignes de 30, défilement, phrase sur la sécurité. Effort S à M. Setup et le menu restent différents.

**B. Une seule ligne de réseau partout (recommandée).** A, plus le même modèle de ligne dans les trois surfaces, une entrée « Autre réseau… », un lien vers le pays, et un petit ajout au pilote pour les réseaux cachés.

**C. Porter wpaconf.** Non : il ajoute du jargon et un redémarrage.

#### La ligne de réseau commune (B)

```
 ▂▄▆█  Maison                                  sécurisé   ✓
 ▂▄▆_  Voisin-5G                               sécurisé
 ▂▄__  FreeWifi                                ouvert
```

* 30 px dans l'applet et dans Setup ; 30 aussi dans le menu, qui passe de 8 à 7 lignes à 800x600.
* Barres en `TEXTE1` sur `LIGNE_FORTE`, nom en 14 `TEXTE1`, « sécurisé » ou « ouvert » en 13 `TEXTE3` à la place du cadenas seul. Coche du réseau courant en `ACCENT`.
* Ligne choisie : boîte `ACCENT_CLAIR` de rayon 6 (règle des listes de Givre).
* Ligne dépliée, pour un réseau sécurisé et inconnu :

```
 ┌───────────────────────────────────────────────────────────┐
 │ ▂▄__  Livebox-1280                       WPA2, signal faible│
 │       Clé  [••••••••••••••          ] [Voir]  [Se connecter]│
 │       8 caractères au moins.                                │
 └───────────────────────────────────────────────────────────┘
```

  « Se connecter » est le bouton principal (`ACCENT`), seul de la vue. La phrase sous le champ devient l'erreur après un échec (« Clé refusée. »), en `DANGER`. Un réseau WEP affiche « WEP n'est pas pris en charge » à la place du champ.

#### L'applet Wi-Fi (Réglages, B)

```
 Wi-Fi
 ┌─────────────────────────────────────────────────────────┐
 │ Connecté à « Maison »                     [Déconnecter] │
 │ 192.168.0.16 · passerelle 192.168.0.1 · signal bon      │
 └─────────────────────────────────────────────────────────┘
 Réseaux visibles                                Actualiser
   (lignes de 30, la liste défile, Recherche... à droite)
   +  Autre réseau…
 Réseaux connus
   Maison            1er    [↑] [↓]   Oublier
   Telephone         2e     [↑] [↓]   Oublier
 Pays du Wi-Fi : France — Clavier et région ›
```

* Le signal en mots (bon, moyen, faible), les dBm dans l'infobulle : le chiffre seul ne parle pas.
* « Autre réseau… » ouvre une ligne dépliée avec Nom, Sécurité (liste déroulante : WPA2/WPA3 personnel, ouvert), Clé, Se connecter. Il remplace le SSID libre de wpaconf.
* Priorité en flèches et rang, plutôt que « priorité 3 » et − / +.
* « La puce Wi-Fi ne répond pas » devient « Wi-Fi indisponible sur cet appareil », le détail allant au journal.

**Setup** prend la même ligne (signal, sécurité, dépliage) dans sa vue dessinée, comme sa liste des paquets (`plist_paint`), et la case « Afficher le mot de passe » devient le bouton Voir.

**Le menu Wi-Fi** reçoit Voir dans sa ligne dépliée et « Autre réseau… » au-dessus de « Paramètres Wi-Fi... ».

### Ce que ça coûte

| Changement | Fichiers | Effort |
|---|---|---|
| Champ de clé UIKit, Voir, lignes de 30, défilement, textes | `src/apps/control/applets/wifi.inc` | M (asm, dessin à la main) |
| Même ligne et Voir dans le menu | `src/sys/wifimenu/wifimenu.asm`, `lang/en.txt` | S |
| Ligne dessinée dans Setup | `src/sys/setup/main.cpp`, `lang/en.txt` | S |
| Autre réseau… : SSID caché essayé même quand d'autres réseaux sont visibles (sonde dirigée, ou drapeau `hidden=1` dans `wifi.txt`) | `src/svc/wifi/wifi.asm`, `src/kits/netkit/netkit.asm`, `src/inc/netd.inc` | S à M |
| Lien vers Clavier et région | `wifi.inc` (l'applet sait déjà ouvrir une autre applet) | S |

Total : **M**. À essayer sous QEMU avec « wifi test » (réseaux Maison, Telephone, Cafe), puis sur le Pi.

---

## 2. Fichiers : les colonnes de fileviewer, l'ergonomie de Korin

### Constat

**Onyx, `fileviewer`** ([capture](../../Onyx/screenshots/fileviewer.png), [800 px](../../Onyx/screenshots/pocket/fileviewer-800.png), [portrait, tiroir](../../Onyx/screenshots/pocket/fileviewer-portrait-drawer.png), `user/Apps/fileviewer/main.cpp`) : colonnes de Miller façon NeXTSTEP. Chaque colonne est un dossier ; choisir un dossier ouvre la colonne suivante ; choisir un fichier ouvre une **colonne d'aperçu** (taille, type, début d'un texte, image réduite, icône d'une app). Lieux en trois groupes repliables (Personnel avec Corbeille et dossiers épinglés, Ordinateur, Réseau avec FTP). Clavier complet : flèches, → entre, ← remonte, **une lettre saute au nom suivant**, Ctrl+C/X/V, Ctrl+N, Ctrl+R renommer, Suppr vers la corbeille. Menu au clic droit, dépôt sur un lieu.

À 800 px, elle n'affiche que deux colonnes de 200 px et un bout de la troisième ; l'aperçu est poussé hors de l'écran ([capture 800](../../Onyx/screenshots/pocket/fileviewer-800.png)). C'est la limite des colonnes sur un petit écran : le chemin prend la place du contenu.

**Korin, Fichiers** (`src/apps/files/files.asm`, [aide](../src/apps/files/help_fr.md), [capture](design-proposal/mise-en-oeuvre/ag_fi1.png)) : panneau `CREUX` à gauche (Accès rapides, Volumes avec éjection), en-tête de 44 px (précédent, suivant, parent, fil d'Ariane, vue icônes ou liste), contenu, ligne d'état. Vue liste avec colonnes triables et redimensionnables, retenues. Volumes mis à jour par le bus.

L'utilisateur a raison : à 800x600, le fil d'Ariane et la grille d'icônes de Korin se lisent mieux que les colonnes. Ce qui manque à Korin, ce sont les **outils** de fileviewer, pas sa disposition.

### Problèmes concrets de Korin

1. **Pas de copie, de déplacement ni de renommage.** L'aide renvoie à `cp` dans le Terminal. C'est le plus gros manque.
2. **Pas de corbeille** : Suppr est définitif, et un dossier non vide ne se supprime pas.
3. **Pas de menu contextuel** sur un élément, au clic droit ni à l'appui long. Pourtant le tactile du PocketTerm donne déjà le clic droit à l'appui long (DESIGN-ELEGANT, 12).
4. **Pas de saisie pour sauter à un nom**, ni de recherche. Dans `SD1:/Roms`, aller à « Zelda » se fait à la flèche.
5. **Lecture arrêtée à 256 éléments** (`MAXE = 256`, l. 60 et 615). Un dossier de ROMs ou de MP3 sur la partition UserFS sera coupé, sans doute sans le dire. À vérifier, puis à corriger en premier.
6. **Rien pour voir un fichier sans l'ouvrir** : ni taille ni date avant la vue liste, ni image, ni début du texte.
7. **Une seule sélection.**
8. Sur un écran plus étroit que la fenêtre de 760 px, le panneau de 190 px reste fixe.

### Propositions

**A. Les outils, sans changer la mise en page.** Presse-papier de fichiers (copier, couper, coller), renommer, corbeille, menu contextuel, saisie au clavier, sélection multiple, plus de limite à 256. Effort M à L.

**B. A, plus un aperçu et la navigation des colonnes (recommandée).** On garde fil d'Ariane, icônes et liste, et on emprunte à fileviewer ce qui marche aussi sans colonnes :

* **un volet d'aperçu à droite**, repliable, qui reprend la colonne d'aperçu de fileviewer ;
* **→ et ← dans la vue liste** : → entre dans le dossier choisi, ← remonte en gardant le dossier quitté choisi. C'est la navigation des colonnes, sans les colonnes ;
* **Espace** : aperçu en feuille par-dessus le contenu quand le volet est fermé ou que la place manque ;
* **un tiroir** pour le panneau de gauche quand la fenêtre fait moins de 640 px, comme fileviewer en portrait.

**C. Une troisième vue « Colonnes ».** Un segment de plus dans l'en-tête, pour qui aime Miller. À 800 px, elle tient deux colonnes et l'aperçu, pas plus. Effort L, et elle double le code des vues. À garder pour plus tard, si B ne suffit pas.

#### Mise en page recommandée (B), 800x600, volet ouvert

```
┌───────────────┬──────────────────────────────────────────────┬─────────────┐
│ Accès rapides │ ← → ↑   SD1: › Roms › gba            [▦ ☰ ◫]   ‒ □ × │
│  Applications ├──────────────────────────────────┬───────────┤             │
│  Documents    │ Nom                    Taille  ▲ │  [image]  │             │
│  Musique      │ ▸ Advance Wars.gba     8 Mo      │           │             │
│  Images       │ ▸ Golden Sun.gba       8 Mo      │ Zelda.gba │             │
│ Volumes       │ ■ Zelda.gba           16 Mo      │ ROM GBA   │             │
│  SD0:  Carte  │ ▸ ...                            │ 16 Mo     │             │
│  SD1:  Carte  │                                  │ 2/11/2026 │             │
│  USB0: Clé  ⏏ │                                  │ [Ouvrir]  │             │
│ Corbeille     │                                  │ Ouvrir    │             │
│               │                                  │  avec…    │             │
├───────────────┴──────────────────────────────────┴───────────┴─────────────┤
│ 312 éléments · 1 choisi, 16 Mo                                            │
└───────────────────────────────────────────────────────────────────────────┘
```

* Le troisième segment `◫` de l'en-tête montre ou cache le volet ; il est retenu comme la vue.
* Volet de 200 px, `SURFACE` séparé par un filet `LIGNE`, seulement si le contenu garde 360 px. Sinon il ne s'ouvre pas et Espace donne la feuille.
* Contenu de l'aperçu, du haut vers le bas : image réduite (BMP, PNG, JPG par les décodeurs déjà présents), début d'un texte en DejaVu Sans Mono 13, icône 48 d'une app ou d'un type ; puis nom en 16 SemiBold, type, taille, date en 13 `TEXTE2` ; puis Ouvrir (principal) et « Ouvrir avec… » (`ui_assoc_*`).
* Corbeille : une ligne de plus dans le panneau, un glyphe, le nombre d'éléments en `TEXTE3`.

#### Le menu contextuel (souris et doigt)

Clic droit, appui long (500 ms, déjà donné par le pilote tactile) ou touche Menu :

```
  Ouvrir
  Ouvrir avec…            ›
  ───────────────────────
  Couper            Ctrl+X
  Copier            Ctrl+C
  Coller            Ctrl+V
  Renommer          F2
  ───────────────────────
  Mettre à la corbeille  Suppr
  Propriétés        Alt+Entrée
```

Le même menu sert le vide du dossier (Coller, Nouveau dossier, Trier par, Afficher). Il passe par `ui_popup`, déjà employé par Fichiers.

#### Le clavier

| Touche | Effet |
|---|---|
| lettres | sauter au nom suivant qui commence ainsi (tapées à moins de 700 ms : le préfixe entier) |
| → / ← en vue liste | entrer / remonter (comme une colonne) |
| Espace | aperçu en feuille ; Espace ou Échap le ferme |
| F2 | renommer sur place, dans la ligne |
| Maj + flèches, Ctrl + clic | sélection multiple |
| Ctrl+F | filtre du dossier courant, dans un champ qui remplace le fil d'Ariane |

#### Le tactile et le petit écran

* Lignes de 30 (32 avec la densité tactile de l'étude 4), icônes de 48 en vue icônes.
* Sélection multiple au doigt : un appui long choisit, les touchers suivants ajoutent, jusqu'à « Terminé » dans la ligne d'état.
* Sous 640 px de large (mode poche en portrait, 640x480) : le panneau devient un tiroir ouvert par un bouton à gauche de ←, au-dessus du contenu sous un voile, comme fileviewer en portrait.

### Ce que ça coûte

| Changement | Fichiers | Effort |
|---|---|---|
| Plus de limite à 256 (tableau grandi au besoin, ou par blocs) | `files.asm` | S à M |
| Saisie au clavier, → / ← en liste, F2 | `files.asm`, `help_*.md` | S |
| Menu contextuel, appui long | `files.asm` | S |
| Copier, couper, coller, déplacer, renommer, avec avancement pour les gros fichiers (dans un thread, comme le Wi-Fi) | `files.asm` ; peut-être un petit `fileops` dans AppKit ou un service, pour que le dialogue d'ouverture et le Terminal en profitent | M à L |
| Corbeille `SD0:/.Trash` (et une par volume) | `files.asm`, `places.ini`, panel (glyphe de la corbeille) | M |
| Volet d'aperçu et feuille Espace | `files.asm` ; décodeurs d'images de UIKit ou d'ImageKit | M |
| Tiroir sous 640 px | `files.asm` | S |
| Aide | `help_fr.md`, `help_en.md`, `help/main.png` | S |

Total : **L** pour l'ensemble, livrable en trois temps : 256 et clavier (S), outils et corbeille (M à L), aperçu et tiroir (M).

---

## 3. Mode repos : moins d'énergie, pas seulement un écran verrouillé

### Constat

**Onyx, `lock`** (`user/Apps/lock/main.cpp`, 135 lignes) : une app plein écran, le fond assombri, l'heure en grand, la date, « Click or press a key to unlock », ou un PIN lu en clair dans `SD:/etc/lock.ini`. Elle redessine chaque minute et tourne en boucle toutes les 100 ms : **elle ne fait rien pour l'énergie**, l'écran reste allumé.

**Korin aujourd'hui.** Aucun verrou, aucune veille. Le bouton d'alimentation du panel ouvre `src/sys/shutdown` (Redémarrer, Éteindre, Annuler). Ce qui tourne quand personne ne touche rien :

| Où | Ce qui réveille le système | Source |
|---|---|---|
| Noyau | tick à 1000 Hz sur les quatre cœurs ; `cpu_idle` fait `wfi` en boucle, réveillé 1000 fois par seconde | `kernel/timer.asm` (`HZ = 1000`), `kernel/sched.asm` l. 563 |
| Noyau | le processeur mis à sa fréquence maximale au démarrage, jamais baissée | `kernel/kernel.asm` l. 186, `mailbox.asm` (`mbox_arm_max`) |
| Elegant | boucle de composition : au plus 8 ms d'attente, puis un tour | `sys/elegant/elegant.asm` l. 69 à 92 |
| usbd | scrute les ports toutes les 2 ms | docs/POLLING.md |
| HIDManager | anneaux des pilotes toutes les 10 ms (50 au repos) | idem |
| gpio | fronts des broches demandées, chaque ms | idem |
| Wi-Fi | 1 ms tant qu'aucune interruption n'est venue, 200 ms au repos ; connexion relue toutes les 250 ms ; la puce n'est jamais mise en économie | `svc/wifi/wifi.asm` |
| netd, genet, hwmanager | 500 ms, 2 s, 2 s | docs/POLLING.md |
| barre de menus, panel | horloge chaque seconde, liste des process chaque seconde | idem |
| gt911 | **déjà bien** : au repos, attend l'interruption du GPIO 4 | `svc/gt911/gt911.asm` |
| v3d | sait déjà couper le domaine du GPU (`fw_domoff`), mais seulement après des blocages | `svc/v3d/v3d.asm` |

Deux faits rendent le travail plus simple qu'il n'y paraît :

* **`SYS_FW_CALL` existe** (`kernel/hardware.asm` l. 210) : un programme système avec les droits matériels envoie déjà des messages au firmware (v3d, usbd, sound, hwmanager le font). Éteindre l'écran et baisser la fréquence ne demandent donc **aucun travail noyau**.
* **Le bus existe** : un sujet `korin.power.state` suffit pour que chaque programme ralentisse, exactement comme `korin.theme`.

Une limite : **le Pi 4 n'a pas de vraie mise en veille en mémoire** (pas de suspension S3 que le firmware sache reprendre). « Repos » veut donc dire : écran éteint, processeur au plus bas, matériel inutile coupé, sondes ralenties. L'arrêt complet existe, mais le réveil du Pi 4 arrêté passe par GPIO 3, qui sert à l'I2C du tactile sur le PocketTerm : à écarter.

### L'expérience

Trois états, que l'utilisateur voit comme deux :

| État | Ce qui se passe | Sortie |
|---|---|---|
| **Actif** | normal | |
| **Écran éteint** | écran éteint, Elegant ne compose plus, les apps tournent (musique, téléchargement) | toute touche, tout toucher, souris |
| **Repos** | écran éteint, plus : processeur au minimum, GPU coupé, Wi-Fi en économie, sondes ralenties, apps prévenues | idem, en moins d'une seconde |

**Entrer**

* **Le bouton d'alimentation du panel** : le dialogue de fin de session devient trois choix, « Mettre en repos » en premier et en bouton principal, puis Redémarrer, Éteindre. C'est l'action la plus fréquente sur un appareil de poche.
* **Une touche** : Sleep (HID « System Sleep ») si le clavier du PocketTerm l'envoie dans son rapport 3, sinon Super+L. À vérifier avec le RP2040.
* **L'inactivité** : écran éteint après 2 min, repos après 10 min, sur batterie comme sur secteur. Réglable.
* **Une commande** `repos` dans le Terminal, et la publication sur le bus pour PocketQI.

**Juste avant d'éteindre**, 10 s avant le délai : un voile `TEXTE1` à 50 % sur tout l'écran, dessiné une fois, sans animation. Un mouvement l'annule. C'est le seul signe, car on ne sait pas encore baisser la luminosité.

**Sortir.** Toute touche, tout toucher, tout mouvement de souris. **Le premier événement est avalé** : un toucher pour réveiller ne doit pas cliquer dans l'app dessous. L'écran revient sur ce qu'il montrait.

**Le verrou, optionnel, désactivé par défaut.** Le projet ne se défend pas contre un attaquant (décision du 29/10/2026 : résistance aux bugs et performance seulement) : ce verrou sert à la discrétion et contre les touches dans la poche, pas à la sécurité. Le dire dans le réglage.

**Ce que montre l'écran de reprise**, seulement si le verrou est actif :

```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│                                                              │
│                          14:32                               │  Inter SemiBold 64, TEXTE1
│                   mercredi 5 novembre                        │  16, TEXTE2
│                                                              │
│              2 notifications · Wi-Fi Maison                  │  13, TEXTE3
│                                                              │
│                     ●  ●  ○  ○                               │  PIN : points de 12, ACCENT
│                 Tapez le code, puis Entrée                   │  13, TEXTE2
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

Fond : la palette du fond d'écran de Givre (ou de Noir), unie, sans facettes, pour ne rien calculer. Pas d'ombre, pas d'animation ; une erreur vide les points et passe le texte en `DANGER`. Sans PIN : « Touchez l'écran ou appuyez sur une touche ».

**Exceptions.** Une app peut empêcher le repos (pas l'écran éteint) tant qu'elle travaille : Lecteur multimédia en lecture, émulateurs en jeu, téléchargement de pkgd, impression. Elle le dit par le bus (`korin.power.inhibit.<app>`, retiré à sa mort comme les sujets retenus). Le réglage montre qui retient le repos.

**Réglages, Énergie** (nouvelle applet, ou une section d'Écran) :

```
 Énergie
 Éteindre l'écran après        [ 2 minutes    ▾]
 Mettre en repos après         [10 minutes    ▾]
 Bouton d'alimentation         [ Repos        ▾]
 Couper le Wi-Fi en repos      [  ]
 Verrou au réveil              [  ]   Code [••••]
   Protège des regards, pas d'une personne qui veut entrer.
 En ce moment : Lecteur multimédia retient le repos.
```

### Ce qui est techniquement possible sur le Pi 4

| Levier | Où | Existe ? | Travail | Noyau ? | Gain estimé | Risque |
|---|---|---|---|---|---|---|
| Éteindre la sortie HDMI (firmware, BLANK_SCREEN `0x00040002`, ou le tag de `display_power`, numéro à confirmer) | Elegant ou un service `powerd`, par `SYS_FW_CALL` | non | S | non | 0,1 à 0,3 W pour le Pi ; **le reste dépend de l'écran** | l'écran 3,5" du PocketTerm garde peut-être son rétroéclairage sans signal : à essayer en premier |
| Rétroéclairage du PocketTerm | un GPIO ou une PWM, si Waveshare en câble une | inconnu | S si une broche existe | non | le plus gros gain possible (souvent 0,5 à 1 W pour un petit écran) | chercher la broche (schéma Waveshare, `gpiowatch`) |
| Elegant ne compose plus | `elegant.asm` : attente sans échéance sur `SYS_WS_NEXT` tant que l'écran est éteint | non | S | non | une partie du cœur 0 | aucun |
| Fréquence du processeur au minimum (SET_CLOCK_RATE ARM à GET_MIN_CLOCK_RATE `0x00030007`, 600 MHz) | `powerd`, par `SYS_FW_CALL` ; retour au maximum au réveil | le noyau sait déjà la régler | S | non | 0,1 à 0,4 W selon la tension que le firmware baisse avec | vérifier GET_VOLTAGE avant et après |
| Gouverneur permanent, hors repos | `powerd`, d'après la charge que l'ordonnanceur mesure déjà (`PC_LOAD`, `PC_BUSY`) | la mesure existe | M | un appel pour lire la charge, peut-être | en usage léger aussi | latence de montée : rester simple (max dès 30 % de charge) |
| GPU V3D coupé | `v3d` : couper le domaine après N s sans travail, et en repos ; le rallumer au travail suivant (il sait déjà le faire après un blocage) | en partie | M | non | 0,1 à 0,3 W | la mémoire signale une panne GPU en cours : ne pas toucher `TI_2D`, essayer après |
| Wi-Fi en économie (mode PM2 de la puce, `WLC_SET_PM`) ou coupé en repos | `svc/wifi/wifi.asm` : un ioctl, une op | non | M | non | 0,1 à 0,3 W | latence réseau ; VNC et telnet plus lents |
| Sondes ralenties | usbd 2 ms → 16 ms, HIDManager, gpio, netd, menubar, panel s'abonnent à `korin.power.state` | le bus | M | non | réveils en moins ; utile surtout avec le point suivant | 16 ms d'usbd reste assez vif pour réveiller |
| Ethernet : PHY éteint sans câble | `svc/genet/genet.asm` (bit power-down du PHY) | non | S à M | non | 0,1 à 0,2 W | rebrancher le câble doit le réveiller : garder la sonde du lien |
| Ports USB-A (VL805) suspendus | usbd, xHCI | non | L | non | 0,1 à 0,3 W | le clavier du PocketTerm est sur l'USB-C (DWC2) : il réveille quand même ; plus tard |
| LED d'activité éteinte | firmware, `SYS_FW_CALL` | non | S | non | négligeable | |
| **Tick sans période au repos** (programmer le minuteur sur la prochaine échéance) | ordonnanceur, minuteur | non | L | **oui** | moins de réveils : c'est ce qui donne tout son sens au reste | horloges, `SYS_SLEEP` à revoir |
| **Cœurs 1 à 3 parqués** en repos (threads sur le cœur 0, minuteur coupé, `wfi` sans tick) | ordonnanceur, `smp.asm` | non | M à L | **oui** | réveils et fuite des cœurs | rendre les cœurs au réveil sans casser l'affinité |

Ordres de grandeur publiés pour un Pi 4 sous Linux : 2,7 à 3 W au repos, écran HDMI branché. Ce sont des repères, pas des mesures de Korin. **Il faut un wattmètre USB-C** entre l'alimentation et le PocketTerm, et mesurer chaque levier seul : actif, écran éteint, repos.

### Propositions

**A. Écran éteint seulement.** HDMI coupé, Elegant au repos, délai d'inactivité, premier événement avalé. Effort S. Gain limité si l'écran garde son rétroéclairage.

**B. Repos en espace utilisateur (recommandée).** A, plus un petit service `powerd` qui tient l'état, publie `korin.power.state` (`actif`, `ecran`, `repos`), compte l'inactivité, applique fréquence, GPU, Wi-Fi, LED, et lit les `korin.power.inhibit.*`. Les programmes qui sondent s'abonnent. Verrou optionnel, applet Énergie, bouton du panel. Aucun travail noyau. Effort M, en deux temps : écran et fréquence d'abord, puis GPU, Wi-Fi et sondes.

**C. B, plus le noyau.** Tick sans période et cœurs parqués. Effort L, et à mesurer avant : si B donne déjà l'essentiel, C peut attendre.

### Ce que ça coûte (B)

| Changement | Fichiers | Effort |
|---|---|---|
| `powerd` : état, inactivité, firmware, bus | nouveau `src/svc/powerd/powerd.asm`, `src/inc/bus.inc` (sujets), `drivers.txt` ou init2 | M |
| Inactivité : Elegant connaît le dernier événement ; il le publie, ou `powerd` le lui demande | `src/sys/elegant/core.inc`, `elegant.asm` | S |
| Elegant : ne plus composer, avaler le premier événement, voile d'avant | `elegant.asm`, `core.inc`, `present.inc` | S |
| Écran de reprise et verrou | nouveau `src/sys/lock/` (UIKit, plein écran, jetons) | S à M |
| Dialogue de fin de session : Mettre en repos | `src/sys/shutdown/shutdown.asm`, `lang` | S |
| Applet Énergie | `src/apps/control/applets/power.inc` (nouveau), `control.asm` | M |
| v3d : coupure à l'inactivité | `src/svc/v3d/v3d.asm` | M |
| Wi-Fi : PM2, op « économie » | `src/svc/wifi/wifi.asm`, `chip.inc`, `inc/netd.inc`, `kits/netkit` | M |
| Abonnements à `korin.power.state` | `svc/usbd`, `svc/hidmanager`, `svc/gpio`, `sys/menubar`, `sys/panel`, `svc/netd` | M |
| Inhibitions | Lecteur multimédia, EmuKit, pkgd, printd | S chacun |
| Commande `repos` | `src/bin/repos` | S |
| Docs | `docs/POWER.md` (ou une section de POLLING.md), `docs/BUS.md` | S |

**Premier essai à faire sur le PocketTerm**, avant tout le reste : un petit programme qui envoie BLANK_SCREEN par `SYS_FW_CALL`, pour voir si l'écran s'éteint vraiment ou reste rétroéclairé en noir.

---

## 4. Mode poche : une coquille pour le PocketTerm, dans le style de Givre calme

### Constat

**Onyx** a construit trois choses ([étude technique](../../Onyx/docs/POCKETUI-TECH-STUDY.md)) :

* **PocketUI**, un second serveur graphique à côté d'Elegant, avec son UIKit chargé sous un alias, des widgets adaptatifs (barres d'outils à priorités, panneaux en tiroir, dialogues en feuille) et des sessions par mode. L'étude l'estimait à 97 à 104 jours de session.
* **`pocketshell`** : accueil v2 ([capture](../../Onyx/docs/compact-shell/real/pocketshell-home-800-fr.png)) avec recherche ronde, ligne « Aujourd'hui », catégories en pastilles colorées, une carte d'apps, bande « Ouvertes » en vignettes ; commutateur Alt+Tab en cartes ([capture](../../Onyx/docs/compact-shell/real/pocketshell-switcher-800-fr.png)) ; réglages rapides en tuiles avec notifications ([capture](../../Onyx/docs/compact-shell/real/pocketshell-quick-800-fr.png)) ; clavier à l'écran ; historique du presse-papier.
* **`consolehome`** : un accueil à la manette, façon console de salon.

Le fond est bon : une app à la fois en plein écran, la barre de menus globale gardée en haut, un accueil derrière les apps, un commutateur, un volet rapide. **La forme ne va pas avec Givre** : pastilles de catégories colorées sur le fond, anneau « Aqua » et lueur, ombres douces dessinées à la main (`look.h`), bande de vignettes à pied sombre, aides de touches en bas. Et **l'architecture est trop lourde** pour Korin : un second serveur et un second UIKit vont contre « un seul UIKit, appelé directement ».

**Korin** a déjà presque toutes les pièces, dans un seul serveur :

| Pièce | Où | Utile au mode poche |
|---|---|---|
| Modes d'Elegant | `sys/elegant/mode.inc` (table `POL_*`), `desktop.inc` : « D'autres modes (PocketUI, console de jeu) auront leur propre table » | le point d'entrée prévu |
| Session en fichier | `SD0:/etc/session`, lue par `bu_init` | une session par mode |
| Barre de menus globale | `sys/menubar` | la barre du haut, telle quelle |
| Lanceur à rail | `sys/panel/launcher.inc` | l'accueil |
| Liste des apps lancées | panel (`SYS_PROCLIST`) | le commutateur |
| Volets du son et du Wi-Fi à vraie ombre | `soundmenu`, `wifimenu` | les réglages rapides |
| Notifications | `sys/notifyd` (toasts de 4 s, sans historique) | à garder en liste |
| Copie de fenêtre | `elegant/snapshot.inc`, `remote.inc` (pour rdpd) | les vignettes |
| Tactile | `svc/gt911` : toucher = clic, glisser vertical = molette, appui long = glisser ; clic droit à l'appui long | suffisant pour commencer |
| Résolution à chaud | `elegant/screen.inc`, `WEV_SCREEN` | les apps suivent déjà une taille qui change |
| Chrome sans barre | `FRAME_CAP_W` libre dans toutes les apps | les glyphes peuvent changer de sens |

### Principes de la coquille poche

1. **Une app devant, en plein écran**, sous la barre de menus. Pas de fenêtres qui se chevauchent, pas d'espaces de travail.
2. **Même Elegant, même UIKit, mêmes apps.** Aucune app recompilée, aucune version « poche » d'une app.
3. **Givre calme tel quel** : coquille neutre, couleur dans les icônes, accent pour le choix, surfaces opaques, rien d'animé.
4. **Clavier d'abord, doigt ensuite** : le PocketTerm a un vrai clavier ; le doigt doit tout atteindre, mais rien ne doit l'exiger.

### Propositions

**A. Un bureau compact.** Le bureau normal avec un réglage : fenêtres agrandies à l'ouverture, panel masqué, cibles plus grandes. Effort S à M. Ce n'est pas une expérience de poche : on retrouve vite des fenêtres empilées.

**B. Un mode « poche » d'Elegant et une coquille cliente (recommandée).** Une table `POL_*` de plus dans Elegant (placement plein écran, ordre, touches système) et trois programmes clients qui reprennent le code existant : barre de menus, accueil (le lanceur du panel en plein écran), réglages rapides. Effort L.

**C. Porter PocketUI.** Second serveur, UIKit par serveur, widgets adaptatifs. Effort très L (l'estimation d'Onyx dépasse trois mois), et contraire aux règles du projet. Non.

### B en détail

#### Le placement (Elegant, mode `poche`)

* Une fenêtre à cadre **redimensionnable** prend toute la zone de travail : sous la barre de 28 px, jusqu'en bas. Elle reçoit sa taille par le même chemin qu'un agrandissement.
* Une fenêtre **à taille fixe** (Calculatrice, dialogues, `WF_MODAL`) est une **carte** centrée, sur un voile `TEXTE1` à 30 % posé sur l'app ou l'accueil derrière.
* Les fenêtres sans cadre (menus, volets, toasts) gardent leur place.
* Les trois glyphes du cadre changent de sens : **réduire** ramène à l'accueil, **fermer** ferme, **agrandir** disparaît. `FRAME_CAP_W` reste libre, rien ne change dans les apps.
* L'accueil est la fenêtre du fond, toujours là.
* F11 (déjà connu des émulateurs) masque la barre : plein écran véritable.

#### L'accueil, 800x600

L'accueil est le lanceur à rail que l'utilisateur a gardé, en plein écran, avec une bande « Ouvertes » :

```
┌────────────────────────────────────────────────────────────────────────────┐
│ ◆ Korin                                                  ◂)  ▂▄▆█  14:32 │ barre de menus, 28
├────────────────────────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────┐   mer. 5 nov.            │
│  │ ⌕  Rechercher une app, un réglage, un fichier│   15:00 Dentiste         │ recherche 38 ; Aujourd'hui
│  └──────────────────────────────────────────────┘                          │ en 13 TEXTE2
│ ┌───────────┐                                                             │
│ │ Récentes  │   [2048]  [Fichiers] [Terminal] [Lettres] [Calc.]           │ icônes 48, 5 colonnes
│ │ Toutes    │                                                             │
│ │ Bureautiq.│   [Agenda] [Mail]    [Musique]  [Photos]  [Réglages]        │
│ │ Internet  │                                                             │
│ │ Jeux      │   ...                                                       │
│ │ Multiméd. │                                                             │
│ │ Système   │                                                             │
│ └───────────┘                                                             │
├────────────────────────────────────────────────────────────────────────────┤
│ Ouvertes   [▣ Terminal  ×] [▣ Fichiers  ×] [▣ Lettres  ×]                  │ bande de 64
└────────────────────────────────────────────────────────────────────────────┘
```

* Fond : la feuille `FACE` opaque du lanceur actuel, sans voile ni facettes derrière.
* Rail des catégories à gauche, 34 px par ligne, **Récentes** en premier (le lanceur n'en a pas encore), choix en `ACCENT_CLAIR`. Pas de pastilles colorées.
* Recherche : apps, puis applets de Réglages, puis fichiers de `home`. Taper n'importe où sur l'accueil écrit dans la recherche.
* « Aujourd'hui » : la prochaine entrée de l'agenda en une ligne, à droite de la recherche. Il remplace les applets Rendez-vous et Notes du bureau.
* Bande « Ouvertes » : une pastille par app, icône 24, nom, croix. **Pas de vignette ici** : elle coûte une copie de fenêtre et se lit mal à cette taille. Les vignettes vont au commutateur.
* Clavier : flèches dans la grille, Tab passe au rail puis à la bande, Entrée ouvre, Suppr ferme l'app choisie dans la bande.

#### Une app devant

```
┌────────────────────────────────────────────────────────────────────────────┐
│ ◆ Lettres  Fichier  Édition  Insertion  Format       ◂)  ▂▄▆█  14:32  ‹3› │ menus de l'app ; ‹3› apps ouvertes
├────────────────────────────────────────────────────────────────────────────┤
│                                                                   ‒    ×  │ glyphes : accueil, fermer
│   (l'app, sur toute la zone : 800 x 572)                                   │
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘
```

* Le losange ◆ ouvre l'accueil (dans le bureau, il ouvre le menu Korin : en poche, ce menu passe dans les réglages rapides).
* Le compteur ‹3› ouvre le commutateur au doigt.
* L'heure ouvre les réglages rapides.

#### Le commutateur

Alt+Tab, Super+Tab, ou le compteur ‹3› :

```
┌────────────────────────────────────────────────────────────────────────────┐
│                                                                            │ voile TEXTE1 à 50 %
│     ┌────────────────┐   ┏━━━━━━━━━━━━━━━━┓   ┌────────────────┐          │
│     │   (vignette)   │   ┃   (vignette)   ┃   │   (vignette)   │          │ 240 x 180, rayon 10
│     │                │   ┃                ┃   │                │          │ choisie : anneau ACCENT 2 px
│     └────────────────┘   ┗━━━━━━━━━━━━━━━━┛   └────────────────┘          │
│       ▣ Terminal            ▣ Lettres      ×      ▣ Fichiers               │ icône 20, nom 14
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘
```

* L'app la plus récente d'abord. Relâcher Alt, Entrée ou un toucher passe à l'app ; Suppr ou × la ferme ; Échap reste.
* Vignettes tirées des copies d'Elegant (`snapshot.inc`), réduites une fois à l'ouverture. Aucune app ne redessine.
* Pas de ligne d'aide des touches comme dans Onyx : les bulles et l'aide suffisent.

#### Les réglages rapides et les notifications

Super+N, l'heure, ou un glissé depuis le bord droit plus tard :

```
                                     ┌──────────────────────────────────┐
                                     │ mercredi 5 novembre        14:32 │ 16 SemiBold
                                     │ ┌──────────────┐┌──────────────┐ │
                                     │ │ Wi-Fi        ││ Ne pas       │ │ tuiles 64, rayon 10
                                     │ │ Maison       ││ déranger     │ │ actives : ACCENT_CLAIR,
                                     │ └──────────────┘└──────────────┘ │ texte ACCENT_FONCE
                                     │ ┌──────────────┐┌──────────────┐ │
                                     │ │ Son          ││ Repos        │ │
                                     │ │ 70 %         ││              │ │
                                     │ └──────────────┘└──────────────┘ │
                                     │ ◂) ━━━━━━━━━━━━━●────────        │ curseur de UIKit
                                     │ Notifications        Tout effacer│
                                     │ ┌──────────────────────────────┐ │
                                     │ │ ▣ Agenda   Dentiste à 15:00  │ │ cartes ELEVE
                                     │ └──────────────────────────────┘ │
                                     │ [Réglages]          [⏻ Éteindre] │
                                     └──────────────────────────────────┘
```

* Le volet a le style des volets du son et du Wi-Fi : `MENU`, rayon 10, ombre de niveau 3, 340 px de large, sous la barre, à droite.
* Toucher le nom d'une tuile ouvre le détail dans le même volet (la liste du Wi-Fi de l'étude 1, la sortie du son) ; toucher l'icône bascule.
* Les notifications restent dans la liste jusqu'à « Tout effacer ». En poche, le toast de `notifyd` prend toute la largeur sous la barre, 4 s.
* « Repos » est le mode de l'étude 3. Les deux études vont ensemble.

#### Clavier physique et tactile

| Geste ou touche | Effet |
|---|---|
| Super seul | accueil (un second appui revient à l'app) |
| Alt+Tab, Super+Tab | commutateur |
| Super+N | réglages rapides |
| Super+L ou Sleep | repos |
| Ctrl+Q, Ctrl+W | fermer, comme sur le bureau |
| F11 | plein écran sans barre |
| toucher, glisser, appui long, deux doigts | comme aujourd'hui (gt911) |

Le clavier à l'écran n'est pas utile avec le clavier du PocketTerm : à garder pour plus tard, s'il y a un jour un appareil sans clavier.

**Densité tactile.** Une troisième ligne dans `uk_mt_tab` de UIKit, choisie par `densite = tactile` dans `theme.txt` : contrôles et lignes de 36, menus de 36. Toutes les apps la prennent d'un coup, sans recompiler, comme pour le passage à 32/30. Les dix apps C++ qui recopient `ctl_h` en dur sont à aligner.

#### Comment ça s'articule

| Élément | Bureau | Poche |
|---|---|---|
| Elegant | mode `desktop` | mode `poche` : nouvelle table `POL_*`, placement plein écran, cartes, touches système |
| Session | `SD0:/etc/session` | `SD0:/etc/session.poche` : voronoy (couleur unie suffit), menubar, accueil, rapides, notifyd |
| Barre du haut | menubar | menubar, avec ◆ = accueil et ‹n› ; un argument `--poche` |
| Panel | lancé | **pas lancé** ; son code de lanceur sert à l'accueil (même programme avec un argument `--poche`, ou `launcher.inc` partagé) |
| Applets du bureau | Rendez-vous, Notes | ligne « Aujourd'hui » de l'accueil |
| UIKit | inchangé | densité `tactile` si choisie ; rien d'autre |
| Apps | inchangées | inchangées ; les fenêtres fixes en cartes |
| Choix | Réglages, Écran : « Interface : Bureau / Poche » ; Setup, étape Écran et thème | idem |

**Passer d'un mode à l'autre** : écrire `shell=poche` dans `system.ini`, puis relancer la session. En premier, un redémarrage, comme Onyx avant son K3 ; ensuite, Elegant change de table à chaud et relance les programmes de session (les apps ouvertes sont replacées, pas fermées). Le PocketTerm reste en bureau par défaut : c'est l'utilisateur qui choisit.

**Une console plus tard** : une table `console` de plus et un accueil à la manette (GameLib existe déjà). Hors de cette étude.

### Ce que ça coûte

| Changement | Fichiers | Effort |
|---|---|---|
| Table `poche` : placement, cartes, glyphes, voile | `src/sys/elegant/pocket.inc` (nouveau), `elegant.asm` (liste des modes), `core.inc` (crochet de placement), `mode.inc` (un ou deux crochets de plus, par exemple `POL_PLACE`, `POL_KEY`) | M |
| Touches système routées vers la coquille avant l'app | `core.inc`, une op de `ws_ext.inc` (inscription d'une touche) | M |
| Session par mode | `desktop.inc` (`bu_init` lit `session.<mode>`), `skel/sd0/etc/session.poche` | S |
| Accueil (lanceur plein écran, Récentes, Aujourd'hui, bande Ouvertes) | `src/sys/panel/launcher.inc`, `panel.asm` (argument), une liste des récents dans `SD0:/etc` | M à L |
| Commutateur et vignettes | coquille ; op de copie réduite dans `elegant/snapshot.inc` | M |
| Réglages rapides | nouveau `src/sys/quick/` à partir de `soundmenu` et `wifimenu` | M |
| Historique des notifications | `src/sys/notifyd` | S à M |
| Barre de menus en poche | `src/sys/menubar/menubar.asm` | S |
| Densité tactile | `src/kits/uikit` (`uk_mt_tab`), dix apps C++ (`ctl_h`) | M |
| Choix du mode | `applets/display.inc`, `src/sys/setup/main.cpp` | S à M |
| Docs | `docs/DESIGN-ELEGANT.md` (section poche), aide | S |

Total : **L**, environ deux à trois semaines. Premier jalon visible en une semaine : table `poche`, session, accueil sans Récentes, apps en plein écran.

---

## Récapitulatif

| Étude | Recommandation | Effort | Ordre |
|---|---|---|---|
| 1. Wi-Fi | **Garder le modèle de Korin**, ne pas porter wpaconf. Une seule ligne de réseau (signal, sécurité en mots, clé dans la ligne) dans le menu, l'applet et Setup ; bouton Voir la clé ; « Autre réseau… » pour les SSID cachés ; lignes de 30 qui défilent ; lien vers le pays | M | 1 |
| 3. Mode repos | **Repos en espace utilisateur** : `powerd` et `korin.power.state` ; HDMI éteint, Elegant au repos, processeur au minimum par `SYS_FW_CALL`, GPU et Wi-Fi en économie, sondes ralenties ; premier toucher avalé ; verrou optionnel de discrétion ; « Mettre en repos » dans le dialogue d'alimentation ; applet Énergie. Le noyau (tick sans période, cœurs parqués) après mesure | M (puis L pour le noyau) | 2 ; d'abord l'essai de BLANK_SCREEN sur le PocketTerm |
| 2. Fichiers | **Garder fil d'Ariane, icônes et liste** ; prendre à fileviewer ses outils : copier, couper, coller, renommer, corbeille, menu contextuel (appui long), saisie au clavier, → / ← en liste, volet d'aperçu et Espace, tiroir sous 640 px ; corriger d'abord la limite de 256 éléments | L (en trois temps : S, M à L, M) | 3, mais la limite de 256 tout de suite |
| 4. Mode poche | **Un mode `poche` d'Elegant et une coquille cliente** : apps en plein écran, accueil tiré du lanceur à rail, commutateur à vignettes, réglages rapides avec notifications, densité tactile de UIKit ; choisi dans Réglages, bureau par défaut ; pas de PocketUI | L | 4, après le repos (qu'il intègre) et les retouches tactiles de Fichiers |

Décisions à prendre :

1. Wi-Fi : l'entrée « Autre réseau… » doit-elle aussi aller dans le menu de la barre, ou seulement dans Réglages ?
2. Repos : délais par défaut (2 et 10 min), et le bouton d'alimentation met-il en repos sans dialogue ?
3. Fichiers : corbeille par volume, ou une seule sur `SD0:` (une copie entre volumes pour SD1: et les clés) ?
4. Poche : l'accueil est-il le panel avec un argument, ou un programme à part qui partage `launcher.inc` ?
5. Poche : densité tactile à 36, ou 32 gardé et seulement des marges plus grandes ? À juger au doigt sur l'appareil.
