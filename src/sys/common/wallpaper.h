/* wallpaper.h : le fond d'écran du bureau, repris d'Onyx (user/Kits/systemkit/wallpaper.h) et mis au
 * style Givre calme (docs/DESIGN-ELEGANT.md 5.1). Commun à /sys/voronoy (il peint le fond) et au volet
 * Thème de Réglages (son aperçu, le fichier qu'il écrit). C portable, sans libc ni UIKit : compilé
 * dans voronoy (sys.conf, « sources = ../common/wallpaper.c ») et inclus par Réglages (C++).
 *
 * Réglage personnel wallpaper.ini (lu dans HOME:/etc, puis HOME:/.config/korin, puis /etc ; écrit
 * dans HOME:/etc par Réglages) :
 *
 *	mode    = voronoi	voronoi (cellules, le fond d'origine), gradient (dégradé), bubbles
 *				(dégradé et bulles), solid (uni), pattern (un motif gris de /res/wallpapers
 *				coloré par le dégradé), image (une image)
 *	tint    = accent	la teinte : vide (aucune : la palette calme du thème), « accent »
 *				(l'accent du thème, qui le suit) ou un nom d'accent de UIKit (bleu,
 *				orange, vert, jaune, violet, fuchsia, sarcelle, graphite)
 *	pattern = waves.png	pattern : le motif (un nom de /res/wallpapers, ou un chemin)
 *	image   = /media/sd1/x.jpg	image : l'image choisie (montrée par Réglages) ; voronoy peint sa
 *				copie à la taille de l'écran, wallpaper.png (HOME:/etc), que Réglages
 *				écrit en la choisissant (JPEG, GIF, WebP, PCX : UIKit ne lit que BMP et PNG)
 *	style   = cover		image : cover (l'écran rempli, centrée) ou tile (en mosaïque)
 *
 * Les couleurs suivent le thème : la palette calme de Givre ou d'Onyx Noir (jeton TJ_DARK), mêlée à
 * la teinte ; une image teintée devient un duotone de la teinte (ses gris). Sans fichier : voronoi,
 * sans teinte (le fond d'avant). Réglages publie korin.wallpaper (busd) après l'avoir écrit : voronoy
 * relit le fichier et repeint. */
#ifndef KORIN_WALLPAPER_H
#define KORIN_WALLPAPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { WP_VORONOI, WP_GRADIENT, WP_BUBBLES, WP_SOLID, WP_PATTERN, WP_IMAGE, WP_NMODES };

#define WP_INI "wallpaper.ini"		/* le réglage (conf_path) */
#define WP_CACHE "wallpaper.png"	/* la copie de l'image (conf_path) */
#define WP_DIR "/res/wallpapers"	/* les motifs */
#define WP_PUBLIC "/home/public/wallpapers" /* les fonds du paquet wallpapers (Choisir... y ouvre) */
#define WP_TOPIC "korin.wallpaper"	/* publié par Réglages : le fond a changé */
#define WP_PATTERN_DEF "waves.png"

struct wp_conf {
	int mode;
	int tile;			/* image : 1 en mosaïque, 0 cover */
	char tint[16];			/* "" : aucune ; « accent » ; un nom d'accent */
	char pattern[96];
	char image[192];
};

extern const char *const wp_mode_key[WP_NMODES]; /* « voronoi »... (les valeurs de mode) */

void wp_defaults(struct wp_conf *c);
void wp_key(struct wp_conf *c, const char *key, const char *val); /* une ligne « clé = valeur » */
void wp_parse(struct wp_conf *c, char *text); /* tout un wallpaper.ini (modifié), après wp_defaults */
long wp_format(const struct wp_conf *c, char *out, long size); /* le fichier -> octets */
/* le chemin du motif (WP_DIR/<nom>, ou le chemin tel quel s'il en a un) -> out */
void wp_pattern_path(const struct wp_conf *c, char *out, long size);

/* ce que voronoy (ou l'aperçu) peint : w x h pixels 0x00RRGGBB, stride pixels par ligne */
struct wp_target {
	uint32_t *px;
	long w, h, stride;
	int dark;			/* thème sombre (TJ_DARK) */
	int tinted;			/* 1 : tint vaut */
	uint32_t tint;			/* la teinte (0x00RRGGBB) */
	uint64_t seed;			/* le hasard (cellules, bulles) */
	const uint32_t *img;		/* motif ou image 0xAARRGGBB (0 : aucun) */
	long iw, ih;
	long scale_w;			/* largeur de l'écran que w représente (aperçu : l'écran ; sinon w) */
};

/* peint le fond c dans t (sans image ou motif lisible : le dégradé, ou l'uni de la teinte) */
void wp_paint(const struct wp_conf *c, const struct wp_target *t);

uint32_t wp_tone(uint32_t c, long n); /* comme ui_tone : < 128 plus sombre, > 128 plus clair */
uint32_t wp_mix(uint32_t a, uint32_t b, long t); /* comme ui_mix : t 0..256 de a vers b */
uint32_t wp_base(int dark, int tinted, uint32_t tint); /* la couleur calme du fond (l'uni) */

#ifdef __cplusplus
}
#endif

#endif
