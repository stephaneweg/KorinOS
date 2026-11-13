/* sounddraw.c : petits dessins du son (src/inc/sounddraw.inc), traduits ligne à ligne. */
#include "sounddraw.h"
#include "sys_consts.h"
#include "uikit_import.h"

static const char sd_s_cross[] = "\xC3\x97";     /* U+00D7 */
static const char sd_s_check[] = "\xE2\x9C\x93"; /* U+2713 */

/* un jeton du thème (32 bits) */
static u32 tok(const u8 *t, long off) { return *(const u32 *)(t + off); }

void sd_speaker(long x, long y, long muted, u32 ink, u32 dim, const struct krt_ui *ui)
{
	(void)dim; /* (comme l'asm : l'encre atténuée n'est pas utilisée) */
	ui_fill(x + 1, y + 5, 3, 5, ink); /* corps */
	for (long i = 0; i <= 4; i++)     /* pavillon : de 5 à 13 de haut */
		ui_fill(x + 4 + i, y - i + 5, 1, 2 * i + 5, ink);
	if (muted) { /* muet : une croix */
		ui_text((long)sd_s_cross, x + 10, y + 12, ink, 11, (long)ui->font_b, 0);
		return;
	}
	ui_fill(x + 11, y + 5, 1, 5, ink); /* ondes */
	ui_fill(x + 14, y + 2, 1, 11, ink);
	ui_fill(x + 12, y + 4, 1, 1, ink);
	ui_fill(x + 12, y + 10, 1, 1, ink);
	ui_fill(x + 13, y + 1, 1, 1, ink);
	ui_fill(x + 13, y + 13, 1, 1, ink);
}

void sd_slider(long x, long y, long w, u64 value, long grey)
{
	u64 v = value <= 100 ? value : 100;
	const u8 *t = (const u8 *)ui_tokens();
	u32 c = tok(t, TJ_SUNKEN); /* piste */
	ui_rbox(x, y + 6, w, 4, 2, c, c);
	long pos = (long)((u64)(w - 12) * v / 100); /* position du bouton */
	c = grey ? tok(t, TJ_TEXT3) : tok(t, TJ_ACCENT); /* part remplie */
	ui_rbox(x, y + 6, pos + 6, 4, 2, c, c);
	c = tok(t, TJ_LINE_STRONG); /* bouton : contour, puis clair */
	ui_rbox(x + pos, y, 16, 16, 8, c, c);
	ui_rbox(x + pos + 1, y + 1, 14, 14, 7, tok(t, TJ_RAISED), tok(t, TJ_RAISED_LOW));
}

long sd_slider_value(long cx, long x, long w)
{
	long v = cx - x - 8;
	long span = w - 12;
	if (v < 0)
		v = 0;
	if (v > span)
		v = span;
	/* (divisions non signées, comme l'asm) */
	return (long)(((u64)v * 100 + ((u64)span >> 1)) / (u64)span);
}

void sd_check(long x, long y, long checked, const struct krt_ui *ui)
{
	const u8 *t = (const u8 *)ui_tokens();
	u32 c = checked ? tok(t, TJ_ACCENT) : tok(t, TJ_LINE_STRONG);
	ui_rbox(x, y, 14, 14, 3, c, c);
	if (!checked) {
		c = tok(t, TJ_FACE);
		ui_rbox(x + 1, y + 1, 12, 12, 2, c, c);
		return;
	}
	/* la coche en blanc, en dur comme l'asm */
	ui_text((long)sd_s_check, x + 2, y + 12, 0xFFFFFF, 12, (long)ui->font_b, 0);
}

void sd_meter(long x, long y, long w, long h, u64 peak)
{
	const u8 *t = (const u8 *)ui_tokens();
	ui_fill(x, y, w, h, tok(t, TJ_SUNKEN));
	u64 lit = ((u64)w * peak) >> 15; /* part allumée */
	if (!lit)
		return;
	/* près de la saturation : alerte */
	ui_fill(x, y, (long)lit, h, peak >= 29000 ? tok(t, TJ_WARNING) : tok(t, TJ_SUCCESS));
}
