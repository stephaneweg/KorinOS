# Les commandes Unix (`SD0:/bin`)

Réécrites en assembleur d'après les outils d'Onyx (`user/BinUtils`), dans le selfhost. Chacune est
un petit programme de `src/bin/<nom>/`, sur le socle `src/inc/tool.inc` (l'équivalent de `tool.h`
d'Onyx). `sh` ([SHELL.md](SHELL.md)) les trouve par `PATH` (`/bin`). Elles se lancent aussi d'un
programme C, avec des arguments sans limite de longueur.

| Commande | Options |
|---|---|
| `cat` | fichiers, `-` : entrée standard |
| `ls` | `-a`, `-l`, trié |
| `cp` | `-r`, source(s) destination |
| `mv` | sans copie sur un volume, un fichier recopié d'un volume à l'autre |
| `rm` | `-f`, `-r` |
| `mkdir` | `-p` |
| `rmdir`, `touch` (`-c`), `basename` (suffixe), `dirname`, `pwd`, `sleep`, `echo` (`-n`), `true`, `false`, `test` | |
| `head`, `tail` | `-n N`, `-N` |
| `wc` | `-l`, `-w`, `-c` |
| `tee` | `-a` |
| `tr` | `-d`, plages `a-z`, `\n`, `\t` |
| `cut` | `-d C -f 1,3-5`, `-c 2-4` |
| `sort` | `-r`, `-n`, `-u` (tri fusion, stable) |
| `uniq` | `-c`, `-d`, `-u` |
| `grep` | `-E`, `-i`, `-v`, `-n`, `-c`, `-l`, `-q`, `-e` |
| `sed` | `-n`, `-E`, `-e` ; adresses (numéro, `$`, `/motif/`, plages) ; `s` (`g`, `p`, `&`, `\1`), `d`, `p`, `q` |
| `find` | `-name`, `-type f\|d`, `-maxdepth` |
| `arc` | archives, sur ArchiveKit : `arc l\|t archive` (lister, vérifier), `arc x archive [dossier]`, `arc c archive [-0..-9] fichiers` (format d'après `.zip`, `.tar`, `.tgz`, `.tar.gz`, `.gz`), `arc f` (formats) |
| `zip` | ZIP comme Info-ZIP, sur ArchiveKit : `zip [-r] [-0..-9] [-j] [-k] [-q] [-p DOSSIER] archive chemins`, `zip -d archive noms` (motifs `*` `?`, ou un dossier) ; une archive qui existe est réécrite à côté (`.tmp`) puis remplacée. `arc` reste l'outil à lettres (TAR, tar.gz, gz aussi) |
| `unzip` | `[-l\|-v\|-t\|-p] [-o\|-n] [-j] [-q] [-d dossier] archive [noms] [-x noms]` ; sans `-o`/`-n`, demande avant de remplacer ; pas d'entrées chiffrées (`-P` refusé) |
| `diff` | `-q` ; sorties `2c2`, `5a6,7`, `8,9d7` avec `<`, `---`, `>` ; codes 0 pareils, 1 différents, 2 erreur |
| `ed` | l'éditeur de lignes d'Onyx : `-p invite` ; adresses `N . $ +N -N /re/ ?re? 'x , ;` ; `a i c d j p n l s m t k g v r w W wq e E f = u q Q h H` (regex de `sed`) |
| `nl` | `-b a` (aussi les lignes vides) ; plusieurs fichiers : un seul texte |
| `hexdump` | `-s position`, `-n nombre` (décimal ou `0x`) |
| `date` | `-u`, `+FORMAT` (`%Y %m %d %H %M %S %y %F %T %s %n %%`) ; ne règle pas l'horloge |
| `uname` | `-a -s -n -r -v -m -p` : Korin, `hostname =` de `etc/system.ini`, version du paquet `armpi`, niveau d'ABI des kits (`etc/pkg/abi`) |
| `df` | `[volume ou chemin...]` : système de fichiers, taille, pris, libre, étiquette (`VFS_VOLUMES`, `FS_INFO`) |
| `sysstat` | `[pid \| nom]` : appels système par seconde (mesurés sur une seconde) et en tout ; pour un process, ses 8 appels les plus fréquents (`SYS_PROC_STATS`, [ABI.md](ABI.md)) |
| `volume` | la sortie principale du mixer : `volume [0..100 \| mute \| unmute \| toggle]`, `volume output [auto\|jack\|hdmi\|usb\|n°\|nom]`, `volume apps`, `volume app <nom\|pid> <0..100\|mute\|unmute>` ; `soundctl` reste l'outil complet, par numéros |
| `wget` | `[-O fichier] [-q] [-S] URL` : HTTP seulement (redirections suivies, 5 au plus) ; le corps sur la sortie standard sans `-O`. HTTPS refusé : pas de TLS pour l'assembleur |
| `nslookup` | `nom` : l'adresse IPv4, par le serveur DNS du bail |
| `netstat` | `-t`, `-u`, `-l`, `-i` : interfaces, configuration IP, sockets de tout le système (op `NET_OP_SOCKETS` de netd, `net_sockets` de NetKit) |
| `whois` | `[-h serveur] [-p port] domaine [serveur]` : whois.iana.org, puis le serveur de son `refer:` |
| `ftp` | `[-u utilisateur] [-p mot de passe] [hôte [port]]`, commandes sur l'entrée standard : `open`, `user`, `ls`, `dir`, `cd`, `cdup`, `pwd`, `get`, `put`, `mget`, `mput`, `delete`, `mkdir`, `rmdir`, `rename`, `size`, `lcd`, `lpwd`, `quote`, `close`, `bye` ; passif, binaire, pas de FTPS. `sh` interactif (telnet) ne donne pas le clavier à ses programmes : `ftp hôte < commandes.txt`, ou un tube |
| `mount` | sans argument : les volumes ; `VOLUME:` le remonte (source de `mounts.txt`, ou `blk.usbNp1`) ; `VOLUME: source [service]` |
| `eject` | `[-f] [USBn:]` : les entrées des fichiers ouverts écrites (`FS_SYNC` de fatfs), refusé s'il en reste sans `-f`, puis démonté ; cartes jamais |
| `mkfs` | `[-t auto\|fat32] [-L étiquette] [-c octets] [-y] [-f] [--card] VOLUME:` : FAT32 seulement, écrit par le service bloc ; confirmation sur l'entrée standard sans `-y` (vide : rien n'est fait) ; `SD0:` jamais, la carte avec `--card` |

`echo`, `test`, `true`, `false` et `pwd` existent aussi dans `sh` : ces programmes servent à `make`,
qui lance directement une commande simple sans passer par le shell. `test` et la commande intégrée
partagent une seule source (`src/inc/testcmd.inc`).

**Expressions régulières** (`src/inc/regex.inc`) : le motif est compilé en une petite machine
(caractère, ensemble, choix, saut, capture), exécutée avec retour arrière. Syntaxe de base
(`\(`, `\)`, `\|`, `\+`, `\?`) ou étendue (`-E`), `.`, `[a-z]`, `[^x]`, `*`, `^`, `$`. Pas encore :
`{n,m}`, renvois `\1` dans le motif (ils marchent dans le remplacement de `sed`).

Pas encore : `awk`, `xargs`, `ps` et
`kill` en programmes (ils restent dans `sh`), et `sort -k`. Réseau et volumes : `nslookup` sans
autre serveur ni nom inverse, `mkfs` sans exFAT ni FAT16, `ftp` sans FTPS.

**Essai** : `/gcc/test/tooltest.sh` (`tools/gcc/test`) se vérifie lui-même. Sous QEMU (30/10/2026) :
71 PASS, 0 FAIL. Pas encore sur le Pi.

**Essai des commandes de novembre** (`zip`, `unzip`, `diff`, `ed`, `nl`, `hexdump`, `date`, `uname`, `df`,
`sysstat`, `volume`) : sous QEMU par telnet le 05/11/2026, une ou plusieurs lignes par option (`ed` d'après un
script sur l'entrée standard). Pas encore sur le Pi ; ni la question de `unzip` ni `ed` au clavier essayés.

Réseau et volumes : essayés sous QEMU (05/11/2026), contre des serveurs de l'hôte seulement (HTTP,
WHOIS et FTP d'essai sur 10.0.2.2) et une clé USB jetable (`--usbdisk`, formatée deux fois, éjectée et
remontée ; l'image relue sur le PC). Pas encore sur le Pi ; le renvoi `refer:` de `whois` (qui passe
par whois.iana.org) et `eject -f` avec des fichiers ouverts ne sont pas essayés.
