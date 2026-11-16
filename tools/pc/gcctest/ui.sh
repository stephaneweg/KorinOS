# ui.sh : une app UIKit compilée dans Korin PC puis ouverte (bureau Elegant, lancé par
# /etc/startup.txt de l'arbre d'essai : /bin/sh /devtools/gcc/test/pc/ui.sh)
mkdir -p /tmp/t
/devtools/gcc/bin/gcc -O2 -I/gcc/include/kits /devtools/gcc/test/pc/uiapp.c -luikit -o /tmp/t/uiapp
echo "ui.sh : uiapp compilée"
/tmp/t/uiapp
