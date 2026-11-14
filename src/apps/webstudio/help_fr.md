# WebStudio

WebStudio écrit des sites web sur Korin. Un site est un **dossier** : pas de projet à créer, tout dossier s'ouvre.

## Commencer

* **Fichier › Nouveau site** (Ctrl+N) : un nom et un modèle (vide, page simple, site vitrine) ; le site est créé dans
  `SD1:/home/sites/<nom>`, avec un dépôt git si vous le voulez.
* **Modèles** : avec le paquet **Modèles de sites** (webstudio-templates), Nouveau site propose huit sites prêts à
  modifier, chacun avec ses polices, ses photos et ses textures : Carnet de voyage, Ardoise (restaurant, café),
  Chambre noire (photographe), Aquarelle (mariage, fête), Gazette (journal, blog), Atelier (studio de création), CV et
  Documentation. Le nom du site remplace `{{SITE}}` dans les pages ; un modèle à vous : un dossier dans `SD0:/apps/webstudio.app/templates/` avec son
  `template.ini` (`name`, `description`, `order`).
* **Fichier › Ouvrir un dossier** (Ctrl+O) : n'importe quel dossier.
* L'accueil montre les sites récents et ceux du serveur web de ce PC (double-clic : l'ouvrir).

## Écrire

* À gauche, l'arbre des fichiers : **double-clic** sur un dossier le déplie ou le replie, sur un fichier l'ouvre dans
  un onglet (ou montre son onglet).
* **Ctrl+S** enregistre ; un point sur l'onglet : pas encore enregistré. **Ctrl+F** cherche dans le fichier,
  **Ctrl+Maj+F** dans tout le site (résultats en bas, double-clic : y aller).
* Fichier › Nouveau fichier, Nouveau dossier, Renommer, Supprimer agissent sur ce qui est choisi dans l'arbre.

## Voir

* **F5** (Voir dans Jet) : le serveur de Korin sert le site (`http://localhost:8080`, ou le port libre suivant) et Jet
  ouvre la page ; ensuite, **chaque enregistrement recharge la page** dans Jet.
* En bas, le **journal du serveur** : chaque requête, son code. Double-clic sur un **404** : créer le fichier qui manque.
* Le serveur web doit être installé (paquet **Serveur web**, webd).

## Partager

* **Site › Publier** : ajoute le site au serveur web de ce PC (Réglages › Serveur web), avec sa liaison
  (`http://*:80`, `https://nom.exemple`).
* **Site › Git** : crée le dépôt, ou montre les changements et les valide.
* **Site › Ouvrir un terminal ici** : un Terminal dans le dossier du site.
