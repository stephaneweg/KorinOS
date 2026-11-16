/* cliptest - essai du presse-papiers (clipd, ui_clip_* d'UIKit, sans fenêtre),
 * comme cliptest.asm : une image, des fichiers coupés, une copie à deux
 * représentations, 12 textes (l'anneau en garde 10), relus et affichés.
 * Comme l'asm : attend le service « clipboard » 30 s au plus (absent : rien
 * d'écrit, code 0) ; résultats des appels non vérifiés ; sortie sans tampon
 * (puts, print_dec) ; code 0. */
#include "korinrt.h"
#include "uikit_import.h"

static char g_txt[32];
static char g_buf[256];
static u32 g_px[64 * 48];
static u32 g_px2[64 * 48];

static const char s_fmts[] = "rtf\0text\0"; /* (« rtf », « text », puis un NUL de plus) */
static const char s_files[] = "HOME:/documents/lettre.txt\nSD0:/home/music/chanson.mp3\n";

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (korin_sys2(SYS_SVC_OPEN, "clipboard", 30000) < 0)
		_exit(0);
	for (long i = 0; i < 64 * 48; i++) /* une image de 64 x 48 */
		g_px[i] = 0x00336699;
	for (u64 i = 0; i < 12; i++) { /* 12 textes */
		g_txt[0] = 0;
		char *e = str_cat(g_txt, "copie ");
		e = fmt_dec(i, e);
		*e = 0;
		ui_clip_set((long)g_txt, -1);
	}
	long id = ui_clip_put((long)"rtf", (long)"{\\rtf1 a b}", 11, 0); /* deux représentations */
	ui_clip_put((long)"text", (long)"a b c", 5, id);
	ak_puts("id ");
	print_dec(id);
	ui_clip_get((long)g_buf, 256);
	ak_puts("\ntexte : ");
	ak_puts(g_buf);
	long n = ui_clip_take((long)s_fmts, (long)g_buf, 255);
	g_buf[n] = 0;
	ak_puts("\nrtf : ");
	ak_puts(g_buf);
	ui_clip_set_files((long)s_files, 1);
	struct krt_r4 f = krt_call_r4(ui_clip_get_files, (long)g_buf, 256, 0, 0);
	ak_puts("\nfichiers, coupes : ");
	print_dec(f.r1);
	ak_puts("\n");
	ak_puts(g_buf);
	ui_clip_set_image((long)g_px, 64, 48);
	struct krt_r4 im = krt_call_r4(ui_clip_get_image, (long)g_px2, 64 * 48 * 4, 0, 0);
	ak_puts("\nimage : ");
	print_dec(im.r0);
	ak_puts("\n");
	print_dec(im.r1);
	ak_puts("\n");
	print_dec((long)g_px2[64 * 48 - 1]);
	ak_puts("\n");
	_exit(0);
}
