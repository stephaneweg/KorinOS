# Viewport3D : la 3D en .NET sur le GPU

`System.3D.dll` (`tools/dotnet/3d/`) donne à une `Form` un contrôle `Viewport3D` à la manière de WPF : une
caméra, des `Visual3D`, des géométries, des matériaux, des lumières. Le rendu passe par GpuKit sur le V3D du
Pi 4, et par le processeur sans GPU.

```csharp
using System.Windows.Media.Media3D;

var view = new Viewport3D { Dock = DockStyle.Fill };
view.Camera = new PerspectiveCamera (new Point3D (0, 2, 6), new Vector3D (0, -2, -6), new Vector3D (0, 1, 0), 50);
var spin = new AxisAngleRotation3D (new Vector3D (0, 1, 0), 0);
var g = new Model3DGroup ();
g.Children.Add (new AmbientLight (Color.FromArgb (40, 40, 40)));
g.Children.Add (new DirectionalLight (Color.White, new Vector3D (-1, -2, -1)));
g.Children.Add (new GeometryModel3D (MeshBuilder.Box (2, 2, 2), new DiffuseMaterial (Color.Orange))
	{ Transform = new RotateTransform3D (spin) });
view.Children.Add (new ModelVisual3D (g));
form.Controls.Add (view);
// ensuite, un Timer change spin.Angle : la scene est retenue, le controle redessine seul, une fois par tour de boucle
```

`Viewport3D` se déclare aussi dans un `.ui` (`<Viewport3D Name="view" Dock="Fill" />`) ; la scène reste dans le code.

## Le nom

L'assembly s'appelle `System.3D`, mais un espace de noms C# ne peut pas commencer par un chiffre. Le code est
dans **`System.Windows.Media.Media3D`**, celui de WPF : les types ont les mêmes noms, un exemple WPF se lit
tel quel, et une seule directive `using` suffit (WPF met `Viewport3D` à part, dans `System.Windows.Controls`).

## Ce que ça utilise

| Couche | Rôle |
|---|---|
| `Scene.cs`, `Math3D.cs` | le graphe de scène retenu ; chaque objet signale ses changements |
| `Renderer.cs` | `Renderer3D` : lumière par sommet, découpe, sommets et lots de `gk_render` ; utilisable sans fenêtre |
| `Rasterizer.cs` | le repli sur le processeur, sur ces mêmes sommets et lots |
| `Viewport3D.cs` | le contrôle : une surface attachée d'Elegant, une vue d'UIKit pour la souris et le clavier |
| `MeshBuilder.cs` | boîte, plan, sphère |

* **Lumière par sommet, sur le processeur.** Les shaders de GpuKit sont écrits à la main et fixes
  (`texel * couleur + couleur 2`). Le diffus et l'ambiant vont dans la couleur, le spéculaire et l'émis dans la
  couleur 2. Pas de lumière par pixel ni de matériau programmable tant qu'il n'y a pas d'autres shaders.
* **Découpe.** `gk_render` refuse un sommet hors de la bande de garde. Un modèle dont la boîte y tient garde
  sa matrice, et le GPU le transforme ; les autres sont transformés et découpés en C# (`Clipped`). C'est ce qui
  permet un sol immense ou une caméra dans l'objet.
* **Image complète.** Le processeur dessine à côté, puis recopie d'un coup, et GpuKit fait de même par son
  tampon d'étape : Elegant compose la surface quand il veut, et il ne doit jamais prendre une image à moitié
  faite. Sous QEMU, sans cette recopie, on voyait la théière se construire.
* **Transparence.** Les modèles dont la couleur a un alpha sont dessinés après les opaques, du plus loin au
  plus près, sans écrire le Z, un modèle à la fois (pas de tri par triangle).

## Limites de cette version

* **Un `Viewport3D` affiché par fenêtre** : Elegant n'a qu'une surface par fenêtre. Un second lève
  `InvalidOperationException`, sauf s'ils sont sur des onglets différents (la surface passe de l'un à l'autre).
* **Rien par-dessus** : la surface recouvre les widgets ; pas de contrôle posé sur la vue 3D.
* **65 536 triangles par image** (`GK_MAX_VERTS`), 1024 lots, 2048 × 2048 : au-delà, le processeur dessine.
  Pas de suite d'images : le Z ne survit pas d'un `gk_render` à l'autre.
* **Pas de sommets indexés** : `gk_render` les veut déroulés, donc 32 octets par sommet de triangle à chaque image.
* **Pas d'anticrénelage**, de mipmaps, d'ombres, de lumière spot, de `HitTest`, ni de scène dans un `.ui`.
* Le Z n'est pas relu, et la lumière se refait à chaque image, même si rien n'a bougé dans le modèle.
* Couleurs et Z sont linéaires à l'écran dans le repli (textures corrigées en perspective), corrigés partout
  sur le GPU : de petits écarts de teinte entre les deux.

## Mesures

| Où | Scène | Scène C# | Rendu | Image |
|---|---|---|---|---|
| PC (x64, WSL, un cœur, processeur) | théière 640 × 480 | 0,4 ms | 2,8 ms | |
| PC | cubes | 0,1 à 0,4 ms | 13 à 18 ms | |
| QEMU, processeur | théière 640 × 484 | 11,2 ms | 128,6 ms | 165,9 ms |
| QEMU, processeur | cubes | 2,1 ms | 347,9 ms | 363,8 ms |

Les lignes QEMU datent d'avant la recopie finale du repli, qui ajoute une copie de 640 × 484 pixels par image.

**Rien n'est mesuré sur le vrai GPU.** QEMU n'a pas de V3D et sa carte exagère les coûts : ces chiffres ne
disent rien du Pi. Sur le Pi, la référence est Onyx, qui a mesuré sa théière de 6400 triangles (C++) à **1,0 ms**
par le GPU directement dans le canevas, 2,1 ms avant cela, 13,4 ms en logiciel.

Estimation pour `view3d` sur le Pi, à vérifier : 1 à 3 ms de scène en C#, environ 1 ms de GPU, 1 à 2 ms de
vérification, copie des sommets et tampon d'étape, soit **de l'ordre de 5 ms pour 640 × 480**, et bien plus
de 60 images/s si la composition d'Elegant suit. Le repli C# devrait rester au-dessus de 15 ms.

## Vérifié, et comment

* `sh tools/dotnet/view3dtest.sh` (PC, sans ARMPI) : images `.ppm` et vérifications (normales, sens des
  triangles, découpe, couleur exacte, face arrière).
* Sous QEMU, dans une `Form` : théière et cubes, souris, molette, boutons, agrandissement ; deux vues sur deux
  onglets, la surface suit l'onglet et disparaît sur une page sans 3D ; une seconde vue affichée est refusée
  (la vue refusée reste dans la fenêtre : c'est une erreur de programmation, pas un cas à rattraper).
* `kits.sh` construit l'assembly et l'application, et un `.ui` contenant `Viewport3D` compile par `uigen`.
* Pas essayé : `--software` et `SoftwareOnly` sur le Pi, une lumière ou un modèle qui change hors de la démo,
  `ImageTexture.FromFile`, `OrthographicCamera` dans l'OS (seulement sur le PC), `formsdemo` après les retouches de `Forms.cs`.
* **`view3d --dry --bench 30`** sous QEMU : `gk_render` en mode à blanc (`GK_DBG_DRY`) construit ses listes sans
  rien envoyer et **accepte** les deux scènes (0 : structures, bande de garde, textures, lots découpés, tampon
  d'étape). Cela prouve que GpuKit prend ces images, pas que le GPU les dessine bien.

## À essayer sur le Pi

Mettre à jour la carte (`kits.sh`, `build.ps1 sd`), puis, dans le terminal :

```sh
/apps/view3d.app/main --bench 300
/apps/view3d.app/main --cubes --bench 300
/apps/view3d.app/main --software --bench 300
```

| Attendu | Si ce n'est pas le cas |
|---|---|
| `view3d : GPU` en première ligne | `processeur (gk_render N)` : le code dit pourquoi (-1 pas de GPU, -2 refusée, -3 perdu, -4 mémoire) |
| théière lisse, cuivre, reflet qui suit | morceaux manquants : sens des faces ; rouge et bleu inversés : la cible |
| cubes : damier net, verre devant, sol jusqu'à l'horizon | sol absent : la découpe ; damier brouillé : textures (`gk_texture`) |
| la même image avec le bouton GPU / CPU | écart franc : à comparer aux chiffres de la ligne du bas |
| molette jusque dans la théière : pas de blocage | GPU éteint (`gk_render -3`) : un sommet extrême a bloqué le binner |
| « image complète » de l'ordre de 5 ms | noter les trois nombres : scène, rendu, job GPU |

Bug trouvé en route : depuis le passage à des tampons de 64 Mo (« Agrandir en HD », 30931dc), `gk_phys` et
`gk_free` décalaient encore pour 16 Mo, et prenaient donc un mauvais numéro de tampon : sous QEMU à blanc,
`gk_render` rendait `GK_E_NOMEM` sur une surface. Tout programme qui passe par `gk_phys` est touché, Jet et son
gpucomp sans doute aussi (pas essayé). Corrigé le 31/10/2026 (3339cd0).

## Suite

1. Mesurer sur le Pi.
2. Quand les chiffres seront là, dire si le tampon d'étape coûte assez pour une surface sous 1 Go (`BUF_LOW` dans
   `uk_sf_alloc`, gain estimé 0,5 à 1 ms) ; il faudrait alors un échange de deux tampons, sinon Elegant verra des
   images partielles.
3. Sommets indexés dans GpuKit (`INDEXED_PRIM_LIST`) et plus de 16 384 triangles.
4. Anticrénelage : MSAA 4x du V3D (à ajouter à GpuKit) plutôt qu'un rendu double réduit en C#.
5. `HitTest` (rayon contre triangles, comme `pick` de 3DForge), lumière spot, scène dans les `.ui`.
6. Shaders propres (lumière par pixel) : il faudrait un assembleur QPU, celui d'Onyx (`tools/qpu`).
