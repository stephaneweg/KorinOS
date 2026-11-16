# Design : le programme d'installation de Korin OS (PC)

Proposition du 15/11/2026, rien n'est fait. Les réponses de l'utilisateur (15/11) sont au §11 et reportées dans le
texte. Elle met en images l'étude technique
[etude/INSTALLATEUR.md](etude/INSTALLATEUR.md) (citée « étude » plus bas) et suit la direction Givre calme
([DESIGN-PROPOSAL.md](DESIGN-PROPOSAL.md)). Maquettes : [maquettes/installateur/index.html](maquettes/installateur/index.html)
(une page à onglets, Givre et Onyx Noir, 1280 x 800 et 800 x 600).

Demande de l'utilisateur : « ça doit être aussi simple qu'une installation windows si possible sans fenêtre ».

## 0. En bref

* **Plein écran, sans fenêtre.** Setup en mode `--install` couvre tout l'écran : ni cadre, ni glyphes, ni panel,
  ni bureau derrière. Une bande fine en haut (marque, étapes, clavier, réseau, thème, alimentation), une colonne
  de contenu au milieu, Retour et Continuer en bas.
* **Sept écrans à remplir, deux à regarder.** Bienvenue, Disque (comment installer, puis la place de Korin),
  Internet, Logiciels, Qui êtes-vous, Résumé, puis Installation et Terminé. Le chemin le plus court : *Installer*,
  *Continuer* (choix déjà fait), *Continuer* (schéma), *Continuer sans Internet*, *Continuer*, un nom et un mot de
  passe, *Installer*. Sept clics et deux champs.
* **Repris d'Ubuntu et de DrakX** (Mandriva, Mageia) pour le disque, la partie la plus difficile : chaque choix
  montre le disque *après* (DrakX), en phrases simples (Ubuntu), puis un grand schéma avec une séparation qu'on
  fait glisser (les deux). Détail au §5.2 et au §12.
* **Des défauts sûrs.** Le disque propose « À côté de Windows » quand c'est possible, et **ne présélectionne jamais**
  un choix qui efface quelque chose. Rien n'est écrit avant le bouton du Résumé.
* **L'avancé n'est jamais imposé.** « Installation manuelle », troisième choix du disque, ouvre la table des
  partitions ; elle se referme par « Revenir aux choix simples ». Les autres réglages fins (démarrage, nom de l'ordinateur, partition
  des fichiers) sont des lignes « Modifier » du Résumé ou des cases repliées, jamais des écrans.
* **Ce qu'on ne demande plus.** Résolution (celle du GOP, la native), interface (Bureau), taille des contrôles,
  accès à distance (coupé) : tout se change ensuite dans Réglages. Le thème se choisit d'un clic dans la bande du
  haut, sans écran.

## 1. Principes

1. **Une question par écran, un bouton principal par écran.** Le titre est la question (« Où installer Korin
   OS ? »), le bouton principal dit ce qui se passe (« Continuer », « Installer », « Effacer et installer »,
   « Redémarrer »).
2. **On décrit l'effet, pas le moyen.** « À côté de Windows », « Tout le disque », « Rien ne sera effacé » ; GPT,
   ESP, `nvme -I`, FAT32 ne paraissent que dans le mode avancé.
3. **Le danger se voit avant de se faire.** Tout ce qui sera effacé est en `DANGER`, nommé (« Windows (C:) »,
   410 Go), et repris au Résumé. Le bouton qui efface est rouge et le dit.
4. **Rien n'est écrit avant le Résumé.** Les écrans écrivent dans `/tmp/install` (plan du disque, choix) ;
   Retour défait tout sans conséquence (étude §3.3). Le clavier, la langue, le Wi-Fi et le thème prennent
   effet tout de suite sur la clé, pour qu'on les essaie.
5. **Calme et immédiat.** Pas d'animation, pas de fondu (règle Givre calme : réponse sous 50 ms). Une seule chose
   bouge pendant l'installation : la barre de progression.
6. **Les jetons, jamais de couleurs en dur.** `ui_tokens()` comme toute app. Givre par défaut ; Onyx Noir d'un clic,
   gardé pour le système installé.
7. **Le clavier seul suffit** (§7) : beaucoup de PC arrivent sans souris branchée, et le pavé tactile du ZBook
   n'est pas garanti sur la clé.

## 2. Ce qui change par rapport à Setup

| Setup du Pi (aujourd'hui) | Installateur (Setup `--install`) |
|---|---|
| Fenêtre 760 x 520, rail des étapes à gauche | Plein écran, sans cadre (`WF_FRAMELESS`), au-dessus de tout ; étapes en haut |
| Bienvenue = langue seule ; Région à part | Bienvenue = langue, pays, clavier, fuseau sur un écran (comme le premier écran de Windows) |
| Écran et thème | Supprimé : résolution native, Bureau, Normale. Thème dans la bande du haut |
| Disque (étendre, créer, supprimer sur la carte) | Disque : trois choix avec le disque après sous chacun, un schéma en grand, et l'installation manuelle |
| Wi-Fi | Internet, passé d'office si un câble est branché ou s'il n'y a pas de Wi-Fi |
| Logiciels : liste de paquets, installés tout de suite | Logiciels : cinq groupes à cocher, détail replié ; installés à la fin |
| Accès à distance | Supprimé (coupé par défaut, dans Réglages) |
| — | Compte, Résumé, Installation, Terminé |

Le rail de gauche disparaît : il fait « fenêtre d'application », et il mange 196 px qu'un écran de 800 de large ne
peut pas donner. Les métriques de Setup (`ROWH`, `PAD`…) restent valables à l'intérieur de la colonne.

## 3. Le cadre commun

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ ◆ Korin OS   Disque ── Internet ── Logiciels ── Compte ── Résumé        ⌁  BE  ◐  ⏻     │  bande 48 px
│                                                                                          │
│                    Où installer Korin OS ?                     ← titre 28 SemiBold       │
│                    Korin prend la place choisie ; le reste…    ← chapeau 15 TEXTE2       │
│                                                                                          │
│                    [ contenu, colonne de 680 px au plus ]                                │
│                                                                                          │
│                                                                                          │
│                    Retour                                   [ Continuer ]                │  pied 72 px
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

* **Fond** : `FACE` uni sur tout l'écran. Pas de facettes de Voronoï derrière : elles disent « bureau », et le
  contraste du texte doit tenir partout. (La facette reste dans la marque ◆.)
* **Bande du haut, 48 px**, sans fond propre, filet `LIGNE` dessous :
  * à gauche la marque (losange ◆ `ACCENT` et « Korin OS » SemiBold 14) ;
  * au milieu les étapes : nom en 13, l'étape en cours en `TEXTE1` SemiBold soulignée de 2 px `ACCENT`, les étapes
    faites en `TEXTE2` avec une coche `SUCCES`, les suivantes en `TEXTE3`. Un clic sur une étape faite y revient.
    Bienvenue, Installation et Terminé ne sont pas des étapes : la bande n'en montre aucune ;
  * à droite quatre pastilles de 32 px : **réseau** (état, ouvre l'écran Internet), **clavier** (« BE », ouvre la
    liste des claviers, utile au moment d'un mot de passe), **thème** (◐ : Givre ↔ Onyx Noir), **alimentation**
    (menu : Redémarrer, Éteindre, Ouvrir un terminal). Le terminal est la sortie du dépanneur, rangée là pour ne
    jamais être sur le chemin.
  * À 800 de large, les noms d'étapes cèdent la place à « Étape 2 sur 5 · Disque ».
* **Colonne** : 680 px au plus, centrée, texte aligné à gauche. Marges : 24 px sous 1024 de large, sinon centrée.
  Le titre est à 64 px de la bande (40 à 800 x 600).
* **Pied, 72 px** : « Retour » en bouton fantôme à gauche, le bouton principal à droite (largeur 160 au moins), et
  entre les deux, si besoin, une ligne d'état en 13 `TEXTE2` (« 2,4 Go · 98 Go libres après »).
* **Défilement** : à 800 x 600, les écrans longs (Résumé, Partitions, Logiciels déplié) font défiler la colonne
  seule ; la bande et le pied restent fixes, et le bouton principal est toujours visible.
* **Typographie** (Inter, échelle de l'installateur, plus grande que celle des apps parce que l'écran est vide) :
  titre 28 / 34 SemiBold (22 à 800 x 600), chapeau 15 / 22 `TEXTE2`, texte 14 / 20, petit 13 / 18 `TEXTE2`,
  chemins et noms techniques DejaVu Sans Mono 13.
* **Cartes de choix** : `SURFACE`, filet `LIGNE`, rayon 10, 16 px de marge intérieure. Choisie : filet de 2 px
  `ACCENT` et fond `ACCENT_CLAIR`. Une carte désactivée garde son texte en `TEXTE3` et dit pourquoi, en clair,
  dans la carte même.
* **Barre du disque** : la façon de montrer un disque partout (Disque, Avancé, Résumé). Hauteur 28 (14 en
  miniature), rayon 6, segments proportionnels avec un minimum de 6 px :

  | Segment | Couleur |
  |---|---|
  | Korin (nouvelle) | `ACCENT` |
  | Windows, autre système | `TEXTE3` |
  | EFI, MSR, récupération | `LIGNE_FORTE` |
  | Espace libre | hachures `LIGNE` sur `SURFACE` |
  | Sera effacé | hachures `DANGER` |
  | Données (`/home`) | `ACCENT_FONCE` |

  Avant → après, l'une sous l'autre, quand un choix change le disque. Les légendes sont sous la barre, jamais
  dedans (une partition de 260 Mo n'a pas la place d'un nom).

## 4. Le parcours

```
           ┌──────────────┐
 clé ───▶  │  Bienvenue   │── Essayer sans installer ──▶ bureau de la clé (icône « Installer Korin OS »)
           └──────┬───────┘
                  ▼
           ┌──────────────┐    Installation manuelle             ┌──────────────┐
           │    Disque    │ ───────────────────────────────────▶ │  Partitions  │
           │ comment ?    │ ◀─────────── Revenir aux choix ───── │  (table)     │
           └──────┬───────┘                                      └──────┬───────┘
                  ▼                                                     │
           ┌──────────────┐                                             │
           │ Place de     │  le schéma en grand, mes fichiers à part    │
           │ Korin        │                                             │
           └──────┬───────┘                                             │
                  ▼ ◀───────────────────────────────────────────────────┘
           ┌──────────────┐
           │   Internet   │  passé d'office : câble branché, ou pas de Wi-Fi
           └──────┬───────┘
                  ▼
           ┌──────────────┐
           │  Logiciels   │  en ligne : le dépôt ; hors ligne : la clé
           └──────┬───────┘
                  ▼
           ┌──────────────┐
           │ Qui êtes-vous│
           └──────┬───────┘
                  ▼
           ┌──────────────┐  « Modifier » sur chaque ligne ramène à l'écran, puis droit au Résumé
           │    Résumé    │
           └──────┬───────┘  Installer / Effacer et installer (seule écriture)
                  ▼
           ┌──────────────┐
           │ Installation │  pas de retour possible une fois le disque écrit
           └──────┬───────┘
                  ▼
           ┌──────────────┐
           │   Terminé    │  « Retirez la clé, puis redémarrez » : le PC démarre le menu de Korin
           └──────────────┘
```

**Disque d'abord** : c'est la décision qui compte et celle qui peut bloquer (aucun disque, pas d'espace libre). Si
elle bloque, l'utilisateur le sait au deuxième écran, pas au sixième.

**« Modifier » depuis le Résumé** : l'écran s'ouvre avec un bouton principal « Revenir au résumé » au lieu de
« Continuer », pour ne pas refaire tout le chemin.

## 5. Les écrans

### 5.1 Bienvenue

* Titre « Installer Korin OS », chapeau « Korin s'installe sur le disque de cet ordinateur, à côté de Windows ou à
  sa place. Comptez une dizaine de minutes. »
* **Langue** : `English | Français` (segmenté). Changer relance Setup dans la langue (comme aujourd'hui,
  `--encore`), en gardant les autres choix dans `/tmp/install`.
* **Pays** (liste) : remplit d'office le clavier et le fuseau (table `PAYS` de Setup, à étendre). Belgique →
  clavier BE, Europe/Brussels.
* **Clavier** (liste de `/res/keyb` : BE, BE-COMMA, FR, US…), pris tout de suite, avec un champ « Essayez le
  clavier ici ». Le défaut à froid est celui de la langue de la clé.
* **Fuseau** : montré en une ligne (« Europe/Brussels ») avec un lien « Changer » qui déplie région puis ville
  (les deux listes de Setup). Aujourd'hui toujours UTC dans le système : la valeur est écrite pour plus tard.
* Pied : à gauche, lien « Essayer Korin sans l'installer » ; à droite, **Installer**.
* Plus tard (étude, étape 9) : si une installation de Korin est trouvée, une troisième ligne « Réinstaller en
  gardant mes fichiers et réglages ».

*Écrit* : `/tmp/install/etc/options.txt` (langue, pays, clavier, fuseau), copié sur la cible à la fin.

### 5.2 Disque : « Comment installer Korin OS ? »

Deux écrans dans la même étape, d'après les installateurs d'**Ubuntu** et de **DrakX** (Mandriva, aujourd'hui
Mageia) : d'abord le choix, où chaque possibilité montre déjà son résultat ; puis le schéma en grand, où l'on règle
la seule chose qui reste à régler. Maquettes : onglets Disque et Place de Korin.

#### 5.2.1 Le choix

* **En haut, le disque tel qu'il est** (DrakX) : « Voici le contenu du disque », la liste des disques à droite
  (« Samsung PM981a · 512 Go · interne », la clé de démarrage jamais listée ; avec un seul disque, la liste reste,
  sans flèche), la barre du disque en grand avec les noms dans les segments assez larges, et la légende des
  couleurs (Windows, système, libre, Korin). Les couleurs sont celles du §3.
* **Trois choix**, une liste de lignes à bouton radio (flèches haut/bas), écrits comme chez Ubuntu, et **chacun
  montre le disque après sous sa phrase** (DrakX) : on compare sans cliquer.

  | Choix | Phrase | Disque après |
  |---|---|---|
  | **Installer Korin OS à côté de Windows** | « Windows et vos fichiers restent. Korin prend les 100 Go libres ; à chaque démarrage, un menu laisse choisir. » | Windows, puis Korin dans l'espace libre |
  | **Effacer le disque et installer Korin OS** | « Supprime Windows et tout ce que contient ce disque : programmes, documents, photos, musique. » (en `DANGER` quand c'est le choix fait) | Korin sur tout le disque |
  | **Installation manuelle** | « Créer, supprimer ou choisir les partitions vous-même. Pour qui a l'habitude. » | — (§8) |

* « À côté de Windows » n'apparaît que si Windows (ou un autre système) est sur le disque ; sans système, le
  premier choix devient « Installer Korin OS sur ce disque vide ». Si le disque porte déjà une partition Korin (le
  ZBook d'aujourd'hui, `KORIN OS`), le premier choix devient « Remplacer l'ancien Korin OS (40 Go) ».
* Korin prend **tout l'espace libre**, sans curseur entre Windows et Korin (réponse 5).
* **Korin ne réduit jamais Windows** (§12). Avec moins de 16 Go libres, « À côté de Windows » reste visible,
  grisé, avec la raison et le geste à faire *dans Windows* : « Il n'y a que 2 Go libres, il en faut 16. Faites de la
  place depuis Windows : Gestion des disques, clic droit sur C:, Réduire le volume. Puis redémarrez sur la clé. » et
  un lien « Comment faire, en 4 images ».
* **BitLocker** trouvé sur la partition Windows : ligne `ALERTE` sous le premier choix : « Windows chiffre son
  disque (BitLocker) : notez sa clé de récupération avant de continuer, il pourra la demander une fois. »
* **Défaut** :
  * Windows et assez d'espace libre (ou une partition Korin) : **le premier choix est fait**.
  * Disque vide : « Installer Korin OS sur ce disque vide » est fait (rien à effacer).
  * Windows sans espace libre : **rien n'est choisi**, Continuer est grisé jusqu'à un clic. On ne présélectionne
    jamais ce qui efface.
* Disque en MBR, ou GPT illisible : le premier choix est grisé (« Ce disque utilise une ancienne table de
  partitions »), les deux autres restent.
* Pied : « Rien n'est écrit avant le résumé. »

#### 5.2.2 La place de Korin : « Voici votre disque après l'installation »

L'écran de répartition d'Ubuntu, adapté : on ne déplace pas la frontière avec Windows (réponse 5, §12), on peut
seulement partager la place de Korin entre le système et les fichiers.

* Chapeau : « Korin prend tout l'espace libre : 100 Go. Windows ne change pas. » (Disque effacé : « Windows (C:,
  410 Go) et tout ce que contient ce disque seront supprimés. », en `DANGER`.)
* **Une petite barre à l'échelle** (« Samsung PM981a, 512 Go, à l'échelle »), pour la vérité des proportions.
* **Le schéma en grand**, 140 px de haut, fait pour être lu plutôt qu'exact : un bloc Windows de largeur fixe
  (glyphe d'écran, « Windows (C:) », 410 Go, « ne change pas », en pointillés `LIGNE_FORTE` sur `CREUX`), puis la
  place de Korin sur tout le reste (bloc `ACCENT`, losange, « Korin OS », la taille en 24 SemiBold). Les petites
  partitions (EFI, récupération) ne sont que dans la barre à l'échelle.
* **Un interrupteur** « Garder mes fichiers sur une partition à part — Pour réinstaller Korin plus tard sans les
  perdre. », **éteint** (réponse 3). Allumé, le bloc de Korin se coupe en deux, « Korin OS, système, `/` »
  (`ACCENT`) et « Mes fichiers, `/home` » (`ACCENT_FONCE`), séparés par **une poignée qu'on fait glisser**, comme
  la séparation d'Ubuntu et de DrakX :
  * les tailles dans les blocs et la barre à l'échelle suivent la poignée ;
  * position de départ : `/` à 64 Go (ou 62 % de l'espace de Korin s'il fait moins de 104 Go) ;
  * bornes : `/` de 16 Go au moins, `/home` de 4 Go au moins ; pas de 1 Go ;
  * clavier : la poignée prend le focus (Tab), flèches ± 1 Go, Page haut/bas ± 10 Go, Début/Fin aux bornes ;
  * `/home` en exFAT (pas de limite de 4 Go par fichier), `/` en FAT32 ;
  * si l'espace de Korin fait moins de 20 Go, l'interrupteur est grisé : « Il faut au moins 20 Go pour séparer
    les fichiers. »
* Le même écran sert après « Effacer le disque » : pas de bloc Windows, Korin a tout le disque.

*Écrit* : `/tmp/install/plan.ini` (étude §3.3), rien sur le disque.

### 5.3 Internet : « Se connecter à Internet »

* Chapeau : « Pour choisir parmi tous les logiciels et avoir les dernières versions. Sans Internet, Korin
  s'installe depuis la clé ; le reste viendra plus tard par Logiciels. »
* Liste des réseaux (lignes de 44, nom, cadenas, force en 4 barres), triée par force. Un clic **déplie la ligne** :
  champ mot de passe (œil pour afficher), bouton Se connecter. Rappel discret sous le champ : « Clavier : BE »,
  pour qui tape un mot de passe sur un clavier qu'il n'a pas choisi.
* États de la ligne : « Connexion… », « Connecté » (coche `SUCCES`), « Mot de passe refusé » (`DANGER`, le champ
  reste ouvert et garde le focus).
* Une fois connecté, l'heure se règle (NTP) en silence ; si elle échoue, l'écran Logiciels le dira.
* Pied : « Continuer sans Internet » (bouton secondaire) tant qu'on n'est pas connecté, **Continuer** ensuite.
* **Passé d'office** quand un câble Ethernet a une adresse (la bande dit « Connecté par câble ») ou quand il n'y a
  pas de Wi-Fi (micrologiciel absent). L'étape reste dans la bande, cochée, et on peut y revenir.

*Écrit* : `/tmp/install/etc/wifi.txt` → `/etc/wifi.txt` de la cible : la machine installée a le réseau au premier
démarrage.

### 5.4 Logiciels : « Que voulez-vous installer ? »

* Cinq cartes cochables, dans l'ordre de l'étude §4 :

  | Groupe | Contenu | Défaut |
  |---|---|---|
  | Système | noyau, services, bureau, apps de base | coché, grisé (« Toujours installé ») |
  | Bureau | Jet, Courier, Photos, Lecteur… | coché |
  | Développement | GCC, .NET, Python, Git, Studio | décoché |
  | Création | Godot, Forge, Vector, Arabesque | décoché |
  | Jeux et émulateurs | | décoché |

* Chaque carte : nom, cinq applications au plus en texte, taille, origine (« sur la clé » ou « à télécharger »),
  et un lien « Choisir » qui déplie la liste des paquets du groupe (lignes de Setup, cases à cocher).
* **Hors ligne** : bandeau en tête « Sans Internet : seuls les logiciels de la clé. Se connecter ». Les groupes
  absents de la clé sont désactivés avec « Avec Internet seulement, plus tard dans Logiciels ». Un groupe en
  partie sur la clé dit « 3 sur 7 sur la clé ».
* **En ligne mais sans heure** (certificat refusé) : même bandeau, « Le dépôt en ligne ne répond pas : seuls les
  logiciels de la clé. Réessayer ».
* Pied : « 2,4 Go, dont 1,9 Go à télécharger · 96 Go libres après ». Si ça ne tient pas : en `DANGER`, Continuer
  grisé.

*Écrit* : la liste des paquets dans `/tmp/install/packages.txt`. Rien n'est installé ici (contrairement à Setup
du Pi) : tout se fait à l'écran Installation.

### 5.5 Compte : « Qui êtes-vous ? »

Repris d'Ubuntu : on tape son nom, le reste se remplit.

* **Votre nom** (« Stéphane »). En dessous, deux champs remplis d'après lui et modifiables :
  * **Nom de l'ordinateur** : `korin-zbook` (« korin- » et le modèle), « Le nom qu'il a sur le réseau. » ;
  * **Nom d'utilisateur** : `stephane` (minuscules, sans accent ni espace), « Son dossier : `/home/stephane`. »
  Un champ modifié à la main n'est plus recalculé.
* **Mot de passe** et **Confirmer le mot de passe**, avec un œil. Sous le premier, une **jauge de force** en quatre
  traits `SUCCES` (« Mot de passe faible / moyen / bon / fort ») ; sous le second, « ✓ Identiques » ou, à la sortie
  du champ, « Les deux mots de passe sont différents. » Facultatif (étude §5.2) ; vide, une ligne le dit : « Sans
  mot de passe, n'importe qui peut ouvrir votre session. »
* **Deux boutons radio** (Ubuntu) : « Ouvrir ma session automatiquement » (choisi quand c'est le seul compte,
  MULTI-UTILISATEUR §2.8) ou « Demander mon mot de passe pour ouvrir la session ».
* Case « **Peut administrer cet ordinateur** », cochée : « Installer des logiciels et changer les réglages du
  système, en redonnant son mot de passe. Le mot de passe de root sera le même ; il se change par `sudo passwd
  root`. » Elle met `stephane` dans `users =` de `/etc/elevate.ini`.
* **Décocher, c'est dire « quelqu'un d'autre administre »** (réponse 6) : un ordinateur confié à un enfant, à un
  proche, à un poste partagé. La case dépliée montre alors, à sa place :
  * la phrase « Quelqu'un d'autre administrera cet ordinateur : choisissez le mot de passe de root et donnez-le à
    cette personne. » ;
  * deux champs **Mot de passe de root** et **Confirmer**, obligatoires, avec la même jauge de force ;
  * le refus d'un mot de passe identique à celui de l'utilisateur, sous le champ : « Choisissez un autre mot de
    passe que le vôtre, sinon vous pourriez administrer quand même. »

  `/etc/elevate.ini` garde alors une liste `users =` vide. Quand cet utilisateur demande une action réservée
  (installer un logiciel, un réglage système, `sudo`), la boîte d'élévation d'authd demande **le mot de passe de
  root** au lieu du sien : l'administrateur le tape à sa place, comme le `su` d'Unix.
* **Mot de passe root** :
  * case cochée : celui du premier utilisateur (réponse 2), sans champ ;
  * case décochée : celui des deux champs ci-dessus ;
  * mot de passe de l'utilisateur vide, case cochée : les deux champs « Mot de passe de root » apparaissent aussi
    et sont obligatoires, car root n'a jamais de mot de passe vide.

  Dans tous les cas, `sudo passwd root` le change ensuite.
* **La règle d'élévation qui en découle**, à reporter dans MULTI-UTILISATEUR (§4, question 2, restée ouverte) :
  authd demande **le mot de passe de l'utilisateur** s'il est dans `elevate.ini` (lui-même ou l'un de ses
  groupes), **sinon celui de root**. Ainsi, un ordinateur a toujours quelqu'un qui peut l'administrer.
* Les groupes, l'image et d'autres comptes viennent ensuite par Réglages › Utilisateurs. Nom réservé (`system`,
  `root`, `admin`) : message sous le champ.

*Écrit* : rien en clair, jamais (étude §5.2). Setup garde l'empreinte en mémoire et l'écrit dans
`/etc/shadow.ini` de la cible à l'étape 6 de l'installation. Le nom de l'ordinateur va dans `/etc/system.ini`.

### 5.6 Résumé : « Prêt à installer »

* Une liste de lignes (libellé 13 `TEXTE2` à gauche, valeur 14 à droite, lien « Modifier » au bout) :
  * Langue et clavier : Français · Belgique · clavier BE
  * Disque : Samsung PM981a, 512 Go · Korin dans les 100 Go libres, à côté de Windows (barre en miniature) ;
    avec la partition des fichiers : « système 64 Go, fichiers 36 Go »
  * Logiciels : Système, Bureau, Développement · 2,4 Go (1,9 Go à télécharger)
  * Compte : Stéphane (`stephane`), administrateur, session ouverte au démarrage
  * Démarrage : menu Korin OS / Windows, Korin dans 5 s — « Modifier » déplie : Korin d'abord / Windows d'abord,
    délai 0, 5, 10 s (étude §6.2)
  * Nom de l'ordinateur : `korin-zbook` — « Modifier » ramène à Qui êtes-vous
* **Le bloc de ce qui sera effacé**, sous la liste, toujours présent :
  * rien : bloc `SUCCES` clair « Rien ne sera effacé. Windows et ses fichiers restent tels quels. » Bouton
    **Installer** (`ACCENT`) ;
  * quelque chose (B, A sur une partition à supprimer, avancé) : bloc `DANGER` qui liste chaque partition (nom,
    étiquette, taille, « contient Windows »), puis **la confirmation** (réponse 1) : « Pour confirmer, tapez le
    nom du disque : **PM981a** » et un champ. Le bouton devient **Effacer et installer** (`DANGER`), grisé jusqu'à ce que le nom
    soit juste (casse ignorée). Étude §3.3 et §8 : c'est le drapeau `wipe = yes` du plan.
* **Batterie** : sur batterie et sous 30 %, ligne `ALERTE` « Branchez le chargeur » et bouton grisé, si Korin sait
  lire la batterie (ACPI) ; sinon un rappel simple.

### 5.7 Installation

* Plein écran, pas d'étapes en haut, pas de pied.
* **Un diaporama** (Ubuntu, DrakX) occupe le haut : une carte de 240 px, à gauche un aplat `ACCENT` avec le losange
  ou une capture, à droite un titre 20 et deux phrases. Cinq cartes sur Korin : bienvenue, Logiciels (« Vos
  logiciels, quand vous voulez »), Fichiers, Givre et Onyx Noir, le Terminal et les langages. Une carte toutes les
  20 s, **sans fondu** (Givre calme) ; flèches gauche/droite pour feuilleter ; points sous le texte.
* Barre de progression de 8 px (`ACCENT` sur `CREUX`, rayon 4), le pourcentage, le temps restant estimé
  (« environ 6 min »), et la tâche en cours en 13 `TEXTE2` (« Copie du système · 1 214 fichiers sur 2 630 »).
* Sous la carte, la progression : « Étape 2 sur 5 · Copie du système, 1 214 fichiers sur 2 630 ». Les cinq
  étapes (regroupement des huit de l'étude §3.3) :
  1. Préparer le disque (plan, GPT, formatage)
  2. Copier le système
  3. Installer les logiciels (clé, puis téléchargements)
  4. Régler l'ordinateur (langue, compte, Wi-Fi)
  5. Rendre Korin démarrable (chargeur, `boot.cfg`, `local.tar`, entrée « Korin OS » en tête de l'ordre UEFI)
* Ligne fixe sous la barre : « Laissez la clé branchée et l'ordinateur sur secteur. »
* Lien « Afficher les détails » : le journal en DejaVu Sans Mono 12, sur `CREUX`, les 12 dernières lignes.
* **Annuler** existe seulement pendant la vérification du plan (avant l'écriture de la GPT) ; ensuite il disparaît.
  Éteindre depuis la bande est refusé avec une question (« L'installation s'arrêtera à mi-chemin… »).
* Un téléchargement qui échoue n'arrête rien : le paquet est noté, la base reste installée depuis la clé (étude
  §4), Terminé le dira.

### 5.8 Terminé

Un seul écran, quel que soit le choix du disque (réponse 4) : l'installateur a déjà installé le menu de démarrage
actuel (`korin.efi` : Korin, Windows, réglages du firmware) et l'a inscrit **en tête de l'ordre de démarrage
UEFI**. Le redémarrage qui suit démarre ce menu, sans F9 ni clé.

* « Korin OS est installé. Retirez la clé USB, puis redémarrez. » Bouton **Redémarrer**. Quand la clé est retirée
  (usbstorage la voit partir), la phrase devient « Redémarrage dans 10 s » avec un décompte, et le bouton reste.
* Une ligne `TEXTE2` dit ce qui attend l'utilisateur : « Au démarrage, un menu laisse choisir Korin OS ou
  Windows ; Korin part seul après 5 s. » (Sans Windows : « Korin OS démarre directement. »)
* Si des logiciels manquent : ligne `ALERTE` « 2 logiciels n'ont pas pu être téléchargés (Godot, Forge) :
  Logiciels les installera dès que l'ordinateur aura Internet. »
* **Si le firmware a refusé l'entrée** (rare, §6) : Korin est installé mais le PC ne le sait pas encore. L'écran
  le dit sans dramatiser : « Korin OS est installé, mais l'ordinateur n'a pas accepté de le démarrer par défaut.
  Gardez la clé : au redémarrage, appuyez sur F9 et choisissez-la ; elle démarrera Korin, qui s'inscrira. » C'est
  la voie de secours de l'étude §6.2, jamais le chemin normal.

**Comment l'entrée est créée.** Setup reste une application. À l'étape « Rendre Korin démarrable », il construit
l'entrée `Boot####` (« Korin OS », la partition par son GUID, le chemin de `korin.efi`), l'écrit et la met en tête
de `BootOrder`. L'écriture passe par `SetVariable`, un service du firmware qu'une application ne peut pas appeler :
le noyau du PC offre pour cela **un seul appel** « écrire une variable UEFI », réservé à Setup pendant
l'installation (§9). L'entrée Windows n'est ni modifiée ni supprimée : elle reste dans l'ordre, derrière Korin, et
le menu de `korin.efi` la propose comme aujourd'hui.

## 6. États d'erreur

Règle : dire ce qui s'est passé, ce qui est intact, et quoi faire. Jamais de code d'erreur seul ; il va dans les
détails.

| Où | Situation | Ce que montre l'écran | Actions |
|---|---|---|---|
| Disque | aucun disque (ni NVMe, ni USB) | « Aucun disque où installer Korin. Branchez un disque USB, ou vérifiez que le disque interne est en mode NVMe dans les réglages du firmware (pas RAID). » | Réessayer (relit les disques), Éteindre |
| Disque | moins de 16 Go libres | « À côté de Windows » grisé, explication et « Comment faire » dans Windows (§5.2.1) | effacer le disque, installation manuelle |
| Disque | MBR ou GPT illisible | « À côté de Windows » grisé, « ancienne table de partitions » ou « table abîmée » | effacer le disque, installation manuelle |
| Disque | disque débranché pendant le choix | bandeau `DANGER` « Le disque WD Elements a été retiré. » Le plan est oublié | choisir un autre disque |
| Internet | mot de passe refusé, réseau perdu | dans la ligne du réseau | réessayer, continuer sans |
| Logiciels | dépôt en ligne injoignable, heure fausse | bandeau, bascule sur la clé | Réessayer |
| Logiciels | ne tient pas sur le disque | pied en `DANGER` | décocher, ou revenir au disque |
| Compte | mots de passe différents, nom réservé ou vide | sous le champ, en `DANGER` | — |
| Résumé | nom du disque mal tapé | le bouton reste grisé, pas de message | — |
| Résumé | la GPT a changé depuis le plan (`nvme` refuse) | « Le disque a changé depuis votre choix. Rien n'a été écrit. » | Revoir le disque |
| Installation | erreur **avant** l'écriture de la GPT | écran d'erreur : « L'installation n'a pas commencé. Rien n'a changé sur le disque. » | Revenir au résumé, Détails |
| Installation | erreur **après** (formatage, copie, paquets de base) | « L'installation n'a pas abouti. » + ce qui est intact : A « Windows démarre comme avant » ; B « Le disque n'a plus de système qui démarre. » | Recommencer l'installation (refait le plan sur le disque tel qu'il est), Détails, Ouvrir un terminal, Éteindre |
| Installation | clé retirée | « La clé a été retirée : l'installation ne peut pas continuer. Rebranchez-la et redémarrez sur la clé. » | Éteindre |
| Installation | le firmware refuse l'entrée « Korin OS » (`SetVariable` en erreur) | Terminé en variante de secours : Korin est installé, F9 sur la clé finit l'inscription (§5.8) | Redémarrer |
| Installation | un paquet facultatif n'a pas pu être téléchargé | rien pendant ; une ligne à Terminé | — |
| Clé | dépôt de la clé à la signature fausse | « Cette clé semble abîmée : refaites-la. » à l'écran Logiciels, la base ne peut pas s'installer | Éteindre |

Les écrans d'erreur gardent le cadre commun : un glyphe de 48 px (`DANGER` ou `ALERTE`), le titre, deux phrases,
les boutons. « Détails » déplie le journal comme à l'installation et propose « Enregistrer le journal sur une clé »
(une autre clé USB : la clé d'installation est en lecture seule).

## 7. Le clavier seul

| Touche | Effet |
|---|---|
| Tab, Maj+Tab | contrôle suivant, précédent, dans l'ordre de lecture ; le pied en dernier. Anneau de focus Givre (2 px `ACCENT` à 45 %) |
| Entrée | le bouton principal de l'écran, s'il est actif ; dans un champ de mot de passe d'un réseau : Se connecter |
| Échap | Retour (rien pendant l'installation) ; referme un dépliage ou une liste ouverte d'abord |
| Flèches | dans un groupe de choix (les trois choix du disque, langue, réseaux, session automatique) : déplacent **et** choisissent, comme des boutons radio |
| Espace | coche, décoche une carte de Logiciels, une case |
| Alt+← | Retour (habitude des navigateurs) |
| Ctrl+Maj+K | ouvre la liste des claviers (pastille de la bande) depuis n'importe quel champ |
| Ctrl+Alt+T | ouvre un terminal sur la clé (le menu ⏻ y mène aussi) |
| F1 | l'aide de l'écran (le texte de `help_fr.md`, dans un volet) |

À l'ouverture de chaque écran, le focus va au premier contrôle utile : le champ du nom (Compte), le choix déjà
fait (Disque), le champ de confirmation quand il y en a un (Résumé), sinon le bouton principal. Ainsi, au chemin le
plus court, **Entrée** fait avancer d'un écran à l'autre.

## 8. Le mode avancé : « Installation manuelle »

* **Entrée** : le troisième choix du Disque, « Installation manuelle ». Il n'y a pas de case « mode expert » au
  début.
* **L'écran** reprend DiskDrake (DrakX) et « Autre chose » d'Ubuntu, sans boîte de dialogue : colonne plus large
  (940 px, toute la largeur sous 1024).
  * Chapeau : « Cliquez sur une partition, puis réglez-la à droite. Rien n'est écrit avant le résumé. »
  * **La barre du disque en grand, cliquable**, les noms dans les segments, et **la légende** dessous avec la taille
    de chaque partition (DrakX) ; un clic choisit le segment et la ligne de la table.
  * **La table** : partition, « utilisée comme » (système de fichiers), « montée sur », taille, et l'**état**
    (Garder, Formater, Supprimer, Nouvelle) en pastille.
  * Sous la table : **+ Créer** (sur un espace libre) et **− Supprimer** ; à droite **Tout défaire**.
  * **Le volet de droite** (240 px) règle la partition choisie, à la place de la boîte « Modifier » d'Ubuntu :
    taille, « utilisée comme », « montée sur », case Formater (cochée et grisée pour une nouvelle partition).
  * Les listes « montée sur » (`/`, `/home`, `/data`, `/media/…`, aucun) et « utilisée comme » viennent de la table
    de l'étude §3.2 : un système de fichiers nouveau s'y ajoute sans changer l'écran.
  * Une case « Garder la partition EFI (recommandé) », cochée (étude §3.1).
  * Une **liste de contrôle** vivante : « ✓ `/` est défini (80 Go, FAT32) », « ✓ La partition EFI est gardée », « ✗
    `/` doit faire au moins 16 Go ». Continuer reste grisé tant qu'une ligne est ✗.
* **Pas de redimensionnement** de partition existante (§12) : « Taille » n'est modifiable que pour une partition
  nouvelle.
* **Les protections ne changent pas** : la partition de la clé n'apparaît pas ; l'EFI gardée n'est jamais écrite ;
  une partition Windows peut être supprimée, mais sa ligne garde l'étiquette « Windows » et le Résumé la reprend
  en `DANGER`.
* **Sortie** : « Revenir aux choix simples » (bouton fantôme du pied, à la place de Retour) oublie le plan manuel
  après une question si quelque chose a été changé. Continuer mène à Internet (le schéma de la place de Korin
  n'a pas lieu d'être).
* Les autres réglages fins sont des lignes « Modifier » du Résumé (démarrage, nom de l'ordinateur) et
  l'interrupteur des fichiers avec sa poignée : jamais un écran de plus.

## 9. Ce que ce design demande au reste

* **Setup** : mode `--install --target /target` plein écran, `WF_FRAMELESS` sans bande d'Elegant (une session de
  la clé qui ne lance ni panel ni fond) ; les pastilles de la bande (réseau, clavier, thème, alimentation) dessinées
  par Setup lui-même, pas par Elegant ; l'aide F1.
* **Détection** : Windows (`\Windows\System32`, déjà prévu), BitLocker (signature `-FVE-FS-`, déjà connue des refus
  de `policy.c`), partition Korin existante, câble Ethernet, présence du Wi-Fi, batterie (si ACPI).
* **Le nom court du disque** pour la confirmation : modèle NVMe ou USB, réduit au dernier mot significatif
  (« PM981a »), unique parmi les disques montrés.
* **Le noyau du PC** : un appel système « écrire une variable UEFI » (`SetVariable`), refusé à tout programme sans
  droits et permis seulement pendant l'installation. Il demande que le chargeur passe au noyau le pointeur des
  services d'exécution UEFI, et que le noyau rende accessibles, le temps de l'appel, les zones que la carte mémoire
  UEFI marque comme utilisables après le démarrage (à leur adresse physique, interruptions coupées, un seul
  processeur). C'est ce que fait Linux pour `efibootmgr`. L'étude §6.2 écartait cette voie ; elle est à revoir.
* **Le chargeur de la clé**, en secours seulement : dans son menu actuel, la ligne « Korin OS installé » quand une
  cible a `firmware = install` (étude §6.2), pour le cas où le firmware refuse l'entrée.
* **Les comptes** : root reçoit l'empreinte du mot de passe du premier utilisateur ; `sudo passwd root` doit
  pouvoir la changer ensuite (à reporter dans MULTI-UTILISATEUR, étape 4 : `passwd` par authd). Si « Peut
  administrer » est décoché, root a son propre mot de passe, et authd demande celui de root à tout utilisateur
  absent d'`elevate.ini` (§5.5).
* **Le plan du disque** : la partition `/home` facultative, en A comme en B, avec la taille choisie au curseur.
* **Premier démarrage du système installé** : le menu de `korin.efi`, puis la session ; Setup ne se relance pas
  (étude §5.1).

## 10. Maquettes

[maquettes/installateur/index.html](maquettes/installateur/index.html), à ouvrir dans un navigateur. Onglets :
Bienvenue, Disque (à côté de Windows, pas d'espace libre, effacer le disque), Place de Korin (simple, mes fichiers
à part, disque effacé : la poignée se fait glisser), Manuel, Internet (choisir, connecté), Logiciels
(en ligne, hors ligne), Qui êtes-vous, Résumé (rien d'effacé, disque entier), Installation, Terminé (normal, logiciels manquants, entrée refusée), Erreurs (aucun
disque, échec après l'écriture). Le bouton Givre / Onyx Noir change les jetons ; 1280 x 800 et 800 x 600 montrent
le même écran aux deux tailles. Les couleurs sont celles de `uk_jet_givre` et `uk_jet_noir`
(`src/kits/uikit/uk_theme.c`).

## 11. Réponses de l'utilisateur (15/11/2026)

1. **Confirmer un effacement** : taper le nom court du disque (« PM981a »), §5.6.
2. **Mot de passe root** : le même que celui du premier utilisateur, sans champ ; `sudo passwd root` le change
   ensuite (§5.5, §9).
3. **Partition à part pour les fichiers** : case décochée par défaut ; cochée, un curseur partage l'espace de
   Korin entre `/` et `/home`, la position montre les deux tailles (§5.2.2). Proposée à côté de Windows comme sur un disque effacé.
4. **Démarrage** : l'installateur installe le menu de démarrage actuel (`korin.efi`) et dit à l'UEFI de le
   démarrer par défaut, sans F9 ni redémarrage par la clé (§5.8, §9). Précisé après une première lecture qui
   gardait F9.
5. **Taille de Korin à côté de Windows** : tout l'espace libre, sans curseur (§5.2).
6. **« Peut administrer » décoché** : quelqu'un d'autre administre. Un mot de passe de root distinct devient
   obligatoire, et l'élévation demande le mot de passe de root à qui n'est pas dans `elevate.ini` (§5.5).

## 12. Inspirations, et ce qu'on n'en prend pas

Après la première version, l'utilisateur a demandé des écrans plus accueillants, surtout le disque, en regardant
Ubuntu et l'installateur de Mandriva (DrakX, toujours celui de Mageia).

| Repris | D'où | Où |
|---|---|---|
| Le disque tel qu'il est, en haut, avec la légende des couleurs | DrakX | §5.2.1 |
| Le disque *après* sous chaque choix, pour comparer sans cliquer | DrakX | §5.2.1 |
| Des choix écrits comme des phrases (« à côté de Windows », « effacer le disque ») et la liste de ce qui disparaît | Ubuntu | §5.2.1 |
| Le schéma en grand et la séparation qu'on fait glisser | Ubuntu, DrakX | §5.2.2 |
| Barre cliquable, légende, table, volet de réglage | DiskDrake, « Autre chose » d'Ubuntu | §8 |
| « Qui êtes-vous ? », champs remplis d'après le nom, jauge du mot de passe, session automatique ou non | Ubuntu | §5.5 |
| Diaporama pendant l'installation | Ubuntu, DrakX | §5.7 |

**Ce qu'on ne prend pas : réduire la partition Windows.** Ubuntu et DrakX (« Use free space on a Windows
partition ») déplacent la frontière avec Windows en réduisant son NTFS. La documentation de Mageia elle-même dit
l'opération risquée : sauvegarde conseillée, Windows arrêté proprement, partition défragmentée. Pour Korin, c'est
exclu (réponse 5 ; étude §3.1) :

* **Korin ne sait pas écrire du NTFS.** Réduire un NTFS, c'est déplacer des clusters et réécrire la MFT et le
  bitmap ; une erreur perd les fichiers de Windows. `ntfsresize` (Linux) a mis des années à être fiable.
* **Windows laisse souvent son disque « sale »** : le démarrage rapide et la mise en veille prolongée gardent le
  volume ouvert. Le réduire de l'extérieur dans cet état corrompt le système de fichiers.
* **BitLocker** chiffre le volume : on ne peut pas le réduire sans le déverrouiller, et Windows demanderait sa clé
  de récupération ensuite.
* **Des fichiers que Windows ne laisse pas déplacer** (fichier d'échange, hibernation, clichés instantanés)
  peuvent se trouver à la fin du volume.
* Windows le fait lui-même sans risque : **Gestion des disques › Réduire le volume** gère BitLocker, les clichés
  et les fichiers immobiles. L'installateur le dit, et le pilote NVMe refuse de toute façon d'écrire dans une
  partition NTFS ou BitLocker (refus fixes de `policy.c`).
