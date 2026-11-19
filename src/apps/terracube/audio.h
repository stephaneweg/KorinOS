// audio.h -- TerraCube : la musique d'ambiance et les effets sonores, faits ici (aucun fichier son).
//
// Un thread calcule le son, échantillon par échantillon, et le donne à AudioKit (un flux PCM, s16 stéréo 44,1 kHz) :
//   - la musique, générative : une tonalité tirée de la graine du monde, une gamme et un tempo qui suivent la
//     situation (le jour, la nuit, la pluie, l'orage, la grotte, sous l'eau, le désert, la neige), des accords qui
//     s'enchaînent (nappes), une mélodie au hasard guidé (cloches, cordes pincées), une basse, de l'écho ; des
//     passages pleins, clairsemés et des silences, comme une musique d'ambiance ;
//   - l'ambiance : la pluie, le vent en altitude, les oiseaux le jour, les grillons la nuit, les gouttes des grottes ;
//   - les effets : casser, frapper, poser un bloc (selon sa matière), les pas, la chute, l'eau, la fabrication,
//     ramasser, un outil cassé, le tonnerre.
// Le jeu donne la situation (setMood) à chaque image et demande les effets (play) ; tout est mélangé dans le thread.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <stdint.h>

namespace tc {
namespace audio {

// la matière d'un bloc, pour le son
enum Material { M_STONE, M_WOOD, M_EARTH, M_SAND, M_GLASS, M_LEAVES, M_SOFT, M_SNOW, M_METAL };

enum Sfx { S_BREAK, S_HIT, S_PLACE, S_STEP, S_LAND, S_JUMP, S_SPLASH, S_CRAFT, S_PICKUP, S_TOOLBREAK, S_CLICK, S_THUNDER };

enum Biome { BI_TEMPERATE, BI_DESERT, BI_SNOW };

struct Mood {
	float daylight = 1;	// 0 la nuit, 1 le jour
	float rain = 0;		// 0 à 1 (la pluie, la neige)
	bool snowing = false;
	float storm = 0;	// 0 à 1
	float cave = 0;		// 0 à 1 : sous terre (loin du ciel)
	bool underwater = false;
	int biome = BI_TEMPERATE;
	float altitude = 0;	// 0 à 1 (le vent)
	float activity = 0;	// 0 à 1 : le joueur casse et pose (la musique s'anime)
	uint32_t seed = 1;	// la tonalité du monde
};

bool start ();			// -> faux sans son (pas d'AudioKit, pas de sortie)
void stop ();
bool running ();
void setMood (const Mood &m);
void setMusic (bool on, float volume);	// volume 0 à 1
void setSfx (bool on, float volume);
// un effet : la matière, le volume (0 à 1), la place à gauche (-1) ou à droite (1)
void play (Sfx s, int material = M_STONE, float volume = 1.0f, float pan = 0.0f);

int materialOf (int block);

} // namespace audio
} // namespace tc
