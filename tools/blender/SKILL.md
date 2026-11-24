---
name: korin-blender
description: Faire de la 3D avec Blender 5.2 sur Korin PC en le pilotant par Python sans fenêtre (`blender -b --python`) - créer ou modifier une scène (objets, modificateurs, matériaux, caméra, lumières), l'enregistrer en .blend, la rendre en image par Cycles sur le processeur, importer ou exporter OBJ, PLY, STL. À utiliser dès qu'on demande un modèle 3D, une scène, un rendu, une image de synthèse, une animation simple ou une conversion de fichiers 3D, et que Blender est installé (/apps/blender.app).
user-invocable: true
---

# Piloter Blender sur Korin

Blender 5.2 LTS est installé dans `/apps/blender.app` (paquet `blender`, Korin PC seulement : `ls /apps/blender.app`
le dit). Son exécutable est **`/apps/blender.app/main`**. Il a son API Python complète (`bpy`, Python 3.13).

Tu le pilotes **sans fenêtre** (`-b`, « background ») : tu écris un script Python, Blender l'exécute puis s'arrête.
L'utilisateur, lui, peut ouvrir le résultat dans Blender avec sa fenêtre.

## 1. Lancer un script

```sh
/apps/blender.app/main -b --factory-startup --python-exit-code 1 --python ~/scene.py -- arg1 arg2
```

* `-b` : sans fenêtre (indispensable : tu ne vois pas l'écran) ;
* `--factory-startup` : la scène de départ d'usine (cube, lumière, caméra), sans les réglages de l'utilisateur ;
* `--python-exit-code 1` : une exception du script fait sortir Blender avec le code 1 (sinon 0 même en cas d'erreur) ;
* ce qui suit `--` va au script : `sys.argv[sys.argv.index("--") + 1:]` ;
* ouvrir un fichier d'abord : `main -b fichier.blend --python script.py`.

Le démarrage prend quelques secondes. Écris tes scripts dans le dossier de l'utilisateur (`~/`), pas dans
`/apps` (en lecture seule pour lui).

## 2. Ce qui marche (et ce qui manque)

| Marche | Manque encore (pas dans cette version) |
|---|---|
| objets, maillages, modificateurs : **subdivision** (OpenSubdiv), **booléens** (solveurs `EXACT`, `MANIFOLD`, `FAST`), biseau, miroir, réseau, **océan**, remaillage… | suivi de mouvement (libmv) |
| matériaux, caméras, lumières ; simulation de fluides (Mantaflow) | numpy (`import numpy` échoue) |
| enregistrer et ouvrir des `.blend` | rendu EEVEE et Workbench sans fenêtre (ils demandent le GPU) |
| rendu **Cycles sur le processeur** (accéléré par Embree), Freestyle, en PNG, JPEG, EXR, TIFF, WebP, Cineon/DPX | FBX, glTF, USD, Alembic, vidéo (FFmpeg), son |
| import/export **OBJ, PLY, STL** (`bpy.ops.wm.obj_export` …) | |

## 3. Exemples

Une scène et son rendu :

```python
import bpy
bpy.ops.object.select_all(action="SELECT"); bpy.ops.object.delete()
bpy.ops.mesh.primitive_uv_sphere_add(radius=1, location=(0, 0, 1))
bpy.ops.mesh.primitive_plane_add(size=10)
mat = bpy.data.materials.new("Rouge"); mat.use_nodes = True
mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.8, 0.1, 0.1, 1)
bpy.data.objects["Sphere"].data.materials.append(mat)
bpy.ops.object.camera_add(location=(6, -6, 4), rotation=(1.1, 0, 0.785))
bpy.context.scene.camera = bpy.context.object
bpy.ops.object.light_add(type="SUN", location=(0, 0, 10))
sc = bpy.context.scene
sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 32
sc.render.resolution_x, sc.render.resolution_y = 800, 600
sc.render.filepath = "/home/rendu.png"           # (un chemin du dossier de l'utilisateur)
bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath="/home/scene.blend")
```

Convertir un STL en OBJ : `bpy.ops.wm.stl_import(filepath=...)` puis `bpy.ops.wm.obj_export(filepath=...)`.

## 4. Conseils

* **Rendu** : Cycles sur le processeur est lent ; commence petit (`samples` 16 à 64, 640 × 480), puis monte.
  `sc.render.threads` vaut le nombre de cœurs.
* **Vérifier** : relis le `.blend` (`bpy.ops.wm.open_mainfile`) ou regarde le PNG produit avant de dire que c'est fait.
* **Messages** : `print()` du script va sur la sortie du Terminal ; les erreurs Python aussi.
* Montre le résultat à l'utilisateur : le chemin de l'image (elle s'ouvre dans Photos) ou du `.blend` (dans Blender).
