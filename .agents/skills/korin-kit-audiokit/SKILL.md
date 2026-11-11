---
name: korin-kit-audiokit
description: AudioKit, le son de Korin OS — flux de lecture et de capture vers le service sound, lecteur de fichiers en arrière-plan, décodage (WAV, FLAC, MP3, Ogg Vorbis/Opus, MIDI), encodage (WAV, MP3, Opus), synthèse FM et MIDI (fonctions ak_*). À charger pour tout programme qui joue, enregistre, décode ou synthétise du son.
---

# AudioKit : le son

AudioKit (`SD0:/lib/audiokit.so`, en assembleur) est **le seul chemin des programmes vers le son**. Il ouvre les
flux auprès du service `sound` (qui mélange, applique les volumes, choisit la sortie) et fait le reste dans le
process : décodeurs, lecteur, encodeurs, synthèse, conversion vers s16 stéréo 44,1 kHz.

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Guide de l'API (exemples, pièges) | `SD0:/apps/help.app/api/audiokit_fr.md` | `src/apps/help/data/api/audiokit_fr.md` |
| Table du kit | — | `src/kits/audiokit/audiokit.abi`, constantes `audiokit.inc` |
| En-tête C | `/gcc/include/kits/audiokit_abi.h` | généré depuis le `.abi` |
| Service, mixer, champs d'état | — | `docs/SON.md`, `src/inc/sound.inc` (`REC_SRC_*`, `SS_*`, `SO_*`) ; formats : `docs/KITS.md` |

Usages réels : `src/apps/media/player.h` (lecteur `ak_play*`), `src/bin/play/play.asm`, `src/bin/volume/volume.asm`.

## Ouvrir le kit

C / C++ :

```c
#include <audiokit_abi.h>
if (!audiokit_kit_open ()) return 1;    /* SD0:/lib/audiokit.so absent ; rappeler ne coûte rien */
```

`pibuild` lie déjà `libaudiokit.a` ; à la main : `gcc -I/gcc/include/kits main.c -laudiokit`.

Assembleur : `include '../../kits/audiokit/audiokit.inc'`, `kit_use '../../kits/audiokit/audiokit.abi'` dans le
segment de code, `bl kits_open` au démarrage, `kit_data` dans les données. AAPCS64 ordinaire (x0-x7 arguments,
x19-x28 gardés).

Les noms des fonctions sont les mêmes dans tous les langages ; les skills `korin-lang-*` expliquent le mécanisme.

## L'essentiel

Arguments `long` (pointeur : `(long) p`), poignées opaques. Erreurs négatives : `AKE_NOMEM` -12, `AKE_INVAL` -22,
`AKE_IO` -5, `AKE_CORRUPT` -74, `AKE_FORMAT` -95, ou une erreur d'AppKit.

Lire un fichier (le plus simple) — un thread du kit décode et joue :
- `ak_play(chemin, sortie 0)` → lecteur ; `ak_play_state(p)` → `AKP_PLAYING` 1, `AKP_PAUSED` 2, `AKP_ENDED` 3, `AKP_ERROR` 4
- `ak_play_pause(p, 1|0)`, `ak_play_seek(p, ms)`, `ak_play_pos(p)` (ms entendus), `ak_play_volume(p, 0..256)`
- `ak_play_decoder(p)` → décodeur pour `ak_dec_info` / `ak_dec_tag` ; `ak_play_stop(p)` **toujours**

Flux de PCM :
- `ak_open(fréquence, canaux, bits 8/16/24/32, sortie 0)` → flux ; `ak_close(s)`
- `ak_write(s, buf, octets)` → octets pris (n'attend pas) ; `ak_space(s)` → octets acceptés maintenant
- `ak_write_all(s, buf, octets)` attend la place (voir Pièges) ; `ak_drain(s)` attend la fin (10 s au plus)
- `ak_position(s)` → trames jouées ; `ak_volume(s, 0..256)` ; `ak_latency(s, 4096..65536)` (avance en octets)

Décoder (format reconnu aux premiers octets) :
- `ak_dec_open(chemin)` / `ak_dec_open_mem(buf, n)` → décodeur ; `ak_dec_close(d)`
- `ak_dec_info(d, (long) long info[AKI_SIZE/8])` : `info[AKI_RATE/8]`, `AKI_CHANNELS`, `AKI_DURATION` (ms), `AKI_FORMAT` (`AKF_*`)…
- `ak_dec_read(d, buf, trames)` → trames s16 entrelacées, canaux de la source ; 0 : fin
- `ak_dec_seek(d, ms)`, `ak_dec_tell(d)`, `ak_dec_tag(d, AKT_TITLE|AKT_ARTIST|AKT_ALBUM, buf, taille)`

Encoder : `ak_enc_open(chemin, AKF_WAV|AKF_MP3|AKF_OPUS, fréquence, canaux 1|2, débit 0)`,
`ak_enc_write(e, pcm s16, trames)`, `ak_enc_size(e)`, `ak_enc_close(e)`.

Capture : `ak_rec_open(source)` (0 micro, 1 boucle de la sortie principale, 0x100+n, 0x200+n),
`ak_rec_read(r, buf, octets)` (n'attend pas, s16 stéréo 44,1 kHz), `ak_rec_status(r, long[3])`, `ak_rec_close(r)`.

Paquets (démultiplexeur à soi ou MediaKit) : `ak_codec_open(AKF_*, fréquence, canaux, extra, n)`,
`ak_codec_decode(c, paquet, n, sortie, trames max, AKC_S16|AKC_F32)`, `ak_codec_reset`, `ak_codec_info`, `ak_codec_close`.

Synthèse : FM `ak_fm_new(voix)`, `ak_fm_live(fm, 0)` (thread de rendu ; -1 l'arrête),
`ak_fm_start(fm, voix, mHz, AKW_SINE…, vol 0..255)`, `ak_fm_stop(fm, voix|-1)`, `ak_fm_free` ;
MIDI `ak_synth_new(0, 0)` (SoundFont du système, sinon banque FM), `ak_synth_midi(sy, canal, 0x90, touche, vél)`,
`ak_synth_render(sy, s16 stéréo, trames)` ; notes : `ak_note_mhz(69)` = 440 000.

Mixer (rarement utile) : `ak_backend()`, `ak_state()` (tampon de `sound`, lecture seule), `ak_out_volume`,
`ak_client_volume`, `ak_route`, `ak_main`. Outils : `ak_resample`, `ak_mix_s16`, `ak_gain_s16`,
`ak_volume_curve`, `ak_wav_header`.

## Exemple

Jouer un fichier (adapté de « Exemple : jouer un fichier » du guide) :

```c
#include <stdio.h>
#include <unistd.h>
#include <audiokit_abi.h>

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "SD0:/res/audiotest/jfk.mp3";
	if (!audiokit_kit_open ()) { printf ("AudioKit absent\n"); return 1; }
	long p = ak_play ((long) path, 0);
	if (p < 0) { printf ("%s : erreur %ld\n", path, p); return 1; }

	long info[AKI_SIZE / 8];
	char title[128];
	long dec = ak_play_decoder (p);
	ak_dec_info (dec, (long) info);
	if (ak_dec_tag (dec, AKT_TITLE, (long) title, sizeof title) <= 0)
		snprintf (title, sizeof title, "%s", path);
	printf ("%s : %ld Hz, %ld ms\n", title, info[AKI_RATE / 8], info[AKI_DURATION / 8]);

	for (;;)
	{
		long st = ak_play_state (p);
		if (st == AKP_ENDED || st == AKP_ERROR) break;
		usleep (250000);
	}
	ak_play_stop (p);		/* toujours : arrête le thread, ferme flux et fichier */
	return 0;
}
```

## Pièges

- **Vieux code** : avant le 11/11/2026, `audiokit_abi.h` déclarait `ak_write_all (void)` et `ak_enc_open_io (long)`, et
  des apps les appellent encore par un pointeur transtypé. L'en-tête est juste désormais (3 et 7 `long`) : appelez-les
  directement.
- **Octets ou trames** : `ak_write`, `ak_space`, `ak_pending`, `ak_rec_read` comptent en **octets** ;
  `ak_dec_read`, `ak_enc_write`, `ak_*_render`, `ak_position` en **trames**.
- **Canaux et fréquence de la source** : ouvrir le flux comme le fichier (`AKI_RATE`, `AKI_CHANNELS`) ; Opus est
  toujours à 48 kHz ; l'encodeur ne prend que 1 ou 2 canaux.
- **`ak_write` n'attend pas** : dans une boucle d'interface, écrire ce que `ak_space` accepte ; `ak_write_all`
  seulement dans un thread à soi.
- **Lecteur** : `ak_play_stop` même après `AKP_ENDED` ; jamais `ak_dec_read` sur `ak_play_decoder`.
- **Encodeur** : sans `ak_enc_close`, WAV aux tailles fausses, MP3 sans sa dernière trame ; lire `ak_enc_size`
  avant la fermeture.
- **Synthé MIDI sans verrou** (`ak_synth_*`) : protéger par un mutex si deux threads s'en servent ; `ak_fm_*` en a un.
- **Pas de micro** sous QEMU ni sur un Pi sans carte son USB : `ak_rec_open (0)` échoue.
- **Sous QEMU**, `sound` a une sortie virtuelle au rythme de l'horloge : tout tourne, on n'entend rien.
- **Pas d'autre chemin** : aucun appel noyau ni périphérique pour le son ; tout passe par AudioKit.
