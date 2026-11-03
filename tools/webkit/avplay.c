/*
 * avplay.c -- essai du moteur media de WebKit sur ARMPI sans WebKit (docs/WEBKIT.md, « Son ») : le
 * lecteur de la bibliotheque av d'Onyx (celui que pilote MediaPlayerPrivateOnyx) lit un fichier, le
 * son sort par AudioKit (glue/av_sound_armpi.c), la video par le point de branchement de MediaKit
 * (glue/av_video_armpi.c : aucun decodeur aujourd'hui).
 *
 *   avplay <fichier> [secondes]     -> lignes « avplay: t=... » puis « avplay: ok » / « avplay: FAIL ... »
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <av.h>

int main (int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf (stderr, "usage: avplay <fichier> [secondes]\n");
		return 2;
	}
	double limit = argc > 2 ? atof (argv[2]) : 10;
	struct av_player *p = av_player_new (AV_PIX_BGRA, NULL);
	if (!p || av_player_open_file (p, argv[1]) != AV_OK)
	{
		printf ("avplay: FAIL ouverture de %s\n", argv[1]);
		return 1;
	}
	av_player_play (p);
	struct av_player_status st = { 0 };
	struct av_video_out v;
	av_us start = av_now (), last = 0;
	int heard = 0;
	for (;;)
	{
		av_player_poll (p, &st, &v);
		heard |= st.sound;
		av_us now = av_now ();
		if (now - last >= 500000)
		{
			printf ("avplay: t=%.2f s / %.2f s, pret %d, son %d (entendu %d), video %d, erreur %d\n",
				st.time / 1e6, st.duration > 0 ? st.duration / 1e6 : 0.0, st.ready, st.has_audio,
				st.sound, st.has_video, st.error);
			fflush (stdout);
			last = now;
		}
		if (st.ended || st.error || (now - start) / 1e6 > limit)
			break;
		usleep (20000);
	}
	double elapsed = (av_now () - start) / 1e6;
	int ok = !st.error && st.has_audio && heard && st.time > 0;
	printf ("avplay: %s : %.2f s joues en %.2f s, son %s, fin %d, erreur %d\n", ok ? "ok" : "FAIL",
		st.time / 1e6, elapsed, heard ? "par AudioKit" : "absent", st.ended, st.error);
	av_player_free (p);
	return ok ? 0 : 1;
}
