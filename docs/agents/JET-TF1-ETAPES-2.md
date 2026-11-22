# Pour l'agent local (Claude Code sous Korin, ZBook) : TF1+, deuxième mesure avec jet 1.4.7

Suite de `docs/agents/JET-TF1-ETAPES-RESULTATS.md` (ton rapport, merci) : relire d'abord `JET-TF1-ETAPES.md`, ses
règles et sa façon de faire valent ici. Tu ne modifies ni Jet ni le système. Le dossier est `~/jet-mesures/10/`.

## Ce que ta mesure a appris, et ce que 1.4.7 ajoute

« other » valait ≈ 400 ms par mise à jour lourde. Dans `Page::updateRendering`, il couvrait surtout **la mise en page
du milieu** : le `layoutIfNeeded()` qui suit les `requestAnimationFrame` et l'animation du curseur. Le patch 0014 ne
la chronométrait pas. Le **patch 0015** (`tools/webkit/patches/0015-style-layout-times.patch`, publié dans **jet
1.4.7**) ajoute à la ligne `steps:` :

```
web: gpu: steps: 4 updates, 826 ms (worst 731 ms); layout 757, compositing layout 0, after 63, layer flush 0,
  middle layout 0, timelines 0, other 1, …étapes… ; style 581 ms (8 recalcs, 1512 elements, 5 rebuilds),
  tree layout 668 ms, post layout 14 ms (3 full, 0 subtree)
```

- `middle layout` : la mise en page du milieu (suspect n° 1 : c'est elle qui devrait reprendre les ≈ 400 ms).
- `timelines` : les frises d'animation liées au défilement (`updateStaleScrollTimelines`).
- `other` doit tomber à ≈ 0. S'il reste gros, le dire : c'est important.
- Après le `;`, des comptes faits **partout dans la fenêtre de 2 s**, aussi hors de la mise à jour, par exemple
  quand le JavaScript du site force le style ou la mise en page en lisant `offsetTop`, `getBoundingClientRect` ou
  `scrollY` :
  - `style` : le temps de `Document::resolveStyle` ; `recalcs`, le nombre de recalculs ; `elements`, la taille des
    mises à jour du style (le nombre d'éléments changés) ; `rebuilds`, les reconstructions complètes du style du
    document (un changement de feuille de style, de police ou de media query en provoque).
  - `tree layout` : la mise en page de l'arbre de rendu.
  - `post layout` : l'après-mise en page (positions des couches, mise à jour des couches composées, repeinture).
  - `full` et `subtree` : le nombre de mises en page complètes (depuis la racine) et partielles.
- Le style et la mise en page se recouvrent un peu : une mise en page peut recalculer le style d'abord. Ne pas les
  additionner à « updates ».

Ce qui décidera le correctif :

- **`style` gros, avec beaucoup d'`elements` à chaque recalcul** (des milliers) : le site change quelque chose qui
  invalide le style de toute la page à chaque image. Une variable CSS sur `:root` ou `body`, ou une classe sur
  `html`, par exemple.
- **`rebuilds` > 0 pendant le défilement** : une reconstruction complète, encore plus chère (feuille de style
  insérée, police, media query).
- **`tree layout` gros avec `full`** : une mise en page complète à chaque image (une taille ou une position qui
  change en haut de l'arbre).
- **`post layout` gros** : les couches. C'est ce qui pourrait expliquer que le bas de la page reste lent avec le
  GPU et pas sans.
- **le style ou la mise en page plus gros que la somme des `updates`** : c'est le JavaScript du site qui les force
  hors de la mise à jour.

## La mesure

1. `pkg update`, puis vérifier que `pkg list | grep jet` dit **1.4.7**.
2. Le même passage que la dernière fois (défilement automatique, saut à 7000 vers 45 s), avec et sans GPU :

   ```bash
   cd ~/jet-mesures/8/sondes
   DUR=68 bash run-exp2.sh steps2-gpu ONYX_WEB_GPU_DEBUG=1 --
   DUR=68 bash run-exp2.sh steps2-nogpu ONYX_WEB_GPU_DEBUG=1 -- --nogpu
   ```

   (Adapter comme la dernière fois : les lignes `steps:` arrivaient au journal.)
3. **Si le temps le permet**, une sonde de plus : savoir *ce qui* invalide le style. Dans `tf1-probe.js`, un
   `MutationObserver` sur `document.documentElement` avec `{attributes: true, subtree: true,
   attributeOldValue: true}`. Pendant 2 s de défilement lent, compter les mutations par (élément, attribut) et
   imprimer les 10 plus fréquentes, avec un exemple de valeur (`style`, `class`, `data-*`…). Une ligne par
   fenêtre, par exemple `MUTATIONS html.style 20, div#header.class 20, …`.

## Le rapport : `docs/agents/JET-TF1-ETAPES-2-RESULTATS.md`

1. Le même tableau que la dernière fois, avec les nouveaux champs **par mise à jour** : `middle layout`, `timelines`,
   `other`. Puis, **par fenêtre** : `style` (ms, `recalcs`, `elements`, `rebuilds`), `tree layout`, `post layout`,
   `full` et `subtree`. Pour les fenêtres lentes, rapides et au repos, avec et sans GPU.
2. **L'étape coupable**, avec les chiffres qui la désignent. Puis, entre style, mise en page de l'arbre et
   après-mise en page, celui qui fait les ≈ 400 ms.
3. Si la sonde a été faite, les mutations les plus fréquentes.
4. En annexe, les lignes brutes.

Le pousser dans le dépôt, comme le dernier rapport : la session du cloud en tire le correctif.

## Le SDK de Jet

Pour essayer toi-même un changement de WebKit (`docs/agents/JET-SDK.md`) : le SDK publié est celui de **1.4.6**,
sans 0015. Pour cette mesure, prendre le paquet 1.4.7, pas le SDK.
