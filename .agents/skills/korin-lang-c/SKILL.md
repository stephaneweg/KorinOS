---
name: korin-lang-c
description: Écrire un programme ou une app en C pour Korin OS et le construire dans l'OS (gcc de /gcc, pibuild) ou sur le PC — dossier d'une app, app.txt, en-têtes et bibliothèques des kits, installation, vérification. À charger pour tout code C ; l'API de chaque kit est dans korin-kit-*, les limites POSIX dans korin-posix.
---

# Le C sous Korin

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Créer une app, app.txt, pibuild | `SD0:/apps/help.app/api/app_fr.md` | `src/apps/help/data/api/app_fr.md` |
| La libc et la couche POSIX | `…/api/libc_fr.md` | idem (**en retard sur `fork` et `exec`** : voir `korin-posix`) |
| Les kits | `…/api/<kit>_fr.md`, index `…/api/index_fr.md` | idem |
| GCC dans l'OS | — | `docs/GCC.md` |

## Outils dans Korin

- **GCC 14.2 :** `/gcc/bin/gcc`, `/gcc/bin/g++`, avec `make`, `ar`, `nm`, `objdump`, `strip`…
- **Rien à ajouter pour un programme seul :** `/gcc/bin/gcc -O2 x.c -o /tmp/x`. Le fichier de specs lie d'office le
  point d'entrée, `libposix` et newlib. Le résultat est un ELF **statique** ; `__unix__` et `__armpi__` sont
  définis.
- **Avec un kit :** `gcc -O2 -I/gcc/include/kits x.c -luikit -lnetkit … -o x`. Les en-têtes sont dans
  `/gcc/include/kits`, les relais dans `/gcc/lib/lib<kit>.a`, et chaque kit s'ouvre par `<kit>_kit_open()` (UIKit :
  `uikit_open(&ui)`).
- **`pibuild [app|bin|sys|svc] <dossier> <nom>`** compile tous les `.c` et `.cpp` du dossier (sauf `data/`) avec
  `-O2`, lie **tous** les kits, retire les symboles et installe :

  | Type | Installé dans |
  |---|---|
  | `app` | `/apps/<nom>.app/main` |
  | `bin` | `/bin/<nom>` |
  | `sys` | `SD0:/sys/<nom>` |
  | `svc` | `SD0:/sys/svc/<nom>` |

  Les objets restent dans `/tmp/pibuild/<nom>` : la fois suivante, seul ce qui a changé est recompilé. L'app
  **PiBuild** fait la même chose dans une fenêtre.
- **Le bit d'exécution :** un exécutable écrit hors de `SD0:/bin`, `SD0:/apps/*.app/main` et des dossiers de
  `SD0:/etc/exec.txt` demande un `chmod +x`.
- **Le temps :** sur le Pi, un `hello.c` se compile en 2 s quand les outils sont préchargés (`preload`,
  `SD0:/etc/preload.txt`).
- **Pas de** `cmake`, `ninja`, LTO, `-mcpu=native` ni `dlopen`.

## Le dossier d'une app

```
<projet>/                 le nom du dossier donne celui de l'app
  main.c                  (ou main.cpp, <nom>.c) ; tous les .c du dossier sont compilés
  app.txt                 le manifeste (voir template/app.txt)
  data/                   copié tel quel dans l'app (lang-en.txt, images…)
  help_fr.md, help_en.md  l'aide : la barre de menus ajoute alors un menu Aide
  icon.rgba               l'icône (sinon le lanceur en dessine une)
```

**Les clés d'`app.txt`** (`clé = valeur` ; `#` commente, et il est interdit dans une valeur) :
- `name` : le nom affiché.
- `category` : Accessoires, Bureautique, Création, Développement, Internet, Jeux, Multimédia, Système ou Réglages.
- `opens` : les extensions que l'app ouvre ; Fichiers lui passe le chemin en `argv`.
- Pour PocketQI : `description`, `keywords`, `help` (plusieurs lignes possibles), `llm.<action>` et `run.<action>`.

## Modèle

Le dossier `template/` est prêt à construire : `main.c` (fenêtre aux couleurs du thème, bouton, message) et
`app.txt`. L'API de la fenêtre est dans `korin-kit-uikit`.
1. `cp -r <ce dossier>/template SD0:/home/projets/bonjour`
2. `pibuild app SD0:/home/projets/bonjour bonjour`
3. `bonjour`

Un programme en ligne de commande n'est que `main` avec `printf` : `pibuild bin <dossier> <nom>` l'installe dans
`/bin`.

## Sur le PC (dépôt)

Les sources vont dans `src/apps/<nom>/`, puis on lance dans WSL :
1. `sh tools/gcc/kits.sh` (les relais des kits) ;
2. `sh tools/office/build.sh <nom>` : mêmes options que pibuild, avec `-mcpu=cortex-a72` ;
3. `.\build.ps1`, qui met `build/office/sdroot/apps/<nom>.app` sur la carte.

L'app doit aussi figurer dans la liste du script.

## Vérifier

- Lancez l'app : `bonjour`, ou `/apps/bonjour.app/main`.
- Regardez l'écran : `screenshot /tmp/ecran.bmp`.
- Lisez la sortie d'erreur, et `journal` en cas de plantage.
- Un plantage du programme n'arrête pas le système : corrigez, puis relancez `pibuild`.

## Pièges

- **OpenSSL n'est pas dans `/gcc` :** un programme TLS ne se construit que sur le PC (`tools/office/build.sh` ajoute
  `-lssl -lcrypto`).
- **Les arguments des kits sont des `long` :** sans cast `(long)`, un pointeur passé tel quel donne un avertissement.
- **Chemins :** `argv` peut contenir `SD0:/…`. Ne testez pas un chemin absolu avec `p[0] == '/'` seulement : un
  chemin qui contient `:` avant le premier `/` est absolu aussi.
