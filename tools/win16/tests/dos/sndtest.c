/* sndtest.c -- essai du son de la machine DOS de win16 (étape D6) :
 *   SNDTEST OPL     détecte l'AdLib par ses minuteries, joue une gamme sur l'OPL (388h)
 *   SNDTEST SPK     une gamme sur le haut-parleur (PIT canal 2, port 61h)
 *   SNDTEST SB      une seconde de sinus par la Sound Blaster (DMA 1 auto-init, IRQ 5, BLASTER)
 * Compilé par Open Watcom (tools/win16/build-tests.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>

static void opl (int reg, int v)
{
	int i;
	outp (0x388, reg);
	for (i = 0; i < 6; i++)
		inp (0x388);
	outp (0x389, v);
	for (i = 0; i < 35; i++)
		inp (0x388);
}

static int adlib_detect (void)
{
	int s1, s2, i;
	opl (4, 0x60);
	opl (4, 0x80);
	s1 = inp (0x388);
	opl (2, 0xff);
	opl (4, 0x21);
	for (i = 0; i < 200; i++)
		inp (0x388);
	s2 = inp (0x388);
	opl (4, 0x60);
	opl (4, 0x80);
	return (s1 & 0xe0) == 0 && (s2 & 0xe0) == 0xc0;
}

static const unsigned fnum[8] = { 0x157, 0x181, 0x1b0, 0x1ca, 0x202, 0x241, 0x287, 0x2ae };

static void test_opl (void)
{
	int n;
	if (!adlib_detect ())
	{
		printf ("pas d'AdLib\n");
		return;
	}
	printf ("AdLib détectée : une gamme\n");
	opl (0x01, 0x20);
	opl (0x20, 0x01); opl (0x40, 0x10); opl (0x60, 0xf0); opl (0x80, 0x77);
	opl (0x23, 0x01); opl (0x43, 0x00); opl (0x63, 0xf0); opl (0x83, 0x77);
	opl (0xc0, 0x00);
	for (n = 0; n < 8; n++)
	{
		opl (0xa0, fnum[n] & 0xff);
		opl (0xb0, 0x20 | (4 << 2) | (fnum[n] >> 8));
		delay (300);
		opl (0xb0, (4 << 2) | (fnum[n] >> 8));
		delay (50);
	}
}

static void test_spk (void)
{
	static const unsigned hz[8] = { 262, 294, 330, 349, 392, 440, 494, 523 };
	int n;
	printf ("haut-parleur : une gamme\n");
	for (n = 0; n < 8; n++)
	{
		unsigned div = (unsigned) (1193182L / hz[n]);
		outp (0x43, 0xb6);
		outp (0x42, div & 0xff);
		outp (0x42, div >> 8);
		outp (0x61, inp (0x61) | 3);
		delay (250);
		outp (0x61, inp (0x61) & ~3);
		delay (50);
	}
}

static volatile int blocks;
static void (__interrupt __far *old_irq5) (void);
static void __interrupt __far sb_irq (void)
{
	inp (0x22e);
	blocks++;
	outp (0x20, 0x20);
}

static void dsp (int v)
{
	while (inp (0x22c) & 0x80)
		;
	outp (0x22c, v);
}

static void test_sb (void)
{
	static unsigned char buf[16384];
	unsigned char far *p;
	unsigned long lin;
	unsigned off, i;
	int t;
	/* reset du DSP */
	outp (0x226, 1);
	delay (1);
	outp (0x226, 0);
	for (t = 0; t < 1000 && !(inp (0x22e) & 0x80); t++)
		;
	if (inp (0x22a) != 0xaa)
	{
		printf ("pas de Sound Blaster\n");
		return;
	}
	dsp (0xe1);
	t = inp (0x22a);
	printf ("Sound Blaster, DSP %d.", t);
	t = inp (0x22a);
	printf ("%02d ; BLASTER=%s\n", t, getenv ("BLASTER") ? getenv ("BLASTER") : "?");
	/* un tampon qui ne passe pas une limite de 64 Ko : 440 Hz à 22 050 Hz */
	p = (unsigned char far *) buf;
	lin = ((unsigned long) FP_SEG (p) << 4) + FP_OFF (p);
	if ((lin & 0xffff) > 0xffff - 8192)
		lin = (lin + 0xffff) & ~0xffffUL;
	off = (unsigned) (lin - (((unsigned long) FP_SEG (p) << 4) + FP_OFF (p)));
	for (i = 0; i < 8192; i++)
		buf[off + i] = (unsigned char) (128 + 100 * sin (i * 2 * 3.14159265 * 440 / 22050));
	old_irq5 = _dos_getvect (0x0d);
	_dos_setvect (0x0d, sb_irq);
	outp (0x21, inp (0x21) & ~0x20);
	outp (0x0a, 5);
	outp (0x0c, 0);
	outp (0x0b, 0x59);	/* canal 1, lecture, auto-init */
	outp (0x02, (int) (lin & 0xff));
	outp (0x02, (int) ((lin >> 8) & 0xff));
	outp (0x83, (int) (lin >> 16));
	outp (0x03, (8192 - 1) & 0xff);
	outp (0x03, (8192 - 1) >> 8);
	outp (0x0a, 1);
	dsp (0xd1);
	dsp (0x40);
	dsp (256 - 1000000L / 22050);
	dsp (0x48);
	dsp ((4096 - 1) & 0xff);
	dsp ((4096 - 1) >> 8);
	dsp (0x1c);
	delay (1500);
	dsp (0xda);
	dsp (0xd3);
	outp (0x21, inp (0x21) | 0x20);
	_dos_setvect (0x0d, old_irq5);
	printf ("blocs joués (IRQ 5) : %d\n", blocks);
}

int main (int argc, char **argv)
{
	const char *w = argc > 1 ? argv[1] : "OPL";
	if (!stricmp (w, "OPL"))
		test_opl ();
	else if (!stricmp (w, "SPK"))
		test_spk ();
	else if (!stricmp (w, "SB"))
		test_sb ();
	return 0;
}
