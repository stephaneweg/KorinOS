/*
 * sdltest -- essai de SDL 3 sur ARMPI (docs/SDL.md) : fenetre, 2D, clavier, fils, minuteur, fichiers, son.
 *     sdltest [--sw] [--dry] [--full] [--frames N] [--events S] [--win W H]
 * --sw : le rendu du processeur meme s'il y a un GPU. --dry : Mesa a blanc, pour QEMU. --events S : garde la fenetre S secondes et affiche les
 * evenements. Les trois essais de pixels relisent l'image : ils disent PASS ou FAIL, a blanc ils sont sautes.
 * Le meme source se compile en SDL 2 par sdl2test.c : c'est ce que voient SuperTux et SuperTuxKart.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int W = 400, H = 300;

static double now_ms (void)
{
	return SDL_GetTicksNS () / 1e6;
}

static int thread_fn (void *p)
{
	SDL_AddAtomicInt ((SDL_AtomicInt *) p, 1000);
	return 7;
}

static Uint32 timer_fn (void *p, SDL_TimerID id, Uint32 interval)
{
	SDL_AddAtomicInt ((SDL_AtomicInt *) p, 1);
	return interval;
}

static int bases (void)
{
	SDL_AtomicInt n;
	SDL_Thread *t[4];
	int fails = 0, rc;

	SDL_SetAtomicInt (&n, 0);
	for (int i = 0; i < 4; i++)
		t[i] = SDL_CreateThread (thread_fn, "essai", &n);
	for (int i = 0; i < 4; i++) {
		SDL_WaitThread (t[i], &rc);
		fails += rc != 7;
	}
	printf ("sdltest : fils : %d, %s\n", SDL_GetAtomicInt (&n), SDL_GetAtomicInt (&n) == 4000 && !fails ? "PASS" : "FAIL");
	fails += SDL_GetAtomicInt (&n) != 4000;

	SDL_SetAtomicInt (&n, 0);
	SDL_TimerID id = SDL_AddTimer (20, timer_fn, &n);
	SDL_Delay (210);
	SDL_RemoveTimer (id);
	rc = SDL_GetAtomicInt (&n);
	printf ("sdltest : minuteur 20 ms : %d en 210 ms, %s\n", rc, rc >= 7 && rc <= 11 ? "PASS" : "FAIL");
	fails += rc < 7 || rc > 11;

	const char *base = SDL_GetBasePath ();
	printf ("sdltest : dossier du programme : %s\n", base ? base : SDL_GetError ());
	const char *f = "RAM0:/sdltest.txt";
	SDL_IOStream *io = SDL_IOFromFile (f, "w");
	if (io) {
		SDL_WriteIO (io, "ARMPI", 5);
		SDL_CloseIO (io);
	}
	char buf[8] = { 0 };
	io = SDL_IOFromFile (f, "r");
	if (io) {
		SDL_ReadIO (io, buf, 5);
		SDL_CloseIO (io);
	}
	SDL_PathInfo info;
	SDL_zero (info);
	bool got = SDL_GetPathInfo (f, &info);
	printf ("sdltest : fichier : %s, taille %d, %s\n", buf, (int) info.size, !SDL_strcmp (buf, "ARMPI") && got && info.size == 5 ? "PASS" : "FAIL");
	fails += SDL_strcmp (buf, "ARMPI") != 0 || !got;
	SDL_RemovePath (f);
	return fails;
}

static SDL_AudioStream *audio;
static float phase;

static void audio_cb (void *u, SDL_AudioStream *s, int need, int total)
{
	short b[1024];
	int n = need / 2 > 1024 ? 1024 : need / 2;
	for (int i = 0; i < n; i++) {
		b[i] = (short) (SDL_sinf (phase) * 6000);
		phase += 2 * SDL_PI_F * 440.0f / 22050.0f;
		if (phase > 2 * SDL_PI_F)
			phase -= 2 * SDL_PI_F;
	}
	SDL_PutAudioStreamData (s, b, n * 2);
}

static void start_audio (void)
{
	SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, 22050 };

	audio = SDL_OpenAudioDeviceStream (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, NULL);
	if (!audio) {
		printf ("sdltest : pas de son : %s\n", SDL_GetError ());
		return;
	}
	SDL_ResumeAudioStreamDevice (audio);
	printf ("sdltest : son : pilote %s, 440 Hz mono 22 kHz\n", SDL_GetCurrentAudioDriver ());
}

/* la scene : fond, rectangles pleins, texture 2 x 2 agrandie, un carre qui bouge */
static SDL_Texture *checker (SDL_Renderer *r)
{
	static const Uint32 px[4] = { 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF };
	SDL_Texture *t = SDL_CreateTexture (r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 2, 2);

	SDL_UpdateTexture (t, NULL, px, 8);
	SDL_SetTextureScaleMode (t, SDL_SCALEMODE_NEAREST);
	return t;
}

static void scene (SDL_Renderer *r, SDL_Texture *tex, int frame)
{
	SDL_FRect a = { 20, 20, 100, 60 }, d = { 140, 20, 80, 80 }, m = { (float) (frame * 3 % (W - 40)), (float) (H - 60), 40, 40 };

	SDL_SetRenderDrawColor (r, 16, 32, 64, 255);
	SDL_RenderClear (r);
	SDL_SetRenderDrawColor (r, 255, 128, 0, 255);
	SDL_RenderFillRect (r, &a);
	SDL_RenderTexture (r, tex, NULL, &d);
	SDL_SetRenderDrawColor (r, 0, 255, 255, 255);
	SDL_RenderFillRect (r, &m);
}

static int check_px (SDL_Surface *s, int x, int y, int cr, int cg, int cb, const char *what)
{
	Uint8 pr, pg, pb, pa;

	SDL_ReadSurfacePixel (s, x, y, &pr, &pg, &pb, &pa);
	int ok = SDL_abs (pr - cr) <= 2 && SDL_abs (pg - cg) <= 2 && SDL_abs (pb - cb) <= 2;
	printf ("sdltest : pixel %s (%d, %d) : %d %d %d, attendu %d %d %d, %s\n", what, x, y, pr, pg, pb, cr, cg, cb, ok ? "PASS" : "FAIL");
	return !ok;
}

static void show_event (const SDL_Event *e)
{
	switch (e->type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		printf ("sdltest : touche %s : scancode %d (%s), code 0x%x, repetee %d\n", e->key.down ? "appui" : "relache", e->key.scancode,
			SDL_GetScancodeName (e->key.scancode), e->key.key, e->key.repeat);
		break;
	case SDL_EVENT_TEXT_INPUT:
		printf ("sdltest : texte « %s »\n", e->text.text);
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		printf ("sdltest : bouton %d %s en %.0f, %.0f\n", e->button.button, e->button.down ? "appui" : "relache", e->button.x, e->button.y);
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		printf ("sdltest : molette %.0f\n", e->wheel.y);
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		printf ("sdltest : taille %d x %d\n", e->window.data1, e->window.data2);
		break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		printf ("sdltest : focus %d\n", e->type == SDL_EVENT_WINDOW_FOCUS_GAINED);
		break;
	case SDL_EVENT_GAMEPAD_ADDED:
		printf ("sdltest : manette %s\n", SDL_GetGamepadNameForID (e->gdevice.which));
		SDL_OpenGamepad (e->gdevice.which);
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		printf ("sdltest : manette, bouton %s\n", SDL_GetGamepadStringForButton (e->gbutton.button));
		break;
	default:
		break;
	}
	fflush (stdout);
}

int main (int argc, char **argv)
{
	int frames = 120, sw = 0, fails = 0, secs = 0;
	SDL_WindowFlags full = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--sw")) sw = 1;
		if (!strcmp (argv[i], "--full")) full = SDL_WINDOW_FULLSCREEN;
		if (!strcmp (argv[i], "--dry")) SDL_setenv_unsafe ("SDL_ARMPI_GL", "dry", 1);
		if (!strcmp (argv[i], "--frames") && i + 1 < argc) frames = atoi (argv[++i]);
		if (!strcmp (argv[i], "--events") && i + 1 < argc) secs = atoi (argv[++i]);
		if (!strcmp (argv[i], "--win") && i + 2 < argc) { W = atoi (argv[++i]); H = atoi (argv[++i]); }
	}
	if (!SDL_Init (SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
		printf ("sdltest : SDL_Init : %s\n", SDL_GetError ());
		return 1;
	}
	printf ("sdltest : SDL %d.%d.%d, %s, video %s, %d coeurs\n", SDL_VERSIONNUM_MAJOR (SDL_GetVersion ()), SDL_VERSIONNUM_MINOR (SDL_GetVersion ()),
		SDL_VERSIONNUM_MICRO (SDL_GetVersion ()), SDL_GetPlatform (), SDL_GetCurrentVideoDriver (), SDL_GetNumLogicalCPUCores ());
	fflush (stdout);
	fails += bases ();
	if (SDL_InitSubSystem (SDL_INIT_AUDIO))
		start_audio ();

	SDL_Window *win = SDL_CreateWindow ("sdltest", W, H, SDL_WINDOW_RESIZABLE | full);
	if (!win) {
		printf ("sdltest : fenetre : %s\n", SDL_GetError ());
		return 1;
	}
	SDL_Renderer *r = SDL_CreateRenderer (win, sw ? SDL_SOFTWARE_RENDERER : NULL);
	if (!r) {
		printf ("sdltest : rendu : %s\n", SDL_GetError ());
		return 1;
	}
	const char *name = SDL_GetRendererName (r);
	printf ("sdltest : rendu %s\n", name);
	fflush (stdout);
	SDL_Texture *tex = checker (r);
	SDL_StartTextInput (win);

	/* (une image relue : a blanc, Mesa n'envoie rien au GPU, l'image reste noire) */
	const char *gl = SDL_getenv ("SDL_ARMPI_GL");
	scene (r, tex, 0);
	if (gl && !SDL_strcmp (gl, "dry"))
		printf ("sdltest : a blanc : pixels non relus\n");
	else {
		SDL_Surface *s = SDL_RenderReadPixels (r, NULL);
		if (s) {
			fails += check_px (s, 5, 5, 16, 32, 64, "fond");
			fails += check_px (s, 60, 50, 255, 128, 0, "rectangle");
			fails += check_px (s, 150, 30, 255, 0, 0, "texture rouge");
			fails += check_px (s, 210, 90, 255, 255, 255, "texture blanche");
			SDL_DestroySurface (s);
		} else {
			printf ("sdltest : relecture : %s\n", SDL_GetError ());
			fails++;
		}
	}
	SDL_RenderPresent (r);

	double t0 = now_ms (), end = t0 + secs * 1000.0;
	int n = 0, quit = 0;
	while (!quit && (secs ? now_ms () < end : n < frames)) {
		SDL_Event e;
		while (SDL_PollEvent (&e)) {
			if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
				quit = 1;
			if (e.type == SDL_EVENT_WINDOW_RESIZED) {
				W = e.window.data1;
				H = e.window.data2;
			}
			show_event (&e);
		}
		scene (r, tex, n);
		SDL_RenderPresent (r);
		n++;
	}
	double dt = now_ms () - t0;
	printf ("sdltest : %d images en %.0f ms, %.1f images/s (%s)\n", n, dt, n * 1000.0 / dt, name);
	if (audio)
		SDL_DestroyAudioStream (audio);
	SDL_DestroyRenderer (r);
	SDL_DestroyWindow (win);
	SDL_Quit ();
	printf ("sdltest : %s\n", fails ? "FAIL" : "PASS");
	return fails != 0;
}
