---
name: armpi-stdio-tampon
description: "libposix : fread lent sur les gros fichiers (tampon newlib de 1 Ko) corrigé le 20/11 par stdiobuf.c (64 Ko pour un fichier régulier) ; vaut seulement pour les programmes reliés ensuite"
metadata:
  type: project
---
Le 20/11/2026, `cdmhost` mettait 11 s à lire le module Widevine (21,7 Mo) par `fread`. Cause : la newlib de Korin
(4.4.0) n'a pas `HAVE_BLKSIZE`, donc le tampon de chaque `FILE` faisait `BUFSIZ` (1 Ko sur le PC), quel que soit
`st_blksize` : un `read ()` de 1 Ko par remplissage. Corrigé dans libposix (df813c57) : `src/lib/posix/stdiobuf.c`
remplace `__smakebuf_r` et `__swhatbuf_r` de `makebuf.o` (64 Ko pour un fichier régulier, `st_blksize` = 64 Ko dans
`stat.c`), et `start.c` y fait référence pour que la version de libposix soit toujours liée. Mesure
(`src/lib/posix/test/fbench.c`, QEMU PC) : 4 à 7 s → 0,58 s ; `posixtest` passe (PC), libposix du Pi se construit
(pas essayée sur le Pi).

**Why:** libposix est liée statiquement : le gain n'arrive qu'aux programmes reliés après (Python, GCC, les apps du PC
et du Pi), et les paquets ne le portent qu'une fois reconstruits et publiés.

**How to apply:** un programme lent sur de gros fichiers lus par stdio : vérifier qu'il a été relié avec une libposix
d'après le 20/11 (`nm prog | grep __posix_stdiobuf_keep`). Pour mesurer, `fbench FICHIER`. Banc sous QEMU : mettre
`posixtest` dans `/bin` (il se relance lui-même par ce chemin : ailleurs, les groupes mem, proc et ipc échouent).
Voir [[armpi-posix-par-necessite]], [[armpi-widevine]], [[armpi-build-world-pc]].
