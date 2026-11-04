# EmuKit : le socle des émulateurs

**EmuKit** (`SD0:/lib/emukit.so`) fait tout ce qui, dans un émulateur, n'est pas la console : choisir la ROM (argument ou dialogue **Ouvrir**), ouvrir la fenêtre et ses menus, mettre l'image à l'échelle, faire tourner la machine dans un thread à priorité haute à la bonne cadence, envoyer le son à [AudioKit](audiokit_fr.md), lire la manette et le clavier, écrire la sauvegarde `<rom>.sav`, gérer la pause, la remise à zéro, les trois vues et la vitesse.

L'application ne donne qu'un **descripteur** : la taille de l'image, la cadence, la fréquence du son, et quelques fonctions (charger, faire une image, rendre le son...). Les émulateurs NES, GBC, SNES, GBA et N64 de Korin OS (en assembleur), DS, GameCube et 3DS (en C++) et Quake (en C) sont faits ainsi : un cœur et une petite fonction `main`.

## Ouvrir le kit

```c
#include <emukit.h>        // inclut emukit_abi.h

// dans main, une fois le descripteur rempli :
if (!emukit_kit_open ()) return 1;
return (int) emu_run ((long) &desc, emu_args (argc, argv));
```

`emukit.h` ajoute à l'en-tête généré la structure `struct emu_desc` et `emu_args`, qui recolle les arguments du programme en une chaîne pour `emu_run`. Rien à ajouter à l'édition de liens : `pibuild` prend toutes les bibliothèques de `/gcc/lib`. EmuKit ouvre lui-même UIKit et AudioKit.

## Le descripteur

`struct emu_desc`, champ pour champ avec `EMD_*` de `emukit.inc` :

| Champ | Sens |
|---|---|
| `name` | nom de la console : titre de la fenêtre et des messages |
| `ext` | motifs des ROM pour le dialogue **Ouvrir**, « `*.nes` » ou « `*.iso;*.gcm` » ; 0 : tous les fichiers |
| `w`, `h` | taille de l'image |
| `maxw`, `maxh` | taille maximale (0 : `w`, `h`) ; `emu_frame_size` fait varier l'image en dessous |
| `fps_milli` | cadence en milli-hertz (60 099 pour 60,099 images par seconde) |
| `rate` | fréquence du son (s16 stéréo) |
| `ctx` | premier argument de toutes les fonctions |
| `load` | `long load (ctx, rom, octets)` : 1 accepte la ROM, 0 la refuse ; la ROM reste en mémoire |
| `frame` | `void frame (ctx, pixels, pas, boutons, drapeaux)` : une image |
| `audio` | `long audio (ctx, lr, trames au plus)` : le son produit, en trames ; 0 : rien |
| `reset` | `void reset (ctx)` : remise à zéro (menu) |
| `save` | `long save (ctx, &pointeur)` : octets à écrire dans `<rom>.sav`, 0 : rien n'a changé |
| `restore` | `void restore (ctx, données, octets)` : le `<rom>.sav` lu au chargement |
| `menu` | menus en plus, au format de `ui_menu`, numéros à partir de 100 |
| `cmd` | `void cmd (ctx, numéro)` : un choix de ces menus |
| `mouse` | `void mouse (ctx, genre, x, y, boutons)` : la souris dans l'image |
| `bufs` | trois pointeurs vers les images (0 : le kit les alloue) |
| `skip` | 1 : `frame` accepte `EMF_SKIP` (la machine rattrape son retard sans dessiner) |
| `error` | texte du message quand `load` refuse (0 : un texte commun) |

Toute fonction peut rester à 0 si l'émulateur n'en a pas besoin, sauf `load` et `frame`.

### Qui appelle quoi, et où

| Fonction | Thread | Quand |
|---|---|---|
| `load`, `restore` | celui de `emu_run` | au chargement de la ROM, avant le premier `frame` |
| `frame`, `audio` | la machine | à chaque image ; `audio` est rappelée tant qu'elle remplit tout le tampon (4096 trames) |
| `save` | la machine | toutes les 300 images ; le kit recopie aussitôt les données, puis la fenêtre écrit le fichier |
| `reset`, `cmd` | la machine | entre deux images |
| `mouse`, fonction des touches de `emu_game` | la fenêtre | à chaque événement |

`frame` reçoit les pixels de l'image à remplir (`0x00RRGGBB`, `maxw` × `maxh` au plus), le **pas en octets**, les **boutons** de toutes les manettes réunis (`PAD_UP`, `PAD_DOWN`, `PAD_LEFT`, `PAD_RIGHT`, `PAD_A`, `PAD_B`, `PAD_X`, `PAD_Y`, `PAD_L`, `PAD_R`, `PAD_L2`, `PAD_R2`, `PAD_SELECT`, `PAD_START`, `PAD_L3`, `PAD_R3`, `PAD_HOME`) et `EMF_SKIP` quand l'image ne sera pas montrée. Ce qui touche à la fois la machine et la fenêtre (souris, touches) doit passer par des variables `volatile` ou atomiques, ou par `emu_command`.

La manette 0 est le clavier, d'après `SD0:/etc/gamepad.ini` (par défaut : les flèches, A, B, X, Y, L, R ; Entrée et Pause pour Start, Retour arrière et Impr. écran pour Select) ; les manettes 1 et 2 sont les manettes USB.

## Les fonctions

| Fonction | Arguments | Résultat |
|---|---|---|
| `emu_run` | descripteur, chaîne d'options et de ROM (0 : le dialogue **Ouvrir**) | code de sortie, à la fermeture ; `EME_CANCEL` (1) : aucune ROM choisie |
| `emu_frame_size` | largeur, hauteur | depuis `frame` : la taille de l'image en cours (au plus `maxw` × `maxh`) |
| `emu_command` | numéro | depuis la fenêtre : `cmd` (ou la remise à zéro) sera appelée sur le thread de la machine |
| `emu_rom_path` | – | le chemin de la ROM |
| `emu_thread` | fonction (argument), argument, pile (octets), nom | numéro du thread ; il partage le TLS du thread principal (Mesa y trouve son contexte) |
| `emu_pad_axes` | manette (0 à 3), tampon de 4 `short` | les axes lx, ly, rx, ry (-1000..1000) |
| `emu_frame_same` | – | depuis `frame` : rien de neuf, la dernière image publiée reste à l'écran |
| `emu_game` | fonction des touches, drapeaux `EMG_*` | avant `emu_run` : un jeu qui veut tout le clavier |
| `emu_pointer` | 1 / 0 | souris relative : `mouse` reçoit le genre 3 et des déplacements (si la fenêtre a le clavier) |
| `emu_rom_by_path` | – | avant `emu_run` : la ROM n'est pas lue, `load` reçoit son chemin à la place des octets (grosses images de disque) |
| `emu_gl_hook` | fonction, ctx | avant `emu_run` : `f (ctx, fenêtre, x, y, l, h)` rend le nombre d'images montrées ; l'application rend elle-même (par Mesa) |

### Un jeu plutôt qu'une console

`emu_game (touches, EMG_NOROM)` fait d'EmuKit le socle d'un jeu : pas de ROM (`load` reçoit 0, 0), pas de dialogue **Ouvrir** ni de remise à zéro, et le clavier n'est plus la manette 0 : chaque touche va à la fonction donnée, sur le thread de la fenêtre :

```c
void keys (void *ctx, long code, long down, long ch, long mods);
// code : code HID de la touche ; down : 1 appui, 0 relâché ; ch : le caractère ; mods : les modificateurs
// code EMG_KEY_WHEEL : la molette (down porte la valeur) ; EMG_KEY_NONE : la fenêtre a perdu le clavier, tout est relâché
```

F9 (plein écran) et F12 (vitesse) restent au kit. Un jeu vit en temps réel : sa cadence ne se cale plus sur le son.

### Ce que l'utilisateur trouve

EmuKit ajoute ses menus **Game**, **View** et **Sound** : ouvrir, pause (P), remise à zéro, quitter ; zoom 1x, 2x, 3x, agrandir, plein écran (F9, Échap pour sortir), vitesse affichée dans le titre (F12) ; son coupé. Les menus du descripteur viennent après. Les options de la ligne de commande : `--full rom` (plein écran d'emblée), `--thumb sortie rom` (fait l'image d'une carte pour Game Library, sans fenêtre).

## Exemple : un jeu sans ROM

Une raquette et une balle, au clavier (flèches) ou à la manette (croix ou stick gauche), avec un bip à chaque rebond. Le jeu ne touche ni à la fenêtre ni au son : EmuKit s'en charge.

```c
#include <string.h>
#include <emukit.h>

enum { W = 256, H = 192, RATE = 44100, FPS = 60000, PER_FRAME = RATE * 1000 / FPS };
enum { HID_RIGHT = 0x4F, HID_LEFT = 0x50, HID_SPACE = 0x2C };

static volatile int keys;			// écrit par le thread de la fenêtre, lu par la machine
static int pad_x = W / 2 - 24, ball_x = W / 2, ball_y = 40, dx = 2, dy = 2, misses, beep;
static short pcm[PER_FRAME * 2];
static int npcm;

static long load (void *ctx, const unsigned char *rom, long size) { return 1; }	// EMG_NOROM : rom = 0

static void key (void *ctx, long code, long down, long ch, long mods)
{
	int bit = code == HID_LEFT ? 1 : code == HID_RIGHT ? 2 : code == HID_SPACE ? 4 : 0;
	if (code == EMG_KEY_NONE) keys = 0;
	else if (bit) keys = down ? keys | bit : keys & ~bit;
}

static void box (unsigned *px, long pitch, int x, int y, int w, int h, unsigned c)
{
	for (int j = y; j < y + h; j++)
	{
		unsigned *row = (unsigned *) ((char *) px + j * pitch);	// le pas est en octets
		for (int i = x; i < x + w; i++) row[i] = c;
	}
}

static void frame (void *ctx, unsigned *px, long pitch, long buttons, long flags)
{
	short ax[4];
	emu_pad_axes (1, (long) ax);				// le stick de la première manette USB
	int k = keys, speed = ax[0] / 200;
	if ((k & 1) || (buttons & PAD_LEFT)) speed = -4;
	if ((k & 2) || (buttons & PAD_RIGHT)) speed = 4;
	pad_x += speed;
	if (pad_x < 0) pad_x = 0;
	if (pad_x > W - 48) pad_x = W - 48;
	if ((k & 4) || (buttons & PAD_START)) misses = 0;

	ball_x += dx; ball_y += dy;
	if (ball_x <= 0 || ball_x >= W - 6) { dx = -dx; beep = 3; }
	if (ball_y <= 0) { dy = -dy; beep = 3; }
	if (dy > 0 && ball_y >= H - 18 && ball_y <= H - 12 && ball_x + 6 > pad_x && ball_x < pad_x + 48) { dy = -dy; beep = 6; }
	if (ball_y >= H) { misses++; ball_x = W / 2; ball_y = 40; dy = 2; }

	// le son de cette image : un signal carré tant que le bip dure
	for (int i = 0; i < PER_FRAME; i++)
	{
		short s = beep > 0 ? ((i / 25) & 1 ? 2500 : -2500) : 0;
		pcm[2 * i] = pcm[2 * i + 1] = s;
	}
	npcm = PER_FRAME;
	if (beep > 0) beep--;

	box (px, pitch, 0, 0, W, H, 0x101828);
	for (int i = 0; i < misses && i < 20; i++) box (px, pitch, 4 + i * 8, 4, 5, 5, 0xE04040);
	box (px, pitch, pad_x, H - 12, 48, 6, 0xF0F0F0);
	box (px, pitch, ball_x, ball_y < H - 6 ? ball_y : H - 6, 6, 6, 0xF0C040);
}

static long audio (void *ctx, short *lr, long max)
{
	long n = npcm < max ? npcm : max;
	memcpy (lr, pcm, n * 4);
	npcm = 0;
	return n;
}

static struct emu_desc d;

int main (int argc, char **argv)
{
	if (!emukit_kit_open ()) return 1;
	d.name = "Paddle";
	d.w = W; d.h = H;
	d.fps_milli = FPS;
	d.rate = RATE;
	d.load = load;
	d.frame = frame;
	d.audio = audio;
	emu_game ((long) key, EMG_NOROM);
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
```

## Exemple : une « console » qui lit des fichiers

Ce programme traite n'importe quel fichier comme une ROM et montre ses octets en couleurs, une rangée de pixels par ligne d'octets. Il utilise le reste du descripteur : la croix fait défiler, A saute d'une page ; un menu **Viewer** change la largeur (et donc la taille de l'image) et inverse les couleurs ; un clic marque un octet ; la position est gardée dans `<fichier>.sav`, à côté du fichier ; **Reset** revient au début.

```c
#include <stdio.h>
#include <string.h>
#include <emukit.h>

enum { MAXW = 256, H = 192 };
enum { ID_W256 = 100, ID_W128, ID_INVERT, ID_CLICK };

struct viewer
{
	const unsigned char *rom;
	long size, offset, saved;		// premier octet montré ; celui du .sav
	int width, invert, prev;
	long cursor;				// octet marqué (-1 : aucun)
	volatile int click_x, click_y;		// le dernier clic (thread de la fenêtre)
};
static struct viewer v = { 0, 0, 0, -1, 256, 0, 0, -1, 0, 0 };

static long load (void *ctx, const unsigned char *rom, long size)
{
	struct viewer *s = ctx;
	if (size <= 0) return 0;			// refusée : le message de d.error
	s->rom = rom;
	s->size = size;
	printf ("%s : %ld octets\n", (const char *) emu_rom_path (), size);
	return 1;
}

static void frame (void *ctx, unsigned *px, long pitch, long buttons, long flags)
{
	struct viewer *s = ctx;
	int pressed = (int) buttons & ~s->prev;	// les boutons qui viennent d'être appuyés
	s->prev = (int) buttons;
	if (buttons & PAD_DOWN) s->offset += s->width * 2;
	if (buttons & PAD_UP) s->offset -= s->width * 2;
	if (pressed & PAD_A) s->offset += (long) s->width * H;
	if (pressed & PAD_B) s->offset -= (long) s->width * H;
	if (s->offset > s->size - s->width) s->offset = s->size - s->width;
	if (s->offset < 0) s->offset = 0;

	emu_frame_size (s->width, H);		// à chaque image : la taille de celle-ci
	for (int y = 0; y < H; y++)
	{
		unsigned *row = (unsigned *) ((char *) px + y * pitch);
		for (int x = 0; x < s->width; x++)
		{
			long i = s->offset + (long) y * s->width + x;
			unsigned b = i < s->size ? s->rom[i] : 0, c = b << 16 | (b * 3 & 255) << 8 | (255 - b);
			if (i >= s->size) c = 0;
			else if (s->invert) c ^= 0xFFFFFF;
			if (i == s->cursor) c = 0xFFFFFF;
			row[x] = c;
		}
	}
}

static void reset (void *ctx) { struct viewer *s = ctx; s->offset = 0; s->cursor = -1; }

static void cmd (void *ctx, long id)
{
	struct viewer *s = ctx;
	switch (id)
	{
	case ID_W256: s->width = 256; break;
	case ID_W128: s->width = 128; break;
	case ID_INVERT: s->invert = !s->invert; break;
	case ID_CLICK:					// (ici, sur le thread de la machine)
		s->cursor = s->offset + (long) s->click_y * s->width + s->click_x;
		if (s->cursor >= s->size) s->cursor = -1;
		else printf ("octet %ld = 0x%02x\n", s->cursor, s->rom[s->cursor]);
		break;
	}
}

// genre : 0 déplacement, 1 appui, 2 relâché ; x, y dans l'image
static void mouse (void *ctx, long kind, long x, long y, long buttons)
{
	struct viewer *s = ctx;
	if (kind != 1 || x < 0 || y < 0 || x >= s->width || y >= H) return;
	s->click_x = (int) x;
	s->click_y = (int) y;
	emu_command (ID_CLICK);			// la machine fera le reste, entre deux images
}

static long save (void *ctx, const void **data)
{
	struct viewer *s = ctx;
	if (s->offset == s->saved) return 0;	// rien de changé : pas d'écriture
	s->saved = s->offset;
	*data = &s->saved;
	return sizeof s->saved;
}

static void restore (void *ctx, const void *data, long size)
{
	struct viewer *s = ctx;
	if (size == sizeof s->offset) memcpy (&s->offset, data, sizeof s->offset);
	s->saved = s->offset;
}

static struct emu_desc d;

int main (int argc, char **argv)
{
	if (!emukit_kit_open ()) return 1;
	d.name = "Byte Viewer";
	d.ext = 0;					// le dialogue Ouvrir montre tous les fichiers
	d.w = MAXW; d.h = H;
	d.maxw = MAXW; d.maxh = H;
	d.fps_milli = 30000;
	d.rate = 44100;					// (pas de son : d.audio reste à 0)
	d.ctx = &v;
	d.load = load;
	d.frame = frame;
	d.reset = reset;
	d.save = save;
	d.restore = restore;
	d.menu = "MViewer\nI100\tWidth 256\nI101\tWidth 128\n-\nI102\tInvert Colors\n";
	d.cmd = cmd;
	d.mouse = mouse;
	d.error = "This file is empty.";
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
```

Les libellés des menus et le message d'erreur sont en anglais, comme ceux d'EmuKit : le dictionnaire de l'application (`lang/fr.txt`, « `Invert Colors = Inverser les couleurs` ») les traduit.

## Pièges

- **`emu_frame_size` à chaque image** : la taille est notée pour le tampon que `frame` remplit ; il y en a trois. L'appeler une seule fois laisse les deux autres images à l'ancienne taille.
- **Le pas est en octets**, l'image a au plus `maxw` × `maxh` pixels : ne pas écrire au-delà de la largeur annoncée.
- **Deux threads** : `frame` tourne sur la machine, `mouse` et la fonction des touches sur la fenêtre. Partagez par des variables `volatile` ou atomiques, ou passez par `emu_command`. Ne touchez pas à UIKit depuis la machine.
- **`audio` rend des trames** (s16 stéréo, 4 octets chacune) : rendez exactement ce que la machine a produit. Un émulateur de console se cale sur ce son : en rendre trop ralentit le jeu, trop peu l'accélère.
- **`save`** : rendez 0 tant que rien n'a changé, sinon la carte SD est récrite toutes les 300 images. Le pointeur doit rester valable jusqu'au retour (le kit recopie aussitôt).
- **`EMG_NOROM`** : le clavier n'est plus la manette 0 ; lisez-le par la fonction des touches. Les manettes USB restent dans `boutons`.
- **F9, F12** sont toujours au kit ; F10 et F11 sont à Elegant.
- **`emu_run` ne rend la main qu'à la fermeture** : préparez tout (descripteur, `emu_game`, `emu_rom_by_path`, `emu_gl_hook`) avant.
- **Sous QEMU**, pas de manette USB : le clavier seul. Le son part dans la sortie virtuelle.

## Voir aussi

- [AudioKit](audiokit_fr.md) : le son, que le kit envoie pour vous.
- [GpuKit](gpukit_fr.md) : le rendu 3D par le GPU (N64, Cosmo Zoom!).
- [UIKit](uikit_fr.md) : les fenêtres, si le jeu en veut une à lui.
- L'aide des émulateurs, par exemple **Game Library**.
