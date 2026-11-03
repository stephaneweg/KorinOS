# Koton sur ARMPI

Koton, le studio d'Onyx (`user/Apps/koton`), en C++, hors du selfhost, comme Jet. Binaire de 780 Ko.

    sh tools/webkit/build-koton.sh     # (WSL) -> $WK_OUT/out/koton.app/{main,app.txt,drums.json,demo.kson,plugins/}, kptest
    sh tools/webkit/stage.sh           # -> build/webkit/sdroot (apps/koton.app, home/koton/songs/demo.kson)
    .\build.ps1                        # recopie sur la carte

La SoundFont se pose à la main : `SD0:/res/soundfonts/GeneralUser-GS.sf2` (31 Mo, elle ne va pas dans git).
Sans `.sf2`, Koton joue sur la banque General MIDI en FM d'AudioKit. Chansons et réglages :
`SD0:/home/koton/` (`songs/`, `settings.json`). Koton est dans le tiroir Multimédia du dock, ou
`SD0:/apps/koton.app/main [chanson.kson]`.

## Ce qui est repris d'Onyx

* `engine/`, `ui/*.h`, `main.cpp` : le moteur, les générateurs, l'interface, le mode pocket, sauf les chemins
  (`SD:/koton` -> `SD0:/home/koton`) et les appels. Thème : `ui/palette.h`.
* `ui/json.hpp`, la géométrie de `ui/vpath.cpp` (`VPath`) : tels quels.
* `plugins/<id>/` : les 11 greffons d'Onyx (`user/Apps/kp_<id>`), `main.cpp` et `plugin.json` tels quels, sauf les `#include`.
  `plug/plugshm.h`, `plug/plugctx.*`, la logique de `plug/plughost.cpp` et le cœur de `plug/kplug.h` aussi.
* Licences : MIT d'Onyx.

**Aucun pont** (règle de tous les ports) : Koton appelle directement les kits (`ui_*`, `ak_*`) et POSIX. Rien de l'uikit
C++ d'Onyx, ni `kapi_*`, ni noms de compatibilité ; ce qui manque s'ajoute au kit.

## Ce qui est écrit pour ARMPI

* `lang/fr.txt` : les mots de Koton en français, fusionnés dans `SD0:/res/lang/fr.txt` (`tools/lang/merge.py`). Les listes déroulantes passent par `joinLabels`, qui traduit ; les tables du moteur restent en anglais.
* `ui/view.h`, `ui/view.cpp` : la fenêtre de Koton. UIKit n'a pas de vue dessinée à la main : une `View` dessine par les
  `ui_*` dans un `ui_canvas` caché, recopié dans la fenêtre sous les contrôles, et reçoit souris et touches. **Un contrôle
  est une poignée de UIKit, sans classe** : le code appelle `ui_pushbutton`, `ui_dropdown`..., le donne à sa vue (`addCtl` :
  sa place, son tag), le lit par `ui_w_get_value`, et ses changements arrivent dans `View::onControl`. Les dialogues : une
  `Dialog` ouvre sa fenêtre (`ui_dialog_begin`) dès sa construction et la garde jusqu'à sa destruction, pour que ses
  contrôles se lisent après `run`. UIKit ne découpe pas un widget par son parent : `view.cpp` raccourcit ou cache ceux qui
  dépassent.
* `ui/sys.h` : l'horloge et les fichiers entiers, sur POSIX. `ui/palette.h` : les petits dessins de Koton (texte centré,
  fondus, barres de défilement).
* `engine/audioutil.cpp` : ce qu'AudioKit ne fait pas (limiteur doux, WAV, recherche de la SoundFont). Le moteur appelle
  directement `ak_sf_*` et `ak_synth_*`, sans synthé à lui. `ui/ai_dialog.h` : le stub ci-dessous.
* `ui/audio.h` : un thread pthread de haute priorité rend des blocs de 256 trames et les écrit par `ak_write`,
  35 ms d'avance. La SoundFont est lue par un autre thread : la fenêtre vient tout de suite, le son ensuite.
* `ui/chrome.h` : la barre du haut, faite pour une grande fenêtre, se resserre sous 1400 px (position plus
  étroite, puces sans leurs mots, plus de bouton d'IA ; sous 900 px, plus de mesure ni de swing). La charge du DSP
  est dans la barre d'état.

## Stubs et différences avec Onyx

* **IA** (`ui/ai_dialog.h`) : le menu et les entrées restent et disent « non disponible » (il faudrait `SD:/bin/llm`) ;
  `engine/ai*.cpp` et le dialogue d'Onyx ne sont pas repris. « Get a SoundFont » aussi.
* **Greffons** : voir plus bas.
* **MIDI matériel** : pas lu (ARMPI n'a pas d'entrée MIDI) ; `midiNote` des éditeurs reste, sans appelant.
* **Synthèse** : celle du kit, un synthé par piste comme Onyx (16 canaux, Koton n'utilise que 0 et 9). Un glissando est un `note on`
  simple : les glissandos des lignes mélodiques sont perdus. Réverbération et chorus par synthé.
  Le niveau est celui du kit (`AK_LEVEL`), pas recalé sur MeltySynth.
* **Fenêtre** : Koton s'ouvre agrandi par Elegant (`ui_maximize`) ; le bouton du cadre le rend à 85 % de la zone libre.
* **Thème** : celui du bureau, pour tous les styles : les contrôles sont ceux de UIKit, les vues prennent les jetons
  (`ui_jetons`) ; `theme.txt` n'est jamais lu par Koton. Il est pris au lancement.
* **Texte** : les polices de UIKit (DejaVu), plus FreeType.
* **Menus contextuels** : une entrée inactive n'est pas montrée, et il n'y a pas de séparateur (`ui_popup_local` sait griser une entrée, octet 0x01 en tête : Koton ne s'en sert pas).
* **Fichiers** : les motifs du premier genre seulement (`ui_file_filter`), sans liste de genres dans le dialogue.
* **Souris** : Elegant ne donne pas les modificateurs avec `WEV_MOUSE` ; Ctrl ou Maj + clic ne sont vus que si une touche
  vient d'arriver avec eux.
* **Une seule fenêtre** : pas de zone d'état ; l'éditeur d'un greffon flotte dans la fenêtre de Koton. `Open FM Tracker` (carte pocket) ne lance rien.
* **Mode pocket** : la logique d'Onyx (`SW < 800`) ; `uk_win_server` le dit en dessous de 800 px de large.

## Les greffons

Un greffon (instrument, effet, générateur) est un **process** à part, comme chez Onyx : chacun garde son état dans ses
statiques, deux instances d'un même effet ne peuvent donc pas partager un process. Ils sont dans
`SD0:/apps/koton.app/plugins/<id>/{main,plugin.json}` et non dans `SD:/koton/plugins` : ils viennent avec l'app, dans son
paquet, sans rien à déclarer dans `packages.ini`, et le tiroir ne montre que les `apps/<nom>.app`. Le moteur est l'hôte
d'Onyx (`plug/plughost.cpp`, protocole `plug/kplug_proto.h`, côté greffon `plug/kplug.h`), ses appels réécrits sur POSIX
(libposix), sans couche `kapi_*` :

| Onyx | ARMPI |
| --- | --- |
| surface partagée (`kapi_surface_create`) | `memfd_create` + `mmap (MAP_SHARED)`, héritée par son numéro |
| `kapi_exec_as` (nom `kp.<id>.<n>`) | `posix_spawn`, tous les descripteurs de Koton en `CLOEXEC` sauf les deux du greffon (`adddup2 (fd, fd)`) |
| boîte aux lettres, service `kotonplug` | `socketpair (AF_UNIX, SOCK_SEQPACKET)` par instance ; fin de canal = l'autre est mort |
| liste des process | `waitpid (WNOHANG)`, `kill (SIGKILL)` |
| `kapi_wait_word` sur `kick` | un sommeil de 3 ms du fil de rendu (20 ms non connecté) : le futex d'ARMPI ne traverse pas les process ; `lead` = 93 ms laisse largement la place |
| `kapi_thread_priority` | `pthread` `SCHED_FIFO` (la haute priorité de libposix) |

**L'éditeur diffère d'Onyx** : là, le greffon dessine ses boutons avec l'uikit C++ dans une surface partagée. Ici, un
greffon n'a pas d'interface (POSIX seul, 180 Ko) ; `PlugEditorView` est une `kui::View` de Koton faite de ses paramètres, avec
les contrôles de UIKit (`ui_knob`, `ui_dropdown`, `ui_checkbox`), comme le faisait `kp__autoEditor`. Aucun des 11 greffons d'Onyx
n'a d'éditeur à lui. `KpDesc::editor` n'existe donc plus ; un greffon à éditeur propre demanderait de le refaire.

`View::floating` : ses contrôles de UIKit étant dessinés et cliqués par le kit, ceux qui passent sous le panneau flottant sont
cachés, et le panneau revient au-dessus de ce qui se repeint dessous.

`SD0:/bin/kptest` (`plug/kptest.cpp`, paquet `system-tests`) : le vrai `PlugHost`, les vrais process, et le rôle du moteur tenu à la
main par `ShmSource` / `ShmEffect` : chaque greffon, deux instances d'un effet, un process tué, un hôte mort.
