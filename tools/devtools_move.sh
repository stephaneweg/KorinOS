#!/bin/sh
# devtools_move.sh ARBRE : range les outils de développement d'un arbre de carte sous devtools/
# (15/11/2026, arbre unique : /devtools/gcc, /devtools/dotnet, /devtools/python, pour ne pas
# encombrer la racine). Les scripts de tools/ construisent toujours gcc/, dotnet/ et python/ à la
# racine de leur arbre ; ceux qui assemblent la carte (tools/pc/sysroot*.sh) l'appellent à la fin.
# Fusionne : un second passage (la CAO ajoute à gcc/) complète devtools/gcc sans l'effacer.
set -e
T=${1:?usage : sh tools/devtools_move.sh ARBRE}
for d in gcc dotnet python; do
	[ -d "$T/$d" ] || continue
	mkdir -p "$T/devtools/$d"
	cp -r "$T/$d/." "$T/devtools/$d/"
	rm -rf "$T/$d"
done
