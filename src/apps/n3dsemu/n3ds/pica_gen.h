//
// n3ds/pica_gen.h -- ce que le coeur dit au generateur de shaders (n3ds_glsl.cpp) : une configuration du combineur
// (picatev) et une liste d'instructions d'un vertex shader specialise (picavs). Les structures sont celles d'Onyx ;
// a la place du code QPU, le generateur rend du GLSL ES, range dans qpu::Prog.
//
#ifndef _n3ds_pica_gen_h
#define _n3ds_pica_gen_h

namespace qpu
{
struct Prog
{
	enum { MAX = 1 << 16 };
	unsigned long long w[MAX / 8];		// le texte, avec son zero final
	int n; bool bad;
	Prog () : n (0), bad (false) {}
	int count () const { return n; }
	const unsigned long long *words () const { return w; }
	char *text () { return (char *) w; }
};
}

namespace picatev
{

struct Config
{
	// a stage: its three colour and alpha sources (0 the vertex colour, 1 the lighting's primary colour, 2 its
	// secondary one, 3..5 the textures, 6 the procedural texture, 13 the buffer, 14 the stage's constant, 15 the
	// stage before), their operands, the modes, the scales (1, 2, 4); pass: the stage changes nothing
	struct Stage { unsigned char src[3], srcA[3], op[3], opA[3], mode, modeA, scale, scaleA; bool pass; } stage[6];
	unsigned char updateRgb, updateA;	// the stages (bits 0..3) whose result goes to the buffer
	bool alphaTest; unsigned char alphaFunc;	// 0 never, 1 always, 2 =, 3 !=, 4 <, 5 <=, 6 >, 7 >= (the alpha against the reference)
	unsigned char texOn;			// the units that have a texture (bit a unit; another one reads 0 0 0 255)
	bool lit;				// the lighting's colours are given (else source 1 is the vertex colour, 2 is 0 0 0 255)
	// what is written, a channel (after the alpha test): 0 the combiner's result, 1 it x the blending constant's
	// channel outConst / 255, 2 it x (255 - that), 3 that constant itself, 4 255 - it. (The blending with a
	// constant colour, which the GPU's interface has not: the host turns the factors into these.)
	unsigned char outMode[4], outConst[4];
	// the procedural texture (source 6): its coordinates (0..2 a unit's), the clamps, the combinations, a separate alpha
	bool procOn, procSeparateA; unsigned char procCoord, procClampU, procClampV, procFuncRgb, procFuncA;
};

enum UniKind { U_CONST, U_KONST, U_BUFFER, U_ALPHAREF, U_TEXP0, U_TEXP1, U_BLEND, U_PROCW, U_PROCO };
// U_CONST: value; U_KONST: stage a's constant, channel b (0..255); U_BUFFER: the buffer's colour, channel b;
// U_BLEND: the blending constant's channel b (0..255); U_ALPHAREF: the reference (0..255); U_TEXP0 / U_TEXP1: lookup a's TMU words (the kernel writes them)
struct Uni { unsigned char kind, a, b; unsigned value; };

enum VaryKind { V_PRIMARY, V_LITP, V_LITS, V_COORD, V_PROC };
// V_PRIMARY / V_LITP / V_LITS: channel a (0..1); V_COORD: lookup a, component b (0 s, 1 t)
struct Vary { unsigned char kind, a, b; };

struct Lookup { unsigned char unit; };

enum { MAX_UNI = 512, MAX_LOOKUPS = 3 };

struct Shader
{
	qpu::Prog prog;
	Uni uni[MAX_UNI]; int nUni;
	Vary vary[32]; int nVary;
	Lookup look[MAX_LOOKUPS]; int nLook;
	unsigned flags;				// kapi_gpu_program flags (0)
	int nRegs;				// the most register-file registers in use at once
	const char *err;			// 0: ok
	bool unsupported;			// a feature not generated was asked (drawn without it)
	// (the tests': the value ranges the generator assumed -- after instruction ip, register reg: 0..5 r0..r5,
	// 6 + n rf n -- for the simulator to check)
	struct Claim { int ip, reg; long long lo, hi; };
	enum { MAX_CLAIMS = 2048 };
	Claim claim[MAX_CLAIMS]; int nClaim;
};

bool build (const Config &c, Shader &s);

// the configuration's key (for a cache of programs): the same key, the same program
unsigned long long key (const Config &c);

} // namespace picatev

namespace picavs
{
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

// An instruction of the list. op: the PICA's (0x00 ADD, 0x01 DP3, 0x02 DP4, 0x03 / 0x18 DPH, 0x05 EX2, 0x06 LG2,
// 0x08 MUL, 0x09 / 0x1A SGE, 0x0A / 0x1B SLT, 0x0B FLR, 0x0C MAX, 0x0D MIN, 0x0E RCP, 0x0F RSQ, 0x13 MOV; shape 2:
// MAD), or one of the list's own below. d: the register written (dOut: an output 0..15, else a register 0..31),
// writes: its components (x = 8). An operand k: a register's components through sw[k] (the component each of the
// four positions takes), negated or not -- or a constant (kind 1: slot[k], four floats as the operand's positions).
enum { OP_PUT = 0xFE, OP_CMP = 0xFD, OP_GUARD = 0xFC, OP_FLAG = 0xFB };
// OP_PUT: the constant slot[0]'s components into d's. OP_CMP: the two comparison results from operands 0 and 1,
// positions x and y (slot[2]: x's code << 3 | y's -- 0 ==, 1 !=, 2 <, 3 <=, 4 >, 5 >=, else true).
// OP_GUARD: guard d = guard reg[0] (0: none) and the results read as a branch does (reg[1]: 0 x or y, 1 x and y,
// 2 x, 3 y; reg[2]: bit 1 the value x must have, bit 0 y's), the opposite when shape is 1.
// OP_FLAG: result d (0 x, 1 y) = reg[0].  guard: the instruction is done where that guard says yes (0: always).
struct Op
{
	u8 op, shape, d, writes, dOut, guard;
	u8 kind[3], reg[3]; u16 slot[3];
	u8 sw[3][4], neg[3];
};
struct List { const Op *ops; u32 nOps; };

// What the program writes: VPM output i is write i.
enum { W_RAW, W_ONE_MINUS, W_XS, W_YS, W_ZS, W_RW, W_XC, W_YC, W_ZC, W_WC, W_LITP, W_LITS, W_ZERO, W_ONE };	// (ZERO, ONE: the number)
struct Write { u8 kind, reg, comp; };		// RAW / ONE_MINUS: output register reg's component comp (0 x); the position's: of the position in
						// output register reg (or build's `pos`); LITP / LITS: the lighting's primary / secondary colour's component comp (3: alpha)
// An output register's component (reg 0xFF: none -- 0).
struct Sem { u8 reg, comp; };
// The uniforms it reads, in order: a list constant's component, or one of the view's values --
// the clip position is (x KX + w CX, y KY + w CY, z KZ + w CZ, w); on the screen x / w * SX and y / w * SY
// (rounded to the V3D's 24.8), z / w * SZ + OZ.
enum { U_CONST, U_VIEW, U_LIGHT, U_TINY };	// (U_LIGHT: slot = a place in the lighting's values, below; U_TINY: 1e-30, under a square root)
enum { V_KX, V_CX, V_KY, V_CY, V_KZ, V_CZ, V_SX, V_SY, V_SZ, V_OZ, V_COUNT };
struct Uni { u8 kind, comp; u16 slot; };		// U_CONST: slot, comp; U_VIEW: slot = V_*
// The VPM inputs it reads: input i is the input register's component.
struct In { u8 reg, comp; };

// The lighting worked out at the vertex (what the 3DS core does on the processor for the host GPU's frames: the
// PICA's fragment lighting at the vertices, its two colours handed on as varyings): the normal from the
// quaternion the shader gives, the view vector, each light's direction and half-way vector, the tables read at the
// dot products the configuration names, the sums; the alphas from the Fresnel table. A table is not looked up:
// it is given as a broken line -- y = a + b x + the sum of c[j] max (x - t[j], 0) over LUT_KNOTS knots --, its
// scale folded in, x in 0..1 (abs) or -1..1.
// The values (uniforms U_LIGHT, floats): the ambient colour (3); each light i at lightAt (i): its position or
// direction (3), diffuse (3), ambient (3), specular 0 (3), specular 1 (3), the distance's bias and scale (2); each
// table k at tableAt (k): a, b, then (t, c) a knot -- k: 0 D0, 1 D1, 2 FR, 3 RR, 4 RG, 5 RB, 6 + i light i's
// distance attenuation.
enum { LUT_KNOTS = 8, LIGHT_VALUES = 17, TABLE_VALUES = 2 + 2 * LUT_KNOTS, LIGHTS_MAX = 8, TABLES = 6 + LIGHTS_MAX };
inline int lightAt (int i) { return 3 + i * LIGHT_VALUES; }
inline int tableAt (int k) { return 3 + LIGHTS_MAX * LIGHT_VALUES + k * TABLE_VALUES; }
enum { LIGHT_FLOATS = 3 + LIGHTS_MAX * LIGHT_VALUES + TABLES * TABLE_VALUES };
struct Lighting
{
	u8 count;					// lights (1..8)
	Sem quat[4], view[3];				// where the shader puts the normal's quaternion and the view vector
	struct { u8 directional, twoSided, distance; } light[LIGHTS_MAX];
	struct { u8 on, abs, input; } table[6];		// D0 D1 FR RR RG RB; input: 0 N.H, 1 V.H, 2 N.V, 3 L.N, else 0
	u8 fresnel, clampHighlights;			// fresnel: bit 0 the primary alpha, bit 1 the secondary's
};

struct Result { u32 nUni, nIn; bool ok; const char *why; int values, regsPeak; };

// The program for the version set (qpu::setVersion). -> false (r.why says): an instruction it does not do, too
// many values alive at once, more uniforms or inputs than there is room for.
// pos: where the position's x y z w are (0: the register a position write names); lit: the lighting (0: none).
// inPre: in[0 .. inPre) are given -- the vertex's first inputs, whether this program reads them or not; what else
// it reads comes after (a vertex shader after its coordinate shader, which the V3D gives a vertex's first values).
bool build (qpu::Prog &p, const List &l, const Write *w, u32 nw, Uni *uni, u32 uniMax, In *in, u32 inMax, Result &r,
	    const Sem *pos = 0, const Lighting *lit = 0, u32 inPre = 0);
}


#endif
