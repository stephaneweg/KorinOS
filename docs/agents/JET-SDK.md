# Le SDK de Jet : recompiler un fichier de WebKit et relier Jet dans Korin

Pour un agent sous Korin (ZBook) qui veut essayer un changement de WebKit sans attendre la session du cloud. Le SDK
correspond à un Jet publié (`VERSION`, aujourd'hui **1.4.6**, patchs 0001 à 0014 de `tools/webkit/patches`).

## Installer

```bash
pkg add gcc                      # le GCC 14.2 de Korin (/devtools/gcc)
sudo bash tools/webkit/jet-sdk-get.sh 1.4.6
```

Il télécharge les parts `jet-sdk-1.4.6-<n>.zip` de la Release « packages » (≈ 230 Mo) et les décompresse dans
`/devtools/jet-sdk` (≈ 1,1 Go). Les parts restent dans `~/jet-sdk-dl` : les effacer ensuite.

## Le cycle

```bash
# 1. modifier les sources (patchées comme Jet) :
#    /devtools/jet-sdk/webkit-gl/Source/WebCore/page/Page.cpp, par exemple
# 2. recompiler le lot unifié qui contient ce fichier :
bash /devtools/jet-sdk/jetbuild WebCore/page/Page.cpp
# 3. relier Jet ; la sortie va dans /tmp/jet-main :
bash /devtools/jet-sdk/jetlink
# 4. l'essayer :
/tmp/jet-main https://…            # ou le copier sur /apps/jet.app/main (garder l'original)
```

- WebCore se compile par **lots unifiés** (`UnifiedSource-page-7.cpp` regroupe une dizaine de `.cpp`) : `jetbuild`
  trouve le lot d'un fichier et recompile ce lot (quelques minutes sur le ZBook).
- `commands.txt` : la commande de chaque objet, réécrite pour `/devtools/gcc/bin/g++`.
- Les archives de `webcore/lib` sont **minces** : elles lisent leurs objets à leur place, donc un objet recompilé
  est pris tel quel, sans `ar`.
- Un `.h` partagé : recompiler tous les lots qui l'incluent. Pour un en-tête très inclus, demander une construction
  dans le cloud.
- Pour que le changement dure : un patch dans `tools/webkit/patches` (format de 0014 ou 0015 : `diff -ru -U2 a b`
  sur `Source/…`), puis la session du cloud reconstruit et republie Jet. `/devtools/jet-sdk` n'est qu'un banc d'essai.

## Essais (22/11/2026)

- Un lot de WebCore compilé par le GCC de Korin sous QEMU (≈ 11 min sans accélération).
- `jetlink` dans Korin sous QEMU : liaison réussie en ≈ 8,5 min (8 Go de mémoire), et le Jet obtenu démarre. Sur le
  ZBook, compter quelques minutes.

## Pièges

- Prévoir ≈ 2 Go de mémoire pour un lot de WebCore, et plusieurs Go pour la liaison : Jet fait 133 Mo, 170 Mo avant
  `strip`.
- Sous QEMU, écrire les 170 Mo de la liaison sur le disque a dépassé le délai du service de fichiers
  (« Connection timed out ») : c'est pourquoi `jetlink` écrit dans `/tmp`.
- `build-jet.sh`, `build-webkit.sh` et les scripts de `tools/webkit/` ne tournent pas dans Korin : il leur faut la
  chaîne croisée, CMake, Ninja et apt. Ils documentent seulement comment le SDK a été construit.

## Construire le SDK (cloud ou WSL)

```bash
ARCH=x86_64 sh tools/mesa/build-webkit.sh
ARCH=x86_64 sh tools/webkit/build-jet.sh
ARCH=x86_64 sh tools/webkit/jet-sdk.sh          # -> build/pc/jet-sdk/jet-sdk-<version>-<n>.zip (< 90 Mo chacune)
```

Puis publier les parts comme les paquets depuis le cloud : une branche `release-upload/jet-sdk-<version>` qui porte
`upload/*.zip`, puis le workflow `release-upload.yml` (il prend les `.opk` et les `.zip`). Voir docs/PAQUETS.md.
