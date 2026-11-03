# FASMARM natif AArch64 pour ARMPI

`fasmarm.asm` est **généré** par `tools/x86toa64.py` (traduction source à source du code x86 de
fasm 1.73.30 + FASMARM 1.44). Ne pas l'éditer à la main.

Régénérer (depuis la racine du dépôt) :

    mkdir build/fasmsrc
    copier ref/fasm17330/w/SOURCE/*.INC, tools/fasmarm/source/*.inc et
    ref/fasm17330/w/SOURCE/LINUX/SYSTEM.INC dans build/fasmsrc
    cd src/bin/fasmarm/x86
    python -I ../../../../tools/x86toa64.py armpi_main.asm ../fasmarm.asm ../../../../build/fasmsrc

Fichiers : open, read, write, seek, close passent par le client de fichiers d'AppKit
(`fichier_sys`, trouvé par `lib_open` au démarrage ; le noyau si AppKit manque : gabarit
`RUNTIME` de `x86toa64.py`, docs/FICHIERS.md 5.1). L'ordre des tables générées dépend du hachage
de Python (`PYTHONHASHSEED`) : deux régénérations diffèrent par des numéros de gestionnaires
permutés, sans effet sur le résultat.

Assembler : `fasmarm.exe fasmarm.asm fasmarm.elf` (≈0,3 s, 464 821 octets), puis copier
`fasmarm.elf` dans `/sd/apps/`. Sous ARMPI : `selfhost` ou spawn de `/sd/apps/fasmarm.elf`
avec la ligne `source.asm sortie`. Options fasm habituelles (`-m <Ko>` pour plus de mémoire).

Validé bit à bit contre fasmarm.exe (Windows) : hello.asm, les 10 fichiers armdoc, le noyau
ARMPI ; et FASMARM traduit se réassemble lui-même à l'identique.
