// game.h -- TerraCube : l'état du jeu partagé par la boucle (main.cpp), les scripts (api.cpp) et l'agent (mcptools.cpp).
//
// Tout ce qui touche au monde passe par la boucle de l'interface : les threads du réseau lui confient leurs commandes
// (api.cpp, mcp.cpp), qui sont exécutées entre deux images.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <stdint.h>
#include <deque>
#include <string>
#include <vector>
#include "items.h"
#include "world.h"

namespace tc {

const char *const VERSION = "1.3.0";
const int API_PORT = 4730;	// le protocole des scripts (Python, BASIC)
const int MCP_PORT = 7312;	// le MCP (Forge a 7311)

struct Player {
	double x = 0.5, y = 80, z = 0.5;	// les pieds
	double vx = 0, vy = 0, vz = 0;
	float yaw = 0, pitch = 0;		// degrés : yaw 0 vers le nord (-z), 90 vers l'est (+x)
	bool fly = false, onGround = false, inWater = false, inLava = false;
	static constexpr double EYE = 1.62, HALF = 0.3, HEIGHT = 1.8;
};

struct ChatLine {
	std::string text;
	double t;		// ms (horloge monotone)
};

struct BlockEvent {
	int kind;		// 1 cassé, 2 posé, 3 clic avec la main vide (touche E)
	int x, y, z;
	int block;
	int face;		// 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z
};

enum Mode { MODE_CREATIVE, MODE_SURVIVAL };
enum Weather { W_CLEAR, W_RAIN, W_SNOW, W_STORM };
const char *modeName (int m);		// « creative », « survival »
const char *weatherName (int w);	// « clear », « rain », « snow », « storm »

struct Game {
	World world;
	Player pl;
	float hour = 9.0f;		// 0 à 24
	bool timeRuns = true;
	int distance = 8;		// tronçons
	std::deque<ChatLine> chat;
	std::vector<BlockEvent> events;	// pour events.blocks() : les 256 derniers
	long apiCommands = 0;		// commandes reçues des scripts
	long agentCalls = 0;		// appels d'outils de l'agent
	int hotbar[9];			// la barre du mode créatif (des blocs)
	int selected = 0;
	bool edited = false;		// des changements à sauver
	// mode survie
	int mode = MODE_CREATIVE;
	Inventory inv;			// les 9 premières cases sont la barre
	float fuel = 0;			// les points de chauffe qui restent au four
	// météo : weather est voulue, level suit (0 à 1) ; cycle : elle change d'elle-même
	int weather = W_CLEAR;
	float weatherLevel = 0;
	bool weatherCycle = true;
	double weatherNext = 0;		// (s de jeu avant le prochain changement)
	double lightning = 0;		// (ms : l'éclair en cours, sous l'orage)
	std::deque<std::string> console;	// les lignes de la console (commandes et réponses)
	// le son (audio.h)
	bool musicOn = true, sfxOn = true;
	float musicVol = 0.6f, sfxVol = 0.8f;
};

extern Game g;

// main.cpp
void gamePost (const std::string &text);		// un message dans la conversation à l'écran
double nowMs ();
// rend une image maintenant et la relit (0x00RRGGBB, ligne du haut d'abord) ; la caméra peut être prise ailleurs
bool gameCapture (std::vector<uint32_t> &px, int &w, int &h, const double *eye, const float *yawPitch);
bool gameSave (std::string &err);
void gameTeleport (double x, double y, double z);
void gameNewWorld (const std::string &name, uint32_t seed);
void gameFlyChanged ();

// api.cpp : le protocole des scripts (une commande par ligne, « world.setBlock(1,2,3,stone) »)
std::string apiExec (const std::string &line, bool &reply);
bool apiWantsReply (const std::string &line);
int stateOf (const std::string &t);		// l'état d'un bloc écrit : un nombre, ou « east+on »…
bool apiStart (int port, bool anyAddress, std::string &err);
void apiStop ();
bool apiRunning ();
int apiClients ();
void apiPump (double budgetMs);		// (boucle) exécute les commandes en attente

// les opérations communes au protocole et au MCP (boucle) -> "" si réussi, sinon l'erreur
std::string opFill (int x1, int y1, int z1, int x2, int y2, int z2, int block, long *count, bool hollow = false);
std::string blockList ();

// main.cpp, suite
void gameSetMode (int mode);
void gameAudioChanged ();	// (après un changement de g.musicOn, g.musicVol, g.sfxOn, g.sfxVol)
void gameSetWeather (int w, double seconds);	// seconds <= 0 : jusqu'au prochain changement du cycle
int gameHeldItem ();				// l'objet en main (créatif : le bloc de la case)
bool gameStations (bool &workbench, bool &furnace);	// un établi, un four à moins de 4 blocs du joueur

// console.cpp : la console de commandes (touche T ou /) ; aussi pour les scripts (game.command) et l'agent
// (run_command) -> les lignes de la réponse
std::vector<std::string> consoleExec (const std::string &line);

// mcptools.cpp
std::string mcpHandle (const std::string &in);

} // namespace tc
