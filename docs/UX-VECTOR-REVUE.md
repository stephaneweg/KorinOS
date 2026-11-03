# Vector : revue des captures QEMU contre la spécification

Revue du 02/11/2026, sur `docs/UX-VECTOR.md`. Captures de `build/try/`, thème Givre, 800 × 600, souris.

Mesures relevées au pixel sur les PNG. Sauf mention, les coordonnées sont celles de l'écran : fenêtre = écran − (4, 32).

**Classement.** Bloquant : une tâche centrale est impossible, ou l'app donne une information fausse, ou un principe fondateur est cassé. Important : un critère du § 11 échoue, ou une fonction est difficile à trouver. Mineur : finition.

**Non jugé.** Onyx Noir, tactile, autres tailles d'écran. Pages Style et Stroke du tiroir, Export, Stylo en cours de tracé : aucune capture ne les montre. Les menus réduits ne comptent pas, ni le dock sur les `b*.png`.

**Déjà corrigé, à ne pas rouvrir.** Champs du panneau manquants sur `d2`, champ hexadécimal absent sur `c4` et `d9` : ils sont là sur `e1` et `e2`, plus récentes.

## Bloquant

| # | Capture | Ce que je vois | Attendu | Correctif |
|---|---|---|---|---|
| B1 | `c2`, `b2`, `b3`, `b4`, `e1` | Hors Sélecteur, la barre ne contient qu'une phrase. Aucun contrôle pour Nœuds, Crayon, Texte, Dégradé. Sélecteur : X, Y, L, H seuls. Aucune case `⋯` | § 3.4, barre de chaque outil. § 3.5, `⋯` toujours là. § 6.12, indication dans le canevas | Barre de x = 4 à 572 (fenêtre) : cases 32 × 28, champs 56 à 72 × 28, y = 4, écart 2, 8 entre groupes. Case `⋯` 32 × 28 en fin de partie outil. Ajouter au Sélecteur verrou 28 et unité 44. Ordre : Nœuds, Texte, Dégradé, puis les autres. La phrase passe dans la bande du bas du canevas : 20 px, fond `FACE` à 85 %, `TEXTE1` 12 px |
| B2 | `d3`, `d2`, `d7`, `d9` | Tiroir ouvert, sélection coupée par le rail : bord gauche à x = −3, canevas à x = 44. Coin haut gauche et losange gauche invisibles | § 2.3 : la vue se recale pour montrer la sélection, sans changer le zoom. § 11 : « sélection encore visible » | À l'ouverture, si la boîte plus 16 px de marge sort du canevas de 504 px, translater la vue du minimum. Ici, environ 64 px vers la droite. Boîte plus large que le canevas : la centrer |
| B3 | `f1`, `f2`, `f4` | Règle horizontale fausse de 18 px. `f1` : graduation 0 à x = 263, bord de page à 281 ; 800 à 562, bord droit à 578. `f4` : 0 à 139, bord à 157. La règle verticale est juste. L'écart vaut la largeur de la règle verticale | § 5.5 : origine à la page | Ajouter 18 px à l'origine de la règle horizontale quand la règle verticale est montrée. Contrôle : la grande graduation 0 tombe sur le bord de page, tiroir ouvert ou fermé |

## Important

| # | Capture | Ce que je vois | Attendu | Correctif |
|---|---|---|---|---|
| I1 | `c4`, `d7`, `e2` | Remplissage : ni genre de peinture, ni opacité, ni modèle, ni pipette, ni champs numériques. Bande vide de y = 281 à 311. « None » en lien bleu, style étranger à l'app. Couleur en aplat de 112 px, sans fonction visible | § 4.3, rangs 1 à 5 et bloc uni | D'abord le rang 3 : 8 cases 29 × 28 sous les onglets de page, « aucune » en première, le lien disparaît. Barre d'opacité 232 × 20 dans la bande vide. Ligne e : pipette 32, hexa 96, opacité 64. Retirer l'aplat ou le passer au rang 2, en 28 px |
| I2 | `c2` | Nœuds carrés de 13 px, losange de 14, contour `ACCENT` : mêmes formes que les poignées de sélection. Boîte en tirets gardée autour du chemin. Aucun nœud rond | § 5.1 : carré, rond, losange à 9 px. § 5.2 : fond `SURFACE`, contour `TEXTE1`, 1,5 px | Nœuds à 9 px, contour `TEXTE1`. Choisi : fond `ACCENT`. Rond pour les lisses. Pas de boîte ni de poignées de sélection dans l'outil Nœuds |
| I3 | `e3` | Ctrl+K : les glyphes du cadre sont dans le champ, le `✕` mord sa bordure. Bande vide de 50 px sous la liste. Bouton agrandir inutile | § 11 : les glyphes ne touchent rien. § 6.10 : un champ, une liste | Champ à y = 36, 28 de haut, sous la zone du cadre. Liste de y = 72 à y = 332, soit 10 lignes de 26. Pas de pied. Retirer l'agrandissement du dialogue |
| I4 | `e1`, toutes | Fenêtre de y = 32 à 535, soit 504 px de contenu. Fond d'écran visible de 536 à 547, le panel commence à 548. Canevas de 436 px de haut | § 2.1 : 792 × 512. § 2.3 : canevas 752 × 444. § 11 : au pixel près | Bas de fenêtre à y = 543, 4 px au-dessus du panel. 8 px rendus au canevas |
| I5 | `e1`, `f4`, `f5` | Transformer et Aligner partagent un onglet. XML et Document ont le leur. Six onglets remplissent la largeur : aucune place pour `⋯` | § 2.3 et § 4.1 : Remplissage, Calques, Texte, Transformer, Aligner, `⋯`, 40 × 32 | Séparer Transformer et Aligner. XML et Document passent dans le menu de `⋯`, à x = 752 (fenêtre). Quand l'un d'eux est ouvert, `⋯` montre l'état actif |
| I6 | `e1`, `e3` | Transformer : « Enter applies. », pas de `ui_segmented`. Aligner : ni « Relatif à », ni troisième rang. Champs sans nom sous « Move » ; `%` et `°` seuls pour nommer les autres | § 4.5 : aperçu vivant, sans bouton ni validation. Relatif à, 6 × 3 cases. § 1, principe 4 | Retirer la phrase, appliquer à chaque changement. Segments en icônes de 20 px en S : à 46 px par segment, les mots ne tiennent pas. Aligner : dropdown 232 × 28 en tête. Libellés `TEXTE2` 12 px : dx, dy, Échelle, Rotation |
| I7 | `f5` | Page : deux champs 793.7 et 1123 sans libellé, largeur et hauteur à deviner | § 4.5 : unité, format | Libellés L et H devant les champs, unité 44 à droite |
| I8 | `c4`, `d3`, `f5`, `f6` | Aucun nom de panneau, onglets en icônes seules. Calques et Document n'ont rien qui les nomme | § 4.1 : nom en tête, Sans gras 13, hauteur 28 | Ligne de 28 px sous les onglets, `TEXTE1` gras 13. Elle prend sur la hauteur du panneau, qui défile |
| I9 | `e1`, toutes | Bloc Calque : « SVG » nu. On lit un format de fichier, pas un calque. Aucun signe de menu | § 2.3, 96 px à x = 584. § 4.2 : tape, menu des calques | Nom du calque, ou « Racine » si le dessin n'en a pas. Triangle de 5 px `TEXTE3` à droite, `ui_text_fit` sur 80 px |
| I10 | `d9`, `b8`, `e1` | Vector parle anglais, le dialogue d'enregistrement français. Barre mixte : « L, H » à côté de « File, Edit » | § 6.1 : titres français. § 6.12 : textes par `ui_tr` | Table de traduction fr pour tous les textes, y compris les listes statiques de libellés et les menus. En anglais, écrire W et H |
| I11 | `f1`, `f2`, `f4` | Règle verticale : 200 lu « 20( », 1000 lu « 100 ». Le chiffre dépasse les 18 px | § 11 : pas de libellé coupé | Écrire le chiffre en vertical, à 90°, dans la bande de 18 px. À défaut, le plafonner à 3 caractères à 11 px : 1k pour 1000 |
| I12 | `b4` | Arrêts de dégradé remplis de blanc et de bleu vif, sans rapport avec les couleurs du dégradé, teal à blanc | § 5.1 : « rempli de sa couleur ». § 5.2 : contour | Fond à la couleur de l'arrêt, contour 1,5 px `SURFACE`, puis 1 px `ACCENT`. Choisi : contour `ACCENT` de 2 px |
| I13 | `d3` | Verrou fermé et plein sur deux objets non verrouillés. Rien ne distingue l'état | § 4.4 : case verrou | Ouvert et contour seul, `TEXTE3`. Verrouillé : fermé, plein, `TEXTE1` |
| I14 | `d3` | Calques : barre du bas à + et − seuls. Ni pastille de couleur, ni retrait | § 4.4 : nouveau, supprimer, monter, descendre, grouper | Cinq cases 32 × 32 sur la barre de 32 px. Pastille de 12 px avant le nom |
| I15 | `e1`, `f3`, `f4` | Pastille C : carré blanc dans un cadre, sur des objets sans contour, comme s'il était blanc. Pastille R noire sur le clone, alors qu'il s'affiche teal | § 3.2 : les pastilles montrent le style. § 4.3, rang 2 | Aucun : fond `SURFACE`, diagonale 2 px `DANGER`. Clone : montrer le style résolu de sa source |
| I16 | `e1`, `f6` | Panneaux sans sélection utile : boutons Aligner et champs actifs sur `e1`. Taille, gras, italique actifs sur un clone (`f6`). Aucune phrase. Police courante non marquée | § 4.1 : champs grisés, phrase d'une ligne `TEXTE3` | Griser, avec la phrase en haut du panneau. Ligne de police active : fond `ACCENT_CLAIR` |
| I17 | `b0`, `f1` | Page vide sans invite. Champs grisés sans aide | § 9.1 : invite centrée `TEXTE3`. § 3.4 : « Touchez un objet » | Peindre l'invite au centre de la page, retirée au premier objet. Sans sélection, l'aide tient dans la barre après les champs |

## Mineur

| # | Capture | Ce que je vois | Attendu | Correctif |
|---|---|---|---|---|
| M1 | `e1` | Point modifié bleu, `ACCENT`. Il se lit comme un pixel égaré | § 9.3 : `ALERTE` | Passer à `ALERTE`, même taille et même place |
| M2 | `b3`, `e1` | Marque de famille : carré de 3 px | § 3.1 : triangle de 5 px `TEXTE3` | Triangle de 5 px au coin bas droit, zone 14 × 14 |
| M3 | `b3`, `b4` | Spirale à trois tours, noire et dense. Dégradé : barres égales, on lit un tableau | § 3.3, § 3.4 : deux tours, bandes qui s'éclaircissent | Spirale à deux tours, 2 px, `TEXTE2`. Quatre bandes de pas décroissant, sans teinte |
| M4 | `b2`, `b3`, `b4` | Case Spirale restée en survol, pointeur loin d'elle | § 3.1 : survol seulement | Effacer le survol à la fermeture du menu de famille et à la sortie du rail |
| M5 | `e1`, `d3`, `f4`, `f5`, `c4` | Bord droit à trois valeurs : 768 (Aligner, Document), 788 (Remplissage), 792 (Calques, XML) | § 4.3 : largeur utile 232, de 556 à 788 | Tous les panneaux de 556 à 788. Deux champs : 112 px chacun, écart 8 |
| M6 | `e1` | Cases Aligner 32 × 30 | § 4.5 : 36 × 32 | 6 × 36 tiennent dans 232 px |
| M7 | `c4` | Repère de la zone 2D carré | § 4.3 : anneau de 11 px | Anneau de 11 px, cible de 40 inchangée |
| M8 | `f1`, `c3` | Bord de page 1 px, sans ombre. Avec la grille, le bord se confond avec une ligne majeure | § 5.2 : ombre `CONTOUR` 2 px | Ajouter l'ombre en bas et à droite, peinte après la grille |
| M9 | `f1`, `f2` | Ni marqueur du pointeur, ni bande de sélection sur les règles : aucun pixel bleu dans la règle | § 5.5 | Trait 1 px `ACCENT`, bande `ACCENT_CLAIR` |
| M10 | `b5`, `d3` | Boîtes de la sélection multiple : pâles sur la page, lignes blanches sur l'objet | § 5.1 : une boîte fine par objet | `ACCENT` 1 px à 60 %, sans halo. Vérifier sur carré noir et sur carré blanc |
| M11 | `c4`, `b5` | Le halo de 3 px à 70 % aplatit l'ellipse là où la boîte la touche | § 5.2 : c'est ma valeur qui est trop forte | Halo à 50 %. Revoir sur carré noir et sur carré blanc, puis corriger la spécification |
| M12 | `f4` | Contour des poignées à cheval sur deux pixels, pâle : `#6296E7` au lieu de `ACCENT` | § 5.2 | Caler le centre des poignées sur la grille de pixels |
| M13 | `f4` | « Apply » en bouton principal | § 4.5, § 11 : seul Export en a un | Bouton secondaire. Il reste nécessaire : Entrée fait un saut de ligne |
| M14 | `f4` | `▲` `▼` isolés, sens non dit | § 3.3 : icônes à nous | Icônes 24 × 24 dans des cases 32 × 28, avec infobulles Monter et Descendre |

## Réussi, à ne pas bouger

* **Le gabarit S** : 792 px de large, barre 36, rail 40, tiroir 248, bas 32. Canevas de 752 et 504 px de large, au pixel près.
* **La zone libre du cadre** : 107 px vides, annuler, rétablir et tiroir à leur place. Rien ne touche les glyphes dans la fenêtre principale.
* **Le rail** : état actif net, icônes alignées sur le même axe, famille qui montre le dernier outil.
* **Le tiroir qui pousse** : le centre de la vue reste, la page passe de 421 à 297 sans saut de zoom.
* **La zone de couleur** : 232 × 112 et barre de teinte 232 × 20.
* **La palette et le zoom** : pas de 28 px, 18 cases, groupe de 92 px. Les couleurs vives respirent sur le gris.
* **Les poignées** : carrés et losanges de 11 px, tige de 24 px. Règle des petits objets respectée : 8 poignées sur `d3`, 4 sur `c4`.
* **Guide contre grille** : guide `ALERTE`, nettement au-dessus de la grille, lisible aussi sur le teal.
* **Calques** : lignes de 28 px, œil et verrou à droite.
* **Ctrl+K** : 480 × 340, ligne et raccourci à droite en `TEXTE3`. Seul le haut est à revoir.
* **Barre globale** : tous les titres visibles, aucune ligne de menus dans la fenêtre.
* **Le dialogue Enregistrer** : proportions et jetons justes sur 800 × 600.

## Les 5 corrections les plus utiles, dans l'ordre

1. **Barres d'outils** (B1) : contrôles réels, case `⋯`, indication dans la bande du canevas. Nœuds d'abord, puis Texte et Dégradé.
2. **Recalage de la vue au tiroir** (B2) : sinon le principe qui justifie le tiroir poussé tombe.
3. **Règles** (B3, I11) : décalage de 18 px, chiffres coupés. Correctif court, et la règle ne doit jamais mentir.
4. **Remplissage** (I1) : genre de peinture et opacité. Il remplace aussi le lien « None ».
5. **Nœuds** (I2) : formes de la spécification, `TEXTE1`, pas de boîte, pour ne plus les confondre avec les poignées.
