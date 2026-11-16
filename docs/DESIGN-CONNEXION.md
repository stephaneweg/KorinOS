# Design : l'écran de connexion

**Fait le 16/11/2026** (première version, PC) : `src/sys/login/login.c` dessine la carte A (A1 plusieurs comptes, A3 sans mot de passe, A4 un seul compte), sur le fond à facettes lancé par Elegant ; menus d'alimentation de UIKit. Pas encore : le verrou (A5), le changement d'utilisateur (A9), les menus clavier et accessibilité (A7, A8), les images des comptes, l'état de Verr. Maj. ; le thème de l'écran de connexion (`/etc/login.ini`).

Proposition du 15/11/2026, à la demande de l'utilisateur (« designer le futur écran de login, proposition fenêtrée ou
sans fenêtres »). **Rien n'est fait dans `src/`** : le login n'existe pas encore.

**Décisions de l'utilisateur (15/11/2026)** :

1. **Variante A, la carte** (« la variante avec cadre »), **sans la barre de menus**, qui ne se lance qu'à l'ouverture
   de session. L'heure passe dans l'en-tête de la carte, les commandes dans son pied (§4).
2. **Le thème est par utilisateur**, comme la langue, le dock et les autres réglages généraux : ils iront dans un
   dossier **`/home/<utilisateur>/etc`** (§2, principe 6).
3. **Le thème de l'écran de connexion se règle dans le panneau de configuration** (Réglages). Ce document part de l'étude
[`etude/MULTI-UTILISATEUR.md`](etude/MULTI-UTILISATEUR.md) (§2.1 comptes, §2.6 élévation, §2.8 connexion et session),
de [`etude/INSTALLATEUR.md`](etude/INSTALLATEUR.md) §5.2 (comptes, `/etc/elevate.ini`), de la direction « Givre calme »
([`DESIGN-PROPOSAL.md`](DESIGN-PROPOSAL.md) §4), du mode Poche ([`MODE-POCHE.md`](MODE-POCHE.md)) et de l'étude du mode
repos ([`DESIGN-ETUDES-ONYX.md`](DESIGN-ETUDES-ONYX.md), étude 3).

Maquettes HTML statiques, en Givre et en Onyx Noir, au bureau et en poche :
[`maquettes/connexion/index.html`](maquettes/connexion/index.html) (vue d'ensemble),
[`a-fenetree.html`](maquettes/connexion/a-fenetree.html) (12 écrans),
[`b-plein-ecran.html`](maquettes/connexion/b-plein-ecran.html) (13 écrans). Ouvrir `index.html` dans un navigateur ;
le bouton en haut à droite change le thème, un clic sur une maquette la montre à sa taille réelle.

## 0. Résumé

* **Un seul écran pour trois usages** : ouvrir une session, déverrouiller, changer d'utilisateur. Le même bloc
  (image, nom, champ, messages) sert aussi à la confirmation d'élévation d'authd.
* **Rien à demander, rien à montrer** : un seul compte sans mot de passe, c'est la connexion automatique, l'appareil
  démarre comme aujourd'hui (§2.8 de l'étude).
* **Le clavier d'abord** : le dernier compte utilisé est choisi, son champ a le focus. On tape, Entrée, c'est fini.
* **Deux variantes** ont été comparées :
  * **A, fenêtrée, retenue** : une carte de dialogue seule sur le fond à facettes, sans barre ni panel ; l'heure dans
    son en-tête, clavier, accessibilité, réseau et alimentation dans son pied.
  * **B, plein écran** : sans fenêtre ni barre ; l'heure en haut, le compte au centre, les comptes en bas à gauche,
    clavier, accessibilité et alimentation en bas à droite ; le verrou commence par un rideau (l'heure seule).
    Gardée pour mémoire.
* La carte de connexion et la carte d'élévation d'authd sont **le même composant** (§7).

## 1. Ce qui est déjà décidé

| Décision | Source |
|---|---|
| init2 lance **login** (uid 0) avant la session ; écran d'Elegant, liste des comptes de `users.ini` avec leur image, champ du mot de passe | MULTI-UTILISATEUR §2.8 |
| Connexion automatique (`system.ini : autologin = <nom>`), **défaut avec un seul compte** | §2.8 |
| **Verrouiller** = l'écran de login sur la session ouverte ; **Fermer la session** dans le menu d'extinction | §2.8 |
| Changement rapide d'utilisateur (deux sessions) : **plus tard** | §2.8 |
| Mot de passe vide = connexion sans mot de passe ; `system` ne se connecte jamais | §2.1 |
| Image du compte : `picture =` dans `users.ini` | §2.1 |
| Élévation : boîte modale **dessinée par Elegant**, pas par l'app ; liste `/etc/elevate.ini` | §2.6, INSTALLATEUR §5.2 |
| Pas de sécurité contre un attaquant : résistance aux bugs, séparation ordinaire des personnes | décision du 29/10/2026 |
| Chrome sans barre de titre, thèmes Givre et Onyx Noir seulement, jetons `ui_tokens`, jamais de couleur en dur | mémoire `armpi-chrome-notitle`, `AGENTS.md` |
| Opaque, aucune animation, réponse sous 50 ms ; ombres calculées une fois | DESIGN-PROPOSAL §4 |
| Poche : barre 36, contrôles 36, boutons plutôt que gestes, 800 × 600 | MODE-POCHE |
| Repos : le premier toucher est avalé ; verrou au réveil **optionnel, désactivé par défaut** ; écran de reprise = heure, date | DESIGN-ETUDES-ONYX, étude 3 |

## 2. Principes

1. **Ne rien demander quand il n'y a rien à demander.** Un seul compte sans mot de passe : aucun écran. Un seul compte
   avec mot de passe : pas de liste, seulement l'image, le nom, le champ (A4).
2. **Le clavier d'abord, la souris ensuite, le doigt aussi.** Le focus est déjà dans le champ ; la première touche d'un
   écran verrouillé s'écrit dans le champ au lieu d'être perdue. Tout se fait au clavier seul (§3.9).
3. **Un écran, trois usages.** Connexion, verrou et changement d'utilisateur sont le même programme dans trois états ;
   l'utilisateur apprend un seul écran.
4. **Dire ce qui se passe, et le prix avant l'action.** Majuscules verrouillées avant l'erreur ; « ses documents non
   enregistrés seront perdus » avant d'éteindre ou de changer d'utilisateur ; pas de faux sentiment de sécurité (le
   réglage du verrou dit qu'il protège des regards, pas d'une personne décidée).
5. **Les jetons, rien d'autre.** Fond à facettes (palette du fond), `ELEVE` pour la carte, `ACCENT` pour l'unique action
   principale, `DANGER` et `ALERTE` pour les messages. Les seules couleurs propres sont les images des comptes et les
   disques d'initiale, pris dans la table des accents de UIKit (`uk_accents`).
6. **Le thème** (décision de l'utilisateur) :
   * **Chaque utilisateur a son thème**, rangé avec ses autres réglages généraux (langue, disposition du clavier,
     dock, panel…) dans **`/home/<utilisateur>/etc`**. La session le prend à l'ouverture.
   * **L'écran de connexion a son propre thème**, réglé dans le panneau de configuration (Réglages > Comptes,
     « Écran de connexion » : Givre ou Onyx Noir). C'est un réglage de la machine, donc dans `/etc` (par exemple
     `/etc/login.ini`), modifié par élévation. Défaut : le thème choisi pour le premier compte à l'installation.
   * **Au verrou** : proposé, le thème du compte de la session, puisqu'on sait qui est là (question 1).
   * « Contraste : Onyx Noir » (menu d'accessibilité) change le thème de cet écran le temps d'une connexion, sans
     toucher au réglage.
7. **Discrétion au verrou.** Les fenêtres sont cachées (on voit le fond seul), aucune notification n'est lue, seulement
   l'heure. Pas de flou : il coûte et il laisse deviner.
8. **Bureau et Poche, le même écran.** Seules la densité et la taille des cibles changent.

## 3. Ce que l'écran contient (commun à A et B)

### 3.1 Comptes

* Les comptes de `users.ini` qui ont une entrée dans `shadow.ini` (les autres ne peuvent pas se connecter et ne sont
  pas montrés, `system` compris). Ordre : le dernier utilisé d'abord, puis l'ordre alphabétique du nom affiché.
* Chaque ligne : image (32 au bureau dans A, 28 dans la liste de B, 96 au centre de B), **nom affiché** (`name =`, sinon
  l'identifiant), et une seconde ligne en `TEXTE3` dans A : « Administrateur » (le compte peut s'élever), « Compte
  standard », « Sans mot de passe », « Session ouverte ».
* **Sans image** : l'initiale en blanc sur une couleur d'accent de `uk_accents`, choisie par l'uid (donc stable). Le
  compte invité, s'il existe, prend le glyphe « personne » sur graphite.
* Une **pastille `SUCCES`** sur l'image marque une session ouverte.
* Au-delà de 6 comptes (A) ou de 5 (liste de B), la liste défile ; en poche, au-delà de 4 tuiles, la rangée défile.

### 3.2 Mot de passe

* Champ de mot de passe de UIKit (`UTF_PASSWORD`, déjà là), 32 px (40 en poche), focus `ACCENT`.
* Œil à droite du champ : montre le mot de passe tant qu'on le tient appuyé (souris, doigt), ou par Alt+F8.
  Il se cache à nouveau au relâchement.
* Bouton → (`ACCENT`, encre `TJ_ACCENT_INK`) dans le champ : même effet qu'Entrée. Il est grisé tant que le champ
  est vide (sauf compte sans mot de passe, §3.3).
* **Après Entrée** : le champ passe en lecture seule, « Ouverture de la session… » en `TEXTE2` sous le champ. L'écran
  reste jusqu'à ce que le bureau de la session soit dessiné : jamais d'écran noir entre les deux.

### 3.3 Compte sans mot de passe

Pas de champ : un bouton principal « Ouvrir la session → » à la même place et à la même largeur, qui a le focus
(A3, B3). Entrée ou un double clic sur la ligne du compte ouvre la session.

### 3.4 Connexion automatique

* `autologin = <nom>` dans `/etc/system.ini`. **Défaut quand il n'y a qu'un compte et qu'il n'a pas de mot de passe**,
  proposé par l'installateur (§5.2) ; Réglages > Comptes permet de le changer.
* Avec un mot de passe, la connexion automatique reste possible (comme aujourd'hui sur un appareil personnel), mais le
  réglage le dit : « Le mot de passe ne sera demandé qu'au déverrouillage. »
* **Comment voir l'écran de connexion quand elle est active** : « Fermer la session » montre l'écran de connexion
  (sans relancer la connexion automatique), et une touche tenue au démarrage le force. La touche Maj est déjà prise
  par la sortie de secours du mode Poche : proposer **Ctrl** (question 4).

### 3.5 Verrouillage

* Entrées : Super+L ; « Verrouiller » dans le dialogue d'alimentation de la session et la bulle du panel (déjà prévue
  dans DESIGN-ELEGANT §8.5) ; l'inactivité (réglage Énergie) ; la sortie du repos si le verrou au réveil est actif.
* C'est **le même écran**, réduit au compte de la session : son image, son nom, « Verrouillée depuis 12:14 », le champ,
  et « Changer d'utilisateur ».
* Les fenêtres de la session sont cachées, les apps continuent (musique, téléchargements).
* Un compte **sans mot de passe** peut verrouiller : le verrou devient un simple écran de discrétion, déverrouillé par
  Entrée (ou par le code de poche, §3.10).

### 3.6 Changer d'utilisateur

* Depuis le verrou (A9, B9) ou depuis l'écran de connexion d'une session fermée.
* **Tant qu'Elegant ne tient qu'une session**, ouvrir la session d'un autre compte ferme celle qui est ouverte. Le prix
  est dit avant de taper, dans un encadré `CREUX` avec un glyphe `ALERTE` : « Une seule session à la fois pour
  l'instant : ouvrir celle de Camille ferme celle de Stéphane. Ses documents non enregistrés seront perdus. » Le bouton
  « Retour à Stéphane » ramène au verrou.
* Plus tard (changement rapide) : l'encadré disparaît, la pastille verte suffit. Rien d'autre ne change à l'écran.

### 3.7 Éteindre, redémarrer, repos

* Un **menu**, pas un dialogue : l'écran de connexion est déjà un moment de choix. Entrées : Mettre en repos (Pi, et
  PC si le repos y existe), Redémarrer, Éteindre. Pas de « Fermer la session » ici : on y est déjà.
* Si une session est ouverte (verrou), le menu commence par « Stéphane a une session ouverte. Ses documents non
  enregistrés seront perdus. »
* Éteindre depuis cet écran ne demande pas d'élévation : la personne a la machine sous la main (même choix que la
  plupart des systèmes ; à confirmer, question 5).
* En poche (B) : une feuille qui monte du bas, trois grandes cibles, Repos en bouton principal (BP4).

### 3.8 Clavier et langue

* Le code de la disposition est **toujours visible** (« BE », « FR », « US ») : on sait avant de taper.
* Menu : les dispositions de `/res/keyb` sous leur nom lisible (Belge, Français, US ; la variante `BE-COMMA` sous le
  nom que Réglages lui donne), puis la langue de cet écran (Français, English : les fichiers de `/res/lang`).
* Super+Espace passe à la disposition suivante.
* Ce choix ne vaut que pour l'écran de connexion. La session prend ensuite la langue et le clavier du compte
  (`/home/<utilisateur>/etc`). Le défaut de l'écran est celui de la machine (réglé avec son thème dans Réglages), sauf
  au verrou où c'est celui du compte de la session.

### 3.9 Accessibilité, clavier seul

| Touche | Effet |
|---|---|
| (rien) | le champ du compte choisi a le focus |
| Tab / Maj+Tab | A : comptes → champ → pied de la carte (clavier, accessibilité, alimentation). B : champ → comptes → coin droit |
| ↑ ↓ | compte précédent, suivant (le champ suit) |
| une lettre, hors du champ | premier compte dont le nom commence par elle |
| Entrée | ouvrir la session, valider un menu |
| Échap | vide le champ ; au verrou de B, revient au rideau ; ferme un menu |
| Super+Espace | disposition suivante |
| Alt+F8 (tenu) | montre le mot de passe |
| Ctrl+Alt+Suppr | ramène à cet écran, champ vide, quel que soit l'état (repère pour qui ne voit pas bien) |

* L'anneau de focus (2 px `ACCENT` à 45 %, à 1 px) apparaît dès la première touche Tab ; au clic, il disparaît.
* Menu d'accessibilité : **Texte plus grand** (échelle grande de UIKit pour cet écran), **Onyx Noir sur cet écran**
  (contraste), **Clavier à l'écran** (grisé, « plus tard » : Korin n'en a pas).
* Les messages sont du texte, jamais seulement une couleur : l'erreur a son glyphe et sa phrase.

### 3.10 Mode Poche

* Le PocketTerm a son clavier : le mot de passe se tape dessus. Toucher le champ ne fait rien apparaître de plus.
* **Code de verrouillage** (facultatif, par compte) : un code de 4 à 8 chiffres pour le verrou seulement, avec un pavé
  à l'écran (cibles de 64 × 52) ; les chiffres du clavier marchent aussi. L'ouverture d'une session, et toute
  élévation, demandent toujours le mot de passe. C'est le « Code » du réglage Énergie de l'étude 3.
* Densité tactile : lignes de 56 et boutons du pied de 40 (A), ou tuiles de 120 et boutons du coin de 44 (B) ; champs de 40.
* Le premier toucher qui réveille est avalé.

### 3.11 Erreurs et messages

| Cas | Message | Rendu |
|---|---|---|
| Mauvais mot de passe | « Mot de passe incorrect. Réessayez. » | champ vidé, contour `DANGER`, glyphe alerte ; → grisé 1 s |
| Deux échecs, indice défini | « Indice : … » | `TEXTE3`, sous l'erreur |
| Verr. Maj active | « Majuscules verrouillées » | `ALERTE`, glyphe ⇪, dès que le champ a le focus |
| Disposition inattendue | aucun message : le code « BE » est toujours affiché | |
| Compte sans `shadow.ini` (cassé) | « Ce compte ne peut pas se connecter. » | à la place du champ |
| authd ou la session ne démarre pas | « La session n'a pas pu s'ouvrir (startup.txt). Voir /var/log. » + bouton « Réessayer » | `DANGER` ; l'écran reste |
| Code de poche faux | les points se vident, la consigne passe en `DANGER` | |

* **Pas de blocage du compte** après N échecs (le projet ne se défend pas contre un attaquant) : une seconde
  d'attente après chaque échec suffit à empêcher les répétitions de touche.
* Aucune secousse, aucun fondu : le message apparaît d'un coup.

### 3.12 Ce que l'écran n'est pas

* Pas l'accès à distance : telnet, ftp et la console texte gardent leur `login:` Unix (§2.8).
* Pas un bureau : aucun lanceur, aucune app ; pas de Wi-Fi à choisir non plus (le glyphe réseau montre l'état ; un
  menu Wi-Fi pourra venir si les homes sur le réseau arrivent).

## 4. Variante A : fenêtrée (retenue)

Le fond à facettes d'Elegant et **une seule carte**, centrée : niveau 4, rayon 12, `ELEVE`, 400 px de large (320 pour
un seul compte ou le verrou, 460 en poche). **Ni barre de menus ni panel** : ils ne se lancent qu'à l'ouverture de
session (décision du 15/11).

* **En-tête** : titre « Ouvrir une session » (16 SemiBold), nom de la machine en `TEXTE2` dessous ; à droite l'heure
  (16 SemiBold) et la date courte (12, `TEXTE2`).
* **Corps** : filet, puis la liste ; le compte choisi est un bloc `ACCENT_CLAIR` qui contient sa ligne et son champ.
* **Pied** : filet, puis des boutons fantômes de 28 (40 en poche) : à gauche clavier (« BE ») et accessibilité ; à
  droite l'état du réseau (et la batterie en poche), puis l'alimentation. Leurs menus (ceux de UIKit) s'ouvrent sous le
  pied ; si la place manque en bas, la carte remonte le temps du menu (A6 à A8, AP3).
* **Un seul compte, verrou** (A4, A5) : carte de 320, l'heure en grand (28) et la date en tête, puis l'image (64),
  le nom, « Verrouillée depuis 12:14 » avec un cadenas, le champ, « Changer d'utilisateur », et le même pied.
* **Changer d'utilisateur** (A9) : « Retour à Stéphane » prend place dans le pied.

**Forces**

* Rien de nouveau : dialogue, liste, menu sont des composants qui existent. C'est la variante la moins chère.
* **La carte de connexion et la carte d'élévation d'authd sont le même objet** : « Réglages veut modifier le système »
  ressemble exactement à l'ouverture de session, ce qui apprend à l'utilisateur à faire confiance à cette carte-là.
* Lisible d'un coup d'œil : tous les comptes et leur état dans une seule boîte.

* Sans barre ni panel, rien ne fait croire qu'une session est ouverte.

**Faiblesses, et ce qui les compense**

* Sur un grand écran (1920 × 1080), une carte de 400 px paraît petite : à 1280 de haut et plus, l'échelle grande de
  UIKit l'agrandit.
* Pas de « rideau » : l'écran de reprise du repos (étude 3, heure seule) reste un écran à part, ou devient la carte du
  verrou (heure en tête). Proposé : la carte du verrou sert aussi de reprise, pour n'avoir qu'un écran.
* En poche, une carte dans un écran de 800 × 600 est un compromis (l'utilisateur a refusé les cartes centrées pour
  les apps du mode Poche, MODE-POCHE décision 7).

Maquettes : A1 choisir son compte, A2 erreur et majuscules, A3 sans mot de passe, A4 un seul compte, A5 verrou,
A6 alimentation, A7 clavier et langue, A8 accessibilité, A9 changer d'utilisateur ; AP1 à AP3 en poche.

## 5. Variante B : plein écran, sans fenêtre (non retenue, pour mémoire)

Le fond à facettes occupe tout l'écran, **sans barre ni carte**. Trois zones fixes :

* **En haut, au centre** : l'heure (Inter Regular 64, la seule graisse fine que Korin livre ; 48 en poche) et la date (16, `TEXTE2`). En haut à gauche, le nom
  de la machine en `TEXTE3` ; en haut à droite, réseau et batterie.
* **Au centre** : l'image du compte (96), son nom (20 SemiBold), le champ de 300 px (ou le bouton principal).
  Les messages se centrent dessous.
* **En bas à gauche** : la liste des comptes (lignes de 44, image 28), le compte choisi sur une pastille `SURFACE`.
  Au verrou, elle devient « Changer d'utilisateur ».
* **En bas à droite** : clavier (« BE »), accessibilité, alimentation, boutons fantômes de 36 (44 en poche) ; leurs
  menus s'ouvrent vers le haut.

**Le rideau** (B4, BP1) : au verrou et au retour du repos, l'heure seule (112 ; 96 en poche), la date, et « Appuyez sur
une touche ou cliquez pour déverrouiller ». Une touche lève le rideau et s'écrit dans le champ ; un clic ou un toucher
est avalé. Échap ou 30 s sans rien taper ramène au rideau. C'est l'écran de reprise de l'étude 3 : **un seul écran pour
le verrou et le repos**.

**Changer d'utilisateur** (B9) et la poche (BP2) : les comptes passent en **tuiles** de 120 au centre (image 72),
le choisi sur une carte `SURFACE`.

**Forces**

* Ne prétend pas être un bureau : on voit qu'aucune session n'est ouverte.
* Tient la place à toutes les tailles, de 800 × 600 à 1920 × 1080 : les zones sont ancrées aux coins et au centre.
* Rideau, verrou et repos ne font qu'un. En poche, tout est naturel : tuiles, pavé du code, feuille d'alimentation.
* Le plus proche de ce que les gens connaissent (Windows, macOS, GNOME) : rien à apprendre.

**Faiblesses**

* Des placements propres à cet écran (rideau, coins, tuiles) : un peu plus de travail que A.
* Le bloc central ne ressemble pas, tel quel, à la carte d'élévation. Il faut le vouloir (§7).
* Le texte posé sur le fond dépend du contraste du fond ; avec la palette de Givre et de Noir, `TEXTE1` et `TEXTE2`
  passent largement, mais un fond d'écran personnel (photo) demandera un voile sous les zones de texte, plus tard.

Maquettes : B1 choisir son compte, B2 erreur et majuscules, B3 sans mot de passe, B4 rideau, B5 verrou, B6 alimentation,
B7 clavier et langue, B8 accessibilité, B9 changer d'utilisateur ; BP1 à BP4 en poche.

## 6. Comparaison

| Critère | A · fenêtrée | B · plein écran |
|---|---|---|
| Clarté (« suis-je connecté ? ») | bonne, sans barre ni panel | bonne |
| Clavier seul | bon | bon |
| Un seul compte | très bon (A4) | très bon (B1 sans liste) |
| Plusieurs comptes | très bon : tout dans la carte | bon : liste en coin, ou tuiles |
| Verrou et repos | deux écrans | un seul (le rideau) |
| Changement d'utilisateur | bon | bon, et prêt pour le changement rapide |
| Poche (800 × 600, tactile) | correct | naturel |
| Grand écran | carte perdue | tient la place |
| Cohérence avec l'élévation d'authd | identique d'office | à obtenir en partageant le bloc |
| Travail | le plus petit | un peu plus |

## 7. Décision : la variante A, sans barre de menus

Le designer recommandait B (rideau commun au verrou et au repos, poche sans carte). **L'utilisateur a choisi A**, la
carte, le 15/11/2026, en retirant la barre de menus. Ce qui en découle :

1. **La carte est un composant**, dessiné par le même code que la carte d'élévation d'authd : en-tête, bloc du compte
   (image, nom, champ, messages, bouton principal), pied. La carte d'élévation est la même carte avec son titre
   (« Réglages veut modifier le système ») et sans pied. L'utilisateur reconnaît le même objet aux deux endroits.
2. **Les menus sont ceux de UIKit**, sans variante propre à cet écran, ancrés sous le pied.
3. **Le thème de la carte** est celui de l'écran de connexion (Réglages), et au verrou celui du compte (§2).
4. Passer plus tard à B ne changerait ni le programme login, ni authd, ni les fichiers de comptes.

## 8. Pistes pour la mise en œuvre (non décidées)

* Un programme `/sys/login` (uid 0), lancé par init2 avant la session, client d'Elegant dans une **couche au-dessus de
  tout** (ni cadre ni glyphes, comme le lanceur). Au verrou, Elegant cache les fenêtres et montre cette couche ;
  Super+L et Ctrl+Alt+Suppr sont pris par Elegant, jamais par l'app au premier plan.
* login demande la vérification à authd (empreinte de `shadow.ini`) ; il ne lit jamais `shadow.ini` lui-même dans le
  cas du verrou (uid de la session).
* L'état de Verr. Maj : à vérifier dans hidmanager (il doit le publier, par exemple sur le bus) ; aujourd'hui rien ne
  semble le lire.
* Images des comptes : `picture =` (PNG), réduite une fois aux tailles 28, 32, 48, 72 et 96, coupée en disque par le
  masque des coins de `ui_rbox`.
* Le fond : le même dessin à facettes qu'Elegant, calculé une fois (aucune animation, rien à recomposer).
* Les textes passent par `ui_tr` (`res/lang/fr.txt`, `en.txt`), pour que « Langue de cet écran » marche.

## 9. Questions pour l'utilisateur

~~Variante~~ : réglé le 15/11, A sans barre de menus ; thème par utilisateur (`/home/<utilisateur>/etc`), thème de
l'écran de connexion dans Réglages.

1. **Thème au verrou** : celui du compte de la session (proposé), ou celui de l'écran de connexion ?
2. **Verrou au réveil** : désactivé par défaut (étude 3), ou activé dès qu'il y a plusieurs comptes ou un mot de passe ?
3. **Code de poche** : accepter un code chiffré pour le seul verrou du PocketTerm, ou toujours le mot de passe ?
4. **Forcer l'écran de connexion au démarrage** quand la connexion automatique est active : touche Ctrl tenue
   (proposé, Maj étant prise par la sortie de secours du mode Poche), ou seulement « Fermer la session » ?
5. **Éteindre depuis l'écran de connexion** : permis à tous sans mot de passe (proposé), ou réservé à la liste
   d'`elevate.ini` quand une autre session est ouverte ?
