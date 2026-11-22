# Pour l'agent local (Claude Code sous Korin, ZBook) : mesurer les étapes de l'affichage de TF1+ avec jet 1.4.6

Suite de `docs/agents/JET-TF1-DEFILEMENT.md` (lu d'abord : ses pièges valent ici). La session du cloud a
instrumenté WebKit (patch `0014-rendering-update-times.patch`, publié dans **jet 1.4.6**) pour trouver quelle étape
de la mise à jour de l'affichage coûte ≈ 500 ms par image quand le haut de TF1+ défile. Elle ne peut pas mesurer
elle-même (tf1.fr est bloqué depuis le cloud). Ton travail : la mesure, puis un rapport. Tu ne modifies ni Jet ni
le système.

## Avant

- `pkg update`, puis vérifier : `pkg list | grep jet` doit dire **1.4.6** (sinon les lignes `steps:` n'existent pas).
- Les règles de `docs/agents/JET-MESURES.md` : aucun compte, aucune publication, rien hors de `~/jet-mesures/`.
- Dossier : `~/jet-mesures/9/`.

## La mesure

Rejouer le passage **`gpu2`** de `~/jet-mesures/8/sondes/` (défilement automatique de la sonde
`tf1-probe.js`, `run-exp2.sh`), avec le diagnostic :

```bash
cd ~/jet-mesures/8/sondes
DUR=68 bash run-exp2.sh steps-gpu ONYX_WEB_GPU_DEBUG=1 --
```

(adapter si `run-exp2.sh` range ses sorties ailleurs : il faut la sortie de l'app **par un tube**,
`… 2>&1 | cat > fichier`, jamais `>` seul ni le journal.) Puis, si le temps le permet, le même passage avec
`--nogpu` (`steps-nogpu`) : il dit si la mise à jour des couches compte.

Garder les lignes utiles :

```bash
grep "steps:\|times:\|frames (\|DEFILE\|scrollY" sortie > ~/jet-mesures/9/steps-gpu.txt
```

## Lire une ligne `steps:`

```
web: gpu: steps: 3 updates, 1650 ms (worst 640 ms); layout 2, compositing layout 1400, after 5, layer flush 30,
  other 8, AnimationFrameCallbacks 120, IntersectionObservations 90, …
```

- `updates` : les mises à jour de l'affichage dans la fenêtre de 2 s ; `ms` leur total ; `worst` la plus longue.
- `layout` : la mise en page du début.
- `compositing layout` : la mise en page finale **avec la mise à jour des couches composées**
  (`RenderLayerCompositor::updateCompositingLayers`, carte de recouvrement) — suspect n° 1.
- `after` : les étapes d'après (curseur, minutage de la peinture, sélection, surlignages).
- `layer flush` : le vidage des couches vers le compositeur (avec le GPU seulement).
- `other` : ce qu'aucune étape ne couvre (petit sur le ZBook ; s'il est gros, le dire).
- puis les étapes de WebKit de plus d'1 ms, de la plus chère à la moins chère : `AnimationFrameCallbacks` (les
  `requestAnimationFrame` du site), `IntersectionObservations`, `ResizeObservations`, `UpdateContentRelevancy`
  (`content-visibility`), `Images` (images lazy), `Animations`, `Scroll`…

## Le rapport : `~/jet-mesures/9/RAPPORT.md`

1. Un tableau, pour les fenêtres **lentes** (haut de la page, `scrollY` < ≈ 5000) et les fenêtres **rapides** (plus
   bas, ou au repos) : updates, total, worst, et chaque champ (`layout`, `compositing layout`, `after`,
   `layer flush`, `other`, les étapes nommées) **par mise à jour** (total ÷ updates).
2. **L'étape coupable** : celle qui fait l'essentiel des ≈ 500 ms par image dans les fenêtres lentes, et ce qu'elle
   vaut dans les rapides.
3. La même chose pour `--nogpu` si fait.
4. En annexe, les lignes brutes `steps:` (toutes), et quelques `times:` / `frames (` des mêmes fenêtres.

Pousser le rapport dans le dépôt (`docs/agents/JET-TF1-ETAPES-RESULTATS.md`, en français, comme les autres
documents de `docs/agents/`) ou le donner à l'utilisateur : la session du cloud en tire le correctif.
