/* voronoy - le fond d'écran du bureau Elegant (/sys/voronoy, lancé par la session, comme le
 * voronoy d'Onyx). Le fond que dit wallpaper.ini (réglage personnel : src/sys/common/wallpaper.h,
 * écrit par le volet Thème de Réglages) : Voronoï « facettes douces » (docs/DESIGN-ELEGANT.md 5.1 :
 * environ une cellule pour 28 000 pixels, germes espacés, couleurs de la palette du thème, plus
 * clair en haut, liseré clair le long des arêtes ; le fond par défaut), dégradé, bulles, uni, un
 * motif de /res/wallpapers ou une image, dans une teinte (aucune, l'accent du thème ou un accent).
 * Fenêtre sans cadre, sous toutes les autres. Relance : nouvelles cellules.
 *
 * Repeint quand l'écran change (WEV_SCREEN), quand le thème change (WEV_THEME : Givre ou Onyx Noir,
 * l'accent) et quand Réglages publie korin.wallpaper (WEV_BUS : le réglage relu). */
#include "sys_consts.h"
#include "uikit_import.h"
#include "buskit_import.h"
#include "wallpaper.h"

static struct krt_ui ui;
static long scr[4];
static struct wp_conf conf;
static long wp_sub; /* abonnement à korin.wallpaper (0 : pas de bus) */
static char text[1024];
static char path[200];

static u32 token(long off)
{
	return *(const u32 *)((const u8 *)ui_tokens() + off);
}

/* conf <- wallpaper.ini (personnel, sinon /etc ; sans fichier : les cellules) */
static void load_conf(void)
{
	wp_defaults(&conf);
	long fd = sys_open(conf_path(WP_INI, 0), O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, text, sizeof text - 1);
	sys_close(fd);
	text[n > 0 ? n : 0] = 0;
	wp_parse(&conf, text);
}

/* la teinte : l'accent du thème, ou un accent nommé (pris un instant dans un thème d'aperçu, comme
 * l'aperçu du volet Thème : ses couleurs sont celles de UIKit) -> 1 s'il y en a une */
static int tint_colour(u32 *out)
{
	if (!conf.tint[0])
		return 0;
	if (str_eq(conf.tint, "accent")) {
		*out = token(TJ_ACCENT) & 0xFFFFFF;
		return 1;
	}
	char *p = str_cat(text, "theme = ");
	p = str_cat(p, token(TJ_DARK) ? "Onyx Noir" : "Givre");
	p = str_cat(p, "\naccent = ");
	str_cat(p, conf.tint);
	ui_theme_text((long)text);
	*out = token(TJ_ACCENT) & 0xFFFFFF;
	ui_theme_text(0); /* (le thème du fichier, à nouveau) */
	return 1;
}

static void paint(void)
{
	load_conf();
	struct wp_target t;
	t.px = ui.surf;
	t.w = ui.w;
	t.h = ui.h;
	t.stride = ui.w;
	t.scale_w = ui.w;
	t.dark = token(TJ_DARK) != 0;
	t.tinted = tint_colour(&t.tint);
	t.seed = (u64)sys_time_ms();
	t.img = 0;
	t.iw = t.ih = 0;
	long img = 0;
	if (conf.mode == WP_PATTERN) {
		wp_pattern_path(&conf, path, sizeof path);
		img = ui_image_load((long)path);
	} else if (conf.mode == WP_IMAGE) {
		img = ui_image_load((long)conf_path(WP_CACHE, 0));
	}
	if (img) {
		t.iw = *(long *)(img + IMG_W);
		t.ih = *(long *)(img + IMG_H);
		t.img = (const u32 *)(img + IMG_PIX);
	}
	wp_paint(&conf, &t);
	if (img)
		ui_image_free(img);
	ui_update_all();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ui_init((long)&ui);
	if (ui_screen((long)scr) < 0)
		return 0;
	if (ui_window_ex(0, 0, scr[0], scr[1], (long)lang_tr("Fond"),
			 WF_FRAMELESS | WF_BACK | WF_NOFOCUS | WF_NOLIST) < 0)
		return 0;
	long s = bus_subscribe((long)WP_TOPIC, 0);
	wp_sub = s > 0 ? s : 0;
	paint();
	for (;;) {
		long e = ui_event(600000);
		if (e == WEV_SCREEN) { /* nouvelle résolution : nouvelles cellules */
			if (ui_screen((long)scr) < 0)
				continue;
			ui_resize(scr[0], scr[1]);
			paint();
		} else if (e == WEV_THEME) { /* autre thème : ses couleurs (de nouvelles cellules) */
			paint();
		} else if (e == WEV_BUS) { /* korin.wallpaper : le réglage a changé */
			if (wp_sub && *(u32 *)(ui.ev + BEV_SUB) == (u32)wp_sub)
				paint();
		} else if (e == WEV_CLOSE)
			return 0;
	}
}
