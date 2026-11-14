# WebStudio : étude de design

Étude du 14/11/2026, demandée avant d'écrire WebStudio (plan de [etude/PC-PILOTES.md](etude/PC-PILOTES.md), « Ordre
du plan » : serveur web maison et WebStudio, avant SDL). **Proposition à valider par l'utilisateur** : rien n'est écrit
dans `src/`. **Réponses de l'utilisateur le 14/11 : §7** ; les maquettes et ce document les suivent.

**Maquettes** (six écrans, Givre et Onyx Noir au choix par écran, bouton *Tweaks*) :
https://claude.ai/artifact/JrC2yQS1RtJsw71MjNFeS5

**Décision de l'utilisateur (14/11)** : WebStudio est une app **C++ native sur UIKit** (pas Avalonia ni .NET). Les
règles des ports s'appliquent comme pour QBStudio : appels `ui_*` directs, aucun pont, jetons du thème
(`ui_tokens()`), aucune couleur en dur.

**Niveau de preuve.** Les maquettes sont dessinées en HTML aux pixels réels, avec les jetons de `uk_theme.c` (Givre,
Onyx Noir), les couleurs du code de `ta_color()` (`uk_area.c`) et la géométrie de Givre calme
([DESIGN-PROPOSAL.md](DESIGN-PROPOSAL.md)). Elles ne sont pas des captures : rien n'a été compilé ni essayé, ni sous
QEMU ni sur le ZBook. Elles supposent un écran de **1920 × 1080** sur le ZBook (à confirmer) et 800 × 600 sur le Pi.
Les icônes sont dessinées au trait pour l'étude : les vraies viendront de la chaîne `tools/icons`.

---

## 1. Ce que WebStudio doit être

Un outil à la WebMatrix : on ouvre un dossier, on écrit, on voit la page, sans rien configurer. Il doit rester
« léché, pro et facile », avec la même exigence que la finition du Studio.

1. **Un site est un dossier.** Pas de fichier de projet obligatoire. Tout dossier s'ouvre. Les réglages propres au site
   (port, fichiers ouverts) vont dans `.webstudio/` à sa racine, qu'on peut ignorer dans git.
2. **Le serveur est celui de Korin.** WebStudio ne contient pas de serveur à lui : il lance le serveur maison en
   **instance privée** sur le dossier ouvert (`127.0.0.1`, premier port libre à partir de 8080). Rien n'est visible sur
   le réseau pendant l'édition. **Publier** consiste à inscrire le site dans la configuration du serveur, celle que
   montre l'applet Réglages › Serveur web.
3. **Voir tout de suite.** Enregistrer recharge la page dans Jet. Visé : moins de 100 ms entre Ctrl+S et la page redessinée
   d'un petit site, sur le ZBook.
4. **Clavier d'abord.** Les raccourcis de Visual Studio et de VS Code là où ils existent (F5, Ctrl+P, Ctrl+Maj+F…).
5. **Givre calme partout.** L'accent marque seulement le choix ou l'action principale (onglet actif, fichier choisi,
   bouton Créer). La couleur vit dans les icônes (types de fichiers). Contrôles de 32, lignes de 30, texte 14, petit
   texte 13, détails 12. Aucun fondu, réponse sous 50 ms. Les 96 px de `FRAME_CAP_W` restent libres en haut à droite.
6. **La même app sur le Pi.** À 800 × 600, un seul volet à la fois (§2.9), mêmes commandes et mêmes raccourcis.

## 2. Les écrans

### 2.1 Accueil (maquette 2)

Montré quand aucun site n'est ouvert, au lancement et après Fichier › Fermer le site.

* À gauche, ce qu'on peut faire : **Nouveau site…** (Ctrl+N, seule ligne teintée d'accent), **Ouvrir un dossier…**
  (Ctrl+O), **Cloner un dépôt git…**. Dessous, un lien vers les Premiers pas de l'aide, et une phrase : on peut glisser
  un dossier sur la fenêtre.
* À droite, **Récents** : nom, chemin, genre (Statique, PHP, ASP.NET Core), date. Un site dont le gestionnaire manque
  le dit sur sa ligne (« PHP pas encore installé ») au lieu d'échouer à l'ouverture. Entrée ouvre, Suppr retire de la
  liste.
* Dessous, **Sur le serveur web de ce PC** : les sites publiés, leurs liaisons, leur état, avec un lien vers l'applet.

### 2.2 Nouveau site (maquette 3)

Un dialogue `WF_MODAL` centré sur le voile (`TEXTE1` à 30 %) :

* **Modèle** : Vide, Page simple, Site vitrine (plusieurs pages, menu commun, page 404), Depuis un dossier. PHP et
  ASP.NET Core sont montrés **grisés avec « bientôt »** tant que leurs gestionnaires manquent : on voit où l'outil va,
  sans proposer ce qui ne marche pas.
* **Nom** et **Dossier** (par défaut `SD1:/home/sites/<nom>` : ce sont des projets de l'utilisateur, ils vont dans
  `home` ; `HOME:/home/sites` quand les volumes seront renommés).
* **Créer un dépôt git** (coché par défaut) ; **Ajouter aussi au serveur web de ce PC** (décoché : publier reste un
  geste volontaire).
* Un encadré dit en une phrase où tourne le site pendant l'édition.

### 2.3 L'espace de travail (maquette 1, choisie)

Une fenêtre en quatre zones, toutes réglables par séparateurs (`ui_splitter`), retenues par site :

| Zone | Contenu | Taille par défaut (1920 × 1080) |
|---|---|---|
| Barre d'outils | site (menu des récents), **pastille du serveur** (état, adresse cliquable, arrêter, redémarrer), **Voir dans Jet** (F5), champ **Aller à** (Ctrl+P), **Publier…** | 52 px |
| Volet de gauche | **l'arbre seul** (décision du 14/11) : fichiers et dossiers du site. **Double-clic sur un dossier** : le déplier ou le replier ; **double-clic sur un fichier** : l'ouvrir dans un onglet, ou montrer son onglet s'il est déjà ouvert. Un fichier modifié est marqué. Ni rail, ni boutons, ni plan : créer, renommer et supprimer passent par le menu Fichier et le clic droit | 300 px |
| Éditeur | onglets de documents (`ui_tabstrip`), fil d'Ariane (dossier › fichier › balises), code ; **pas d'aperçu dans la fenêtre** (§2.5) | le reste |
| Panneau du bas | Serveur, Problèmes, Recherche, Terminal (Ctrl+J le replie) | 230 px |
| Barre d'état | serveur et adresse, mode de rechargement, branche git, Ln/Col, indentation, encodage, fins de ligne, langage | 26 px |

Les menus de l'app sont dans la barre de menus d'Elegant : **Fichier, Édition, Affichage, Site, Exécuter, Aide**.

### 2.4 L'éditeur

* **Langages** : HTML (avec CSS et JavaScript inclus), CSS, JavaScript, JSON, XML, Markdown ; PHP et C#/Razor dès la
  première version de l'éditeur (la syntaxe), même si les gestionnaires viennent après.
* **Complétion** : balises et attributs HTML, propriétés et valeurs CSS, **chemins du site** dans `href` et `src`
  (maquette 1 : `img/` propose les images avec leurs dimensions et leur poids), **classes** des feuilles du site dans
  `class=""`, identifiants dans `#`. Fermeture automatique des balises et des guillemets.
* **Vérifications pendant la frappe**, soulignées en ondulé `WARNING` : lien vers un fichier absent du site
  (`tarifs.html` dans la maquette), image sans `alt`, balise mal fermée, propriété CSS inconnue. Elles alimentent
  l'onglet Problèmes.
* **Ctrl+clic** sur un chemin ouvre le fichier ; sur une classe, la règle CSS.
* **Couleurs CSS** : une pastille devant chaque couleur (maquette 5). Un clic ouvre le sélecteur de couleur d'UIKit.
* Rechercher et remplacer dans le fichier (Ctrl+F, Ctrl+H) et **dans tout le site** (Ctrl+Maj+F : les résultats
  vont dans l'onglet Recherche du panneau du bas).
* Formater le document (Maj+Alt+F). Commenter (Ctrl+/). Pas de multi-curseur dans la première version.

### 2.5 L'aperçu : Jet, à côté (maquette 4)

Décision du 14/11 : **l'aperçu reste séparé de WebStudio**, comme dans WebMatrix. Lancer le site (**F5**, Voir dans
Jet) démarre au besoin le serveur du site et **ouvre Jet juste à côté** : WebStudio à gauche (environ 60 % de l'écran),
Jet à droite.

* La page est **servie par le serveur** (`http://localhost:8080/…`), pas lue en `file:` : ce qu'on voit est ce que le
  serveur rendra, PHP et .NET compris plus tard.
* **Un onglet par site, réutilisé** : un second F5 ramène Jet devant et montre la page du fichier actif dans le même
  onglet, sans ouvrir de nouvel onglet ni de nouvelle fenêtre.
* **Rechargement à l'enregistrement** (décision du 14/11), pas pendant la frappe. Le défilement de la page est gardé.
* F5 montre la page du fichier actif : sur `contact.html`, Jet ouvre `/contact.html`. Sur une feuille CSS ou un
  script, la page déjà ouverte.
* Jet garde tout ce qu'il a : onglets, adresse, outils. Fermer Jet ne touche ni au serveur ni à WebStudio.
* À 800 × 600 : Jet s'ouvre en plein écran ; Alt+Tab revient à WebStudio.

### 2.6 Le panneau du bas

* **Serveur** : le journal de l'instance privée (heure, méthode, chemin, code, durée, taille). Les codes en couleur
  (200 `SUCCESS`, 404 `WARNING`, 500 `DANGER`). Un **404 sur un fichier du site propose de le créer** (« Créer
  tarifs.html »). Filtre texte, bascule Tout | Erreurs, Effacer.
* **Problèmes** : ceux de l'éditeur (§2.4) et ceux du serveur (erreurs des gestionnaires), cliquables.
* **Recherche** : les résultats de Ctrl+Maj+F, par fichier, avec Remplacer tout.
* **Terminal** : un shell dans le dossier du site, sur un pty de `vterm` ([VTERM.md](VTERM.md)).

Le panneau ne s'ouvre jamais seul, sauf pour une erreur du serveur au démarrage.

### 2.7 Git

Pas de rail : **Site › Git…** ouvre un dialogue avec les fichiers changés, un champ de message et **Valider**
(Ctrl+Entrée), **Pousser**, **Tirer**. La barre d'état montre la branche et le nombre de changements ; un clic ouvre le
même dialogue. Pas d'outil de fusion dans la première version : un conflit renvoie
au Terminal.

### 2.8 Publier

**Site › Publier sur le serveur web de ce PC…** : choisir les liaisons (hôte, port, http ou https) et le certificat,
puis le site apparaît dans l'applet. Publier vers une autre machine (FTP, SFTP, autre Korin) viendra plus tard.

### 2.9 Le Pi et le PocketTerm, 800 × 600 (maquette 5)

Un seul volet à la fois :

* barre de 40 px : ☰ (l'arbre en **tiroir**, Ctrl+B, mêmes double-clics), nom du site, pastille du serveur, **Jet** (F5), arrêter
  ou lancer ;
* onglets, code, barre d'état réduite (port, nombre de problèmes, Ln/Col, langage) ;
* le panneau du bas s'ouvre par-dessus le code (Ctrl+J), en demi-hauteur.

### 2.10 L'applet Réglages › Serveur web (maquette 6)

Même langage visuel, pour le chantier du serveur qui précède WebStudio :

* en tête : état du serveur et interrupteur **Démarrer avec Korin** ;
* **Sites** : nom, dossier, liaisons, état ; Ajouter, Arrêter, Ouvrir dans WebStudio, Supprimer ;
* **Liaisons** du site choisi : schéma, hôte, port, certificat ; « + Ajouter une liaison » ;
* **Certificat** : nom, émetteur, fin de validité, date du renouvellement automatique ; Renouveler maintenant ;
  Autre certificat… (auto-signé, demande CSR puis certificat reçu et sa chaîne, Let's Encrypt) ;
* **Gestionnaires** : Fichiers statiques (intégré), PHP (Installer le paquet…), .NET (bientôt).

## 3. Deux dispositions : A ou B

| | **A, aperçu intégré** (écartée, plus de maquette) | **B, Jet à côté** (maquettes 1 et 4, choisie) |
|---|---|---|
| Fenêtres | une | deux, WebStudio place Jet à droite |
| Appareils simulés | oui | non (redimensionner Jet à la main) |
| Lien code ↔ page | possible (Alt+clic, plus tard) | non |
| Console de la page dans WebStudio | oui | non |
| 800 × 600 | bascule Code / Aperçu | deux fenêtres plein écran à alterner |
| Travail | relier le moteur de Jet à WebStudio (§5.3) | presque rien : lancer Jet, le recharger |

**B choisie le 14/11** : l'aperçu reste séparé, Jet s'ouvre à côté. (Un premier choix de A, le même jour, a été
remplacé.) WebStudio reste plus léger (pas de WebKit dedans) et l'utilisateur garde un vrai navigateur.

## 4. Raccourcis

| Touche | Action |
|---|---|
| F5 / Maj+F5 | voir le site dans Jet (le serveur démarre au besoin) / arrêter le serveur du site |
| Ctrl+S | enregistrer (et recharger la page dans Jet) |
| Ctrl+P | aller à un fichier ; `>` pour une commande, `@` pour un symbole du fichier |
| Ctrl+Maj+F | rechercher dans le site |
| Ctrl+B / Ctrl+J | arbre des fichiers / panneau du bas |
| Ctrl+Tab, Ctrl+W | document suivant, fermer le document |
| Ctrl+/ , Maj+Alt+F | commenter, formater |
| Ctrl+clic | suivre un chemin ou une classe |

Lettres par la position HID, comme Jet et l'éditeur, pour que AZERTY et QWERTY tombent sur les mêmes touches.

## 5. Ce que ça demande, en C++ sur UIKit

### 5.1 L'app

`src/apps/webstudio` (`webstudio.app`), C++ sur libposix, construite par `tools/office/build.sh` pour le Pi et le PC.
Catégorie Développement. Structure inspirée de QBStudio (`codeedit.h` : éditeur réglé, complétion par
`ui_popup_local`, plan, problèmes). Réglages de l'app et récents : comme QBStudio, un `webstudio.ini` à côté de ses
projets.

### 5.2 Dans UIKit (ce qui sert à plusieurs apps va dans le kit)

* **Analyseurs de couleurs** : UIKit n'a que C# et XML (`uk_lex.c`, `UTA_CS`, `UTA_XML`). À ajouter : `UTA_HTML` (avec
  CSS et JS inclus, état par ligne comme XML), `UTA_CSS`, `UTA_JS`, `UTA_JSON`, `UTA_MD`, `UTA_PHP`, `UTA_RAZOR`. Les
  genres `UKC_*` existants suffisent : balise = mot-clé, attribut et propriété = étiquette, valeur = chaîne. Nouvelles
  fonctions **en fin de table** de `uikit.abi`.
* **Repères dans le texte** : soulignés ondulés, pastilles de couleur devant une plage, marques dans la gouttière.
  Proposé : `ui_textarea_mark (w, début, fin, genre, donnée)` et `ui_textarea_clear_marks`.
* **Complétion riche** : `ui_popup_local` n'a que du texte ; la maquette montre des vignettes et une colonne de
  détails. Soit une liste à icônes dans le menu local, soit une petite fenêtre `WF_GRAB` comme le lanceur du panel.
* **Couleurs du code en Onyx Noir** (accord du 14/11) : `UKC_LABEL` prenait `ACCENT_DARK` (`#2560C0`), peu lisible
  sur `#1B1E23` (≈ 3 : 1). Il prend maintenant `ACCENT_VIVID` dans les thèmes sombres, pour toutes les apps
  (`ta_color()` de `uk_area.c` et `uk_ta_color` de `uikit_area.inc`, le 14/11 ; **pas encore compilé ni essayé**).
* **Texte en `WARNING` sur blanc** (accord du 14/11) : `#C4861C` sur `#FFFFFF` ne donne qu'environ 3 : 1. Le jeton reste
  pour les pastilles et les soulignés ; un **texte** en avertissement se mélange à `TEXTE1`, comme les nombres du code
  (`ui_mix`), ce qui donne `#9A6A12` environ. Même règle pour `DANGER` en Noir. Pas de jeton nouveau (la table des
  jetons ne bouge pas).

### 5.3 Jet à côté

Ce qu'il faut pour que F5 ouvre Jet au bon endroit et que la page suive les enregistrements :

1. **Ouvrir ou réutiliser** : Jet accepte déjà des adresses en arguments (`main url1 url2 …`, un onglet par
   adresse). Il manque d'ouvrir une adresse **dans le Jet déjà lancé** et d'y **réutiliser l'onglet** du site au lieu
   d'en créer un. Proposé : Jet s'inscrit comme service nommé (`jet`) et accepte un message « ouvrir cette adresse,
   onglet `webstudio:<site>` » ; un second `jet` lancé le lui passe et s'en va, comme `home` en mode poche.
2. **Placer les deux fenêtres** : Jet doit pouvoir s'ouvrir à une place donnée (argument de géométrie), et WebStudio
   se placer à gauche. Un programme ne sait pas encore déplacer sa fenêtre (`UIKIT-MANQUES.md`, « Déplacer sa fenêtre
   depuis le programme ») : à ajouter à UIKit (`ui_window_place` ou équivalent), et le dire à Elegant.
3. **Recharger** : par le serveur, sans rien changer dans Jet. En mode développement, l'instance privée ajoute à chaque
   page HTML un petit script qui écoute `/__webstudio/reload` (événements serveur) ; à chaque Ctrl+S, WebStudio le dit
   au serveur, qui prévient les pages ouvertes. Ça marche aussi dans un autre navigateur, ou depuis un autre PC si le
   site est publié en développement. Repli : un message sur le bus (`korin.jet.reload`).

### 5.4 Le contrat avec le serveur maison

Ce que le serveur doit offrir pour que WebStudio n'ait aucun serveur à lui (à reprendre dans l'étude du serveur) :

* lancer une **instance privée** : un dossier, une adresse `127.0.0.1`, un port (ou « premier libre », rendu à
  WebStudio), sans toucher à la configuration des sites publiés ;
* un **journal structuré**, une ligne par requête (heure, méthode, chemin, code, durée, taille, erreur du
  gestionnaire), lu par WebStudio sur un canal ou un tube ;
* en mode développement, sans cache, et, pour les navigateurs extérieurs, un petit script de rechargement injecté
  (événements serveur sur `/__webstudio/reload`) ;
* une **configuration des sites** lisible et modifiable par l'applet et par WebStudio (Publier), dans un format à
  fixer avec le serveur.

### 5.5 Fichiers changés hors de l'éditeur

Korin n'a pas de notification de changement de fichiers. Pas de sondage permanent : WebStudio compare les dates de
modification des fichiers ouverts quand sa fenêtre reprend le focus et après chaque commande du Terminal. Un fichier
changé et non modifié dans l'éditeur est rechargé. S'il est aussi modifié dans l'éditeur, un bandeau propose de garder
l'une ou l'autre version.

## 6. Étapes proposées

| Étape | Contenu | Effort |
|---|---|---|
| 0 | Le serveur maison et son applet (chantier précédent) avec le contrat du §5.4 | (à part) |
| 1 | Accueil, Nouveau site, ouvrir un dossier, arbre, onglets, éditeur ; analyseurs HTML/CSS/JS/JSON dans UIKit | M |
| 2 | Instance privée du serveur, pastille, panneau Serveur, barre d'état | M |
| 3 | Voir dans Jet (§5.3) : service de Jet et onglet réutilisé, placement des fenêtres, rechargement par le serveur | M |
| 4 | Complétion (chemins, classes), vérifications, Problèmes, plan, repères dans le texte | M à L |
| 5 | Dialogue Git, Terminal, Publier, mise en page 800 × 600 | M |
| 6 | PHP et Razor quand leurs gestionnaires seront branchés | plus tard |

S : moins d'un jour, M : quelques jours, L : une à deux semaines.

## 7. Réponses de l'utilisateur (14/11)

1. **Aperçu séparé** (B) : lancer le site ouvre Jet juste à côté, comme dans la maquette 4. (Un aperçu intégré, qu'on
   pouvait cacher, a d'abord été choisi, puis remplacé le même jour.)
2. Sites dans **`SD1:/home/sites`** : ce sont des projets de l'utilisateur, ils vont dans `home`.
3. Rechargement **à l'enregistrement**.
4. À gauche, **seulement un panneau avec l'arbre** des fichiers et dossiers : double-clic sur un dossier = le déplier
   ou le replier, double-clic sur un fichier = l'ouvrir s'il ne l'est pas (sinon montrer son onglet). Pas de rail.
5. Couleurs du code en Noir et texte en `WARNING` : **corriger dans UIKit** pour toutes les apps (§5.2).
6. Icône **violette avec `</>`** : d'accord.
7. Écran du ZBook : **1920 × 1080**.

## 8. État de la première version (14/11)

`src/apps/webstudio` (`webstudio.app`, C++ sur UIKit), construit pour le PC par `tools/pc/apps.sh webstudio` ; paquet
`webstudio` (il demande `webd`). Icône violette `</>` (`tools/icons`).

**Fait** : l'accueil (Nouveau site, Ouvrir un dossier, récents dans `SD0:/home/webstudio.ini`, sites du serveur de ce
PC) ; Nouveau site (modèles Vide, Page simple, Site vitrine ; `SD1:/home/sites/<nom>` ; dépôt git ; ajout au serveur) ;
l'arbre seul à gauche (double-clic : un dossier se déplie, un fichier s'ouvre ou montre son onglet) ; onglets et
éditeur (couleurs : HTML par l'analyseur XML d'UIKit, CSS et JavaScript par celui de C#) ; l'instance `webd --dev`
(pastille, Arrêter et Démarrer, journal en bas ; double-clic sur un 404 : créer le fichier) ; F5 lance Jet sur la page
du fichier actif, un second F5 recharge ; Ctrl+S enregistre et recharge la page ; rechercher dans le fichier et dans
le site ; nouveau fichier, dossier, renommer, supprimer ; Git (créer le dépôt, valider) ; Publier ; Terminal dans le
dossier du site.

**Vérifié sous QEMU** (Korin PC, 14/11) : l'espace de travail sur un site (arbre, onglet coloré, instance webd lancée,
requêtes de l'hôte dans le journal : 200, 200, 404 « fichier absent », script de rechargement dans la page) et
l'accueil (captures). **Pas essayé** : les dialogues et les commandes au clic (Nouveau site, Git, Publier, 404 → créer),
F5 avec Jet (Jet n'est pas construit dans le cloud), l'écran du ZBook.

**Pas encore fait** : Jet placé à droite et onglet réutilisé (§5.3 : service de Jet, déplacement de fenêtre dans
UIKit) ; analyseurs HTML, CSS, JS, PHP, Razor à eux dans UIKit et repères dans le texte (§5.2) ; complétion (chemins,
classes) et vérification des liens ; fichiers changés hors de l'éditeur (§5.5) ; Aller à (Ctrl+P) ; mise en page
800 × 600 soignée ; le Pi (construction AArch64).
