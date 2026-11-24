# Chaine d'outils de CMake pour llama.cpp construit dans Korin PC (tools/llamacpp/build.sh).
#  * uname -m repond « armpi ? » : sans CMAKE_SYSTEM_PROCESSOR, ggml se croit sur ARM et compile ses noyaux ARM ;
#    nommer le systeme (ARMPI, Modules/Platform/ARMPI.cmake du CMake de Korin) suffit a ce que le processeur donne ici
#    soit pris ;
#  * newlib ne declare wcwidth (common/console.cpp), strcasestr... qu'avec une macro de fonctions : _GNU_SOURCE.
set(CMAKE_SYSTEM_NAME ARMPI)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER /devtools/gcc/bin/gcc)
set(CMAKE_CXX_COMPILER /devtools/gcc/bin/g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_C_FLAGS_INIT "-D_GNU_SOURCE")
set(CMAKE_CXX_FLAGS_INIT "-D_GNU_SOURCE")
