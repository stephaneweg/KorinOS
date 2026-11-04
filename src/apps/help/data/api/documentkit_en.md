# DocumentKit: the plumbing of office formats

**DocumentKit** gathers what office applications all have to redo: an event-driven XML reader and XML escaping, UTF-8, CP1252 and Latin-1 conversions, Office and ODF lengths (EMU, twips, points, centimetres...), colours, civil dates and Excel serial numbers, CSV, recognising a document (DOCX, XLSX, PPTX, ODT, ODS, ODP, RTF), and the relationships and envelopes of OOXML and ODF packages. Letters, Sheet and Slides use it.

The kit is written in assembly, in `SD0:/lib/documentkit.so`. It only works on **buffers** owned by the caller: no files, no heap, no other kit. ZIP is the business of [ArchiveKit](archivekit_en.md): extract the part (`word/document.xml`) with it, then hand it to DocumentKit.

## Opening the kit

```c
#include <documentkit_abi.h>

if (!documentkit_kit_open ())
	return 1;		/* SD0:/lib/documentkit.so missing */
```

Nothing to add to the link. Every argument is a `long`; readers (XML, CSV) are handles, small integers. Errors are negative:

| Error | Value | Meaning |
|---|---|---|
| `DKE_NOTFOUND` | -2 | attribute, relationship, character missing |
| `DKE_BADF` | -9 | unknown handle |
| `DKE_NOMEM` | -12 | no handle left: 64 readers open in the program |
| `DKE_INVAL` | -22 | invalid argument |
| `DKE_CORRUPT` | -74 | invalid UTF-8 |

**Text output.** Every function that writes text takes `out, cap` as its last arguments and behaves like `snprintf`: it returns the **full** length without the final 0, writes at most `cap - 1` bytes then a 0. With `cap = 0` nothing is written: you get the length to size the buffer.

## XML reader

The reader moves from event to event: start of element, end of element, text. It follows the behaviour of the Onyx reader, including on malformed XML (it does not stop). Comments, declarations and DOCTYPE are skipped; a CDATA section gives a raw text event; `<a/>` gives a start then an end.

| Function | Arguments | Result |
|---|---|---|
| `dk_xml_open` | buffer, bytes | reader; the buffer stays the caller's until `dk_xml_close` |
| `dk_xml_close` | reader | 0 |
| `dk_xml_next` | reader | the next event: `DKX_EOF` (0), `DKX_START` (1), `DKX_END` (2), `DKX_TEXT` (3) |
| `dk_xml_event` | reader | the current event |
| `dk_xml_empty` | reader | 1: this start comes from an `<a/>` |
| `dk_xml_is` | reader, name | 1: the element has this name, without its prefix (`w:p` is `p`) |
| `dk_xml_isq` | reader, name | 1: exact name, prefix included (`text:p`) |
| `dk_xml_name` | reader, out, cap | length of the name (with its prefix) |
| `dk_xml_attr` | reader, name, out, cap | length of the decoded value; `DKE_NOTFOUND` |
| `dk_xml_attr_int` | reader, name, default | integer (missing: the default) |
| `dk_xml_attr_bool` | reader, name, default | 1 if the value starts with `1`, `t` or `T` |
| `dk_xml_attr_on` | reader, name (0: `val`) | Word switch: missing, `1`, `true`, `on` give 1; `0`, `false`, `off`, `no` give 0 |
| `dk_xml_text` | reader, out, cap | length of the decoded text (entities, numeric references, CDATA) |
| `dk_xml_skip` | reader | 0; after a start, skips the whole element, end included |
| `dk_xml_pos` | reader | bytes read since the start of the buffer |
| `dk_xml_esc` | text, bytes (< 0: up to the 0), out, cap | length; `&`, `<`, `>`, `"` escaped, control characters removed (except tab, LF, CR) |
| `dk_xml_esc_attr` | text, bytes, out, cap | like `dk_xml_esc`, plus tab, LF and CR as numeric references (for an attribute value) |

An attribute asked for **without** a prefix is also found under any prefix (except `xmlns`): `dk_xml_attr (r, "val", ...)` reads `w:val`. With a prefix, the name must be exact. An unknown entity is left as is, `&#0;` is removed, an invalid reference becomes U+FFFD.

## UTF-8, CP1252, Latin-1

| Function | Arguments | Result |
|---|---|---|
| `dk_utf8_encode` | code point, out (4 bytes, no final 0) | bytes written; invalid: U+FFFD |
| `dk_utf8_decode` | text, bytes, `unsigned *cp` | bytes read; 0: end; `DKE_CORRUPT`: invalid sequence |
| `dk_utf8_valid` | text, bytes | 1: valid UTF-8 |
| `dk_utf8_count` | text, bytes | number of code points |
| `dk_utf8_to_cp` | text, bytes, `unsigned out[]`, slots | code points written (an invalid byte is read as Latin-1) |
| `dk_cp_to_utf8` | `unsigned cp[]`, count, out, cap | length |
| `dk_latin1_to_utf8` | text, bytes, out, cap | length |
| `dk_cp1252_to_utf8` | text, bytes, out, cap | length |
| `dk_utf8_to_latin1` | text, bytes, out, cap, replacement (0: removed) | length |
| `dk_utf8_to_cp1252` | text, bytes, out, cap, replacement (0: removed) | length |
| `dk_cp1252_cp` | byte | code point |
| `dk_cp_cp1252` | code point | byte, or `DKE_NOTFOUND` |

Decoding is strict: no overlong sequence, no UTF-16 surrogate half, no code point beyond U+10FFFF. The five CP1252 bytes without a character map to themselves (as WHATWG does).

## Lengths and colours

Units: `DKU_EMU` (1/914,400 inch), `DKU_TWIP` (1/20 point), `DKU_HMM` (1/100 mm), `DKU_PT`, `DKU_IN`, `DKU_CM`, `DKU_MM`, `DKU_PC` (pica, 12 points), `DKU_PX` (1/96 inch). Conversions are exact, rounded to nearest, going through EMU.

| Function | Arguments | Result |
|---|---|---|
| `dk_len_parse` | text ("2.54cm", "3,5mm", "12pt"), wanted unit, unit of a bare number, default | rounded integer; no number, or a `%`: the default |
| `dk_len_conv` | value, its unit, wanted unit | rounded integer |
| `dk_len_write` | value, its unit, written unit, decimals (0 to 6), out, cap | length ("1.2700cm") |
| `dk_color_parse` | text (`#RRGGBB`, `RRGGBB`, `AARRGGBB`), default | `0xRRGGBB`; `auto` or other text: the default |
| `dk_color_write` | `0xRRGGBB`, `DKC_HASH` \| `DKC_LOWER` \| `DKC_ARGB`, out (10 bytes) | length |

## Dates

Days counted from 1 January 1970, Gregorian calendar extended into the past. Like a spreadsheet's DATE function, an out-of-range month or day carries over: 32 January is 1 February.

| Function | Arguments | Result |
|---|---|---|
| `dk_days_from_civil` | year, month, day | days |
| `dk_civil_from_days` | days, `int *year`, `int *month`, `int *day` (0: ignored) | 0 |
| `dk_weekday` | days | 0 Sunday to 6 Saturday |
| `dk_excel_from_civil` | year, month, day, 1: 1904 system | Excel serial number |
| `dk_civil_from_excel` | serial number, 1: 1904 system, `int *year`, `int *month`, `int *day` | 0 |

Excel serials keep its fictitious 29 February 1900 (serial 60) and its "0 January 1900" (serial 0); years 0 to 1899 are counted from 1900 (99 is 1999). The fractional part of a serial (the time of day) is left to the caller.

## CSV

| Function | Arguments | Result |
|---|---|---|
| `dk_csv_open` | buffer, bytes, separator (0: guessed among `,` `;` tab over 20 lines) | reader; the UTF-8 BOM is skipped |
| `dk_csv_next` | reader | 0 at the end, otherwise `DKV_FIELD`, plus `DKV_EOL` (last field of the line), plus `DKV_QUOTED` (it was quoted) |
| `dk_csv_text` | reader, out, cap | length of the current field, quotes decoded |
| `dk_csv_sep` | reader | the separator chosen |
| `dk_csv_close` | reader | 0 |
| `dk_csv_esc` | text, bytes (< 0: up to the 0), separator, out, cap | length; quoted when needed |

The reader follows RFC 4180: a quote only opens at the start of a field, an unclosed field runs to the end, a line break inside quotes belongs to the field.

## OOXML and ODF packages

| Function | Arguments | Result |
|---|---|---|
| `dk_doc_kind` | whole file, bytes | `DKK_DOCX`, `DKK_XLSX`, `DKK_PPTX` (from the ZIP names and `[Content_Types].xml`), `DKK_ODT`, `DKK_ODS`, `DKK_ODP` (from the leading `mimetype`), `DKK_RTF`, `DKK_OLE` (old Office, not supported), `DKK_ZIP` (other ZIP), `DKK_UNKNOWN` |
| `dk_doc_kind_mime` | MIME type, bytes (< 0: up to the 0) | `DKK_*` |
| `dk_doc_mime` | `DKK_*` | the MIME type (the content of `mimetype` to write), 0 if none |
| `dk_part_path` | base part, relative target, out, cap | length (`word/document.xml` + `media/a.png` gives `word/media/a.png`; `../` and `/` handled) |
| `dk_rels_path` | part, out, cap | length (`word/_rels/document.xml.rels`) |
| `dk_rel_count` | `.rels`, bytes | number of relationships |
| `dk_rel_get` | `.rels`, bytes, rank, `DKR_ID` / `DKR_TYPE` / `DKR_TARGET` / `DKR_MODE`, out, cap | length; `DKE_NOTFOUND`: no such rank; `DKR_TYPE`: the last component (`slideMaster`) |
| `dk_rel_target` | `.rels`, bytes, identifier (`rId3`), out, cap | length of the target; `DKE_NOTFOUND` |
| `dk_rel_by_type` | `.rels`, bytes, end of the type (`officeDocument`), out, cap | length of the first internal target of that type |
| `dk_env_text` | `DKS_*` | the fixed texts of the envelopes, 0 if unknown |
| `dk_rel_write` | n (of `rIdn`), type, target, 1: external, out, cap | length (`<Relationship .../>`; a type without `:` gets the officeDocument prefix) |
| `dk_opc_default` | extension, content type, out, cap | length (`<Default .../>` of `[Content_Types].xml`) |
| `dk_opc_override` | part, content type, out, cap | length (`<Override .../>`) |
| `dk_odf_entry` | path (`/`: the root), type, out, cap | length (`<manifest:file-entry .../>`) |
| `dk_hash` | buffer, bytes | 32-bit FNV-1a fingerprint ("modified since saving?") |

The texts of `dk_env_text`: `DKS_XML_DECL` (the XML declaration), `DKS_RELS_OPEN` and `DKS_RELS_CLOSE` (`<Relationships>`), `DKS_TYPES_OPEN` (with the `Default` entries of `rels` and `xml`) and `DKS_TYPES_CLOSE`, `DKS_MANIFEST_OPEN` and `DKS_MANIFEST_CLOSE` (the ODF manifest).

## Example: reading XML

Reads a piece of `word/document.xml`: the text of each paragraph, the style and bold; then escapes a text for writing.

```c
#include <stdio.h>
#include <string.h>
#include <documentkit_abi.h>

static const char doc[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	"<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
	"<w:p><w:pPr><w:pStyle w:val=\"Title\"/></w:pPr><w:r><w:t>Caf&#233; &amp; t&#xE9;a</w:t></w:r></w:p>"
	"<w:p><w:r><w:rPr><w:b/></w:rPr><w:t xml:space=\"preserve\">Bold, </w:t></w:r>"
	"<w:r><w:rPr><w:b w:val=\"0\"/></w:rPr><w:t>then plain.</w:t></w:r></w:p>"
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
			dk_xml_attr (r, (long) "val", (long) style, sizeof style);	/* reads w:val */
		else if (e == DKX_START && dk_xml_is (r, (long) "r"))
			bold = 0;
		else if (e == DKX_START && dk_xml_is (r, (long) "b"))
			bold = (int) dk_xml_attr_on (r, 0);			/* <w:b/>: 1, w:val="0": 0 */
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
			printf ("paragraph %d%s%s: %s\n", para, style[0] ? ", style " : "", style, line);
	}
	printf ("%ld bytes read\n", dk_xml_pos (r));
	dk_xml_close (r);

	char esc[128];
	long n = dk_xml_esc ((long) "a < b & \"c\"", -1, (long) esc, sizeof esc);
	printf ("escaped (%ld): %s\n", n, esc);
	n = dk_xml_esc_attr ((long) "line 1\nline 2", -1, (long) esc, sizeof esc);
	printf ("attribute (%ld): %s\n", n, esc);
	return 0;
}
```

## Example: CSV, dates, units and encodings

Reads a CSV with a guessed separator, with a column of dates as Excel serial numbers; then a few conversions of lengths, colours and encodings.

```c
#include <stdio.h>
#include <string.h>
#include <documentkit_abi.h>

static const char csv[] =
	"\xEF\xBB\xBF" "Name;Date;Note\r\n"
	"\"Smith; John\";45658;\"He said \"\"hello\"\"\"\r\n"
	"Brown;46326;\r\n";

static const char *days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

int main (void)
{
	if (!documentkit_kit_open ())
		return 1;
	long r = dk_csv_open ((long) csv, sizeof csv - 1, 0);
	printf ("guessed separator: '%c'\n", (int) dk_csv_sep (r));
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
			printf ("[%s %d-%02d-%02d] ", days[dk_weekday (day)], y, m, d);
		}
		else
			printf ("[%s%s] ", f, k & DKV_QUOTED ? " (quoted)" : "");
		col++;
		if (k & DKV_EOL)
		{
			printf ("\n");
			col = 0;
			row++;
		}
	}
	dk_csv_close (r);
	printf ("serial of 1 March 1900: %ld\n", dk_excel_from_civil (1900, 3, 1, 0));

	char out[64];
	dk_csv_esc ((long) "a;b", -1, ';', (long) out, sizeof out);
	printf ("field ready for a CSV: %s\n", out);

	long tw = dk_len_parse ((long) "2.54cm", DKU_TWIP, DKU_PT, 0);
	dk_len_write (tw, DKU_TWIP, DKU_MM, 2, (long) out, sizeof out);
	printf ("2.54 cm = %ld twips = %s = %ld EMU\n", tw, out, dk_len_conv (tw, DKU_TWIP, DKU_EMU));

	long c = dk_color_parse ((long) "FF336699", 0);
	dk_color_write (c, DKC_HASH | DKC_LOWER, (long) out);
	printf ("colour %06lX -> %s\n", c, out);

	const char *utf8 = "\xE2\x82\xAC" " and " "\xC3\xA9" " and " "\xE2\x9C\x93";	/* euro, e acute, check mark */
	char cp[32];
	long n = dk_utf8_to_cp1252 ((long) utf8, (long) strlen (utf8), (long) cp, sizeof cp, '?');
	printf ("%ld code points -> %ld CP1252 bytes: %02X ... %02X ... %c\n",
		dk_utf8_count ((long) utf8, (long) strlen (utf8)), n, (unsigned char) cp[0], (unsigned char) cp[6], cp[n - 1]);
	n = dk_cp1252_to_utf8 ((long) cp, n, (long) out, sizeof out);
	printf ("back to UTF-8 (%ld bytes, %s): %s\n", n, dk_utf8_valid ((long) out, n) ? "valid" : "invalid", out);
	return 0;
}
```

## Example: a minimal DOCX

Assembles the parts of a Word document with DocumentKit and puts them in a ZIP with [ArchiveKit](archivekit_en.md), writes `/tmp/hello.docx`, then reads it back: kind of document, relationship to the main part, its text. The file opens in [Letters](help:letters).

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
	put_kit (dk_xml_esc ((long) "Hello <Korin OS> & Letters", -1, (long) (buf + len), sizeof buf - len));
	put ("</w:t></w:r></w:p></w:body></w:document>");
	ar_add_mem (w, (long) "word/document.xml", (long) buf, len, AR_DEFAULT);
	long size = ar_finish (w);
	ar_free (w);
	printf ("/tmp/hello.docx: %ld bytes\n", size);

	/* read it back: the whole file in memory */
	static unsigned char file[8192];
	FILE *f = fopen ("/tmp/hello.docx", "rb");
	if (!f)
		return 1;
	long n = (long) fread (file, 1, sizeof file, f);
	fclose (f);
	long kind = dk_doc_kind ((long) file, n);
	printf ("kind %ld (%s)\n", kind, kind == DKK_DOCX ? "DOCX" : "other");

	long a = ar_open_mem ((long) file, n);
	long rels = ar_load (a, ar_find (a, (long) "_rels/.rels"));
	char part[128];
	dk_rel_by_type (ar_data (rels), ar_bytes (rels), (long) "officeDocument", (long) part, sizeof part);
	printf ("main part: %s (%ld relationship)\n", part, dk_rel_count (ar_data (rels), ar_bytes (rels)));
	ar_free (rels);

	long x = ar_load (a, ar_find (a, (long) part));
	long r = dk_xml_open (ar_data (x), ar_bytes (x));
	char t[128];
	for (long e; (e = dk_xml_next (r)) != DKX_EOF; )
		if (e == DKX_TEXT && dk_xml_text (r, (long) t, sizeof t) > 0)
			printf ("text: %s\n", t);
	dk_xml_close (r);
	printf ("fingerprint: %08lx\n", dk_hash (ar_data (x), ar_bytes (x)));
	ar_free (x);
	ar_close (a);
	return 0;
}
```

## Pitfalls

- **Buffers stay yours**: an XML or CSV reader reads the buffer passed to `dk_xml_open`, `dk_csv_open` until it is closed; do not free it before.
- **64 readers at a time** per program: close each reader (`dk_xml_close`, `dk_csv_close`), or you get `DKE_NOMEM`.
- **Truncated output**: a result greater than or equal to `cap` means the text was cut; call again with a larger buffer.
- **Date output pointers** are `int *`, converted to `long`.
- **No floating point**: lengths and serials are integers; the time of day in an Excel serial (its fractional part) is up to the caller.
- **ZIP is not here**: `dk_doc_kind` only reads the central directory to recognise the kind; to read a part, use [ArchiveKit](archivekit_en.md).

## See also

- [ArchiveKit](archivekit_en.md): open and write ZIP packages.
- [PrinterKit](printerkit_en.md): print a document once laid out.
- The apps that use it: [Letters](help:letters), [Sheet](help:sheet), [Slides](help:slides).
