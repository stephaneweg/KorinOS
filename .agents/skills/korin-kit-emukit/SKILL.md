---
name: korin-kit-emukit
description: EmuKit, le socle des émulateurs et des petits jeux de Korin OS — choix de la ROM, fenêtre et menus, mise à l'échelle, thread de la machine à la bonne cadence, son par AudioKit, manettes et clavier, sauvegarde .sav, trois vues (fonctions emu_*, descripteur struct emu_desc / EMD_*, boutons PAD_*). À charger pour écrire ou modifier un émulateur, ou un jeu en boucle d'images qui veut tout cela sans toucher à UIKit.
---

# EmuKit : le socle des émulateurs

`/lib/emukit.so`. L'application ne donne qu'un **descripteur** (taille de l'image, cadence, fréquence du son,
fonctions `load`, `frame`, `audio`…) puis appelle `emu_run`, qui ne rend la main qu'à la fermeture. EmuKit **importe
lui-même AppKit, UIKit et AudioKit**. Les émulateurs NES, GBC, SNES, GBA, N64 (asm), DS, GameCube, 3DS (C++) sont
faits ainsi : un cœur et un petit `main`.

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Guide de l'API (2 exemples complets, pièges) | `/apps/help.app/api/emukit_fr.md` | `src/apps/help/data/api/emukit_fr.md` |
| Table du kit | — | `src/kits/emukit/emukit.abi`, constantes `emukit.inc`, boutons `src/inc/pad.inc` |
| En-têtes C | `/devtools/gcc/include/kits/emukit.h` (+ `emukit_abi.h`) | `src/kits/emukit/include/emukit.h` |
| Émulateurs, manettes, `gamepad.ini`, Game Library | — | `docs/EMULATEURS.md` (§ « Le socle commun ») |

Usages réels : `src/apps/nesemu/nesemu.asm`, `gbemu`, `snesemu`, `gbaemu`, `n64emu` (asm) ;
`src/apps/gcemu/main.cpp` (`emu_rom_by_path`, `emu_gl_hook`, `emu_thread`), `src/apps/n3dsemu` (C++).

## Ouvrir le kit

C / C++ :

```c
#include <emukit.h>          /* inclut emukit_abi.h ; ajoute struct emu_desc et emu_args */
if (!emukit_kit_open ()) return 1;
return (int) emu_run ((long) &desc, emu_args (argc, argv));
```

`pibuild` lie déjà tout `/devtools/gcc/lib/lib*.a` ; à la main : `-lemukit`. Assembleur : `include '../../kits/emukit/emukit.inc'`
et `'../../inc/pad.inc'`, `kit_use '../../kits/emukit/emukit.abi'` (après celui d'AppKit), `bl kits_open`, `kit_data` ;
le descripteur se remplit aux décalages `EMD_*` (`EMD_SIZE` 168). Mêmes noms dans tous les langages (skills `korin-lang-*`).

## L'essentiel

Descripteur `struct emu_desc` (champ = `EMD_*`) : `name`, `ext` (« `*.nes` », « `*.iso;*.gcm` », 0 : tout), `w`, `h`,
`maxw`, `maxh` (0 : `w`, `h`), `fps_milli` (60099 = 60,099 im/s), `rate` (son s16 stéréo), `ctx`, puis les fonctions :
- `long load(ctx, rom, octets)` → 1 accepte ; **obligatoire** ; `void frame(ctx, px, pas_octets, boutons, drapeaux)` :
  une image `0x00RRGGBB` ; **obligatoire** ; `drapeaux & EMF_SKIP` : image non montrée (si `skip` = 1)
- `long audio(ctx, short *lr, max)` → trames produites ; `void reset(ctx)`
- `long save(ctx, const void **p)` → octets pour `<rom>.sav`, 0 : rien changé ; `void restore(ctx, données, octets)`
- `menu` (format `ui_menu`, ids ≥ 100) + `void cmd(ctx, id)` ; `void mouse(ctx, genre, x, y, boutons)` (0 déplacement,
  1 appui, 2 relâché, 3 relatif) ; `bufs` (3 images, 0 : le kit alloue) ; `error` (texte si `load` refuse)

Fonctions :
- `emu_run(desc, options_et_rom | 0)` → code de sortie à la fermeture ; 0 : dialogue Ouvrir ; `EME_CANCEL` (1) : aucune ROM
- `emu_frame_size(l, h)` : depuis `frame`, la taille de l'image en cours (≤ `maxw` × `maxh`)
- `emu_frame_same()` : depuis `frame`, rien de neuf, la dernière image reste
- `emu_command(id)` : depuis la fenêtre, fait appeler `cmd` sur le thread de la machine
- `emu_rom_path()` → chemin de la ROM ; `emu_pad_axes(manette 0..3, short[4])` : lx, ly, rx, ry en -1000..1000
- `emu_thread(fn, arg, pile_octets, nom)` → tid ; partage le TLS du thread principal (contexte Mesa)
- Avant `emu_run` : `emu_game(fn_touches, EMG_NOROM)` (un jeu : pas de ROM, tout le clavier à
  `fn(ctx, code_hid, appui, car, mods)`, `EMG_KEY_WHEEL`, `EMG_KEY_NONE`) ; `emu_rom_by_path()` (`load` reçoit le
  chemin, pour les grosses images de disque) ; `emu_gl_hook(fn, ctx)` (`fn(ctx, fenêtre, x, y, l, h)` rend le nombre
  d'images montrées, l'app rend elle-même par Mesa) ; `emu_pointer(1)` (souris relative)

Boutons (`boutons` = toutes les manettes réunies) : `PAD_UP DOWN LEFT RIGHT A B X Y L R L2 R2 SELECT START L3 R3 HOME`.
Manette 0 = clavier d'après `/etc/gamepad.ini` ; 1 et 2 = manettes USB.

## Exemple

Une « console » minimale : un dégradé qui défile, la croix le déplace (réduit de l'exemple « Byte Viewer » du guide) :

```c
#include <emukit.h>

enum { W = 256, H = 192 };
static int pos;

static long load (void *ctx, const unsigned char *rom, long size) { return size > 0; }

static void frame (void *ctx, unsigned *px, long pitch, long buttons, long flags)
{
	if (buttons & PAD_LEFT) pos -= 2;
	if (buttons & PAD_RIGHT) pos += 2;
	emu_frame_size (W, H);				/* à chaque image (trois tampons) */
	for (int y = 0; y < H; y++)
	{
		unsigned *row = (unsigned *) ((char *) px + y * pitch);	/* pas en octets */
		for (int x = 0; x < W; x++) row[x] = ((x + pos) & 255) << 16 | y << 8 | 0x40;
	}
}

static struct emu_desc d;

int main (int argc, char **argv)
{
	if (!emukit_kit_open ()) return 1;
	d.name = "Demo"; d.ext = 0;			/* le dialogue Ouvrir montre tout */
	d.w = W; d.h = H; d.fps_milli = 60000; d.rate = 44100;
	d.load = load; d.frame = frame;			/* d.audio à 0 : pas de son */
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
```

Un jeu sans ROM avec son (raquette et balle) : premier exemple de `emukit_fr.md`.

## Pièges

- **Deux threads** : `frame`, `audio`, `save`, `reset`, `cmd` tournent sur la machine ; `mouse` et la fonction des
  touches sur la fenêtre. Partager par `volatile`/atomiques ou `emu_command`. **Jamais d'UIKit depuis la machine.**
- `emu_frame_size` à **chaque** image, sinon deux des trois tampons gardent l'ancienne taille. Ne pas écrire au-delà
  de la largeur annoncée ; le pas est en octets.
- `audio` rend des **trames** (4 octets) : exactement ce que la machine a produit. Une console se cale sur ce son :
  trop ralentit, trop peu accélère. Un jeu `EMG_NOROM` vit en temps réel.
- `save` : rendre 0 tant que rien ne change (appelée toutes les 300 images), sinon la carte est récrite sans cesse.
- `EMG_NOROM` : le clavier n'est plus la manette 0 ; les manettes USB restent dans `boutons`.
- F9 (plein écran) et F12 (vitesse) sont au kit, F10 et F11 à Elegant. Options : `--full rom`, `--thumb sortie rom`.
- `emu_run` ne rend la main qu'à la fermeture : `emu_game`, `emu_rom_by_path`, `emu_gl_hook`, `emu_thread` avant.
- Sous QEMU : pas de manette USB (clavier seul), pas de GPU (un rendu par Mesa doit prévoir son repli).
- Libellés de menus en anglais, traduits par le `lang/fr.txt` de l'app. Aucune ROM commerciale dans le dépôt.
