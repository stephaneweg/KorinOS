# ztest3.sh : troisieme serie (docs/SDL.md) : la taille de la cible avec Z.
# Lancer : sh SD0:/home/ztest3.sh ; essais risques limites a 2 images, un essai sain entre deux.
T=SD0:/home/sdl2test
echo "== depart"; gpuinfo
echo "== N : texture + Z24S8, 800 x 600"; $T --gl --fbo d24s8 --fbosize 800 600 --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== O : texture + Z24S8, 1280 x 720"; $T --gl --fbo d24s8 --fbosize 1280 720 --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== P : texture + Z24S8, 1920 x 1080"; $T --gl --fbo d24s8 --fbosize 1920 1080 --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== Q : texture sans Z, 1920 x 1080"; $T --gl --fbo c --fbosize 1920 1080 --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== R : texture + Z16, 1920 x 1080"; $T --gl --fbo d16 --fbosize 1920 1080 --frames 2; gpuinfo
$T --gl --noread --frames 5
echo "== fin"; gpuinfo
