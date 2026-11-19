// render.cpp -- TerraCube : le rendu par OpenGL ES 3.0 (render.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include "items.h"

namespace tc {

// ---- matrices -----------------------------------------------------------------------------------------------------

void matPerspective (float *m, float fovDeg, float aspect, float zn, float zf)
{
	float f = 1.0f / tanf (fovDeg * (float) M_PI / 360.0f);
	memset (m, 0, 16 * sizeof (float));
	m[0] = f / aspect;
	m[5] = f;
	m[10] = (zf + zn) / (zn - zf);
	m[11] = -1;
	m[14] = 2 * zf * zn / (zn - zf);
}

void matMul (float *o, const float *a, const float *b)
{
	float r[16];
	for (int c = 0; c < 4; c++)
		for (int rr = 0; rr < 4; rr++) {
			float s = 0;
			for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
			r[c * 4 + rr] = s;
		}
	memcpy (o, r, sizeof r);
}

bool matInvert (float *out, const float *m)
{
	float inv[16];
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (fabsf (det) < 1e-12f) return false;
	for (int i = 0; i < 16; i++) out[i] = inv[i] / det;
	return true;
}

void viewDir (float yaw, float pitch, float d[3])
{
	float y = yaw * (float) M_PI / 180.0f, p = pitch * (float) M_PI / 180.0f;
	d[0] = sinf (y) * cosf (p);
	d[1] = sinf (p);
	d[2] = -cosf (y) * cosf (p);
}

// la vue : rotation de la caméra (pas de translation : tout est relatif à l'œil)
static void viewMatrix (float *m, float yaw, float pitch)
{
	float f[3];
	viewDir (yaw, pitch, f);
	float up[3] = { 0, 1, 0 };
	float s[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
	float sl = sqrtf (s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	if (sl < 1e-6f) s[0] = 1, s[1] = s[2] = 0, sl = 1;
	for (float &v : s) v /= sl;
	float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
	float r[16] = { s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1 };
	memcpy (m, r, sizeof r);
}

// ---- shaders ------------------------------------------------------------------------------------------------------

// l'eau (u_water = 1) bouge : la surface (les sommets à 14/16 de bloc) ondule en hauteur, la texture glisse et se
// déforme, des reflets passent. Les ondes ont des périodes qui divisent 2048 blocs : u_cell (la case de l'œil modulo
// 2048) les garde continues d'un tronçon à l'autre.
static const char *VS_WORLD = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_light;
layout(location = 2) in vec2 a_uv;
uniform mat4 u_vp;
uniform vec3 u_origin;
uniform vec3 u_cam;
uniform float u_fog0, u_fog1, u_day;
uniform float u_water, u_time;
uniform vec2 u_cell;
out vec2 v_uv;
out float v_light;
out float v_fog;
out vec3 v_world;
void main () {
	vec3 p = a_pos * (1.0 / 16.0) + u_origin;
	vec3 w = vec3 (p.x + u_cell.x, p.y, p.z + u_cell.y);
	if (u_water > 0.5 && abs (mod (a_pos.y, 16.0) - 14.0) < 0.5) {
		float h = sin (w.x * 0.785398 + u_time * 1.7) * 0.55 + sin ((w.x + w.z) * 0.392699 - u_time * 1.1) * 0.30
			+ sin (w.z * 1.570796 + u_time * 2.3) * 0.15;
		p.y += (h - 1.0) * 0.045;
	}
	gl_Position = u_vp * vec4 (p - u_cam, 1.0);
	v_uv = a_uv * (1.0 / 4096.0);
	v_light = max (a_light.x * (0.24 + 0.76 * u_day), a_light.y);
	float d = length ((p - u_cam).xz);
	v_fog = clamp ((d - u_fog0) / (u_fog1 - u_fog0), 0.0, 1.0);
	v_world = w;
}
)";

static const char *FS_WORLD = R"(#version 300 es
precision highp float;
uniform sampler2D u_tex;
uniform vec3 u_fogcol;
uniform float u_alpha;
uniform float u_water, u_time, u_day;
in vec2 v_uv;
in float v_light;
in float v_fog;
in vec3 v_world;
out vec4 o;
void main () {
	vec4 t;
	float light = v_light;
	if (u_water > 0.5) {
		// la tuile de l'eau, lue à une place qui glisse et ondule
		vec2 tile = floor (v_uv * 16.0) / 16.0;
		vec2 q = v_world.xz + vec2 (v_world.y * 0.37, v_world.y * 0.61);
		vec2 d = vec2 (sin (q.y * 1.7 + u_time * 1.3), cos (q.x * 1.9 + u_time * 1.1)) * 0.07;
		vec2 l = fract (q + vec2 (u_time * 0.045, u_time * 0.028) + d);
		t = texture (u_tex, tile + clamp (l, 0.004, 0.996) / 16.0);
		// des reflets qui passent (plus forts de jour)
		float sh = sin (v_world.x * 1.3 + u_time * 2.0 + sin (v_world.z * 0.7)) * sin (v_world.z * 1.1 - u_time * 1.6);
		t.rgb *= 1.0 + 0.10 * sh;
		t.rgb += vec3 (0.30, 0.34, 0.38) * smoothstep (0.82, 1.0, sh) * (0.3 + 0.7 * u_day);
		t.a = clamp (t.a + 0.12 * sh, 0.45, 0.9);
	} else
		t = texture (u_tex, v_uv);
	if (u_alpha < 0.5 && t.a < 0.5) discard;
	o = vec4 (mix (t.rgb * light, u_fogcol, v_fog), u_alpha < 0.5 ? 1.0 : t.a * u_alpha);
}
)";

static const char *VS_SKY = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
out vec2 v_ndc;
void main () { v_ndc = a_pos; gl_Position = vec4 (a_pos, 0.9999, 1.0); }
)";

static const char *FS_SKY = R"(#version 300 es
precision highp float;
uniform mat4 u_invvp;
uniform vec3 u_sun;
uniform vec3 u_zen, u_hor;
uniform float u_day;
in vec2 v_ndc;
out vec4 o;
void main () {
	vec4 a = u_invvp * vec4 (v_ndc, -1.0, 1.0), b = u_invvp * vec4 (v_ndc, 1.0, 1.0);
	vec3 d = normalize (b.xyz / b.w - a.xyz / a.w);
	float h = clamp (d.y * 1.6, 0.0, 1.0);
	vec3 c = mix (u_hor, u_zen, sqrt (h));
	float s = dot (d, u_sun);
	c += vec3 (1.0, 0.9, 0.6) * smoothstep (0.9985, 0.9993, s);		// le soleil
	c += vec3 (1.0, 0.7, 0.4) * pow (max (s, 0.0), 24.0) * 0.25 * (1.0 - h);	// son halo
	c += vec3 (0.85, 0.9, 1.0) * smoothstep (0.9990, 0.9995, -s) * (1.0 - u_day);	// la lune
	o = vec4 (c, 1.0);
}
)";

static const char *VS_HUD = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
uniform vec2 u_proj;
out vec2 v_uv;
out vec4 v_col;
void main () {
	v_uv = a_uv * (1.0 / 4096.0);
	v_col = a_col;
	gl_Position = vec4 (a_pos.x * u_proj.x - 1.0, 1.0 - a_pos.y * u_proj.y, 0.0, 1.0);
}
)";

static const char *FS_HUD = R"(#version 300 es
precision mediump float;
uniform sampler2D u_tex;
in vec2 v_uv;
in vec4 v_col;
out vec4 o;
void main () {
	vec4 t = texture (u_tex, v_uv);
	o = t * v_col;
	if (o.a < 0.02) discard;
}
)";

static const char *VS_LINE = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
uniform mat4 u_vp;
void main () { gl_Position = u_vp * vec4 (a_pos, 1.0); }
)";

static const char *FS_LINE = R"(#version 300 es
precision mediump float;
uniform vec4 u_col;
out vec4 o;
void main () { o = u_col; }
)";

// la pluie et la neige : des gouttes faites par le vertex shader seul (gl_VertexID, aucun tampon), six sommets par
// goutte, dans une boîte de 48 × 32 × 48 blocs autour de l'œil, accrochées au monde (u_wrap : l'œil modulo la boîte) ;
// une goutte sous le plus haut bloc de sa colonne (u_hmap, 64 × 64 autour de l'œil) n'est pas montrée : il ne pleut
// pas sous un toit ni sous les arbres.
static const char *VS_RAIN = R"(#version 300 es
uniform mat4 u_vp;
uniform vec3 u_eye;
uniform vec3 u_wrap;
uniform float u_time, u_kind, u_count;
uniform highp sampler2D u_hmap;
out float v_a;
out vec2 v_q;
float hash (float n) { return fract (sin (n) * 43758.5453); }
void main () {
	int id = gl_VertexID / 6, k = gl_VertexID - id * 6;
	float f = float (id);
	v_q = vec2 (k == 1 || k == 2 || k == 4 ? 1.0 : 0.0, k == 2 || k == 4 || k == 5 ? 1.0 : 0.0);
	if (f >= u_count) { gl_Position = vec4 (2.0, 2.0, 2.0, 1.0); v_a = 0.0; return; }
	vec3 h = vec3 (hash (f * 1.173 + 0.31), hash (f * 2.317 + 1.73), hash (f * 3.771 + 4.13));
	float snow = u_kind > 1.5 ? 1.0 : 0.0;
	float speed = mix (15.0 + h.z * 5.0, 1.3 + h.z * 0.9, snow);
	vec3 c;
	c.x = mod (h.x * 48.0 - u_wrap.x, 48.0) - 24.0;
	c.z = mod (h.y * 48.0 - u_wrap.z, 48.0) - 24.0;
	c.y = mod (h.z * 997.0 - u_time * speed - u_wrap.y, 32.0) - 14.0;
	c.x += snow * sin (u_time * 1.1 + f) * 0.35;
	c.z += snow * cos (u_time * 0.9 + f * 1.7) * 0.35;
	ivec2 col = ivec2 (floor (u_eye.x + c.x), floor (u_eye.z + c.z)) + ivec2 (32);
	float top = texelFetch (u_hmap, clamp (col, ivec2 (0), ivec2 (63)), 0).r * 255.0;
	if (u_eye.y + c.y < top) { gl_Position = vec4 (2.0, 2.0, 2.0, 1.0); v_a = 0.0; return; }
	vec2 hd = c.xz;
	float l = length (hd);
	vec3 right = l > 0.01 ? vec3 (-hd.y, 0.0, hd.x) / l : vec3 (1.0, 0.0, 0.0);
	vec2 size = mix (vec2 (0.035, 0.75), vec2 (0.13, 0.13), snow);
	vec3 p = c + right * (v_q.x - 0.5) * size.x + vec3 (0.0, (v_q.y - 0.5) * size.y, 0.0);
	gl_Position = u_vp * vec4 (p, 1.0);
	v_a = (1.0 - smoothstep (14.0, 24.0, l)) * mix (0.42, 0.9, snow);
}
)";

static const char *FS_RAIN = R"(#version 300 es
precision highp float;
uniform float u_kind;
in float v_a;
in vec2 v_q;
out vec4 o;
void main () {
	if (u_kind > 1.5) {
		vec2 d = v_q - 0.5;
		if (dot (d, d) > 0.25) discard;
		o = vec4 (1.0, 1.0, 1.0, v_a);
	} else
		o = vec4 (0.72, 0.78, 0.9, v_a);
}
)";

static unsigned compile (unsigned type, const char *src, std::string &err)
{
	unsigned s = glCreateShader (type);
	glShaderSource (s, 1, &src, nullptr);
	glCompileShader (s);
	int ok = 0;
	glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog (s, sizeof log, nullptr, log);
		err = std::string ("shader : ") + log;
		return 0;
	}
	return s;
}

static unsigned program (const char *vs, const char *fs, std::string &err)
{
	unsigned v = compile (GL_VERTEX_SHADER, vs, err), f = v ? compile (GL_FRAGMENT_SHADER, fs, err) : 0;
	if (!v || !f) return 0;
	unsigned p = glCreateProgram ();
	glAttachShader (p, v);
	glAttachShader (p, f);
	glLinkProgram (p);
	int ok = 0;
	glGetProgramiv (p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog (p, sizeof log, nullptr, log);
		err = std::string ("programme : ") + log;
		return 0;
	}
	glDeleteShader (v);
	glDeleteShader (f);
	return p;
}

bool Renderer::init (std::string &err)
{
	const char *r = (const char *) glGetString (GL_RENDERER);
	name = r ? r : "?";
	progWorld = program (VS_WORLD, FS_WORLD, err);
	progSky = progWorld ? program (VS_SKY, FS_SKY, err) : 0;
	progHud = progSky ? program (VS_HUD, FS_HUD, err) : 0;
	progLine = progHud ? program (VS_LINE, FS_LINE, err) : 0;
	progRain = progLine ? program (VS_RAIN, FS_RAIN, err) : 0;
	if (!progRain) return false;
	static const char *RU[8] = { "u_vp", "u_eye", "u_wrap", "u_time", "u_kind", "u_count", "u_hmap", 0 };
	for (int i = 0; RU[i]; i++) rU[i] = glGetUniformLocation (progRain, RU[i]);
	glGenTextures (1, &hmapTex);
	glBindTexture (GL_TEXTURE_2D, hmapTex);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_R8, HMAP, HMAP, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	uVp = glGetUniformLocation (progWorld, "u_vp");
	uOrigin = glGetUniformLocation (progWorld, "u_origin");
	uCam = glGetUniformLocation (progWorld, "u_cam");
	uFog0 = glGetUniformLocation (progWorld, "u_fog0");
	uFog1 = glGetUniformLocation (progWorld, "u_fog1");
	uDay = glGetUniformLocation (progWorld, "u_day");
	uFogCol = glGetUniformLocation (progWorld, "u_fogcol");
	uAlpha = glGetUniformLocation (progWorld, "u_alpha");
	uTex = glGetUniformLocation (progWorld, "u_tex");
	uWater = glGetUniformLocation (progWorld, "u_water");
	uTime = glGetUniformLocation (progWorld, "u_time");
	uCell = glGetUniformLocation (progWorld, "u_cell");
	sInvVp = glGetUniformLocation (progSky, "u_invvp");
	sSun = glGetUniformLocation (progSky, "u_sun");
	sDay = glGetUniformLocation (progSky, "u_day");
	sZen = glGetUniformLocation (progSky, "u_zen");
	sHor = glGetUniformLocation (progSky, "u_hor");
	hProj = glGetUniformLocation (progHud, "u_proj");
	hTex = glGetUniformLocation (progHud, "u_tex");
	lVp = glGetUniformLocation (progLine, "u_vp");
	lCol = glGetUniformLocation (progLine, "u_col");
	// l'atlas
	std::vector<uint8_t> px (256 * 256 * 4);
	buildAtlas (px.data ());
	glGenTextures (1, &tex);
	glBindTexture (GL_TEXTURE_2D, tex);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data ());
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenVertexArrays (1, &vao);
	glBindVertexArray (vao);
	glGenBuffers (1, &ibo);
	glGenBuffers (1, &hudVbo);
	glGenBuffers (1, &lineVbo);
	ensureIndices (4096);
	return glGetError () == GL_NO_ERROR || true;
}

void Renderer::ensureIndices (int quads)
{
	if (quads <= iboQuads) return;
	int n = iboQuads ? iboQuads : 4096;
	while (n < quads) n *= 2;
	std::vector<uint32_t> idx ((size_t) n * 6);
	for (int q = 0; q < n; q++) {
		uint32_t b = (uint32_t) q * 4;
		uint32_t *p = &idx[(size_t) q * 6];
		p[0] = b, p[1] = b + 1, p[2] = b + 2, p[3] = b, p[4] = b + 2, p[5] = b + 3;
	}
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData (GL_ELEMENT_ARRAY_BUFFER, (long) (idx.size () * 4), idx.data (), GL_STATIC_DRAW);
	iboQuads = n;
}

void Renderer::upload (Mesh &m, const std::vector<Vertex> &v)
{
	m.quads = (int) (v.size () / 4);
	if (!m.quads) {
		if (m.vbo) glDeleteBuffers (1, &m.vbo);
		m.vbo = 0;
		return;
	}
	if (!m.vbo) glGenBuffers (1, &m.vbo);
	glBindBuffer (GL_ARRAY_BUFFER, m.vbo);
	glBufferData (GL_ARRAY_BUFFER, (long) (v.size () * sizeof (Vertex)), v.data (), GL_STATIC_DRAW);
	ensureIndices (m.quads);
}

void Renderer::freeChunk (Chunk *c)
{
	if (c->solid.vbo) glDeleteBuffers (1, &c->solid.vbo);
	if (c->water.vbo) glDeleteBuffers (1, &c->water.vbo);
	c->solid.vbo = c->water.vbo = 0;
}

static double nowMs ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

int Renderer::updateMeshes (World &w, const Camera &cam, int distance, double maxMs)
{
	// les plus proches d'abord
	int pcx = floorDiv ((int) floor (cam.x), CW), pcz = floorDiv ((int) floor (cam.z), CW);
	double t0 = nowMs ();
	int done = 0;
	static std::vector<Vertex> sv, wv;
	for (int r = 0; r <= distance; r++)
		for (int dz = -r; dz <= r; dz++)
			for (int dx = -r; dx <= r; dx++) {
				if (abs (dx) != r && abs (dz) != r) continue;
				Chunk *c = w.find (pcx + dx, pcz + dz);
				if (!c || !c->dirty) continue;
				// les quatre voisins faits (sinon leurs bords manquent)
				if (!w.find (c->cx + 1, c->cz) || !w.find (c->cx - 1, c->cz) || !w.find (c->cx, c->cz + 1) || !w.find (c->cx, c->cz - 1)) continue;
				sv.clear (), wv.clear ();
				buildMesh (w, *c, sv, wv);
				upload (c->solid, sv);
				upload (c->water, wv);
				c->dirty = false;
				c->meshed = true;
				done++;
				if (nowMs () - t0 > maxMs) return done;
			}
	return done;
}

// ---- une image ----------------------------------------------------------------------------------------------------

static void rgbf (unsigned c, float *o)
{
	o[0] = (c >> 16 & 255) / 255.0f, o[1] = (c >> 8 & 255) / 255.0f, o[2] = (c & 255) / 255.0f;
}

static void mix3 (const float *a, const float *b, float t, float *o)
{
	for (int i = 0; i < 3; i++) o[i] = a[i] + (b[i] - a[i]) * t;
}

void Renderer::draw (World &w, const Camera &cam, const Frame &f, const HudState &hud)
{
	glViewport (0, 0, f.w, f.h);
	// les couleurs du ciel selon l'heure (données du monde, pas de l'interface)
	float dayZen[3], dayHor[3], nightZen[3], nightHor[3], duskHor[3];
	rgbf (0x4f8fe6, dayZen), rgbf (0xb9d6f2, dayHor), rgbf (0x0b1430, nightZen), rgbf (0x24304c, nightHor), rgbf (0xf09a5a, duskHor);
	float zen[3], hor[3];
	mix3 (nightZen, dayZen, f.daylight, zen);
	mix3 (nightHor, dayHor, f.daylight, hor);
	float dusk = 1.0f - fabsf (f.daylight - 0.5f) * 2.0f;
	if (dusk > 0) mix3 (hor, duskHor, dusk * 0.55f * (1.0f - f.gloom), hor);
	// le temps gris (pluie, neige, orage), l'éclair
	if (f.gloom > 0) {
		float l = (zen[0] + zen[1] + zen[2]) / 3.0f * 0.9f + 0.03f, lh = (hor[0] + hor[1] + hor[2]) / 3.0f * 0.85f + 0.03f;
		float gz[3] = { l, l, l * 1.05f }, gh[3] = { lh, lh, lh * 1.04f };
		mix3 (zen, gz, f.gloom * 0.8f, zen);
		mix3 (hor, gh, f.gloom * 0.8f, hor);
	}
	if (f.flash > 0) {
		float wf[3] = { 0.85f, 0.88f, 1.0f };
		mix3 (zen, wf, f.flash * 0.7f, zen);
		mix3 (hor, wf, f.flash * 0.7f, hor);
	}
	float fog[3] = { hor[0], hor[1], hor[2] };
	if (hud.underwater) fog[0] = 0.05f, fog[1] = 0.18f + 0.2f * f.daylight, fog[2] = 0.35f + 0.3f * f.daylight;

	float proj[16], view[16], vp[16], inv[16];
	matPerspective (proj, cam.fov, (float) f.w / (float) (f.h ? f.h : 1), 0.08f, (float) (f.distance + 2) * CW * 1.5f);
	viewMatrix (view, cam.yaw, cam.pitch);
	matMul (vp, proj, view);
	glDisable (GL_BLEND);
	glDepthMask (GL_TRUE);
	glClearColor (fog[0], fog[1], fog[2], 1);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// le ciel (sous l'eau : la couleur du brouillard suffit)
	glBindVertexArray (vao);
	if (!hud.underwater && matInvert (inv, vp)) {
		static const float TRI[6] = { -1, -1, 3, -1, -1, 3 };
		glUseProgram (progSky);
		glUniformMatrix4fv (sInvVp, 1, GL_FALSE, inv);
		float sun[3] = { cosf (f.sunAngle), sinf (f.sunAngle), 0.25f };
		float sl = sqrtf (sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
		for (float &v : sun) v /= sl;
		glUniform3fv (sSun, 1, sun);
		glUniform3fv (sZen, 1, zen);
		glUniform3fv (sHor, 1, hor);
		glUniform1f (sDay, f.daylight);
		glBindBuffer (GL_ARRAY_BUFFER, hudVbo);
		glBufferData (GL_ARRAY_BUFFER, sizeof TRI, TRI, GL_STREAM_DRAW);
		glEnableVertexAttribArray (0);
		glDisableVertexAttribArray (1);
		glDisableVertexAttribArray (2);
		glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
		glDisable (GL_DEPTH_TEST);
		glDepthMask (GL_FALSE);
		glDrawArrays (GL_TRIANGLES, 0, 3);
		glDepthMask (GL_TRUE);
	}

	// les tronçons
	glEnable (GL_DEPTH_TEST);
	glDepthFunc (GL_LEQUAL);
	glEnable (GL_CULL_FACE);
	glCullFace (GL_BACK);
	glFrontFace (GL_CCW);
	glUseProgram (progWorld);
	glActiveTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, tex);
	glUniform1i (uTex, 0);
	glUniformMatrix4fv (uVp, 1, GL_FALSE, vp);
	double bx = floor (cam.x), bz = floor (cam.z);
	float camLocal[3] = { (float) (cam.x - bx), (float) cam.y, (float) (cam.z - bz) };
	glUniform3fv (uCam, 1, camLocal);
	float fogEnd = ((float) f.distance * CW - 4.0f) * (1.0f - 0.35f * f.gloom), fogStart = fogEnd * (hud.underwater ? 0.0f : 0.62f - 0.3f * f.gloom);
	if (hud.underwater) fogEnd = 18.0f;
	glUniform1f (uFog0, fogStart);
	glUniform1f (uFog1, fogEnd);
	glUniform1f (uDay, std::min (1.0f, f.daylight + f.flash * 0.6f));
	glUniform3fv (uFogCol, 1, fog);
	// (l'heure des vagues : les secondes, ramenées sous une heure pour la précision ; la case de l'œil modulo 2048)
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	glUniform1f (uTime, (float) fmod (ts.tv_sec + ts.tv_nsec / 1e9, 3600.0));
	glUniform2f (uCell, (float) (bx - floor (bx / 2048.0) * 2048.0), (float) (bz - floor (bz / 2048.0) * 2048.0));
	glUniform1f (uWater, 0.0f);
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, ibo);
	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);
	glEnableVertexAttribArray (2);
	// plans du tronc de vue (repère relatif à la case de l'œil), pour écarter les tronçons hors champ
	float pl[6][4];
	for (int i = 0; i < 4; i++) {
		pl[0][i] = vp[i * 4 + 3] + vp[i * 4 + 0];
		pl[1][i] = vp[i * 4 + 3] - vp[i * 4 + 0];
		pl[2][i] = vp[i * 4 + 3] + vp[i * 4 + 1];
		pl[3][i] = vp[i * 4 + 3] - vp[i * 4 + 1];
		pl[4][i] = vp[i * 4 + 3] + vp[i * 4 + 2];
		pl[5][i] = vp[i * 4 + 3] - vp[i * 4 + 2];
	}
	auto visible = [&] (float x0, float y0, float z0, float x1, float y1, float z1) {
		for (auto &p : pl) {
			float x = p[0] >= 0 ? x1 : x0, y = p[1] >= 0 ? y1 : y0, z = p[2] >= 0 ? z1 : z0;
			if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false;
		}
		return true;
	};
	int pcx = floorDiv ((int) bx, CW), pcz = floorDiv ((int) bz, CW);
	std::vector<Chunk *> shown;
	drawnChunks = drawnQuads = 0;
	for (auto &kv : w.chunks) {
		Chunk *c = kv.second;
		if (!c->meshed) continue;
		if (abs (c->cx - pcx) > f.distance || abs (c->cz - pcz) > f.distance) continue;
		float ox = (float) (c->cx * CW - bx), oz = (float) (c->cz * CW - bz);
		if (!visible (ox - camLocal[0], -cam.y, oz - camLocal[2], ox + CW - camLocal[0], (float) CH - (float) cam.y, oz + CW - camLocal[2])) continue;
		shown.push_back (c);
	}
	auto drawMesh = [&] (Chunk *c, Mesh &m) {
		float o[3] = { (float) (c->cx * CW - bx), 0, (float) (c->cz * CW - bz) };
		glUniform3fv (uOrigin, 1, o);
		glBindBuffer (GL_ARRAY_BUFFER, m.vbo);
		glVertexAttribPointer (0, 3, GL_UNSIGNED_SHORT, GL_FALSE, sizeof (Vertex), (void *) 0);
		glVertexAttribPointer (1, 2, GL_UNSIGNED_BYTE, GL_TRUE, sizeof (Vertex), (void *) 6);
		glVertexAttribPointer (2, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof (Vertex), (void *) 8);
		glDrawElements (GL_TRIANGLES, m.quads * 6, GL_UNSIGNED_INT, nullptr);
		drawnQuads += m.quads;
	};
	glUniform1f (uAlpha, 0.0f);
	for (Chunk *c : shown)
		if (c->solid.quads) drawMesh (c, c->solid), drawnChunks++;
	// l'eau : mélangée, sans écrire la profondeur, des deux côtés
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask (GL_FALSE);
	glDisable (GL_CULL_FACE);
	glUniform1f (uAlpha, 1.0f);
	glUniform1f (uWater, 1.0f);
	for (Chunk *c : shown)
		if (c->water.quads) drawMesh (c, c->water);
	glDepthMask (GL_TRUE);
	glEnable (GL_CULL_FACE);

	if (f.precip && f.precipLevel > 0.01f && !hud.underwater) drawPrecip (cam, f, vp);
	if (f.hasTarget) drawSelection (cam, f.target, vp);
	drawHud (f, hud);
}

void Renderer::drawPrecip (const Camera &cam, const Frame &f, const float *vp)
{
	if (f.heights) {
		glBindTexture (GL_TEXTURE_2D, hmapTex);
		glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, HMAP, HMAP, GL_RED, GL_UNSIGNED_BYTE, f.heights);
	}
	glUseProgram (progRain);
	glDisableVertexAttribArray (0);
	glDisableVertexAttribArray (1);
	glDisableVertexAttribArray (2);
	glActiveTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, hmapTex);
	glUniform1i (rU[6], 0);
	glUniformMatrix4fv (rU[0], 1, GL_FALSE, vp);
	double bx = floor (cam.x), bz = floor (cam.z);
	glUniform3f (rU[1], (float) (cam.x - bx), (float) cam.y, (float) (cam.z - bz));
	glUniform3f (rU[2], (float) (cam.x - floor (cam.x / 48.0) * 48.0), (float) (cam.y - floor (cam.y / 32.0) * 32.0), (float) (cam.z - floor (cam.z / 48.0) * 48.0));
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	glUniform1f (rU[3], (float) fmod (ts.tv_sec + ts.tv_nsec / 1e9, 3600.0));
	glUniform1f (rU[4], (float) f.precip);
	const int MAXD = 5000;
	float n = f.precipLevel * (f.precip == 2 ? 3500.0f : MAXD);
	glUniform1f (rU[5], n);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask (GL_FALSE);
	glDisable (GL_CULL_FACE);
	glDrawArrays (GL_TRIANGLES, 0, (int) n * 6);
	glDepthMask (GL_TRUE);
	glEnable (GL_CULL_FACE);
	glBindTexture (GL_TEXTURE_2D, tex);
}

void Renderer::drawSelection (const Camera &cam, const Hit &h, const float *vp)
{
	double bx = floor (cam.x), bz = floor (cam.z);
	float x0 = (float) (h.x - bx) - (float) (cam.x - bx) - 0.004f, y0 = (float) (h.y - cam.y) - 0.004f, z0 = (float) (h.z - bz) - (float) (cam.z - bz) - 0.004f;
	float x1 = x0 + 1.008f, y1 = y0 + 1.008f, z1 = z0 + 1.008f;
	float L[24][3] = {
		{ x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x1, y0, z1 }, { x0, y0, z1 }, { x0, y0, z1 }, { x0, y0, z0 },
		{ x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, { x0, y1, z1 }, { x0, y1, z0 },
		{ x0, y0, z0 }, { x0, y1, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y0, z1 }, { x0, y1, z1 },
	};
	glUseProgram (progLine);
	glUniformMatrix4fv (lVp, 1, GL_FALSE, vp);
	glUniform4f (lCol, 0.05f, 0.05f, 0.05f, 0.75f);
	glBindBuffer (GL_ARRAY_BUFFER, lineVbo);
	glBufferData (GL_ARRAY_BUFFER, sizeof L, L, GL_STREAM_DRAW);
	glDisableVertexAttribArray (1);
	glDisableVertexAttribArray (2);
	glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDrawArrays (GL_LINES, 0, 24);
}

// ---- affichage tête haute -----------------------------------------------------------------------------------------

namespace {

struct HV {
	float x, y;
	float u, v;
	uint8_t c[4];
};

struct HudBatch {
	std::vector<HV> v;
	void quad (float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, unsigned rgb, int a)
	{
		uint8_t c[4] = { (uint8_t) (rgb >> 16), (uint8_t) (rgb >> 8), (uint8_t) rgb, (uint8_t) a };
		HV p[6] = { { x0, y0, u0, v0, {} }, { x1, y0, u1, v0, {} }, { x1, y1, u1, v1, {} },
			    { x0, y0, u0, v0, {} }, { x1, y1, u1, v1, {} }, { x0, y1, u0, v1, {} } };
		for (auto &q : p) memcpy (q.c, c, 4), v.push_back (q);
	}
	// un quadrilatère quelconque (les faces de l'icône d'un bloc)
	void poly (const float (*xy)[2], int tile, unsigned rgb)
	{
		float u0 = (tile & 15) * 256 + 1, v0 = (tile >> 4) * 256 + 1, u1 = u0 + 254, v1 = v0 + 254;
		float uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
		uint8_t c[4] = { (uint8_t) (rgb >> 16), (uint8_t) (rgb >> 8), (uint8_t) rgb, 255 };
		int o[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k : o) {
			HV p = { xy[k][0], xy[k][1], uv[k][0], uv[k][1], {} };
			memcpy (p.c, c, 4);
			v.push_back (p);
		}
	}
	void solid (float x0, float y0, float x1, float y1, unsigned rgb, int a)
	{
		float u = (T_SELECT & 15) * 256 + 128, vv = (T_SELECT >> 4) * 256 + 128;
		quad (x0, y0, x1, y1, u, vv, u, vv, rgb, a);
	}
};

void blockIcon (HudBatch &b, int item, float cx, float cy, float s)
{
	if (item >= 100 || info (item).shape == SH_CROSS || info (item).shape == SH_NONE) {
		int t = itemInfo (item).tile;
		float u0 = (t & 15) * 256 + 1, v0 = (t >> 4) * 256 + 1;
		b.quad (cx - s * 0.5f, cy - s * 0.5f, cx + s * 0.5f, cy + s * 0.5f, u0, v0, u0 + 254, v0 + 254, 0xffffff, 255);
		return;
	}
	const BlockInfo &bi = info (item);
	// un cube vu de trois quarts : le dessus, la face gauche, la face droite
	float h = s * 0.5f, q = s * 0.25f;
	float top[4][2] = { { cx, cy - h }, { cx + h, cy - q }, { cx, cy }, { cx - h, cy - q } };
	float left[4][2] = { { cx - h, cy - q }, { cx, cy }, { cx, cy + h }, { cx - h, cy + q } };
	float right[4][2] = { { cx, cy }, { cx + h, cy - q }, { cx + h, cy + q }, { cx, cy + h } };
	b.poly (top, bi.top, 0xffffff);
	b.poly (left, bi.side, 0xc0c0c0);
	b.poly (right, bi.side, 0x9a9a9a);
}

// un nombre en chiffres de l'atlas, son bord droit en xr (le chiffre occupe les colonnes 1 à 11 de sa tuile)
void number (HudBatch &b, int n, float xr, float y, float sz)
{
	std::string t = std::to_string (n);
	float adv = sz * 0.62f;
	float x = xr - sz * 12.0f / 16.0f - adv * (float) (t.size () - 1);
	for (char c : t) {
		int tile = T_DIGIT0 + (c - '0');
		float u0 = (tile & 15) * 256 + 1, v0 = (tile >> 4) * 256 + 1;
		b.quad (x, y, x + sz, y + sz, u0, v0, u0 + 254, v0 + 254, 0xffffff, 255);
		x += adv;
	}
}

} // namespace

void Renderer::drawHud (const Frame &f, const HudState &hud)
{
	HudBatch b;
	float W = (float) f.w, H = (float) f.h;
	if (hud.underwater) b.solid (0, 0, W, H, 0x1a4a90, 70);
	// la barre des blocs
	float slot = 46, gap = 4, total = 9 * slot + 8 * gap;
	float x0 = (W - total) * 0.5f, y0 = H - slot - 12;
	struct Num {
		int n;
		float x, y;
	};
	std::vector<Num> nums;
	if (hud.hotbar) b.solid (x0 - 6, y0 - 6, x0 + total + 6, y0 + slot + 6, hud.panel, 200);
	for (int i = 0; i < 9 && hud.hotbar; i++) {
		float x = x0 + i * (slot + gap);
		bool sel = i == hud.selected;
		if (sel) b.solid (x - 3, y0 - 3, x + slot + 3, y0 + slot + 3, hud.accent, 255);
		b.solid (x, y0, x + slot, y0 + slot, hud.panelLine, sel ? 255 : 160);
		if (hud.slots[i] > 0) blockIcon (b, hud.slots[i], x + slot * 0.5f, y0 + slot * 0.5f, slot * 0.72f);
		if (hud.slots[i] > 0 && hud.counts[i] > 1) nums.push_back ({ hud.counts[i], x + slot - 3, y0 + slot - 16 });
		if (hud.slots[i] > 0 && hud.wear[i] >= 0) {	// l'usure d'un outil
			float k = 1.0f - hud.wear[i];
			b.solid (x + 5, y0 + slot - 6, x + slot - 5, y0 + slot - 3, 0x000000, 200);
			b.solid (x + 5, y0 + slot - 6, x + 5 + (slot - 10) * k, y0 + slot - 4, k > 0.5f ? hud.good : k > 0.2f ? hud.warn : hud.bad, 255);
		}
	}
	// les nombres, par-dessus toutes les cases
	for (auto &n : nums) number (b, n.n, n.x, n.y, 15);
	// le bloc en train d'être cassé
	if (hud.mining >= 0) {
		float cx = floorf (W * 0.5f), cy = floorf (H * 0.5f) + 18;
		b.solid (cx - 24, cy, cx + 24, cy + 5, 0x000000, 160);
		b.solid (cx - 23, cy + 1, cx - 23 + 46 * hud.mining, cy + 4, hud.accent, 255);
	}
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram (progHud);
	glUniform2f (hProj, 2.0f / W, 2.0f / H);
	glUniform1i (hTex, 0);
	glBindTexture (GL_TEXTURE_2D, tex);
	glBindBuffer (GL_ARRAY_BUFFER, hudVbo);
	auto flush = [&] (HudBatch &bb) {
		if (bb.v.empty ()) return;
		glBufferData (GL_ARRAY_BUFFER, (long) (bb.v.size () * sizeof (HV)), bb.v.data (), GL_STREAM_DRAW);
		glEnableVertexAttribArray (0);
		glEnableVertexAttribArray (1);
		glEnableVertexAttribArray (2);
		glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, sizeof (HV), (void *) 0);
		glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, sizeof (HV), (void *) 8);
		glVertexAttribPointer (2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof (HV), (void *) 16);
		glDrawArrays (GL_TRIANGLES, 0, (int) bb.v.size ());
	};
	flush (b);
	// le viseur : la couleur inverse de ce qui est dessous
	if (hud.crosshair) {
		HudBatch c;
		float cx = floorf (W * 0.5f), cy = floorf (H * 0.5f);
		c.solid (cx - 10, cy - 1, cx + 10, cy + 1, 0xffffff, 255);
		c.solid (cx - 1, cy - 10, cx + 1, cy - 1, 0xffffff, 255);
		c.solid (cx - 1, cy + 1, cx + 1, cy + 10, 0xffffff, 255);
		glBlendFunc (GL_ONE_MINUS_DST_COLOR, GL_ZERO);
		flush (c);
	}
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable (GL_DEPTH_TEST);
}

} // namespace tc
