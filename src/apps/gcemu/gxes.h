//
// gxes.h -- le GX sur OpenGL ES 3.0 (Mesa, docs/EMULATEURS.md) : gc::GxGpu pour Machine::gpu, coupe en deux.
//
//   * Rec, sur le thread du GX : chaque draw est transforme, eclaire, ses coordonnees de texture faites sur le
//     processeur (le vertex shader de gxgl.cpp d'Onyx, en C++), dans l'espace de tout l'EFB. Une configuration du
//     TEV est une cle (TevCfg) ; le meme etat se fond dans un lot. Une copie d'EFB est un ordre du flux.
//   * Player, sur le thread qui tient le contexte GL : les ordres d'une image, dans leur ordre, sur un FBO de
//     640 x 528 : les lots, les copies vers des textures, l'image vers la fenetre.
//
// Pas de vertex shader genere : une image de The Wind Waker fait 4 800 draws de 5 sommets, Mesa n'en tiendrait
// pas autant ; fondus, il en reste 370. Le GPU ne fait que le TEV, le plan de gxv3d.h d'Onyx.
//
#ifndef GCEMU_GXES_H
#define GCEMU_GXES_H
#include "gc/gc.h"
#include <string.h>

namespace gxes
{
using gc::u8; using gc::s8; using gc::u32; using gc::s32; using gc::u64;

enum { EFB_W = 640, EFB_H = 528 };
enum { MAX_PROGS = 2048, MAX_FLOATS = 3 << 20, MAX_BATCHES = 8192, MAX_COPIES = 256, NFRAMES = 4 };
enum { MAX_CHUNKS = 8 };			// (vec4 apres la position : couleurs et coordonnees)

// une configuration du TEV : tout ce qui change le fragment shader. Comparee d'un bloc : remise a zero avant
struct TevCfg
{
	u32 nStages, nInd, iref, iss0, iss1;
	u32 cenv[16], aenv[16], tref[16], ksel[16], ind[16];
	u8 swap[16];
	u8 alphaFunc[2], alphaLogic, efbFmt, dstAlpha, projMask, fogSel, fogProj, fogRange, zop, zfmt, biasMask;
};

enum { V_C0, V_C1, V_ST, V_Q };
struct Vary { u8 kind, a, b; };			// V_C0 / V_C1 : composante a ; V_ST : coordonnee a, composante b ; V_Q : coordonnee a

struct Prog
{
	TevCfg cfg;
	u64 key;
	int nVary, chunks;			// (flottants apres la position ; vec4 qu'ils prennent)
	Vary vary[36];
	u32 mapMask;				// les cartes lues
	s8 c0At, c1At;				// (ou commence la couleur, en flottants ; -1 : aucune)
	s8 normMap[8];				// la carte par laquelle cette coordonnee est deja divisee, -1 : en texels (uIS)
	// (au thread GL)
	u32 program;				// 0 : pas encore, ~0u : refuse
	int uR, uK, uA, uIS, uF0, uF1, uF2, uFI, uIM;
};

// les uniformes d'un lot : ce que le TEV lit du GxState
struct Uni
{
	s32 regs[16], konst[16];
	s32 alpha[4];				// reference 0, 1, alpha de destination (0..255), biais de la texture de z
	s32 fogI[4]; float fogF[4], fogColor[4], fogK[12];
	s32 indMtx[12];
	float texInv[8][4];			// 1 / largeur, 1 / hauteur de chaque carte, biais de LOD, -
};

// gx_asm.asm : position -> oeil -> clip ; les offsets sont GP_*
struct GxPos
{
	const gc::GxVertex *vx; const u32 *xf; float *out; u8 *oc; float *eye;
	int nv, stride, proj; u32 lastPm;
	float ax, bx, ay, by, az, bz, P[6], VP0, VP1;
	float cols[4][4];
};
extern "C" void gx_positions (GxPos *p);
extern "C" u32 gx_expand (float *d, const float *sv, const u32 *idx, u32 prims, u32 per, u32 strideBytes, u32 nv, const u8 *oc);

enum { B_DRAW, B_COPY };
enum { P_TRI, P_LINE, P_POINT };
struct Batch
{
	u8 kind, prim, zon, zfunc, zwrite, cull, cmask, amask;
	u8 blendOn, blendSrc, blendDst, blendSub;
	int prog;
	u32 off, count;				// (off : sommets depuis le debut, a sa taille)
	int chunks;
	u32 uni;				// (dans Frame::u)
	s32 scissor[4];				// x0 y0 x1 y1 dans l'EFB
	float lp;
	int tex[8]; u32 mode0[8], mode1[8];	// (Machine::tex[], ou GX_TEX_COPY + n)
	int copy;				// (B_COPY : dans Frame::c)
};

struct Frame
{
	float *v; u32 nf;
	Batch *b; u32 nb;
	Uni *u; u32 nu;
	gc::GxCopy *c; u32 nc;
	void reset () { nf = nb = nu = nc = 0; }
};

class Rec : public gc::GxGpu
{
public:
	Prog *prog = 0; volatile int nProg = 0;
	Frame frame[NFRAMES];
	volatile u32 wr = 0, rd = 0;		// images finies, lues (le lecteur)
	volatile int quit = 0;			// (plus personne ne lit : ne plus attendre)
	u32 skipped = 0, dropped = 0;
	volatile u64 drawTicks = 0, drawVerts = 0, waitTicks = 0;

	bool init ();
	void draw (gc::Machine &m, const gc::GxState &s, const gc::GxVertex *vx, int nv, const u32 *idx, int ni, int prim) override;
	void copy (gc::Machine &m, const gc::GxCopy &c) override;
	void flushPlan () { pl.valid = false; }
	// le cache de <jeu>.gxs : une configuration deja vue, avant que la machine parte
	int addCfg (const TevCfg &c);

private:
	float *tmp = 0, *eyes = 0; u8 *oc = 0;
	float inv255[256];
	TevCfg lastCfg; int lastProg = -1;
	int findProg (const TevCfg &c);
	int newProg (const TevCfg &c, u64 k);
	Frame &cur () { return frame[wr % NFRAMES]; }

	// (les plans du draw d'avant : voir gxv3d.h d'Onyx, 90 % des draws gardent l'etat du precedent)
	struct Light { float col[4], cosA[3], distA[3], daN[3], pos[3], dir[3]; };
	struct ChanPlan { int ctl, dfn, atn, nL; bool matV, ambV; Light L[8]; };
	struct TgPlan { u32 info, pr; int srcSel, type, sr; bool three, norm; float post[12]; };
	struct VyPlan { int kind, a, coord; float scale, ts, inv; };
	struct Plan
	{
		bool valid; u32 serial, xfSerial;
		bool skip;
		int pi; Batch b; Uni u;
		float ax, bx, ay, by, az, bz, m0[4], m1[4], a0[4], a1[4], P0, P1, P2, P3, P4, P5, VP0, VP1;
		bool hasN, h0, h1, proj, lit0, lit1, lit0c, lit1c, lit0L, lit0aL, lit1L, lit1aL, a0Own, a1Own, needN, dual;
		int tMax, c0End, c1End, lmask, vtx1, nVary;
		ChanPlan cp0, cp0a, cp1, cp1a;
		TgPlan tp[8]; VyPlan vp[36];
		float Mp[12], Mn[9], Mt[8][12]; u32 lastPm, lastPn, lastTm[8];
		float cols[4][4];
		u32 lastWr, lastNb; int lastPrim;
	};
	Plan pl;
	Light lt[8];
	void readLights (const gc::Machine &m, int mask);
	void chanPlan (ChanPlan &c, int ctl) const;
	static void chanEval (const ChanPlan &c, const float regMat[4], const float regAmb[4], const float vcol[4],
			      const float pos[3], const float n[3], float out[4], int c0, int c1);
	void planState (gc::Machine &m, const gc::GxState &s);
	void planXf (gc::Machine &m, const gc::GxState &s);
};

// le lecteur : tout appel GL se fait ici, sur un seul thread
class Player
{
public:
	struct Stats { u32 frames, skipped, batches, programs, compiled, fallback, failed, copies, uploads; u64 compileUs, maxCompileUs; const char *err; };
	Stats st;
	int budgetUs = 6000;			// le temps de compilation permis par image : au-dela, le repli
	bool init (void *(*proc) (const char *));
	// les images en attente -> 1 : une image a montrer (present vient d'etre appele)
	int pump (Rec &r, gc::Machine &m, void (*present) (void *), void *ctx, int winW, int winH, unsigned defaultFbo);
	// l'image du VI (le jeu ecrit lui-meme dans le XFB) vers la fenetre
	void showFb (const unsigned *px, int w, int h, int pitch, void (*present) (void *), void *ctx, int winW, int winH, unsigned defaultFbo);
	// (essais) l'EFB, 0xAARRGGBB, ligne 0 d'abord
	void readEfb (unsigned *px, int x, int y, int w, int h);
	// le programme n et sa variante pour l'EFB, avant son premier lot -> 1 : fait, 0 : refuse
	int precompile (Rec &r, int index);
	int lastW = 0, lastH = 0;
	unsigned *grab = 0;			// (essais) l'EFB de chaque image, avant qu'elle l'efface
private:
	struct Impl; Impl *p = 0;
};

// (essais) le fragment shader d'une configuration, en GLSL ES
int fragmentSource (const Prog &p, char *out, int cap);

} // namespace gxes
#endif
