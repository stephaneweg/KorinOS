/* wifidraw.c : petits dessins du Wi-Fi (src/inc/wifidraw.inc), traduits ligne à ligne. */
#include "wifidraw.h"
#include "uikit_import.h"

long wd_level(long rssi)
{
	long r = (i32)rssi; /* (sxtw : seuls les 32 bits du bas comptent) */
	if (r >= -55)
		return 4;
	if (r >= -65)
		return 3;
	if (r >= -75)
		return 2;
	return 1;
}

void wd_bars(long x, long top, long lit, u32 on, u32 off)
{
	for (u64 i = 0; i < 4; i++) {
		long h = 4 + 3 * (long)i; /* hauteur : 4, 7, 10, 13 */
		/* (allumée si i < lit, comparaison non signée comme l'asm) */
		ui_fill(x + 4 * (long)i, top + 13 - h, 3, h, i < (u64)lit ? on : off);
	}
}

void wd_lock(long x, long top, u32 color)
{
	ui_fill(x + 2, top, 4, 1, color); /* anse : un cadre de 4 x 5 */
	ui_fill(x + 1, top + 1, 1, 4, color);
	ui_fill(x + 6, top + 1, 1, 4, color);
	ui_fill(x, top + 5, 8, 5, color); /* corps : 8 x 5 */
}

void wd_check(long x, long top, u32 color)
{
	for (long i = 0; i < 3; i++) /* branche courte : vers le bas */
		ui_fill(x + i, top + 3 + i, 2, 2, color);
	for (long i = 0; i < 6; i++) /* branche longue : vers le haut */
		ui_fill(x + 3 + i, top + 5 - i, 2, 2, color);
}
