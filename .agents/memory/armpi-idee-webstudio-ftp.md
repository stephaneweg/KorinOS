---
name: armpi-idee-webstudio-ftp
description: "Idée pour plus tard (14/11) : WebStudio ouvre un site depuis un serveur FTP, le synchronise avec un dossier local, bouton Publier à côté d'Exécuter, bouton Synchroniser dans la liste des fichiers"
metadata:
  type: project
  modified: 2026-11-14
---

Demande de l'utilisateur du 14/11/2026, pour améliorer WebStudio (pas encore un chantier lancé) :

- **Ouvrir un site depuis un serveur FTP** : le dossier distant (par exemple `/www`) est synchronisé avec un dossier
  local, et c'est la copie locale qu'on édite.
- **Bouton « Publier »** à côté du bouton Exécuter : il envoie les changements locaux vers le serveur. Si les
  paramètres FTP du site ne sont pas définis, un dialogue les demande, puis ils sont **conservés** avec le site.
- **Bouton « Synchroniser »** dans la liste des fichiers, seulement quand le site est lié à un serveur distant : il
  récupère les nouveaux fichiers distants (et ceux qui ont changé).

Points à trancher au moment du chantier : où ranger les paramètres (hôte, port, utilisateur, dossier distant) ; le mot
de passe hors du dossier du site ; FTPS et SFTP en plus de FTP ; détection des changements (MLSD : taille et date) et
conflits quand un fichier a changé des deux côtés.

Lien : DESIGN-WEBSTUDIO.md prévoit déjà « Publier vers une autre machine (FTP, SFTP, autre Korin) » pour plus tard.
Voir [[armpi-idee-web]], [[armpi-idee-ftpfs]].
