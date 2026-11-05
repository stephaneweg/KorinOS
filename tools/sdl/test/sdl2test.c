/*
 * sdl2test -- l'API de SDL 2 par sdl2-compat, comme la voient SuperTux et SuperTuxKart (docs/SDL.md).
 *     sdl2test [--sw] [--gl] [--dry] [--nodepth] [--noread] [--discard] [--keep c|z|cz] [--split] [--split-store] [--fbo c|d16|d24s8|d32f] [--fbosize L H] [--tri N] [--frames N] [--pad S]
 * Sans --gl : SDL_Renderer, comme SuperTux avec son rendu SDL. --gl : la suite d'appels de CIrrDeviceSDL de
 * SuperTuxKart : fenetre SDL_WINDOW_OPENGL, attributs GL, contexte ES 3.0, fonctions par SDL_GL_GetProcAddress
 * (glad y passe), shaders #version 300 es, puis cube qui tourne avec profondeur, comme testgles2 de SDL.
 * --nodepth, --noread, --discard (jette le Z avant chaque image, comme EGL sous Linux), --tri N (N triangles) : pour isoler le job que le Pi n'a pas fini (docs/SDL.md).
 * --keep c|z|cz : n'efface plus la couleur, le Z ou les deux apres la premiere image : le job recharge alors ce tampon depuis
 * la memoire. --split : glFlush au milieu de l'image, le second job recharge couleur et Z (ce que fait un jeu qui vide
 * son job en cours d'image, ou V3D_DEBUG=always_flush). --split-store : pareil, mais le Z est jete apres le glFlush :
 * le premier job ecrit le Z, le second ne le recharge pas (ecriture seule). --fbo : dessine dans une texture RGBA8
 * (format UIF, pas l'image raster de la fenetre), avec un Z du format donne (c : sans Z) ; rien n'est montre.
 * --fbosize L H : la taille de cette texture (la fenetre sinon ; celle de l'ecran : comme la surface GL d'une fenetre).
 */
#include <SDL.h>
#include <stdio.h>

#define GL_TRIANGLES		0x0004
#define GL_FLOAT		0x1406
#define GL_UNSIGNED_BYTE	0x1401
#define GL_RGBA			0x1908
#define GL_FRAGMENT_SHADER	0x8B30
#define GL_VERTEX_SHADER	0x8B31
#define GL_COMPILE_STATUS	0x8B81
#define GL_LINK_STATUS		0x8B82
#define GL_COLOR_BUFFER_BIT	0x4000
#define GL_DEPTH_BUFFER_BIT	0x0100
#define GL_DEPTH_TEST		0x0B71
#define GL_ARRAY_BUFFER		0x8892
#define GL_STATIC_DRAW		0x88E4
#define GL_VENDOR		0x1F00
#define GL_RENDERER		0x1F01
#define GL_VERSION		0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_EXTENSIONS		0x1F03
#define GL_FRAMEBUFFER		0x8D40
#define GL_DEPTH		0x1801
#define GL_STENCIL		0x1802
#define GL_TEXTURE_2D		0x0DE1
#define GL_RGBA8		0x8058
#define GL_TEXTURE_MIN_FILTER	0x2801
#define GL_NEAREST		0x2600
#define GL_COLOR_ATTACHMENT0	0x8CE0
#define GL_DEPTH_ATTACHMENT	0x8D00
#define GL_STENCIL_ATTACHMENT	0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_RENDERBUFFER		0x8D41
#define GL_DEPTH_COMPONENT16	0x81A5
#define GL_DEPTH24_STENCIL8	0x88F0
#define GL_DEPTH_COMPONENT32F	0x8CAC
#define GL_FRAMEBUFFER_COMPLETE	0x8CD5

static const char *vs_src =
	"#version 300 es\n"
	"precision highp float;\n"
	"layout(location = 0) in vec3 pos;\n"
	"layout(location = 1) in vec3 col;\n"
	"uniform float angle;\n"
	"out vec3 v;\n"
	"void main () {\n"
	"	float c = cos (angle), s = sin (angle), c2 = cos (angle * 0.7), s2 = sin (angle * 0.7);\n"
	"	vec3 p = vec3 (c * pos.x + s * pos.z, pos.y, -s * pos.x + c * pos.z);\n"
	"	p = vec3 (p.x, c2 * p.y - s2 * p.z, s2 * p.y + c2 * p.z);\n"
	"	v = col;\n"
	"	gl_Position = vec4 (p.xy * 0.6, p.z * 0.3, 1.0);\n"
	"}\n";
static const char *fs_src =
	"#version 300 es\n"
	"precision highp float;\n"
	"in vec3 v;\n"
	"out vec4 c;\n"
	"void main () { c = vec4 (v, 1.0); }\n";

/* les 36 sommets du cube, faits au demarrage */
static float cube[36 * 6];

static void make_cube (void)
{
	static const signed char q[6][4][3] = {
		{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}, {{-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1}},
		{{-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}}, {{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}},
		{{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}}, {{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}} };
	static const float col[6][3] = { {1,0,0}, {0,1,0}, {0,0,1}, {1,1,0}, {1,0,1}, {0,1,1} };
	static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
	float *o = cube;
	for (int f = 0; f < 6; f++)
		for (int t = 0; t < 6; t++) {
			for (int k = 0; k < 3; k++)
				*o++ = q[f][tri[t]][k];
			for (int k = 0; k < 3; k++)
				*o++ = col[f][k];
		}
}

#define FN(ret, name, ...) ret (*name) (__VA_ARGS__) = (ret (*) (__VA_ARGS__)) SDL_GL_GetProcAddress (#name); \
	if (!name) { printf ("sdl2test : %s manque\n", #name); return 1; }

static int nodepth, noread, discard_z, ntri = 12, split, split_store;
static const char *keep = "", *fbo = NULL;
static int fbo_w, fbo_h;

static int run_gl (int frames, const char *mode)
{
	SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_Window *win = SDL_CreateWindow ("sdl2test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 400, 400,
					    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!win) {
		printf ("sdl2test : fenetre GL refusee : %s (repli propre)\n", SDL_GetError ());
		return 0;
	}
	SDL_GLContext ctx = SDL_GL_CreateContext (win);
	if (!ctx) {
		printf ("sdl2test : contexte refuse : %s (repli propre)\n", SDL_GetError ());
		return 0;
	}
	FN (const unsigned char *, glGetString, unsigned)
	FN (unsigned, glCreateShader, unsigned)
	FN (void, glShaderSource, unsigned, int, const char *const *, const int *)
	FN (void, glCompileShader, unsigned)
	FN (void, glGetShaderiv, unsigned, unsigned, int *)
	FN (void, glGetShaderInfoLog, unsigned, int, int *, char *)
	FN (unsigned, glCreateProgram, void)
	FN (void, glAttachShader, unsigned, unsigned)
	FN (void, glLinkProgram, unsigned)
	FN (void, glGetProgramiv, unsigned, unsigned, int *)
	FN (void, glUseProgram, unsigned)
	FN (int, glGetUniformLocation, unsigned, const char *)
	FN (void, glUniform1f, int, float)
	FN (void, glGenBuffers, int, unsigned *)
	FN (void, glBindBuffer, unsigned, unsigned)
	FN (void, glBufferData, unsigned, long, const void *, unsigned)
	FN (void, glGenVertexArrays, int, unsigned *)
	FN (void, glBindVertexArray, unsigned)
	FN (void, glVertexAttribPointer, unsigned, int, unsigned, unsigned char, int, const void *)
	FN (void, glEnableVertexAttribArray, unsigned)
	FN (void, glViewport, int, int, int, int)
	FN (void, glClearColor, float, float, float, float)
	FN (void, glClear, unsigned)
	FN (void, glEnable, unsigned)
	FN (void, glDisable, unsigned)
	FN (void, glDrawArrays, unsigned, int, int)
	FN (void, glReadPixels, int, int, int, int, unsigned, unsigned, void *)
	FN (unsigned, glGetError, void)
	FN (void, glInvalidateFramebuffer, unsigned, int, const unsigned *)
	FN (void, glFlush, void)
	FN (void, glGenFramebuffers, int, unsigned *)
	FN (void, glBindFramebuffer, unsigned, unsigned)
	FN (void, glFramebufferTexture2D, unsigned, unsigned, unsigned, unsigned, int)
	FN (void, glGenTextures, int, unsigned *)
	FN (void, glBindTexture, unsigned, unsigned)
	FN (void, glTexStorage2D, unsigned, int, unsigned, int, int)
	FN (void, glTexParameteri, unsigned, unsigned, int)
	FN (void, glGenRenderbuffers, int, unsigned *)
	FN (void, glBindRenderbuffer, unsigned, unsigned)
	FN (void, glRenderbufferStorage, unsigned, unsigned, int, int)
	FN (void, glFramebufferRenderbuffer, unsigned, unsigned, unsigned, unsigned)
	FN (unsigned, glCheckFramebufferStatus, unsigned)

	printf ("sdl2test : %s / %s / %s / %s\n", glGetString (GL_VENDOR), glGetString (GL_RENDERER), glGetString (GL_VERSION),
		glGetString (GL_SHADING_LANGUAGE_VERSION));
	const char *ext = (const char *) glGetString (GL_EXTENSIONS);
	static const char *want[] = { "GL_EXT_color_buffer_float", "GL_EXT_texture_format_BGRA8888", "GL_OES_vertex_half_float",
		"GL_EXT_texture_compression_s3tc", "GL_KHR_texture_compression_astc_ldr", "GL_EXT_texture_filter_anisotropic", NULL };
	for (int i = 0; want[i]; i++)
		printf ("sdl2test : %s : %s\n", want[i], ext && SDL_strstr (ext, want[i]) ? "oui" : "non");

	const char *srcs[2] = { vs_src, fs_src };
	unsigned kinds[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	unsigned prog = glCreateProgram (), vao, vbo;
	int ok = 0;
	for (int i = 0; i < 2; i++) {
		char log[512];
		unsigned s = glCreateShader (kinds[i]);
		glShaderSource (s, 1, &srcs[i], NULL);
		glCompileShader (s);
		glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			glGetShaderInfoLog (s, sizeof log, NULL, log);
			printf ("sdl2test : shader refuse : %s\n", log);
			return 1;
		}
		glAttachShader (prog, s);
	}
	glLinkProgram (prog);
	glGetProgramiv (prog, GL_LINK_STATUS, &ok);
	if (!ok)
		return 1;
	make_cube ();
	glGenVertexArrays (1, &vao);
	glBindVertexArray (vao);
	glGenBuffers (1, &vbo);
	glBindBuffer (GL_ARRAY_BUFFER, vbo);
	glBufferData (GL_ARRAY_BUFFER, sizeof cube, cube, GL_STATIC_DRAW);
	glVertexAttribPointer (0, 3, GL_FLOAT, 0, 24, (void *) 0);
	glVertexAttribPointer (1, 3, GL_FLOAT, 0, 24, (void *) 12);
	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);
	glUseProgram (prog);
	if (!nodepth)
		glEnable (GL_DEPTH_TEST);
	int angle = glGetUniformLocation (prog, "angle");

	int w, h, n = 0, quit = 0;
	SDL_GL_GetDrawableSize (win, &w, &h);
	glViewport (0, 0, w, h);
	unsigned zatt[2] = { GL_DEPTH, GL_STENCIL };
	if (fbo) {	/* une cible UIF : texture RGBA8, Z du format demande */
		unsigned fb, tex, rb, zf = 0;
		if (fbo_w > 0 && fbo_h > 0) {
			w = fbo_w;
			h = fbo_h;
			glViewport (0, 0, w, h);
		}
		if (!SDL_strcmp (fbo, "d16")) zf = GL_DEPTH_COMPONENT16;
		if (!SDL_strcmp (fbo, "d24s8")) zf = GL_DEPTH24_STENCIL8;
		if (!SDL_strcmp (fbo, "d32f")) zf = GL_DEPTH_COMPONENT32F;
		glGenFramebuffers (1, &fb);
		glBindFramebuffer (GL_FRAMEBUFFER, fb);
		glGenTextures (1, &tex);
		glBindTexture (GL_TEXTURE_2D, tex);
		glTexStorage2D (GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
		if (zf) {
			glGenRenderbuffers (1, &rb);
			glBindRenderbuffer (GL_RENDERBUFFER, rb);
			glRenderbufferStorage (GL_RENDERBUFFER, zf, w, h);
			glFramebufferRenderbuffer (GL_FRAMEBUFFER, zf == GL_DEPTH24_STENCIL8 ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
						   GL_RENDERBUFFER, rb);
		} else
			nodepth = 1;
		zatt[0] = GL_DEPTH_ATTACHMENT;
		zatt[1] = GL_STENCIL_ATTACHMENT;
		unsigned st = glCheckFramebufferStatus (GL_FRAMEBUFFER);
		printf ("sdl2test : fbo %s, %d x %d, %s\n", fbo, w, h, st == GL_FRAMEBUFFER_COMPLETE ? "complet" : "INCOMPLET");
		if (st != GL_FRAMEBUFFER_COMPLETE)
			return 1;
		noread = 1;
	}
	if (nodepth)
		glDisable (GL_DEPTH_TEST);
	Uint64 t0 = SDL_GetPerformanceCounter (), f = SDL_GetPerformanceFrequency ();
	while (!quit && n < frames) {
		SDL_Event e;
		while (SDL_PollEvent (&e)) {
			if (e.type == SDL_QUIT)
				quit = 1;
			if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
				SDL_GL_GetDrawableSize (win, &w, &h);
				glViewport (0, 0, w, h);
				printf ("sdl2test : %d x %d\n", w, h);
			}
		}
		glClearColor (0.0f, 0.0f, 0.2f, 1.0f);
		unsigned bits = nodepth ? GL_COLOR_BUFFER_BIT : GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
		if (n > 0 && SDL_strchr (keep, 'c'))
			bits &= ~GL_COLOR_BUFFER_BIT;
		if (n > 0 && SDL_strchr (keep, 'z'))
			bits &= ~GL_DEPTH_BUFFER_BIT;
		if (bits)
			glClear (bits);
		glUniform1f (angle, n * 0.03f);
		if (split || split_store) {
			int half = ntri / 2 * 3;
			glDrawArrays (GL_TRIANGLES, 0, half);
			glFlush ();
			if (split_store)	/* le Z ecrit par le premier job n'est pas relu */
				glInvalidateFramebuffer (GL_FRAMEBUFFER, 2, zatt);
			glDrawArrays (GL_TRIANGLES, half, ntri * 3 - half);
		} else
			glDrawArrays (GL_TRIANGLES, 0, ntri * 3);
		if (n == 0 && !noread && !(mode && !SDL_strcmp (mode, "dry"))) {
			unsigned char c[4], bg[4];
			glReadPixels (w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
			glReadPixels (2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bg);
			printf ("sdl2test : centre %d %d %d, coin %d %d %d, %s\n", c[0], c[1], c[2], bg[0], bg[1], bg[2],
				c[0] + c[1] + c[2] > 200 && bg[2] > 40 && bg[0] < 5 ? "PASS" : "FAIL");
		}
		if (discard_z)
			glInvalidateFramebuffer (GL_FRAMEBUFFER, 2, zatt);
		SDL_GL_SwapWindow (win);
		n++;
	}
	double dt = (double) (SDL_GetPerformanceCounter () - t0) / f;
	printf ("sdl2test : GL : %d images, %.1f images/s, erreur GL %u\n", n, n / dt, glGetError ());
	SDL_GL_DeleteContext (ctx);
	SDL_DestroyWindow (win);
	return 0;
}

static int run_2d (int frames, int sw)
{
	SDL_Window *win = SDL_CreateWindow ("sdl2test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 400, 300, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!win) {
		printf ("sdl2test : fenetre : %s\n", SDL_GetError ());
		return 1;
	}
	SDL_Renderer *r = SDL_CreateRenderer (win, -1, sw ? SDL_RENDERER_SOFTWARE : 0);
	if (!r) {
		printf ("sdl2test : rendu : %s\n", SDL_GetError ());
		return 1;
	}
	SDL_RendererInfo info;
	SDL_GetRendererInfo (r, &info);
	printf ("sdl2test : rendu %s, drapeaux 0x%x\n", info.name, info.flags);
	Uint64 t0 = SDL_GetPerformanceCounter (), f = SDL_GetPerformanceFrequency ();
	for (int n = 0; n < frames; n++) {
		SDL_Event e;
		SDL_Rect m = { n * 3 % 360, 240, 40, 40 };
		while (SDL_PollEvent (&e))
			;
		SDL_SetRenderDrawColor (r, 16, 32, 64, 255);
		SDL_RenderClear (r);
		SDL_SetRenderDrawColor (r, 0, 255, 255, 255);
		SDL_RenderFillRect (r, &m);
		SDL_RenderPresent (r);
	}
	printf ("sdl2test : 2D : %d images, %.1f images/s\n", frames, frames * (double) f / (SDL_GetPerformanceCounter () - t0));
	SDL_DestroyRenderer (r);
	SDL_DestroyWindow (win);
	return 0;
}

/* --pad N : N secondes, une fenetre qui a le clavier ; affiche chaque manette vue et chaque appui, bouton de
   manette et touche, comme les recoit un jeu (SuperTuxKart lit les deux) */
static int run_pad (int seconds)
{
	SDL_Window *win = SDL_CreateWindow ("sdl2test --pad", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 400, 300, SDL_WINDOW_SHOWN);
	if (!win) {
		printf ("sdl2test : fenetre : %s\n", SDL_GetError ());
		return 1;
	}
	SDL_Renderer *r = SDL_CreateRenderer (win, -1, SDL_RENDERER_SOFTWARE);
	printf ("sdl2test : %d s : appuyez sur les touches (fleches, A B X Y L R, Select, Start)\n", seconds);
	Uint32 end = SDL_GetTicks () + (Uint32) seconds * 1000;
	while (!SDL_TICKS_PASSED (SDL_GetTicks (), end)) {
		SDL_Event e;
		while (SDL_PollEvent (&e)) {
			if (e.type == SDL_QUIT)
				end = SDL_GetTicks ();
			if (e.type == SDL_CONTROLLERDEVICEADDED) {
				SDL_GameController *c = SDL_GameControllerOpen (e.cdevice.which);
				printf ("sdl2test : manette ajoutee : %s\n", c ? SDL_GameControllerName (c) : SDL_GetError ());
			}
			if (e.type == SDL_CONTROLLERBUTTONDOWN)
				printf ("sdl2test :   manette : %s\n", SDL_GameControllerGetStringForButton ((SDL_GameControllerButton) e.cbutton.button));
			if (e.type == SDL_KEYDOWN && !e.key.repeat)
				printf ("sdl2test :   clavier : %s (scancode %d)\n", SDL_GetScancodeName (e.key.keysym.scancode), e.key.keysym.scancode);
		}
		if (r) {
			SDL_SetRenderDrawColor (r, 0, 0, 64, 255);
			SDL_RenderClear (r);
			SDL_RenderPresent (r);
		}
		SDL_Delay (10);
	}
	if (r)
		SDL_DestroyRenderer (r);
	SDL_DestroyWindow (win);
	return 0;
}

int main (int argc, char **argv)
{
	int frames = 120, sw = 0, gl = 0, rc, pad = 0;

	for (int i = 1; i < argc; i++) {
		if (!SDL_strcmp (argv[i], "--sw")) sw = 1;
		if (!SDL_strcmp (argv[i], "--gl")) gl = 1;
		if (!SDL_strcmp (argv[i], "--nodepth")) nodepth = 1;
		if (!SDL_strcmp (argv[i], "--noread")) noread = 1;
		if (!SDL_strcmp (argv[i], "--discard")) discard_z = 1;
		if (!SDL_strcmp (argv[i], "--split")) split = 1;
		if (!SDL_strcmp (argv[i], "--split-store")) split_store = 1;
		if (!SDL_strcmp (argv[i], "--fbo") && i + 1 < argc) fbo = argv[++i];
		if (!SDL_strcmp (argv[i], "--fbosize") && i + 2 < argc) { fbo_w = SDL_atoi (argv[++i]); fbo_h = SDL_atoi (argv[++i]); }
		if (!SDL_strcmp (argv[i], "--keep") && i + 1 < argc) keep = argv[++i];
		if (!SDL_strcmp (argv[i], "--tri") && i + 1 < argc) ntri = SDL_atoi (argv[++i]);
		if (!SDL_strcmp (argv[i], "--dry")) SDL_setenv ("SDL_ARMPI_GL", "dry", 1);
		if (!SDL_strcmp (argv[i], "--frames") && i + 1 < argc) frames = SDL_atoi (argv[++i]);
		if (!SDL_strcmp (argv[i], "--pad") && i + 1 < argc) { pad = SDL_atoi (argv[++i]); }
	}
	if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
		printf ("sdl2test : SDL_Init : %s\n", SDL_GetError ());
		return 1;
	}
	SDL_version v;
	SDL_GetVersion (&v);
	printf ("sdl2test : SDL %d.%d.%d, %s, video %s, %d manettes\n", v.major, v.minor, v.patch, SDL_GetPlatform (), SDL_GetCurrentVideoDriver (), SDL_NumJoysticks ());
	SDL_DisplayMode dm;
	if (SDL_GetCurrentDisplayMode (0, &dm) == 0)
		printf ("sdl2test : ecran %d x %d (taille de la surface GL d'une fenetre)\n", dm.w, dm.h);
	rc = pad ? run_pad (pad) : gl ? run_gl (frames, SDL_getenv ("SDL_ARMPI_GL")) : run_2d (frames, sw);
	SDL_Quit ();
	printf ("sdl2test : fin %d\n", rc);
	return rc;
}
