# korin-mcp : le serveur MCP de Korin

*22/11/2026. Fait : le serveur (`src/sys/mcp`), l'enregistrement pour Claude Code (paquet `claude-code`) et pour
Vibe (libacp). Étude d'origine, plus large : [etude/MCP.md](etude/MCP.md).*

`korin-mcp` donne aux agents de code (Claude Code, la CLI officielle sous l'hôte Linux ; Vibe ; tout client MCP) des
**outils typés qui rendent du JSON** sur l'état du système, au lieu de lancer `sysstat`, `journal`, `df` ou `pkg` par
le shell et d'en découper la sortie, faite pour un humain. Il lit tout par les appels système, NetKit et les services
(vfs, `fs.<volume>`, intelgpu, Elegant), jamais en analysant la sortie d'une commande.

## 1. Les outils

| Outil | Ce qu'il rend | D'où |
|---|---|---|
| `processes` | chaque process : pid, nom, état, threads, mémoire (Ko à lui, partagés, tas), appels système (en tout, par seconde), temps de processeur (ms, % pendant la mesure), programme de `/sys` ; filtre `name`, tri `cpu` | `SYS_PROCLIST_FROM`, `SYS_PROC_STATS`, `SYS_THREAD_LIST`, `SYS_VM_STATS`, `SYS_PROC_RIGHTS` |
| `process` | un process (pid ou nom) : mémoire détaillée (fautes, copies sur écriture, tables), chaque thread (état, priorité, cœur, temps), ses 12 appels système les plus fréquents **par nom** pendant la mesure ; pour un process de `launch` : sa commande, son journal, son code de sortie | les mêmes, `waitpid` |
| `cpu` | charge et MHz de chaque cœur, mémoire totale et libre, temps depuis le démarrage, commutations, les threads qui consomment le plus pendant la mesure (comme `cpuwatch`) | `SYS_CPU_LOAD` (PC), `SYS_SYSINFO`, `SYS_THREAD_LIST` |
| `kernel_log` | les N dernières lignes du journal du noyau, filtrées par **source** (le premier mot : `netd`, `intelgpu`, `vfs`…), par **regex** POSIX étendue, ou depuis un instant (`since_ms`) | `SYS_KLOG` (ce que `journal` copie dans `/journal.txt`) |
| `list_dir` | nom, type, taille, date UTC, droits ; dossiers d'abord | libposix (`opendir`, `stat`) |
| `read_file` | un fichier ou un morceau : par octets (64 Ko par défaut, 512 Ko au plus) ou par lignes ; texte UTF-8, sinon base64. **Refuse** les fichiers de mots de passe et de clés (`/etc/shadow.ini`, `/etc/remote.txt`, `/etc/wifi*`, `/etc/keyvault`, `/etc/oauth/`, `.env`, `.credentials.json`…) | libposix |
| `find_files` | par motif (`*.c`) ou morceau du nom, type, profondeur ; limité en nombre (5 000), en entrées vues (300 000) et en temps (20 s) ; `/net` seulement si on part de dedans | libposix (`fnmatch`) |
| `volumes` | chaque volume monté : nom, point de montage, système de fichiers, taille, prise, libre, étiquette (`/media/sd1` : rappel qu'on n'y écrit jamais) | `VFS_MOUNTS` de vfs, `FS_INFO` de `fs.<volume>` (comme `df`) |
| `packages` | `installed` (version, version du dépôt, mise à jour ?), `updates`, `available` (tout le dépôt), `info` (la base d'un paquet et son entrée du dépôt) | `/var/pkg/db/*.ini`, `/var/pkg/index.txt` (le dernier index **vérifié** ; rien sur le réseau) |
| `services` | chaque ligne de `/etc/services.txt` (init2) et de `/etc/startup.txt`, active ou en commentaire, en marche (pid) ou non ; tous les process qui ont les droits de `/sys` (services, pilotes de hwmanager) ; quelques services enregistrés | les fichiers, `SYS_PROCLIST_FROM`, `SYS_PROC_RIGHTS`, `SYS_SVC_OPEN` |
| `gpu` | intelgpu : prêt, simulé ou perdu, identité, EU, remises à zéro, le texte de `intelgpu status` et le temps des lots ; ou « ne tourne pas » | `IG_OP_INFO`, `IG_OP_STATUS`, `IG_OP_TIMES` du service `intelgpu` |
| `network` | adresse, masque, passerelle, DNS, MAC, DHCP ; les interfaces (lien, débit, SSID) ; tous les sockets (TCP, UDP, état, ports, adresse distante, programme) | NetKit : `net_info`, `net_ifaces`, `net_sockets` (comme `netstat`) |
| `time` | UTC, locale, décalage, temps depuis le démarrage, heure venue du réseau ou non | `SYS_CLOCK_GET`, `SYS_TIME_MS`, `net_time` |
| `windows` | les fenêtres d'Elegant, du fond vers l'avant : id, titre, process, position, taille, au clavier, cachée, couche | `WS_OP_WIN_LIST` |
| `screenshot` | **une image PNG** (bloc `image` de MCP) de l'écran, d'une fenêtre (morceau du titre ou id, avec ou sans cadre) ou d'un rectangle, réduite d'un facteur entier sous `max_width` (1280) ; la taille et l'échelle en texte | `WS_OP_SNAPSHOT` (comme `screenshot`), PNG écrit par `png.c` |
| `send_signal` | un signal (TERM par défaut, INT, HUP, KILL, STOP, CONT, USR1…) ; refusé pour un programme de `/sys` sans `allow_system`, et pour korin-mcp lui-même | `kill` de libposix (`SYS_SIG_SEND`), `SYS_KILL` |
| `launch` | lance une commande ou une app (`terminal`, `/apps/files.app`) **en arrière-plan**, arguments, dossier ; sortie et erreurs dans `/tmp/korin-mcp/<pid>-<n>.log` ; rend le pid et, après `wait_ms`, l'état. Refusés : `/sys`, `/sbin`, `/boot`, les commandes qui effacent, formatent, montent ou changent les comptes (`rm`, `mv`, `mkfs`, `gpt`, `osinstall`, `reboot`, `sudo`, `mount`…), tout argument dans `/media/sd1` | `posix_spawnp` de libposix |

Tous les outils sauf `send_signal` et `launch` ne font que lire (annotation `readOnlyHint`) ; korin-mcp n'écrit rien,
sauf les journaux de ce qu'il lance. Les accords restent ceux du client : Claude Code demande avant chaque outil MCP
(sauf réglage contraire de l'utilisateur), et korin-mcp refuse de lui-même ce qui est listé ci-dessus.

Exemple (`korin-mcp --call cpu '{"sample_ms":500,"top":2}'`) :

```json
{"sample_ms":500,"cores":2,"per_core":[{"core":0,"load_percent":3,"busy_ms":2210}, …],
 "memory":{"total_kb":4064256,"free_kb":3701248,"used_kb":363008},"uptime_ms":61234,"processes":31,
 "context_switches":181220,"top_threads":[{"pid":12,"process":"netd","tid":40,"thread":"netd","core":1,
 "cpu_percent":1.2,"runtime_ms":844}, …]}
```

## 2. Le protocole

* **Transport stdio** de MCP : JSON-RPC 2.0, un message par ligne sur l'entrée et la sortie standard ; la fin de
  l'entrée arrête le serveur. Versions **2025-11-25** (celle que demande Claude Code 2.1.330), 2025-06-18,
  2025-03-26, 2024-11-05 : le serveur rend celle du client s'il la connaît, sinon la plus récente.
* Méthodes : `initialize` (capacité `tools`, `instructions` : un court guide pour le modèle), `ping`, `tools/list`
  (avec `title` et les annotations depuis 2025-06-18), `tools/call`. Le résultat d'un outil : un bloc `text` (le JSON),
  plus un bloc `image` (PNG en base64) pour `screenshot` ; un échec d'outil : `isError` et son explication en texte.
* **Claude Code 2.1.330 sonde d'abord `server/discover`** (le protocole sans état de la version 2026-07-28 du
  projet de spécification), puis repasse à `initialize` quand la méthode est inconnue : korin-mcp rend `-32601`, comme
  pour `resources/*` et `prompts/*`. Les notifications (`initialized`, `cancelled`) sont lues et ignorées.
* `korin-mcp --list` : les outils ; `korin-mcp --call OUTIL '{json}'` : un outil, une fois (essais à la main ; une
  capture va dans `/tmp/korin-mcp.png`).

## 3. Le choix du C

Le serveur est en **C sur libposix**, comme `linuxhost`, `pkg`, `webd`, et non en Python (que proposait l'étude) :

* il **appelle le noyau directement** (`<armpi/syscall.h>`) et les kits par leur table (`<armpi/kit.h>`) : process,
  journal, mémoire, fenêtres, capture et intelgpu n'ont pas d'interface en Python ; il aurait fallu un module C de
  toute façon ;
* **aucune dépendance** : le paquet `python` (CPython 3.14, ≈ 100 Mo) n'est pas demandé, korin-mcp marche sur un
  système qui n'a que `claude-code` ; un binaire de ≈ 100 Ko, qui démarre tout de suite (Claude Code lance ses
  serveurs à chaque séance) ;
* libposix plutôt que korinrt (le runtime des commandes de `/bin`) : `snprintf`, `malloc`, `opendir`, `stat`,
  `regcomp`, `fnmatch`, `posix_spawn`, `waitpid` sont là ; korinrt n'a pas de quoi écrire du JSON de taille libre.

Les numéros des appels système, les dispositions (`THL_*`, `WE_*`, `NIF_*`, `NSK_*`, `NI_*`), les opérations de vfs et
d'Elegant et les **rangs de NetKit** sont tirés des `.inc` et de `netkit.abi` à la construction
(`build/pc/mcp/gen/korin_consts.h`, `korin_sysnames.h`) : une table qui grandit ne demande rien d'autre. Seule la
structure `ig_info` d'intelgpu (`src/svc/intelgpu/igproto.h`) est recopiée.

| Fichier | Rôle |
|---|---|
| `src/sys/mcp/main.c` | la boucle stdio, JSON-RPC, MCP, `--list`, `--call` |
| `src/sys/mcp/tools.c` | les outils du système et la table des outils (schémas, descriptions, annotations) |
| `src/sys/mcp/files.c` | fichiers, volumes, paquets |
| `src/sys/mcp/json.c` | lire (descente récursive, `\u` et paires de substitution) et écrire (échappements, UTF-8 faux remplacé par U+FFFD, base64) |
| `src/sys/mcp/png.c` | PNG RGB : filtre Sub ou Up par ligne, deflate à codes fixes et LZ77 (aucun kit n'écrit de PNG) |
| `src/sys/mcp/mcp.json` | `/etc/claude-code/mcp.json` |
| `tools/mcp/build.sh` | la construction (`ARCH=x86_64` par défaut ; `aarch64` pour le Pi) ; `--host` : les essais du JSON et du PNG sur la machine de construction (`tools/mcp/hosttest.c`) |
| `tools/mcp/pctest.py` | l'essai sous QEMU PC (§ 5) |

## 4. L'enregistrement

### Claude Code

`/bin/claude` (paquet `claude-code`) lance Claude Code avec **`--mcp-config=/etc/claude-code/mcp.json`** (serveur
`korin`, commande `/bin/korin-mcp`) : ses outils s'appellent `mcp__korin__processes`, `mcp__korin__screenshot`…,
pour tous les utilisateurs et toutes les séances (Terminal, l'app Claude Code, KorinStudio, Craft Anything : tous
passent par `/bin/claude`). Les consignes gérées (`/etc/claude-code/CLAUDE.md`) disent de les préférer aux commandes.

Pourquoi pas le fichier géré `managed-mcp.json` de `/etc/claude-code` : **vérifié avec Claude Code 2.1.330** (dans
un espace de montage à part), il prend le **contrôle exclusif** des serveurs MCP : les serveurs de l'utilisateur
(`claude mcp add`), ceux de KorinStudio (`--mcp-config`), de Forge et de TerraCube seraient ignorés (« Ignored: an
enterprise MCP config (managed-mcp.json) is present and has exclusive control over MCP servers »). La clé
`managedMcpServers` de `managed-settings.json` n'est pas lue de ce fichier (elle vient de la console
d'administration). `--mcp-config` s'ajoute aux autres : vérifié, deux `--mcp-config` (celui de `/bin/claude` et celui
de KorinStudio) démarrent leurs deux serveurs. La forme `--mcp-config=FICHIER` ne prend qu'une valeur
(`--mcp-config FICHIER` avalerait l'invite qui suit) ; elle est sans effet sur `--version` et `mcp add|list|remove`.
Pour s'en passer : lancer `/devtools/claude-code/claude` par `linuxhost run` sans le lanceur, ou `--strict-mcp-config`.

### Vibe (et l'agent)

libacp (`src/lib/acp/acp.c`, l'app Vibe et `agent`) passe `korin-mcp` dans `mcpServers` de `session/new` et
`session/load` quand `/bin/korin-mcp` est installé (`KORIN_MCP=0` : sans) ; vibe-acp le lance sur stdio. Les
consignes de Vibe (`tools/vibe/home/AGENTS.md`) le citent.

### Paquets

`korin-mcp` est dans le paquet **`claude-code`** (`bin/korin-mcp`, `etc/claude-code/mcp.json`) ; Vibe s'en sert s'il
est installé. Construire : `ARCH=x86_64 sh tools/posix/build.sh` (une fois), `sh tools/mcp/build.sh`, puis
`sh tools/pc/sysroot.sh` le pose dans `build/pc/sysroot/bin`.

## 5. Essais

* `sh tools/mcp/build.sh --host` : le JSON (lecture, écriture, échappements, `\u`, UTF-8 faux) et le PNG (relu par
  zlib de Python, pixel pour pixel), sur la machine de construction.
* `python3 -I tools/mcp/pctest.py` : sous QEMU PC (le disque de `tools/linux/exectest/run.sh`), un client MCP envoie
  `initialize`, `tools/list`, `ping`, `server/discover`, puis **chaque outil** (34 appels, dont des erreurs voulues :
  fichier secret, signal à un service, `/sys` à lancer, regex fausse, outil inconnu, JSON illisible) ; chaque réponse
  est vérifiée ici, la capture relue comme PNG (`build/pc/mcp/test/screen.png`) ; `launch`, `process` et
  `send_signal` par `--call` ; puis **Claude Code 2.1.330 lui-même, sous linuxhost**, lance korin-mcp comme serveur
  MCP (`claude mcp add -s user`, `claude mcp list` : « ✔ Connected »). Aucun compte ni aucune clé : `mcp list` ne
  fait que le contrôle des serveurs.

## 6. Ce qui reste

* L'étude ([etude/MCP.md](etude/MCP.md)) va plus loin : entrées synthétiques (clavier, souris par `HID_OP_PILOTE`),
  arbre des widgets d'UIKit, fiches de faute du noyau, `kit_call`/`syscall` génériques, `korin-mcp-sys` (matériel,
  pilotes, audit). `launch` + `screenshot` font déjà la moitié de la boucle « lancer, regarder » ; il manque cliquer.
* Le Pi : `ARCH=aarch64 sh tools/mcp/build.sh` construit (même source ; `SYS_CPU_LOAD` y manque, `cpu` prend
  `SYS_SYSINFO`), pas essayé ; Claude Code ne tourne que sur le PC.
* La capture d'une fenêtre prend le rectangle de l'écran (ce qui la recouvre se voit) ; `WS_OP_WIN_READ` donnerait
  son contenu seul.
