# ztest.sh : quel tampon fait bloquer le GPU (docs/SDL.md). Lancer : sh SD0:/home/ztest.sh
# Chaque essai risque est limite a 2 images : le service v3d eteint le GPU apres
# plus de 3 blocages de suite ; un essai qui passe entre deux remet ce compte a zero.
T=SD0:/home/sdl2test
echo "== depart"; gpuinfo
echo "== A : deux jobs, couleur seule"; $T --gl --noread --nodepth --split --frames 30; gpuinfo
echo "== B : le premier job ecrit le Z, le second ne le relit pas"; $T --gl --noread --split-store --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== C : texture RGBA8, sans Z"; $T --gl --fbo c --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== D : texture RGBA8 + Z24S8"; $T --gl --fbo d24s8 --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== E : texture RGBA8 + Z16"; $T --gl --fbo d16 --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== F : texture RGBA8 + Z32F"; $T --gl --fbo d32f --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== fin"; gpuinfo
