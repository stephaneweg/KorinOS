---
name: korin-godot
description: Faire un jeu avec Godot 4 pour Korin PC - le projet (GDScript ; rendu Compatibility, ou Forward+ et Mobile par Vulkan), l'éditeur de Korin, l'export en app de Korin par le greffon Korin Export (préréglage Linux x86_64, modèle d'export de Korin), l'export sans fenêtre en ligne de commande, l'export depuis Godot sous Windows, les limites. À utiliser dès qu'on parle de Godot, d'un jeu Godot, d'un projet .godot, d'un PCK ou d'un export de jeu pour Korin.
user-invocable: true
---

# Faire un jeu Godot pour Korin

Godot **4.7.2** est porté sur Korin pour **créer des jeux qui ciblent Korin**. Sur le **PC** (x86-64) seulement
pour l'instant : pas sur le Pi. Ce que Korin a :

* l'éditeur : `/apps/godot.app/main` (paquet `godot`) ;
* les modèles d'export de Korin : `/apps/godot.app/templates/template_release.x86_64` et `template_debug.x86_64`
  (paquet `godot-templates`) ;
* le greffon **Korin Export** : `/apps/godot.app/addons/korin_export` (dans le paquet `godot`).

Vérifier d'abord : `ls /apps/godot.app/main /apps/godot.app/templates`. S'il en manque, demander à l'utilisateur
avant d'installer (`pkg add godot godot-templates`, compétence `korin-sys-packages`).

## Ce qui marche, ce qui ne marche pas

* **Les trois rendus** (depuis `pc` 2026.11.29 et `godot` 4.7.2-2.2) :
  * **Compatibility** (OpenGL ES 3.0, par GlKit) : marche partout, sous QEMU compris (en logiciel) ; le plus
    léger sur le GPU Intel du PC. Le choix sûr pour un jeu simple ou 2D.
  * **Forward+** et **Mobile** (Vulkan, par VkKit : `/lib/vkkit.so`, le pilote anv de Mesa sur le GPU Intel) :
    l'éclairage et les effets complets (SDFGI, volumétrie, calcul...). Plus lourds sur un petit GPU : Mobile est
    souvent plus rapide que Forward+ sur le PC.
  * Sans VkKit ou sans GPU (QEMU, `vkkit.so` absent), un projet Forward+ ou Mobile **passe en Compatibility** à
    l'ouverture, avec un avertissement : il marche, avec un aspect plus simple.
  * Vulkan sur Korin est **tout neuf** : s'il se passe quelque chose d'anormal en Forward+ ou Mobile (écran
    noir, fermeture), essayer en Compatibility et le dire à l'utilisateur. Diagnostic : `vktest` et
    `vktest --win` (paquet `vktest`), puis les lignes `korin_i915` du journal.
  * Choisir le rendu dans `project.godot` (§ 1) ; en ligne de commande pour un essai :
    `--rendering-method gl_compatibility` (ou `forward_plus`, `mobile`).
* **GDScript.** Pas de C# dans Godot (module mono absent) ; pas de caméra, d'OpenXR, d'UPnP, de WebRTC.
* Physique 3D Jolt, son par AudioKit, souris (mode captif compris), clavier (AZERTY suivi). Manettes et dépôt de
  fichiers : pas encore.
* Une seule fenêtre : les dialogues de l'éditeur sont dessinés dedans. F9 bascule un jeu en plein écran.
* `user://` est sous `HOME:/.local/share/godot/app_userdata/<nom du projet>/`.

## 1. Le projet

Un dossier dans le dossier personnel (`HOME:/` ; pour un programme POSIX comme Godot, `$HOME`, soit `/home`), par
exemple `/home/monjeu`. Le minimum :

`project.godot` :

```ini
config_version=5

[application]

config/name="Mon jeu"
run/main_scene="res://main.tscn"
config/features=PackedStringArray("4.7", "GL Compatibility")
config/icon="res://icon.svg"

[editor_plugins]

enabled=PackedStringArray("res://addons/korin_export/plugin.cfg")

[rendering]

renderer/rendering_method="gl_compatibility"
renderer/rendering_method.mobile="gl_compatibility"
```

Pour Vulkan : `config/features=PackedStringArray("4.7", "Forward Plus")` et
`renderer/rendering_method="forward_plus"` (ou `"Mobile"` et `"mobile"`) ; sans la section `[rendering]`, Godot
prend Forward+. Demander à l'utilisateur quel rendu il veut si ce n'est pas clair ; sinon Compatibility pour un
jeu 2D ou simple, Forward+ pour un jeu 3D qui veut de beaux éclairages.

puis une scène `main.tscn` et ses scripts `.gd` (format texte de Godot 4 : `[gd_scene load_steps=2 format=3]`,
`[ext_resource type="Script" path="res://main.gd" id="1"]`, `[node name="Main" type="Node2D"]`,
`script = ExtResource("1")`). Pour le greffon, le copier dans le projet :

```sh
mkdir -p /home/monjeu/addons && cp -r /apps/godot.app/addons/korin_export /home/monjeu/addons/
```

Essayer un script sans fenêtre (rend la main, messages dans le terminal) :

```sh
/apps/godot.app/main --headless --path /home/monjeu -s res://essai.gd     # essai.gd : extends SceneTree ... quit()
```

Lancer le jeu ou l'éditeur dans une fenêtre, **sans bloquer le shell** (le shell de Korin ne rend pas la main sur
un simple `&` après une app graphique) :

```sh
(/apps/godot.app/main --path /home/monjeu >/dev/null 2>&1 &)        # le jeu
(/apps/godot.app/main -e --path /home/monjeu >/dev/null 2>&1 &)     # l'éditeur
```

Les erreurs de script d'un jeu lancé ainsi ne se voient pas : essayer d'abord avec `--headless` et
`--quit-after 300` (300 images, puis sortie) pour lire les erreurs.

## 2. Le préréglage d'export

`export_presets.cfg` à la racine du projet : un préréglage **Linux** (Korin PC lit les jeux Linux x86-64), avec le
**modèle personnalisé** de Korin, et les options du greffon :

```ini
[preset.0]

name="Linux"
platform="Linux"
runnable=true
dedicated_server=false
custom_features=""
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path="../export/monjeu.x86_64"

[preset.0.options]

custom_template/debug="/apps/godot.app/templates/template_debug.x86_64"
custom_template/release="/apps/godot.app/templates/template_release.x86_64"
debug/export_console_wrapper=0
binary_format/embed_pck=true
texture_format/s3tc_bptc=true
texture_format/etc2_astc=false
binary_format/architecture="x86_64"
korin/make_app=true
korin/app_id="monjeu"
korin/category="Jeux"
korin/description="Une phrase qui dit ce qu'est le jeu."
korin/zip=true
korin/install_dir=""
```

Les options `korin/*` (greffon Korin Export) :

| Option | Rôle |
|---|---|
| `make_app` | faire l'app de Korin après l'export |
| `app_id` | nom du dossier `<id>.app` : minuscules et chiffres ; vide : d'après le nom du projet |
| `category` | catégorie du lanceur (`Jeux`) |
| `description` | la phrase du lanceur ; vide : celle du projet |
| `zip` | aussi l'archive `<id>.app.zip` |
| `install_dir` | un dossier où copier l'app après l'export (`/apps` pour l'installer ; **demander à l'utilisateur**) |

`binary_format/embed_pck=true` : un seul fichier (`main`), le conseil.

## 3. Exporter

```sh
cd /home/monjeu && /apps/godot.app/main --headless --path . --export-release Linux
```

(`--export-debug Linux` pour le modèle de débogage.) Environ une minute. Godot importe d'abord les ressources
(dossier `.godot/`) ; si l'export s'en plaint, lancer avant `/apps/godot.app/main --headless --path . --import`.

Le greffon fait, à côté du fichier exporté (`../export/`) :

```
monjeu.app/
  main        le modèle d'export de Korin, avec le jeu (PCK intégré)
  main.pck    seulement si le PCK n'est pas intégré
  app.txt     le manifeste (nom, catégorie, description)
  icon.rgba   l'icône du projet aux tailles du bureau
monjeu.app.zip
```

Vérifier les lignes `Korin : app faite : ...` dans la sortie. Installer : copier `monjeu.app` dans `/apps`
(**après accord de l'utilisateur**, c'est un dossier du système), ou `install_dir="/apps"`. Dans `/apps`, `main` est
exécutable d'office ; le jeu apparaît dans le lanceur. Lancer : `(/apps/monjeu.app/main >/dev/null 2>&1 &)`.

Sans le greffon : l'export donne `monjeu.x86_64` (et `monjeu.pck` s'il n'est pas intégré). Hors de `/apps`, il faut
`chmod +x monjeu.x86_64` une fois (sinon `sh: erreur 1`).

## 4. Depuis Godot sous Windows (pour l'utilisateur)

Même chose dans l'éditeur de Windows : copier `korin_export` dans `res://addons/`, l'activer (Projet > Paramètres du
projet > Extensions), préréglage d'export **Linux** x86_64, **Modèle personnalisé** = les deux modèles de Korin (à
copier de `/apps/godot.app/templates` du PC Korin, par le partage SMB par exemple), rendu Compatibility, options
« Korin » comme ci-dessus ; `install_dir` peut être le partage SMB de Korin (`\\<adresse>\...\apps`). Le rendu
choisi sous Windows (Compatibility, Forward+, Mobile) vaut aussi sur Korin. Cette voie n'a pas encore été essayée
depuis Windows : le dire.

## Pièges

* Un projet Forward+ ou Mobile sur un Korin sans VkKit (ou sous QEMU) passe en Compatibility : l'aspect change
  (pas de SDFGI, de volumétrie...). Pour un jeu qui doit tourner partout, viser Compatibility.
* Le binaire exporté fait environ 75 Mo (le moteur, Vulkan compris) : c'est normal.
* Sous QEMU (pas de GPU), Godot rend en logiciel, en Compatibility : 10 à 15 images/s. Sur le PC réel (GPU
  Intel), Compatibility est fluide ; Forward+ et Mobile dépendent de la scène.
* Ne pas écrire dans `/apps/godot.app` (fichiers du paquet).
