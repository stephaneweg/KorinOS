# Python et Vibe sous Korin : reprendre le chantier (étapes 7 et suivantes)

Point de reprise écrit le 12/11/2026 (mis à jour après l'étape 6, le même jour), pour une session qui continue le
portage **sans le contexte des sessions précédentes** (une session cloud, par exemple). Il dit où en est le chantier, comment tout reconstruire depuis un
dépôt vierge, et ce qu'il reste à faire, étape par étape. Le détail est dans les documents cités ; ce fichier n'en
recopie que l'essentiel.

| Document | Contenu |
|---|---|
| [PYTHON.md](PYTHON.md) | l'étude : architecture de Vibe (§1), construction de CPython (§2), `dlopen` (§2.5 bis), cible Rust (§3), Vibe sur Korin (§4), façade ACP (§5), Python langage de Korin (§6), risques (§8), **étapes et essais d'acceptation (§9)**, décisions (§10) |
| [PYTHON-PORT.md](PYTHON-PORT.md) | ce qui est fait : étapes 0, 1, 2, 3, 5 et 6, construction, résultats, ce qui reste |
| [RUST.md](RUST.md) | étape 4 : la cible `aarch64-unknown-korin`, `cargo korin`, caisses patchées, limites |
| [POSIX.md](POSIX.md) | libposix, dont la section « dlopen » et les ajouts pour CPython |
| [NODEJS.md](NODEJS.md) §6.3, [POSIX.md](POSIX.md) | l'**arbre POSIX des chemins** (`/sd0`, `/sd1`, `/ram0`), commun avec Node : fait à l'étape 6, section « L'arbre POSIX des chemins » de POSIX.md |
| [CONSTRUIRE.md](CONSTRUIRE.md) | toutes les sources tierces, leurs versions et la construction du système |
| [PAQUETS.md](PAQUETS.md) | `pkg`, le dépôt de paquets, la publication (étape 10) |

## 1. Où en est le chantier

Les étapes 0 à 6 sont dans `main` (poussé sur GitHub, `f1e632ec` pour l'étape 6). **Tout a été essayé sous QEMU,
rien sur le Pi.**

| Étape (§9) | État | Où |
|---|---|---|
| 0 Vibe 3.1.2 sans V8, sur le PC | **fait** : séance ACP complète avec le compte Mistral (outil, permission, MCP) | `tools/vibe/`, PYTHON-PORT.md « Étape 0 » |
| 1, 2 CPython 3.14.10 | **fait** : OpenSSL, zlib, SQLite, expat, libffi, libmpdec, bibliothèque en zip, `python3` hôte de `dlopen` | `tools/python/build.sh`, PYTHON-PORT.md |
| 2 bis `dlopen` | **fait** (libposix) | POSIX.md « dlopen » |
| 3 modules C en `.so`, pip | **fait** : `tree_sitter`, `tree_sitter_bash`, `_cffi_backend`, pip, un paquet C compilé sur la carte | `tools/python/build-ext.sh`, `site-packages.sh` |
| 4 cible Rust | **fait** : Rust 1.97.1, `-Zbuild-std`, `std.patch`, `libc.patch`, `cdylib` par `dlopen` | `tools/rust/`, RUST.md |
| 5 modules Rust de Vibe | **fait** : `pydantic-core` 2.46.4, `rpds-py` 0.30.0, `cryptography` 50.0.0, une `cdylib` chacun | `tools/python/build-rust-ext.sh`, PYTHON-PORT.md « Étape 5 » |
| 6 harness, `site-packages` de Vibe, arbre POSIX | **fait** : `_native` en `cdylib` (9,3 Mo, TLS 0x118), 147 Mo de `site-packages`, `vibe-acp` et `vibe-app-server` répondent à `initialize` sur la carte, `getcwd` → `/sd0/...` | `tools/vibe/build-korin.sh`, PYTHON-PORT.md « Étape 6 », POSIX.md « L'arbre POSIX des chemins » |
| **7** Vibe de bout en bout sur la carte | **à faire** | §3 plus bas |
| 8 `libacp` et `agent` (client terminal en C) | à faire | §4 |
| 9 l'app UIKit | à faire | §4 |
| 10 paquets `python`, `vibe` | à faire | §4 |
| 11 vitesse | à faire | §4 |
| 12 à 15 (module `korin`, Pillow, numpy, artefacts) | à faire, indépendantes de Vibe | PYTHON.md §6 et §9 |

## 2. Reconstruire depuis un dépôt vierge

Toutes les sources tierces se téléchargent **hors du dépôt** (rien de lourd n'y est commité). Les scripts ont été
écrits et essayés dans **WSL (Ubuntu)** ; sur un autre Linux, ils devraient marcher tels quels, mais **c'est à vérifier
en premier**.

### 2.1 Ce qu'il faut sur la machine

| Outil | Pour | Remarque |
|---|---|---|
| la chaîne `aarch64-onyx-elf` 14.2 dans `/opt/toolchains/aarch64-onyx-elf-14.2` | tout le C, CPython, l'éditeur de liens de Rust | `sh tools/extra/fetch.sh toolchain` (dépôt public `stephaneweg/onyx-toolchain`, `SHA256SUMS` vérifiés) ; `/opt/toolchains` doit être inscriptible : `sudo mkdir -p /opt/toolchains && sudo chown $USER /opt/toolchains` |
| `python3.14` (3.14.x) | Python de construction de CPython, `build-pc.sh` | CPython croisé exige le **même** 3.14 ; Ubuntu 24.04 ne l'a pas d'office (deadsnakes, ou le construire) |
| `curl`, `patch`, `git`, `make`, `xz`, `pkg-config` | les scripts | |
| rustup | `tools/rust/build.sh` l'installe lui-même dans `~/.cache/korin-rust` | Rust **1.97.1** (celle du harness de Vibe), `RUSTC_BOOTSTRAP=1`, pas de nightly |
| `uv` (`~/.local/bin/uv`) | `tools/vibe/build-pc.sh` (le venv de Vibe depuis `uv.lock`), `tools/vibe/build-korin.sh` (la liste des paquets du verrou) | |
| QEMU ≥ 9.0 (`qemu-system-aarch64`, machine `raspi4b`) | les essais | chemin dans `ARMPI_QEMU` (par défaut le chemin Windows, `tools/qemu_drive.py:32`) |

### 2.2 Dans l'ordre

```bash
sh tools/extra/fetch.sh toolchain            # la chaîne
SANS_WEBKIT=1 sh tools/extra/fetch.sh        # src/extra (zlib, SQLite... ; sans WebKit, 500 Mo de moins)
sh tools/posix/build.sh                      # libposix (build/posix)
sh tools/openssl/build.sh                    # build/openssl (build.sh de Python le lance s'il manque)
sh tools/python/build.sh                     # CPython : ≈ 8 min sur 16 cœurs ; build/python/sdroot
bash tools/rust/build.sh                     # la cible Rust ; SKIP_TESTS=1 sans les essais
. ~/.cache/korin-rust/env.sh                 # puis : cargo korin build --release
sh sh tools/python/site-packages.sh             # site-packages de la carte (voie A), Vibe compris (build-korin.sh)
tools/python/build-rust-ext.sh ...           # un paquet pyo3 de PyPI en roue pour Korin (PYTHON-PORT.md « Étape 5 »)
STAGES=posix sh tools/gcc/build.sh           # /gcc : la libposix du moment, et libgcc_mod.a
tools/vibe/build-pc.sh                       # Vibe 3.1.2 sur le PC, dans ~/vibe-pc
```

Après un changement de libposix sur une carte qui a déjà `/gcc` : `STAGES=posix sh tools/gcc/build.sh` (sinon `/gcc`
ne sait pas faire de `.so`).

### 2.3 La carte et QEMU : le point à vérifier d'abord

* **La carte** se fait par `build.ps1` (Windows PowerShell) : il assemble le système par `tools/fasmarm/fasmarm.exe`
  (un exécutable **Windows**) et recopie les `build/<partie>/sdroot` faits dans WSL. La CI le fait sur
  `windows-latest` (`.github/workflows/ci.yml`). **Hors de Windows, rien n'est prévu** : essayer `pwsh` + `wine` pour
  `fasmarm.exe`, ou faire construire la carte par un poste Windows. C'est à régler avant tout essai sous QEMU.
* **Les essais** : `python -I tools/python/testimage.py` fait `build/python/sd.img` depuis `build/sdroot` (mot de passe
  telnet d'essai « essai », `Lib/test`, les scripts de `tools/python/tests` et `tools/vibe/acp_client.py` dans
  `SD0:/home/pytest`) ;
  `tools/qemu_drive.py` démarre QEMU et pilote la carte (`tools/armpi_remote.py`, `ARMPI_REMOTE_PW`). Les essais de
  chaque étape : `python3 /home/pytest/step2.py --net`, `step3.py --net --gcc`, `step5.py`, `step5-pytest.sh`,
  `step6.py`. Prendre d'autres ports que ceux d'une autre session (`--port 4461 --vnc 61 --fwd 2361:23` par exemple).
* **Chemins longs sous Windows** : un worktree (`.claude\worktrees\...`) allonge les chemins de `build\sdroot` ;
  `LongPathsEnabled` n'est pas mis. `site-packages.sh` retire donc les `.pyc` aux noms très longs (`mistralai`), et un
  `build\python\testroot` qui ne s'efface plus se retire par `rd /s /q \\?\<chemin>`.
* **Arrêter toutes les tâches de fond** (QEMU, boucles d'attente) avant de finir une séance : des lanceurs oubliés ont
  tourné des heures.

## 3. L'étape 7 : Vibe de bout en bout sur la carte

L'étape 6 a laissé : `vibe-acp` et `vibe-app-server` dans `SD0:/bin`, tout Vibe dans `site-packages`, le client
`tools/vibe/acp_client.py` (bibliothèque standard seulement) dans `SD0:/home/pytest` de la carte d'essai, et
`session/new` qui répond `-32000 Missing API key` sans compte. Ce qu'il reste :

* **La connexion** : `python3 acp_client.py --sign-in --mode ask` sur la carte ; l'agent donne un lien que
  **l'utilisateur ouvre lui-même** (sur le PC ou un téléphone) ; la clé obtenue va dans `$VIBE_HOME/.env` (mode 600).
  Ne jamais demander, écrire ni afficher une clé. Le réseau de QEMU (mode « user ») sort vers Internet : `urllib`
  joint `https://api.mistral.ai` (step2).
* **La séance** (§9) : lire un fichier, le modifier (approbation demandée et accordée), `git status` et `rg` par le
  shell (bash et git sont reliés à l'arbre POSIX), Échap, `session/load` ; noter les temps.
* **Le temps** : sous QEMU, `initialize` prend 65 à 195 s (le premier lancement compile les `.pyc` de `mistralai`),
  `import mistralai_vibe_local_harness` 28 à 32 s. Le Pi devrait faire mieux ; sinon, voir l'étape 11.
* **Le premier `session/new`** a pris 59 s sur le PC (amorçage des connecteurs) : prévoir de longs délais
  (`acp_client.py` attend 120 s ; à relever sous QEMU).

## 4. Les étapes suivantes, en bref

| Étape | Quoi | Points connus |
|---|---|---|
| 7 | Vibe de bout en bout **sur la carte** | §3 plus haut (sur le PC : `initialize` 2 s ; le premier `session/new` 59 s, puis 2,6 s) |
| 8 | `libacp` et `agent`, client terminal en C | messages ACP : PYTHON.md §5 ; reprendre le `json.h` de PocketQI (`src/apps/pocketqi`) |
| 9 | l'app UIKit (ou le mode « code » de PocketQI) | thèmes Givre et Onyx Noir, mode poche, chrome sans barre ; UIKit de l'OS seulement |
| 10 | paquets `python` et `vibe` (`pkg`) | [PAQUETS.md](PAQUETS.md), `tools/pkg` |
| 11 | vitesse : `preload`, `file_pin`, imports paresseux, charge au repos | Vibe a `scripts/check_startup_import_cost.py` ; viser < 10 s sur le Pi |

Ce que la façade (étapes 8 et 9) doit savoir, appris à l'étape 0 :

* **Connexion** : `authenticate` avec la méthode `browser-auth-delegated` : une URL que l'utilisateur ouvre (dans Jet
  ou sur un autre appareil), puis `complete`. Les arguments (`action`, `attemptId`) vont **dans `_meta`**. Une première
  erreur `-32602 Browser sign-in status could not be retrieved` se reprend. Le résultat est **une clé d'API**, rangée
  dans `$VIBE_HOME/.env` (mode 600) faute de trousseau, ce qui sera le cas sous Korin.
* `session/new` sans clé : `-32000 Missing API key` : proposer la connexion.
* Mode par défaut `accept-edits` (les écritures ne demandent rien) : la façade doit proposer `ask`.
* Sans code mode : pas de sous-agents, pas de process en arrière-plan, pas de connecteurs de Le Chat ; les outils MCP
  deviennent directs (`<groupe>_<outil>`, point 6 de `korin.patch`).

## 5. Défauts connus du système (hors de ce chantier, à ne pas oublier)

* **Quand la mémoire manque, le noyau tue le process qui demande une page**, quel qu'il soit : pendant un essai de
  `cryptography` de 1,5 Go, ce fut le service ramfs, et `/tmp` a disparu jusqu'au redémarrage.
* RamFS : `readdir` pendant des effacements saute une entrée sur deux (contourné dans la `std` de Rust).
* Les programmes reliés avant l'arbre POSIX gardent l'ancien `getcwd` (`SD0:/...`) jusqu'à ce qu'on les relie : bash,
  git, `python3` et la libposix de `/gcc` l'ont été à l'étape 6 ; les autres binaires de `prebuilt/` et des paquets
  (rg, Node...) pas encore.
* Des `print` qui échouent en `EINVAL` au fil de la suite de pydantic-core (passent seuls) : cause inconnue.
* Pas de `tzdata` (`zoneinfo`), pas de socket `AF_UNIX` nommé, pas d'IPv6.
* Des arrêts de QEMU sous charge réseau, sans message : à suivre sur le Pi.
* `python3` n'exporte pas libstdc++ (`dlhost-cxx.ld`) : à ajouter avant un module C++ (numpy, étape 14).

## 6. Règles du propriétaire

* Documentation, commentaires et messages de commit **en français** ; noms d'exécutables, de scripts, de paramètres et
  de nouvelles fonctions **en anglais**.
* Rien de lourd dans le dépôt : sources tierces téléchargées dans `~/.cache/...` ou un dossier ignoré, correctifs en
  patchs ; dire d'où vient chaque archive et la vérifier (sha256 ou révision git).
* Une fonction manquante de libposix s'ajoute **dans libposix** et se note dans POSIX.md, plutôt qu'un contournement
  dans le port ; un écart de la cible Rust se corrige dans `tools/rust` et se note dans RUST.md.
* POSIX par nécessité : seulement ce qu'il faut pour porter, pas un but de conformité. Pas de sécurité contre un
  attaquant : résistance aux bugs et vitesse.
* Les apps graphiques passent par l'UIKit de l'OS et suivent le thème du bureau.
* Chaque étape se note dans PYTHON-PORT.md (ou RUST.md) avec ses essais, et dit **« pas essayé sur le Pi »** tant que
  c'est vrai. Les étapes faites se marquent dans l'encadré du §9 de PYTHON.md.
* **Secrets** : jamais de clé d'API ni d'identifiant dans le dépôt, un message ou une sortie d'outil. Pour les essais
  avec le compte Mistral, la connexion par navigateur (l'utilisateur ouvre le lien) ; une clé éventuelle dans un
  fichier hors dépôt (`~/vibe-secrets/mistral.env`, mode 600), lue sans l'afficher.
* Ne pas pousser sur `main` ni fusionner sans l'accord de l'utilisateur ; travailler sur une branche.
