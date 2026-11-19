# Étude : un serveur MCP pour Korin (`korin-mcp`)

*15/11/2026. État : étude, rien n'est construit.*

## 1. Le problème

Deux agents de code tournent sur Korin : **claude-acp** (`tools/claude/claude_acp`, SDK anthropic, voir
`docs/CLAUDE-ACP.md`) et **Vibe** (Mistral). Ils agissent sur le système par le shell et les fichiers, et
connaissent Korin par les compétences (`.agents/skills`, `tools/vibe/home/skills`).

Cela suffit pour lire, écrire et compiler. Mais trois choses manquent :

- **L'agent ne voit pas l'écran.** Il écrit une app, lance `pibuild`, lit `Installe : …` et s'arrête. Il ne peut pas
  lancer une app graphique au premier plan (elle bloque l'outil `bash`), ni regarder ce qu'elle dessine, ni cliquer
  dedans. Une fenêtre vide, un texte qui déborde, une couleur en dur ou une coordonnée oubliée en 1/16 de pixel ne
  se voient qu'en demandant à l'utilisateur.
- **L'état du système se lit mal.** Les commandes de `/bin` sont en assembleur et ont peu d'options (pas de `ps`,
  `top`, `awk`, `xargs`). L'agent lit des sorties faites pour un humain.
- **L'API des kits se devine.** Les pièges listés dans `tools/vibe/home/AGENTS.md` (unités de `ui_vp_line`,
  `KEY_*` redéfinies…) viennent tous d'une fonction appelée sans avoir lu sa ligne du guide.

Un serveur **MCP** (Model Context Protocol) donne à l'agent des outils typés, qui rendent du JSON ou des images,
au lieu de texte à interpréter.

Au-delà des apps, le même principe sert au travail sur le système : examiner le matériel, mettre au point un
pilote, remplacer un service sans tout casser, auditer (§ 6).

## 2. Ce qui existe déjà

| Brique | Où | Ce qu'elle donne |
|---|---|---|
| Capture de l'écran composé | `WS_OP_SNAPSHOT` d'Elegant ; commande `screenshot` (`src/bin/screenshot`) | l'écran en BMP 32 bits, curseur compris ; **les surfaces du GPU comprises sur le vrai matériel** (vérifié par l'utilisateur) |
| Fenêtres | `WS_OP_WIN_LIST`, `WIN_READ`, `WIN_THUMB`, `WIN_POS`, `WIN_MOVE`, `WIN_CLOSE`, `RAISE` (`src/inc`) | lister, lire, placer, fermer une fenêtre |
| Menus d'une app | `WS_OP_MENU_GET`, `WS_OP_MENU_COMMAND` | lire la barre de menus d'une fenêtre, en lancer une commande |
| Entrées synthétiques | `vncd` : `SYS_SVC_OPEN "hid"` puis `HID_OP_PILOTE` → l'anneau de HIDManager ; `EV_KEY`, `EV_MOUSE_ABS` (0..32767), `EV_WHEEL` | clavier et souris comme un vrai périphérique |
| Process, charge | `sysstat` | l'état des process |
| Journal du noyau | `journal` | les messages du noyau |
| Paquets | `pkg` | installer, mettre à jour, lister |
| Construction | `pibuild` | construire et installer une app |
| API des kits | `src/kits/*/*.abi`, `uikit_abi.h`, `src/apps/help/data/api/*_en.md` | signatures et guide |
| MCP côté Vibe | `tools/vibe/mcp_test_server.py`, `acp_client.py --mcp-test` | Vibe appelle déjà un serveur MCP sur stdio |
| MCP côté ACP | `session/new` et `session/load` portent `mcpServers` | la façade dit à l'agent quels serveurs lancer |

Il n'y a donc presque rien à écrire côté système : tout passe par des opérations qui existent. Le travail est
d'emballer ces opérations dans un serveur, et d'apprendre MCP à claude-acp.

**Ce qui manque à claude-acp** : ses outils sont fixes (`tools.py` : `read_file`, `write_file`, `edit_file`, `bash`,
`glob`, `grep`, `skill`, `update_plan`) et il ignore `mcpServers`.

## 3. Ce que le serveur offrirait

On garde peu d'outils : chaque schéma est envoyé à chaque requête (le cache de prompt réduit le coût, mais le
crédit d'API est compté). Une dizaine d'outils, regroupés par domaine, avec un argument `action` quand c'est
naturel.

### 3.1 Voir et piloter l'écran (la priorité)

| Outil | Entrée | Sortie |
|---|---|---|
| `screen_capture` | `window` (facultatif : id ou titre), `scale` | une **image** PNG ; la taille réelle et l'échelle en texte |
| `window_list` | — | id, titre, app, process, position, taille, au premier plan ou non |
| `window_action` | `window`, `action` (`raise`, `move`, `resize`, `close`) | ok, ou l'erreur |
| `input` | une suite : `click {x, y, button}`, `move`, `drag`, `scroll`, `key {name, mods}`, `type {text}`, `wait {ms}` | ok ; une capture si `screenshot_after` |
| `menu` | `window`, puis rien (lire la barre) ou `command` (en lancer une) | l'arbre des menus, ou ok |

Coordonnées en pixels de l'écran (ou de la fenêtre si `window` est donné) ; le serveur les convertit en
0..32767 pour `EV_MOUSE_ABS`. Le texte passe par `EV_KEY` caractère par caractère : pas de souci de clavier US comme
avec `qemu_drive.py`.

Avec ces outils, l'agent peut faire ce que fait un développeur : lancer son app, la regarder, cliquer sur un bouton,
voir le résultat, corriger.

### 3.2 Lancer et surveiller une app

| Outil | Entrée | Sortie |
|---|---|---|
| `app_run` | chemin de l'app ou de la commande, arguments, dossier | un id ; le process tourne **en arrière-plan** |
| `app_status` | id | vivant ou non, code de sortie, faute (adresse, registre, symbole si connu), dernières lignes de sortie |
| `app_stop` | id | ok |

C'est ce qui manque aujourd'hui à `bash` : une app graphique lancée à travers lui bloque l'outil.

### 3.3 Lire l'état du système

| Outil | Entrée | Sortie (JSON) |
|---|---|---|
| `sys_state` | `what` : `processes`, `journal` (avec `since`, `grep`), `volumes`, `network`, `memory`, `devices` | des listes structurées au lieu de colonnes à découper |
| `pkg` | `action` : `list`, `info`, `install`, `update` | l'état des paquets ; `install`/`update` demandent l'accord de l'utilisateur |

### 3.4 Toutes les fonctions des kits et tous les appels système

Décision de l'utilisateur : l'agent doit pouvoir **chercher et appeler** toutes les fonctions des kits (environ 800
lignes dans les `.abi` : UIKit 221, AudioKit 100, DocumentKit 90…) et tous les appels système (environ 130 dans
`src/inc/syscalls.inc`), pas seulement les lire.

**Le coût.** Un outil par fonction ferait près de 1 000 schémas : bien plus de 100 000 tokens à chaque requête.
Deux façons de l'éviter, et le serveur offre les deux :

1. **Trois outils génériques**, pour tout client (Vibe compris) :

   | Outil | Entrée | Sortie |
   |---|---|---|
   | `api_search` | un nom (`ui_vp_line`), un préfixe (`ui_vp_`) ou des mots (`polygon fill`) ; `kind` : `kit`, `syscall` ou les deux | pour chaque résultat : signature, kit ou numéro, unités, classe (§ ci-dessous), la ligne du guide, un exemple s'il y en a un |
   | `kit_call` | kit, fonction, arguments | la valeur rendue, les tampons de sortie décodés, l'erreur |
   | `syscall` | nom (`SYS_VM_STATS`), arguments | la valeur rendue, les sorties décodées, le nom de l'erreur (`E_SRCH`…) |

2. **Un outil par fonction, chargé à la demande**, pour claude-acp : chaque fonction devient un outil MCP
   `kit__uikit__ui_vp_line`, `sys__SYS_VM_STATS`…, que claude-acp envoie à l'API avec `defer_loading: true`, plus
   l'outil de recherche de l'API (`tool_search_tool_bm25_20251119`). Le modèle ne reçoit que les schémas des outils
   qu'il a trouvés, et les appelle avec des arguments typés et vérifiés. Les outils de §3.1 à §3.3 restent chargés.

**Des signatures lisibles par une machine.** Les en-têtes (`uikit_abi.h`…) donnent les types C, mais pas le sens d'un
pointeur ; les commentaires de `syscalls.inc` sont en prose (« (pid (0 : soi), sortie 64 o) »). Il faut une
description par argument : `int`, `str` (entrée), `buf in N`, `buf out N` (avec son format : `VM_STATS` = 8 × u64
nommés), `handle`, `fnptr` (refusé), et l'unité (`px16` pour les `ui_vp_*`). Elle se range **à côté des sources**,
dans un fichier par kit (`src/kits/uikit/uikit.sig`) et un pour le noyau (`src/inc/syscalls.sig`), dans l'ordre des
tables, et `build.ps1` vérifie qu'elle suit l'`.abi` (même nombre de lignes, mêmes noms), comme
`tools/check_constants.py` pour les constantes. Le guide de `help/` et les en-têtes peuvent ensuite en être tirés.

**Où l'appel s'exécute.** Dans le serveur, ou plutôt dans un **process d'appel** (`kitcall`) qu'il lance par séance :
une fonction qui fait une faute tue ce process, pas le serveur, et la faute revient à l'agent (§ 3.5). Un appel
d'UIKit s'y fait dans la fenêtre de ce process, pas dans celle d'une autre app : pour agir dans une app, c'est
l'arbre des widgets (§ 3.6). Ce que `kit_call` apporte : essayer une fonction pour voir ce qu'elle rend
(`ui_tokens`, une mesure de texte, une résolution DNS de NetKit, un son d'AudioKit, le contenu d'une archive),
apprendre l'API par l'essai.

**Les classes.** Chaque ligne de `.sig` dit sa classe :

| Classe | Exemples | Accord |
|---|---|---|
| `read` | `SYS_PROCLIST`, `SYS_VM_STATS`, `ui_tokens`, mesures | aucun |
| `effect` | écrire un fichier, jouer un son, `SYS_KILL`, `SYS_SPAWN` | comme `bash` |
| `sys` | plage 0x40 (`SYS_DEV_MAP`, `SYS_IO_PORT`, `SYS_IRQ_*`, `SYS_DMA_ALLOC`), `SYS_CLOCK_SET`, `SYS_POWER` | seulement dans `korin-mcp-sys`, un accord à chaque appel |
| `never` | ce qui rend la main à une adresse, change le tas du process d'appel (`SYS_SBRK`), `SYS_EXIT` | refusé |

### 3.5 Toutes les fautes d'une app, sans le journal

Décision de l'utilisateur : l'agent reçoit toutes les fautes d'une app directement, sans lire le journal du noyau.

Aujourd'hui, une faute devient un signal (`idt.c` sur le PC, `signal.asm` sur le Pi) ; non rattrapée, le process
meurt (`proc_exit(-SIGSEGV)`) et le détail ne va qu'au journal. Il faut que le noyau **garde** ce détail :

- **Une fiche de faute par process**, remplie par le noyau à chaque faute, rattrapée ou non : thread (id, nom),
  signal, cause (ESR sur AArch64, vecteur et code d'erreur sur x86-64), adresse fautive, `pc`, registres, et la pile
  remontée par les pointeurs de cadre (32 cadres au plus). Le noyau en garde les **16 dernières** par process (le
  runtime .NET et les JIT rattrapent des fautes exprès : la dernière n'est pas toujours la bonne), et les garde
  après la mort du process jusqu'au `wait` de son parent.
- **Un appel pour les lire**, `SYS_PROC_FAULTS (pid, sortie, n)`, à la fin de la plage 0x10 (ou 0x60 si elle est
  pleine), réservé au parent et aux programmes de `/sys`.
- **Un événement sur busd**, `korin.proc.fault` (pid, signal, rattrapée ou non), pour que le serveur l'apprenne à
  l'instant au lieu de demander.

Le serveur transforme chaque `pc` en `image + décalage` (`SYS_IMAGE_LIST`) puis en symbole (table des symboles de
l'ELF, de l'app ou du kit `.so`) et, si les infos de débogage sont là, en `fichier:ligne`. `app_status` (§ 3.2) rend
alors pour chaque faute : `« SIGSEGV dans ui_vp_poly (uikit.so+0x1a2c), appelée par draw_board (main.c:212) »`.

D'autres échecs que les fautes, rendus au même endroit :

- `abort`, `assert`, sortie avec un code non nul, mort par `SYS_KILL` ;
- **les erreurs signalées par les kits** : une fonction `korin_report (niveau, texte)` de korinrt, que les kits
  appellent sur un mauvais usage (coordonnées hors de l'écran une fois divisées par 16, jeton de thème inconnu,
  fenêtre fermée), écrit dans un petit anneau par process ; le serveur le lit comme les fautes. C'est ce qui rend
  visibles les erreurs silencieuses d'aujourd'hui (« sinon rien ne s'affiche »).

### 3.6 L'arbre des widgets

Décision de l'utilisateur : UIKit expose tout l'arbre de ses widgets, pour que l'agent vise « le bouton OK » et non
des coordonnées.

Le chemin existe déjà pour les menus : `WS_OP_MENU_GET` et `WS_OP_MENU_COMMAND` passent par Elegant jusqu'à l'app.
Même chose avec deux opérations ajoutées **à la fin** de la liste des `WS_OP_` :

- `WS_OP_UI_TREE` : Elegant le transmet à la boucle d'événements de l'app, UIKit répond par l'arbre dans un tampon
  partagé : pour chaque widget, un id stable pendant la vie de la fenêtre, son type (bouton, champ, liste, case,
  onglet, arbre, zone de dessin…), son texte, sa valeur, son rectangle, ses états (actif, visible, focus, coché,
  choisi), ses enfants.
- `WS_OP_UI_ACT` : une action sur un widget par son id : `click`, `set_text`, `set_value`, `select`, `toggle`,
  `focus`, `scroll_to`. UIKit la fait comme si l'utilisateur l'avait faite (mêmes rappels de l'app).

Outils : `ui_tree (window)` rend le JSON, `ui_act (window, id, action, valeur)` agit et rend l'arbre changé (ou une
capture si on la demande).

Une app bloquée ne répond pas : le délai dépassé est lui-même une information (« la boucle d'événements ne tourne
plus »). Une zone dessinée à la main (`ui_vp_*`, un jeu) n'est qu'un widget « zone de dessin » : là, il reste la
capture et les clics. WinForms sur UIKit en profite tel quel ; ce qui dessine lui-même (Avalonia, SDL) non, sauf à
faire le même travail dans ces couches.

C'est aussi une base pour l'**accessibilité** (lecteur d'écran) et pour des **essais d'interface** rejoués sans agent :
ces deux opérations vont dans UIKit et Elegant pour tous, pas seulement pour le MCP.

### 3.7 Garde-fous

Le serveur refuse de lui-même, quoi que demande l'agent :

- toute écriture dans `/media/sd1` ;
- toute écriture dans `/bin`, `/sys`, `/etc`, `/devtools` hors d'une action prévue pour cela (`pkg`) ;
- d'envoyer des entrées à une fenêtre qui n'est pas celle d'une app lancée par `app_run`, sauf si l'utilisateur l'a
  permis pour la séance (l'agent ne doit pas taper dans le terminal ou dans les réglages de l'utilisateur).

Ces règles s'ajoutent à celles du prompt : un prompt demande, le serveur empêche.

## 4. Côté client

### 4.1 claude-acp

L'API Messages a un connecteur MCP, mais pour des serveurs qu'**Anthropic** joint par Internet. Un serveur sur
Korin n'est pas joignable ainsi ; c'est donc claude-acp qui fait le pont :

1. À `session/new` et `session/load`, lire `mcpServers` et lancer chaque serveur (stdio).
2. `initialize`, puis `tools/list` ; chaque outil devient un outil de l'API, nommé `mcp__<serveur>__<outil>`, avec
   son `inputSchema` tel quel.
3. Dans `Session.call`, un nom en `mcp__` part en `tools/call` vers son serveur ; le résultat revient en blocs
   `text` et **`image`** (base64) dans le `tool_result`.
4. Les accords : un outil qui agit (`input`, `window_action close`, `app_run`, `pkg install`) passe par
   `needs_permission`, comme `bash` ; un outil qui lit ne demande rien.
5. Si un serveur meurt, ses outils disparaissent de la requête suivante et l'agent en est averti.
6. Les outils marqués « à la demande » par le serveur (les fonctions des kits et les appels système, § 3.4) partent
   avec `defer_loading: true`, et claude-acp ajoute l'outil de recherche de l'API
   (`tool_search_tool_bm25_20251119`). Au moins un outil doit rester chargé, sinon l'API refuse la requête. Vibe,
   qui n'a pas cette recherche, se sert de `api_search`, `kit_call` et `syscall`.

Le client tient en 150 à 200 lignes de Python, sans dépendance : JSON-RPC sur stdio, comme `mcp_test_server.py`.
Le paquet `mcp` de PyPI tire pydantic et anyio ; on peut s'en passer.

Les images coûtent des tokens : le serveur réduit la capture (par défaut 1280 px de large au plus) et l'agent peut
demander une fenêtre seule.

### 4.2 Vibe

Rien à faire, sauf que la façade (`src/apps/vibe`, `agent`) doit passer `korin-mcp` dans `mcpServers`. Le même
changement sert à claude.app et à `agent --claude`.

### 4.3 Depuis le PC

Plus tard, le serveur peut aussi écouter sur le réseau (TCP, ou HTTP par `webd`). Claude Code sur le PC pilote
alors le Pi ou le ZBook par des outils typés, au lieu de `armpi_remote.py telnet/ftp/vnc`. Il faut alors une
authentification (un jeton dans `/etc`, comme le mot de passe de vncd), et rien d'ouvert par défaut.

## 5. Langage et place du serveur

| Choix | Pour | Contre |
|---|---|---|
| **Python** (bibliothèque standard), dans le paquet `claude` ou un paquet `korin-mcp` | vite écrit, même langage que claude-acp, facile à changer | lent à démarrer ; parle aux services par des commandes (`screenshot`, `sysstat`…) ou par un petit module C |
| **C** (`src/sys/mcpd`), sur korinrt | parle directement à Elegant et à HIDManager ; léger ; sert aussi au réseau | plus long à écrire ; JSON à la main |

Proposition : **commencer en Python**, avec un petit module C (`_korin`, construit comme les extensions de
`tools/python`) pour trois appels : capture d'une fenêtre (`WS_OP_SNAPSHOT` / `WIN_READ`), liste des fenêtres
(`WS_OP_WIN_LIST`), et l'anneau de HIDManager (`HID_OP_PILOTE`). Le reste passe par les commandes existantes. On
réécrira en C si la lenteur gêne ou si le serveur doit vivre sans Python.

Ce qui sert à d'autres programmes (lire une fenêtre, injecter des entrées) va dans **UIKit** ou un kit du domaine,
pas dans le serveur : `qemu_drive.py`, un futur enregistreur de tests ou l'accessibilité en profiteraient.

## 6. Au-delà des apps : système, pilotes, matériel, audit

Le même serveur peut servir au travail sur Korin lui-même. Ici, l'agent n'écrit pas une app : il examine le
matériel, écrit ou corrige un pilote, répare un service, ou contrôle que le système est sain. Ces usages demandent
plus de droits et plus de prudence ; ils ont leurs propres outils, séparés de ceux des apps.

### 6.1 Examiner le matériel

Korin découvre déjà le matériel : `pcid` énumère le PCIe par l'ECAM (identité, classe, BAR, MSI/MSI-X), `usbd`
annonce chaque interface USB, `hwmanager` sonde l'I2C du Pi et lance le pilote trouvé dans `/etc/drivers.txt`,
`SYS_ACPI_TABLE` donne les tables ACPI. Mais tout cela ne se lit que par morceaux, dans le journal ou par des
commandes d'essai.

| Outil | Sortie |
|---|---|
| `hw_inventory` | l'arbre complet : fonctions PCIe (BAR, capacités, IRQ), périphériques et interfaces USB (descripteurs), adresses I2C qui répondent, périphériques du SoC ; pour chacun **le pilote qui le sert**, ou aucun |
| `hw_unclaimed` | ce qui n'a pas de pilote, avec la ligne de `drivers.txt` qui le servirait : la liste des pilotes à écrire |
| `acpi` | la liste des tables ; une table brute ou décodée (MCFG, MADT, DSDT en résumé) |
| `irq_stats` | les interruptions par ligne et par pilote : un pilote qui n'en reçoit aucune, ou trop, se voit |
| `bus_watch` | les événements de busd (`korin.usb.*`…) pendant N secondes : ce qui arrive quand on branche une clé |

C'est ce qu'on fait aujourd'hui à la main sur le ZBook (touchpad, son, Wi-Fi : `docs/etude/PC-PILOTES.md`,
`TOUCHPAD-PC.md`, `SON-PC.md`, `WIFI-PC.md`). Un agent qui rend un inventaire complet et compare avec la
documentation du composant gagne une bonne partie de ces études.

### 6.2 Écrire et mettre au point un pilote

Un pilote est un service de `/sys/svc` lancé par `hwmanager`. La boucle de mise au point serait :

| Outil | Ce qu'il fait |
|---|---|
| `svc_list` | les services : nom, pid, enregistré ou non, droits, relances par hwmanager |
| `svc_replace` | pose un nouveau binaire de service et le relance ; **garde l'ancien** et le remet si le nouveau ne s'enregistre pas dans un délai donné |
| `svc_restart` / `svc_stop` | relancer ou arrêter un service |
| `klog` | le journal du noyau depuis un repère, filtré par service : les messages et les fautes du pilote seulement |
| `reg_read` | lire des registres dans une BAR ou une zone du SoC (MMIO 8/16/32/64 bits, ports d'E/S sur le PC) |
| `reg_write` | écrire un registre : **un accord de l'utilisateur à chaque appel** |
| `pci_config` | lire l'espace de configuration d'une fonction |

**Le piège** : un pilote faux peut figer la machine, et l'agent avec elle s'il tourne dessus. Pour les pilotes,
l'agent doit donc tourner **ailleurs** que sur la machine essayée :

- **Claude Code sur le PC** pilote la machine cible par le serveur en réseau (§ 4.3) : le ZBook ou le Pi plantent,
  le PC garde la séance, le code et le journal reçu jusque-là. C'est le cas le plus utile.
- Ou **sous QEMU** sur le PC, avec un petit serveur MCP côté PC pour QEMU lui-même : démarrer, arrêter, la sortie
  série, les instantanés, les registres par le gdbstub. QEMU n'émule qu'une partie du matériel (`raspi4b` n'a pas
  de GPU), mais une faute du noyau s'y examine sans rien perdre.

Le serveur cible envoie le journal du noyau **au fil de l'eau** (pas seulement à la demande), pour que le client
ait les dernières lignes même si la machine se fige ensuite.

### 6.3 Corriger le système

Sur le Pi, les sources sont dans `/src` et Korin se construit en partie lui-même (`fasm`, `fasmarm`, `selfhost`,
GCC). Sur le PC, on construit sous Windows (`build.ps1`, WSL) puis on installe. Dans les deux cas, l'agent a besoin
de remplacer un morceau du système sans tout casser :

- `svc_replace` (§ 6.2) pour un service, avec retour à l'ancien ;
- `sys_install` pour une commande, un kit ou une app système : la copie de l'ancien dans un dossier de secours,
  la liste de ce qui a été changé, `sys_rollback` pour tout défaire ;
- jamais le noyau ni `/boot` à chaud : l'outil prépare et demande un redémarrage à l'utilisateur.

### 6.4 Auditer le système

Un audit, c'est une suite de contrôles qui rendent une liste d'écarts. Le serveur les fait mieux que l'agent par le
shell, parce qu'il lit les structures du noyau directement.

| Contrôle | D'où |
|---|---|
| Les services et leurs droits : qui est dans `/sys` (`SYS_PROC_RIGHTS`), qui a mappé du matériel | `SYS_PROCLIST`, `SYS_PROC_INFO` |
| Les ports ouverts et qui les écoute ; vncd, ftpd, telnetd, smbd, rdpd actifs sans mot de passe | `netstat`, `/etc` |
| La mémoire et les descripteurs par process, **suivis dans le temps** : une fuite se voit sur dix minutes | `SYS_PROC_STATS`, `SYS_VM_STATS`, `SYS_HANDLE_LIST` |
| Les fichiers installés comparés aux paquets : un fichier changé, manquant ou de trop | `pkg`, les listes des paquets |
| `drivers.txt` : une ligne vers un pilote absent ; un pilote qui ne sert plus rien | `/etc/drivers.txt`, `/sys/svc` |
| Les apps et leurs kits : une app qui appelle un rang au-delà de la table `.abi` installée | les tables des kits, les binaires |
| Le journal : fautes, relances de pilotes, erreurs répétées depuis le démarrage | `SYS_KLOG` |

`audit` rend un rapport JSON ; l'agent l'explique et propose les corrections, sans les faire seul.

### 6.5 Les droits : deux serveurs

Lire le matériel demande des droits que n'a pas une app : `SYS_DEV_MAP`, `SYS_IO_PORT` et `SYS_ACPI_TABLE` sont
réservés aux programmes de `/sys`. Plutôt qu'un seul serveur tout-puissant :

- **`korin-mcp`** (§ 3) : les apps, l'écran, l'état du système en lecture ; droits d'une app ordinaire.
- **`korin-mcp-sys`**, dans `/sys` : matériel, services, audit. **Lecture seule par défaut** ; `reg_write`,
  `svc_replace`, `sys_install` et l'arrêt d'un service demandent un accord à chaque fois. Il ne touche jamais au
  contrôleur du disque système ni à `/media/sd1`, et ne s'écoute en réseau qu'avec un jeton.

La façade ne passe `korin-mcp-sys` dans `mcpServers` que si l'utilisateur l'a demandé pour la séance.

### 6.6 Montrer sans écrire d'app (idée du 19/11)

L'utilisateur veut dire à l'agent « affiche-moi ceci », « montre-moi cela », et que l'agent le **montre lui-même**,
sans lancer ni créer d'app. Le MCP parle alors directement au système :

- **Tous les kits, les services et Elegant** sont joignables par le MCP, pas seulement la capture et les entrées :
  UIKit, MediaKit, DocumentKit, NetKit… (les fonctions des kits déjà prévues au § 3, `kit_call`), les services
  (`svc_call` sur leurs ops), et Elegant (fenêtres, menus, notifications, bureau).
- **Des fenêtres à la volée** : l'agent décrit une fenêtre (titre, taille, contrôles : textes, listes, boutons,
  champs, images, graphiques) et le serveur la crée par UIKit, dans un process à lui. Les événements (clic, saisie,
  choix dans une liste) remontent à l'agent comme des notifications MCP, qui peut répondre en changeant la fenêtre.
- **Des vues prêtes** : un tableau, une image, un graphique, un texte riche, un formulaire de quelques champs, la
  carte d'un fichier ; l'agent n'écrit que les données.
- **Rien de permanent** : la fenêtre vit tant que la séance ; « garde-la » devient alors une vraie app (le skill
  `korin-app` reprend la description).

À trancher : un process-hôte des fenêtres par séance, ou par fenêtre ; le format de description (JSON proche de
l'arbre des widgets, `WS_OP_UI_TREE` du § 3, pour qu'une même forme serve à lire une app et à en montrer une) ; ce
que l'agent peut faire sans accord (montrer : oui ; agir sur le système par un service : accord, comme § 6.5).

## 7. Étapes

1. **Client MCP dans claude-acp** (§ 4.1), essayé avec `mcp_test_server.py`. **En cours dans le chantier de la CAO**
   (branche `claude/cao-occt-367ad4`, 15/11) : un client MCP pour piloter Forge par MCP avec Claude. `korin-mcp`
   reprend ce client, sans en écrire un second ; ce qui manque ici (images dans `tool_result`, accords,
   `defer_loading`) s'y ajoute. Forge devient aussi le premier exemple d'outils fournis par une app (étape 8).
2. **`korin-mcp` v0** : `screen_capture`, `window_list`, `input`, `app_run`/`app_status`/`app_stop`. C'est la boucle
   « écrire, lancer, regarder, corriger ».
3. Les façades passent `korin-mcp` dans `mcpServers` ; la compétence `korin-app` dit d'essayer l'app avant de
   rendre la main.
4. **Les fautes** (§ 3.5) : fiches de faute du noyau, `SYS_PROC_FAULTS`, `korin.proc.fault`, puis `korin_report`
   dans les kits. **L'arbre des widgets** (§ 3.6) : `WS_OP_UI_TREE`, `WS_OP_UI_ACT` dans Elegant et UIKit.
5. **Les fichiers `.sig`** (§ 3.4) et leur contrôle dans `build.ps1`, puis `api_search`, `kit_call`, `syscall` et les
   outils à la demande ; ensuite `sys_state`, `menu`, `window_action`, `pkg`.
6. **`korin-mcp-sys` en lecture seule** : `hw_inventory`, `hw_unclaimed`, `klog`, `svc_list`, `audit`. Sans risque, et
   utile tout de suite sur le ZBook.
7. **L'écoute réseau authentifiée** (§ 4.3) et le journal envoyé au fil de l'eau ; Claude Code sur le PC s'en sert
   pour la mise au point des pilotes. Puis `svc_replace`, `reg_read`, `reg_write`, les appels système de classe
   `sys`, et le serveur MCP de QEMU côté PC.
8. Plus tard : `sys_install`/`sys_rollback`, et des outils fournis par les apps elles-mêmes (Files, Office,
   WebStudio déclarent leurs actions par un kit commun).

Critère de réussite de l'étape 2 : sous QEMU, l'agent écrit une petite app avec un bouton, la lance, clique dessus,
voit le changement dans la capture et le dit, sans aide.

## 8. Questions ouvertes

Réglées par l'utilisateur le 15/11 : la capture marche avec les surfaces du GPU sur le vrai matériel ; l'agent
reçoit toutes les fautes sans le journal (§ 3.5) ; UIKit expose tout l'arbre des widgets (§ 3.6) ; toutes les
fonctions des kits et tous les appels système sont exposés (§ 3.4).

- **Les fiches de faute** : la pile par pointeurs de cadre suppose `-fno-omit-frame-pointer` dans les apps et les kits
  en C, et un cadre tenu à la main dans l'assembleur. Que faire du code qui n'en a pas ?
- **Numéro libre** pour `SYS_PROC_FAULTS` : reste-t-il une place dans la plage 0x10 ?
- **Les `.sig`** : les écrire à la main pour 1 000 lignes est long ; peut-on en tirer la plus grande part des en-têtes
  C et des commentaires de `syscalls.inc`, et ne relire que les pointeurs ?
- **Plusieurs agents** sur la même session graphique : un seul `korin-mcp` à la fois pilote les entrées ?
- **Le compteur d'interruptions** (`irq_stats`) existe-t-il dans le noyau, ou faut-il l'ajouter ?
- **`svc_replace`** : comment savoir qu'un pilote « marche », au-delà de « il s'est enregistré » ? Un essai propre à
  chaque classe (un bloc lu pour un disque, un paquet pour le réseau) ?
- **Les droits de `korin-mcp-sys`** : `SYS_PROC_RIGHTS` ne dit que « programme de `/sys` ou non ». Avec les comptes
  (`docs/etude/MULTI-UTILISATEUR.md`), qui a le droit de lancer ce serveur ?
