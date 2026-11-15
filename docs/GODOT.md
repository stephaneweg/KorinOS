# Godot sur Korin

Godot 4 porté sur Korin pour **créer des jeux qui ciblent Korin** (décision du 13/11/2026, mémoire
`armpi-idee-godot`), pas pour faire tourner des projets existants. Commencé le 14/11/2026 sur la branche
`claude/x86-64-pc`, pour le PC d'abord (Korin x86-64) ; le Pi viendra ensuite (même code, `arch=arm64`).

**Publié le 15/11/2026** (main 660741f) : paquets `godot` et `godot-templates` 4.7.2 du PC (archives dans la Release
packages) : `pkg add godot godot-templates` ou Logiciels.

**État (15/11/2026) : l'éditeur tourne sous QEMU** (Korin PC, GlKit en softpipe : pas de GPU sous QEMU), la démo 2D
aussi, à la souris ; l'export d'un jeu (PCK) et son lancement par le modèle d'export de Korin aussi. **Pas encore
essayé sur le ZBook ni sur le Pi**, ni l'export depuis Godot sous Windows.

## Choix

| Sujet | Choix |
|---|---|
| Version | Godot **4.7.2-stable** (commit `ed1daf0b`), sources hors du dépôt (`~/korin-godot/src`, `tools/godot/fetch.sh`) |
| Plateforme | `platform/korin`, gardée dans le dépôt (`tools/godot/platform/korin`) et copiée dans l'arbre de Godot par `build.sh` |
| Retouches des sources | une seule, `tools/godot/korin.patch` : `_S`, nom d'une variable de manifold (CSG), est une macro de `ctype.h` dans newlib |
| Rendu | **Compatibility** seulement : OpenGL ES 3.0 par armgl (GlKit et Mesa iris sur le PC, v3d sur le Pi). Pas de Vulkan (Forward+ et Mobile absents) |
| Chargement de GL | GLAD de Godot, par `eglGetProcAddress` (avec `EGL_STATIC`), que `korin_ui.c` fait passer à `armgl_proc` ; aucun autre appel EGL |
| Fenêtres | une seule fenêtre d'UIKit, cadre et couleurs du thème ; les fenêtres secondaires de l'éditeur (dialogues) sont dessinées dedans, comme Godot le fait sur le web et Android (`FEATURE_SUBWINDOWS` absent) |
| Boutons du cadre | le chrome de Korin dessine réduire, agrandir, fermer en haut à droite du contenu : `FEATURE_EXTEND_TO_TITLE` et `window_get_safe_title_margins` (`FRAME_CAP_W` + 8, `FRAME_CAP_H`) font que l'éditeur leur laisse la place (réglage `expand_to_title`, actif par défaut) |
| Physique 3D | Jolt, avec `JPH_PLATFORM_BSD` (Jolt ne connaît que des systèmes nommés ; celui des BSD convient à libposix) |
| Plein écran | une fenêtre sans cadre au-dessus de tout, comme SDL et EmuKit ; F9 bascule dans un jeu |
| Son | AudioKit : flux s16 stéréo 44,1 kHz (`audio_driver_korin.cpp`, comme le pilote de SDL) |
| Clavier | codes HID → touches de Godot (positions du QWERTY pour `physical_keycode`) ; la touche logique et le caractère suivent la disposition de HIDManager (AZERTY) |
| Souris | boutons, molette, double clic ; mode captif par `ui_pointer_lock` (déplacements relatifs) |
| Langages | GDScript ; C# (module `mono`) après .NET pour Godot, plus tard |
| Modules coupés | camera, mono, openxr, raycast (Embree), upnp, webrtc |

UIKit et armgl ne sont vus que de `korin_ui.c` (en C) : leurs en-têtes générés ne se mêlent pas à ceux de Godot.

## Fichiers

| Fichier | Rôle |
|---|---|
| `tools/godot/fetch.sh` | sources de Godot à la révision fixée |
| `tools/godot/build.sh` | l'éditeur (`build/pc/godot/sdroot/apps/godot.app`) ou un modèle d'export (`template_release`, `template_debug`) |
| `tools/godot/app.txt` | manifeste de l'app Godot (catégorie Développement) |
| `platform/korin/detect.py` | chaîne `x86_64-onyx-elf`, libposix, options et modules |
| `platform/korin/os_korin.*` | `OS_Korin` sur `OS_Unix` : chemins (`$HOME/.config`, `.local/share`, `.cache`), boucle principale, liens ouverts dans Jet |
| `platform/korin/display_server_korin.*` | fenêtre, GL, événements, curseur, presse-papiers |
| `platform/korin/audio_driver_korin.*` | AudioKit |
| `platform/korin/korin_ui.*` | UIKit et armgl en C |
| `tools/godot/korin.patch` | la seule retouche des sources de Godot (manifold) |
| `tools/godot/pctest.py` | essais sous QEMU q35 : `prep` (carte d'essai), `run` (commandes par telnet, captures, clics) |
| `tools/godot/test/` | `cli.gd` (sans affichage) et `demo/` (projet 2D) : `SD0:/home/godot` de la carte d'essai |

Ajouté à libposix pour Godot : `sys/timex.h` (basis_universal n'y prend que `gettimeofday`) et `sys/xattr.h` (les
attributs étendus : `ENOTSUP`, aucun volume de Korin n'en garde).

## Construire

Dans WSL, depuis la racine du dépôt, une fois la chaîne et les bibliothèques du PC faites
(`tools/pc/toolchain.sh`, `ARCH=x86_64 sh tools/posix/build.sh`, `sh tools/pc/apps.sh`, Mesa du PC :
`tools/mesa/fetch.sh`, `build-clc.sh`, `configure-iris.sh`, `build-iris.sh`) :

```sh
sh tools/godot/fetch.sh
sh tools/godot/build.sh                    # l'éditeur
sh tools/godot/build.sh template_release   # le modèle d'export
```

Paquets de WSL : `scons` (et pour Mesa : LLVM 21, clang, libclc, SPIR-V, voir `tools/mesa/build-clc.sh`).

L'éditeur : 34 min avec 6 tâches (16 cœurs, mais WSL n'a que 7 Go par défaut : avec 12 tâches et une autre
compilation en même temps, WSL a manqué de mémoire et s'est arrêté). Binaire de 133 Mo (sans symboles).

## Installer sur le ZBook

Après la suite d'installation habituelle (`tools/pc/sysroot.sh`, …, `sysroot_gcc.sh`) :
`sh tools/pc/sysroot_godot.sh` met l'éditeur dans `apps/godot.app` (avec `lib/glkit.so` s'il manque) et les modèles
d'export dans `apps/godot.app/templates`, puis `tools\pc\korin.ps1 install`. Pas `home/` : c'est SD1, que
`korin.ps1` ne touche jamais.

## Essais sous QEMU

```sh
python3 -I tools/godot/pctest.py prep          # dans WSL ; d'abord tools/pc/build.sh et tools/pc/sysroot.sh
python3 -I tools/godot/pctest.py run "@600 /sd0/apps/godot.app/main --headless -s /sd0/home/godot/cli.gd"
python3 -I tools/godot/pctest.py run --qemu "-accel kvm -cpu host -device usb-tablet,bus=xhci.0"     sleep:10 "@15 /sd0/apps/godot.app/main -e --path /sd0/home/godot/demo &" sleep:150 shot:editeur
```

* QEMU et OVMF de Linux (`qemu-system-x86`, `ovmf`). L'OVMF d'Ubuntu ne lance pas le shell UEFI (`startup.nsh` de la
  carte) : `pctest.py prep` met le chargeur aussi en `EFI/BOOT/BOOTX64.EFI` de la partition KORIN OS.
* **KVM** (`-accel kvm -cpu host`) : sans lui, QEMU émule le processeur et Mesa en softpipe devient inutilisable.
  `/dev/kvm` est au groupe `kvm` : l'essai a été lancé par `wsl -u root` (l'utilisateur de WSL n'est pas dans ce groupe).
* Le shell de Korin ne rend pas la main sur `&` (`gltest --win &` pareil) : après le lancement d'une app graphique,
  la session telnet attend ; `pctest.py` saute alors les commandes suivantes et continue les captures et les clics
  (QMP).

| Essai, 15/11/2026, QEMU q35, 4 cœurs, KVM | Résultat |
|---|---|
| `--headless -s cli.gd` | GDScript, fichiers (`user://` = `SD0:/.local/share/godot/app_userdata/…`), threads, heure : OK, 12,5 s (sans KVM) |
| démo 2D (`--path demo`) | fenêtre de Korin au cadre du thème, titre « Demo Korin (DEBUG) », carré qui tourne, cercle qui suit la souris : **9 à 12 images/s** (softpipe) |
| export (`--headless --export-pack Linux`) puis `demo.x86_64` (modèle d'export) | PCK de 3,4 Ko en 20 à 24 s ; le jeu exporté tourne, 14 images/s |
| éditeur (`-e --path demo`) | s'ouvre en moins de 60 s : arbre de scène, vue 3D (grille, axes, ciel), inspecteur, système de fichiers, onglets 2D / 3D / Script / Game |

## Exporter un jeu pour Korin PC depuis Godot sous Windows

Demande de l'utilisateur (14/11) : faire le jeu sous Windows, l'exporter pour Korin PC.

1. **Premier temps, sans greffon.** Korin PC et Linux x86-64 lisent le même PCK : dans Godot sous Windows, un
   préréglage d'export **Linux** (x86_64), avec **Modèle personnalisé** = `template_release.x86_64` de Korin
   (`build/pc/godot/templates`), rendu Compatibility. L'export donne le binaire et le `.pck` (ou le PCK dans le
   binaire, section `pck` comme sur Linux). Sur Korin : lancer le binaire ; il trouve le `.pck` de même nom à côté.
   Sur la carte FAT, le binaire copié n'est pas exécutable : `chmod +x jeu.x86_64` une fois (vu sous QEMU :
   `sh: erreur 1` sans).
   **Essayé sous QEMU le 15/11** avec l'éditeur de Korin à la place de celui de Windows (même PCK) :
   `godot --headless --path demo --export-pack Linux demo.pck` (préréglage `tools/godot/test/demo/export_presets.cfg`)
   en 20 à 24 s, puis `demo.x86_64` (= `template_release.x86_64`, 66 Mo) trouve `demo.pck` à côté de lui : fenêtre
   « Demo Korin », 14 images/s (softpipe).
2. **Ensuite** : un greffon d'éditeur (EditorExportPlugin) qui fait directement une app Korin
   (`jeu.app/main`, `app.txt`, icône) et l'archive pour la copier sur la carte.

## Reste

* **Sur le ZBook** (iris, le vrai GPU) : l'éditeur, la démo, un jeu 3D ; la vitesse.
* L'export depuis Godot sous Windows (préréglage Linux, modèle personnalisé de Korin), puis le greffon qui fait une app
  Korin (`jeu.app`).
* Clavier dans l'éditeur (AZERTY, raccourcis Ctrl), molette, redimensionnement, plein écran : pas encore essayés.
* Manettes (WEV_PAD → `Input::joy_*`), dépôt de fichiers (WEV_DROP), icône de la fenêtre, IME.
* Le Pi (`arch=arm64`, Mesa v3d liée, GL ES 3.0 tout juste).

## Retours du ZBook (15/11/2026)

L'utilisateur a installé les paquets : l'éditeur démarre. Deux défauts, vus dans `SD0:/journal.txt` :

* **Fermeture après la conversion d'un projet** (Starter Kit Racing de Kenney, fait pour 4.6 et Forward+) : le
  gestionnaire de projets lance l'éditeur dans un nouveau process, par le chemin de l'exécutable ; lancé du bureau,
  `argv[0]` vaut `godot` et la plateforme en faisait `/sd0/godot` (« Could not create child process »). Corrigé :
  un nom seul est cherché dans `/sd0/apps/<nom>.app/main`, puis dans le PATH.
* **Projet Forward+ ou Mobile** : le pilote demandé est `vulkan`, inconnu ici, et Godot abandonnait. La plateforme
  l'accepte et passe en Compatibility (comme Linux sans Vulkan). Essayé sous QEMU avec le Racing Kit : il s'ouvre
  et importe ses ressources.
* **Lenteur de l'interface** : le GPU n'était pas servi. Après un redémarrage à chaud depuis Windows, la GGTT garde
  des entrées « présentes » aux bits 39..63 remplis ; `intelgpu` ne trouvait plus de fenêtre libre et s'arrêtait
  (« pas de fenêtre libre de 4096 entrées »), GlKit passait en softpipe. Corrigé dans `src/svc/intelgpu/intelgpu.c`
  (ces entrées comptent libres). Pas encore essayé sur le ZBook.

Publié : pc 2026.11.6 (seul `intelgpu` changé), godot et godot-templates 4.7.2-1 (main c82601b).

### L'éditeur relancé par le gestionnaire de projets (15/11, suite)

Après les correctifs ci-dessus, le journal du ZBook a dit (lignes `REFUS` d'intelgpu) : « contexte du client 48 sur
l'espace 1, qui n'est pas à lui (propriétaire 47) ». C'est un défaut d'iris (Mesa 26.2, `i915/iris_batch.c`,
`iris_hw_context_set_vm_id`) : il passe à `SETPARAM VM` le booléen `iris_bufmgr_use_global_vm_id()`, soit 1, au lieu
de l'espace. Sous Linux, le premier espace d'un fd vaut 1 et rien ne se voit ; les numéros d'intelgpu sont communs
au système : seul le premier programme GL tombait juste. Le faux libdrm (`tools/mesa/libdrm-pc/korin_i915.c`)
n'accepte plus pour un contexte qu'un espace créé par ce process. Reproduit puis corrigé sous QEMU (`intelgpu start
simu`, `tools/godot/test/spawn` : un Godot qui en lance un autre). Publié : pc 2026.11.9 (main db6ada5).
