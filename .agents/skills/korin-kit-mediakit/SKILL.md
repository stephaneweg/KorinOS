---
name: korin-kit-mediakit
description: MediaKit, la vidéo de Korin OS — ouvre WebM/Matroska, MP4 et RIFF, sépare les pistes, décode VP8, VP9 et AV1 en images XRGB8888 à l'échelle, rend le son en s16 par AudioKit (fonctions mk_*). À charger pour tout programme qui lit, inspecte, affiche ou extrait une vidéo.
---

# MediaKit : la vidéo

MediaKit (`SD0:/lib/mediakit.so`) est un kit en C **précompilé** (bibliothèque *av* d'Onyx, libvpx, dav1d ;
`prebuilt/mediakit.so`, construit par `tools/mediakit`, hors du selfhost). Il n'a **aucun codec audio** : Opus,
Vorbis, MP3 et FLAC sont décodés par AudioKit au travers de MediaKit.

| | |
|---|---|
| Conteneurs | WebM / Matroska (`.webm`, `.mkv`, `.mka`), MP4 (fragmenté compris), RIFF |
| Vidéo | VP8, VP9, AV1 8 bits, I420 non compressé |
| Pas pris | H.264, H.265, AAC |

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Guide de l'API (exemples, pièges) | `SD0:/apps/help.app/api/mediakit_fr.md` | `src/apps/help/data/api/mediakit_fr.md` |
| Table du kit | — | `src/kits/mediakit/mediakit.abi`, structures asm `mediakit.inc` |
| En-têtes C | `/gcc/include/kits/mediakit.h` (structures) et `mediakit_abi.h` (fonctions) | `src/kits/mediakit/mediakit.h` |
| Conception | — | `docs/MEDIAKIT.md`, `docs/KITS.md` |

Usages réels : `src/apps/media/thumbs.h` (vignettes : `mk_open`, `mk_seek`, `mk_video_next`) ;
`src/bin/mediatest/mediatest.asm` (assembleur). Vidéos d'essai : `SD0:/home/video`.

## Ouvrir le kit

C / C++ — deux en-têtes, **dans cet ordre** :

```c
#include <mediakit.h>        /* structures mk_info, mk_track, mk_frame, mk_packet, mk_stats */
#include <mediakit_abi.h>    /* fonctions et relais */
if (!mediakit_kit_open ()) return 1;     /* SD0:/lib/mediakit.so absent */
```

`pibuild` lie déjà `libmediakit.a` ; à la main : `gcc -I/gcc/include/kits main.c -lmediakit -laudiokit`
(AudioKit pour jouer le son : `audiokit_kit_open ()` aussi). `mediakit.h` offre en plus `mk_kit_open (&api, kit_open)`
(table de pointeurs) pour un programme qui ne se lie pas à `libmediakit.a`.

Assembleur : `kit_use '../../kits/mediakit/mediakit.abi'`, `bl kits_open`, `kit_data` ; structures de
`mediakit.inc` (`MKI_*`, `MKT_*`, `MKF_*`…). Fonctions C (AAPCS64 : x19-x28 et d8-d15 gardés, le reste changé).

Les noms des fonctions sont les mêmes dans tous les langages ; les skills `korin-lang-*` expliquent le mécanisme.

## L'essentiel

Arguments `long` (pointeurs `(long) &s`). Poignée > 0 ; erreurs `MK_E_*` négatives : `NOENT` -2, `IO` -5,
`NOMEM` -12, `INVAL` -22, `SPIPE` -29 (pas d'index), `NODATA` -61, `BADMSG` -74, `UNSUP` -95.
`(const char *) mk_error_text (err)` les dit en clair. Champs des structures : `int64_t`.

- `mk_open(chemin)` → poignée ; choisit la première vidéo décodable et la première piste audio ; `mk_close(h)`
- `mk_info(h, &mk_info)` : `duration` (µs), `ntracks`, `vtrack`/`atrack` (-1 : aucune), `width`, `height`,
  `fps1000`, `rate`, `channels`, `vok`/`aok` (1 : décodable), `container`, `vname`, `aname`
- `mk_track(h, n, &mk_track)` : `kind` (`MK_VIDEO`/`MK_AUDIO`), `codec` (`MK_C_*`), `extra`/`extra_len`,
  `supported`, `codec_id` (« V_VP9 »), `delay` (µs, pré-saut Opus)
- `mk_select(h, piste vidéo, piste audio)` (-1 : aucune) → 0 ; rouvre les décodeurs
- `mk_video_next(h, &mk_frame ou 0)` → 1 image décodée et **gardée**, 0 fin ; `mk_frame` : `pts`, `dur` (µs), `width`, `height`, `decode_us`
- `mk_video_convert(h, pixels, pas en octets, w, h)` → 0 : l'image gardée en XRGB8888 à cette taille (bilinéaire)
- `mk_video_read(h, pixels, pas, w, h, &mk_frame)` → les deux d'un coup
- `mk_audio_open(h, fréquence)` → 0 ; `MK_E_UNSUP` (codec non pris), `MK_E_NODATA` (pas de son)
- `mk_audio_read(h, s16 stéréo, trames max, (long) &int64 pts ou 0)` → trames (0 : fin)
- `mk_audio_packet(h, &mk_packet)` → 1 paquet brut (`data`, `size`, `pts`, `key`, `codec`), pour `ak_codec_decode`
- `mk_seek(h, µs)` → temps atteint (image clé d'avant) ; `mk_stats(h, &mk_stats)` (`dropped`…) ; `mk_version()`

## Exemple

Une image du milieu, réduite (adapté de « Exemple : ce que contient une vidéo » du guide) :

```c
#include <stdio.h>
#include <stdlib.h>
#include <mediakit.h>
#include <mediakit_abi.h>

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/home/video/sintel-vp9-opus.webm";
	if (!mediakit_kit_open ()) return 1;
	long h = mk_open ((long) path);
	if (h < 0) { printf ("%s : %s\n", path, (const char *) mk_error_text (h)); return 1; }
	struct mk_info in;
	mk_info (h, (long) &in);
	printf ("%s, %s %lld x %lld, %lld ms\n", in.container, in.vname,
		(long long) in.width, (long long) in.height, (long long) (in.duration / 1000));
	if (in.vtrack < 0 || !in.vok) { mk_close (h); return 0; }

	mk_select (h, in.vtrack, -1);		/* pas de son : ses paquets ne s'accumulent pas */
	if (mk_seek (h, in.duration / 2) < 0)	/* MK_E_SPIPE : pas d'index */
		printf ("pas de recherche : image du debut\n");
	enum { W = 160, H = 90 };
	unsigned *px = malloc (W * H * 4);
	struct mk_frame f;
	if (px && mk_video_read (h, (long) px, W * 4, W, H, (long) &f) == 1)
		printf ("image a %lld ms, decodee en %lld us\n", (long long) (f.pts / 1000), (long long) f.decode_us);
	free (px);
	mk_close (h);
	return 0;
}
```

Lecteur dans une fenêtre UIKit, son par AudioKit, paquets bruts : autres exemples du guide.

## Pièges

- **Ordre des en-têtes** : `mediakit.h` avant `mediakit_abi.h`, sinon les `#define MK_C_*` cassent l'`enum`.
- **Le pas est en octets** (largeur × 4) ; dans une fenêtre UIKit, `ui.w * 4`, et l'on peut convertir directement
  dans `ui.surf + y * ui.w + x`. (GpuKit compte en pixels.)
- **Image gardée** : `mk_video_convert` ne vaut que pour la dernière image de `mk_video_next`.
- **Piste choisie que personne ne lit** : ses paquets s'accumulent (4096 paquets ou 48 Mo par piste) puis les plus
  vieux sont jetés. `mk_select (h, vtrack, -1)` si le son ne sert pas. Lire vidéo et son en alternance dans le même
  thread est le cas prévu.
- **`mk_audio_read` et `mk_audio_packet`** s'excluent sur une même poignée.
- **`mk_seek`** revient à l'image clé d'avant : décoder sans montrer jusqu'au temps voulu. WebM sans Cues : seul 0
  marche. Media rouvre la poignée après un `mk_seek` refusé (`src/apps/media/thumbs.h`).
- **AAC et H.264** ne sont pas pris : `mk_audio_open` rend `MK_E_UNSUP`, la vidéo reste lisible muette.
- **Un thread par poignée** (plusieurs poignées sur plusieurs threads : permis). Les données rendues (`extra`,
  `data`, `mk_error_text`) appartiennent au kit.
- **Opus** rend du 48 kHz par `ak_codec_decode` ; le pré-saut (`delay`) et le rognage sont à l'appelant.
- **Sous QEMU**, tout marche mais lentement : VP9 480p n'y tient pas le temps réel.
