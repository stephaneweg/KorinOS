//
// korin_port.h -- ce que les deux couches du port Linux partagent (UIKit sur GTK 4, AudioKit sur FluidSynth) :
// la traduction des chemins de Korin (HOME:/, /apps/<app>.app/, /home/public/..., /res/soundfonts) vers ceux de
// Linux, et la SoundFont par defaut.
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

// AudioKit (audiokit_fluid.cpp) : la SoundFont du systeme changee pendant que l'app tourne -> 1
int kport_audio_reload_soundfont (const char *path);
// UIKit (uikit_gtk.cpp) : ajoute a la barre de menus un menu « Son » (choisir la SoundFont) ; pour les apps qui
// n'ont pas de choix de SoundFont a elles (Arabesque)
extern int kport_soundfont_menu;

#ifdef __cplusplus
}
#endif

#endif
