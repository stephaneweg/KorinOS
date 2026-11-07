# Budget mensuel

Le port, en C++ sur UIKit, de l'app Windows de l'utilisateur (`C:\Projects\gestion\BudgetApp`, C# WinForms), elle-même
tirée de son classeur Excel `Prévisions_Budget.xlsx`. Mêmes données, mêmes calculs, même fichier : un `budget.json`
passe de Windows à Korin et retour sans conversion.

| | |
|---|---|
| Sources | `src/apps/budget` : `model.h` (données, calculs, saisie, JSON), `grid.h` (la grille éditable), `main.cpp` (la fenêtre) |
| Sur la carte | `SD0:/apps/budget.app` ; tiroir **Bureautique** |
| Données | `SD0:/home/budget/budget.json`, copies du jour dans `SD0:/home/budget/sauvegardes` (les 60 dernières), largeurs des colonnes dans `budget.ini` |
| Construire | `sh tools/office/build.sh budget` (WSL), puis `build.ps1` (`pibuild app /src/apps/budget budget` sous Korin : pas essayé) |
| Essai des calculs sur le PC | `g++ -std=c++17 -I src/apps/budget tools/budget/calctest.cpp -o /tmp/calctest && /tmp/calctest budget.json 2026-11-07` |
| Paquet | `budget` (section `[app.budget]` de `tools/pkg/packages.ini`), version 1.0.0 |

## Ce qui est repris de l'app Windows

* **Le modèle** (`Models.cs`) : jour de début (23), catégories des dépenses courantes, lignes récurrentes (Jour,
  Libellé, Revenu, Dépense, Chèque repas). Mois créés depuis le modèle (début = fin du mois précédent, fin = +1 mois,
  nom = mois du milieu, revenus cochés reçus), modifiables librement ; copier un mois dans le modèle ; supprimer un mois ;
  dates de période ; catégories par mois, renommées par position (les colonnes retirées vont dans la dernière).
* **La case « Utiliser » du modèle** (ajoutée le 7/11 dans les deux apps, à la demande de l'utilisateur) : une ligne
  décochée reste dans le modèle mais n'est pas recopiée dans les nouveaux mois ; les totaux du bas du dialogue ne
  comptent que les lignes cochées et suivent chaque clic (plus « 21 lignes utilisées sur 23 » sous les totaux à Korin,
  au bout de la ligne sous Windows). Les lignes décochées sont grisées. Dans le JSON : `"Skip": true`, écrit seulement
  quand la ligne est décochée (`JsonIgnore WhenWritingDefault`), entre `MealVoucher` et `Planned` ; un fichier sans
  `Skip` garde toutes ses lignes. Les largeurs des colonnes du modèle passent sous `model2=` dans `budget.ini`.
* **Les calculs** (`Calc.cs`) : formule pour formule. Montants en dix-millièmes entiers (le `decimal` de C#), prorata en
  double, arrondi loin de zéro à l'affichage comme `N2`.
* **La saisie** (`GridKit.cs`) : `12,50`, `1 234,56 €`, `22*8`, `=50+30` ; `+75` dans « Déjà payé » s'ajoute, et la
  ligne se coche quand le montant est atteint ; dates `7` (jour dans la période), `7/11`, `07/11/2026`.
* **Le stockage** (`Storage.cs`) : le JSON de System.Text.Json (PascalCase, `WriteIndented`, dates
  `yyyy-MM-ddT00:00:00`, `Planned` et `Remaining` écrits aussi), écriture dans un `.tmp` puis renommage, une copie par
  jour. Enregistrement 0,7 s après la dernière modification et à la fermeture. Un fichier illisible n'est jamais touché.
* **Les écrans** : la barre (mois, Nouveau mois, période, Modèle…), les quatre indicateurs, la grille des charges
  (Réglé, Jour, Libellé, Revenu, Dépense, Déjà payé, Reste à payer, Ch. repas, Solde ; réglées grisées, non réglées en
  gras, soldes négatifs en rouge, paiement partiel en orange), le suivi (soldes, jours couverts, Budget initial /
  Prévision / Réel), les dépenses courantes (complétion des libellés, totaux par catégorie avec les chèques repas dans
  la première, total, moyenne par jour, tri par date), le dialogue du modèle, Enregistrer un paiement.

## Ce qui change pour Korin

* **800 x 600** : l'écran Windows met tout côte à côte. Ici, les indicateurs restent en haut et le reste va dans deux
  onglets en capsule (`ui_segmented`) : **Charges et suivi** (Ctrl+1) et **Dépenses courantes** (Ctrl+2). La fenêtre
  s'ouvre agrandie sur un écran de 1024 de large ou moins ; la barre du haut laisse `FRAME_CAP_W` libre.
* **La grille** (`grid.h`), à la manière du tableur : une `ui_view` avec en-têtes de colonnes et de lignes, ligne
  nouvelle « * », saisie dans la cellule. Clavier : flèches, Début / Fin (Ctrl : coins), Page ; Tab / Maj+Tab ; Entrée ;
  taper remplace, F2 modifie, Retour arrière vide, Échap annule ; Espace coche ; Suppr vide la cellule ; **Inser** insère
  une ligne au-dessus, **Ctrl+Suppr** la retire ; Ctrl+C / Ctrl+V (blocs d'Excel, tabulations) ; Menu ou Maj+F10 : le
  menu de la ligne ; Ctrl+Tab sort de la grille. Souris : clic, double-clic, bouton droit, **bord d'un en-tête glissé :
  la largeur** (gardée dans `budget.ini`), molette.
* **Menus** : Fichier (Dossier des données), Mois (Nouveau Ctrl+N, Supprimer, précédent / suivant Ctrl+PgPréc /
  Ctrl+PgSuiv, Période, Catégories…, Modèle… Ctrl+M, Copier dans le modèle), Lignes (Paiement Ctrl+P, Payé Ctrl+D, Non
  payé Ctrl+U, Insérer, Supprimer, Trier), Présentation (Ctrl+1, Ctrl+2, Soldes Ctrl+B, Retour au tableau Ctrl+G).
  Entrée dans un champ passe au suivant.
* Le menu déroulant des mois ne montre que le nom quand la place manque (la période est dans les deux champs à côté).
* Arguments : `--today AAAA-MM-JJ` et `--month Novembre` (le mois montré au départ), pour les essais.

## Essayé

* **Calculs** : `calctest` sur le `budget.json` réel et sur sa sauvegarde du 7/11, comparé au `Calc.cs` d'origine
  compilé par dotnet : mêmes chiffres. Sur la sauvegarde, novembre 2026 au 07/11/2026 : reste à payer 160,00 €,
  prévision 435,70 €, réel −4,67 €, −0,67 €/jour, écart −440,37 €, dépenses courantes 1 088,10 € et 68,01 €/jour.
  Aller-retour JSON identique ; le fichier écrit sous Korin, relu par `Models.cs` / `Calc.cs`, redonne les mêmes chiffres.
* **Sous QEMU, en 800 x 600** (`kernel8_800.img`) : les chiffres ci-dessus à l'écran ; saisie d'une ligne, `22*8`,
  paiement partiel puis `+76` qui coche la ligne, refus d'un montant invalide (contour rouge), insertion et suppression
  de ligne, menu contextuel, largeur d'une colonne à la souris, complétion « Col » → « Colruyt », date `7`, Ctrl+P,
  Ctrl+B, Ctrl+M (dialogue du modèle), Ctrl+N (Décembre 2026 depuis le modèle), Ctrl+PgPréc / PgSuiv,
  enregistrement automatique et copie du jour.
* **Case « Utiliser »** : `calctest` sur un fichier écrit par `Models.cs` avec une ligne décochée (nouveau mois sans
  elle, aller-retour identique) ; sous QEMU, Espace sur la case de Salaire puis de Maison (totaux mis à jour, lignes
  grisées), Enregistrer le modèle, Ctrl+N : janvier 2027 sans ces deux lignes ; le fichier écrit par Korin relu par
  `Models.cs` donne les deux lignes décochées. Sous Windows : compilé, JSON vérifié, dialogue pas ouvert à l'écran.

## Pas essayé, ou qui reste

* **Rien sur le vrai Pi** (tactile, clavier du PocketTerm, vitesse de la carte).
* Le **pavé numérique** ne donne aucun caractère sous QEMU (à voir sur le Pi : les chiffres de la rangée du haut
  marchent).
* `ui_menu_key` ne lit que lettres, chiffres et F1-F12 : « Ctrl+PgUp » y serait pris pour Ctrl+P. Inser, Ctrl+Suppr,
  Ctrl+PgPréc / PgSuiv sont donc traités par la grille et la boucle, et écrits dans le libellé du menu, pas dans sa
  colonne des raccourcis.
* Pas encore essayés : Ctrl+V d'un bloc venu d'Excel, Catégories…, Copier ce mois dans le modèle, Supprimer ce mois,
  modifier la période, mode poche.
* Pas d'annuler / rétablir (l'app Windows non plus) ; pas de `lang-en.txt` (les textes passent par `TR`).
* Comme sous Windows, le mois montré au départ est le dernier créé, pas celui d'aujourd'hui.
* Pas de sélection de plusieurs lignes : « Marquer comme payé » agit sur la ligne courante.
