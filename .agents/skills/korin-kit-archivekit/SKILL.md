---
name: korin-kit-archivekit
description: ArchiveKit de Korin OS (fonctions ar_*) — deflate/inflate, zlib, gzip, CRC-32, Adler-32, lecture de ZIP (ZIP64), tar, tar.gz, gz sur la carte ou en mémoire, écriture de ZIP, tar, tar.gz, gz. À charger pour compresser, décompresser, lister, extraire ou créer une archive, ou ouvrir le ZIP d'un .docx/.xlsx/.odt.
---

# ArchiveKit : compression et archives

Kit en assembleur, `/lib/archivekit.so` (36 fonctions), qui importe AppKit pour les fichiers. L'app Archiveur et
la commande `arc` sont bâties dessus. Un fichier abîmé ne fait jamais planter le kit : la fonction rend une erreur.

## Où lire

| Sujet | Sur la carte | Dans le dépôt |
|---|---|---|
| Guide complet (3 exemples vérifiés, pièges) | `/apps/help.app/api/archivekit_fr.md` | `src/apps/help/data/api/archivekit_fr.md` |
| En-tête C (prototypes et constantes) | `/devtools/gcc/include/kits/archivekit_abi.h` | généré : `build/gcc/sdroot/gcc/include/kits/archivekit_abi.h` |
| Table ABI (arguments de chaque fonction) | `/src/kits/archivekit/archivekit.abi` | `src/kits/archivekit/archivekit.abi` |
| Constantes (asm) | `/src/kits/archivekit/archivekit.inc` | `src/kits/archivekit/archivekit.inc` |
| Une vraie app | — | `src/apps/archiver/main.cpp`, `src/bin/arc/arc.asm` |

## Ouvrir le kit

C / C++ : `#include <archivekit_abi.h>`, puis `if (!archivekit_kit_open ()) return 1;` (0 : `.so` absent).
Construire par `pibuild` (il ajoute `-I/devtools/gcc/include/kits` et toutes les `lib<kit>.a`) ou à la main :
`gcc -I/devtools/gcc/include/kits prog.c -larchivekit -o prog`.

Assembleur : `include '../../kits/archivekit/archivekit.inc'` (constantes), dans le code
`kit_use '../../kits/appkit/appkit.abi'` puis `kit_use '../../kits/archivekit/archivekit.abi'`, `bl kits_open` (ou
`bl t_debut`) au départ, `kit_data` dans les variables. Les noms sont les mêmes dans tous les langages ; les skills
`korin-lang-*` expliquent le mécanisme.

Tous les arguments sont des `long` (pointeur : `(long) p`). Archives, écrivains et tampons sont des **poignées
opaques**. Résultat négatif = erreur ; `ar_strerror (e)` rend le texte en français.

## L'essentiel

Constantes — enveloppe : `AR_RAW` 0, `AR_ZLIB` 1, `AR_GZIP` 2, `AR_AUTO` 3 (décompression seulement). Niveau : 0
`AR_STORE` … 9 `AR_BEST`, -1 ou `AR_DEFAULT` = 6. Formats : `AR_FMT_ZIP` 1, `AR_FMT_TAR` 2, `AR_FMT_TGZ` 3,
`AR_FMT_GZIP` 4. Erreurs : `AR_E_IO` -5, `AR_E_NOMEM` -12, `AR_E_CRYPT` -13, `AR_E_INVAL` -22, `AR_E_FBIG` -27,
`AR_E_SPACE` -28, `AR_E_METHOD` -38, `AR_E_CORRUPT` -74, `AR_E_CRC` -84, `AR_E_FORMAT` -95, `E_NOENT` -2.

Sommes : `ar_crc32 (crc, données, octets)` (0 au début), `ar_adler32 (adler, données, octets)` (1 au début) ; par
morceaux en repassant le résultat.

Mémoire → mémoire :
- `ar_deflate_bound (octets, env)` : taille de sortie toujours suffisante.
- `ar_deflate (src, n, out, cap, env, niveau)` / `ar_inflate (src, n, out, cap, env)` : octets écrits.
- `ar_inflate_size (src, n, env)` : taille décompressée (décode tout pour la trouver).
- `ar_deflate_new (src, n, env, niveau)`, `ar_inflate_new (src, n, env, taille prévue|0)` : tampon du kit.
- `ar_data (t)`, `ar_bytes (t)`, `ar_free (t)` : lire puis rendre un tampon (aussi un écrivain, une archive).

Lire une archive (format reconnu aux premiers octets) :
- `ar_open (chemin)`, `ar_open_mem (tampon, octets)`, `ar_close (a)` ; `ar_probe (chemin)` → `AR_FMT_*` sans tout lire.
- `ar_format (a)`, `ar_count (a)`, `ar_name (a, i)`, `ar_entry_size (a, i)`.
- `ar_entry (a, i, (long) info)` avec `long info[8]` : indices `ARE_x / 8` (`ARE_NAME`, `ARE_SIZE`, `ARE_PACKED`,
  `ARE_TIME` secondes UTC, `ARE_DIR`, `ARE_METHOD` 0/8, `ARE_CRC`, `ARE_FLAGS` & `ARF_ENCRYPTED`).
- `ar_find (a, nom)` : indice (exact, puis sans casse ASCII) ou `E_NOENT`.
- `ar_read (a, i, buf, cap)`, `ar_load (a, i)` (tampon du kit, un 0 suit les données), `ar_test (a, i)`.
- `ar_extract (a, i, chemin)`, `ar_extract_all (a, dossier)` (saute les noms en `..`).

Écrire : `ar_create (chemin, fmt)` (fichier remplacé) ou `ar_create_mem (fmt)` ; `ar_set_time (w, s|0)`,
`ar_add_mem (w, nom, données, n, niveau)`, `ar_add_file (w, nom|0, chemin, niveau)` (un dossier : récursif),
`ar_add_dir (w, nom)` ; `ar_finish (w)` → taille ; puis **`ar_free (w)`**. Divers : `ar_format_name (fmt)`,
`ar_formats (tableau, places)` (fiches de `ARFI_SIZE` = 128 octets).

## Exemple

D'après `archivekit_fr.md` (« créer un ZIP, le lister, l'extraire ») et `src/apps/archiver/main.cpp` :

```c
#include <stdio.h>
#include <string.h>
#include <archivekit_abi.h>

int main (void)
{
	if (!archivekit_kit_open ())
		return 1;
	const char *txt = "Bonjour\n";
	long w = ar_create ((long) "/tmp/demo.zip", AR_FMT_ZIP);
	if (w < 0)
		return printf ("%s\n", (const char *) ar_strerror (w)), 1;
	ar_set_time (w, 0);					/* maintenant */
	ar_add_mem (w, (long) "a.txt", (long) txt, (long) strlen (txt), AR_DEFAULT);
	long total = ar_finish (w);
	ar_free (w);						/* toujours, après ar_finish */
	if (total < 0)
		return 1;

	long a = ar_open ((long) "/tmp/demo.zip");
	if (a < 0)
		return 1;
	for (long i = 0; i < ar_count (a); i++)
	{
		long info[8];
		ar_entry (a, i, (long) info);
		printf ("%s %ld -> %ld\n", (const char *) info[ARE_NAME / 8], info[ARE_SIZE / 8], info[ARE_PACKED / 8]);
	}
	long i = ar_find (a, (long) "a.txt");
	if (i >= 0)
	{
		long b = ar_load (a, i);			/* un 0 suit les données */
		printf ("%s", (const char *) ar_data (b));
		ar_free (b);
	}
	ar_extract_all (a, (long) "/tmp/demo");
	ar_close (a);
	return 0;
}
```

## Pièges

- `ar_open_mem` **ne copie pas** : le tampon doit rester intact jusqu'à `ar_close`. Les noms (`ar_name`, `ARE_NAME`)
  meurent avec l'archive.
- Rendre chaque tampon (`ar_load`, `ar_*_new`) et chaque écrivain par `ar_free`, chaque archive par `ar_close`. Un
  `ar_free` sans `ar_finish` abandonne l'archive.
- Les `ARE_*` sont des **décalages en octets** : `info[ARE_SIZE / 8]`, pas `info[ARE_SIZE]`.
- `ar_read` rend `AR_E_SPACE` si le tampon est plus petit que `ar_entry_size`.
- Une archive existante ne se modifie pas : en écrire une nouvelle (l'Archiveur recopie les entrées par
  `ar_load` + `ar_add_mem`). `AR_FMT_GZIP` ne prend qu'une entrée.
- Un tar.gz est entièrement décompressé en mémoire à l'ouverture ; un `.gz` simple est décodé à la volée.
- Pas de chiffrement (`AR_E_CRYPT`) ; ZIP64 se lit mais ne s'écrit pas (`AR_E_FBIG` au-delà de 4 Gio par entrée) ;
  Deflate64, bzip2, LZMA : `AR_E_METHOD`.
- ODF : l'entrée `mimetype` doit être la **première**, au niveau `AR_STORE`.
- Une même archive ne sert qu'à un thread à la fois (deux poignées : deux threads possibles).
- Un programme ne peut pas définir une fonction de même nom qu'une fonction du kit (`ar_*`).
