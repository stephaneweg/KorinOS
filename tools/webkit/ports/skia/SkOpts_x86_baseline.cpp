/*
 * SkOpts_x86_baseline.cpp -- Korin PC (x86-64) : la copie de Skia de src/extra (skia-m154) n'a pas les sources des
 * optimisations x86 choisies a l'execution (src/core/*_opts_ssse3.cpp, *_opts_hsw.cpp, SkOpts_hsw.cpp : retirees avec
 * le reste du x86 pour le Pi). SkOpts::Init et ses voisins les appellent quand SkCpu voit SSSE3 ou ML3 (AVX2, FMA :
 * SkCpu lit XCR0, donc jamais sur Korin PC, qui ne sauve que l'etat SSE). Ici elles ne font rien : Skia garde ses
 * chemins SSE2 de base (compiles d'office pour x86-64), justes, moins rapides que ceux de SSSE3 / AVX2.
 */
namespace SkOpts {
void Init_ml3() { }
void Init_Swizzler_ssse3() { }
void Init_Swizzler_ml3() { }
void Init_BitmapProcState_ssse3() { }
void Init_BlitMask_ssse3() { }
void Init_BlitRow_ml3() { }
}
