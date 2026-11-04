# ArchiveKit : compression et archives

**ArchiveKit** compresse et décompresse (deflate, zlib, gzip), calcule les sommes CRC-32 et Adler-32, lit les archives ZIP (ZIP64 compris), tar, tar.gz et gz, et écrit des ZIP, tar, tar.gz et gz. Il travaille sur la carte comme en mémoire : c'est par lui que Letters, Sheet et Slides ouvrent les `.docx`, `.xlsx`, `.pptx`, `.odt`... qui sont des ZIP. Le kit est en assembleur, dans `SD0:/lib/archivekit.so` ; il se sert d'AppKit pour les fichiers. L'app **Archiveur** et l'outil `arc` du Terminal sont bâtis dessus.

Un fichier abîmé ne fait jamais planter le kit : tailles et positions sont bornées par la source, chaque entrée est vérifiée par son CRC, et la fonction rend une erreur.

## Ouvrir le kit

```c
#include <archivekit_abi.h>

if (!archivekit_kit_open ())
	return 1;		/* SD0:/lib/archivekit.so absent */
```

Rien à ajouter au lien. Chaque argument est un `long` (un pointeur se convertit : `(long) buf`). Archives, écrivains et tampons du kit sont des **poignées opaques**. Un résultat négatif est une erreur ; `ar_strerror (e)` la dit en français.

| Erreur | Valeur | Sens |
|---|---|---|
| `AR_E_IO` | -5 | lecture ou écriture impossible |
| `AR_E_NOMEM` | -12 | plus de mémoire |
| `AR_E_CRYPT` | -13 | entrée chiffrée (ZipCrypto, AES : non pris en charge) |
| `AR_E_INVAL` | -22 | poignée, indice, enveloppe ou niveau invalide |
| `AR_E_FBIG` | -27 | trop grand pour le format (ZIP écrit : 4 Gio par entrée) |
| `AR_E_SPACE` | -28 | tampon de sortie trop petit |
| `AR_E_METHOD` | -38 | méthode inconnue (Deflate64, bzip2, LZMA) |
| `AR_E_CORRUPT` | -74 | données abîmées ou tronquées |
| `AR_E_CRC` | -84 | somme de contrôle fausse |
| `AR_E_FORMAT` | -95 | pas un format connu |
| `E_NOENT` | -2 | (d'AppKit) fichier ou entrée absent |

Deux threads peuvent travailler en même temps sur deux poignées ; une même archive ne sert qu'à un thread à la fois.

## Sommes de contrôle

| Fonction | Arguments | Résultat |
|---|---|---|
| `ar_crc32` | somme (0 au début, puis le dernier résultat), données, octets | CRC-32 (celui de ZIP, gzip, PNG) |
| `ar_adler32` | somme (1 au début, puis le dernier résultat), données, octets | Adler-32 (celui de zlib) |

Les sommes se calculent par morceaux : passer le résultat précédent au morceau suivant.

## Compresser un bloc de mémoire

L'**enveloppe** dit ce qui entoure le flux deflate : `AR_RAW` (0, le flux seul, comme dans une entrée de ZIP), `AR_ZLIB` (1, en-tête zlib et Adler-32, comme dans PNG ou PDF), `AR_GZIP` (2, en-tête gzip et CRC-32, un fichier `.gz`), et pour décompresser seulement `AR_AUTO` (3, zlib ou gzip d'après les premiers octets). Le **niveau** va de 0 (`AR_STORE`, rien de compressé) à 9 (`AR_BEST`) ; -1 vaut 6 (`AR_DEFAULT`).

| Fonction | Arguments | Résultat |
|---|---|---|
| `ar_deflate_bound` | octets, enveloppe | taille de sortie toujours suffisante |
| `ar_deflate` | source, octets, sortie, capacité, enveloppe, niveau | octets écrits ; `AR_E_SPACE` si la sortie est trop petite |
| `ar_inflate` | source, octets, sortie, capacité, enveloppe | octets écrits ; sommes vérifiées |
| `ar_inflate_size` | source, octets, enveloppe | taille décompressée (tout est décodé et vérifié) |

Le kit peut aussi fournir la sortie : il rend un **tampon** qu'on lit par `ar_data` et `ar_bytes` et qu'on rend par `ar_free`.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ar_deflate_new` | source, octets, enveloppe, niveau | tampon |
| `ar_inflate_new` | source, octets, enveloppe, taille prévue (0 : inconnue) | tampon ; un 0 suit les données (un texte s'utilise tel quel) |
| `ar_data` | tampon (ou écrivain en mémoire après `ar_finish`) | pointeur sur les octets |
| `ar_bytes` | tampon (ou écrivain après `ar_finish`) | nombre d'octets |
| `ar_free` | tampon, écrivain ou archive | 0 (0 : sans effet) |

## Lire une archive

Le format est reconnu aux premiers octets : ZIP (avec ou sans données devant, ZIP64, descripteurs de données), tar (ustar, GNU, pax), tar.gz, gz (plusieurs membres compris). Les noms en CP437 des vieux ZIP sont convertis en UTF-8.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ar_open` | chemin | archive |
| `ar_open_mem` | tampon, octets | archive ; le tampon reste à l'appelant, intact jusqu'à `ar_close` |
| `ar_close` | archive | 0 (0 : sans effet) |
| `ar_probe` | chemin | `AR_FMT_*` d'après les premiers octets, sans tout lire ; `AR_E_FORMAT` sinon |
| `ar_format` | archive | `AR_FMT_ZIP` (1), `AR_FMT_TAR` (2), `AR_FMT_TGZ` (3), `AR_FMT_GZIP` (4) |
| `ar_count` | archive | nombre d'entrées |
| `ar_entry` | archive, indice, `long info[8]` | 0 ; la fiche de l'entrée (ci-dessous) |
| `ar_name` | archive, indice | nom (UTF-8, `/` entre les parties, valable jusqu'à `ar_close`) |
| `ar_entry_size` | archive, indice | taille d'origine |
| `ar_find` | archive, nom | indice : nom exact d'abord, puis sans tenir compte de la casse ASCII ; `E_NOENT` |
| `ar_read` | archive, indice, tampon, capacité | octets lus ; CRC vérifié ; `AR_E_SPACE` : prévoir `ar_entry_size` octets |
| `ar_load` | archive, indice | tampon du kit (`ar_data`, `ar_bytes`, `ar_free`) ; un 0 suit les données |
| `ar_extract` | archive, indice, chemin | octets écrits ; dossiers du chemin créés, date posée, rien ne reste en cas d'erreur |
| `ar_extract_all` | archive, dossier | fichiers écrits ; les noms qui remontent par `..` sont sautés |
| `ar_test` | archive, indice | 0 : l'entrée se lit et sa somme est bonne |

La fiche de `ar_entry` (`long info[8]`, constantes `ARE_*` en octets) : `info[0]` nom (pointeur), `info[1]` taille d'origine, `info[2]` taille dans l'archive, `info[3]` date (secondes UTC depuis 1970, 0 : aucune), `info[4]` 1 pour un dossier, `info[5]` méthode (0 stocké, 8 deflate), `info[6]` CRC-32 annoncé, `info[7]` drapeaux (`ARF_ENCRYPTED`).

## Créer une archive

| Fonction | Arguments | Résultat |
|---|---|---|
| `ar_create` | chemin, format | écrivain ; le fichier est remplacé |
| `ar_create_mem` | format | écrivain en mémoire |
| `ar_add_mem` | écrivain, nom, données, octets, niveau | 0 |
| `ar_add_file` | écrivain, nom (0 : celui du fichier), chemin, niveau | 0 ; un dossier : lui et tout ce qu'il contient |
| `ar_add_dir` | écrivain, nom | 0 : une entrée dossier |
| `ar_set_time` | écrivain, secondes UTC (0 : maintenant) | 0 ; pour les entrées suivantes |
| `ar_finish` | écrivain | octets de l'archive ; le fichier est fermé ; en mémoire : `ar_data`, `ar_bytes` |

Après `ar_finish`, l'écrivain se rend par `ar_free` ; un `ar_free` sans `ar_finish` abandonne l'archive. `AR_FMT_GZIP` ne prend qu'une entrée. Une archive existante ne se modifie pas : on en écrit une nouvelle.

Au niveau 0, une entrée est stockée telle quelle : c'est ainsi que se met le `mimetype` d'un document ODF, en **première** entrée. Une entrée que deflate ne réduit pas est stockée d'office.

## Le kit et ses formats

| Fonction | Arguments | Résultat |
|---|---|---|
| `ar_formats` | tableau de `ARFI_SIZE` (128) octets par format (0 : compter), places | nombre de formats |
| `ar_format_name` | `AR_FMT_*` | « ZIP », « TAR », « TAR.GZ », « GZIP », 0 si inconnu |
| `ar_strerror` | erreur négative | texte en français |

Une fiche de `ar_formats` : identifiant (`ARFI_ID`, `long`), lu (`ARFI_READ`), écrit (`ARFI_WRITE`), nom (`ARFI_NAME`, 16 octets), extensions séparées par un blanc (`ARFI_EXT`, 88 octets, « zip docx xlsx... »).

## Exemple : compresser en mémoire et écrire un .gz

Compresse un texte en gzip, le décompresse pour vérifier, compare les CRC, puis écrit le résultat dans `/tmp/demo.txt.gz` (le `/tmp` est en mémoire, `RAM0:/tmp`).

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <archivekit_abi.h>

int main (void)
{
	if (!archivekit_kit_open ())
		return 1;
	/* un texte qui se repete : il se comprime bien */
	static char text[20000];
	for (int i = 0; i < 400; i++)
		strcat (text, "Korin OS compresse avec ArchiveKit. ");
	long n = (long) strlen (text);

	long cap = ar_deflate_bound (n, AR_GZIP);
	unsigned char *gz = malloc ((size_t) cap);
	long packed = ar_deflate ((long) text, n, (long) gz, cap, AR_GZIP, AR_DEFAULT);
	if (packed < 0)
	{
		printf ("deflate : %s\n", (const char *) ar_strerror (packed));
		return 1;
	}
	printf ("%ld octets -> %ld (%ld %%)\n", n, packed, packed * 100 / n);

	/* la taille d'origine, puis la decompression elle-meme */
	long size = ar_inflate_size ((long) gz, packed, AR_AUTO);
	char *back = malloc ((size_t) size + 1);
	long got = ar_inflate ((long) gz, packed, (long) back, size, AR_AUTO);
	printf ("inflate : %ld octets, %s\n", got, got == n && !memcmp (back, text, (size_t) n) ? "identiques" : "DIFFERENTS");
	printf ("CRC-32 %08lx, Adler-32 %08lx\n", ar_crc32 (0, (long) text, n), ar_adler32 (1, (long) text, n));

	/* la meme chose, le kit fournit le tampon */
	long b = ar_deflate_new ((long) text, n, AR_ZLIB, AR_BEST);
	if (b > 0)
	{
		printf ("zlib au niveau 9 : %ld octets\n", ar_bytes (b));
		ar_free (b);
	}

	FILE *f = fopen ("/tmp/demo.txt.gz", "wb");
	if (f)
	{
		fwrite (gz, 1, (size_t) packed, f);
		fclose (f);
		printf ("/tmp/demo.txt.gz : %s\n", (const char *) ar_format_name (ar_probe ((long) "/tmp/demo.txt.gz")));
	}
	free (gz);
	free (back);
	return 0;
}
```

## Exemple : créer un ZIP, le lister, l'extraire

Écrit `/tmp/demo.zip` (un dossier, deux fichiers), puis le rouvre : liste des entrées, lecture d'une entrée par son nom, vérification, extraction complète dans `/tmp/demo`.

```c
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <archivekit_abi.h>

#define ZIP "/tmp/demo.zip"

static int fail (const char *what, long e)
{
	printf ("%s : %s\n", what, (const char *) ar_strerror (e));
	return 1;
}

int main (void)
{
	if (!archivekit_kit_open ())
		return 1;
	const char *readme = "Une archive ecrite par ArchiveKit.\n";
	const char *data = "a;b;c\n1;2;3\n";

	long w = ar_create ((long) ZIP, AR_FMT_ZIP);
	if (w < 0)
		return fail ("ar_create", w);
	ar_set_time (w, 0);					/* l'heure courante */
	ar_add_mem (w, (long) "readme.txt", (long) readme, (long) strlen (readme), AR_DEFAULT);
	ar_add_dir (w, (long) "data");
	ar_add_mem (w, (long) "data/table.csv", (long) data, (long) strlen (data), AR_BEST);
	long total = ar_finish (w);
	ar_free (w);
	if (total < 0)
		return fail ("ar_finish", total);
	printf ("%s : %ld octets\n", ZIP, total);

	long a = ar_open ((long) ZIP);
	if (a < 0)
		return fail ("ar_open", a);
	printf ("format %s, %ld entrees\n", (const char *) ar_format_name (ar_format (a)), ar_count (a));
	for (long i = 0; i < ar_count (a); i++)
	{
		long info[8];
		ar_entry (a, i, (long) info);
		time_t t = (time_t) info[ARE_TIME / 8];
		char when[32];
		strftime (when, sizeof when, "%Y-%m-%d %H:%M", gmtime (&t));
		printf ("  %-16s %6ld -> %6ld  %s %s%s\n", (const char *) info[ARE_NAME / 8], info[ARE_SIZE / 8],
			info[ARE_PACKED / 8], when, info[ARE_DIR / 8] ? "(dossier)" : "",
			ar_test (a, i) == 0 ? "" : " ABIMEE");
	}

	long i = ar_find (a, (long) "DATA/TABLE.CSV");		/* sans tenir compte de la casse */
	if (i >= 0)
	{
		long b = ar_load (a, i);
		printf ("%s :\n%s", (const char *) ar_name (a, i), (const char *) ar_data (b));
		ar_free (b);
	}
	long files = ar_extract_all (a, (long) "/tmp/demo");
	printf ("%ld fichier(s) extraits dans /tmp/demo\n", files);
	ar_close (a);
	return 0;
}
```

## Exemple : un ZIP en mémoire

Un document OOXML ou ODF arrive souvent en mémoire (lu d'un fichier, reçu du réseau). Ici, un ZIP est fabriqué en mémoire, puis relu par `ar_open_mem`, comme Letters lit un `.docx`. La liste des formats connus du kit termine.

```c
#include <stdio.h>
#include <string.h>
#include <archivekit_abi.h>

int main (void)
{
	if (!archivekit_kit_open ())
		return 1;
	/* un paquet minimal : mimetype stocke en premier, comme ODF le veut */
	const char *mime = "application/vnd.oasis.opendocument.text";
	const char *body = "<office:document-content/>";
	long w = ar_create_mem (AR_FMT_ZIP);
	ar_add_mem (w, (long) "mimetype", (long) mime, (long) strlen (mime), AR_STORE);
	ar_add_mem (w, (long) "content.xml", (long) body, (long) strlen (body), AR_DEFAULT);
	long n = ar_finish (w);
	if (n < 0)
	{
		printf ("%s\n", (const char *) ar_strerror (n));
		ar_free (w);
		return 1;
	}
	const unsigned char *zip = (const unsigned char *) ar_data (w);
	printf ("ZIP en memoire : %ld octets ; 'mimetype' a l'octet 30 : %.8s\n", ar_bytes (w), (const char *) zip + 30);

	long a = ar_open_mem ((long) zip, n);	/* le tampon doit vivre jusqu'a ar_close */
	long i = ar_find (a, (long) "content.xml");
	char xml[128];
	long got = ar_read (a, i, (long) xml, sizeof xml - 1);
	if (got >= 0)
	{
		xml[got] = 0;
		printf ("content.xml : %s\n", xml);
	}
	ar_close (a);
	ar_free (w);					/* l'ecrivain, et ses octets avec lui */

	unsigned char fm[8][ARFI_SIZE];
	long k = ar_formats ((long) fm, 8);
	for (long j = 0; j < k; j++)
		printf ("%-7s lu %s, ecrit %s : %s\n", (char *) fm[j] + ARFI_NAME,
			*(long *) (fm[j] + ARFI_READ) ? "oui" : "non", *(long *) (fm[j] + ARFI_WRITE) ? "oui" : "non",
			(char *) fm[j] + ARFI_EXT);
	return 0;
}
```

## Pièges

- **`ar_open_mem` ne copie pas** : le tampon doit rester intact jusqu'à `ar_close`. De même, les noms rendus par `ar_name` et `ARE_NAME` meurent avec l'archive.
- **Rendre ce que le kit donne** : chaque tampon (`ar_load`, `ar_*_new`) et chaque écrivain se rendent par `ar_free`, chaque archive par `ar_close`.
- **Un tar.gz est décompressé en mémoire** à l'ouverture : sa taille est bornée par la mémoire libre. Un `.gz` d'un seul fichier, lui, est décodé à la volée.
- **Dates** : une date ZIP n'a pas de fuseau ; elle est lue comme UTC.
- **Pas de chiffrement** : une entrée chiffrée se liste, mais sa lecture rend `AR_E_CRYPT`.
- **ZIP64 se lit, ne s'écrit pas** : au-delà de 4 Gio par entrée, `AR_E_FBIG`.

## Voir aussi

- [DocumentKit](documentkit_fr.md) : lire le XML d'un `.docx` ou d'un `.odt` une fois la partie extraite.
- [PrinterKit](printerkit_fr.md), qui compresse les images des travaux par ArchiveKit.
- [libc et couche POSIX](libc_fr.md) : fichiers et chemins (`/tmp`, `SD0:/...`).
