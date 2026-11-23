//
// korin_port.h -- ce que les deux couches du port Linux partagent (UIKit sur GTK 4, AudioKit sur FluidSynth) :
// la traduction des chemins de Korin (HOME:/, /apps/<app>.app/, /home/public/..., /res/soundfonts) vers ceux de
// Linux, la SoundFont par defaut, le navigateur du systeme (a la place de Jet), le coffre a secrets.
//
// Les sources des apps (src/apps/<app>) ne changent pas : leurs appels de fichiers (open, fopen, opendir, mkdir,
// stat, posix_spawn...) passent par korin_paths.cpp (edition de liens avec -Wl,--wrap=...).
//
#ifndef KORIN_PORT_H
#define KORIN_PORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// au debut de main : le nom de l'app (« arabesque », « koton ») choisit sa table de chemins
void kport_setup (const char *app, const char *argv0);
// un chemin de Korin -> celui de Linux (tel quel s'il n'est pas a traduire) ; buf : au moins PATH_MAX octets
const char *kport_path (const char *p, char *buf, size_t cap);
const char *kport_app (void);			// « arabesque », « koton »
const char *kport_bindir (void);		// le dossier de l'executable
const char *kport_rootdir (void);		// la racine du depot (src/apps/...), "" si introuvable
const char *kport_homedir (void);		// ce que devient HOME:/ (~/Korin par defaut, $KORIN_HOME)

// la SoundFont General MIDI : $KORIN_SOUNDFONT, sinon le reglage enregistre (~/.config/korin-ports/settings.ini),
// sinon /usr/share/soundfonts/FluidR3_GM.sf2 (fluid-soundfont-gm), sinon la premiere .sf2 trouvee
const char *kport_soundfont (void);
int kport_soundfont_save (const char *path);	// -> 1 : enregistree pour les prochains lancements

// une adresse (https://...) ou un fichier, un dossier ouverts par le systeme : xdg-open, sinon « gio open »
// ($KORIN_BROWSER : une autre commande) ; sans attendre -> 1 : lance
int kport_open_external (const char *what);
// le navigateur du systeme est-il la ? (xdg-open ou gio ; pour les apps qui demandent si Jet est installe)
int kport_browser_available (void);

// UIKit (uikit_gtk.cpp) : un mot de l'app dit autrement sous Linux (ui_tr : « Ouvrir dans Jet » -> « ... navigateur »)
void kport_tr_add (const char *from, const char *to);

// le coffre a secrets de Korin sur libsecret (korin_vault.cpp), pour cette app -> 1 : installe (0 : construit sans
// libsecret ; l'app fait alors comme sans coffre)
int kport_vault_install (void);
// (korin_sys.cpp : les appels systeme de Korin, l'horloge et le coffre)
long kport_syscall (long n, long a, long b, long c, long d, long e, long f);

// AudioKit (audiokit_fluid.cpp) : la SoundFont du systeme changee pendant que l'app tourne -> 1
int kport_audio_reload_soundfont (const char *path);
// UIKit (uikit_gtk.cpp) : ajoute a la barre de menus un menu « Son » (choisir la SoundFont) ; pour les apps qui
// n'ont pas de choix de SoundFont a elles (Arabesque)
extern int kport_soundfont_menu;
// UIKit (uikit_gtk.cpp) : apres chaque tour de la boucle de GTK, remet courant le contexte OpenGL ES de l'app (pose par
// glkit_egl.cpp au premier armgl_make_current ; 0 : pas de GL)
extern void (*kport_gl_restore) (void);

#ifdef __cplusplus
}
#endif

#endif
