# MediaKit : la vidéo

**MediaKit** (`SD0:/lib/mediakit.so`) ouvre les fichiers vidéo, sépare leurs pistes et décode les images. C'est un kit en C précompilé (la bibliothèque *av* d'Onyx, libvpx et dav1d), partagé par toutes les applications : la bibliothèque **Media** et le lecteur `mediaplayer` s'en servent.

| | |
|---|---|
| Conteneurs | WebM / Matroska (`.webm`, `.mkv`, `.mka`), MP4 / ISO BMFF (fragmenté compris), RIFF |
| Vidéo | VP8, VP9 (libvpx), AV1 8 bits (dav1d), I420 non compressé |
| Image rendue | XRGB8888 (`0x00RRGGBB`), à la taille de l'image ou mise à l'échelle (bilinéaire) |
| Son | **pas de codec dans MediaKit** : Opus, Vorbis, MP3 et FLAC sont décodés par [AudioKit](audiokit_fr.md) ; MediaKit rend du s16 stéréo à la fréquence voulue |
| Pas pris en charge | H.264, H.265, AAC |

Les décodeurs tournent sur un seul thread, celui qui appelle. Les vidéos d'essai de la carte sont dans `SD0:/home/video`.

## Ouvrir le kit

Deux en-têtes, **dans cet ordre** : `mediakit.h` (les structures) puis `mediakit_abi.h` (les fonctions et leurs relais).

```c
#include <mediakit.h>
#include <mediakit_abi.h>

if (!mediakit_kit_open ()) return 1;   // SD0:/lib/mediakit.so absent
```

Rien à ajouter à l'édition de liens : `pibuild` prend toutes les bibliothèques de `/gcc/lib`. Pour le son d'une vidéo, ouvrez aussi AudioKit (`audiokit_kit_open`), qui joue le PCM rendu par MediaKit.

`mediakit.h` contient aussi `mk_kit_open (&api, kit_open)`, une autre façon d'ouvrir le kit, par une table de pointeurs de fonctions : elle sert aux programmes qui ne se lient pas à `libmediakit.a`. Avec `pibuild`, les relais de `mediakit_abi.h` suffisent.

## Les fonctions

Une poignée (> 0) représente un fichier ouvert. Elle ne sert qu'à **un thread à la fois** ; plusieurs poignées sur plusieurs threads sont permises. Les erreurs sont négatives.

| Fonction | Arguments | Résultat |
|---|---|---|
| `mk_version` | – | `0x00010000` |
| `mk_open` | chemin | poignée, ou `MK_E_*` ; lit jusqu'à connaître les pistes, choisit la première vidéo décodable et la première piste audio |
| `mk_close` | poignée | 0 |
| `mk_info` | poignée, `struct mk_info *` | 0 |
| `mk_track` | poignée, numéro de piste, `struct mk_track *` | 0 |
| `mk_select` | poignée, piste vidéo, piste audio (-1 : aucune) | 0 ; les décodeurs sont rouverts |
| `mk_video_next` | poignée, `struct mk_frame *` (ou 0) | 1 : une image décodée et **gardée** ; 0 : fin |
| `mk_video_convert` | poignée, pixels, pas (octets), largeur, hauteur | 0 : l'image gardée en XRGB8888 à cette taille |
| `mk_video_read` | poignée, pixels, pas, largeur, hauteur, `struct mk_frame *` | 1 : les deux à la fois ; 0 : fin |
| `mk_audio_open` | poignée, fréquence | 0 : `mk_audio_read` rendra du s16 stéréo à cette fréquence ; `MK_E_UNSUP` : codec non pris (piste muette) ; `MK_E_NODATA` : pas de son |
| `mk_audio_read` | poignée, tampon s16 stéréo, trames au plus, `int64_t *pts` (ou 0) | trames écrites (0 : fin) ; pts (µs) de la première |
| `mk_audio_packet` | poignée, `struct mk_packet *` | 1 : le paquet audio suivant, brut ; 0 : fin |
| `mk_seek` | poignée, temps (µs) | temps atteint (µs, l'image clé d'avant) ; `MK_E_SPIPE` : pas d'index |
| `mk_stats` | poignée, `struct mk_stats *` | 0 |
| `mk_error_text` | erreur | texte UTF-8 (au kit) |

## Les structures

Tous les champs numériques sont des entiers de 64 bits.

| `struct mk_info` | Contenu |
|---|---|
| `duration` | durée (µs, 0 : inconnue) |
| `ntracks` | nombre de pistes |
| `vtrack`, `atrack` | pistes choisies (-1 : aucune) |
| `width`, `height` | taille d'affichage |
| `fps1000` | images par seconde × 1000 |
| `rate`, `channels` | son de la piste choisie |
| `vcodec`, `acodec` | `MK_C_*` |
| `vok`, `aok` | 1 : décodable par MediaKit (le son : par AudioKit au travers de MediaKit) |
| `container`, `vname`, `aname` | noms (16 octets chacun) : « webm », « VP9 », « Opus »... |

| `struct mk_track` | Contenu |
|---|---|
| `kind` | `MK_VIDEO` 1, `MK_AUDIO` 2 |
| `codec` | `MK_C_VP8`, `MK_C_VP9`, `MK_C_AV1`, `MK_C_H264`... `MK_C_OPUS`, `MK_C_VORBIS`, `MK_C_AAC`, `MK_C_MP3`, `MK_C_FLAC`, `MK_C_PCM_S16LE` |
| `width`, `height`, `rate`, `channels`, `duration` | selon le genre |
| `extra`, `extra_len` | données d'initialisation du codec (pour `ak_codec_open`) |
| `supported` | 1 : décodable |
| `name`, `codec_id` | nom de la piste, nom du codec dans le conteneur (« V_VP9 », « A_OPUS ») |
| `delay` | µs (pré-saut d'Opus) |

| Structure | Champs |
|---|---|
| `struct mk_frame` | `pts`, `dur` (µs), `width`, `height` (taille décodée), `decode_us` |
| `struct mk_packet` | `data`, `size`, `pts`, `dur`, `key` (1 : paquet clé), `codec` |
| `struct mk_stats` | `vframes`, `vdecode_us`, `aframes`, `adecode_us`, `dropped` (paquets jetés), `heap_big` |

Erreurs : `MK_E_NOENT` (-2), `MK_E_IO` (-5), `MK_E_BADF` (-9), `MK_E_NOMEM` (-12), `MK_E_INVAL` (-22), `MK_E_SPIPE` (-29), `MK_E_NODATA` (-61), `MK_E_BADMSG` (-74), `MK_E_UNSUP` (-95).

## Lire les images

`mk_video_next` décode l'image suivante et la garde ; `mk_video_convert` la convertit autant de fois qu'on veut, à la taille voulue, par exemple après un redimensionnement. `mk_video_read` fait les deux d'un coup. Le pas est en **octets** ; pour la fenêtre d'UIKit, c'est `ui.w * 4`, et on peut convertir **directement dans les pixels de la fenêtre**.

Les paquets sont rangés par piste (au plus 4096 paquets ou 48 Mo par piste) : lire la vidéo et le son dans le même thread, en alternance, est le cas prévu. Une piste choisie que personne ne lit perd ses plus vieux paquets ; si vous ne voulez pas le son, `mk_select (h, vtrack, -1)`.

## Exemple : ce que contient une vidéo

Programme en mode texte : les informations, les pistes, puis une image du milieu, réduite à 160 × 90, et le temps de décodage.

```c
#include <stdio.h>
#include <stdlib.h>
#include <mediakit.h>
#include <mediakit_abi.h>

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/home/video/sintel-vp9-opus.webm";
	if (!mediakit_kit_open ()) { printf ("MediaKit absent\n"); return 1; }

	long h = mk_open ((long) path);
	if (h < 0) { printf ("%s : %s\n", path, (const char *) mk_error_text (h)); return 1; }
	struct mk_info in;
	mk_info (h, (long) &in);
	printf ("%s : %s, %lld ms, %lld pistes\n", path, in.container, (long long) (in.duration / 1000), (long long) in.ntracks);
	for (long i = 0; i < in.ntracks; i++)
	{
		struct mk_track t;
		if (mk_track (h, i, (long) &t) != 0) continue;
		if (t.kind == MK_VIDEO)
			printf ("  %ld : vidéo %s %lld x %lld%s\n", i, t.codec_id, (long long) t.width, (long long) t.height, t.supported ? "" : " (non prise)");
		else
			printf ("  %ld : son %s %lld Hz, %lld canaux%s\n", i, t.codec_id, (long long) t.rate, (long long) t.channels, t.supported ? "" : " (non pris)");
	}
	if (in.vtrack < 0 || !in.vok) { printf ("pas de vidéo décodable\n"); mk_close (h); return 0; }

	mk_select (h, in.vtrack, -1);			// pas de son : ses paquets ne s'accumulent pas
	long at = mk_seek (h, in.duration / 2);		// l'image clé d'avant le milieu
	if (at < 0) at = 0;					// (MK_E_SPIPE : pas d'index, on reste au début)
	enum { W = 160, H = 90 };
	unsigned *px = malloc (W * H * 4);
	struct mk_frame f;
	if (px && mk_video_read (h, (long) px, W * 4, W, H, (long) &f) == 1)
	{
		unsigned long r = 0, g = 0, b = 0;
		for (int i = 0; i < W * H; i++) { r += px[i] >> 16 & 255; g += px[i] >> 8 & 255; b += px[i] & 255; }
		printf ("image à %lld ms (%lld x %lld, décodée en %lld µs) : couleur moyenne #%02lx%02lx%02lx\n",
			(long long) (f.pts / 1000), (long long) f.width, (long long) f.height, (long long) f.decode_us,
			r / (W * H), g / (W * H), b / (W * H));
	}
	struct mk_stats s;
	mk_stats (h, (long) &s);
	printf ("%lld images décodées, %lld µs\n", (long long) s.vframes, (long long) s.vdecode_us);
	free (px);
	mk_close (h);
	return 0;
}
```

## Exemple : un petit lecteur

Une fenêtre redimensionnable aux couleurs du thème : l'image est convertie directement dans les pixels de la fenêtre, ajustée sous la bande du haut ; le son passe par un flux d'AudioKit, tenu plein sans attendre. L'horloge est celle du système : simple, mais elle ne suit pas une sortie qui prendrait du retard (le lecteur `mediaplayer` se cale sur la position du flux, `ak_position`).

```c
#include <stdio.h>
#include <time.h>
#include <uikit.h>
#include <mediakit.h>
#include <mediakit_abi.h>
#include <audiokit_abi.h>

enum { RATE = 44100, CHUNK = 1024, TOP = 40 };

static struct ui_vars ui;
static struct mk_info in;
static const char *title;

// une couleur du thème (TJ_* : décalages dans la table des jetons)
static unsigned tok (int off) { return *(const unsigned *) ((const char *) ui_jetons () + off) & 0xFFFFFF; }
static long now_us (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000L + t.tv_nsec / 1000; }

// le rectangle de l'image : sous la bande du haut, proportions gardées
static int video_rect (long *x, long *y, long *w, long *h)
{
	long aw = ui.w - 16, ah = ui.h - TOP - 8;
	if (aw < 16 || ah < 16 || in.width <= 0 || in.height <= 0) return 0;
	*w = aw; *h = aw * in.height / in.width;
	if (*h > ah) { *h = ah; *w = ah * in.width / in.height; }
	*x = 8 + (aw - *w) / 2; *y = TOP + (ah - *h) / 2;
	return *w > 0 && *h > 0;
}

static long paint (long w, long h)
{
	long x, y, vw, vh;
	ui_fill (0, 0, ui.w, ui.h, tok (TJ_FACE));
	ui_text ((long) title, 12, 26, tok (TJ_TEXTE1), 14, (long) ui.font_b, 0);
	if (video_rect (&x, &y, &vw, &vh)) ui_fill (x, y, vw, vh, tok (TJ_SOMBRE));
	ui_update_all ();
	return 0;
}

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/home/video/sintel-vp8-opus.webm";
	title = path;
	if (!uikit_open (&ui)) return 1;
	if (!mediakit_kit_open ())
	{
		ui_message ((long) "MediaKit", (long) ui_tr ((long) "SD0:/lib/mediakit.so est absent."), UM_OK);
		return 1;
	}
	long h = mk_open ((long) path);
	if (h < 0) { ui_message ((long) path, mk_error_text (h), UM_OK); return 1; }
	mk_info (h, (long) &in);
	if (in.vtrack < 0 || !in.vok) { ui_message ((long) path, mk_error_text (MK_E_UNSUP), UM_OK); return 1; }

	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	if (ui_window_ex ((scr[0] - 680) / 2, scr[3] + 40, 680, 440, (long) ui_tr ((long) "Vidéo"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (320, 240);
	ui_on_resize ((long) paint, 0);
	paint (ui.w, ui.h);

	// le son : s16 stéréo 44,1 kHz, le format des flux de sound (aucune conversion)
	long snd = -1;
	if (in.atrack >= 0 && in.aok && audiokit_kit_open () && mk_audio_open (h, RATE) == 0) snd = ak_open (RATE, 2, 16, 0);
	if (snd < 0) mk_select (h, in.vtrack, -1);
	static short pcm[CHUNK * 2];
	int audio_end = snd < 0;

	struct mk_frame fr;
	int have = mk_video_next (h, (long) &fr) == 1;
	long t0 = now_us ();
	for (;;)
	{
		long t = ui_event (have ? 5 : 100);
		if (t == WEV_CLOSE) break;
		while (!audio_end && ak_space (snd) >= CHUNK * 4)
		{
			long n = mk_audio_read (h, (long) pcm, CHUNK, 0);
			if (n <= 0) { audio_end = 1; break; }
			ak_write (snd, (long) pcm, n * 4);
		}
		if (have && fr.pts <= now_us () - t0)		// l'heure de cette image est venue
		{
			long x, y, w, vh;
			if (video_rect (&x, &y, &w, &vh) && mk_video_convert (h, (long) (ui.surf + y * ui.w + x), ui.w * 4, w, vh) == 0)
				ui_update (x, y, w, vh);
			have = mk_video_next (h, (long) &fr) == 1;
		}
	}
	if (snd >= 0) ak_close (snd);
	mk_close (h);
	return 0;
}
```

Les textes passés à `ui_tr` sont les clés françaises du dictionnaire du système (le `lang/en.txt` d'une app en donne la version anglaise), comme dans le [squelette](skeleton_fr.md).

## Exemple : extraire le son en WAV

MediaKit sépare la piste audio et la fait décoder par AudioKit ; l'encodeur d'AudioKit écrit le fichier. Un deuxième argument donne le point de départ en secondes.

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <mediakit.h>
#include <mediakit_abi.h>
#include <audiokit_abi.h>

enum { RATE = 44100, CHUNK = 4096 };

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/home/video/sintel-vp8-opus.webm";
	const char *out = "SD0:/home/soundtrack.wav";
	if (!mediakit_kit_open () || !audiokit_kit_open ()) return 1;

	long h = mk_open ((long) path);
	if (h < 0) { printf ("%s : %s\n", path, (const char *) mk_error_text (h)); return 1; }
	struct mk_info in;
	mk_info (h, (long) &in);
	if (in.atrack < 0) { printf ("pas de son\n"); mk_close (h); return 1; }
	mk_select (h, -1, in.atrack);			// pas de vidéo : rien à décoder pour rien
	if (argc > 2)
	{
		long at = mk_seek (h, (int64_t) atol (argv[2]) * 1000000);
		printf ("départ : %ld ms\n", at < 0 ? 0 : at / 1000);
	}
	long r = mk_audio_open (h, RATE);
	if (r != 0) { printf ("son %s : %s\n", in.aname, (const char *) mk_error_text (r)); mk_close (h); return 1; }

	long e = ak_enc_open ((long) out, AKF_WAV, RATE, 2, 0);
	if (e < 0) { mk_close (h); return 1; }
	short *pcm = malloc (CHUNK * 4);
	long n, frames = 0;
	int64_t first = -1, pts;
	while (pcm && (n = mk_audio_read (h, (long) pcm, CHUNK, (long) &pts)) > 0)
	{
		if (first < 0) first = pts;
		if (ak_enc_write (e, (long) pcm, n) < 0) break;
		frames += n;
	}
	ak_enc_close (e);
	free (pcm);
	mk_close (h);
	printf ("%s : %ld trames (%ld ms), depuis %lld ms\n", out, frames, frames * 1000 / RATE, (long long) (first / 1000));
	return 0;
}
```

## Exemple : les paquets bruts

`mk_audio_packet` rend les paquets de la piste audio tels qu'ils sont dans le fichier : c'est au programme de les décoder, ici par l'API par paquets d'AudioKit, avec les données d'initialisation de la piste. À ne pas mêler à `mk_audio_read` sur la même poignée.

```c
#include <stdio.h>
#include <mediakit.h>
#include <mediakit_abi.h>
#include <audiokit_abi.h>

// le codec d'un conteneur -> le format d'AudioKit (0 : pas pour AudioKit)
static long ak_format (long mk_codec)
{
	switch (mk_codec)
	{
	case MK_C_OPUS: return AKF_OPUS;
	case MK_C_VORBIS: return AKF_VORBIS;
	case MK_C_MP3: return AKF_MP3;
	case MK_C_FLAC: return AKF_FLAC;
	}
	return 0;
}

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/home/video/sintel-vp8-opus.webm";
	if (!mediakit_kit_open () || !audiokit_kit_open ()) return 1;
	long h = mk_open ((long) path);
	if (h < 0) return 1;
	struct mk_info in;
	struct mk_track t;
	mk_info (h, (long) &in);
	if (in.atrack < 0 || mk_track (h, in.atrack, (long) &t) != 0) { printf ("pas de son\n"); mk_close (h); return 1; }
	long fmt = ak_format (t.codec);
	if (!fmt) { printf ("%s : pas un codec d'AudioKit\n", t.codec_id); mk_close (h); return 1; }
	mk_select (h, -1, in.atrack);

	// MP3 n'a pas de données d'initialisation
	long c = ak_codec_open (fmt, t.rate, t.channels, fmt == AKF_MP3 ? 0 : (long) t.extra, fmt == AKF_MP3 ? 0 : t.extra_len);
	if (c < 0) { printf ("ak_codec_open : %ld\n", c); mk_close (h); return 1; }
	static short pcm[5760 * 8];			// le plus grand paquet : 5760 trames d'Opus, 8 canaux au plus
	struct mk_packet p;
	long packets = 0, frames = 0, bad = 0;
	while (mk_audio_packet (h, (long) &p) == 1)
	{
		long n = ak_codec_decode (c, (long) p.data, p.size, (long) pcm, 5760, AKC_S16);
		packets++;
		if (n < 0) bad++; else frames += n;
	}
	long info[AKI_SIZE / 8];
	ak_codec_info (c, (long) info);
	printf ("%s : %ld paquets (%ld refusés), %ld trames à %ld Hz, %ld canaux, pré-saut %lld µs\n", t.codec_id, packets, bad,
		frames, info[AKI_RATE / 8], info[AKI_CHANNELS / 8], (long long) t.delay);
	ak_codec_close (c);
	mk_close (h);
	return 0;
}
```

## Pièges

- **Ordre des en-têtes** : `mediakit.h` avant `mediakit_abi.h`. Dans l'autre ordre, les `#define MK_C_*` de `mediakit_abi.h` cassent l'`enum` de `mediakit.h`.
- **Le pas est en octets** pour `mk_video_convert` et `mk_video_read` (largeur × 4 pour une image pleine), contrairement à [GpuKit](gpukit_fr.md), qui compte en pixels.
- **L'image gardée** : `mk_video_convert` convertit la dernière image rendue par `mk_video_next` ; après l'appel suivant, la précédente est perdue.
- **Piste lue par personne** : ses paquets s'accumulent jusqu'à la limite puis les plus vieux sont jetés (`dropped` dans `mk_stats`). Désélectionnez la piste qui ne sert pas avec `mk_select`.
- **`mk_audio_read` et `mk_audio_packet`** s'excluent sur une même poignée.
- **`mk_seek`** revient à l'image clé d'avant : il faut décoder (et ne pas montrer) les images jusqu'au temps voulu. Sans index (WebM sans Cues), seul 0 marche (`MK_E_SPIPE`).
- **Son non pris** : `mk_audio_open` rend `MK_E_UNSUP` pour AAC ; la vidéo reste lisible, muette.
- **Un thread par poignée** : pour décoder dans un thread à part (comme Media), ouvrez la poignée dans ce thread ou ne l'utilisez que là. Les décodeurs ont leur propre pile de 2 Mo : rien à prévoir.
- **Sous QEMU**, tout marche, mais lentement : le VP9 480p n'y tient pas le temps réel.

## Voir aussi

- [AudioKit](audiokit_fr.md) : le son, les flux, l'API par paquets.
- [UIKit](uikit_fr.md) : les fenêtres et les surfaces.
- [GpuKit](gpukit_fr.md) : dessiner par le GPU.
- L'aide de **Media**, la bibliothèque multimédia faite sur MediaKit.
