# Le SDK de Jet (Korin PC)

Recompiler un fichier de WebKit et relier Jet **dans Korin**, avec le GCC du paquet `gcc`, sans reconstruire WebKit
(la construction complète se fait dans le cloud ou WSL : `tools/mesa/build-webkit.sh`, `tools/webkit/build-jet.sh`).
Fait par `tools/webkit/jet-sdk.sh` ; installé dans `/devtools/jet-sdk` par `tools/webkit/jet-sdk-get.sh`.
Version de Jet : `VERSION` (le SDK correspond à ce Jet publié, avec ses patchs de `tools/webkit/patches`).

```bash
# 1. modifier un fichier des sources de WebKit (patchées comme Jet)
#    /devtools/jet-sdk/webkit-gl/Source/WebCore/…/Fichier.cpp
# 2. recompiler son lot (quelques minutes ; plusieurs fichiers à la suite possible)
bash /devtools/jet-sdk/jetbuild WebCore/…/Fichier.cpp
# 3. relier Jet (en mémoire : /tmp/jet-main)
bash /devtools/jet-sdk/jetlink
# 4. l'essayer à la place de celui du paquet (garder l'original une fois)
sudo cp /apps/jet.app/main /apps/jet.app/main.orig
sudo cp /tmp/jet-main /apps/jet.app/main
```

- `commands.txt` : la commande de chaque objet (`objet<TAB>commande`, lancée depuis `webcore/`).
- Un `.h` changé : recompiler les lots qui l'incluent (WebKit en a beaucoup ; un en-tête très partagé demande une
  construction complète : la faire dans le cloud).
- Les archives de `webcore/lib` sont minces : elles lisent leurs objets à leur place (`webcore/Source/…/*.o`) ; un
  objet recompilé est pris tel quel, sans `ar`.
- Rendre le changement durable : un patch dans `tools/webkit/patches` (comme 0013, 0014), appliqué dans le cloud, Jet
  republié ; la copie de `/devtools/jet-sdk` n'est qu'un banc d'essai.
- Pièges : sous QEMU, écrire 170 Mo d'un coup sur le disque a dépassé le délai du service de fichiers (« Connection
  timed out ») : `jetlink` écrit dans `/tmp`. Compiler un lot de WebCore : ≈ 30 s natif, ≈ 11 min sous QEMU sans
  accélération ; prévoir ≈ 2 Go de mémoire, et plusieurs Go pour la liaison.
