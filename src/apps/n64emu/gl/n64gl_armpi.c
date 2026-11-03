/*
 * n64gl_armpi.c -- n64emu relie a Mesa : ce main, libposix et Mesa d'un cote, l'emulateur en assembleur tel quel
 * de l'autre, a son adresse fixe (tools/emu/build-n64.sh). Il lui passe la table de n64gl.h ; si Mesa ne demarre
 * pas, init rend 0 et l'emulateur retombe sur GpuKit, puis sur son rendu logiciel, comme l'assembleur seul.
 *     n64emu [--gldry] [--glstat] [--nogl] [--nosave] [--gllose image] ...      le reste comme l'emulateur
 *     (--gllose : essai du repli, le GPU est declare perdu a cette image)
 *     n64emu [--gldry] [--nogl] --bench images rom [pas sortie]   sans fenetre : temps par image, une image .ppm tous les <pas>
 *            (--bench n'ecrit jamais de .sav)
 * Les programmes vus pour une ROM vont dans KEYDIR, jamais a cote d'elle : elle peut etre sur un volume a l'utilisateur.
 * Le thread de la machine (EmuKit) a le meme bloc TLS que celui-ci : le contexte courant lui sert sans rien
 * refaire, et rien d'autre n'appelle Mesa.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "armgl.h"
#include "n64gl.h"

#define FRAME_BYTES	(640 * 576 * 4)		/* N64_FBW * N64_FBH * 4 : ce qu'EmuKit attend d'une image */
#define STAT_EVERY	300
#define KEYDIR		"SD0:/home/n64gl"

extern void n64_asm_start (const char *args, const struct n64gl_api *api);

static void *surface, *images[3];
static int dry, stat_on, nogl;
static long lose_at;

static double now_ms (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static long gl_init (void **bufs, long w, long h, long pitch)
{
	double t0 = now_ms ();
	if (nogl)
		return 0;
	int mode = armgl_init (dry ? ARMGL_F_DRY_BUF : 0);
	if (mode == ARMGL_NONE)
		return 0;
	void *ctx = armgl_context (3, 0, NULL);
	if (!ctx)
		return 0;
	surface = armgl_buffers (images, 3, FRAME_BYTES, w, h, pitch);
	if (!surface || !armgl_make_current (ctx, surface) || !n64gl_start (armgl_proc, w, h))
		return 0;
	for (int i = 0; i < 3; i++)
		bufs[i] = images[i];
	printf ("n64gl : pret en %.0f ms (%s)\n", now_ms () - t0, mode == ARMGL_DRY ? "a blanc" : "GPU");
	return 1;
}

static long gl_frame (const struct n64gl_frame *f)
{
	static double sum_draw, sum_gpu;
	static int frames;
	int i = f->px == images[1] ? 1 : f->px == images[2] ? 2 : 0;
	double t0 = now_ms ();
	armgl_buffer_bind (surface, i);
	n64gl_render (f);
	double t1 = now_ms ();
	armgl_buffer_done (surface);
	double t2 = now_ms ();
	static long seen;
	if (armgl_lost () || (lose_at && ++seen >= lose_at)) {
		printf ("n64gl : GPU perdu, rendu logiciel\n");
		return 0;
	}
	sum_draw += t1 - t0;
	sum_gpu += t2 - t1;
	if (stat_on && ++frames == STAT_EVERY) {
		int programs, compiled, skipped;
		n64gl_stats (&programs, &compiled, &skipped);
		printf ("n64gl : %d images, %.2f ms de commandes, %.2f ms de GPU, %d programmes, %d compiles, %d lots sans programme\n",
			frames, sum_draw / frames, sum_gpu / frames, programs, compiled, skipped);
		sum_draw = sum_gpu = 0;
		frames = 0;
	}
	return 1;
}

static long bench_frames, bench_step;
static const char *bench_rom, *bench_out;

static long bench_tick (long frame, const uint32_t *px)
{
	static double t_last, sum, worst;
	double t = now_ms ();
	if (frame) {
		sum += t - t_last;
		if (t - t_last > worst)
			worst = t - t_last;
	}
	if (frame && frame % STAT_EVERY == 0) {
		printf ("bench : image %ld, %.2f ms par image, pire %.1f ms\n", frame, sum / STAT_EVERY, worst);
		sum = worst = 0;
	}
	if (bench_step && frame && frame % bench_step == 0 && bench_out) {
		static char name[512];
		static unsigned char line[640 * 3];
		snprintf (name, sizeof name, "%s/f%05ld.ppm", bench_out, frame);
		FILE *f = fopen (name, "wb");
		if (f) {
			fprintf (f, "P6\n640 480\n255\n");
			for (int y = 0; y < 480; y++) {
				for (int x = 0; x < 640; x++) {
					line[x * 3] = px[y * 640 + x] >> 16;
					line[x * 3 + 1] = px[y * 640 + x] >> 8;
					line[x * 3 + 2] = px[y * 640 + x];
				}
				fwrite (line, 3, 640, f);
			}
			fclose (f);
		}
	}
	t_last = now_ms ();
	if (frame + 1 >= bench_frames)
		printf ("bench : %ld images : PASS\n", frame + 1);
	return frame + 1 < bench_frames;
}

static void gl_load (const char *rom)
{
	static char path[512];
	if (bench_rom)
		rom = bench_rom;
	const char *name = strrchr (rom, '/');
	mkdir (KEYDIR, 0755);
	snprintf (path, sizeof path, KEYDIR "/%s.glk", name ? name + 1 : rom);
	armgl_buffer_bind (surface, 0);
	double t0 = now_ms ();
	int n = n64gl_keys_load (path);
	armgl_finish ();
	if (n)
		printf ("n64gl : %d programmes en %.0f ms\n", n, now_ms () - t0);
}

static void gl_command (long n)
{
	n64gl_filter (n);
}

static struct n64gl_api api = { gl_init, gl_frame, gl_load, gl_command, 0, NULL, 0, bench_tick };

static void bench_load (void)
{
	FILE *f = fopen (bench_rom, "rb");
	if (!f) {
		printf ("bench : ROM illisible\n");
		exit (1);
	}
	fseek (f, 0, SEEK_END);
	api.romsize = ftell (f);
	fseek (f, 0, SEEK_SET);
	void *p = malloc (api.romsize);
	api.romsize = fread (p, 1, api.romsize, f);
	fclose (f);
	api.rom = p;
	api.nosave = 1;
	stat_on = 1;
}

int main (int argc, char **argv)
{
	static char args[1024];
	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--gldry"))
			dry = 1;
		else if (!strcmp (argv[i], "--glstat"))
			stat_on = 1;
		else if (!strcmp (argv[i], "--nogl"))
			nogl = 1;
		else if (!strcmp (argv[i], "--nosave"))
			api.nosave = 1;
		else if (!strcmp (argv[i], "--gllose") && i + 1 < argc)
			lose_at = atol (argv[++i]);
		else if (!strcmp (argv[i], "--bench") && i + 2 < argc) {
			bench_frames = atol (argv[i + 1]);
			bench_rom = argv[i + 2];
			if (i + 4 < argc) {
				bench_step = atol (argv[i + 3]);
				bench_out = argv[i + 4];
			}
			bench_load ();
			break;
		}
		else {
			if (args[0])
				strncat (args, " ", sizeof args - strlen (args) - 1);
			strncat (args, argv[i], sizeof args - strlen (args) - 1);
		}
	}
	n64_asm_start (args, &api);
	return 0;
}
