# QBasic

L'éditeur de BASIC d'Onyx (`user/Apps/qbasic`) sur UIKit, sans pont. Compilation : `pibuild app /src/apps/qbasic qbasic`.

* **Repris** : modules (programme découpé en SUB et FUNCTION), `split`, `compose`, vérification de syntaxe, `.bax`.
* **Réécrit** : fenêtre, menus, dialogues (`ui_*`) ; l'éditeur est `ui_textarea` avec `UTA_CODE | UTA_BASIC`, aux couleurs du thème,
  et non le bleu de QBasic. Les erreurs à l'exécution reviennent par un tube (`basic -i 3`), pas par IPC.
* **Autrement** : *Make App* écrit toujours un exécutable autonome (`/bin/basic` + le programme), car une app d'ARMPI est un
  exécutable. Pas d'icône d'app.
* **Pas essayé** : le vrai Pi ; Make App ; Make .bax ; glisser-déposer.
