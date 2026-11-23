//
// linux_main.cpp -- Arabesque sous Linux : l'entree du programme. Le main.cpp de Korin (src/apps/arabesque) est
// compile tel quel, renomme kport_app_main (-Dmain=kport_app_main) ; ici, avant lui, les chemins de Korin
// (korin_paths.cpp) et le menu « Son » (la SoundFont), et un mode sans fenetre pour recomposer un preregage :
//
//   arabesque [fichier]                                   la fenetre (comme sous Korin)
//   arabesque --render prereglage.arb sortie.mid [--wav sortie.wav] [--mp3 sortie.mp3] [--fms sortie.fms]
//                                                         recompose le morceau du prereglage (meme graine -> meme .mid)
//   arabesque --soundfont fichier.sf2 ...                 une autre SoundFont pour ce lancement
//
// Le rendu passe par le meme moteur (recipe.h, replay.h, song.h) et le meme export que la fenetre (audio.h).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uikit.h>
#include "korin_port.h"
#include "audio.h"
#include "replay.h"
#include "fmsout.h"

int kport_app_main (int argc, char **argv);

static bool decode (const unsigned char *data, long size, Image &out)
{
	long im = ui_image_decode ((long) data, size);
	if (!im) return false;
	long h = 0, px = 0, w = uikit_image_size (im, &h, &px);
	image_alloc (out, (int) w, (int) h);
	const uint32_t *s = (const uint32_t *) px;
	for (long i = 0; i < w * h; i++) out.px[i] = s[i] & 0xFFFFFF;
	ui_image_free (im);
	return true;
}

static int audio_export (const char *path, int format, int bits)
{
	g_synth = ak_synth_new (0, 0);
	if (g_synth < 0) { fprintf (stderr, "arabesque: pas de SoundFont (%s)\n", kport_soundfont ()); return 1; }
	g_synthOk = true;
	snprintf (g_expPath, sizeof g_expPath, "%s", path);
	g_expFormat = format; g_expBits = bits; g_expCancel = false; g_expState = EXPORT_RUN;
	static short buf[BLOCK * 2];
	export_run (buf);
	ak_synth_free (g_synth);
	g_synthOk = false;
	if (g_expState != EXPORT_DONE) { fprintf (stderr, "arabesque: impossible d'ecrire %s\n", path); return 1; }
	printf ("%s\n", path);
	return 0;
}

static int render (int argc, char **argv)
{
	const char *preset = 0, *mid = 0, *wav = 0, *mp3 = 0, *fms = 0;
	for (int i = 2; i < argc; i++)
	{
		if (!strcmp (argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
		else if (!strcmp (argv[i], "--mp3") && i + 1 < argc) mp3 = argv[++i];
		else if (!strcmp (argv[i], "--fms") && i + 1 < argc) fms = argv[++i];
		else if (!preset) preset = argv[i];
		else if (!mid) mid = argv[i];
	}
	if (!preset) { fprintf (stderr, "arabesque --render prereglage.arb [sortie.mid] [--wav x.wav] [--mp3 x.mp3] [--fms x.fms]\n"); return 2; }
	Recipe *r = new Recipe ();
	const char *err = r->load (preset);
	if (err) { fprintf (stderr, "arabesque: %s : %s\n", preset, err); return 1; }
	g_decodeImage = decode;
	bool ok = recipe_replay (*r, g_song);
	g_style = *r->ch[0].style;
	if (!ok) fprintf (stderr, "arabesque: l'image gardee dans le prereglage ne se lit pas (un mandala la remplace)\n");
	int notes = 0;
	for (int i = 0; i < NPART; i++) notes += g_song.tr[i].notes ();
	fprintf (stderr, "arabesque: %s : %d notes, %u pulsations, graine %u\n", preset, notes, g_song.length (), r->seed);
	int rc = 0;
	if (mid)
	{
		if (!song_save_midi (g_song, g_style, mid)) { fprintf (stderr, "arabesque: impossible d'ecrire %s\n", mid); rc = 1; }
		else printf ("%s\n", mid);
	}
	if (fms)
	{
		FmsResult fr;
		char title[32]; const char *b = strrchr (fms, '/'); snprintf (title, sizeof title, "%s", b ? b + 1 : fms);
		char *dot = strrchr (title, '.'); if (dot) *dot = 0;
		if (!fms_export (g_song, g_style, fms, title, fr)) { fprintf (stderr, "arabesque: impossible d'ecrire %s\n", fms); rc = 1; }
		else printf ("%s\n", fms);
	}
	if (wav && audio_export (wav, AKF_WAV, 0)) rc = 1;
	if (mp3 && audio_export (mp3, AKF_MP3, 192000)) rc = 1;
	return rc;
}

int main (int argc, char **argv)
{
	kport_setup ("arabesque", argv[0]);
	// --soundfont : pour ce lancement seulement
	for (int i = 1; i + 1 < argc; i++)
		if (!strcmp (argv[i], "--soundfont"))
		{
			setenv ("KORIN_SOUNDFONT", argv[i + 1], 1);
			kport_setup ("arabesque", argv[0]);
			for (int k = i; k + 2 <= argc; k++) argv[k] = argv[k + 2];
			argc -= 2;
			break;
		}
	if (argc > 1 && !strcmp (argv[1], "--render")) return render (argc, argv);
	if (argc > 1 && (!strcmp (argv[1], "--help") || !strcmp (argv[1], "-h")))
	{
		printf ("arabesque [fichier .arb .ifs .stl .sca .mid .png .bmp]\n"
			"arabesque --render prereglage.arb [sortie.mid] [--wav x.wav] [--mp3 x.mp3] [--fms x.fms]\n"
			"arabesque --soundfont fichier.sf2 ...\n"
			"SoundFont : %s\nHOME:/ = %s\n", kport_soundfont (), kport_homedir ());
		return 0;
	}
	kport_soundfont_menu = 1;
	return kport_app_main (argc, argv);
}
