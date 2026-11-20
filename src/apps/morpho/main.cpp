//
// morpho -- Morpho, l'animal de compagnie qui évolue : un petit être en 3D, à nourrir, faire jouer, laver, soigner,
// coucher et câliner, qui grandit en temps réel (même app fermée) de l'œuf à l'âge d'ancien. À chaque stade, sa forme
// (corps, pattes, cornes, oreilles, bouche, ailes…) et ses couleurs sont tirées au hasard, d'après le stade d'avant et
// les soins reçus. La créature est paramétrique (genome.h, creature.cpp) et dessinée par gk_draw (GpuKit : le GPU, ou le
// processeur) ; l'interface est en 2D, aux couleurs du thème (hud.cpp). README.md du dossier.
//
//   Souris : glisser sur l'animal pour le caresser ; glisser ailleurs (ou clic droit) pour tourner autour ; molette
//   pour s'approcher ; clic sur une crotte pour la ramasser ; pendant le jeu, clic dans la prairie pour lancer la balle.
//   Clavier : N nourrir (1 2 3 : le repas), J jouer, L laver, O soigner, E lumière, C câliner, I fiche, Échap ferme.
//   Ligne de commande (essais) : new (un nouvel œuf), speed=N, hour=H (heure fixe), stage=N (aller au stade N), seed=N.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener (docs/LICENSING.md).
//
#include "scene.h"
#include "actor.h"
#include "hud.h"
#include "tr.h"
#include <gpukit_abi.h>
#include <audiokit_abi.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#if defined(__x86_64__) && !defined(MORPHO_HOST) && defined(MORPHO_GL)
#include "gpukit_gl.h"			// (Korin PC : GpuKit par GlKit, le GPU Intel)
#endif

namespace ga { struct ui_vars ui; }
using ga::ui;
using namespace mo;

#define SAVEDIR "HOME:/morpho"
#define SAVE SAVEDIR "/pet.ini"

// ==== l'état ==========================================================================================================
static Pet g_pet;
static Actor g_act;
static Scene g_scene;
static Hud g_hud;
static ga::Snd g_snd;
static float g_camYaw = 0.35f, g_camPitch = 0.3f, g_camZoom = 1, g_camDist = 3;
static V3 g_camAt (0, 0.4f, 0);
static float g_hourFixed = -1;
static long g_clockOff = 0;
static bool g_quit = false;
static float g_saveIn = 20;
static int g_pendingCard = C_NONE; static float g_cardIn = 0; static char g_cardTitle[128], g_cardText[512];
static bool g_firstPlay = true;
static uint32_t *g_px; static int g_W, g_H;
// la souris
static int g_mx = -1, g_my = -1, g_downX, g_downY, g_downHud = H_NONE;
static bool g_down = false, g_dragCam = false, g_dragPet = false, g_rdown = false;
static float g_cursorFor = 0, g_strokeFor = 0, g_caressHold = 0;
static V3 g_cursor;
static float g_t = 0;

static float hourNow ()
{
	if (g_hourFixed >= 0) return g_hourFixed;
	long s = (long) time (0) + g_clockOff;
	s %= 86400; if (s < 0) s += 86400;
	return (float) s / 3600.0f;
}
static void loadClockOffset ()
{
	FILE *f = fopen ("/etc/clock.cfg", "r");
	if (!f) return;
	char b[32] = "";
	if (fgets (b, sizeof b, f)) g_clockOff = atol (b) % 86400;
	fclose (f);
}

// ==== les sons : la voix de l'animal, plus aiguë quand il est petit ======================================================
static void playSound (int id)
{
	float v = g_pet.g.voice * (g_pet.g.stage <= ST_BABY ? 1.45f : g_pet.g.stage == ST_CHILD ? 1.25f : g_pet.g.stage == ST_ELDER ? 0.88f : 1);
	auto hz = [&] (float f) { return (int) (f * v); };
	switch (id)
	{
	case SND_HAPPY: g_snd.later (0, hz (660), 70, ga::SOUND_TRIANGLE, 70); g_snd.later (80, hz (880), 70, ga::SOUND_TRIANGLE, 70); g_snd.later (160, hz (1100), 110, ga::SOUND_TRIANGLE, 70); break;
	case SND_SAD: g_snd.later (0, hz (520), 140, ga::SOUND_TRIANGLE, 70); g_snd.later (150, hz (390), 220, ga::SOUND_TRIANGLE, 70); break;
	case SND_EAT: g_snd.note (220, 35, ga::SOUND_NOISE, 50); g_snd.later (60, 180, 30, ga::SOUND_NOISE, 40); break;
	case SND_BOUNCE: g_snd.note (190, 50, ga::SOUND_TRIANGLE, 80); break;
	case SND_CATCH: g_snd.later (0, hz (784), 60, ga::SOUND_SQUARE, 45); g_snd.later (70, hz (1175), 90, ga::SOUND_SQUARE, 45); break;
	case SND_POP: g_snd.note (300, 50, ga::SOUND_TRIANGLE, 80); g_snd.later (50, 200, 70, ga::SOUND_TRIANGLE, 70); break;
	case SND_EVOLVE: for (int i = 0; i < 8; i++) g_snd.later (i * 110, 392 * (1 << (i / 4)) * (i % 4 == 0 ? 4 : i % 4 == 1 ? 5 : i % 4 == 2 ? 6 : 8) / 4, 130, ga::SOUND_TRIANGLE, 60); break;
	case SND_HATCH: g_snd.note (900, 40, ga::SOUND_NOISE, 60); g_snd.later (120, hz (880), 90, ga::SOUND_TRIANGLE, 70); g_snd.later (220, hz (1320), 160, ga::SOUND_TRIANGLE, 70); break;
	case SND_YUCK: g_snd.note (hz (140), 260, ga::SOUND_SAW, 40); break;
	case SND_SPLASH: g_snd.note (1200, 120, ga::SOUND_NOISE, 35); g_snd.later (140, 900, 90, ga::SOUND_NOISE, 25); break;
	case SND_CLEAN: g_snd.later (0, 1568, 60, ga::SOUND_TRIANGLE, 45); g_snd.later (70, 2093, 60, ga::SOUND_TRIANGLE, 45); g_snd.later (140, 2637, 100, ga::SOUND_TRIANGLE, 45); break;
	case SND_PURR: g_snd.note (hz (95), 380, ga::SOUND_TRIANGLE, 35); break;
	case SND_CRACK: g_snd.note (2400, 25, ga::SOUND_NOISE, 50); break;
	}
}
static void blip () { g_snd.note (1400, 18, ga::SOUND_TRIANGLE, 30); }

// ==== les cartes et les messages ========================================================================================
static void say (const char *fmt, int icon = -1)
{
	char b[160]; snprintf (b, sizeof b, fmt, g_pet.name);
	g_hud.toast (b, icon);
}
static void cardLater (int c, float delay, const char *title, const char *text)
{
	g_pendingCard = c; g_cardIn = delay;
	snprintf (g_cardTitle, sizeof g_cardTitle, title, g_pet.name);
	snprintf (g_cardText, sizeof g_cardText, "%s", text);
}

static void saveNow ()
{
	mkdir ("HOME:", 0777); mkdir (SAVEDIR, 0777);
	g_pet.lastReal = (int64_t) time (0);
	g_pet.save (SAVE);
	g_saveIn = 20;
}

static void newEgg (bool child)
{
	Genome parent = g_pet.g;
	int gen = child ? g_pet.generation + 1 : 1;
	uint32_t lin = child ? g_pet.lineage : hash32 ((uint32_t) time (0) ^ (uint32_t) rand ());
	g_pet.reset (lin, gen, child ? &parent : 0);
	g_pet.lastReal = (int64_t) time (0);
	g_act.reset (g_pet);
	g_hud.card = C_NONE; g_hud.infoOpen = g_hud.feedOpen = false;
	saveNow ();
}

// ==== les évènements de la vie ==========================================================================================
static void onPetEvent (int e)
{
	switch (e)
	{
	case EV_HATCH:
		g_act.start (A_HATCH, g_pet);
	{
		char t[400];
		snprintf (t, sizeof t, "%s\n%s\n%s", TR ("Prenez-en soin : nourrissez-le, jouez avec lui,"), TR ("gardez-le propre et laissez-le dormir."),
			  TR ("Il grandira, et changera à chaque stade."));
		cardLater (C_HATCHED, 3.6f, TR ("%s est né !"), t);
	}
		break;
	case EV_EVOLVE:
	{
		g_act.start (A_EVOLVE, g_pet);
		say (TR ("%s se transforme !"), -1);
		char lines[400], t[512];
		evolutionText (g_pet.prev, g_pet.g, lines, sizeof lines);
		snprintf (t, sizeof t, "%s : %s\n%s", TR ("Nouveau stade"), stageName (g_pet.g.stage), lines);
		cardLater (C_EVOLVED, 5.2f, TR ("%s a évolué !"), t);
		break;
	}
	case EV_SICK: say (TR ("%s est malade : soignez-le !"), N_HEALTH); break;
	case EV_POOP: say (TR ("%s a fait une crotte."), N_CLEAN); break;
	case EV_SLEEP: say (TR ("%s s'endort…"), N_ENERGY); break;
	case EV_WAKE: say (TR ("%s se réveille !"), N_ENERGY); if (g_act.act == A_IDLE) g_act.start (A_HAPPY, g_pet); break;
	case EV_HUNGRY: say (TR ("%s a faim !"), N_FOOD); break;
	case EV_BORED: say (TR ("%s s'ennuie…"), N_FUN); break;
	case EV_TIRED: say (TR ("%s est fatigué."), N_ENERGY); break;
	case EV_GONE:
	{
		g_act.start (A_GONE, g_pet);
		g_act.emit (P_STAR, g_act.out.center, 30, 0xFFF0B0, g_act.out.radius * 2);
		bool old = g_pet.g.stage == ST_ELDER && g_pet.st[N_HEALTH] > 1;
		char t[400];
		char l1[160];
		snprintf (l1, sizeof l1, old ? TR ("Après une longue et belle vie, %s est parti") : TR ("Trop longtemps sans soins, %s est parti."), g_pet.name);
		snprintf (t, sizeof t, "%s\n%s", l1, old ? TR ("rejoindre les étoiles. Il vous laisse un œuf.") : TR ("Il vous laisse un œuf : prenez-en bien soin."));
		g_hud.showCard (C_GONE, old ? TR ("Au revoir") : TR ("Il est parti…"), t);
		break;
	}
	}
	saveNow ();
}

// ==== les soins =========================================================================================================
static void rename ()
{
	char b[32]; snprintf (b, sizeof b, "%s", g_pet.name);
	if (ui_ask ((long) TR ("Renommer"), (long) TR ("Le nom de votre animal :"), (long) b, sizeof b) == 1 && b[0])
	{
		snprintf (g_pet.name, sizeof g_pet.name, "%s", b);
		saveNow ();
	}
}

static void action (int a)
{
	Result r;
	if (a != H_FEED && a < H_MEAL) g_hud.feedOpen = false;
	switch (a)
	{
	case H_FEED: g_hud.feedOpen = !g_hud.feedOpen; g_hud.infoOpen = false; blip (); break;
	case H_MEAL: case H_FRUIT: case H_TREAT:
		g_hud.feedOpen = false;
		if (g_act.act == A_EAT || g_act.act == A_EVOLVE || g_act.act == A_HATCH) break;
		r = g_pet.feed (a - H_MEAL);
		if (r == RS_OK) { g_act.ballOn = false; g_act.food = a - H_MEAL; g_act.start (A_EAT, g_pet); }
		else if (r == RS_FULL) { say (TR ("%s n'a plus faim."), N_FOOD); g_act.start (A_REFUSE, g_pet); }
		else if (r == RS_ASLEEP) say (TR ("%s dort."), N_ENERGY);
		else if (r == RS_REFUSED) { say (TR ("Pas de friandise quand on est malade !"), N_HEALTH); g_act.start (A_REFUSE, g_pet); }
		break;
	case H_PLAY:
		if (g_act.ballOn) { g_act.ballOn = false; if (g_act.act == A_PLAY) g_act.act = A_IDLE; blip (); break; }
		if (g_act.act == A_EVOLVE || g_act.act == A_HATCH) break;
		r = g_pet.play ();
		if (r == RS_OK)
		{
			g_act.start (A_PLAY, g_pet);
			if (g_firstPlay) { say (TR ("Cliquez dans la prairie pour lancer la balle !"), -1); g_firstPlay = false; }
		}
		else if (r == RS_TOOTIRED) { say (TR ("%s est trop fatigué pour jouer."), N_ENERGY); g_act.start (A_REFUSE, g_pet); }
		else if (r == RS_REFUSED) { say (TR ("%s est trop malade pour jouer."), N_HEALTH); g_act.start (A_REFUSE, g_pet); }
		else if (r == RS_ASLEEP) say (TR ("%s dort."), N_ENERGY);
		break;
	case H_CLEAN:
		r = g_pet.clean ();
		if (r == RS_OK)
		{
			if (!g_pet.asleep && g_act.act != A_EVOLVE && g_act.act != A_HATCH) { g_act.ballOn = false; g_act.start (A_BATH, g_pet); }
			else playSound (SND_CLEAN);
		}
		else say (TR ("Tout est déjà propre !"), N_CLEAN);
		break;
	case H_HEAL:
		if (g_act.act == A_EVOLVE || g_act.act == A_HATCH) break;
		r = g_pet.heal ();
		if (r == RS_OK || r == RS_CURED)
		{
			g_act.ballOn = false; g_act.start (A_MEDICINE, g_pet);
			if (r == RS_CURED) say (TR ("%s est guéri !"), N_HEALTH);
			else if (g_pet.sick) say (TR ("Encore une dose, et %s sera guéri."), N_HEALTH);
		}
		else if (r == RS_REFUSED) { say (TR ("%s n'est pas malade."), N_HEALTH); g_act.start (A_REFUSE, g_pet); }
		break;
	case H_LAMP:
		g_pet.lamp (); blip ();
		if (g_pet.lampOff && !g_pet.asleep) say (TR ("Il fait nuit… %s va bientôt dormir."), N_ENERGY);
		break;
	case H_CARESS: if (g_pet.canAct () && !g_pet.asleep) g_caressHold = 1.6f; break;
	case H_WAKE: if (g_pet.wake () == RS_OK) { say (TR ("%s se réveille en grognant."), N_FUN); g_act.start (A_REFUSE, g_pet); } break;
	case H_INFO: g_hud.infoOpen = !g_hud.infoOpen; blip (); break;
	case H_CLOSE: g_hud.infoOpen = false; blip (); break;
	case H_RENAME: rename (); break;
	case H_CARD: g_hud.card = C_NONE; blip (); break;
	case H_NEWEGG:
	{
		newEgg (true);
		char b[96]; snprintf (b, sizeof b, TR ("Un nouvel œuf ! Génération %d."), g_pet.generation);
		g_hud.toast (b, -1);
		break;
	}
	}
	saveNow ();
}

// ==== les menus =========================================================================================================
enum { M_NEW = 1, M_RENAME, M_SOUND, M_FEED = 10, M_PLAY, M_CLEAN, M_HEAL, M_LAMP, M_CARESS, M_INFO, M_SP1 = 20, M_SP10, M_SP60 };
static const char MENU[] =
	"MPartie\n"
	"I1\tNouvel œuf…\n"
	"I2\tRenommer…\tF2\n"
	"-\n"
	"I3c\tSon\n"
	"MSoins\n"
	"I10\tNourrir\tN\n"
	"I11\tJouer\tJ\n"
	"I12\tLaver\tL\n"
	"I13\tSoigner\tO\n"
	"I14\tLumière\tE\n"
	"I15\tCâliner\tC\n"
	"-\n"
	"I16\tFiche de l'animal\tI\n"
	"MRythme\n"
	"I20\tTemps réel\n"
	"I21\tRapide (×10)\n"
	"I22\tTrès rapide (×60)\n";
static void menuStates ()
{
	ui_menu_state (M_SOUND, g_snd.mute ? 0 : UMS_CHECKED);
	ui_menu_state (M_SP1, g_pet.speed == 1 ? UMS_CHECKED : 0);
	ui_menu_state (M_SP10, g_pet.speed == 10 ? UMS_CHECKED : 0);
	ui_menu_state (M_SP60, g_pet.speed == 60 ? UMS_CHECKED : 0);
}
static void command (int id)
{
	switch (id)
	{
	case M_NEW:
		if (ui_message ((long) TR ("Nouvel œuf"), (long) TR ("Recommencer avec un nouvel œuf ? L'animal actuel sera perdu."), UM_YESNO) == 1)
		{ newEgg (false); say (TR ("Un nouvel œuf vient d'apparaître !"), -1); }
		break;
	case M_RENAME: rename (); break;
	case M_SOUND: g_snd.toggle (); break;
	case M_FEED: action (H_FEED); break;
	case M_PLAY: action (H_PLAY); break;
	case M_CLEAN: action (H_CLEAN); break;
	case M_HEAL: action (H_HEAL); break;
	case M_LAMP: action (H_LAMP); break;
	case M_CARESS: action (g_pet.asleep ? H_WAKE : H_CARESS); break;
	case M_INFO: action (H_INFO); break;
	case M_SP1: g_pet.speed = 1; saveNow (); break;
	case M_SP10: g_pet.speed = 10; saveNow (); break;
	case M_SP60: g_pet.speed = 60; saveNow (); break;
	}
	menuStates ();
}

// ==== la souris, le clavier =============================================================================================
static bool groundAt (const V3 &o, const V3 &d, V3 &p)
{
	if (d.y > -0.01f) return false;
	float t = -o.y / d.y;
	p = o + d * t;
	return true;
}
static bool g_trace = false;
static void onMouse (int kind, int x, int y, int buttons)
{
	if (g_trace && kind) fprintf (stderr, "morpho : souris %d (%d, %d) boutons %d, interface %d\n", kind, x, y, buttons, g_hud.hit (x, y));
	if (kind == 0)
	{
		if (g_down && !g_dragPet && (abs (x - g_downX) + abs (y - g_downY) > 4) && g_downHud == H_NONE && !g_hud.card) g_dragCam = true;
		if ((g_dragCam && g_down) || g_rdown)
		{
			g_camYaw -= (float) (x - g_mx) * 0.008f;
			g_camPitch = clampf (g_camPitch + (float) (y - g_my) * 0.005f, 0.06f, 0.95f);
		}
		if (g_dragPet && g_down && (x != g_mx || y != g_my)) g_strokeFor = 0.3f;
		g_mx = x; g_my = y;
		g_hud.hover = g_hud.hit (x, y);
		V3 d = g_scene.cam.ray ((float) x, (float) y);
		g_cursor = g_scene.cam.eye + d * len (g_act.out.head - g_scene.cam.eye);
		g_cursorFor = g_hud.over (x, y) ? 0 : 1.5f;
		ui_cursor (g_hud.hover != H_NONE ? UC_HAND : (g_dragCam || g_rdown) ? UC_MOVE : g_act.hitPet (g_scene.cam.eye, d) ? UC_HAND : UC_ARROW);
		return;
	}
	if (kind == 1)
	{
		g_mx = x; g_my = y;
		if (buttons & 2) { g_rdown = true; return; }
		if (!(buttons & 1)) return;
		g_down = true; g_downX = x; g_downY = y; g_dragCam = false; g_dragPet = false;
		g_downHud = g_hud.hit (x, y);
		if (g_downHud != H_NONE || g_hud.over (x, y)) { if (g_downHud == H_NONE) g_downHud = -1; return; }
		if (g_hud.feedOpen) { g_hud.feedOpen = false; }
		V3 d = g_scene.cam.ray ((float) x, (float) y);
		if (g_act.clickPoop (g_scene.cam.eye, d) && g_pet.poops > 0)
		{
			g_pet.poops--; g_pet.st[N_CLEAN] = fminf (100, g_pet.st[N_CLEAN] + 5);
			playSound (SND_CLEAN);
			g_down = false;
			return;
		}
		if (g_act.hitPet (g_scene.cam.eye, d) && g_pet.canAct ()) { g_dragPet = true; g_strokeFor = 0.3f; }
		return;
	}
	if (kind == 2)
	{
		if (g_rdown && !(buttons & 2)) g_rdown = false;
		if (!g_down) return;
		g_down = false;
		int h = g_hud.hit (x, y);
		if (g_downHud > H_NONE && h == g_downHud) action (h);
		else if (g_downHud == H_NONE && !g_dragCam && !g_dragPet && g_act.ballOn && !g_hud.card)
		{
			V3 d = g_scene.cam.ray ((float) x, (float) y), p;
			if (groundAt (g_scene.cam.eye, d, p))
			{
				V3 from = g_scene.cam.eye + d * 1.2f;
				from.y = fmaxf (from.y, 0.6f);
				g_act.throwBall (from, p);
			}
		}
		g_dragCam = false; g_dragPet = false; g_downHud = H_NONE;
	}
}
static void onKey (int code, int c)
{
	if (c >= 'A' && c <= 'Z') c += 32;
	if (code == KEY_ESC) { g_hud.feedOpen = false; g_hud.infoOpen = false; if (g_hud.card && g_hud.card != C_GONE) g_hud.card = C_NONE; return; }
	if (g_hud.card)
	{
		if (code == KEY_ENTER || c == ' ') action (g_hud.card == C_GONE ? H_NEWEGG : H_CARD);
		return;
	}
	if (g_hud.feedOpen && c >= '1' && c <= '3') { action (H_MEAL + (c - '1')); return; }
	switch (c)
	{
	case 'n': action (H_FEED); break;
	case 'j': action (H_PLAY); break;
	case 'l': action (H_CLEAN); break;
	case 'o': action (H_HEAL); break;
	case 'e': action (H_LAMP); break;
	case 'c': action (g_pet.asleep ? H_WAKE : H_CARESS); break;
	case 'i': action (H_INFO); break;
	}
}
static void oneEvent (long t)
{
	if (t == WEV_CLOSE) { g_quit = true; return; }
	if (t == WEV_MENU) { command ((int) ui_ev_field (&ui, EV_CODE)); return; }
	if (t == WEV_FOCUS && ui_ev_field (&ui, EV_KIND) == 0) { g_down = g_rdown = g_dragCam = g_dragPet = false; return; }
	if (t == WEV_WHEEL) { g_camZoom = clampf (g_camZoom * (ui_ev_field (&ui, EV_Y) > 0 ? 0.9f : 1.11f), 0.55f, 1.9f); return; }
	if (t == WEV_KEY)
	{
		if (ui_ev_field (&ui, EV_KIND) == 0) return;
		long id = ui_menu_key (0);
		if (id >= 0) { command ((int) id); return; }
		if (ui_ev_field (&ui, EV_CODE) == 0x3B) { rename (); return; }		// F2
		onKey ((int) ui_ev_field (&ui, EV_CODE), (int) ui_ev_field (&ui, EV_CHAR));
		return;
	}
	if (t == WEV_MOUSE)
	{
		onMouse ((int) ui_ev_field (&ui, EV_KIND), (int) ui_ev_field (&ui, EV_X), (int) ui_ev_field (&ui, EV_Y), (int) ui_ev_field (&ui, EV_CODE));
		if (ui_ev_field (&ui, EV_KIND) != 1) return;
	}
	long code = 0, taken = 0;
	uikit_w_event (&code, &taken);			// (la taille de la fenêtre, son déplacement par le haut)
}

// ==== une image =========================================================================================================
void app_frame (float dt, float realDt)
{
	g_t += dt;
	float hour = hourNow ();
	// la vie (le temps simulé : le rythme choisi)
	int evs[16], ne = 0;
	// (le temps de la vie est le vrai temps écoulé, même quand les images sont lentes ; l'animation, elle, est bornée)
	if (!g_pet.gone) g_pet.step (realDt * (float) g_pet.speed, hour, false, evs, ne);
	g_pet.lastReal = (int64_t) time (0);
	for (int i = 0; i < ne; i++) onPetEvent (evs[i]);
	if (g_pendingCard)
	{
		g_cardIn -= dt;
		if (g_cardIn <= 0) { g_hud.showCard (g_pendingCard, g_cardTitle, g_cardText); g_pendingCard = C_NONE; playSound (SND_HAPPY); }
	}
	// les caresses
	if (g_strokeFor > 0) g_strokeFor -= dt;
	if (g_caressHold > 0) g_caressHold -= dt;
	g_act.stroking = (g_dragPet && g_strokeFor > 0) || g_caressHold > 0;
	if (g_act.stroking) g_pet.caress (dt);
	if (g_cursorFor > 0) g_cursorFor -= dt;
	// l'animal
	g_act.update (dt, g_pet, g_scene.cam.eye, g_cursorFor > 0 ? &g_cursor : 0);
	for (int i = 0; i < g_act.nsnd; i++) playSound (g_act.snd[i]);
	g_snd.tick ();
	g_hud.update (dt);
	// la caméra : elle suit l'animal, s'éloigne quand il grandit
	{
		V3 c = g_act.out.center;
		V3 want (c.x * 0.7f, fmaxf (0.2f, c.y * 0.75f + 0.08f), c.z * 0.7f);
		float k = 1 - expf (-dt * 2.5f);
		g_camAt = g_camAt + (want - g_camAt) * k;
		float d = (1.45f + g_act.out.radius * 3.2f) * g_camZoom;
		g_camDist += (d - g_camDist) * k;
		float yaw = g_camYaw + 0.04f * sinf (g_t * 0.13f);
		V3 dir (sinf (yaw) * cosf (g_camPitch), sinf (g_camPitch), cosf (yaw) * cosf (g_camPitch));
		g_scene.cam.at = g_camAt;
		g_scene.cam.eye = g_camAt + dir * g_camDist;
		if (g_scene.cam.eye.y < 0.15f) g_scene.cam.eye.y = 0.15f;
	}
	// la scène
	g_scene.begin ();
	g_act.build (g_scene.cm, g_scene.props, g_scene.propsSheets, g_scene.propsAlpha);
	if (g_act.skinDirty)
	{
		static std::vector<uint32_t> sk ((size_t) SKIN_W * SKIN_H);
		if (g_act.shown.stage == ST_EGG) makeShell (g_act.shown, sk.data ()); else makeSkin (g_act.shown, sk.data ());
		g_scene.setSkin (sk.data ());
		g_act.skinDirty = false;
	}
	g_scene.shadowAt = V3 (g_act.out.center.x, 0, g_act.out.center.z);
	g_scene.shadowR = g_act.out.footprint * 1.2f;
	g_scene.shadowK = g_act.act == A_GONE ? 0 : clampf (1 - g_act.pose.hop * 1.6f, 0.3f, 1);
	g_scene.light = lightAt (hour, g_pet.lampOff, 0);
	g_px = ui.surf; g_W = (int) ui.w; g_H = (int) ui.h;
	g_scene.draw (g_px, g_W, g_H, g_W, g_t);
	ga::Img im = ga::window ();
	g_hud.draw (im, g_pet, g_act, g_scene.cam, g_scene.light);
	g_saveIn -= dt;
	if (g_saveIn <= 0) saveNow ();
}

// les essais : la ligne de commande
static void testArgs (int argc, char **argv, bool &fresh, int &stage, uint32_t &seed)
{
	for (int i = 1; i < argc; i++)
	{
		const char *p = argv[i];
		if (!strcmp (p, "new")) fresh = true;
		else if (!strncmp (p, "speed=", 6)) g_pet.speed = clampi (atoi (p + 6), 1, 600);
		else if (!strncmp (p, "hour=", 5)) g_hourFixed = (float) atof (p + 5);
		else if (!strncmp (p, "stage=", 6)) { stage = clampi (atoi (p + 6), 0, ST_COUNT - 1); fresh = true; }
		else if (!strncmp (p, "seed=", 5)) seed = (uint32_t) strtoul (p + 5, 0, 10);
		else if (!strcmp (p, "trace")) g_trace = true;
	}
}

void app_init (int argc, char **argv)
{
	srand ((unsigned) time (0));
	loadClockOffset ();
	bool fresh = false; int stage = -1; uint32_t seed = 0;
	int speedArg = 0;
	g_pet.speed = 1;
	testArgs (argc, argv, fresh, stage, seed);
	speedArg = g_pet.speed;
	g_scene.init (12345);
	g_snd.open ();
	bool loaded = !fresh && g_pet.load (SAVE);
	if (!loaded)
	{
		g_pet.reset (seed ? seed : hash32 ((uint32_t) time (0) ^ (uint32_t) rand ()), 1, 0);
		g_pet.speed = speedArg;
		g_pet.lastReal = (int64_t) time (0);
		if (stage > 0)
		{
			// (aller tout droit à un stade, pour les essais)
			for (int s = 1; s <= stage; s++) { g_pet.prev = g_pet.g; g_pet.g = evolve (g_pet.g, hash32 (g_pet.g.seed + (uint32_t) s), 0.8f); g_pet.hist[s] = 1; g_pet.histSeed[s] = g_pet.g.seed; }
			g_pet.age = 3600.0 * stage * stage; g_pet.stageAge = 60;
			for (int i = 0; i < N_COUNT; i++) g_pet.st[i] = 75;
		}
		g_act.reset (g_pet);
		if (stage <= 0)
		{
			char t[400];
			snprintf (t, sizeof t, "%s\n%s\n%s", TR ("Un œuf mystérieux vient d'apparaître dans la prairie."),
				  TR ("Il va bientôt éclore : la créature qui en sortira"), TR ("est unique, et changera à chaque stade de sa vie."));
			g_hud.showCard (C_WELCOME, TR ("Un œuf !"), t);
		}
		saveNow ();
	}
	else
	{
		if (speedArg > 1) g_pet.speed = speedArg;
		int64_t before = g_pet.lastReal;
		double done = g_pet.catchUp ((int64_t) time (0), hourNow ());
		g_act.reset (g_pet);
		if (g_pet.gone)
		{
			char t[300];
			snprintf (t, sizeof t, "%s\n%s", TR ("Pendant votre absence, il est parti."), TR ("Il vous laisse un œuf."));
			g_hud.showCard (C_GONE, TR ("Il est parti…"), t);
		}
		else if (done > 15 * 60)
		{
			char b[256], d[64];
			formatDur ((double) ((int64_t) time (0) - before), d, sizeof d);
			snprintf (b, sizeof b, TR ("%s vous attendait (absent %s)."), g_pet.name, d);
			g_hud.toast (b, N_FUN);
		}
	}
	// la caméra regarde l'animal de face
	g_camYaw = 0.35f;
}

#ifndef MORPHO_HOST
int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	audiokit_kit_open ();
	if (!gpukit_kit_open ()) return 1;
#if defined(__x86_64__) && defined(MORPHO_GL)
	gk_gl_attach ();				// (sans GPU Intel : rien, GpuKit dessine au processeur)
#endif
	long scr[4] = { 1280, 800, 0, 0 };
	ui_screen ((long) scr);
	long w = scr[0] - 120, h = scr[1] - scr[2] - scr[3] - 90;
	if (w > 980) w = 980;
	if (h > 700) h = 700;
	if (w < 600) w = 600;
	if (h < 460) h = 460;
	if (ui_window_ex (-1, -1, w, h, (long) "Morpho", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (560, 440);
	app_init (argc, argv);
	ui_menu ((long) MENU);
	menuStates ();
	struct timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts);
	double last = (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
	while (!g_quit)
	{
		long t;
		while ((t = ui_event (0)) > 0) { oneEvent (t); if (g_quit) break; }
		if (g_quit) break;
		clock_gettime (CLOCK_MONOTONIC, &ts);
		double now = (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
		float real = (float) (now - last); last = now;
		if (real > 30) real = 30;
		app_frame (real > 0.1f ? 0.1f : real, real);
		ui_update_all ();
		// l'image suivante quand Elegant a pris celle-ci ; au plus 30 images par seconde
		if (ui_frame_request ())
		{
			bool got = false;
			for (double t0 = now; !g_quit; )
			{
				long e = ui_event (6);
				clock_gettime (CLOCK_MONOTONIC, &ts);
				double n2 = (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
				if (e == WEV_FRAME) got = true;
				else if (e > 0) oneEvent (e);
				if ((got && n2 - t0 >= 0.032) || n2 - t0 > 0.2) break;
			}
		}
		else
		{
			long e = ui_event (25);
			if (e > 0) oneEvent (e);
		}
	}
	saveNow ();
	g_snd.close ();
	return 0;
}
#endif
