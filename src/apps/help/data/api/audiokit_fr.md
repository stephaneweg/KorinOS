# AudioKit : le son

**AudioKit** (`SD0:/lib/audiokit.so`) est le seul chemin des programmes vers le son. Il ouvre des flux de lecture et de capture auprès du service `sound`, et fait tout le reste **dans le process de l'application** : décodeurs (WAV, FLAC, MP3, Ogg Vorbis, Ogg Opus, MIDI, FM Song), lecteur en arrière-plan, encodeurs (WAV, MP3, Opus), synthèse FM et synthétiseur General MIDI sur SoundFont, conversion et mixage.

Le kit est écrit en assembleur ; ses fonctions suivent l'AAPCS64 et ne prennent que des entiers, des pointeurs et des poignées opaques. Depuis le C, chaque argument est un `long`.

| Couche | Rôle |
|---|---|
| votre programme | appelle `ak_*` |
| AudioKit | décode, synthétise, convertit au format des flux (s16 stéréo 44,1 kHz) |
| `sound` (service) | mélange les flux, applique les volumes, choisit la sortie |
| pilotes | prise jack, HDMI, carte son USB, micro I2S |

Sous QEMU, `sound` a une **sortie virtuelle** qui consomme le son au rythme de l'horloge : un programme qui joue tourne normalement, on n'entend simplement rien (ou le son va dans le fichier de QEMU s'il émule une carte USB).

## Ouvrir le kit

```c
#include <audiokit_abi.h>

if (!audiokit_kit_open ()) return 1;   // SD0:/lib/audiokit.so absent
```

Rien à ajouter à l'édition de liens : `pibuild` prend toutes les bibliothèques de `/gcc/lib`, dont les relais de `libaudiokit.a`. Le kit se mappe à la première ouverture ; l'appeler plusieurs fois ne coûte rien.

Les erreurs sont des valeurs négatives : `AKE_NOMEM` (-12), `AKE_INVAL` (-22), `AKE_IO` (-5), `AKE_CORRUPT` (-74), `AKE_FORMAT` (-95), ou une erreur d'AppKit (fichier absent...).

## Flux de lecture

Un flux reçoit du PCM entrelacé au format choisi à l'ouverture ; AudioKit le convertit en s16 stéréo 44,1 kHz (mono vers stéréo, 8, 24, 32 bits, autre fréquence par son rééchantillonneur). Le volume du flux se combine à celui du programme (réglé dans le Mixer) et à celui de la sortie.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_open` | fréquence, canaux, bits (8, 16, 24, 32), sortie (0 : celle du programme) | flux, < 0 si erreur |
| `ak_write` | flux, tampon, octets | octets pris (n'attend pas) |
| `ak_write_all` | flux, tampon, octets | octets ; attend la place (voir les pièges) |
| `ak_space` | flux | octets acceptés tout de suite, au format du flux |
| `ak_pending` | flux | octets en attente (s16 stéréo 44,1 kHz) |
| `ak_position` | flux | trames jouées |
| `ak_volume` | flux, volume 0..256 | 0 |
| `ak_status` | flux, tampon de 3 `long` | 0 : en attente, libre, côté matériel |
| `ak_drain` | flux | attend la fin de la lecture (10 s au plus) |
| `ak_latency` | flux, avance maximale (octets s16 stéréo, 4096..65536) | 0 |
| `ak_close` | flux | 0 |

L'avance par défaut est d'environ deux blocs de 1024 trames : `ak_write_all` ne laisse pas le programme prendre des secondes d'avance. Un jeu qui veut moins de latence baisse `ak_latency` ; un lecteur qui veut plus de marge la monte.

## Capture

Une capture rend du s16 stéréo 44,1 kHz. Les sources (constantes `REC_SRC_*` de `src/inc/sound.inc`, à recopier dans votre programme) :

| Source | Valeur |
|---|---|
| micro (l'entrée USB s'il y en a une, sinon la première) | 0 |
| boucle : ce que joue la sortie principale | 1 |
| boucle d'une sortie n | 0x100 + n |
| entrée n | 0x200 + n |

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_rec_open` | source | capture, < 0 si erreur (pas de micro : erreur) |
| `ak_rec_read` | capture, tampon, octets | octets lus (n'attend pas) |
| `ak_rec_status` | capture, tampon de 3 `long` | 0 : disponibles, source, pertes |
| `ak_rec_close` | capture | 0 |

## Mixer et sorties

Ce que font le Mixer et le volet du son de la barre de menus ; un programme ordinaire n'en a pas besoin.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_backend` | – | 1 : service `sound` présent, 0 : aucun |
| `ak_state` | – | adresse du tampon d'état de `sound` (lecture seule, 4096 octets), 0 sans service |
| `ak_out_volume` | sortie, volume 0..100, muet | 0 |
| `ak_client_volume` | pid (0 : soi-même), volume 0..100, muet | 0 |
| `ak_route` | pid (0 : soi-même), sortie (0 : la principale) | 0 : tous les flux du programme y vont |
| `ak_main` | sortie préférée (0 : automatique) | 0 ; la préférence est enregistrée |

Le tampon d'état (champs `SS_*`, `SO_*`, `SC_*` de `src/inc/sound.inc`) commence par un numéro de génération, qui change à chaque modification : il suffit de le relire de temps en temps. L'exemple [Enregistrer](#exemple--enregistrer-ce-qui-sort) en lit les sorties.

## Outils

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_resample` | entrée s16 stéréo, trames, sortie, état de 3 `long` | trames produites |
| `ak_mix_s16` | destination, source, trames, gain 0..65536 | destination += source × gain (s16 stéréo, saturé) |
| `ak_gain_s16` | tampon, trames, gain 0..65536 | 0 |
| `ak_volume_curve` | volume 0..100 | gain 0..65536 « à l'oreille » (le carré du volume) |
| `ak_wav_header` | tampon de 44 octets, fréquence, canaux, bits, octets de données | 44 |

L'état de `ak_resample` : la trame précédente (gauche dans les 16 bits bas, droite dans les 16 suivants), la position (16.16) et le pas (16.16) : pas = (fréquence d'entrée << 16) / fréquence de sortie. Mettre les trois à zéro sauf le pas avant le premier bloc, puis garder l'état d'un bloc au suivant.

## Décoder un fichier

Le format est reconnu aux premiers octets, pas à l'extension.

| Format | Décodage | Encodage |
|---|---|---|
| WAV | PCM 8, 16, 24, 32 bits, flottant 32 bits | PCM 16 bits |
| FLAC | 4 à 24 bits, 1 à 8 canaux | – |
| MP3 | MPEG-1, 2, 2.5 couche III, ID3v1 et v2 | CBR 32 à 320 kbit/s |
| Ogg Vorbis | Vorbis I | – |
| Ogg Opus | CELT, SILK, hybride ; toujours 48 kHz | CELT, 16 à 256 kbit/s |
| MIDI (`.mid`) | formats 0, 1, 2, par le synthétiseur du kit, s16 stéréo 44,1 kHz | – |
| FM Song (`.fms`) | les morceaux de FM Tracker, s16 stéréo 44,1 kHz | – |

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_dec_open` | chemin | décodeur, < 0 si erreur |
| `ak_dec_open_mem` | tampon, octets | décodeur ; le tampon doit vivre jusqu'à `ak_dec_close` |
| `ak_dec_info` | décodeur, tampon de `AKI_SIZE` (64) octets | 0 |
| `ak_dec_read` | décodeur, tampon, trames | trames rendues, **s16 entrelacé aux canaux de la source** ; 0 : fin ; < 0 : erreur |
| `ak_dec_seek` | décodeur, position (ms) | 0, < 0 |
| `ak_dec_tell` | décodeur | position (trames) |
| `ak_dec_tag` | décodeur, `AKT_TITLE` / `AKT_ARTIST` / `AKT_ALBUM`, tampon, taille | octets (UTF-8, 0 final écrit) ; 0 : absente |
| `ak_dec_close` | décodeur | 0 |

Le tampon d'information se lit comme un tableau de `long` (indice = décalage / 8) :

| Décalage | Contenu |
|---|---|
| `AKI_FORMAT` | `AKF_WAV` 1, `AKF_FLAC` 2, `AKF_MP3` 3, `AKF_VORBIS` 4, `AKF_OPUS` 5, `AKF_MIDI` 6, `AKF_FMS` 7 |
| `AKI_RATE`, `AKI_CHANNELS` | fréquence, canaux |
| `AKI_FRAMES` | trames en tout (-1 : inconnu) |
| `AKI_DURATION` | durée (ms) |
| `AKI_BITRATE` | débit (bits/s) |
| `AKI_POSITION` | position (trames) |
| `AKI_BITS` | bits par échantillon de la source |

## Le lecteur

`ak_play` ouvre le fichier et un flux à son format, puis lance **un thread** du programme qui décode et écrit par blocs de 1024 trames. Le programme reste libre : il interroge l'état quand il veut.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_play` | chemin, sortie (0 : celle du programme) | lecteur, < 0 si erreur ; la lecture commence |
| `ak_play_pause` | lecteur, 1 pause / 0 reprise | 0 |
| `ak_play_seek` | lecteur, position (ms) | 0 |
| `ak_play_pos` | lecteur | position **entendue** (ms) |
| `ak_play_state` | lecteur | `AKP_PLAYING` 1, `AKP_PAUSED` 2, `AKP_ENDED` 3, `AKP_ERROR` 4 |
| `ak_play_volume` | lecteur, volume 0..256 | 0 |
| `ak_play_decoder` | lecteur | son décodeur, pour `ak_dec_info` et `ak_dec_tag` (pas `ak_dec_read`) |
| `ak_play_stop` | lecteur | 0 ; arrête le thread, ferme flux et fichier |

## Décoder des paquets

Pour un démultiplexeur (celui de [MediaKit](mediakit_fr.md), ou le vôtre) : AudioKit ne lit aucun fichier, il reçoit les paquets un par un.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_codec_open` | `AKF_*`, fréquence, canaux, données d'initialisation, octets | poignée, < 0 si erreur |
| `ak_codec_decode` | poignée, paquet, octets, sortie, trames au plus, `AKC_S16` ou `AKC_F32` | trames (0 : paquet sans son ; < 0 : paquet ignoré) |
| `ak_codec_reset` | poignée | 0 ; à appeler après une recherche |
| `ak_codec_info` | poignée, tampon `AKI_SIZE` | 0 ; fréquence et canaux effectifs |
| `ak_codec_close` | poignée | 0 |

Données d'initialisation : Vorbis, les trois en-têtes en laçage Xiph (le `CodecPrivate` de Matroska) ; Opus, l'`OpusHead` ; FLAC, `fLaC` et ses blocs, ou STREAMINFO ; MP3, aucune (0, 0). Prévoir une sortie de 1152 trames en MP3, 4096 en Vorbis, 5760 en Opus. Opus rend toujours du 48 kHz ; le pré-saut et le rognage de fin sont à l'appelant. Un exemple complet est dans la page [MediaKit](mediakit_fr.md).

## Encoder

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_enc_open` | chemin, `AKF_WAV` / `AKF_MP3` / `AKF_OPUS`, fréquence, canaux (1, 2), débit (bits/s ; 0 : 128 000 en MP3, 96 000 en Opus) | encodeur ; le fichier est créé |
| `ak_enc_write` | encodeur, PCM s16 entrelacé, trames | 0, < 0 |
| `ak_enc_close` | encodeur | 0, < 0 ; termine le flux et ferme le fichier |
| `ak_enc_size` | encodeur | octets écrits |
| `ak_enc_open_io` | écriture `(ctx, octets, n) -> n`, positionnement `(ctx, position)` ou 0, ctx, format, fréquence, canaux, débit | encodeur sans fichier (voir les pièges) |

Opus rééchantillonne toute fréquence à 48 kHz. Un encodeur ouvert doit toujours être fermé par `ak_enc_close` : c'est là que WAV écrit ses tailles et MP3 sa dernière trame.

## Synthèse FM

Un synthé FM a de 1 à 64 voix (0 : 16). Chaque voix joue une onde simple ou un instrument à deux opérateurs à la manière de l'OPL2. Les fonctions prennent un verrou : un thread peut rendre pendant qu'un autre joue.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_fm_new` | voix (0 : 16) | synthé, < 0 si erreur |
| `ak_fm_free` | synthé | 0 |
| `ak_fm_instrument` | synthé, voix, instrument (`AKFI_SIZE` = 24 octets) | 0, < 0 |
| `ak_fm_from_fms` | instrument (24 octets, sortie), 26 octets d'un instrument FM Song | 0 |
| `ak_fm_start` | synthé, voix, fréquence (milli-Hz), onde `AKW_*`, volume 0..255 | 0 |
| `ak_fm_stop` | synthé, voix (-1 : toutes) | 0 ; le relâchement s'entend |
| `ak_fm_silence` | synthé | 0 ; tout se tait d'un coup |
| `ak_fm_active` | synthé | 1 tant qu'une voix sonne |
| `ak_fm_pan` | synthé, voix, 0 gauche .. 128 centre .. 255 droite | 0 |
| `ak_fm_pitch` | synthé, voix, milli-Hz | 0 ; la note glisse |
| `ak_fm_gain` | synthé, voix, volume 0..255 | 0 |
| `ak_fm_render` | synthé, sortie s16 stéréo, trames | 0 ; les trames sont **toutes écrites** (44,1 kHz) |
| `ak_fm_live` | synthé, sortie (0 : celle du programme ; -1 : arrêt) | 0 ; un thread du kit rend les voix dans un flux |
| `ak_note_mhz` | touche MIDI | milli-Hz (69 : la4 = 440 000) |
| `ak_note_octave_mhz` | note 0..11 (do..si), octave (do4 = touche 60) | milli-Hz |

Ondes : `AKW_SQUARE` 0, `AKW_SINE` 1, `AKW_TRIANGLE` 2, `AKW_SAW` 3, `AKW_NOISE` 4, `AKW_FM` 5 (l'instrument de la voix). Un instrument est fait de deux opérateurs (`AKFI_OP0`, `AKFI_OP1`, 10 octets chacun : `AKFO_ATTACK`, `AKFO_DECAY`, `AKFO_SUSTAIN`, `AKFO_RELEASE`, `AKFO_LEVEL`, `AKFO_KSL`, `AKFO_MULT`, `AKFO_WAVE`, `AKFO_FLAGS` sur 2 octets avec `AKFF_*`), suivis de `AKFI_FEEDBACK` et `AKFI_CONN`.

## Synthétiseur MIDI et SoundFont

`ak_synth_new` donne 16 canaux General MIDI et 64 voix : notes, programmes, molette, contrôleurs usuels (volume 7, panoramique 10, expression 11, pédale 64, réverbération 91, chorus 93, RPN). Deux moteurs : une **SoundFont 2** (celle du système, `SD0:/res/soundfonts`, ou une autre), sinon une **banque FM** intégrée.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_sf_load` | chemin d'un `.sf2` | SoundFont, < 0 si erreur |
| `ak_sf_open_mem` | fichier `.sf2` en mémoire, octets | SoundFont ; le tampon reste à l'appelant |
| `ak_sf_free` | SoundFont | 0 ; après avoir libéré ses synthés |
| `ak_sf_default` | – | la SoundFont du système, chargée une fois par programme ; 0 : aucune |
| `ak_sf_name` | SoundFont | son nom (UTF-8) |
| `ak_sf_presets` | SoundFont | nombre de presets |
| `ak_sf_preset` | SoundFont, numéro, tampon `AKSP_SIZE` (32) | 0 : banque (`AKSP_BANK`), programme (`AKSP_PROGRAM`), nom (`AKSP_NAME`) |
| `ak_synth_new` | SoundFont (0 : celle du système), `AKS_FM` (banque FM) et/ou `AKS_NOFX` (sans effets) | synthé |
| `ak_synth_free` | synthé | 0 |
| `ak_synth_midi` | synthé, canal 0..15, commande (0x80, 0x90, 0xB0, 0xC0, 0xE0), donnée 1, donnée 2 | 0 |
| `ak_synth_render` | synthé, sortie s16 stéréo, trames | 0 ; trames **toutes écrites** (44,1 kHz) |
| `ak_synth_render_f32` | synthé, gauche, droite (`float`), trames | 0 |
| `ak_synth_reset` | synthé | 0 ; tout se tait, canaux remis à zéro |
| `ak_synth_voices` | synthé | voix qui sonnent |

Plusieurs synthés peuvent partager une SoundFont (lecture seule). Un synthé MIDI n'a pas de verrou : si un thread rend pendant qu'un autre envoie des notes, protégez les appels par un mutex.

## Exemple : jouer un fichier

Le lecteur fait tout ; le programme affiche le titre, la durée et la position.

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

	// le décodeur du lecteur : informations et étiquettes
	long dec = ak_play_decoder (p), info[AKI_SIZE / 8];
	char title[128];
	ak_dec_info (dec, (long) info);
	if (ak_dec_tag (dec, AKT_TITLE, (long) title, sizeof title) <= 0) snprintf (title, sizeof title, "%s", path);
	printf ("%s : %ld Hz, %ld canaux, %ld ms\n", title, info[AKI_RATE / 8], info[AKI_CHANNELS / 8], info[AKI_DURATION / 8]);

	ak_play_volume (p, 200);
	for (;;)
	{
		long st = ak_play_state (p);
		if (st == AKP_ENDED || st == AKP_ERROR) break;
		printf ("\r%ld ms", ak_play_pos (p));
		fflush (stdout);
		usleep (250000);
	}
	printf ("\nfin\n");
	ak_play_stop (p);	// toujours : arrête le thread et ferme le fichier
	return 0;
}
```

## Exemple : un son calculé

Un flux mono 16 bits à 22 050 Hz : AudioKit le convertit au format de `sound`. `ak_write_all` attend la place, puis `ak_drain` attend que tout soit joué.

```c
#include <math.h>
#include <stdio.h>
#include <audiokit_abi.h>

// ak_write_all est déclaré sans arguments dans l'en-tête : on l'appelle par ce pointeur
typedef long (*write_all_fn) (long stream, long buf, long bytes);

enum { RATE = 22050, BLOCK = 1024 };

int main (void)
{
	if (!audiokit_kit_open ()) return 1;
	write_all_fn write_all = (write_all_fn) ak_write_all;
	long s = ak_open (RATE, 1, 16, 0);
	if (s < 0) { printf ("pas de flux : %ld\n", s); return 1; }
	ak_volume (s, 128);

	static short buf[BLOCK];
	double phase = 0;
	long total = RATE * 2;					// deux secondes
	for (long done = 0; done < total; done += BLOCK)
	{
		// la 440 Hz qui monte d'une octave
		double f = 440.0 * pow (2.0, (double) done / total);
		for (int i = 0; i < BLOCK; i++)
		{
			buf[i] = (short) (sin (phase) * 9000);
			phase += 2 * M_PI * f / RATE;
		}
		write_all (s, (long) buf, sizeof buf);
	}
	ak_drain (s);
	printf ("%ld trames jouées\n", ak_position (s));
	ak_close (s);
	return 0;
}
```

## Exemple : une gamme en FM

`ak_fm_live` confie le rendu à un thread du kit : le programme n'a plus qu'à démarrer et arrêter des notes.

```c
#include <unistd.h>
#include <audiokit_abi.h>

int main (void)
{
	if (!audiokit_kit_open ()) return 1;
	long fm = ak_fm_new (4);
	if (fm < 0 || ak_fm_live (fm, 0) < 0) return 1;

	static const int scale[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };	// do majeur
	for (int i = 0; i < 8; i++)
	{
		int v = i & 1;						// deux voix qui alternent
		long mhz = ak_note_octave_mhz (scale[i] % 12, 4 + scale[i] / 12);
		ak_fm_pan (fm, v, v ? 192 : 64);
		ak_fm_start (fm, v, mhz, i < 4 ? AKW_TRIANGLE : AKW_SQUARE, 160);
		usleep (250000);
		ak_fm_stop (fm, v);
	}
	// un accord final, puis on laisse le relâchement finir
	ak_fm_start (fm, 0, ak_note_mhz (60), AKW_SINE, 140);
	ak_fm_start (fm, 1, ak_note_mhz (64), AKW_SINE, 140);
	ak_fm_start (fm, 2, ak_note_mhz (67), AKW_SINE, 140);
	usleep (800000);
	ak_fm_stop (fm, -1);
	while (ak_fm_active (fm)) usleep (20000);

	ak_fm_live (fm, -1);	// arrête le thread
	ak_fm_free (fm);
	return 0;
}
```

## Exemple : du MIDI vers un MP3

Le synthétiseur rend hors ligne, aussi vite que possible, et l'encodeur écrit le fichier : rien ne passe par `sound`.

```c
#include <stdio.h>
#include <audiokit_abi.h>

enum { RATE = 44100, BLOCK = 512 };

static long synth, enc;
static short pcm[BLOCK * 2];

// rend ms millisecondes et les donne à l'encodeur
static int render (int ms)
{
	for (long n = (long) RATE * ms / 1000; n > 0; n -= BLOCK)
	{
		ak_synth_render (synth, (long) pcm, BLOCK);
		if (ak_enc_write (enc, (long) pcm, BLOCK) < 0) return 0;
	}
	return 1;
}

int main (void)
{
	const char *out = "SD0:/home/arpeggio.mp3";
	if (!audiokit_kit_open ()) return 1;
	synth = ak_synth_new (0, 0);		// la SoundFont du système, sinon la banque FM
	if (synth < 0) return 1;
	enc = ak_enc_open ((long) out, AKF_MP3, RATE, 2, 160000);
	if (enc < 0) { printf ("%s : erreur %ld\n", out, enc); return 1; }

	ak_synth_midi (synth, 0, 0xC0, 0, 0);		// programme 0 : piano
	ak_synth_midi (synth, 0, 0xB0, 91, 60);		// un peu de réverbération
	static const int keys[] = { 60, 64, 67, 72, 67, 64 };
	int ok = 1;
	for (int bar = 0; bar < 4 && ok; bar++)
		for (int i = 0; i < 6 && ok; i++)
		{
			int k = keys[i] + (bar & 1 ? 5 : 0);
			ak_synth_midi (synth, 0, 0x90, k, 100);
			ok = render (180);
			ak_synth_midi (synth, 0, 0x80, k, 0);
		}
	if (ok) ok = render (1500);			// la fin de la réverbération

	long bytes = ak_enc_size (enc);		// avant ak_enc_close : la poignée n'existe plus après
	if (ak_enc_close (enc) < 0) ok = 0;
	ak_synth_free (synth);
	printf ("%s : %s, %ld octets\n", out, ok ? "écrit" : "erreur", bytes);
	return ok ? 0 : 1;
}
```

## Exemple : convertir en WAV

Tout fichier que le kit décode devient un WAV 16 bits, aux canaux et à la fréquence de la source. `ak_dec_seek` permet de ne garder qu'un extrait.

```c
#include <stdio.h>
#include <stdlib.h>
#include <audiokit_abi.h>

enum { BLOCK = 2048 };

int main (int argc, char **argv)
{
	const char *in = argc > 1 ? argv[1] : "SD0:/res/audiotest/kenney_select.ogg";
	const char *out = argc > 2 ? argv[2] : "SD0:/home/extract.wav";
	long from_ms = argc > 3 ? atol (argv[3]) : 0;
	if (!audiokit_kit_open ()) return 1;

	long d = ak_dec_open ((long) in);
	if (d < 0) { printf ("%s : erreur %ld\n", in, d); return 1; }
	long info[AKI_SIZE / 8];
	ak_dec_info (d, (long) info);
	long rate = info[AKI_RATE / 8], ch = info[AKI_CHANNELS / 8];
	if (ch > 2) { printf ("%ld canaux : l'encodeur n'en prend que 1 ou 2\n", ch); ak_dec_close (d); return 1; }
	if (from_ms > 0 && ak_dec_seek (d, from_ms) < 0) printf ("recherche refusée : depuis le début\n");

	long e = ak_enc_open ((long) out, AKF_WAV, rate, ch, 0);
	if (e < 0) { ak_dec_close (d); return 1; }
	short *pcm = malloc (BLOCK * 2 * sizeof (short));
	long n, frames = 0;
	while (pcm && (n = ak_dec_read (d, (long) pcm, BLOCK)) > 0)
	{
		if (ak_enc_write (e, (long) pcm, n) < 0) break;
		frames += n;
	}
	long bytes = ak_enc_size (e);
	ak_enc_close (e);
	ak_dec_close (d);
	free (pcm);
	printf ("%s : %ld trames, %ld Hz, %ld canaux, %ld octets\n", out, frames, rate, ch, bytes);
	return 0;
}
```

## Exemple : enregistrer ce qui sort

Le programme liste les sorties d'après l'état de `sound`, puis capture trois secondes de la boucle de la sortie principale (ce qu'on entend) dans un WAV. Avec `REC_SRC_MIC`, il prendrait le micro, s'il y en a un.

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <audiokit_abi.h>

// de src/inc/sound.inc
enum { REC_SRC_MIC = 0, REC_SRC_LOOP = 1 };
enum { SS_GEN = 0, SS_MAIN = 8, SS_NOUT = 16, SS_OUTS = 64, SO_SIZE = 64,
       SO_ID = 0, SO_VOLUME = 12, SO_MUTE = 16, SO_NAME = 32 };

static void list_outputs (void)
{
	const unsigned char *st = (const unsigned char *) ak_state ();
	if (!st) { printf ("pas de service sound\n"); return; }
	long n = *(const long *) (st + SS_NOUT), main_out = *(const long *) (st + SS_MAIN);
	for (long i = 0; i < n && i < 8; i++)
	{
		const unsigned char *o = st + SS_OUTS + i * SO_SIZE;
		int id = *(const int *) (o + SO_ID);
		printf ("%c sortie %d : %.32s, volume %d%s\n", id == main_out ? '*' : ' ', id, (const char *) (o + SO_NAME),
			*(const int *) (o + SO_VOLUME), *(const int *) (o + SO_MUTE) ? " (muette)" : "");
	}
}

int main (void)
{
	if (!audiokit_kit_open ()) return 1;
	list_outputs ();

	long r = ak_rec_open (REC_SRC_LOOP);
	if (r < 0) { printf ("pas de capture : %ld\n", r); return 1; }
	long e = ak_enc_open ((long) "SD0:/home/loop.wav", AKF_WAV, 44100, 2, 0);
	if (e < 0) { ak_rec_close (r); return 1; }

	static short buf[4096 * 2];
	long frames = 0;
	for (int idle = 0; frames < 44100 * 3 && idle < 500; )	// 5 s sans rien : on abandonne
	{
		long n = ak_rec_read (r, (long) buf, sizeof buf);	// n'attend pas
		if (n <= 0) { idle++; usleep (10000); continue; }
		idle = 0;
		ak_enc_write (e, (long) buf, n / 4);			// 4 octets par trame
		frames += n / 4;
	}
	long st[3];
	ak_rec_status (r, (long) st);
	printf ("%ld trames, %ld perdues\n", frames, st[2]);
	ak_rec_close (r);
	ak_enc_close (e);
	return 0;
}
```

## Pièges

- **`ak_write_all` et `ak_enc_open_io`** : l'en-tête généré les déclare avec un mauvais nombre d'arguments (`ak_write_all (void)`, `ak_enc_open_io (long)`). Appelez-les par une variable pointeur de fonction du bon type, comme dans l'exemple du son calculé : `typedef long (*write_all_fn) (long, long, long);` puis `write_all_fn write_all = (write_all_fn) ak_write_all;` ; pour `ak_enc_open_io`, un pointeur à 7 `long`. (Appeler directement le résultat du transtypage, `((write_all_fn) ak_write_all) (...)`, compile mais donne un avertissement en C.)
- **Trames et octets** : `ak_write`, `ak_space`, `ak_rec_read` comptent en **octets** ; `ak_dec_read`, `ak_enc_write`, `ak_*_render` en **trames** (une trame = un échantillon par canal).
- **Canaux de la source** : `ak_dec_read` rend les canaux du fichier (1, 2, jusqu'à 8 en FLAC ou Opus) ; ouvrez le flux avec le même nombre de canaux et la même fréquence. Opus est toujours à 48 kHz.
- **`ak_write` n'attend pas** : il rend ce qu'il a pu prendre. Dans une boucle d'interface, écrivez ce que `ak_space` accepte et revenez plus tard ; dans un thread à soi, `ak_write_all`.
- **Le lecteur a son thread** : appelez toujours `ak_play_stop`, même après `AKP_ENDED`, sinon le flux et le fichier restent ouverts. N'appelez pas `ak_dec_read` sur `ak_play_decoder`.
- **Pas de micro** : `ak_rec_open (REC_SRC_MIC)` échoue s'il n'y en a pas, ce qui est le cas ordinaire sous QEMU et sur un Pi sans carte son USB.
- **Synthé MIDI et threads** : `ak_synth_*` n'a pas de verrou (`ak_fm_*` en a un).
- **Fin d'un encodeur** : sans `ak_enc_close`, un WAV a des tailles fausses et un MP3 perd sa dernière trame.

## Voir aussi

- [MediaKit](mediakit_fr.md) : la vidéo, et le son des vidéos par l'API par paquets.
- [EmuKit](emukit_fr.md) : les émulateurs envoient leur son par AudioKit.
- [UIKit](uikit_fr.md) : les fenêtres, pour un lecteur avec une interface.
- [Les kits](kit_fr.md) : comment un kit se charge et s'appelle depuis le C.
