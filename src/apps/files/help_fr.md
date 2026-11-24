# Fichiers

**Fichiers** est le navigateur de fichiers de Korin OS. À gauche, les volumes branchés (la carte SD, une clé USB...) et les accès rapides ; à droite, le contenu du dossier courant. On y parcourt la carte, on ouvre un document ou une application, on crée, copie, déplace, renomme et supprime des fichiers et des dossiers, et on démonte une clé USB avant de la retirer. Fichiers s'ouvre par le menu Korin OS, catégorie **Système**.

![La fenêtre de Fichiers](help/main.png)

## La fenêtre

- **Le panneau de gauche** montre d'abord les **Accès rapides** (Accueil, Musiques, Images, Téléchargements, Projets, Applications, Documents, Vidéos...), chacun avec son icône, puis les **Volumes** : par défaut seulement les clés USB et les volumes du réseau (partages, Google Drive) ; **Affichage > Tous les volumes** montre aussi les autres (**/**, **/tmp**…). Un partage s'appelle « partage sur serveur », un Google Drive « Google Drive ». Chaque volume affiche son nom, sa place libre et sa taille (« 12,3 Go libres sur 29,7 Go ») avec une jauge de la place prise (rouge au-delà de 90 %) ; un volume dont la place n'est pas connue (certains partages) affiche son genre : **Carte SD**, **Clé USB**, **RAM**, **Serveur**… Une clé USB a un bouton d'éjection à droite de sa ligne. La liste des volumes se met à jour toute seule quand on branche ou retire une clé.
- **L'en-tête**, en haut à droite : les boutons ← (précédent), → (suivant) et ↑ (dossier parent), le **fil d'Ariane** (le chemin du dossier courant, un segment par dossier), et le choix de la vue : icônes ou liste.
- **Le contenu** du dossier : les dossiers d'abord, puis les fichiers, par ordre alphabétique. Une application (un dossier `.app`) apparaît avec son icône et son nom.
- **La ligne d'état**, en bas : le nombre d'éléments, ou le nom et la taille de l'élément choisi, ou le résultat de la dernière action (« Supprimé », « Volume démonté »...) ; à droite, la place libre du volume courant et sa jauge.

## Se déplacer

- **Entrer dans un dossier** : double-clic dessus, ou le choisir et appuyer sur Entrée.
- **Remonter d'un dossier** : le bouton ↑, la touche Retour arrière, ou **Aller > Dossier parent**. Le dossier qu'on vient de quitter reste choisi.
- **Revenir en arrière, puis en avant** : les boutons ← et →. Ils sont grisés quand il n'y a rien avant ou après.
- **Sauter à un dossier du chemin** : un clic sur un segment du fil d'Ariane. Un chemin trop long est coupé à gauche ; le « … » mène au dossier qu'il remplace.
- **Aller à un accès rapide ou à un volume** : un clic dans le panneau de gauche, ou le menu **Aller**, qui liste les accès rapides et **/**.
- **Retrouver un fichier ouvert récemment** : l'accès rapide **Récents** liste les derniers fichiers ouverts (par Fichiers, par « Ouvrir avec… » ou par le dialogue Ouvrir ou Enregistrer d'une application), le plus récent d'abord, 50 au plus. Clic droit : **Ouvrir l'emplacement** (son dossier, lui choisi) ou **Retirer des récents** (aussi la touche Suppr) : le fichier lui-même reste. La liste est `HOME:/etc/recent-files.txt`.
- **Taper un chemin** : Ctrl+L (**Aller > Aller à l'emplacement…**), un clic dans le fil d'Ariane à côté des noms, ou la touche **/** ou **~** dans la vue : le chemin devient un champ. On y tape `/apps`, `~/documents` (`~` : le dossier personnel), `HOME:/music` ou un nom relatif au dossier courant (`..` remonte). **Tab** complète le dernier nom, **Entrée** y va (un fichier : son dossier s'ouvre, lui choisi), **Échap** ou un clic ailleurs referme le champ.
- **Aller à un élément en tapant son nom** : dans la vue, tapez le début du nom, l'élément qui commence ainsi est choisi (la ligne d'état rappelle ce qui est tapé). Les lettres tapées à moins d'une seconde d'intervalle s'ajoutent ; la même lettre répétée passe à l'élément suivant qui commence par elle.

## Ouvrir un fichier

Double-clic, ou Entrée (menu **Fichier > Ouvrir**). Ce qui se passe dépend de l'élément :

| Élément | Ce qui s'ouvre |
|---|---|
| Un dossier | on y entre |
| Une application (`nom.app`) | l'application démarre |
| Un programme (fichier exécutable) | il est lancé ; la ligne d'état dit « Programme lancé » |
| Un autre fichier | l'application qui déclare son extension, sinon l'Éditeur |

Une application déclare les extensions qu'elle sait ouvrir dans son manifeste (`app.txt`, ligne `opens`). Un fichier `.txt`, comme tout fichier qu'aucune application ne réclame, s'ouvre dans l'Éditeur.

## Créer un dossier

1. Allez dans le dossier qui doit le contenir.
2. **Fichier > Nouveau dossier** (Ctrl+N).

Le dossier s'appelle « Nouveau dossier », ou « Nouveau dossier 2 », « Nouveau dossier 3 »... si le nom est déjà pris.

## Renommer

1. Choisissez le fichier ou le dossier.
2. **Fichier > Renommer…**, la touche **F2**, ou **Renommer…** dans le menu du clic droit.
3. Un dialogue demande le nouveau nom, l'ancien déjà écrit et sélectionné : tapez-le, puis **OK** (ou Entrée).

L'élément renommé reste choisi. Un nom ne peut pas être vide ni contenir `/ \ : * ? " < > |` (les cartes et les clés, en FAT32 ou exFAT, les refusent), et deux éléments d'un même dossier ne peuvent pas porter le même nom, aux majuscules près : dans ces cas, un message le dit et le dialogue revient. On peut changer seulement les majuscules d'un nom (« photo.jpg » en « Photo.jpg »).

## Supprimer

1. Choisissez le fichier ou le dossier.
2. **Fichier > Supprimer**, ou la touche Suppr.
3. Un petit menu demande confirmation : **Supprimer « nom »** (pour un dossier : **Supprimer le dossier « nom » et son contenu**) ou **Annuler**.

La suppression est définitive : il n'y a pas de corbeille dans Fichiers (sur Google Drive, ce qui est supprimé va à la corbeille du Drive). Un dossier est supprimé avec tout ce qu'il contient, en arrière-plan, comme une copie : la ligne d'état devient une bande de progression (« fichier 3 sur 120 ») avec un bouton **Annuler**, qui arrête la suppression (ce qui est déjà supprimé l'est). Si un fichier ne peut pas être supprimé, un message dit lequel et pourquoi.

## Copier et déplacer

1. Choisissez le fichier ou le dossier ; plusieurs avec **Ctrl+clic** (ajoute ou retire un élément) ou **Maj+clic** (une plage, depuis le dernier choisi), comme sous Windows ; **Ctrl+A** les choisit tous. Copier, couper, glisser et supprimer agissent sur toute la sélection.
2. **Édition > Copier** (Ctrl+C) pour le copier, ou **Édition > Couper** (Ctrl+X) pour le déplacer ; aussi dans le menu du clic droit.
3. Allez dans le dossier de destination, sur le même volume ou un autre (une clé, un partage réseau, Google Drive…).
4. **Édition > Coller** (Ctrl+V), ou **Coller** dans le menu d'un clic droit dans le vide.

On peut aussi **faire glisser** un élément hors de la fenêtre, et **déposer** des fichiers sur Fichiers (depuis une autre fenêtre de Fichiers, par exemple) : ils vont dans le dossier sur lequel on les lâche, sinon dans le dossier courant. Déposer **copie** ; avec **Maj** tenue au moment de lâcher, les éléments sont **déplacés**.

Déplacer sur un même volume est immédiat. Sinon, Fichiers copie, puis (pour **Couper**) efface l'original. Coller dans le dossier d'origine fait une copie nommée « nom (copie) ». Si un élément du même nom existe déjà, un message demande s'il faut le **remplacer** (un dossier : y mettre ce qu'il contient), le laisser (**Non**) ou tout arrêter (**Annuler**).

La copie se fait **en arrière-plan** : on continue de travailler dans Fichiers (changer de dossier, ouvrir, renommer, coller encore) pendant qu'elle avance. Une copie qui dure plus d'un instant remplace la ligne d'état par une **bande de progression** : l'élément en cours (et « fichier 3 sur 120 » pour un dossier), une barre, les octets copiés sur le total, le débit et le temps restant. Ce qu'on colle pendant une copie se met **en file** et passe ensuite (« 2 en attente »). Même vers ou depuis un partage réseau ou Google Drive, où une lecture ou une écriture peut prendre du temps, la fenêtre reste réactive. Le bouton **Annuler** de la bande arrête la copie à la fin du morceau en cours (1 Mo) et retire celles en attente : le fichier à moitié copié est supprimé, les éléments déjà copiés restent, et pour **Couper** les originaux ne sont pas effacés. Vers Google Drive, un fichier est envoyé à Google quand il est fermé (« Finalisation de… ») : cette étape ne peut pas s'interrompre, l'annulation attend sa fin. Fermer Fichiers pendant une copie demande s'il faut l'arrêter. Un dossier lent à lire (Google Drive pendant une copie, un serveur loin) s'affiche quand il arrive (« Chargement… » dans la ligne d'état) : en attendant, la fenêtre reste utilisable.

Si une erreur arrête la copie (plus de place, volume en lecture seule, fichier de plus de 4 Go vers une clé en FAT32…), un message dit quel fichier et pourquoi.

## Deux vues côte à côte

Le bouton à deux colonnes de l'en-tête, juste à gauche du choix icônes / liste (ou **Affichage > Deux vues**), partage la fenêtre en **deux vues indépendantes**, chacune dans son dossier, avec sa sélection et sa présentation (icônes ou liste). Chaque vue a un bandeau : le nom du dossier, son chemin et la place libre de son volume. La **vue active** a son bandeau aux couleurs de l'accent ; un clic n'importe où dans une vue l'active. L'en-tête (flèches, fil d'Ariane), le panneau de gauche et la ligne d'état suivent la vue active ; la sélection de l'autre reste, grisée.

- **Édition > Copier vers l'autre vue** et **Déplacer vers l'autre vue** (aussi dans le menu du clic droit, avec le nom du dossier de destination) : directement, sans question, sauf si un élément du même nom serait remplacé.
- **Glisser** d'une vue à l'autre copie ; avec **Maj**, déplace. La vue où l'on dépose devient l'active.
- La séparation entre les deux vues se déplace à la souris.
- Le même bouton revient à **une seule vue** (la vue active reste).

Les deux vues, leurs dossiers et la place de la séparation sont retenus pour la prochaine fois.

## Choisir la vue

- **Icônes** : une grille de grandes icônes, le nom sous chacune.
- **Liste** : une ligne par élément, avec les colonnes **Nom**, **Taille** et **Type** (Dossier, Application, Fichier).

Le choix se fait dans le menu **Affichage** ou par le petit contrôle à deux segments de l'en-tête. En vue liste :

- un clic sur le titre d'une colonne trie par cette colonne ; un second clic inverse l'ordre (une flèche ▲ ou ▼ le montre) ;
- on élargit une colonne en faisant glisser le séparateur entre deux titres.

La vue et la largeur des colonnes sont retenues pour la prochaine fois.

## Retirer une clé USB

Avant de débrancher une clé, démontez-la pour que tout soit bien écrit :

- un clic sur son bouton d'éjection, dans le panneau de gauche ;
- ou, quand on est dans un dossier de la clé, **Volume > Démonter le volume courant**.

La ligne d'état dit « Volume démonté ». Si une clé est retirée sans être démontée alors qu'on y était, Fichiers revient à **/** (« Volume retiré : retour à / »).

## Se connecter à un serveur (partage réseau)

**Volume > Se connecter à un serveur…** (Ctrl+K) ouvre un dossier partagé par un PC Windows, un Mac, un NAS ou un autre Korin (SMB) :

- **Adresse** : `smb://192.168.0.9/Partage` ou `\192.168.0.9\Partage` (on peut ajouter un dossier au bout) ; la liste propose les serveurs déjà connus ;
- **Utilisateur** et **Mot de passe** : un compte du serveur (`nom`, ou `DOMAINE
om`). Laissés vides, ceux déjà enregistrés pour ce partage servent.

Le partage devient un volume **/net/net0** (genre « Serveur ») et s'ouvre. Il est **remonté à chaque démarrage**, jusqu'à ce qu'on le démonte (bouton d'éjection, ou **Volume > Démonter le volume courant**) ; le compte, lui, reste enregistré (dans `/etc/smbfs.ini`) pour la prochaine fois. Pour partager des dossiers de Korin vers Windows : Réglages, volet **Partages**.

## Se connecter à Google Drive

**Volume > Se connecter à Google Drive…** monte votre Google Drive comme un volume (sur le PC) :

- **Nom du Drive** : un nom court à vous (`perso`, `travail`…), pour avoir plusieurs comptes Google ;
- **Se connecter** : le navigateur s'ouvre sur la page de Google ; connectez-vous et autorisez Korin. La fenêtre d'attente se ferme toute seule.

Le Drive devient un volume **/net/net0** (genre « Google Drive ») et s'ouvre, **lu et écrit** : on y copie, crée, renomme, déplace et supprime comme ailleurs. Un fichier est lu en flux (on n'attend pas tout le fichier) ; un fichier écrit est envoyé à Google quand le programme le ferme. Supprimer met à la **corbeille** de Google Drive (30 jours). Les Google Docs, Sheets et Slides n'apparaissent pas. Le Drive est remonté à chaque démarrage jusqu'à ce qu'on le démonte ; aucun mot de passe n'est gardé, seulement un jeton chiffré (dans `HOME:/oauth`).

## Menus et raccourcis

| Menu | Commande | Raccourci |
|---|---|---|
| Fichier | Ouvrir | Entrée |
| Fichier | Nouveau dossier | Ctrl+N |
| Fichier | Renommer… | F2 |
| Fichier | Supprimer | Suppr |
| Fichier | Actualiser | F5 ou Ctrl+R |
| Fichier | Fermer | Ctrl+W |
| Édition | Couper | Ctrl+X |
| Édition | Copier | Ctrl+C |
| Édition | Coller | Ctrl+V |
| Édition | Copier vers l'autre vue, Déplacer vers l'autre vue | |
| Aller | Dossier parent | Retour arrière |
| Aller | Aller à l'emplacement… | Ctrl+L |
| Aller | les accès rapides, / | |
| Affichage | Icônes, Liste, Deux vues, Tous les volumes | |
| Affichage | Fichiers cachés (un nom qui commence par « . », ou l'attribut caché de FAT) | Ctrl+H |
| Volume | Se connecter à un serveur… | Ctrl+K |
| Volume | Se connecter à Google Drive… | |
| Volume | Démonter le volume courant | |

Au clavier, dans le contenu :

| Touche | Effet |
|---|---|
| Flèches haut et bas | élément précédent ou suivant (une rangée en vue icônes) |
| Flèches gauche et droite | élément précédent ou suivant, en vue icônes |
| Début, Fin | premier ou dernier élément |
| Entrée | ouvrir |
| Retour arrière | dossier parent |
| F2 | renommer |
| Suppr | supprimer, après confirmation |

## Les accès rapides

La liste des accès rapides vient du fichier `/etc/places.ini`, une ligne par accès, dans l'ordre :

```sh
Accueil = HOME:/
Musiques = HOME:/music
Applications = /apps
```

À gauche le nom affiché, à droite le dossier. Un dossier qui n'existe pas n'est pas montré. L'icône vient du dossier : une maison pour **HOME:/** (le dossier personnel), une note pour `music`, une image pour `pictures`, une flèche pour `downloads`, une équerre et un crayon pour `projects`, une page pour `documents`, un film pour `video`, une grille pour **/apps**, un dossier pour les autres. Fichiers crée au démarrage ceux de ces dossiers personnels qui manquent. Dans le dossier personnel, ils portent aussi leur icône sur le dossier, dans la vue en icônes ; les dossiers prennent la couleur d'accent du thème. On peut modifier ce fichier dans l'Éditeur ; **Fichier > Actualiser** relit la liste.

## Astuces

- **Actualiser** (F5 ou Ctrl+R) relit le dossier et les volumes, sans perdre l'élément choisi : utile après avoir copié un fichier depuis le [Terminal](help:terminal).
- Pour copier beaucoup de fichiers choisis par un motif (`*.jpg`), la commande `cp` du [Terminal](help:terminal) reste pratique ; Fichiers copie un élément à la fois (un dossier entier compte pour un).
- Les applications installées sont dans **/apps** (accès rapide **Applications**) ; [Logiciels](help:pkgman) les installe et les retire.
