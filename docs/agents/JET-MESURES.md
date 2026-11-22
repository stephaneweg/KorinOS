# Mesurer la fluidité de Jet sur le ZBook (consignes pour Claude Code sous Korin)

But : trouver pourquoi Jet saccade sur Netflix et Disney+ (défilement, agrandissement d'une vignette), alors que
Wikipédia et Spotify sont fluides. Tu tournes sur la machine : fais les essais toi-même et écris un rapport. **Tu ne
peux pas reconstruire Jet ici** (WebKit se construit dans WSL ou dans le cloud) : tu mesures avec les interrupteurs
qui existent, tu ne modifies ni Jet ni le système.

## Règles

- Ne connecte aucun compte et ne tape aucun mot de passe : l'utilisateur est déjà connecté à Netflix dans l'app.
- Ne publie rien, ne touche pas à `/apps`, `/etc`, `/sys`, `/media`. Écris seulement dans `~/jet-mesures/`.
- Lance l'app **depuis bash**, une seule à la fois ; ferme-la entre deux essais (`kill` de son pid si besoin).
- Les actions dans la fenêtre (défiler, ouvrir une vignette) sont faites par l'utilisateur : demande-lui de le faire
  pendant un temps donné (« défile 10 s, ouvre 3 vignettes, ferme l'app »), puis lis le fichier.

## Recueillir les messages

Lancée depuis bash, l'app écrit ses messages sur sa sortie d'erreur, **pas dans le journal** (`journal` n'y voit que
les lancements depuis le panneau). Passer par un tube, jamais `>` seul (les process de Jet s'y écrasent) :

```bash
mkdir -p ~/jet-mesures
ONYX_WEB_GPU_DEBUG=1 /apps/netflix.app/main 2>&1 | grep "web: gpu" > ~/jet-mesures/debug.txt
```

`/apps/jet.app/main <adresse>` marche de même (mêmes interrupteurs). Les variables passent au process de la page.

## Les interrupteurs (variables d'environnement)

| Variable | Effet |
|---|---|
| `ONYX_WEB_GPU_DEBUG=1` | l'arbre des couches, chaque peinture, et toutes les 2 s une ligne `times:` (mise à jour de la page, parcours des couches, enregistrement, tracé, composition) |
| `ONYX_WEB_GPU_TRACE=1` | une ligne par zone invalidée : `invalidate layer … from <adresses>` (les fonctions qui l'ont demandée) |
| `ONYX_WEB_GPU_CPU=1` | composition par le processeur (gpucomp) au lieu du GPU |
| `ONYX_WEB_GPU_NOCORES=1` | les tuiles tracées par le fil principal seul (pas les 7 fils) |
| `ONYX_WEB_GPU_MAINWAITS=1` | le fil principal ne trace pas, il attend les fils |
| `ONYX_WEB_GPU_NOANIM=1` | les animations sur le fil principal (pas par le compositeur) |
| `ONYX_WEB_GPU_NOGROUPS=1` | pas de groupes d'opacité |
| `ONYX_WEB_GPU_3D=1` | des couches pour les transformations 3D (déjà essayé : rien gagné, vignettes noires une fois) |
| `KORIN_WEB_RASTER_THREADS=N` | nombre de fils de tracé (0 à 7) |

Les fichiers `SD:/etc/web-gpu-*` ne marchent pas (chemin refusé depuis l'arbre unique) : seulement les variables.

## Lire les lignes

`2.0 s: N frames (M without a page update), V video pictures, T tiles painted, R rects repainted, P px, paint X ms,
upload Y ms, composite Z ms` : N/2 = images par seconde ; `composite Z / N` = coût d'une composition.

`times: … page update A ms, layer walk B ms (… record C ms, raster D ms …), composite E ms` : A + B + E = le temps du
fil principal passé dans les images ; le reste des 2 s est hors des images (JavaScript du site, ou attente).

## Ce qu'on sait déjà (21-22/11, Jet 1.4.3, fenêtre 1000 × 666)

- Au repos et pour une animation seule : 24 à 58 images/s, composition 1 à 2 ms. Le GPU suit.
- Au défilement sur Netflix : 2 à 5 images/s. Par image : composition **15 à 45 ms** (parfois 1 ms sur la même page :
  une attente, pas du travail du GPU ?), enregistrement ~14 ms, mise à jour de la page 5 à 20 ms. Les images ne
  prennent que 20 à 30 % du fil principal : le reste n'est pas mesuré.
- Les redessins viennent tous de mises en page de Netflix (`repaintAfterLayoutIfNeeded`,
  `RenderBlockFlow/RenderFlexibleBox/RenderGrid/RenderImage::layout`) : le document grandit (1305 → 4907 px), les
  images arrivent. Le défilement d'une page simple ne redessine rien (vérifié sous QEMU).

## Les essais à faire (dans cet ordre), même geste à chaque fois

1. **Référence** : `ONYX_WEB_GPU_DEBUG=1` → `debug.txt`.
2. **Composition par le processeur** : `ONYX_WEB_GPU_DEBUG=1 ONYX_WEB_GPU_CPU=1` → `cpu.txt`. Si `composite` par image
   tombe bien sous 15-45 ms, la composition GPU attend quelque chose (service intelgpu, réveil du fil).
3. **Sans les fils de tracé** : `ONYX_WEB_GPU_DEBUG=1 ONYX_WEB_GPU_NOCORES=1` → `nocores.txt`. Si la composition GPU
   redevient ~1-2 ms, les fils de tracé gênent l'attente du GPU.
4. **Le reste du temps** : pendant l'essai 1, relever la charge (`sysstat` toutes les 2 s dans un autre bash, 10 fois,
   → `sysstat.txt`) : le process web de Jet est-il à 100 % d'un cœur (JavaScript) ou presque au repos (attente) ?
5. Si le temps le permet : Disney+ (`/apps/disneyplus.app/main`) avec l'essai 1 ; et Wikipédia
   (`/apps/jet.app/main https://fr.wikipedia.org/wiki/Belgique`) avec l'essai 1, pour comparer.

Pour chaque essai, garder seulement les lignes utiles : `grep "frames (\|times:" fichier`.

## Le rapport

Écrire `~/jet-mesures/RAPPORT.md` : pour chaque essai, un tableau (images/s au défilement, composition par image,
enregistrement par image, mise à jour de la page, part du fil principal dans les images), la charge relevée, et
une conclusion en trois lignes : où part le temps. Joindre les lignes brutes en annexe. Des adresses de
`ONYX_WEB_GPU_TRACE` : les laisser brutes (les symboles de Jet ne sont pas sur la machine ; ils se traduisent dans le
cloud).

---

# Deuxième série (22/11) : où attend la composition par le GPU

Résultat de la première série (`~/jet-mesures/RAPPORT.md`) : sur Netflix, la composition par le GPU coûte 15 à 45 ms
par image, le processeur fait la même en ≈ 1 ms ; sans les fils de tracé, 1 à 8 ms. Le travail du GPU n'explique
pas ces temps : le fil de Jet attend. Piste (lue dans `src/svc/intelgpu/serve.c` et `engine.c`) : le service
`intelgpu` n'envoie un lot au GPU que quand le précédent est fini **et** que le moteur a dit « repos » (le CSB, une
interruption de changement de contexte) ; s'il ne le dit pas, le lot suivant attend jusqu'à **50 ms**. Une image de
Jet fait plusieurs lots (tuiles envoyées, dessin, relecture) : quelques retards par image suffisent.

Mêmes règles que plus haut. Dossier : `~/jet-mesures/2/`.

1. **Les messages du service** : `journal`, puis
   `grep -a "intelgpu\|CSB\|tempête\|sondage\|ANOMALIE\|blocage" /journal.txt > ~/jet-mesures/2/journal-gpu.txt`.
   Une ligne « CSB sans « repos » 50 ms après la fin » ou « tempête d'interruptions … : sondage » confirme la piste.
2. **Le compte des lots** : `/sys/svc/intelgpu status > ~/jet-mesures/2/avant.txt`, puis l'essai 1 de la première
   série (Netflix, `ONYX_WEB_GPU_DEBUG=1`, défiler 10 s), puis `/sys/svc/intelgpu status > ~/jet-mesures/2/apres.txt`
   aussitôt l'app fermée. La différence des « soumissions », « interruptions » et « changements de contexte »,
   divisée par le nombre d'images (`frames (` de l'essai), donne les lots par image et s'il y a une interruption par
   lot.
3. **Au repos pour comparer** : la même chose pendant 20 s d'une bande-annonce, sans toucher à rien
   (`avant-repos.txt`, `apres-repos.txt`) : là, la composition coûtait ≈ 2 ms par image.
4. **Le délai de 50 ms** : si `intelgpu status` dit « interruptions par sondage » au lieu de « MSI », le noter en tête
   du rapport : tout le service tourne alors au rythme de son sondage.

Rapport : `~/jet-mesures/2/RAPPORT.md`, les trois relevés de `status` en entier, les lots et interruptions par image
au défilement et au repos, et les lignes du journal trouvées.
