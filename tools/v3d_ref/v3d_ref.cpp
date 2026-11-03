// Reference : listes V3D construites avec les paquets d'Onyx (kern/v3d_cl.h, valides sur Pi 4),
// selon Draw/BuildRCL de kernel/sys/v3d.cpp. Variante ARMPI : pas d'uniforms/record/draw si n == 0,
// chargement optionnel depuis une source distincte. Sortie : lignes BCL/IND/RCL en hex.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "v3d_cl.h"

struct List { u8 b[1 << 17]; u32 n = 0; u32 base;
  template <typename T> List &operator<< (const T &x) { memcpy (b + n, &x, sizeof (T)); n += sizeof (T); return *this; }
  void Align (u32 a) { while (n % a) *this << (u8) 0; }
  u32 Bus () const { return base + n; } };

static List B, R, I;
static void dump (const char *t, List &L) { printf ("V3DT %s ", t); for (u32 i = 0; i < L.n; i++) printf ("%02X", L.b[i]); printf ("\n"); }

int main (int argc, char **argv)
{
  u32 bcl = strtoul (argv[1], 0, 0), rcl = strtoul (argv[2], 0, 0), ind = strtoul (argv[3], 0, 0),
      state = strtoul (argv[4], 0, 0), verts = strtoul (argv[5], 0, 0), talloc = strtoul (argv[6], 0, 0);
  struct C { u32 clear, tgt, stride, w, h, src, sstride, n; } cs[] = {
    { 0x00336699, 0x3C1640C8, 4096, 300, 200, 0, 0, 0 },
    { 0, 0x21000000, 4096, 130, 70, 0x20000000, 2048, 0 },
    { 0x00101828, 0x3C100000, 4096, 640, 480, 0, 0, 3 },
    { 0x00FF8000, 0x10000000, 8192, 2048, 2048, 0, 0, 0 } };
  for (auto &c : cs) {
    B.n = R.n = I.n = 0; B.base = bcl; R.base = rcl; I.base = ind;
    int w = c.w, h = c.h; u32 tilesX = (w + 63) / 64, tilesY = (h + 63) / 64;
    u32 rec = 0;
    if (c.n) {
      u32 nFragUnif = I.Bus (), nVtxUnif = I.Bus ();
      I << 1.0f << (float) (w / 2) * 256.0f << (float) (h / 2) * -256.0f << 0.5f << 0.5f;
      u32 nCoordUnif = I.Bus ();
      I << 1.0f << (float) (w / 2) * 256.0f << (float) (h / 2) * -256.0f;
      I.Align (32); rec = I.Bus ();
      GLShaderStateRecord Rec {};
      Rec.enable_clipping = true; Rec.fragment_shader_uses_real_pixel_centre_w_in_addition_to_centroid_w2 = true;
      Rec.disable_implicit_point_line_varyings = true; Rec.number_of_varyings_in_fragment_shader = 4;
      Rec.coordinate_shader_output_vpm_segment_size = 1; Rec.coordinate_shader_input_vpm_segment_size = 1;
      Rec.vertex_shader_output_vpm_segment_size = 1; Rec.vertex_shader_input_vpm_segment_size = 1;
      Rec.address_of_default_attribute_values = state;
      Rec.fragment_shader_code_address = (state + 256) >> 3; Rec.fragment_shader_uniforms_address = nFragUnif;
      Rec.fragment_shader_4_way_threadable = true; Rec.fragment_shader_start_in_final_thread_section = false; Rec.fragment_shader_propagate_nans = true;
      Rec.vertex_shader_code_address = (state + 512) >> 3; Rec.vertex_shader_uniforms_address = nVtxUnif;
      Rec.vertex_shader_4_way_threadable = true; Rec.vertex_shader_start_in_final_thread_section = true; Rec.vertex_shader_propagate_nans = true;
      Rec.coordinate_shader_code_address = (state + 1024) >> 3; Rec.coordinate_shader_uniforms_address = nCoordUnif;
      Rec.coordinate_shader_4_way_threadable = true; Rec.coordinate_shader_start_in_final_thread_section = true; Rec.coordinate_shader_propagate_nans = true;
      I << Rec;
      GlShaderStateAttributeRecord Pos {}; Pos.address = verts; Pos.number_of_values_read_by_vertex_shader = 3;
      Pos.number_of_values_read_by_coordinate_shader = 3; Pos.stride = 16; Pos.maximum_index = 0xFFFFFF; Pos.vec_size = 3; Pos.type = 2;
      I << Pos;
      GlShaderStateAttributeRecord Col {}; Col.address = verts + 12; Col.number_of_values_read_by_vertex_shader = 4;
      Col.number_of_values_read_by_coordinate_shader = 0; Col.stride = 16; Col.maximum_index = 0xFFFFFF; Col.type = 4; Col.normalized_int_type = true;
      I << Col;
    }
    B << TileBinningModeCfg (0, 0, 1, 0, false, false, (u16) w, (u16) h);
    B << OP_FLUSH_VCD_CACHE << OcclusionQueryCounter (0) << OP_START_TILE_BINNING << ClipWindow (0, 0, (u16) w, (u16) h);
    B << CfgBits (true, true, true, false, 0, 0, false, 1, true, false, false, false, false, false, false);
    B << PointSize (1.0f) << LineWidth (1.0f) << ClipperXYScaling ((float) (w / 2) * 256.0f, (float) (h / 2) * -256.0f);
    B << ClipperZScaleAndOffset (0.5f, 0.5f) << CLipperZMinMaxClippingPlanes (0.0f, 1.0f) << ViewportOffset ((float) (w / 2), (float) (h / 2), 0, 0);
    B << ColorWriteMasks (0x8) << BlendConstantColor (0, 0, 0, 0);
    B << OP_ZERO_ALL_FLAT_SHADE_FLAGS << OP_ZERO_ALL_NON_PERSPECTIVE_FLAGS << OP_ZERO_ALL_CENTROID_FLAGS;
    B << TransformFeedbackSpecs (0, false) << OcclusionQueryCounter (0) << SampleState (0xF, 1.0f) << VcmCacheSize (4, 4);
    if (c.n) B << GlShaderState (rec, 2) << VertexArrayPrims (4, c.n, 0);
    B << OP_FLUSH;
    // RCL (BuildRCL, cible directe)
    u32 nClear = c.clear, nClearRGBA = ((nClear & 0xFF) << 16) | (nClear & 0xFF00) | ((nClear >> 16) & 0xFF);
    R << TileRenderingModeCfgCommon (1, (u16) w, (u16) h, 0, false, false, 0, false, 2, false);
    R << TileRenderingModeCfgClearColorsPart1 (0, nClearRGBA, 0);
    R << TileRenderingModeCfgColor (0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    R << TileRenderingModeCfgZSClearValues (0, 1.0f);
    R << TileListInitialBlockSize (0, true);
    u32 stW = 1, stH = 1, fW, fH;
    for (;;) { fW = (tilesX + stW - 1) / stW; fH = (tilesY + stH - 1) / stH; if (fW * fH < 256) break; if (stW < stH) stW++; else stH++; }
    R << MulticoreRenderingTileListSetBase (talloc);
    R << MulticoreRenderingSupertileCfg (stW, stH, fW, fH, tilesX, tilesY, 1, false, false);
    R << TileCoordinates (0, 0) << OP_END_OF_LOADS << StoreTileBufferGeneral () << ClearTileBuffers (true, true) << OP_END_OF_TILE_MARKER;
    R << TileCoordinates (0, 0) << OP_END_OF_LOADS << StoreTileBufferGeneral () << OP_END_OF_TILE_MARKER;
    R << OP_FLUSH_VCD_CACHE;
    u32 nGeneric = I.Bus ();
    I << OP_TILE_COORDINATES_IMPLICIT;
    if (c.src) I << V3dLoadTarget (true, false, c.sstride, c.src);
    I << OP_END_OF_LOADS << PrimListFormat (LIST_TRIANGLES, false) << BranchToImplicitTileList (0);
    I << V3dStoreTarget (true, c.stride, c.tgt) << ClearTileBuffers (true, true) << OP_END_OF_TILE_MARKER << OP_RETURN_FROM_SUB_LIST;
    R << StartAddressOfGenericTileList (nGeneric, I.Bus ());
    for (u32 y = 0; y <= (u32) (h - 1) / (64 * stH); y++)
      for (u32 x = 0; x <= (u32) (w - 1) / (64 * stW); x++)
        R << SupertileCoorinates ((u8) x, (u8) y);
    R << OP_END_OF_RENDERING;
    dump ("BCL", B); dump ("IND", I); dump ("RCL", R);
  }
  printf ("V3DT FIN\n");
}
