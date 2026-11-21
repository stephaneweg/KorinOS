regex -- regcomp, regexec, regerror, regfree de libposix (docs/POSIX.md) : le moteur de musl 1.2.5
(src/regex, d'après TRE de Ville Laurikari), licence MIT (COPYRIGHT-musl), tel quel sauf :
- tre.h : la macro « hidden » définie ici (musl la prend dans ses en-têtes internes) ;
- regerror.c : sans locale_impl.h ni LCTRANS_CUR (messages en anglais, comme sans traduction).
L'en-tête public est include/regex.h. Ajouté le 21/11/2026 pour BusyBox (awk, sed, grep, expr, find -regex).
