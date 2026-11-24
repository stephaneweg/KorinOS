# Bogue : chmod +x échoue en « No space left on device » (index .exec plein)

Relevé le 24/11/2026 sur Korin PC (ZBook, paquet pc 2026.11.98), pendant le port de llama.cpp et d'OpenJarvis
(`tools/llamacpp`, `tools/openjarvis`). Le disque n'était pas plein (400 Go libres sur `/home`).

## Ce qu'on voit

Sur `/home` (ext4), tout `chmod` qui ajoute le bit d'exécution échoue avec `ENOSPC` :

```
$ echo x > f; chmod 755 f
chmod: f : inaccessible
$ python3 -c "import os; os.chmod('f', 0o755)"
OSError: [Errno 28] No space left on device: 'f'
```

- **pip** échoue à chaque installation qui crée un script (`pip/_vendor/distlib/util.py`, `set_mode` → `os.chmod`) :
  `ERROR: Could not install packages due to an OSError: [Errno 28] No space left on device: '.../bin/trove-classifiers'` ;
- **`fchmod`** échoue de même : l'outil d'édition de Claude Code n'arrivait plus à réécrire un fichier
  (`ENOSPC: no space left on device, fchmod`) ;
- les `chmod` sans bit `x` réussissent ; le mode lu ensuite est toujours `700` ou `600` (attendu : il est recomposé
  à partir de l'index, ci-dessous).

Après la suppression de ~2 Go de clones et d'environnements Python (`rm -rf`), tout est revenu à la normale.

## La cause

`chmod` ne touche pas le système de fichiers : libposix (`src/lib/posix/stat.c`, `__posix_chmod_path`) envoie
`V_CHMOD` à vfs, et vfs (`src/svc/vfs/exec.c`, `idx_chmod`) **ajoute le chemin à l'index des exécutables du volume**
(`<racine du volume>/.exec`, un chemin par ligne), ou l'en retire. Cet index est tenu en mémoire avec une taille fixe :

```c
#define IDX_CAP 32768 /* octets de l'index d'un volume */
```

Plein, `idx_ajouter` rend `E_NOSPC` (exec.c, « -> 1 ajouté, 0 déjà là, E_NOSPC (index plein) »), que libposix
transmet tel quel : `ENOSPC`, « No space left on device ».

32 Ko, c'est quelques centaines de chemins (les chemins de `/home` font ~80 octets). Ce qui l'a rempli en une
session :
- deux clones de llama.cpp : git rend exécutables, par `chmod`, tous les fichiers `100755` du dépôt ;
- un clone d'OpenJarvis (de même) ;
- deux environnements `pip --user` : un script exécutable par commande installée (`pyenv/bin/*`).

Après les `rm -rf` (vfs retire les chemins d'un dossier effacé), `/home/.exec` était retombé à 7 650 octets,
101 lignes.

## Ce qu'il faut changer (demande de l'utilisateur)

**L'index `.exec` ne doit plus servir qu'aux volumes FAT (FAT32, exFAT), qui n'ont pas de bit d'exécution. Pour un
volume ext4, le bit `x` est celui de l'inode : vfs le demande et le règle par ext4fs.**

1. **ext4fs** (`src/svc/ext4fs/ext4fs.c`, `src/lib/ext4/ext4w.c`) :
   - une opération de changement de mode (un nouveau `FS_CHMOD` dans `src/inc/fs.inc`, ajouté à la fin des
     opérations) : `ext4w_chmod(path, mode)` sur le modèle de `ext4w_utime` (lire l'inode, remplacer les 12 bits
     bas de `i_mode` en gardant le type `0xF000`, mettre `i_ctime`, réécrire l'inode, `sb_write`) ;
   - le bit `x` de l'inode dans la réponse de stat (`ATTR_EXEC` quand `i_mode & 0111`), pour que `V_STATX` et
     `V_EXEC` n'aient pas à consulter l'index.
2. **vfs** (`src/svc/vfs/exec.c`, `path.c`) : le volume dit son service (`struct volume`, champ `fs` : « ext4fs ») :
   - `V_CHMOD` sur un volume ext4 : `FS_CHMOD` au service, pas d'index ;
   - « est-ce exécutable ? » sur un volume ext4 : les règles de chemin (`/bin`, `/sys`, `/apps/*.app/main`,
     `/etc/exec.txt`) d'abord, comme aujourd'hui, puis le bit de l'inode au lieu de l'index ;
   - l'index reste pour FAT32 et exFAT (et ramfs, qui n'a pas de mode à lui, à décider).
3. **Reprise des index existants** : `/.exec` et `/home/.exec` sont sur des volumes ext4. À la première lecture,
   appliquer leurs lignes aux inodes (`FS_CHMOD` +x), puis ne plus écrire l'index de ce volume (le retirer, ou le
   garder lu en secours pour les chemins dont l'inode n'a pas de `x`).
4. **Pour FAT** : un index qui grandit (ou bien plus grand), et un refus plus parlant que `ENOSPC`, qui fait croire
   à un disque plein (un message dans le journal du noyau : « vfs : index des exécutables de VOL plein »).

Bénéfices : `git clone` et `pip install` sur `/home` gardent leurs bits `x` sans limite, `stat` montre le vrai mode
d'un fichier ext4, et un dépôt cloné sous Korin puis copié ailleurs garde ses droits.

## Reproduire

Sur un volume ext4, remplir l'index puis demander un bit `x` :

```bash
mkdir -p ~/exectest && cd ~/exectest
for i in $(seq 1 600); do echo x > "fichier-au-nom-assez-long-pour-remplir-l-index-$i"; chmod +x "fichier-au-nom-assez-long-pour-remplir-l-index-$i" || { echo "échec au fichier $i"; break; }; done
wc -c /home/.exec          # proche de 32768
cd && rm -rf ~/exectest    # l'index se vide, chmod +x remarche
```

Après la correction : la boucle va au bout sur `/home`, `/home/.exec` ne grandit pas, `stat -c %a` (ou
`ls -l`) montre `rwx` ; sur une clé FAT, le comportement reste celui de l'index.

## Pièges voisins vus le même jour

- pip tue `rustc --version` (son User-Agent, délai de 0,5 s) : ce SIGKILL pendant une lecture a fait tomber ext4fs
  deux fois (adresse 0 inaccessible) ; corrigé dans pc 2026.11.98.
- Les commits sous Korin : git n'a pas d'identité configurée sur le ZBook.
