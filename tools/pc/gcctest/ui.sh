# ui.sh : une app UIKit compilée dans Korin PC puis ouverte (bureau Elegant, lancé par
# SD0:/etc/startup.txt de l'arbre d'essai : SD0:/bin/sh /gcc/test/pc/ui.sh)
mkdir -p /tmp/t
/gcc/bin/gcc -O2 -I/gcc/include/kits /gcc/test/pc/uiapp.c -luikit -o /tmp/t/uiapp
echo "ui.sh : uiapp compilée"
/tmp/t/uiapp
