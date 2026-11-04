# ArchiveKit: compression and archives

**ArchiveKit** compresses and decompresses (deflate, zlib, gzip), computes CRC-32 and Adler-32 checksums, reads ZIP (ZIP64 included), tar, tar.gz and gz archives, and writes ZIP, tar, tar.gz and gz. It works on the card as well as in memory: it is how Letters, Sheet and Slides open `.docx`, `.xlsx`, `.pptx`, `.odt`... files, which are ZIPs. The kit is written in assembly, in `SD0:/lib/archivekit.so`; it uses AppKit for files. The **Archiver** app and the `arc` Terminal tool are built on it.

A damaged file never crashes the kit: sizes and offsets are bounded by the source, each entry is checked against its CRC, and the function returns an error.

## Opening the kit

```c
#include <archivekit_abi.h>

if (!archivekit_kit_open ())
	return 1;		/* SD0:/lib/archivekit.so missing */
```

Nothing to add to the link. Every argument is a `long` (convert a pointer: `(long) buf`). Archives, writers and kit buffers are **opaque handles**. A negative result is an error; `ar_strerror (e)` describes it (in French).

| Error | Value | Meaning |
|---|---|---|
| `AR_E_IO` | -5 | read or write failed |
| `AR_E_NOMEM` | -12 | out of memory |
| `AR_E_CRYPT` | -13 | encrypted entry (ZipCrypto, AES: not supported) |
| `AR_E_INVAL` | -22 | invalid handle, index, wrapper or level |
| `AR_E_FBIG` | -27 | too large for the format (ZIP written: 4 GiB per entry) |
| `AR_E_SPACE` | -28 | output buffer too small |
| `AR_E_METHOD` | -38 | unknown method (Deflate64, bzip2, LZMA) |
| `AR_E_CORRUPT` | -74 | damaged or truncated data |
| `AR_E_CRC` | -84 | wrong checksum |
| `AR_E_FORMAT` | -95 | not a known format |
| `E_NOENT` | -2 | (from AppKit) file or entry missing |

Two threads may work at the same time on two handles; one archive serves only one thread at a time.

## Checksums

| Function | Arguments | Result |
|---|---|---|
| `ar_crc32` | sum (0 at the start, then the last result), data, bytes | CRC-32 (the one of ZIP, gzip, PNG) |
| `ar_adler32` | sum (1 at the start, then the last result), data, bytes | Adler-32 (the one of zlib) |

Sums are computed piece by piece: pass the previous result to the next piece.

## Compressing a block of memory

The **wrapper** says what surrounds the deflate stream: `AR_RAW` (0, the bare stream, as in a ZIP entry), `AR_ZLIB` (1, zlib header and Adler-32, as in PNG or PDF), `AR_GZIP` (2, gzip header and CRC-32, a `.gz` file), and for decompression only `AR_AUTO` (3, zlib or gzip from the first bytes). The **level** goes from 0 (`AR_STORE`, nothing compressed) to 9 (`AR_BEST`); -1 means 6 (`AR_DEFAULT`).

| Function | Arguments | Result |
|---|---|---|
| `ar_deflate_bound` | bytes, wrapper | output size that is always enough |
| `ar_deflate` | source, bytes, output, capacity, wrapper, level | bytes written; `AR_E_SPACE` if the output is too small |
| `ar_inflate` | source, bytes, output, capacity, wrapper | bytes written; checksums verified |
| `ar_inflate_size` | source, bytes, wrapper | decompressed size (everything is decoded and checked) |

The kit can also provide the output: it returns a **buffer**, read with `ar_data` and `ar_bytes` and given back with `ar_free`.

| Function | Arguments | Result |
|---|---|---|
| `ar_deflate_new` | source, bytes, wrapper, level | buffer |
| `ar_inflate_new` | source, bytes, wrapper, expected size (0: unknown) | buffer; a 0 follows the data (text can be used as is) |
| `ar_data` | buffer (or memory writer after `ar_finish`) | pointer to the bytes |
| `ar_bytes` | buffer (or writer after `ar_finish`) | number of bytes |
| `ar_free` | buffer, writer or archive | 0 (0: no effect) |

## Reading an archive

The format is recognised from the first bytes: ZIP (with or without data in front, ZIP64, data descriptors), tar (ustar, GNU, pax), tar.gz, gz (several members included). CP437 names of old ZIPs are converted to UTF-8.

| Function | Arguments | Result |
|---|---|---|
| `ar_open` | path | archive |
| `ar_open_mem` | buffer, bytes | archive; the buffer stays the caller's, untouched until `ar_close` |
| `ar_close` | archive | 0 (0: no effect) |
| `ar_probe` | path | `AR_FMT_*` from the first bytes, without reading it all; `AR_E_FORMAT` otherwise |
| `ar_format` | archive | `AR_FMT_ZIP` (1), `AR_FMT_TAR` (2), `AR_FMT_TGZ` (3), `AR_FMT_GZIP` (4) |
| `ar_count` | archive | number of entries |
| `ar_entry` | archive, index, `long info[8]` | 0; the entry's record (below) |
| `ar_name` | archive, index | name (UTF-8, `/` between parts, valid until `ar_close`) |
| `ar_entry_size` | archive, index | original size |
| `ar_find` | archive, name | index: exact name first, then ignoring ASCII case; `E_NOENT` |
| `ar_read` | archive, index, buffer, capacity | bytes read; CRC checked; `AR_E_SPACE`: provide `ar_entry_size` bytes |
| `ar_load` | archive, index | kit buffer (`ar_data`, `ar_bytes`, `ar_free`); a 0 follows the data |
| `ar_extract` | archive, index, path | bytes written; folders of the path created, date set, nothing left behind on error |
| `ar_extract_all` | archive, folder | files written; names climbing up through `..` are skipped |
| `ar_test` | archive, index | 0: the entry reads and its checksum is right |

The `ar_entry` record (`long info[8]`, `ARE_*` constants in bytes): `info[0]` name (pointer), `info[1]` original size, `info[2]` size in the archive, `info[3]` date (UTC seconds since 1970, 0: none), `info[4]` 1 for a folder, `info[5]` method (0 stored, 8 deflate), `info[6]` announced CRC-32, `info[7]` flags (`ARF_ENCRYPTED`).

## Creating an archive

| Function | Arguments | Result |
|---|---|---|
| `ar_create` | path, format | writer; the file is replaced |
| `ar_create_mem` | format | memory writer |
| `ar_add_mem` | writer, name, data, bytes, level | 0 |
| `ar_add_file` | writer, name (0: the file's own), path, level | 0; a folder: itself and everything in it |
| `ar_add_dir` | writer, name | 0: a folder entry |
| `ar_set_time` | writer, UTC seconds (0: now) | 0; for the following entries |
| `ar_finish` | writer | bytes of the archive; the file is closed; in memory: `ar_data`, `ar_bytes` |

After `ar_finish`, give the writer back with `ar_free`; an `ar_free` without `ar_finish` abandons the archive. `AR_FMT_GZIP` takes a single entry. An existing archive cannot be modified: write a new one.

At level 0, an entry is stored as is: this is how the `mimetype` of an ODF document goes in, as the **first** entry. An entry that deflate does not shrink is stored automatically.

## The kit and its formats

| Function | Arguments | Result |
|---|---|---|
| `ar_formats` | array of `ARFI_SIZE` (128) bytes per format (0: count), slots | number of formats |
| `ar_format_name` | `AR_FMT_*` | "ZIP", "TAR", "TAR.GZ", "GZIP", 0 if unknown |
| `ar_strerror` | negative error | text (in French) |

An `ar_formats` record: identifier (`ARFI_ID`, `long`), readable (`ARFI_READ`), writable (`ARFI_WRITE`), name (`ARFI_NAME`, 16 bytes), extensions separated by a space (`ARFI_EXT`, 88 bytes, "zip docx xlsx...").

## Example: compressing in memory and writing a .gz

Compresses a text as gzip, decompresses it to check, compares the CRCs, then writes the result to `/tmp/demo.txt.gz` (`/tmp` is in memory, `RAM0:/tmp`).

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <archivekit_abi.h>

int main (void)
{
	if (!archivekit_kit_open ())
		return 1;
	/* a repetitive text: it compresses well */
	static char text[20000];
	for (int i = 0; i < 400; i++)
		strcat (text, "Korin OS compresses with ArchiveKit. ");
	long n = (long) strlen (text);

	long cap = ar_deflate_bound (n, AR_GZIP);
	unsigned char *gz = malloc ((size_t) cap);
	long packed = ar_deflate ((long) text, n, (long) gz, cap, AR_GZIP, AR_DEFAULT);
	if (packed < 0)
	{
		printf ("deflate: %s\n", (const char *) ar_strerror (packed));
		return 1;
	}
	printf ("%ld bytes -> %ld (%ld %%)\n", n, packed, packed * 100 / n);

	/* the original size, then the decompression itself */
	long size = ar_inflate_size ((long) gz, packed, AR_AUTO);
	char *back = malloc ((size_t) size + 1);
	long got = ar_inflate ((long) gz, packed, (long) back, size, AR_AUTO);
	printf ("inflate: %ld bytes, %s\n", got, got == n && !memcmp (back, text, (size_t) n) ? "identical" : "DIFFERENT");
	printf ("CRC-32 %08lx, Adler-32 %08lx\n", ar_crc32 (0, (long) text, n), ar_adler32 (1, (long) text, n));

	/* the same, the kit provides the buffer */
	long b = ar_deflate_new ((long) text, n, AR_ZLIB, AR_BEST);
	if (b > 0)
	{
		printf ("zlib at level 9: %ld bytes\n", ar_bytes (b));
		ar_free (b);
	}

	FILE *f = fopen ("/tmp/demo.txt.gz", "wb");
	if (f)
	{
		fwrite (gz, 1, (size_t) packed, f);
		fclose (f);
		printf ("/tmp/demo.txt.gz: %s\n", (const char *) ar_format_name (ar_probe ((long) "/tmp/demo.txt.gz")));
	}
	free (gz);
	free (back);
	return 0;
}
```

## Example: creating a ZIP, listing it, extracting it

Writes `/tmp/demo.zip` (a folder, two files), then opens it again: list of entries, reading one entry by name, checking, full extraction into `/tmp/demo`.

```c
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <archivekit_abi.h>

#define ZIP "/tmp/demo.zip"

static int fail (const char *what, long e)
{
	printf ("%s: %s\n", what, (const char *) ar_strerror (e));
	return 1;
}

int main (void)
{
	if (!archivekit_kit_open ())
		return 1;
	const char *readme = "An archive written by ArchiveKit.\n";
	const char *data = "a;b;c\n1;2;3\n";

	long w = ar_create ((long) ZIP, AR_FMT_ZIP);
	if (w < 0)
		return fail ("ar_create", w);
	ar_set_time (w, 0);					/* the current time */
	ar_add_mem (w, (long) "readme.txt", (long) readme, (long) strlen (readme), AR_DEFAULT);
	ar_add_dir (w, (long) "data");
	ar_add_mem (w, (long) "data/table.csv", (long) data, (long) strlen (data), AR_BEST);
	long total = ar_finish (w);
	ar_free (w);
	if (total < 0)
		return fail ("ar_finish", total);
	printf ("%s: %ld bytes\n", ZIP, total);

	long a = ar_open ((long) ZIP);
	if (a < 0)
		return fail ("ar_open", a);
	printf ("format %s, %ld entries\n", (const char *) ar_format_name (ar_format (a)), ar_count (a));
	for (long i = 0; i < ar_count (a); i++)
	{
		long info[8];
		ar_entry (a, i, (long) info);
		time_t t = (time_t) info[ARE_TIME / 8];
		char when[32];
		strftime (when, sizeof when, "%Y-%m-%d %H:%M", gmtime (&t));
		printf ("  %-16s %6ld -> %6ld  %s %s%s\n", (const char *) info[ARE_NAME / 8], info[ARE_SIZE / 8],
			info[ARE_PACKED / 8], when, info[ARE_DIR / 8] ? "(folder)" : "",
			ar_test (a, i) == 0 ? "" : " DAMAGED");
	}

	long i = ar_find (a, (long) "DATA/TABLE.CSV");		/* case is ignored */
	if (i >= 0)
	{
		long b = ar_load (a, i);
		printf ("%s:\n%s", (const char *) ar_name (a, i), (const char *) ar_data (b));
		ar_free (b);
	}
	long files = ar_extract_all (a, (long) "/tmp/demo");
	printf ("%ld file(s) extracted into /tmp/demo\n", files);
	ar_close (a);
	return 0;
}
```

## Example: a ZIP in memory

An OOXML or ODF document often arrives in memory (read from a file, received from the network). Here a ZIP is built in memory, then read back with `ar_open_mem`, the way Letters reads a `.docx`. The list of formats the kit knows comes last.

```c
#include <stdio.h>
#include <string.h>
#include <archivekit_abi.h>

int main (void)
{
	if (!archivekit_kit_open ())
		return 1;
	/* a minimal package: mimetype stored first, as ODF requires */
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
	printf ("ZIP in memory: %ld bytes; 'mimetype' at byte 30: %.8s\n", ar_bytes (w), (const char *) zip + 30);

	long a = ar_open_mem ((long) zip, n);	/* the buffer must live until ar_close */
	long i = ar_find (a, (long) "content.xml");
	char xml[128];
	long got = ar_read (a, i, (long) xml, sizeof xml - 1);
	if (got >= 0)
	{
		xml[got] = 0;
		printf ("content.xml: %s\n", xml);
	}
	ar_close (a);
	ar_free (w);					/* the writer, and its bytes with it */

	unsigned char fm[8][ARFI_SIZE];
	long k = ar_formats ((long) fm, 8);
	for (long j = 0; j < k; j++)
		printf ("%-7s read %s, write %s: %s\n", (char *) fm[j] + ARFI_NAME,
			*(long *) (fm[j] + ARFI_READ) ? "yes" : "no", *(long *) (fm[j] + ARFI_WRITE) ? "yes" : "no",
			(char *) fm[j] + ARFI_EXT);
	return 0;
}
```

## Pitfalls

- **`ar_open_mem` does not copy**: the buffer must stay intact until `ar_close`. Likewise, names returned by `ar_name` and `ARE_NAME` die with the archive.
- **Give back what the kit gives**: each buffer (`ar_load`, `ar_*_new`) and each writer goes back through `ar_free`, each archive through `ar_close`.
- **A tar.gz is decompressed in memory** when opened: its size is bounded by free memory. A single-file `.gz` is decoded on the fly.
- **Dates**: a ZIP date has no time zone; it is read as UTC.
- **No encryption**: an encrypted entry is listed, but reading it returns `AR_E_CRYPT`.
- **ZIP64 is read, not written**: beyond 4 GiB per entry, `AR_E_FBIG`.

## See also

- [DocumentKit](documentkit_en.md): read the XML of a `.docx` or `.odt` once the part is extracted.
- [PrinterKit](printerkit_en.md), which compresses the images of print jobs with ArchiveKit.
- [libc and POSIX layer](libc_en.md): files and paths (`/tmp`, `SD0:/...`).
