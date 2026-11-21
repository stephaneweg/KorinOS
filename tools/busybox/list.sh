#!/bin/sh
# list.sh BUILD : les commandes d'une construction de BusyBox (include/applet_tables.h), une par ligne
sed -n 's/^\(.*\)"\\0" *$/\1/p; /applet_names\[\]/,/;/p' "$1/include/applet_tables.h" | tr -d ' "\\' | tr '0' '\n' | grep -v '^$\|applet_names\|const\|;\|{\|}' | sort -u
