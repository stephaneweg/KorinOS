/* GpuKit du PC (Korin x86-64, docs/etude/PC-PILOTES.md « GPU Intel, étape 3 ») : la même table que
 * le Pi (gpukit.abi), écrite en C. Le Pi garde son GpuKit en assembleur sur le V3D (gpukit.asm,
 * draw.asm, render.asm) : ce fichier n'est construit que pour x86-64 (kit.conf : arch = x86_64).
 *
 * Deux côtés :
 * - le processeur, toujours là : gk_fill, gk_copy, et gk_draw par le rastériseur de gk_draw du Pi
 *   sans GPU (celui du BASIC 3D d'Onyx, bas3d.h : découpe par le plan proche, couleurs et texture
 *   corrigées de la perspective, Z, faces, mélanges 0 à 4, test d'alpha) ;
 * - le GPU, par GlKit (Mesa iris) : un kit ne peut pas porter Mesa (il lui faut la libc du
 *   programme), c'est donc le programme qui le donne. Un programme C ou C++ lié à
 *   libgpukit_gl.a appelle gk_gl_attach () : sa table de fonctions arrive ici par
 *   gk_debug (GK_DBG_BACKEND, table, 0), et gk_info, gk_texture, gk_texture_rect, gk_render,
 *   gk_draw passent par elle. Sans elle (programme en assembleur, pas de GPU Intel, QEMU) :
 *   gk_info rend 0, gk_texture et gk_render GK_E_NOGPU, gk_draw dessine au processeur,
 *   comme le Pi sans GPU.
 * Variables par process (données du kit) ; aucun pointeur dans les données initialisées. */
#include "../common/korin_kit.h"

/* gpukit.inc */
#define GKI_GPU 0
#define GKI_STATE 8
#define GKI_VERSION 16
#define GKI_SIZE 64
#define GK_E_NOGPU (-1)
#define GK_E_INVAL (-2)
#define GK_E_LOST (-3)
#define GK_E_NOMEM (-4)
#define GK_B_ZFUNC 7
#define GK_B_NOZWRITE (1 << 3)
#define GK_B_CULL_BACK (1 << 4)
#define GK_B_CULL_FRONT (1 << 5)
#define GK_B_BLEND_SHIFT 8
#define GK_B_LINEAR (1 << 12)
#define GK_B_WRAP_S_SHIFT 13
#define GK_B_WRAP_T_SHIFT 15
#define GK_B_NOMATRIX (1 << 17)
#define GK_B_ALPHATEST (1 << 18)
#define GK_B_AT_SHIFT 19
#define GK_F_KEEP 1
#define GK_DBG_DRY 1
#define GK_DBG_BACKEND 4
#define GK_MAX_W 2048
#define GK_MAX_H 2048
#define GK_ALLOC_MAX (64UL << 20)

#define PROT_READ 1
#define PROT_WRITE 2

struct gk_vertex { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };
struct gk_batch { unsigned first, count; int tex; unsigned flags; float m[16]; };
struct gk_frame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };
struct gk_tex { unsigned *px; int w, h, id, pad; };
_Static_assert(sizeof(struct gk_vertex) == 32 && sizeof(struct gk_batch) == 80, "GKV_SIZE, GKB_SIZE");
_Static_assert(sizeof(struct gk_frame) == 32 && sizeof(struct gk_tex) == 24, "GKF_SIZE, GKT_SIZE");

/* la table que donne libgpukit_gl.a (src/kits/gpukit/gl/gpukit_gl.h, même disposition) */
#define GK_BACKEND_VERSION 1
struct gk_backend {
	long version;
	long (*texture)(long t, long px, long w, long h, long pitch);
	long (*texture_rect)(long t, long x, long y, long w, long h, long px, long pitch);
	long (*draw)(long frame, long v, long n, long b, long nb, long texs);
	long (*info)(long *out);
};

static const struct gk_backend *s_be;
static long s_dry;

/* ---- mémoire ---- */
#define ALLOCS 64
static unsigned long s_alloc_va[ALLOCS], s_alloc_len[ALLOCS];

long gk_alloc(unsigned long bytes)
{
	if (!bytes || bytes > GK_ALLOC_MAX)
		return 0;
	unsigned long n = (bytes + 4095) & ~4095UL;
	for (int i = 0; i < ALLOCS; i++)
		if (!s_alloc_va[i]) {
			long p = korin_sys4(SYS_VM_MAP, 0, n, PROT_READ | PROT_WRITE, 0);
			if (p <= 0)
				return 0;
			s_alloc_va[i] = (unsigned long)p;
			s_alloc_len[i] = n;
			return p;
		}
	return 0;
}

long gk_free(unsigned long va)
{
	for (int i = 0; i < ALLOCS; i++)
		if (va && s_alloc_va[i] == va) {
			korin_sys2(SYS_VM_UNMAP, va, s_alloc_len[i]);
			s_alloc_va[i] = 0;
		}
	return 0;
}

/* ---- état ---- */
long gk_info(long *out)
{
	long gpu = s_be != 0;
	if (out) {
		for (int i = 0; i < GKI_SIZE / 8; i++)
			out[i] = 0;
		if (s_be && s_be->info)
			s_be->info(out);
		out[GKI_GPU / 8] = gpu;
		out[GKI_STATE / 8] = gpu;
	}
	return gpu;
}

long gk_debug(long what, long a, long b)
{
	(void)b;
	if (what == GK_DBG_DRY) {
		s_dry = a;
		return 0;
	}
	if (what == GK_DBG_BACKEND) { /* a = table (0 : retirée) */
		const struct gk_backend *t = (const struct gk_backend *)a;
		if (t && t->version < GK_BACKEND_VERSION)
			return GK_E_INVAL;
		s_be = t;
		return 0;
	}
	return GK_E_NOGPU;
}

/* le GPU a refusé : le processeur désormais (comme GK_E_LOST du Pi) */
static long lost(long r)
{
	if (r == GK_E_LOST)
		s_be = 0;
	return r;
}

/* ---- rectangles ---- */
long gk_fill(unsigned *px, long pitch, long x, long y, long w, long h, unsigned long color)
{
	if (!px || w <= 0 || h <= 0)
		return 0;
	for (long j = 0; j < h; j++) {
		unsigned *d = (unsigned *)((char *)px + (y + j) * pitch) + x;
		for (long i = 0; i < w; i++)
			d[i] = (unsigned)color;
	}
	return 0;
}

long gk_copy(const unsigned char *src, long sp, unsigned char *dst, long dp, long w, long h)
{
	if (!src || !dst || w <= 0 || h <= 0)
		return 0;
	for (long j = 0; j < h; j++)
		memmove(dst + j * dp, src + j * sp, (size_t)w * 4);
	return 0;
}

/* ---- textures (le GPU seulement) ---- */
long gk_texture(long t, long px, long w, long h, long pitch)
{
	if (!s_be)
		return GK_E_NOGPU;
	return lost(s_be->texture((int)t, px, (int)w, (int)h, (int)pitch));
}

long gk_texture_rect(long t, long x, long y, long w, long h, long px, long pitch)
{
	if (!s_be)
		return GK_E_NOGPU;
	return lost(s_be->texture_rect((int)t, (int)x, (int)y, (int)w, (int)h, px, (int)pitch));
}

long gk_texture_info(long t, long out)
{
	(void)t;
	(void)out;
	return GK_E_NOGPU; /* (pas de texture partagée avec Mesa par une adresse physique sur le PC) */
}

/* ---- le rastériseur du processeur (bas3d.h d'Onyx, en C) ---- */
struct clipv { float x, y, z, w, s, t, r, g, b, a, r2, g2, b2, a2; };

static float *s_zb;
static unsigned long s_zb_len;

static float lerpf(float a, float b, float f) { return a + (b - a) * f; }

static int wrapi(int i, int n, int mode)
{
	if (mode == 1)
		return i < 0 ? 0 : i >= n ? n - 1 : i;
	if (mode == 2) {
		int p = 2 * n;
		i %= p;
		if (i < 0)
			i += p;
		return i < n ? i : p - 1 - i;
	}
	i %= n;
	return i < 0 ? i + n : i;
}

static void texel(const struct gk_tex *T, int x, int y, int ws, int wt, float k, float *o)
{
	unsigned c = T->px[(long)wrapi(y, T->h, wt) * T->w + wrapi(x, T->w, ws)];
	o[0] += k * (float)((c >> 16) & 255);
	o[1] += k * (float)((c >> 8) & 255);
	o[2] += k * (float)(c & 255);
	o[3] += k * (float)(c >> 24);
}

static int floori(float v)
{
	int i = (int)v;
	return (float)i > v ? i - 1 : i;
}

static void sample(const struct gk_tex *T, float s, float t, unsigned flags, float out[4])
{
	int ws = (int)(flags >> GK_B_WRAP_S_SHIFT) & 3, wt = (int)(flags >> GK_B_WRAP_T_SHIFT) & 3;
	out[0] = out[1] = out[2] = out[3] = 0;
	float u = s * (float)T->w, v = t * (float)T->h;
	if (!(flags & GK_B_LINEAR)) {
		texel(T, floori(u), floori(v), ws, wt, 1, out);
		return;
	}
	u -= 0.5f;
	v -= 0.5f;
	int x = floori(u), y = floori(v);
	float fx = u - (float)x, fy = v - (float)y;
	texel(T, x, y, ws, wt, (1 - fx) * (1 - fy), out);
	texel(T, x + 1, y, ws, wt, fx * (1 - fy), out);
	texel(T, x, y + 1, ws, wt, (1 - fx) * fy, out);
	texel(T, x + 1, y + 1, ws, wt, fx * fy, out);
}

static unsigned c8(float f) { return f <= 0 ? 0u : f >= 255 ? 255u : (unsigned)(f + 0.5f); }

static void raster(unsigned *dst, int W, int H, int stride, float *zb, const struct clipv *c, unsigned flags,
		   const struct gk_tex *T)
{
	float X[3], Y[3], Z[3], IW[3];
	for (int k = 0; k < 3; k++) {
		IW[k] = 1.0f / c[k].w;
		X[k] = (c[k].x * IW[k] + 1) * 0.5f * (float)W;
		Y[k] = (1 - c[k].y * IW[k]) * 0.5f * (float)H;
		Z[k] = c[k].z * IW[k] * 0.5f + 0.5f;
	}
	float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
	if (area == 0)
		return;
	int front = area < 0; /* sens inverse des aiguilles avec y vers le haut */
	if ((flags & GK_B_CULL_BACK) && !front)
		return;
	if ((flags & GK_B_CULL_FRONT) && front)
		return;
	int o[3] = {0, 1, 2};
	if (area < 0) {
		o[1] = 2;
		o[2] = 1;
		area = -area;
	}
	float minX = X[0], maxX = X[0], minY = Y[0], maxY = Y[0];
	for (int k = 1; k < 3; k++) {
		if (X[k] < minX) minX = X[k];
		if (X[k] > maxX) maxX = X[k];
		if (Y[k] < minY) minY = Y[k];
		if (Y[k] > maxY) maxY = Y[k];
	}
	if (maxX < 0 || maxY < 0 || minX >= (float)W || minY >= (float)H)
		return;
	int x0 = minX < 0 ? 0 : (int)minX, y0 = minY < 0 ? 0 : (int)minY;
	int x1 = maxX >= (float)W ? W - 1 : (int)maxX, y1 = maxY >= (float)H ? H - 1 : (int)maxY;
	float inv = 1.0f / area;
	unsigned zf = flags & GK_B_ZFUNC;
	if (zf == 0)
		zf = 1;
	int zw = !(flags & GK_B_NOZWRITE) && zf != 7;
	int blend = (int)(flags >> GK_B_BLEND_SHIFT) & 15;
	float A[3][10];
	for (int k = 0; k < 3; k++) {
		const struct clipv *v = &c[o[k]];
		float iw = IW[o[k]];
		A[k][0] = v->s * iw; A[k][1] = v->t * iw; A[k][2] = v->r * iw; A[k][3] = v->g * iw; A[k][4] = v->b * iw;
		A[k][5] = v->a * iw; A[k][6] = v->r2 * iw; A[k][7] = v->g2 * iw; A[k][8] = v->b2 * iw; A[k][9] = v->a2 * iw;
	}
	float PX[3] = {X[o[0]], X[o[1]], X[o[2]]}, PY[3] = {Y[o[0]], Y[o[1]], Y[o[2]]};
	float PZ[3] = {Z[o[0]], Z[o[1]], Z[o[2]]}, PW[3] = {IW[o[0]], IW[o[1]], IW[o[2]]};
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float px = (float)x + 0.5f, py = (float)y + 0.5f;
			float b0 = ((PX[1] - px) * (PY[2] - py) - (PX[2] - px) * (PY[1] - py)) * inv;
			float b1 = ((PX[2] - px) * (PY[0] - py) - (PX[0] - px) * (PY[2] - py)) * inv;
			float b2 = 1 - b0 - b1;
			if (b0 < 0 || b1 < 0 || b2 < 0)
				continue;
			float z = b0 * PZ[0] + b1 * PZ[1] + b2 * PZ[2];
			if (z < 0 || z > 1)
				continue;
			float *zr = &zb[(long)y * W + x];
			int pass;
			switch (zf) {
			case 2: pass = z == *zr; break;
			case 3: pass = z <= *zr; break;
			case 4: pass = z > *zr; break;
			case 5: pass = z != *zr; break;
			case 6: pass = z >= *zr; break;
			case 7: pass = 1; break;
			default: pass = z < *zr;
			}
			if (!pass)
				continue;
			float w = 1.0f / (b0 * PW[0] + b1 * PW[1] + b2 * PW[2]);
			float at[10];
			for (int k = 0; k < 10; k++)
				at[k] = (b0 * A[0][k] + b1 * A[1][k] + b2 * A[2][k]) * w;
			float r = at[2], g = at[3], bl = at[4], a = at[5];
			if (T) {
				float tx[4];
				sample(T, at[0], at[1], flags, tx);
				r *= tx[0] / 255;
				g *= tx[1] / 255;
				bl *= tx[2] / 255;
				a *= tx[3] / 255;
			}
			r += at[6]; g += at[7]; bl += at[8]; a += at[9];
			if (r > 255) r = 255;
			if (g > 255) g = 255;
			if (bl > 255) bl = 255;
			if (a > 255) a = 255;
			if ((flags & GK_B_ALPHATEST) && a < (float)((flags >> GK_B_AT_SHIFT) & 255))
				continue;
			unsigned *d = &dst[(long)y * stride + x];
			float dr = (float)((*d >> 16) & 255), dg = (float)((*d >> 8) & 255), db = (float)(*d & 255);
			float fa = a / 255;
			switch (blend) {
			case 1: r = r * fa + dr * (1 - fa); g = g * fa + dg * (1 - fa); bl = bl * fa + db * (1 - fa); break;
			case 2: r = r * fa + dr; g = g * fa + dg; bl = bl * fa + db; break;
			case 3: r = r * dr / 255; g = g * dg / 255; bl = bl * db / 255; break;
			case 4: r = r + dr * (1 - fa); g = g + dg * (1 - fa); bl = bl + db * (1 - fa); break;
			default: break;
			}
			*d = c8(r) << 16 | c8(g) << 8 | c8(bl);
			if (zw)
				*zr = z;
		}
}

static void cpu_batches(const struct gk_frame *F, const struct gk_vertex *v, long nv, const struct gk_batch *bt,
			long nb, const struct gk_tex *texs)
{
	static const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	for (long bi = 0; bi < nb; bi++) {
		const struct gk_batch *B = &bt[bi];
		const float *M = (B->flags & GK_B_NOMATRIX) ? I : B->m;
		const struct gk_tex *T = 0;
		if (B->tex >= 0 && texs && texs[B->tex].px && texs[B->tex].w > 0 && texs[B->tex].h > 0)
			T = &texs[B->tex];
		for (unsigned t = 0; t + 3 <= B->count && B->first + t + 3 <= (unsigned long)nv; t += 3) {
			struct clipv in[3];
			for (int k = 0; k < 3; k++) {
				const struct gk_vertex *p = &v[B->first + t + k];
				struct clipv *q = &in[k];
				q->x = M[0] * p->x + M[1] * p->y + M[2] * p->z + M[3] * p->w;
				q->y = M[4] * p->x + M[5] * p->y + M[6] * p->z + M[7] * p->w;
				q->z = M[8] * p->x + M[9] * p->y + M[10] * p->z + M[11] * p->w;
				q->w = M[12] * p->x + M[13] * p->y + M[14] * p->z + M[15] * p->w;
				q->s = p->s; q->t = p->t;
				q->r = p->rgba[0]; q->g = p->rgba[1]; q->b = p->rgba[2]; q->a = p->rgba[3];
				q->r2 = p->rgba2[0]; q->g2 = p->rgba2[1]; q->b2 = p->rgba2[2]; q->a2 = p->rgba2[3];
			}
			/* découpe par le plan proche (z >= -w, w > 0) : un polygone de 4 sommets au plus */
			struct clipv poly[4];
			int n = 0;
			const float EPS = 1e-5f;
			for (int k = 0; k < 3; k++) {
				const struct clipv *a = &in[k], *b = &in[(k + 1) % 3];
				float da = a->z + a->w, db = b->z + b->w;
				int ina = da >= 0 && a->w > EPS, inb = db >= 0 && b->w > EPS;
				if (ina)
					poly[n++] = *a;
				if (ina != inb && n < 4) {
					float f = da / (da - db);
					struct clipv *q = &poly[n++];
					q->x = lerpf(a->x, b->x, f); q->y = lerpf(a->y, b->y, f);
					q->z = lerpf(a->z, b->z, f); q->w = lerpf(a->w, b->w, f);
					q->s = lerpf(a->s, b->s, f); q->t = lerpf(a->t, b->t, f);
					q->r = lerpf(a->r, b->r, f); q->g = lerpf(a->g, b->g, f);
					q->b = lerpf(a->b, b->b, f); q->a = lerpf(a->a, b->a, f);
					q->r2 = lerpf(a->r2, b->r2, f); q->g2 = lerpf(a->g2, b->g2, f);
					q->b2 = lerpf(a->b2, b->b2, f); q->a2 = lerpf(a->a2, b->a2, f);
					if (q->w <= EPS)
						n--;
				}
			}
			for (int k = 1; k + 1 < n; k++) {
				struct clipv tri[3] = {poly[0], poly[k], poly[k + 1]};
				raster(F->px, F->w, F->h, F->stride, s_zb, tri, B->flags, T);
			}
		}
	}
}

static long cpu_draw(const struct gk_frame *F, const struct gk_vertex *v, long nv, const struct gk_batch *bt, long nb,
		     const struct gk_tex *texs)
{
	unsigned long need = ((unsigned long)F->w * (unsigned long)F->h * 4 + 4095) & ~4095UL;
	if (need > s_zb_len) {
		if (s_zb)
			korin_sys2(SYS_VM_UNMAP, s_zb, s_zb_len);
		long p = korin_sys4(SYS_VM_MAP, 0, need, PROT_READ | PROT_WRITE, 0);
		if (p <= 0) {
			s_zb = 0;
			s_zb_len = 0;
			return GK_E_NOMEM;
		}
		s_zb = (float *)p;
		s_zb_len = need;
	}
	if (!(F->flags & GK_F_KEEP))
		for (int y = 0; y < F->h; y++)
			for (int x = 0; x < F->w; x++)
				F->px[(long)y * F->stride + x] = F->clear & 0xFFFFFF;
	for (long i = 0; i < (long)F->w * F->h; i++)
		s_zb[i] = 1.0f;
	cpu_batches(F, v, nv, bt, nb, texs);
	return 0;
}

static int frame_ok(const struct gk_frame *F)
{
	return F && F->px && F->w > 0 && F->h > 0 && F->w <= 8192 && F->h <= 8192 && F->stride >= F->w;
}

long gk_draw(long frame, long verts, long n, long batches, long nb, long texs)
{
	const struct gk_frame *F = (const struct gk_frame *)frame;
	if (!frame_ok(F) || (n && !verts) || (nb && !batches) || n < 0 || nb < 0)
		return GK_E_INVAL;
	if (s_be && !s_dry && F->w <= GK_MAX_W && F->h <= GK_MAX_H) {
		long r = lost(s_be->draw(frame, verts, n, batches, nb, texs));
		if (r == 1)
			return 1;
		/* refusé (texture de GpuKit inconnue, GPU perdu) : le processeur */
	}
	long r = cpu_draw(F, (const struct gk_vertex *)verts, n, (const struct gk_batch *)batches, nb,
			  (const struct gk_tex *)texs);
	return r < 0 ? r : 0;
}

/* gk_render : le GPU seul ; les lots désignent des textures de gk_texture (GKB_TEX) */
long gk_render(long frame, long verts, long n, long batches, long nb)
{
	if (!s_be)
		return GK_E_NOGPU;
	const struct gk_frame *F = (const struct gk_frame *)frame;
	if (!frame_ok(F) || F->w > GK_MAX_W || F->h > GK_MAX_H)
		return GK_E_INVAL;
	long r = lost(s_be->draw(frame, verts, n, batches, nb, 0));
	return r == 1 ? 0 : r < 0 ? r : GK_E_NOGPU;
}

/* le triangle de démonstration : par le GPU seulement (0 : pas de GPU), comme le Pi */
long gk_demo_triangle(unsigned *px, long pitch, long w, long h)
{
	if (!s_be || !px || w <= 0 || h <= 0 || (pitch & 3))
		return 0;
	struct gk_vertex v[3];
	memset(v, 0, sizeof v);
	static const float p[3][2] = {{-0.8f, -0.8f}, {0.8f, -0.8f}, {0.0f, 0.8f}};
	for (int i = 0; i < 3; i++) {
		v[i].x = p[i][0];
		v[i].y = p[i][1];
		v[i].w = 1;
		v[i].rgba[i] = 255;
		v[i].rgba[3] = 255;
	}
	struct gk_batch b;
	memset(&b, 0, sizeof b);
	b.count = 3;
	b.tex = -1;
	b.flags = 7 | GK_B_NOMATRIX;
	struct gk_frame f = {px, (int)w, (int)h, (int)(pitch / 4), 0x000040, 0, 0};
	return gk_draw((long)&f, (long)v, 3, (long)&b, 1, 0) == 1;
}
