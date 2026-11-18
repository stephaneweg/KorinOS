// sound.h -- les effets sonores d'Asteroids, synthétisés en direct (AudioKit)
#ifndef ASTEROIDS_SOUND_H
#define ASTEROIDS_SOUND_H

// les effets ponctuels
enum
{
	SFX_FIRE,		// tir du vaisseau
	SFX_BANG_L,		// gros astéroïde qui éclate
	SFX_BANG_M,		// astéroïde moyen
	SFX_BANG_S,		// petit astéroïde
	SFX_DIE,		// le vaisseau détruit
	SFX_BEAT1,		// battement de fond, note haute
	SFX_BEAT2,		// battement de fond, note basse
	SFX_UFO_FIRE,		// tir de l'ovni
	SFX_UFO_BANG,		// l'ovni détruit
	SFX_LIFE,		// vie gagnée
	SFX_HYPER,		// saut dans l'hyperespace
	SFX_START,		// début de partie
	SFX_COUNT
};

int snd_init (void);			// 1 : le son marche (sinon le jeu reste muet)
void snd_quit (void);
void snd_play (int sfx);		// déclenche un effet (n'attend pas)
void snd_thrust (float level);		// grondement du réacteur, 0 à 1
void snd_ufo (int kind);		// sirène de l'ovni : 0 aucune, 1 gros, 2 petit
void snd_enable (int on);		// coupe ou rend le son

#endif
