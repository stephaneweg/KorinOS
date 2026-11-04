# GpuKit: the GPU

**GpuKit** (`SD0:/lib/gpukit.so`) gives programs the Raspberry Pi 4's V3D 4.2 GPU: rectangle fills and copies, textures, and triangle rendering (colors, textures, depth, blending). The `v3d` service owns the GPU and runs the jobs one after another; GpuKit prepares everything else in the application's process.

**There is not always a GPU**: under QEMU, with the `no_gpu` option, or after a hang (the service switches it off until the next restart). GpuKit is built for that:

| Functions | Without a GPU |
|---|---|
| `gk_fill`, `gk_copy`, `gk_draw` | the processor does the work; the result tells who did it (1 GPU, 0 processor) |
| `gk_texture`, `gk_texture_rect`, `gk_render` | `GK_E_NOGPU` (-1): up to the caller to do otherwise (often: `gk_draw`) |
| `gk_demo_triangle` | 0: nothing is drawn |

At a higher level, two other paths lead to the same GPU: **Viewport3D** for .NET applications, and **OpenGL ES 3.0 through Mesa** (see [below](#opengl-es-and-viewport3d)).

## Opening the kit

```c
#include <gpukit_abi.h>

if (!gpukit_kit_open ()) return 1;     // SD0:/lib/gpukit.so missing
int gpu = gk_info (0) == 1;            // 1: the GPU works; 0: everything will go to the processor
```

Nothing to add when linking: `pibuild` takes every library of `/gcc/lib`.

## Knowing whether there is a GPU

| Function | Arguments | Result |
|---|---|---|
| `gk_info` | buffer of `GKI_SIZE` (64) bytes, or 0 | 1: GPU; 0: processor |

The buffer reads as an array of `long` (index = offset / 8):

| Offset | Content |
|---|---|
| `GKI_GPU` | 1: the GPU works; 0: the processor |
| `GKI_STATE` | service state: 1 ready, 0 missing (QEMU, `no_gpu`), -1 switched off after a hang |
| `GKI_VERSION` | 42: V3D 4.2 |
| `GKI_REV` | GPU revision |
| `GKI_JOBS` | jobs done by the GPU since startup (whole system) |
| `GKI_BUSY_US` | total GPU time (µs) |
| `GKI_LAST_US` | duration of this program's last job (µs) |

`gk_info` tells how things are **now**. The GPU can stop along the way: a render then returns `GK_E_LOST` (-3), and everything goes to the processor until the next restart. A well-made program checks the result of each call rather than trusting a single `gk_info` at the start.

## Memory and rectangles

The GPU has no MMU: it only writes into **shared buffers in the first GB**. `gk_alloc` gives one. A target elsewhere (a window's pixels, a `malloc`) works too: GpuKit then goes through a staging buffer of its own, at the cost of a copy.

| Function | Arguments | Result |
|---|---|---|
| `gk_alloc` | bytes (64 MB at most) | address of a buffer the GPU reaches, zeroed; 0: no memory |
| `gk_free` | address returned by `gk_alloc` | 0 |
| `gk_fill` | pixels, pitch (bytes), x, y, width, height, color `0x00RRGGBB` | 1 GPU, 0 processor |
| `gk_copy` | source, pitch, destination, pitch, width, height | 1 GPU, 0 processor |
| `gk_demo_triangle` | pixels, pitch, width, height | 1: a red, green, blue shaded triangle on a midnight blue background; 0: no GPU |

A job is 2048 × 2048 pixels at most; beyond that, the processor takes over. Pixels are `0x00RRGGBB`: the high byte is 0 after the GPU.

## Textures and rendering

`gk_render` draws **triangles** into a target, in batches: each batch has its texture, its state (depth, faces, blending, filtering) and its 4 × 4 matrix. These are the structures and codes of Onyx's `gpu_render`.

| Function | Arguments | Result |
|---|---|---|
| `gk_texture` | texture (< 0: new), pixels `0xAARRGGBB` (0 with an existing texture: frees it), width, height (1..2048), pitch (**pixels**) | texture number (>= 0), 0 when freed, `GK_E_*` |
| `gk_texture_rect` | texture, x, y, width, height, pixels, pitch (pixels) | 0: that rectangle replaced, `GK_E_*` |
| `gk_texture_info` | texture, 48-byte output | 0: physical address, bytes, layout, width, height, address; to render into the texture with Mesa |
| `gk_render` | target, vertices, count, batches, count | 0: the pixels are in the target on return; `GK_E_*` |
| `gk_draw` | target, vertices, count, batches, count, texture table (or 0) | 1 GPU, 0 processor, `GK_E_*` |
| `gk_debug` | `GK_DBG_*`, argument, argument | 0, `GK_E_*`; for testing |

Errors: `GK_E_NOGPU` (-1) no GPU, `GK_E_INVAL` (-2) bad arguments or a vertex outside the guard band, `GK_E_LOST` (-3) the GPU stopped (textures lost), `GK_E_NOMEM` (-4) no more memory or free texture. Limits: 512 textures per program, 1024 batches and 196,608 vertices per render.

**`gk_render` or `gk_draw`?** `gk_render` is the GPU only: the vertices, once multiplied by the matrix, must stay inside the guard band (w > 0, |x| and |y| at most 4 w, z at least -w), otherwise the picture is refused. `gk_draw` does the rest on the processor: it transforms, rejects what is outside the picture, **clips** against the near plane, then hands the picture to `gk_render`; when there is no GPU, or a texture is not on it, it draws with its **rasterizer** (blend modes 0 to 4 only). For a program that simply wants 3D, `gk_draw` is enough.

### The structures

They are not in the header: declare them yourself, and check their size against the kit's constants.

```c
struct gk_vertex { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };	// GKV_SIZE = 32
struct gk_batch { unsigned first, count; int tex; unsigned flags; float m[16]; };	// GKB_SIZE = 80
struct gk_frame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };	// GKF_SIZE = 32
struct gk_tex { unsigned *px; int w, h, id, pad; };				// GKT_SIZE = 24
```

| Field | Meaning |
|---|---|
| vertex `x y z w` | position, multiplied by the batch's matrix (clip space, as in OpenGL) |
| vertex `s t` | texture coordinates, 0..1 |
| vertex `rgba` | color, multiplied by the texel (bytes r, g, b, a) |
| vertex `rgba2` | added color (0: none) |
| batch `first`, `count` | the batch's vertices: `count` / 3 triangles |
| batch `tex` | `gk_render`: number from `gk_texture`, -1: none; `gk_draw`: index in its `gk_tex` table, -1: none |
| batch `flags` | `GK_B_*` below |
| batch `m` | 4 × 4 matrix, row by row: position = m × (x y z w) |
| target `px`, `w`, `h` | `0x00RRGGBB` pixels, size |
| target `stride` | pitch in **pixels** |
| target `clear` | background color, `0xRRGGBB` |
| target `flags` | `GK_F_KEEP`: draw over the pixels (otherwise the background is cleared); `GK_F_ALPHA`: premultiplied `0xAARRGGBB` target (GPU only) |
| table `px`, `w`, `h` | the texture's pixels (for the processor) |
| table `id` | its `gk_texture` number (for the GPU), < 0: not on the GPU, the picture goes to the processor |

Batch flags (`flags`):

| Flag | Effect |
|---|---|
| `GK_Z_LESS` (0), `GK_Z_EQUAL`, `GK_Z_LEQUAL`, `GK_Z_GREATER`, `GK_Z_NOTEQUAL`, `GK_Z_GEQUAL`, `GK_Z_ALWAYS` (7) | depth test (bits 0 to 2, mask `GK_B_ZFUNC`) |
| `GK_B_NOZWRITE` | do not write the depth |
| `GK_B_CULL_BACK`, `GK_B_CULL_FRONT` | do not draw back, front faces |
| mode << `GK_B_BLEND_SHIFT` | blending: `GK_BLEND_NONE` 0, `ALPHA` 1, `ADD` 2, `MUL` 3, `PREMUL` 4, and for the GPU only `MULCOL` 5 to `DSTOUT` 12 |
| `GK_B_LINEAR` | bilinear filtering (otherwise nearest) |
| mode << `GK_B_WRAP_S_SHIFT`, << `GK_B_WRAP_T_SHIFT` | `GK_WRAP_REPEAT` 0, `GK_WRAP_CLAMP` 1, `GK_WRAP_MIRROR` 2 |
| `GK_B_NOMATRIX` | identity matrix: vertices already in clip space (`gk_render`) |
| `GK_B_ALPHATEST` + threshold << `GK_B_AT_SHIFT` | a pixel whose alpha is below the threshold (0..255) is not drawn |

### Testing without a GPU

`gk_debug (GK_DBG_DRY, 1, 0)` makes `gk_render` build its command lists without sending anything to the GPU: under QEMU, it checks that a picture would be accepted (structures, guard band, textures). `GK_DBG_LISTS` (output of `GKD_SIZE` bytes) returns the lists of the last job, `GK_DBG_TEX` (texture, output) a texture's texels. These are debugging tools, not rendering.

## Example: what the GPU gives

In text mode: the GPU's state, then a fill, a copy, the demo triangle and a texture, each one telling who did it. Under QEMU, everything goes through the processor and textures return `GK_E_NOGPU`.

```c
#include <stdio.h>
#include <gpukit_abi.h>

int main (void)
{
	if (!gpukit_kit_open ()) { printf ("GpuKit missing\n"); return 1; }

	long info[GKI_SIZE / 8];
	long gpu = gk_info ((long) info);
	long state = info[GKI_STATE / 8];
	printf ("rendering: %s; v3d service: %s\n", gpu ? "GPU" : "processor",
		state > 0 ? "ready" : state < 0 ? "switched off after a hang" : "missing");
	if (gpu)
		printf ("V3D %ld.%ld, revision %ld, %ld jobs, last: %ld µs\n", info[GKI_VERSION / 8] / 10,
			info[GKI_VERSION / 8] % 10, info[GKI_REV / 8], info[GKI_JOBS / 8], info[GKI_LAST_US / 8]);

	enum { W = 320, H = 200 };
	unsigned *a = (unsigned *) gk_alloc (W * H * 4 * 2);	// two pictures in the first GB
	if (!a) { printf ("no memory\n"); return 1; }
	unsigned *b = a + W * H;
	long r1 = gk_fill ((long) a, W * 4, 0, 0, W, H, 0x203040);		// pitch in bytes
	long r2 = gk_fill ((long) a, W * 4, 40, 30, 100, 60, 0xE0A020);
	long r3 = gk_copy ((long) a, W * 4, (long) b, W * 4, W, H);
	printf ("fills: %s, %s; copy: %s\n", r1 ? "GPU" : "processor", r2 ? "GPU" : "processor", r3 ? "GPU" : "processor");
	printf ("copy: pixel (50, 40) = %06x, pixel (0, 0) = %06x\n", b[40 * W + 50] & 0xFFFFFF, b[0] & 0xFFFFFF);

	if (gk_demo_triangle ((long) b, W * 4, W, H))
		printf ("triangle: center = %06x\n", b[(H / 2) * W + W / 2] & 0xFFFFFF);
	else
		printf ("triangle: no GPU, nothing drawn\n");

	long t = gk_texture (-1, (long) a, 64, 64, W);		// pitch in pixels
	if (t == GK_E_NOGPU) printf ("texture: no GPU (to be done by the processor, or by gk_draw)\n");
	else if (t < 0) printf ("texture: error %ld\n", t);
	else { printf ("texture %ld created\n", t); gk_texture (t, 0, 0, 0, 0); }	// pixels 0: freed

	gk_free ((long) a);
	return 0;
}
```

## Example: a spinning cube

A resizable window: `gk_draw` draws straight into the window's pixels, through the GPU when it is there, otherwise through the processor. The background takes the theme's color; the top band tells who is drawing.

```c
#include <math.h>
#include <string.h>
#include <time.h>
#include <uikit.h>
#include <gpukit_abi.h>

struct gk_vertex { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };
struct gk_batch { unsigned first, count; int tex; unsigned flags; float m[16]; };
struct gk_frame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };
_Static_assert (sizeof (struct gk_vertex) == GKV_SIZE && sizeof (struct gk_batch) == GKB_SIZE
		&& sizeof (struct gk_frame) == GKF_SIZE, "gpukit.inc");

enum { TOP = 40 };
static struct ui_vars ui;
static struct gk_vertex cube[36];
static int by_gpu = -1;

static unsigned tok (int off) { return *(const unsigned *) ((const char *) ui_jetons () + off) & 0xFFFFFF; }

// six faces of two triangles, one color per face
static void build_cube (void)
{
	static const float c[8][3] = { {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}, {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} };
	static const int face[6][4] = { {4,5,6,7}, {1,0,3,2}, {5,1,2,6}, {0,4,7,3}, {7,6,2,3}, {0,1,5,4} };
	static const unsigned col[6] = { 0xE05050, 0x50C050, 0x5080E0, 0xE0C040, 0xC060D0, 0x40C0C0 };
	static const int corner[6] = { 0, 1, 2, 0, 2, 3 };
	int n = 0;
	for (int i = 0; i < 6; i++)
		for (int k = 0; k < 6; k++)
		{
			const float *p = c[face[i][corner[k]]];
			struct gk_vertex *q = &cube[n++];
			memset (q, 0, sizeof *q);
			q->x = p[0]; q->y = p[1]; q->z = p[2]; q->w = 1;
			q->rgba[0] = col[i] >> 16; q->rgba[1] = col[i] >> 8 & 255; q->rgba[2] = col[i] & 255; q->rgba[3] = 255;
		}
}

// m = a × b (4 × 4 matrices, row by row)
static void mul (float *m, const float *a, const float *b)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
		{
			float s = 0;
			for (int k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c];
			m[r * 4 + c] = s;
		}
}

// projection × step back × rotations
static void matrix (float *m, float t, float aspect)
{
	float cy = cosf (t), sy = sinf (t), cx = cosf (t * 0.7f), sx = sinf (t * 0.7f);
	float ry[16] = { cy, 0, sy, 0,  0, 1, 0, 0,  -sy, 0, cy, 0,  0, 0, 0, 1 };
	float rx[16] = { 1, 0, 0, 0,  0, cx, -sx, 0,  0, sx, cx, 0,  0, 0, 0, 1 };
	float back[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, -5,  0, 0, 0, 1 };
	float n = 1, f = 20, k = 1 / tanf (0.5f);
	float proj[16] = { k / aspect, 0, 0, 0,  0, k, 0, 0,  0, 0, (f + n) / (n - f), 2 * f * n / (n - f),  0, 0, -1, 0 };
	float a[16], b[16];
	mul (a, ry, rx);
	mul (b, back, a);
	mul (m, proj, b);
}

static void header (void)
{
	ui_fill (0, 0, ui.w, TOP, tok (TJ_FACE));
	const char *who = by_gpu < 0 ? "..." : by_gpu ? "GPU" : (const char *) ui_tr ((long) "Processeur");
	ui_text ((long) who, 12, 26, tok (TJ_TEXTE2), 13, (long) ui.font, 0);
	ui_update (0, 0, ui.w, TOP);
}

static long on_resize (long w, long h)
{
	ui_fill (0, 0, ui.w, ui.h, tok (TJ_FACE));
	header ();
	ui_update_all ();
	return 0;
}

static void draw (float t)
{
	long x = 8, y = TOP, w = ui.w - 16, h = ui.h - TOP - 8;
	if (w < 16 || h < 16) return;
	struct gk_batch b = { 0, 36, -1, GK_Z_LESS, { 0 } };
	matrix (b.m, t, (float) w / h);
	struct gk_frame f = { ui.surf + y * ui.w + x, (int) w, (int) h, (int) ui.w, tok (TJ_SURFACE), 0, 0 };
	long r = gk_draw ((long) &f, (long) cube, 36, (long) &b, 1, 0);
	if (r < 0) return;
	if (r != by_gpu) { by_gpu = (int) r; header (); }
	ui_update (x, y, w, h);
}

static double now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main (void)
{
	if (!uikit_open (&ui) || !gpukit_kit_open ()) return 1;
	build_cube ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	if (ui_window_ex ((scr[0] - 480) / 2, scr[3] + 40, 480, 400, (long) ui_tr ((long) "Cube"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (200, 160);
	ui_on_resize ((long) on_resize, 0);
	on_resize (ui.w, ui.h);

	double t0 = now ();
	for (;;)
	{
		long ev = ui_event (15);
		if (ev == WEV_CLOSE) break;
		draw ((float) (now () - t0));
	}
	return 0;
}
```

The texts given to `ui_tr` are the French keys of the system dictionary (an app's `lang/en.txt` gives their English version), as in the [skeleton](skeleton_en.md).

## Example: a texture, with or without a GPU

`gk_render` only works with the GPU: the program tries it, then, on `GK_E_NOGPU` or `GK_E_LOST`, draws the same picture through `gk_draw`, handing it the texture's pixels. The picture is written to `SD0:/home/quad.ppm`.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gpukit_abi.h>

struct gk_vertex { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };
struct gk_batch { unsigned first, count; int tex; unsigned flags; float m[16]; };
struct gk_frame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };
struct gk_tex { unsigned *px; int w, h, id, pad; };
_Static_assert (sizeof (struct gk_tex) == GKT_SIZE && sizeof (struct gk_frame) == GKF_SIZE, "gpukit.inc");

enum { W = 256, H = 256, T = 64 };

// a square of two triangles, the whole texture, white color (the texel as is)
static void quad (struct gk_vertex *v)
{
	static const float p[6][4] = { {-0.8f,-0.8f,0,0}, {0.8f,-0.8f,1,0}, {0.8f,0.8f,1,1}, {-0.8f,-0.8f,0,0}, {0.8f,0.8f,1,1}, {-0.8f,0.8f,0,1} };
	memset (v, 0, 6 * sizeof *v);
	for (int i = 0; i < 6; i++)
	{
		v[i].x = p[i][0]; v[i].y = p[i][1]; v[i].w = 1;
		v[i].s = p[i][2]; v[i].t = p[i][3];
		memset (v[i].rgba, 255, 4);
	}
}

int main (void)
{
	if (!gpukit_kit_open ()) return 1;
	unsigned *px = (unsigned *) gk_alloc (W * H * 4);	// target in the first GB: no copy
	unsigned *tex = malloc (T * T * 4);
	if (!px || !tex) return 1;
	for (int y = 0; y < T; y++)
		for (int x = 0; x < T; x++) tex[y * T + x] = (x ^ y) & 8 ? 0xFFF0F0F0 : 0xFF3060C0;

	struct gk_vertex v[6];
	quad (v);
	struct gk_batch b = { 0, 6, -1, GK_Z_ALWAYS | GK_B_LINEAR, { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 } };
	struct gk_frame f = { px, W, H, W, 0x202020, 0, 0 };

	const char *how;
	long id = gk_texture (-1, (long) tex, T, T, T);
	long r = id;						// GK_E_NOGPU without a GPU
	if (id >= 0)
	{
		b.tex = (int) id;
		r = gk_render ((long) &f, (long) v, 6, (long) &b, 1);
	}
	if (r == 0) how = "GPU, through gk_render";
	else
	{
		// no GPU (GK_E_NOGPU), or it stopped (GK_E_LOST): gk_draw, which manages without
		struct gk_tex table = { tex, T, T, -1, 0 };	// id < 0: the texture is not on the GPU
		b.tex = 0;					// index in the table
		r = gk_draw ((long) &f, (long) v, 6, (long) &b, 1, (long) &table);
		how = r == 1 ? "GPU, through gk_draw" : r == 0 ? "processor, through gk_draw" : "error";
	}
	printf ("rendering: %s (%ld)\n", how, r);

	FILE *o = fopen ("SD0:/home/quad.ppm", "wb");
	if (o)
	{
		fprintf (o, "P6\n%d %d\n255\n", W, H);
		for (int i = 0; i < W * H; i++) { unsigned char rgb[3] = { px[i] >> 16, px[i] >> 8, px[i] }; fwrite (rgb, 1, 3, o); }
		fclose (o);
	}
	if (id >= 0) gk_texture (id, 0, 0, 0, 0);
	free (tex);
	gk_free ((long) px);
	return 0;
}
```

## OpenGL ES and Viewport3D

GpuKit has fixed shaders (texel × color + color 2). Two other layers go further, on the same `v3d` service:

- **OpenGL ES 3.0 through Mesa** (Gallium's `v3d` driver, ported to Korin OS): shaders in GLSL ES, several textures per draw. It is not a kit: Mesa and its small `armgl` front end are linked statically into the program (about fifteen MB), with the scripts of `tools/mesa` on the PC. Jet (WebGL), Skia, the N64, GameCube and 3DS emulators use it. Without a GPU, `armgl_init` returns 0: the program must then have its own rendering, `gk_draw` for instance. `gk_texture_info` gives the physical address and layout of a GpuKit texture so that Mesa can render into it.
- **Viewport3D** for .NET applications (`System.3D.dll`, namespace `System.Windows.Media.Media3D`, WPF's names): a control to put in a `Form`, a camera (`PerspectiveCamera`, `OrthographicCamera`), models (`GeometryModel3D`, `MeshBuilder.Box`, `Sphere`, `Plane`), materials and lights (`AmbientLight`, `DirectionalLight`, `PointLight`), with lighting computed per vertex. Rendering goes through `gk_render`, and through the processor without a GPU. One 3D view shown per window, nothing on top of it, 65,536 triangles per picture.

## Pitfalls

- **Pitches**: in **bytes** for `gk_fill` and `gk_copy` (as in MediaKit); in **pixels** for `gk_texture`, `gk_texture_rect` and a target's `stride`.
- **QEMU has no GPU**: everything that works through the processor works there, but `gk_texture` and `gk_render` return `GK_E_NOGPU`. Write the GPU-less path from the start; test the command lists with `GK_DBG_DRY`.
- **`GK_E_LOST`**: the GPU stopped and will not come back before a restart; textures are lost. Switch to the processor for good (as `gk_draw` does), do not retry on every picture.
- **The guard band** of `gk_render`: an extreme vertex gets the picture refused (`GK_E_INVAL`). Wild coordinates can even hang the GPU. Let `gk_draw` clip, unless your vertices stay well inside the picture.
- **No copy**: a target in a `gk_alloc` buffer is written directly; a window's pixels or a `malloc` go through a staging buffer (one copy before with `GK_F_KEEP`, one after).
- **Depth does not survive** from one `gk_render` to the next: a picture is drawn in one call.
- **`gk_draw` without a GPU** only does blend modes 0 to 4 and ignores `GK_F_ALPHA`.
- **Pixels** are `0x00RRGGBB` in the target, `0xAARRGGBB` in textures.

## See also

- [UIKit](uikit_en.md): the windows you draw into.
- [MediaKit](mediakit_en.md): the pictures of a video.
- [EmuKit](emukit_en.md): the emulators, which render through GpuKit or Mesa.
- [Kits](kit_en.md): how a kit is loaded and called from C.
