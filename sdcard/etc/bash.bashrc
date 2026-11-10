# /etc/bash.bashrc (SD0:/etc/bash.bashrc) : lu par tout bash interactif, avant ~/.bashrc (docs/BASH.md)
[ -z "$PS1" ] && return
# l'invite : le dossier courant (\w : ~ pour HOME), en couleur
PS1='\[\e[1;32m\]korin\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]\$ '
HISTSIZE=1000
HISTCONTROL=ignoredups
