---
name: korin-kit-documentkit
description: DocumentKit de Korin OS (fonctions dk_*) — lecteur XML par événements et échappement, UTF-8/CP1252/Latin-1, longueurs Office/ODF, couleurs, dates et séries Excel, CSV, genre d'un document (DOCX, XLSX, PPTX, ODT, ODS, ODP, RTF), relations et enveloppes OOXML/ODF, sur des tampons. À charger pour lire ou écrire un format bureautique, un XML ou un CSV.
---

# DocumentKit : la plomberie des formats bureautiques

Kit en assembleur, `SD0:/lib/documentkit.so` (60 fonctions). Il ne travaille que sur des **tampons de l'appelant** :
pas de fichier, pas de tas, aucun kit importé. Le ZIP est l'affaire d'ArchiveKit (skill `korin-kit-archivekit`) :
on extrait la partie (`word/document.xml`) avec lui, puis on la passe à DocumentKit. Letters, Sheet, Slides s'en servent.

## Où lire

| Sujet | Sur la carte | Dans le dépôt |
|---|---|---|
| Guide complet (3 exemples vérifiés, pièges) | `SD0:/apps/help.app/api/documentkit_fr.md` | `src/apps/help/data/api/documentkit_fr.md` |
| En-tête C | `/gcc/include/kits/documentkit_abi.h` | généré : `build/gcc/sdroot/gcc/include/kits/documentkit_abi.h` |
| Table ABI | `SD0:/src/kits/documentkit/documentkit.abi` | `src/kits/documentkit/documentkit.abi` |
| Constantes (asm) | `SD0:/src/kits/documentkit/documentkit.inc` | `src/kits/documentkit/documentkit.inc` |
| Conception, ce qui reste aux apps | — | `docs/KITS.md` (section DocumentKit), `docs/BUREAUTIQUE.md` |
| Vraie app | — | `src/apps/letters/docx.h` (lecture DOCX), `src/apps/letters/main.cpp` |

## Ouvrir le kit

C / C++ : `#include <documentkit_abi.h>`, `if (!documentkit_kit_open ()) return 1;`. `pibuild` lie tous les kits ;
à la main : `gcc -I/gcc/include/kits prog.c -ldocumentkit -larchivekit -o prog`.
Assembleur : `include '../../kits/documentkit/documentkit.inc'`, `kit_use '../../kits/documentkit/documentkit.abi'`
(après celui d'AppKit), `bl kits_open`, `kit_data`. Mêmes noms dans tous les langages (skills `korin-lang-*`).

Arguments en `long`. Erreurs négatives : `DKE_NOTFOUND` -2, `DKE_BADF` -9, `DKE_NOMEM` -12 (64 lecteurs ouverts),
`DKE_INVAL` -22, `DKE_CORRUPT` -74.

**Sortie texte `out, cap`** (derniers arguments) : comme `snprintf` — rend la longueur **complète** sans le 0, écrit
au plus `cap - 1` octets puis un 0 ; `cap = 0` : rien d'écrit, la longueur seule.

## L'essentiel

XML (événements `DKX_EOF` 0, `DKX_START` 1, `DKX_END` 2, `DKX_TEXT` 3 ; `<a/>` = START puis END) :
- `dk_xml_open (buf, n)` → lecteur ; `dk_xml_close (r)` ; `dk_xml_next (r)` → événement.
- `dk_xml_is (r, "p")` (sans préfixe : `w:p` est `p`), `dk_xml_isq (r, "text:p")` (exact), `dk_xml_name (r, out, cap)`.
- `dk_xml_attr (r, "val", out, cap)` (sans préfixe : trouve aussi `w:val`), `dk_xml_attr_int (r, nom, défaut)`,
  `dk_xml_attr_on (r, nom|0)` (interrupteur de Word : `<w:b/>` = 1, `w:val="0"` = 0).
- `dk_xml_text (r, out, cap)` (entités, CDATA décodées), `dk_xml_skip (r)` (après START : saute l'élément).
- `dk_xml_esc (txt, n|-1, out, cap)`, `dk_xml_esc_attr (…)` : échapper pour écrire.

Encodages : `dk_utf8_decode (txt, n, (long) &cp)`, `dk_utf8_encode (cp, out)`, `dk_utf8_valid`, `dk_utf8_count`,
`dk_cp1252_to_utf8`, `dk_latin1_to_utf8 (txt, n, out, cap)`, `dk_utf8_to_cp1252 (txt, n, out, cap, remplaçant)`.

Longueurs (`DKU_EMU`, `DKU_TWIP`, `DKU_HMM`, `DKU_PT`, `DKU_IN`, `DKU_CM`, `DKU_MM`, `DKU_PC`, `DKU_PX`) :
`dk_len_parse ("2.54cm", voulue, unité_nu, défaut)`, `dk_len_conv (v, de, vers)`,
`dk_len_write (v, de, vers, décimales, out, cap)`. Couleurs : `dk_color_parse (txt, défaut)` → `0xRRGGBB`,
`dk_color_write (c, DKC_HASH|DKC_LOWER|DKC_ARGB, out)`.

Dates (jours depuis 1970) : `dk_days_from_civil (a, m, j)`, `dk_civil_from_days (jours, &a, &m, &j)`, `dk_weekday`,
`dk_excel_from_civil (a, m, j, 1904?)`, `dk_civil_from_excel (série, 1904?, &a, &m, &j)`.

CSV : `dk_csv_open (buf, n, sép|0 deviné)`, `dk_csv_next (r)` → 0 fin, sinon `DKV_FIELD` | `DKV_EOL` | `DKV_QUOTED` ;
`dk_csv_text (r, out, cap)`, `dk_csv_sep`, `dk_csv_close`, `dk_csv_esc (txt, n, sép, out, cap)`.

Paquets : `dk_doc_kind (fichier, n)` → `DKK_DOCX` 1, `XLSX` 2, `PPTX` 3, `ODT` 4, `ODS` 5, `ODP` 6, `RTF` 7, `ZIP` 8,
`OLE` 9, `UNKNOWN` 0 ; `dk_doc_mime (kind)` ; `dk_part_path (base, cible, out, cap)`, `dk_rels_path (partie, out, cap)` ;
`dk_rel_by_type (rels, n, "officeDocument", out, cap)`, `dk_rel_target (rels, n, "rId3", out, cap)`, `dk_rel_count`,
`dk_rel_get (rels, n, rang, DKR_ID|DKR_TYPE|DKR_TARGET|DKR_MODE, out, cap)`. Écrire : `dk_env_text (DKS_*)`
(`DKS_XML_DECL`, `DKS_RELS_OPEN/CLOSE`, `DKS_TYPES_OPEN/CLOSE`, `DKS_MANIFEST_OPEN/CLOSE`), `dk_rel_write`,
`dk_opc_default`, `dk_opc_override`, `dk_odf_entry`. `dk_hash (buf, n)` : empreinte FNV-1a 32 bits.

## Exemple

Le texte d'un `.docx` (d'après l'exemple « un DOCX minimal » de `documentkit_fr.md`) :

```c
#include <stdio.h>
#include <documentkit_abi.h>
#include <archivekit_abi.h>

int main (int argc, char **argv)
{
	if (argc < 2 || !documentkit_kit_open () || !archivekit_kit_open ())
		return 1;
	long a = ar_open ((long) argv[1]);
	if (a < 0)
		return 1;
	char part[128] = "word/document.xml";
	long ri = ar_find (a, (long) "_rels/.rels");
	if (ri >= 0)
	{
		long rels = ar_load (a, ri);
		dk_rel_by_type (ar_data (rels), ar_bytes (rels), (long) "officeDocument", (long) part, sizeof part);
		ar_free (rels);
	}
	long x = ar_load (a, ar_find (a, (long) part));
	if (x < 0)
		return 1;
	long r = dk_xml_open (ar_data (x), ar_bytes (x));	/* x doit vivre jusqu'à dk_xml_close */
	char t[512];
	for (long e; (e = dk_xml_next (r)) != DKX_EOF; )
		if (e == DKX_TEXT && dk_xml_text (r, (long) t, sizeof t) > 0)
			fputs (t, stdout);
		else if (e == DKX_END && dk_xml_is (r, (long) "p"))
			putchar ('\n');
	dk_xml_close (r);
	ar_free (x);
	ar_close (a);
	return 0;
}
```

## Pièges

- Les tampons restent à vous : ne libérez pas celui de `dk_xml_open` / `dk_csv_open` avant `dk_*_close`.
- **64 lecteurs** (XML + CSV) à la fois par programme : fermez chacun, sinon `DKE_NOMEM`.
- Résultat `>= cap` : le texte a été tronqué ; rappelez avec un tampon plus grand (ou `cap = 0` pour mesurer).
- Les sorties des dates sont des `int *`, passés en `(long) &y`.
- Pas de flottants : longueurs et séries Excel sont entières ; la partie horaire d'une série est à l'appelant.
- Le lecteur XML ne s'arrête pas sur du XML mal formé ; une entité inconnue reste telle quelle.
- `dk_doc_kind` ne lit que le répertoire central du ZIP ; pour lire une partie, ArchiveKit.
- Le préfixe `dk_` est aussi celui de DemoKit, l'exemple de `kit_fr.md` : ne pas confondre.
- Un programme ne peut pas définir une fonction de même nom qu'une fonction du kit.
