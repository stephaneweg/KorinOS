/* ftrender_test : le rendu d'un glyphe par le FreeType du paquet freetype-dev, comme QFontEngineFT le fait sous une
 * mise à l'échelle (FT_Set_Transform) : diagnostic de « render glyph failed err=62 » (Raster_Overflow) vu dans Qt.
 *   ftrender_test FONT [pixels] [échelle] */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H

static jmp_buf jb;
static int deep(int n) { if (n == 0) longjmp(jb, 7); return deep(n - 1) + 1; }

int main(int argc, char **argv)
{
	/* setjmp / longjmp à travers des appels (le raster de FreeType s'en sert pour son débordement) */
	volatile int r = setjmp(jb);
	if (r == 0) deep(5);
	printf("setjmp/longjmp : %d (attendu 7)\n", r);

	FT_Library lib; FT_Face face;
	if (FT_Init_FreeType(&lib) || FT_New_Face(lib, argc > 1 ? argv[1] : "/res/fonts/DejaVuSans.ttf", 0, &face)) {
		printf("police introuvable\n"); return 1;
	}
	double px = argc > 2 ? atof(argv[2]) : 1.5, sc = argc > 3 ? atof(argv[3]) : 16;
	FT_Set_Char_Size(face, (FT_F26Dot6)(px * 64), 0, 72, 72);
	FT_Matrix m = { (FT_Fixed)(sc * 65536), 0, 0, (FT_Fixed)(sc * 65536) };
	for (int t = 0; t < 2; t++) {
		FT_Set_Transform(face, t ? &m : NULL, NULL);
		int ok = 0, bad = 0, err = 0;
		for (unsigned c = 'a'; c <= 'z'; c++) {
			FT_UInt g = FT_Get_Char_Index(face, c);
			if (FT_Load_Glyph(face, g, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP)) { bad++; continue; }
			int e = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
			if (e) { err = e; bad++; } else ok++;
		}
		printf("%s : %d bons, %d en erreur (dernière %#x)\n", t ? "avec matrice" : "sans matrice", ok, bad, err);
	}
	return 0;
}
