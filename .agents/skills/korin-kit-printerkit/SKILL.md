---
name: korin-kit-printerkit
description: PrinterKit de Korin OS (fonctions print_*) — dialogue d'impression, travail et pages, dessin (rectangles, traits, tracés, images, texte) en millièmes de point, imprimantes et file, confiés au service print (printd) qui fait un PDF ou imprime en IPP. À charger pour ajouter « Imprimer » à une app, produire un PDF par l'imprimante PDF ou gérer imprimantes et travaux.
---

# PrinterKit : imprimer

Kit en assembleur, `/lib/printerkit.so` (35 fonctions) ; il importe AppKit, UIKit (le dialogue) et ArchiveKit
(les images). Les pages sont enregistrées dans `/var/spool/print/<id>.opj` et rejouées par le service `print`
(`/sys/printd`, lancé par la session ou par le kit) : l'app n'attend pas l'imprimante. Deux sortes d'imprimantes :
**PDF** (toujours là, le texte reste du texte) et **réseau** IPP Everywhere / AirPrint. Une notification prévient à la fin.

## Où lire

| Sujet | Sur la carte | Dans le dépôt |
|---|---|---|
| Guide complet (3 exemples, `pdfprint.h`, pièges) | `/apps/help.app/api/printerkit_fr.md` | `src/apps/help/data/api/printerkit_fr.md` |
| En-tête C (structures, `print_mpt`) | `/devtools/gcc/include/kits/printerkit.h` (+ `printerkit_abi.h`, `pdfprint.h`) | `src/kits/printerkit/include/printerkit.h` |
| Table ABI | `/src/kits/printerkit/printerkit.abi` | `src/kits/printerkit/printerkit.abi` |
| Constantes (asm) | `/src/kits/printerkit/printerkit.inc` | `src/kits/printerkit/printerkit.inc` |
| Service, format, différences avec Onyx | — | `docs/IMPRESSION.md`, `src/sys/printd/` |
| Vraie app | — | `src/apps/paint/main.cpp` (`cmd_print`), `src/apps/control/applets/printers.inc` (asm) |

## Ouvrir le kit

C / C++ : `#include <printerkit.h>` (**pas** seulement `printerkit_abi.h` : les structures `PrintSetup`... et
`print_mpt` sont dans `printerkit.h`), puis `if (!printerkit_kit_open ()) …`. `pibuild` lie tous les kits ; à la main :
`gcc -I/devtools/gcc/include/kits prog.c -lprinterkit -o prog`.
Assembleur : `include '../../kits/printerkit/printerkit.inc'`, `kit_use '../../kits/printerkit/printerkit.abi'`,
`bl kits_open`, `kit_data`. Mêmes noms dans tous les langages (skills `korin-lang-*`).

Arguments en `long` ; une structure passe par son adresse `(long) &s`. **Longueurs en millièmes de point** (entiers,
72 000 = 1 pouce, 28 346 ≈ 1 cm, A4 = 595 276 × 841 890) ; `print_mpt (pt)` convertit des points `float`. **y descend**
depuis le haut. Couleurs `0xRRGGBB`.

## L'essentiel

Dialogue :
- `print_setup_default (&s)` : imprimante par défaut, son papier, portrait, 1 exemplaire (à appeler d'abord).
- `print_setup_paper (&s, "iso_a4_210x297mm"|0, PRINT_PORTRAIT|PRINT_LANDSCAPE)` → 1, 0 : papier inconnu.
- `print_dialog (&s, &di)` → 1 imprimer, 0 abandon. `PrintDialogInfo di = { sizeof di, titre, pages, courante,
  flags, 0, 0 }` (`PRINT_DLG_OWN_PAPER` : le document a sa taille).
- `PrintSetup` : `printer` (« PDF »), `media`, `paper_w/h`, `margin_l/t/r/b`, `orientation`, `copies`, `mono`,
  `quality`, `from`/`to`, `output` (fichier de l'imprimante PDF).

Travail (un seul à la fois par programme) :
- `print_begin (&s, titre)` → travail (0 : file non inscriptible).
- `print_page (j, l, h | 0, 0)` → 1 ; **0 = page non demandée, ne pas la dessiner**.
- `print_end (j)` → numéro en file (> 0), < 0 refusé ; `print_abort (j)`.

Dessin : `print_rect (j, x, y, l, h, c)`, `print_frame (j, x, y, l, h, ép, c)`, `print_line (j, x0, y0, x1, y1, ép, c)` ;
tracés `print_path_move/line/curve/close`, puis `print_path_fill (j, c)` / `print_path_stroke (j, ép, c)`.
Images : `print_image_mode (j, PRINT_IMG_ALPHA|0)`, `print_image (j, pixels, lpx, hpx, x, y, l, h)` (réduite à
300 ppp), `print_image_jpeg (j, jpeg, octets, l | h << 32, x, y, l, h)`.

Texte : `print_font (j, "DejaVu Sans", PRINT_BOLD|PRINT_ITALIC)` → police (-1 aucune) ;
`print_text (j, police, taille, x, base, utf8, c)` → avance ; `print_text_width`, `print_font_metrics (j, p, taille,
(long) m)` (`long m[3]` : ascendante, descendante, interligne) ; mise en page propre : `print_font_data`, `print_glyph`.

Imprimantes et file : `print_printers (PrintPrinter[], n)`, `print_printer_media`, `print_printer_status (nom, out, n)`,
`print_printers_find`, `print_printer_add`, `print_printer_remove`, `print_printer_default`,
`print_jobs (PrintJobInfo[], n)` (états `PRINT_JOB_QUEUED` 0 … `_DONE` 4, `_FAILED` 5, `_CANCELED` 6),
`print_job_cancel (id)`, `print_jobs_forget ()`.

## Exemple

Un PDF sans dialogue, depuis le Terminal (abrégé de l'exemple 1 de `printerkit_fr.md`) :

```c
#include <stdio.h>
#include <string.h>
#include <printerkit.h>

int main (void)
{
	if (!printerkit_kit_open ())
		return 1;
	PrintSetup s;
	print_setup_default ((long) &s);
	strcpy (s.printer, "PDF");				/* pas de dialogue : remplir soi-même */
	strcpy (s.output, "/tmp/demo.pdf");
	if (!s.paper_w)
		print_setup_paper ((long) &s, (long) "iso_a4_210x297mm", PRINT_PORTRAIT);
	long j = print_begin ((long) &s, (long) "Demo");
	if (!j)
		return 1;
	long cm = 28346, f = print_font (j, (long) "DejaVu Sans", PRINT_BOLD);
	if (print_page (j, 0, 0))
	{
		print_text (j, f, print_mpt (24), 2 * cm, 4 * cm, (long) "Korin OS imprime", 0x203040);
		print_line (j, 2 * cm, 4 * cm + 6000, s.paper_w - 2 * cm, 4 * cm + 6000, 1500, 0x3060C0);
		print_rect (j, 2 * cm, 6 * cm, 4 * cm, 2 * cm, 0xE8EEF8);
	}
	long id = print_end (j);
	printf (id > 0 ? "travail %ld en file\n" : "refuse (%ld)\n", id);
	return id > 0 ? 0 : 1;
}
```

Dans une app UIKit : `print_setup_default`, `print_dialog`, puis le même travail (voir `cmd_print` de Paint).

## Pièges

- **Un travail à la fois** : finir par `print_end` ou `print_abort` avant d'en commencer un autre.
- `print_page` rend 0 pour une page hors de celles choisies : ce n'est pas une erreur.
- `print_end` rend quand le travail est **en file**, pas imprimé : suivre par `print_jobs` (état `>= PRINT_JOB_DONE`).
- Les marges du `PrintSetup` sont ce que l'imprimante ne sait pas imprimer : dessiner dedans (le PDF n'en a pas).
- Le dialogue est fait de widgets UIKit : il demande `uikit_open`. Sans fenêtre, remplir `printer` et `output`.
- Pas de crénage dans `print_text` ni sa mesure ; pas de flottants dans le kit (`print_mpt` est en C).
- IPP en clair seulement (port 631) : une imprimante qui exige IPPS est refusée.
- `pdfprint.h` (`PrintWriter`, `print_ask`) s'inclut **après** `pdfwrite.h` de Letters (`src/apps/letters/pdfwrite.h`).
- Préfixe générique `print_` : une fonction du programme ne doit pas porter le nom d'une fonction du kit.
