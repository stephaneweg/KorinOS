// Reference de « gputest lists » (GpuKit gk_texture / gk_render, src/kits/gpukit/render.asm) :
// les memes textures et images, construites avec les paquets d'Onyx (kern/v3d_cl.h, valides
// sur Pi 4), le rangement des texels d'Onyx (kern/v3d_tiling.h) et la recette de Render
// (kernel/sys/v3d.cpp), avec les choix d'ARMPI : etat de texture copie dans la liste
// indirecte apres le sampler, cible toujours « directe » (tampon d'etape sinon).
// Entree : la sortie de gputest lists (adresses physiques des lignes TEX / TGT / WK) ;
// sortie : les lignes V3DT attendues.
//   g++ -std=c++20 -I. -Istub gk_ref.cpp -o gk_ref && ./gk_ref < sortie.txt > attendu.txt
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include "v3d_cl.h"
#include "v3d_tiling.h"

static const u32 WK_BCL = 96 * 1024, WK_RCL = 160 * 1024, WK_IND = 176 * 1024, WK_VERTS = 768 * 1024;
static const u32 WK_TSTATE = WK_VERTS + 3 * 65536 * 32, WK_TALLOC = WK_TSTATE + 256 * 1024;
static const u32 ST_VS_CLIP = 2048, ST_CS_CLIP = ST_VS_CLIP + 1024, ST_FS_COLOR = ST_CS_CLIP + 1024,
		 ST_FS_TEX = ST_FS_COLOR + 512, ST_FS_COLOR_AT = ST_FS_TEX + 512, ST_FS_TEX_AT = ST_FS_COLOR_AT + 512;

struct List { u8 b[1 << 20]; u32 n = 0; u32 base;
  template <typename T> List &operator<< (const T &x) { memcpy (b + n, &x, sizeof (T)); n += sizeof (T); return *this; }
  void Align (u32 a) { while (n % a) *this << (u8) 0; }
  u32 Bus () const { return base + n; } };
static List B, R, I;
static void dump (const char *t, List &L) { printf ("V3DT %s ", t); for (u32 i = 0; i < L.n; i++) printf ("%02X", L.b[i]); printf ("\n"); }

static void SetBits (u8 *p, u32 nStart, u32 nSize, u32 v)
{ for (u32 i = 0; i < nSize; i++) if (v & (1u << i)) p[(nStart + i) / 8] |= (u8) (1 << ((nStart + i) % 8)); }

static unsigned pattern (u32 x, u32 y) { return ((x * 7 + y * 131) & 0xFFFFFF) | (((x + y) & 0xFF) << 24); }

struct Tex { u32 w, h, phys; u8 state[32]; };
static Tex tex[7];
static const u32 dims[7][2] = { { 100, 70 }, { 7, 20 }, { 13, 9 }, { 3, 5 }, { 40, 256 }, { 24, 232 }, { 20, 264 } };

// gk_texture : texels + TEXTURE_SHADER_STATE (Onyx Texture ())
static void MakeTex (int i, u32 phys)
{
  Tex &t = tex[i]; t.w = dims[i][0]; t.h = dims[i][1]; t.phys = phys;
  TLayout L; Layout (t.w, t.h, L);
  u32 bytes = L.nPadW * L.nPadH * 4, pages = (bytes + 4095) / 4096;
  std::vector<u8> m (pages * 4096, 0);
  for (u32 y = 0; y < t.h; y++)
    for (u32 x = 0; x < t.w; x++) {
      u32 c = pattern (x, y);
      *(u32 *) (m.data () + TexelOffset (L, x, y)) = (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
    }
  u32 h = 0x811C9DC5; for (u8 b : m) { h ^= b; h *= 0x01000193; }
  printf ("V3DT TEX %d %08X %u %08X\n", i, phys, pages * 4096, h);
  u8 *pS = t.state; memset (pS, 0, 32);
  memcpy (pS, &phys, 4);
  SetBits (pS, 58, 14, t.w); SetBits (pS, 72, 14, t.h); SetBits (pS, 86, 14, 1); SetBits (pS, 100, 7, 4);
  SetBits (pS, 108, 3, 2); SetBits (pS, 111, 3, 3); SetBits (pS, 114, 3, 4); SetBits (pS, 117, 3, 5);
  if (L.nKind == TL_UIF) { SetBits (pS, 107, 1, 1); SetBits (pS, 128, 4, L.nUbPad); SetBits (pS, 132, 1, L.bXor ? 1 : 0); SetBits (pS, 134, 1, 1); }
}

struct Batch { u32 first, count; int tex; u32 flags; float m[16]; };
static const u32 B_2D = 7 | (1 << 3) | (1 << 17) | (1 << 13) | (1 << 15);
static const float Ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static const float Half[16] = { 0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static Batch mk (u32 first, u32 count, int t, u32 flags, const float *m)
{ Batch b; b.first = first; b.count = count; b.tex = t; b.flags = flags; memcpy (b.m, m, 64); return b; }

static void Render (int w, int h, u32 clear, u32 flags, u32 tgt, u32 strideBytes, u32 wk, const std::vector<Batch> &bs)
{
  B.n = R.n = I.n = 0; B.base = wk + WK_BCL; R.base = wk + WK_RCL; I.base = wk + WK_IND;
  bool bKeep = flags & 1, bAlpha = flags & 2;
  float fXs = (float) (w / 2) * 256.0f, fYs = (float) (h / 2) * -256.0f;
  std::vector<u32> recs;
  for (const Batch &b : bs) {			// la liste indirecte d'abord (comme le kit)
    if (b.count == 0) { recs.push_back (0); continue; }
    const float *M = (b.flags & (1u << 17)) ? Ident : b.m;
    I.Align (4);
    u32 nVtxUnif = I.Bus ();
    for (int k = 0; k < 16; k++) I << M[k];
    I << fXs << fYs << 0.5f << 0.5f;
    u32 nCoordUnif = I.Bus ();
    for (int k = 0; k < 16; k++) I << M[k];
    I << fXs << fYs;
    u32 nFragUnif = I.Bus ();
    if (b.tex >= 0) {
      I.Align (32);
      u32 nSampler = I.Bus ();
      u8 S[32]; memset (S, 0, sizeof S);
      bool bLinear = (b.flags & (1u << 12)) != 0;
      static const u32 Wrap[4] = { 0, 1, 2, 0 };
      SetBits (S, 0, 1, bLinear ? 0 : 1); SetBits (S, 1, 1, bLinear ? 0 : 1); SetBits (S, 2, 1, 1);
      SetBits (S, 48, 3, Wrap[(b.flags >> 13) & 3]); SetBits (S, 51, 3, Wrap[(b.flags >> 15) & 3]); SetBits (S, 54, 3, 1);
      for (unsigned k = 0; k < sizeof S; k++) I << S[k];
      u32 nState = I.Bus ();
      for (int k = 0; k < 32; k++) I << tex[b.tex].state[k];
      nFragUnif = I.Bus ();
      I << (u32) (nState | 3);
      I << nSampler;
    }
    bool bAT = (b.flags & (1u << 18)) != 0;
    if (bAT) I << (float) ((b.flags >> 19) & 255) / 255.0f;
    if (b.tex < 0 && !bAT) I << 0u;
    I.Align (32);
    u32 nShaderRec = I.Bus ();
    recs.push_back (nShaderRec);
    GLShaderStateRecord Rec {};
    Rec.enable_clipping = true;
    Rec.fragment_shader_uses_real_pixel_centre_w_in_addition_to_centroid_w2 = true;
    Rec.disable_implicit_point_line_varyings = true;
    Rec.number_of_varyings_in_fragment_shader = 10;
    Rec.coordinate_shader_output_vpm_segment_size = 1; Rec.coordinate_shader_input_vpm_segment_size = 1;
    Rec.vertex_shader_output_vpm_segment_size = 2; Rec.vertex_shader_input_vpm_segment_size = 2;
    Rec.address_of_default_attribute_values = wk;
    Rec.fragment_shader_code_address = (wk + (b.tex >= 0 ? (bAT ? ST_FS_TEX_AT : ST_FS_TEX) : (bAT ? ST_FS_COLOR_AT : ST_FS_COLOR))) >> 3;
    Rec.fragment_shader_uniforms_address = nFragUnif;
    Rec.fragment_shader_4_way_threadable = true; Rec.fragment_shader_start_in_final_thread_section = false; Rec.fragment_shader_propagate_nans = true;
    Rec.vertex_shader_code_address = (wk + ST_VS_CLIP) >> 3; Rec.vertex_shader_uniforms_address = nVtxUnif;
    Rec.vertex_shader_4_way_threadable = true; Rec.vertex_shader_start_in_final_thread_section = true; Rec.vertex_shader_propagate_nans = true;
    Rec.coordinate_shader_code_address = (wk + ST_CS_CLIP) >> 3; Rec.coordinate_shader_uniforms_address = nCoordUnif;
    Rec.coordinate_shader_4_way_threadable = true; Rec.coordinate_shader_start_in_final_thread_section = true; Rec.coordinate_shader_propagate_nans = true;
    I << Rec;
    GlShaderStateAttributeRecord A {};
    A.address = wk + WK_VERTS; A.number_of_values_read_by_vertex_shader = 4; A.number_of_values_read_by_coordinate_shader = 4;
    A.stride = 32; A.maximum_index = 0xFFFFFF; A.vec_size = 0; A.type = 2;
    I << A;
    A.address = wk + WK_VERTS + 16; A.number_of_values_read_by_vertex_shader = 2; A.number_of_values_read_by_coordinate_shader = 0; A.vec_size = 2;
    I << A;
    A.address = wk + WK_VERTS + 24; A.number_of_values_read_by_vertex_shader = 4; A.vec_size = 0; A.type = 4; A.normalized_int_type = true;
    I << A;
    A.address = wk + WK_VERTS + 28;
    I << A;
  }
  B << TileBinningModeCfg (0, 0, 1, 0, false, false, (u16) w, (u16) h);
  B << OP_FLUSH_VCD_CACHE << OcclusionQueryCounter (0) << OP_START_TILE_BINNING << ClipWindow (0, 0, (u16) w, (u16) h);
  B << PointSize (1.0f) << LineWidth (1.0f) << ClipperXYScaling (fXs, fYs);
  B << ClipperZScaleAndOffset (0.5f, 0.5f) << CLipperZMinMaxClippingPlanes (0.0f, 1.0f) << ViewportOffset ((float) (w / 2), (float) (h / 2), 0, 0);
  B << ColorWriteMasks (!bAlpha ? 0x8 : 0) << BlendConstantColor (0, 0, 0, 0);
  B << OP_ZERO_ALL_FLAT_SHADE_FLAGS << OP_ZERO_ALL_NON_PERSPECTIVE_FLAGS << OP_ZERO_ALL_CENTROID_FLAGS;
  B << TransformFeedbackSpecs (0, false) << OcclusionQueryCounter (0) << SampleState (0xF, 1.0f) << VcmCacheSize (4, 4);
  u32 nPrevBlend = ~0u;
  for (size_t i = 0; i < bs.size (); i++) {
    const Batch &b = bs[i];
    if (b.count == 0) continue;
    u32 nZ = b.flags & 7; if (nZ == 0) nZ = 1;
    u32 nBlend = (b.flags >> 8) & 15;
    bool bZWrite = !(b.flags & (1u << 3)) && nZ != 7;
    B << CfgBits (!(b.flags & (1u << 5)), !(b.flags & (1u << 4)), true, false, 0, 0, false, nZ, bZWrite, false, false, false, nBlend != 0, false, false);
    if (nBlend != 0 && nBlend != nPrevBlend) {
      static const u8 Fac[13][6] = {
        { 1, 0, 0, 1, 0, 0 }, { 6, 7, 0, 1, 7, 0 }, { 6, 1, 0, 6, 1, 0 }, { 4, 0, 0, 4, 0, 0 }, { 1, 7, 0, 1, 7, 0 },
        { 4, 7, 0, 0, 1, 0 }, { 9, 1, 0, 1, 7, 0 }, { 1, 3, 0, 1, 7, 0 }, { 1, 1, 0, 1, 7, 0 }, { 1, 1, 2, 0, 1, 0 },
        { 1, 1, 4, 1, 7, 0 }, { 0, 6, 0, 0, 6, 0 }, { 0, 7, 0, 0, 7, 0 } };
      const u8 *f = Fac[nBlend];
      B << BlendEnables (1);
      B << BlendCfg (f[5], f[3], f[4], f[2], f[0], f[1], 0xF);
      nPrevBlend = nBlend;
    }
    B << GlShaderState (recs[i], 4);
    B << VertexArrayPrims (4, b.count, b.first);
  }
  B << OP_FLUSH;
  // RCL (BuildRCL, cible directe)
  u32 tilesX = (w + 63) / 64, tilesY = (h + 63) / 64;
  u32 nClearA = bAlpha ? clear & 0xFF000000 : 0;
  u32 nClearRGBA = nClearA | ((clear & 0xFF) << 16) | (clear & 0xFF00) | ((clear >> 16) & 0xFF);
  R << TileRenderingModeCfgCommon (1, (u16) w, (u16) h, 0, false, false, 0, false, 2, false);
  R << TileRenderingModeCfgClearColorsPart1 (0, nClearRGBA, 0);
  R << TileRenderingModeCfgColor (0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  R << TileRenderingModeCfgZSClearValues (0, 1.0f);
  R << TileListInitialBlockSize (0, true);
  u32 stW = 1, stH = 1, fW, fH;
  for (;;) { fW = (tilesX + stW - 1) / stW; fH = (tilesY + stH - 1) / stH; if (fW * fH < 256) break; if (stW < stH) stW++; else stH++; }
  R << MulticoreRenderingTileListSetBase (wk + WK_TALLOC);
  R << MulticoreRenderingSupertileCfg (stW, stH, fW, fH, tilesX, tilesY, 1, false, false);
  R << TileCoordinates (0, 0) << OP_END_OF_LOADS << StoreTileBufferGeneral () << ClearTileBuffers (true, true) << OP_END_OF_TILE_MARKER;
  R << TileCoordinates (0, 0) << OP_END_OF_LOADS << StoreTileBufferGeneral () << OP_END_OF_TILE_MARKER;
  R << OP_FLUSH_VCD_CACHE;
  u32 nGeneric = I.Bus ();
  I << OP_TILE_COORDINATES_IMPLICIT;
  if (bKeep) I << V3dLoadTarget (true, bAlpha, strideBytes, tgt);
  I << OP_END_OF_LOADS << PrimListFormat (LIST_TRIANGLES, false) << BranchToImplicitTileList (0);
  I << V3dStoreTarget (true, strideBytes, tgt) << ClearTileBuffers (true, true) << OP_END_OF_TILE_MARKER << OP_RETURN_FROM_SUB_LIST;
  R << StartAddressOfGenericTileList (nGeneric, I.Bus ());
  for (u32 y = 0; y <= (u32) (h - 1) / (64 * stH); y++)
    for (u32 x = 0; x <= (u32) (w - 1) / (64 * stW); x++)
      R << SupertileCoorinates ((u8) x, (u8) y);
  R << OP_END_OF_RENDERING;
  printf ("V3DT R 0\nV3DT TGT %08X\nV3DT WK %08X\n", tgt, wk);
  dump ("BCL", B); dump ("IND", I); dump ("RCL", R);
}

int main ()
{
  char line[1 << 16]; std::vector<u32> tgts, wks; u32 texPhys[7] = { 0 };
  while (fgets (line, sizeof line, stdin)) {
    char *p = strstr (line, "V3DT ");
    if (!p) continue;
    int i; u32 a;
    if (sscanf (p, "V3DT TEX %d %x", &i, &a) == 2 && i >= 0 && i < 7) texPhys[i] = a;
    else if (sscanf (p, "V3DT TGT %x", &a) == 1) tgts.push_back (a);
    else if (sscanf (p, "V3DT WK %x", &a) == 1) wks.push_back (a);
  }
  if (tgts.size () < 3 || wks.size () < 3) { fprintf (stderr, "gk_ref : 3 images attendues\n"); return 1; }
  for (int i = 0; i < 7; i++) MakeTex (i, texPhys[i]);
  Render (300, 200, 0x00112233, 0, tgts[0], 300 * 4, wks[0], {
    mk (0, 6, 0, B_2D | (1u << 12) | (4u << 8), Ident),
    mk (6, 3, -1, 0, Half),
    mk (9, 3, 1, (1u << 17) | (1u << 4) | (1u << 18) | (128u << 19) | (5u << 8), Ident),
    mk (0, 0, -1, 0, Ident),
    mk (0, 3, 2, (1u << 17) | 4 | (1u << 5) | (4u << 8), Ident) });
  Render (130, 70, 0x80FF0000, 3, tgts[1], 160 * 4, wks[1], {
    mk (0, 6, 4, 7 | (1u << 17) | (2u << 15) | (12u << 8), Ident) });
  Render (64, 64, 0, 1, tgts[2], 64 * 4, wks[2], {
    mk (9, 3, -1, 7 | (1u << 17) | (1u << 18) | (10u << 19) | (2u << 8), Ident) });
  printf ("V3DT BAD -2\nV3DT FIN\n");
}
