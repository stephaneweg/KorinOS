# Cardfile sur ARMPI

La base de fiches d'Onyx (`user/Apps/cardfile`) : un fichier `.card`, un formulaire et ses fiches, à la manière d'Access
sans SQL. Vues Formulaire (F5), Liste (F6), Conception (F7). C++, relais `-luikit`.

    wsl sh tools/office/build.sh cardfile        # chaîne croisée -> build/office/sdroot/apps/cardfile.app
    pibuild app /src/apps/cardfile cardfile      # sous ARMPI, GCC natif

`SD0:/apps/cardfile.app/main [fiche.card]`. Sans argument : la fiche gardée à la dernière fermeture (question posée), sinon
la dernière ouverte (`SD0:/home/cardfile/last.txt`), sinon un formulaire neuf en Conception. Exemples : `books.card`,
`contacts.card` à côté de `main` (`data/` du dossier source). Enregistrement par défaut : `SD0:/home/documents`.

## Repris d'Onyx

* `model.h` : tel quel (formulaire, fiches, types, valeurs, fichier `.card`, CSV, recherche, tri), sauf l'horloge (`time`,
  `localtime`). Il reste **seul** (`<time.h>` et rien d'autre) et en **Latin-1** comme le fichier : Letters le reprendra.
* La logique de `main.cpp` (annuler = le document entier avant chaque changement, ordre, fiche neuve abandonnée, fichiers,
  récupération) et des trois vues : mêmes règles, mêmes textes (anglais).

Un `.card` d'Onyx s'ouvre et se réécrit à l'octet près (`books.card`, `contacts.card` vérifiés) ; l'en-tête `# Onyx Cardfile`
reste : Onyx relit ce qu'ARMPI enregistre.

## Écrit pour ARMPI

**Aucun pont, aucune classe de contrôle** : `widgets.h` n'est pas repris. Un éditeur est une poignée UIKit.

* `app.h` : l'état partagé, les jetons, et le **seul passage Latin-1 / UTF-8** : `U8 ()` vers les `ui_*`, `L1 ()` depuis eux. Un
  caractère que Latin-1 n'a pas (€, ...) devient `?` à la frappe : le fichier ne saurait pas le garder.
* `formview.h` : une `ui_view` peint la fiche (bandeau, noms des champs), les éditeurs sont ses enfants : `ui_textbox` (nombres :
  `UTF_RIGHT`), `ui_textarea`, `ui_checkbox`, `ui_dropdown`, et deux assemblages : date = champ + bouton (`ui_date_popup`, car
  `ui_datepicker` ne sait ni être vide ni se taper) ; couleur = `ui_colorpicker` + champ (vide : aucune ; code ou nom). Une fiche
  plus haute que la vue défile : les enfants bougent, la vue les coupe.
* `listview.h` : `ui_list` virtuelle à colonnes, `UL_HSCROLL`, `ui_list_cell` pour la pastille et la coche. Tri : UIKit dit la
  colonne (`UN_SORT`), le cycle croissant / décroissant / ordre du fichier est celui d'Onyx.
* `designview.h` : `ui_panel`, `ui_groupbox`, `ui_list` virtuelle, `ui_numeric`, `ui_textarea`.

## Stubs et différences avec Onyx

* **Publipostage** : le menu demande la lettre (.rtf, .docx, .odt), tous les enregistrements montrés ou celui-ci, écrit `SD0:/home/cardfile/merge.card` et `merge.job`, et lance `letters --merge`. La sortie en fichiers reste au dialogue de Letters. La ligne `merge =` d'un `.card`
  est gardée à l'enregistrement.
* **Fermeture** : comme Onyx, pas de question : la fiche modifiée va dans `recovered.card`, proposée au lancement suivant.
* **Dossiers** : `SD0:/home/cardfile` (récupération, dernier fichier) au lieu du dossier de l'app.
* **Texte multiligne** : sans coupure aux mots (`ui_textarea`), barre verticale seulement.
* **Saisie** : pas de filtre des caractères à la frappe (Onyx : `accept`) ; le contrôle se fait quand on quitte la fiche, comme
  pour les valeurs invalides d'Onyx. Colonne d'un champ : refusée en rouge, pas filtrée.
* **Liste** : première lettre sur la 1re colonne (UIKit), pas la colonne triée ; menu contextuel sans raccourcis affichés ;
  colonnes centrées absentes (coche au milieu seulement).
* **Fichiers** : un seul filtre par dialogue, sans liste de genres. **CSV** : l'export reste en Latin-1 comme Onyx ; l'import prend
  aussi l'UTF-8 (reconnu tout seul, ainsi qu'un `.card` en UTF-8).
* **Écran** : Cardfile s'ouvre au-dessus du dock (qui flotte sans réserver le bas), soit 442 px de contenu en 800x600 : la fiche
  défile, Tab la suit. Pas de mode PocketUI ni de barre réduite sous 700 px.
* Ctrl+Z dans une zone multiligne : le journal du widget d'abord ; ailleurs, le document.
