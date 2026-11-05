# ztest2.sh : deuxieme serie (docs/SDL.md), avec le glkit.so qui dessine dans une image UIF.
# Lancer : sh SD0:/home/ztest2.sh ; essais risques limites a 2 images, un essai sain entre deux.
T=SD0:/home/sdl2test
echo "== depart"; gpuinfo
echo "== G : un job par image, Z jete (temoin)"; $T --gl --noread --frames 30; gpuinfo
echo "== H : fenetre, le 1er job ecrit le Z, le 2e ne le relit pas"; $T --gl --noread --split-store --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== I : texture + Z24S8, deux jobs, le 2e relit le Z"; $T --gl --fbo d24s8 --split --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== J : texture + Z24S8, deux jobs, le 2e ne relit pas le Z"; $T --gl --fbo d24s8 --split-store --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== K : texture sans Z, deux jobs"; $T --gl --fbo c --split --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== L : texture + Z16, deux jobs, le 2e relit le Z"; $T --gl --fbo d16 --split --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== fin"; gpuinfo
