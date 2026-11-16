---
name: korin-forge
description: Modéliser une pièce en 3D dans Forge, la CAO de Korin (OpenCASCADE, esquisses PlaneGCS), en la pilotant par son serveur MCP - brancher Forge à Vibe ou à Claude, les outils forge_*, les repères, les numéros de faces et d'arêtes, les esquisses, les paramètres, vérifier par capture, exporter en STEP ou STL. À utiliser dès qu'on demande une pièce, un modèle 3D, une CAO, un boîtier, un support, une impression 3D ou un usinage.
user-invocable: true
---

# Piloter Forge (la CAO de Korin) par MCP

Forge est la CAO paramétrique de Korin : des solides exacts (OpenCASCADE), des esquisses contraintes (PlaneGCS), un
**historique** d'opérations rejoué (comme Fusion 360). Elle a un **serveur MCP** : chaque outil `forge_*` ajoute ou
change une opération de l'historique, que l'utilisateur voit tout de suite dans la fenêtre de Forge et peut annuler.
Tu ne dessines pas de maillage : tu décris une suite d'opérations.

Forge existe **sur le PC** (x86-64) seulement pour l'instant : pas sur le Pi. `ls /apps/forge.app` le dit.

## 1. Brancher Forge (une fois)

1. **Forge doit tourner, son pilotage ouvert.** Soit l'utilisateur fait *Fichier > Piloter par un agent (MCP)* dans
   Forge, soit tu la lances toi-même, **sans bloquer le shell** :

   ```sh
   (/apps/forge.app/main --mcp >/dev/null 2>&1 &)
   ```

   Elle écoute sur le port **7311**. Le jeton est dans `/home/forge/mcp.txt` (`token = ...`, créé au premier
   départ) : `cat /home/forge/mcp.txt`. Une fenêtre s'ouvre pour l'utilisateur ; Forge se ferme comme une app.
2. **Déclarer le serveur à l'agent**, dans son `config.toml` : `/.vibe/config.toml` pour Vibe,
   `/.claude-acp/config.toml` pour Claude (claude-acp). Le même bloc pour les deux ; le dire à l'utilisateur et lui
   demander d'abord ; ne pas toucher au reste du fichier ; remplacer `JETON` par celui du fichier :

   ```toml
   [[mcp_servers]]
   name = "forge"
   transport = "streamable-http"
   url = "http://127.0.0.1:7311/mcp"
   headers = { Authorization = "Bearer JETON" }
   startup_timeout_sec = 30
   tool_timeout_sec = 300
   ```

   Forge sur une autre machine : son adresse à la place de `127.0.0.1` (Forge l'affiche dans *Piloter par un agent*).
3. **Une nouvelle séance** (Vibe, ou Claude) : les outils apparaissent alors sous le nom `forge_<outil>`
   (`forge_add_box`...). Claude les voit aussi en cours de séance dès que Forge répond (réessayé à chaque tour). Sans
   eux : Forge n'est pas lancée, ou pas avec `--mcp`, ou le jeton ne correspond pas (Forge répond 401). Avec Claude,
   chaque outil qui change la pièce demande l'accord de l'utilisateur (mode « ask »), sauf s'il l'a donné pour toute
   la séance.

## 2. La règle d'or

1. **`forge_get_document`** d'abord : l'historique (opérations, champs modifiables, formules, erreurs), les corps
   (volume, boîte englobante), les paramètres. Les corps sont numérotés (`index`).
2. Avant d'utiliser une face ou une arête : **`forge_get_body_topology`** du corps. Chaque face a un `index`, un
   `type` (`plane`, `cylinder`...), son `center`, sa `normal` (plane, vers l'extérieur) ; chaque arête son `mid`,
   sa `tangent`, ses bouts, son `type` (`line`, `circle`). **Les numéros changent après chaque opération** sur le
   corps : relire la topologie avant d'en citer d'autres. Choisir par la géométrie (« la face de normale [0,0,1] »,
   « les arêtes de tangente ±Z »), jamais par un numéro deviné.
3. Après une opération : lire ce qu'elle rend (`bodies`, `features_in_error`) ; une étape refusée n'est **pas**
   ajoutée et l'erreur le dit (un congé trop grand donne le plus grand rayon possible : le prendre).
4. **Vérifier en regardant** : `forge_screenshot` (avec `view` : `iso`, `front`, `top`...) rend une image de la vue.
   Le faire après les étapes importantes et à la fin.
5. Une erreur : `forge_undo`, puis corriger. Ne pas empiler des opérations sur un corps faux.

## 3. Les conventions

* Unités : **millimètres** et **degrés**. Z vers le haut ; le sol est le plan XY.
* Une valeur peut être un nombre ou une **expression** (chaîne) : `"40"`, `"2*ep+1"`, `"1 in"`. Une expression qui
  cite un **paramètre** (`forge_set_parameter name="ep" expression="4"`) reste liée : changer `ep` fait suivre toute
  la pièce. Utiliser des paramètres pour les cotes importantes (épaisseur de paroi, diamètre de vis...).
* **Boîte** (`forge_add_box`) : `origin` = le **centre de sa base** ; `width` le long de `x_direction` (+X), `depth`
  le long de la normale × x (+Y), `height` le long de `normal` (+Z ; négatif : vers le bas). Une boîte de 60 × 40 × 10
  posée au sol, centrée sur l'origine : `origin=[0,0,0]`.
* **Cylindre** (`forge_add_cylinder`) : `origin` = centre de la base, `diameter`, `height` le long de `normal`.
* `operation` : `new` (un nouveau corps, par défaut), `join` (ajouter à `body`), `cut` (retirer de `body`),
  `intersect`. Pour poser une forme sur une pièce : `operation="join", body=0`. Pour creuser : `"cut"`.
* `on_face {body, face}` : prendre le plan d'une face plane (l'origine y est projetée). Pratique pour poser sur le
  dessus ou sur un flanc.

## 4. Les outils

| Outil | Pour |
|---|---|
| `get_document`, `get_feature`, `get_body_topology` | lire l'historique, une étape en entier, les faces et arêtes d'un corps |
| `add_box`, `add_cylinder` | les formes de base (nouveau corps, ou jointes, ou retirées) |
| `push_pull` | tirer (+) ou pousser (−) une face plane le long de sa normale |
| `fillet`, `chamfer` | arrondir, chanfreiner des arêtes ; `tangent_chain=true` prend la suite tangente |
| `add_hole` | des trous dans une face plane : `centers` en 3D, `diameter`, `depth` (0 : traversant), `counterbore {diameter, depth}` |
| `shell` | évider un corps : `open_faces` (les ouvertures), `thickness` |
| `pattern` | répéter une étape (forme, perçage, extrusion) : linéaire (`direction`, `spacing`) ou circulaire (`axis`, `center`, `angle`) ; `count` compte l'original |
| `mirror` | symétrie d'un corps : par une face, ou `plane` `yz`/`xz`/`xy` passant par `origin` ; `join` |
| `boolean` | `union` ou `cut` de deux corps (l'outil est consommé) |
| `create_sketch`, `set_sketch_dimension` | une esquisse 2D contrainte ; changer une cote |
| `extrude`, `revolve` | donner du volume aux contours fermés d'une esquisse |
| `edit_feature`, `delete_feature`, `suppress_feature`, `set_rollback` | changer une étape existante (ses `fields`), l'enlever, la sauter, la barre de reconstruction |
| `set_parameter`, `delete_parameter` | les paramètres nommés |
| `undo` (`redo`, `steps`), `new_document` | annuler, rétablir, repartir de zéro |
| `set_view`, `screenshot`, `highlight` | la vue, une image PNG, montrer une face ou une arête à l'utilisateur |
| `export` | STEP, IGES, STL (binaire) ou BREP ; un chemin relatif va dans `/home/forge/` |

## 5. Les esquisses

`forge_create_sketch` : `plane` = `xy` (par défaut), `xz` (x → X, y → Z), `yz` (x → Y, y → Z), avec `offset` le long
de la normale ; ou `on_face {body, face}`. Les points sont en **2D dans le plan** ; un point à **3 nombres** est un point
de l'espace projeté sur le plan (le plus simple sur une face).

Éléments : `line {from, to}`, `polyline {points, closed}`, `rectangle {corner1, corner2}`, `circle {center, diameter}`,
`arc {center, start, end}` (sens direct), `construction: true` pour un axe. Les points aux mêmes coordonnées sont
partagés (contour fermé). Lignes horizontales et verticales : contraintes H/V posées d'elles-mêmes. `constraints`
cite les éléments par leur numéro dans l'esquisse (`elements_created` dans la réponse) : `length`, `radius`,
`diameter` (avec `value`), `horizontal`, `vertical`, `parallel`, `perpendicular`, `equal`, `tangent`, `concentric`.
Un contour dans un plus grand fait un trou à l'extrusion. `revolve` tourne autour de la **première ligne de
construction**.

## 6. Recettes

**Plaque percée aux coins**

```
set_parameter name=ep expression=6
add_box origin=[0,0,0] width=80 depth=50 height="ep"           -> corps 0
get_body_topology body=0   (dessus : normal [0,0,1] ; arêtes verticales : tangent [0,0,±1])
fillet body=0 edges=[les 4 verticales] radius=6
get_body_topology body=0   (les numéros ont changé)
add_hole body=0 face=<dessus> centers=[[-32,-17,6],[32,-17,6],[32,17,6],[-32,17,6]] diameter=4.5 counterbore={diameter:8, depth:3}
screenshot view=iso
```

**Boîtier** : `add_box` (l'extérieur) → `fillet` des arêtes verticales → `shell` avec `open_faces=[le dessus]`,
`thickness="paroi"` → `add_hole` sur un flanc (passage de câble).

**Bride (révolution)** : `create_sketch plane=xz` avec une `line` de construction sur l'axe (`from [0,0] to [0,30]`)
et le profil fermé (`polyline` closed) à droite de l'axe → `revolve sketch=<n> angle=360` → `add_hole` sur la face
du dessus, puis `pattern feature=<perçage> mode=circular axis=z center=[0,0,0] count=6`.

**Bossage sur une face** : `create_sketch on_face={body:0, face:<dessus>}` avec un `circle` en 3D sur cette face →
`extrude sketch=<n> distance=10 operation=join body=0`.

**Pièce symétrique** : modéliser une moitié contre le plan YZ, puis `mirror body=0 plane=yz join=true`.

## 7. À savoir

* Les messages et noms d'étapes de Forge sont en français (`Boîte 1`, « le congé ne passe pas »...) : c'est normal.
* L'utilisateur peut travailler dans Forge en même temps : si un outil répond que l'utilisateur édite une esquisse,
  lui demander de la terminer. Une commande qu'il avait commencée est annulée par ton outil.
* Forge est lente sous QEMU (une opération : jusqu'à quelques secondes) ; ne pas relancer un outil qui tarde.
* Exporter pour l'utilisateur : `forge_export path="/sd1/documents/piece.step"` (ses données sont sur `/sd1`) ;
  STL pour l'impression 3D, STEP pour une autre CAO.
* Forge n'enregistre pas encore son document (étape 5 de la CAO) : exporter en STEP pour garder la pièce, et
  rappeler à l'utilisateur que l'historique est perdu à la fermeture.
