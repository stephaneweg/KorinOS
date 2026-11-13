/*
 * avplay.c -- essai du moteur media de WebKit sur ARMPI sans WebKit (docs/WEBKIT.md, « Son et video ») :
 * le lecteur de la bibliotheque av d'Onyx (celui que pilote MediaPlayerPrivateOnyx) lit un fichier, le
 * son sort par AudioKit (glue/av_sound_armpi.c), la video par libvpx (VP8, VP9) et dav1d (AV1), lies
 * dans libonyxav.a (tools/webkit/av.mk).
 *
 *   avplay <fichier> [secondes]   lecture : lignes « avplay: t=... » puis « avplay: ok » / « avplay: FAIL ... »
 *   avplay --decode <fichier>     banc : toutes les images video decodees (et converties en BGRA) au plus vite
 *                                 -> « avplay: decode vp9 854x480 : 48 images en 1.31 s, 36.6 images/s »
 *   avplay --types                ce que repondent canPlayType / isTypeSupported (av_type_supported)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <av.h>

static int types (void)
{
	static const char *const mimes[] = {
		"video/webm", "video/webm; codecs=\"vp8, vorbis\"", "video/webm; codecs=\"vp8, opus\"",
		"video/webm; codecs=\"vp9, opus\"", "video/webm; codecs=\"vp09.00.10.08\"",
		"video/webm; codecs=\"av01.0.05M.08\"", "video/mp4; codecs=\"av01.0.05M.08\"",
		"video/mp4; codecs=\"vp09.00.10.08\"", "video/mp4; codecs=\"avc1.42E01E\"",
		"audio/webm; codecs=\"opus\"", "audio/ogg; codecs=\"opus\"", "audio/ogg; codecs=\"vorbis\"",
		"audio/mpeg", "audio/flac", "audio/wav", "audio/mp4; codecs=\"mp4a.40.2\"",
	};
	for (unsigned i = 0; i < sizeof mimes / sizeof mimes[0]; i++)
	{
		int smooth;
		int a = av_type_supported (mimes[i], 0, NULL), m = av_type_supported (mimes[i], 1, &smooth);
		printf ("avplay: %-42s canPlayType %-8s MSE %s\n", mimes[i], a == 2 ? "probably" : a ? "maybe" : "\"\"",
			m ? (smooth ? "oui (fluide)" : "oui") : "non");
	}
	int n;
	const struct av_codec_info *c = av_codec_list (&n);
	for (int i = 0; i < n; i++)
		printf ("avplay: codec %-6s %s\n", c[i].name, c[i].library);
	return 0;
}

/* toutes les images de la premiere piste video, decodees et converties comme le lecteur */
static int decode (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f)
	{
		printf ("avplay: FAIL ouverture de %s\n", path);
		return 1;
	}
	fseek (f, 0, SEEK_END);
	long size = ftell (f);
	fseek (f, 0, SEEK_SET);
	unsigned char *buf = malloc (size);
	if (!buf || fread (buf, 1, size, f) != (size_t) size)
	{
		printf ("avplay: FAIL lecture de %s\n", path);
		return 1;
	}
	fclose (f);
	struct av_demux *d = av_demux_new (av_probe (buf, size));
	av_demux_feed (d, 0, buf, size);
	av_demux_end (d);
	struct av_decoder *dec = NULL;
	int vtrack = -1, frames = 0, w = 0, h = 0;
	av_us start = 0, decode_us = 0;
	unsigned char *rgb = NULL;
	struct av_packet pkt;
	for (;;)
	{
		int r = av_demux_read (d, &pkt);
		if (r == AV_EOF || r < 0 || r == AV_AGAIN)
			break;
		if (vtrack < 0)
			for (int i = 0; i < av_demux_ntracks (d); i++)
			{
				const struct av_track *t = av_demux_track (d, i);
				if (t->kind == AV_VIDEO)
				{
					vtrack = i;
					dec = av_decoder_new (t);
					if (!dec)
					{
						printf ("avplay: FAIL pas de decodeur pour %s (%s)\n", t->codec_id, t->codec_str);
						return 1;
					}
					start = av_now ();
					break;
				}
			}
		if (pkt.track == vtrack && dec)
		{
			struct av_frame fr;
			av_us t0 = av_now ();
			av_decoder_send (dec, &pkt);
			while (av_decoder_receive (dec, &fr) == AV_OK)
			{
				if (fr.pix == AV_PIX_I420)
				{
					if (!rgb || fr.width != w || fr.height != h)
					{
						w = fr.width;
						h = fr.height;
						free (rgb);
						rgb = malloc ((size_t) w * h * 4);
					}
					av_yuv_to_rgb (&fr, rgb, w * 4, AV_PIX_BGRA);
				}
				frames++;
			}
			decode_us += av_now () - t0;
		}
		av_pkt_free (&pkt);
	}
	if (!dec)
	{
		printf ("avplay: FAIL pas de piste video dans %s\n", path);
		return 1;
	}
	double s = (av_now () - start) / 1e6;
	printf ("avplay: decode %s %dx%d : %d images en %.2f s (decodage %.2f s), %.1f images/s\n", av_decoder_name (dec),
		w, h, frames, s, decode_us / 1e6, frames / (decode_us > 0 ? decode_us / 1e6 : 1));
	av_decoder_free (dec);
	av_demux_free (d);
	return frames > 0 ? 0 : 1;
}

int main (int argc, char **argv)
{
	if (argc > 1 && !strcmp (argv[1], "--types"))
		return types ();
	if (argc > 2 && !strcmp (argv[1], "--decode"))
		return decode (argv[2]);
	if (argc < 2)
	{
		fprintf (stderr, "usage: avplay <fichier> [secondes] | --decode <fichier> | --types\n");
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
	int heard = 0, pictures = 0;
	unsigned serial = 0;
	for (;;)
	{
		if (av_player_poll (p, &st, &v) && v.pixels && v.serial != serial)
		{
			serial = v.serial;
			pictures++;
		}
		heard |= st.sound;
		av_us now = av_now ();
		if (now - last >= 500000)
		{
			printf ("avplay: t=%.2f s / %.2f s, pret %d, son %d (entendu %d), video %d %dx%d (%u decodees, %u sautees, %d ms), erreur %d\n",
				st.time / 1e6, st.duration > 0 ? st.duration / 1e6 : 0.0, st.ready, st.has_audio,
				st.sound, st.has_video, st.width, st.height, st.decoded, st.dropped, (int) (st.decode_us / 1000), st.error);
			fflush (stdout);
			last = now;
		}
		if (st.ended || st.error || (now - start) / 1e6 > limit)
			break;
		usleep (10000);
	}
	double elapsed = (av_now () - start) / 1e6;
	int ok = !st.error && st.time > 0 && (st.has_video ? pictures > 0 : st.has_audio && heard);
	printf ("avplay: %s : %.2f s joues en %.2f s, son %s, images %d montrees (%u decodees, %u sautees, %.1f images/s), fin %d, erreur %d\n",
		ok ? "ok" : "FAIL", st.time / 1e6, elapsed, heard ? "par AudioKit" : "absent", pictures, st.decoded, st.dropped,
		elapsed > 0 ? pictures / elapsed : 0.0, st.ended, st.error);
	av_player_free (p);
	return ok ? 0 : 1;
}
