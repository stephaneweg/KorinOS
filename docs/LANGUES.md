# Les langues

La langue vaut pour tout le système : `lang = fr` ou `en` dans `SD0:/etc/system.ini`, choisie dans Réglages (Région et
langue) et au premier démarrage. Sans clé : `fr`. Un programme lit la langue une fois, à son premier texte traduit : un
changement vaut pour les programmes lancés ensuite.

| Où | Quoi |
|---|---|
| `src/kits/appkit/lang.inc` | `lang_tr`, `lang_puts`, `lang_get`, `lang_load` : la langue, les dictionnaires, une table de hachage |
| UIKit | `ui_tr`, `ui_tr_load`, `ui_lang` passent par AppKit ; boutons, cases, listes, onglets, menus, titres et messages traduisent eux-mêmes |
| `src/inc/tool.inc` | `t_dire` et `t_derr` : la sortie traduite d'une commande de `/bin`. `t_puts` reste brut : il sert aussi aux données |
| `src/inc/tr.inc` | `tr_x0` à `tr_x5` : traduire un registre au milieu d'un appel |
| `tools/lang/extract.py` | le catalogue des textes d'un programme |
| `tools/lang/merge.py` | appelé par `build.ps1` : assemble `SD0:/res/lang/<xx>.txt` |

## Les dictionnaires

`SD0:/res/lang/<xx>.txt` est fait de `skel/sd0/res/lang/<xx>.txt` et de tous les `src/<catégorie>/<nom>/lang/<xx>.txt`. Une
app peut aussi charger le sien (`ui_tr_load`), comme les ports d'Onyx avec `data/lang-fr.txt`.

Une ligne : `texte source = traduction`, `#` pour un commentaire.

* Le premier `=` non échappé sépare les deux. Dans le texte : `\n`, `\=`, `\\`.
* Les espaces autour du `=` sont retirées. Pour en garder une : `Score : \ = Score: ` en fin de clé, `\ ` en tête.
* Le dictionnaire est global : une clé n'a qu'un sens. Là où le même mot français a deux sens, le source en emploie deux
  (« Annuler » pour Cancel, « Défaire » pour Undo).

Le source est en français pour ce qui est en assembleur et ce qui a été écrit ici, en anglais pour les ports d'Onyx :
`en.txt` traduit les premiers, `fr.txt` les seconds.

## Ce qui ne se traduit pas

Ce qui est écrit dans les fichiers, les noms de formats et de clés, les journaux de diagnostic, les noms propres (Givre,
Onyx Noir), les fonctions du tableur.
