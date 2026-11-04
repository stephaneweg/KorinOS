# DocumentKit : la plomberie des formats bureautiques

**DocumentKit** rassemble ce que les applications de bureautique ont toutes à refaire : un lecteur XML par événements et l'échappement du XML, les conversions UTF-8, CP1252 et Latin-1, les longueurs d'Office et d'ODF (EMU, twips, points, centimètres...), les couleurs, les dates civiles et les numéros de série d'Excel, le CSV, la reconnaissance d'un document (DOCX, XLSX, PPTX, ODT, ODS, ODP, RTF) et les relations et enveloppes des paquets OOXML et ODF. Letters, Sheet et Slides s'en servent.

Le kit est en assembleur, dans `SD0:/lib/documentkit.so`. Il ne travaille que sur des **tampons** que l'appelant possède : pas de fichier, pas de tas, aucun autre kit. Le ZIP est l'affaire d'[ArchiveKit](archivekit_fr.md) : on extrait la partie (`word/document.xml`) avec lui, puis on la passe à DocumentKit.

## Ouvrir le kit

```c
#include <documentkit_abi.h>

if (!documentkit_kit_open ())
	return 1;		/* SD0:/lib/documentkit.so absent */
```

Rien à ajouter au lien. Chaque argument est un `long` ; les lecteurs (XML, CSV) sont des poignées, de petits entiers. Les erreurs sont négatives :

| Erreur | Valeur | Sens |
|---|---|---|
| `DKE_NOTFOUND` | -2 | attribut, relation, caractère absent |
| `DKE_BADF` | -9 | poignée inconnue |
| `DKE_NOMEM` | -12 | plus de poignée : 64 lecteurs ouverts dans le programme |
| `DKE_INVAL` | -22 | argument invalide |
| `DKE_CORRUPT` | -74 | UTF-8 invalide |

**Sortie texte.** Toutes les fonctions qui écrivent du texte prennent `out, cap` en derniers arguments et se comportent comme `snprintf` : elles rendent la longueur **complète** sans le 0 final, écrivent au plus `cap - 1` octets puis un 0. Avec `cap = 0`, rien n'est écrit : on obtient la longueur pour dimensionner le tampon.

## Lecteur XML

Le lecteur avance d'événement en événement : début d'élément, fin d'élément, texte. Il suit le comportement du lecteur d'Onyx, y compris sur du XML mal formé (il ne s'arrête pas). Commentaires, déclarations et DOCTYPE sont sautés ; une section CDATA donne un événement texte brut ; `<a/>` donne un début puis une fin.

| Fonction | Arguments | Résultat |
|---|---|---|
| `dk_xml_open` | tampon, octets | lecteur ; le tampon reste à l'appelant jusqu'à `dk_xml_close` |
| `dk_xml_close` | lecteur | 0 |
| `dk_xml_next` | lecteur | l'événement suivant : `DKX_EOF` (0), `DKX_START` (1), `DKX_END` (2), `DKX_TEXT` (3) |
| `dk_xml_event` | lecteur | l'événement courant |
| `dk_xml_empty` | lecteur | 1 : ce début vient d'un `<a/>` |
| `dk_xml_is` | lecteur, nom | 1 : l'élément porte ce nom, sans son préfixe (`w:p` est `p`) |
| `dk_xml_isq` | lecteur, nom | 1 : nom exact, préfixe compris (`text:p`) |
| `dk_xml_name` | lecteur, out, cap | longueur du nom (avec son préfixe) |
| `dk_xml_attr` | lecteur, nom, out, cap | longueur de la valeur décodée ; `DKE_NOTFOUND` |
| `dk_xml_attr_int` | lecteur, nom, défaut | entier (absent : le défaut) |
| `dk_xml_attr_bool` | lecteur, nom, défaut | 1 si la valeur commence par `1`, `t` ou `T` |
| `dk_xml_attr_on` | lecteur, nom (0 : `val`) | interrupteur de Word : absent, `1`, `true`, `on` donnent 1 ; `0`, `false`, `off`, `no` donnent 0 |
| `dk_xml_text` | lecteur, out, cap | longueur du texte décodé (entités, références numériques, CDATA) |
| `dk_xml_skip` | lecteur | 0 ; après un début, passe tout l'élément, fin comprise |
| `dk_xml_pos` | lecteur | octets lus depuis le début du tampon |
| `dk_xml_esc` | texte, octets (< 0 : jusqu'au 0), out, cap | longueur ; `&`, `<`, `>`, `"` échappés, caractères de contrôle retirés (sauf tabulation, LF, CR) |
| `dk_xml_esc_attr` | texte, octets, out, cap | comme `dk_xml_esc`, plus tabulation, LF et CR en références numériques (pour une valeur d'attribut) |

Un attribut demandé **sans** préfixe se trouve aussi sous n'importe quel préfixe (sauf `xmlns`) : `dk_xml_attr (r, "val", ...)` lit `w:val`. Avec un préfixe, le nom doit être exact. Une entité inconnue est laissée telle quelle, `&#0;` est retiré, une référence invalide devient U+FFFD.

## UTF-8, CP1252, Latin-1

| Fonction | Arguments | Résultat |
|---|---|---|
| `dk_utf8_encode` | point de code, out (4 octets, sans 0 final) | octets écrits ; invalide : U+FFFD |
| `dk_utf8_decode` | texte, octets, `unsigned *cp` | octets lus ; 0 : fin ; `DKE_CORRUPT` : suite invalide |
| `dk_utf8_valid` | texte, octets | 1 : UTF-8 valide |
| `dk_utf8_count` | texte, octets | nombre de points de code |
| `dk_utf8_to_cp` | texte, octets, `unsigned out[]`, places | points écrits (un octet invalide est lu comme du Latin-1) |
| `dk_cp_to_utf8` | `unsigned cp[]`, nombre, out, cap | longueur |
| `dk_latin1_to_utf8` | texte, octets, out, cap | longueur |
| `dk_cp1252_to_utf8` | texte, octets, out, cap | longueur |
| `dk_utf8_to_latin1` | texte, octets, out, cap, remplaçant (0 : retiré) | longueur |
| `dk_utf8_to_cp1252` | texte, octets, out, cap, remplaçant (0 : retiré) | longueur |
| `dk_cp1252_cp` | octet | point de code |
| `dk_cp_cp1252` | point de code | octet, ou `DKE_NOTFOUND` |

Le décodage est strict : ni suite trop longue, ni moitié de paire UTF-16, ni point au-delà de U+10FFFF. Les cinq octets sans caractère de CP1252 valent eux-mêmes (comme le fait le WHATWG).

## Longueurs et couleurs

Les unités : `DKU_EMU` (1/914 400 pouce), `DKU_TWIP` (1/20 point), `DKU_HMM` (1/100 mm), `DKU_PT`, `DKU_IN`, `DKU_CM`, `DKU_MM`, `DKU_PC` (pica, 12 points), `DKU_PX` (1/96 pouce). Les conversions sont exactes, arrondies au plus près, en passant par l'EMU.

| Fonction | Arguments | Résultat |
|---|---|---|
| `dk_len_parse` | texte (« 2.54cm », « 3,5mm », « 12pt »), unité voulue, unité d'un nombre nu, défaut | entier arrondi ; pas de nombre, ou un `%` : le défaut |
| `dk_len_conv` | valeur, son unité, unité voulue | entier arrondi |
| `dk_len_write` | valeur, son unité, unité écrite, décimales (0 à 6), out, cap | longueur (« 1.2700cm ») |
| `dk_color_parse` | texte (`#RRGGBB`, `RRGGBB`, `AARRGGBB`), défaut | `0xRRGGBB` ; `auto` ou autre texte : le défaut |
| `dk_color_write` | `0xRRGGBB`, `DKC_HASH` \| `DKC_LOWER` \| `DKC_ARGB`, out (10 octets) | longueur |

## Dates

Jours comptés depuis le 1er janvier 1970, calendrier grégorien prolongé vers le passé. Comme la fonction DATE d'un tableur, un mois ou un jour hors bornes se reporte : le 32 janvier est le 1er février.

| Fonction | Arguments | Résultat |
|---|---|---|
| `dk_days_from_civil` | année, mois, jour | jours |
| `dk_civil_from_days` | jours, `int *année`, `int *mois`, `int *jour` (0 : ignoré) | 0 |
| `dk_weekday` | jours | 0 dimanche à 6 samedi |
| `dk_excel_from_civil` | année, mois, jour, 1 : système 1904 | numéro de série d'Excel |
| `dk_civil_from_excel` | numéro de série, 1 : système 1904, `int *année`, `int *mois`, `int *jour` | 0 |

Les séries d'Excel gardent son 29 février 1900 fictif (série 60) et son « 0 janvier 1900 » (série 0) ; les années 0 à 1899 sont comptées depuis 1900 (99 vaut 1999). La partie décimale d'une série (l'heure) reste à l'appelant.

## CSV

| Fonction | Arguments | Résultat |
|---|---|---|
| `dk_csv_open` | tampon, octets, séparateur (0 : deviné parmi `,` `;` tabulation sur 20 lignes) | lecteur ; le BOM UTF-8 est sauté |
| `dk_csv_next` | lecteur | 0 à la fin, sinon `DKV_FIELD`, plus `DKV_EOL` (dernier champ de la ligne), plus `DKV_QUOTED` (il était entre guillemets) |
| `dk_csv_text` | lecteur, out, cap | longueur du champ courant, guillemets décodés |
| `dk_csv_sep` | lecteur | le séparateur retenu |
| `dk_csv_close` | lecteur | 0 |
| `dk_csv_esc` | texte, octets (< 0 : jusqu'au 0), séparateur, out, cap | longueur ; entre guillemets s'il le faut |

Le lecteur suit la RFC 4180 : un guillemet n'ouvre qu'en début de champ, un champ non fermé va jusqu'à la fin, un saut de ligne entre guillemets fait partie du champ.

## Paquets OOXML et ODF

| Fonction | Arguments | Résultat |
|---|---|---|
| `dk_doc_kind` | fichier entier, octets | `DKK_DOCX`, `DKK_XLSX`, `DKK_PPTX` (d'après les noms du ZIP et `[Content_Types].xml`), `DKK_ODT`, `DKK_ODS`, `DKK_ODP` (d'après le `mimetype` en tête), `DKK_RTF`, `DKK_OLE` (ancien Office, non pris en charge), `DKK_ZIP` (autre ZIP), `DKK_UNKNOWN` |
| `dk_doc_kind_mime` | type MIME, octets (< 0 : jusqu'au 0) | `DKK_*` |
| `dk_doc_mime` | `DKK_*` | le type MIME (le contenu de `mimetype` à écrire), 0 si aucun |
| `dk_part_path` | partie de base, cible relative, out, cap | longueur (`word/document.xml` + `media/a.png` donne `word/media/a.png` ; `../` et `/` traités) |
| `dk_rels_path` | partie, out, cap | longueur (`word/_rels/document.xml.rels`) |
| `dk_rel_count` | `.rels`, octets | nombre de relations |
| `dk_rel_get` | `.rels`, octets, rang, `DKR_ID` / `DKR_TYPE` / `DKR_TARGET` / `DKR_MODE`, out, cap | longueur ; `DKE_NOTFOUND` : pas de tel rang ; `DKR_TYPE` : le dernier composant (`slideMaster`) |
| `dk_rel_target` | `.rels`, octets, identifiant (`rId3`), out, cap | longueur de la cible ; `DKE_NOTFOUND` |
| `dk_rel_by_type` | `.rels`, octets, fin du type (`officeDocument`), out, cap | longueur de la première cible interne de ce type |
| `dk_env_text` | `DKS_*` | les textes fixes des enveloppes, 0 si inconnu |
| `dk_rel_write` | n (de `rIdn`), type, cible, 1 : externe, out, cap | longueur (`<Relationship .../>` ; un type sans `:` reçoit le préfixe officeDocument) |
| `dk_opc_default` | extension, type de contenu, out, cap | longueur (`<Default .../>` de `[Content_Types].xml`) |
| `dk_opc_override` | partie, type de contenu, out, cap | longueur (`<Override .../>`) |
| `dk_odf_entry` | chemin (`/` : la racine), type, out, cap | longueur (`<manifest:file-entry .../>`) |
| `dk_hash` | tampon, octets | empreinte FNV-1a sur 32 bits (« modifié depuis l'enregistrement ? ») |

Les textes de `dk_env_text` : `DKS_XML_DECL` (la déclaration XML), `DKS_RELS_OPEN` et `DKS_RELS_CLOSE` (`<Relationships>`), `DKS_TYPES_OPEN` (avec les `Default` de `rels` et `xml`) et `DKS_TYPES_CLOSE`, `DKS_MANIFEST_OPEN` et `DKS_MANIFEST_CLOSE` (le manifeste ODF).

## Exemple : lire du XML

Lit un morceau de `word/document.xml` : le texte de chaque paragraphe, le style et le gras ; puis échappe un texte pour l'écrire.

```c
#include <stdio.h>
#include <string.h>
#include <documentkit_abi.h>

static const char doc[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	"<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
	"<w:p><w:pPr><w:pStyle w:val=\"Title\"/></w:pPr><w:r><w:t>Caf&#233; &amp; th&#xE9;</w:t></w:r></w:p>"
	"<w:p><w:r><w:rPr><w:b/></w:rPr><w:t xml:space=\"preserve\">Gras, </w:t></w:r>"
	"<w:r><w:rPr><w:b w:val=\"0\"/></w:rPr><w:t>puis normal.</w:t></w:r></w:p>"
	"</w:body></w:document>";

int main (void)
{
	if (!documentkit_kit_open ())
		return 1;
	long r = dk_xml_open ((long) doc, sizeof doc - 1);
	if (r < 0)
		return 1;
	char style[32] = "", line[256] = "", t[128];
	int para = 0, bold = 0;
	for (long e; (e = dk_xml_next (r)) != DKX_EOF; )
	{
		if (e == DKX_START && dk_xml_is (r, (long) "p"))
		{
			style[0] = line[0] = 0;
			para++;
		}
		else if (e == DKX_START && dk_xml_is (r, (long) "pStyle"))
			dk_xml_attr (r, (long) "val", (long) style, sizeof style);	/* lit w:val */
		else if (e == DKX_START && dk_xml_is (r, (long) "r"))
			bold = 0;
		else if (e == DKX_START && dk_xml_is (r, (long) "b"))
			bold = (int) dk_xml_attr_on (r, 0);			/* <w:b/> : 1, w:val="0" : 0 */
		else if (e == DKX_TEXT)
		{
			dk_xml_text (r, (long) t, sizeof t);
			if (bold)
				strcat (line, "*");
			strncat (line, t, sizeof line - strlen (line) - 3);
			if (bold)
				strcat (line, "*");
		}
		else if (e == DKX_END && dk_xml_is (r, (long) "p"))
			printf ("paragraphe %d%s%s : %s\n", para, style[0] ? ", style " : "", style, line);
	}
	printf ("%ld octets lus\n", dk_xml_pos (r));
	dk_xml_close (r);

	char esc[128];
	long n = dk_xml_esc ((long) "a < b & \"c\"", -1, (long) esc, sizeof esc);
	printf ("echappe (%ld) : %s\n", n, esc);
	n = dk_xml_esc_attr ((long) "ligne 1\nligne 2", -1, (long) esc, sizeof esc);
	printf ("attribut (%ld) : %s\n", n, esc);
	return 0;
}
```

## Exemple : CSV, dates, unités et encodages

Lit un CSV au séparateur deviné, dont une colonne de dates en numéros de série d'Excel ; puis quelques conversions de longueurs, de couleurs et d'encodages.

```c
#include <stdio.h>
#include <string.h>
#include <documentkit_abi.h>

static const char csv[] =
	"\xEF\xBB\xBF" "Nom;Date;Note\r\n"
	"\"Dupont; Jean\";45658;\"Il a dit \"\"bonjour\"\"\"\r\n"
	"Martin;46326;\r\n";

static const char *days[] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };

int main (void)
{
	if (!documentkit_kit_open ())
		return 1;
	long r = dk_csv_open ((long) csv, sizeof csv - 1, 0);
	printf ("separateur devine : '%c'\n", (int) dk_csv_sep (r));
	int col = 0, row = 0;
	char f[128];
	for (long k; (k = dk_csv_next (r)) != 0; )
	{
		dk_csv_text (r, (long) f, sizeof f);
		if (row > 0 && col == 1)
		{
			int y, m, d;
			long serial = 0;
			sscanf (f, "%ld", &serial);
			dk_civil_from_excel (serial, 0, (long) &y, (long) &m, (long) &d);
			long day = dk_days_from_civil (y, m, d);
			printf ("[%s %02d/%02d/%d] ", days[dk_weekday (day)], d, m, y);
		}
		else
			printf ("[%s%s] ", f, k & DKV_QUOTED ? " (entre guillemets)" : "");
		col++;
		if (k & DKV_EOL)
		{
			printf ("\n");
			col = 0;
			row++;
		}
	}
	dk_csv_close (r);
	printf ("serie du 1er mars 1900 : %ld\n", dk_excel_from_civil (1900, 3, 1, 0));

	char out[64];
	dk_csv_esc ((long) "a;b", -1, ';', (long) out, sizeof out);
	printf ("champ pret pour un CSV : %s\n", out);

	long tw = dk_len_parse ((long) "2,54cm", DKU_TWIP, DKU_PT, 0);
	dk_len_write (tw, DKU_TWIP, DKU_MM, 2, (long) out, sizeof out);
	printf ("2,54 cm = %ld twips = %s = %ld EMU\n", tw, out, dk_len_conv (tw, DKU_TWIP, DKU_EMU));

	long c = dk_color_parse ((long) "FF336699", 0);
	dk_color_write (c, DKC_HASH | DKC_LOWER, (long) out);
	printf ("couleur %06lX -> %s\n", c, out);

	const char *utf8 = "\xE2\x82\xAC" " et " "\xC3\xA9" " et " "\xE2\x9C\x93";	/* euro, e accent, coche */
	char cp[32];
	long n = dk_utf8_to_cp1252 ((long) utf8, (long) strlen (utf8), (long) cp, sizeof cp, '?');
	printf ("%ld points de code -> %ld octets CP1252 : %02X ... %02X ... %c\n",
		dk_utf8_count ((long) utf8, (long) strlen (utf8)), n, (unsigned char) cp[0], (unsigned char) cp[5], cp[n - 1]);
	n = dk_cp1252_to_utf8 ((long) cp, n, (long) out, sizeof out);
	printf ("retour en UTF-8 (%ld octets, %s) : %s\n", n, dk_utf8_valid ((long) out, n) ? "valide" : "invalide", out);
	return 0;
}
```

## Exemple : un DOCX minimal

Assemble les parties d'un document Word avec DocumentKit et les met dans un ZIP avec [ArchiveKit](archivekit_fr.md), écrit `/tmp/hello.docx`, puis le relit : genre du document, relation vers la partie principale, son texte. Le fichier s'ouvre dans [Letters](help:letters).

```c
#include <stdio.h>
#include <string.h>
#include <documentkit_abi.h>
#include <archivekit_abi.h>

static char buf[4096];
static long len;

static void put (const char *s) { len += (long) snprintf (buf + len, sizeof buf - (size_t) len, "%s", s); }
static void put_kit (long n) { (void) n; len += (long) strlen (buf + len); }

int main (void)
{
	if (!documentkit_kit_open () || !archivekit_kit_open ())
		return 1;
	const char *main_ct = "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml";
	long w = ar_create ((long) "/tmp/hello.docx", AR_FMT_ZIP);
	if (w < 0)
		return 1;

	len = 0;							/* [Content_Types].xml */
	put ((const char *) dk_env_text (DKS_XML_DECL));
	put ((const char *) dk_env_text (DKS_TYPES_OPEN));
	put_kit (dk_opc_override ((long) "/word/document.xml", (long) main_ct, (long) (buf + len), sizeof buf - len));
	put ((const char *) dk_env_text (DKS_TYPES_CLOSE));
	ar_add_mem (w, (long) "[Content_Types].xml", (long) buf, len, AR_DEFAULT);

	len = 0;							/* _rels/.rels */
	put ((const char *) dk_env_text (DKS_XML_DECL));
	put ((const char *) dk_env_text (DKS_RELS_OPEN));
	put_kit (dk_rel_write (1, (long) "officeDocument", (long) "word/document.xml", 0, (long) (buf + len), sizeof buf - len));
	put ((const char *) dk_env_text (DKS_RELS_CLOSE));
	ar_add_mem (w, (long) "_rels/.rels", (long) buf, len, AR_DEFAULT);

	len = 0;							/* word/document.xml */
	put ((const char *) dk_env_text (DKS_XML_DECL));
	put ("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body><w:p><w:r><w:t>");
	put_kit (dk_xml_esc ((long) "Bonjour <Korin OS> & Letters", -1, (long) (buf + len), sizeof buf - len));
	put ("</w:t></w:r></w:p></w:body></w:document>");
	ar_add_mem (w, (long) "word/document.xml", (long) buf, len, AR_DEFAULT);
	long size = ar_finish (w);
	ar_free (w);
	printf ("/tmp/hello.docx : %ld octets\n", size);

	/* relire : le fichier entier en memoire */
	static unsigned char file[8192];
	FILE *f = fopen ("/tmp/hello.docx", "rb");
	if (!f)
		return 1;
	long n = (long) fread (file, 1, sizeof file, f);
	fclose (f);
	long kind = dk_doc_kind ((long) file, n);
	printf ("genre %ld (%s)\n", kind, kind == DKK_DOCX ? "DOCX" : "autre");

	long a = ar_open_mem ((long) file, n);
	long rels = ar_load (a, ar_find (a, (long) "_rels/.rels"));
	char part[128];
	dk_rel_by_type (ar_data (rels), ar_bytes (rels), (long) "officeDocument", (long) part, sizeof part);
	printf ("partie principale : %s (%ld relation)\n", part, dk_rel_count (ar_data (rels), ar_bytes (rels)));
	ar_free (rels);

	long x = ar_load (a, ar_find (a, (long) part));
	long r = dk_xml_open (ar_data (x), ar_bytes (x));
	char t[128];
	for (long e; (e = dk_xml_next (r)) != DKX_EOF; )
		if (e == DKX_TEXT && dk_xml_text (r, (long) t, sizeof t) > 0)
			printf ("texte : %s\n", t);
	dk_xml_close (r);
	printf ("empreinte : %08lx\n", dk_hash (ar_data (x), ar_bytes (x)));
	ar_free (x);
	ar_close (a);
	return 0;
}
```

## Pièges

- **Les tampons restent à vous** : un lecteur XML ou CSV lit le tampon passé à `dk_xml_open`, `dk_csv_open` jusqu'à sa fermeture ; ne le libérez pas avant.
- **64 lecteurs à la fois** par programme : fermez chaque lecteur (`dk_xml_close`, `dk_csv_close`), sinon `DKE_NOMEM`.
- **Sortie tronquée** : un résultat supérieur ou égal à `cap` veut dire que le texte a été coupé ; rappelez avec un tampon plus grand.
- **Les pointeurs de sortie des dates** sont des `int *`, convertis en `long`.
- **Pas de flottants** : longueurs et séries sont des entiers ; l'heure dans une série d'Excel (sa partie décimale) est à traiter par l'appelant.
- **Le ZIP n'est pas ici** : `dk_doc_kind` lit seulement le répertoire central pour reconnaître le genre ; pour lire une partie, [ArchiveKit](archivekit_fr.md).

## Voir aussi

- [ArchiveKit](archivekit_fr.md) : ouvrir et écrire les paquets ZIP.
- [PrinterKit](printerkit_fr.md) : imprimer un document une fois mis en page.
- Les apps qui s'en servent : [Letters](help:letters), [Sheet](help:sheet), [Slides](help:slides).
