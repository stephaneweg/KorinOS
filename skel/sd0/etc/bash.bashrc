# /etc/bash.bashrc (SD0:/etc/bash.bashrc) : lu par tout bash interactif, avant ~/.bashrc (docs/BASH.md)
[ -z "$PS1" ] && return
# l'invite : le dossier courant (\w : ~ pour HOME), en couleur
PS1='\[\e[1;32m\]korin\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]\$ '
HISTSIZE=1000
HISTCONTROL=ignoredups
# ls en couleur et en colonnes sur un terminal (seul dans un tube : un nom par ligne, sans couleur)
alias ls='ls --color=auto -C'
alias ll='ls -l --color=auto'
alias la='ls -a --color=auto -C'
