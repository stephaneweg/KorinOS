# /etc/bash.bashrc (/etc/bash.bashrc) : lu par tout bash interactif, avant ~/.bashrc (docs/BASH.md)
[ -z "$PS1" ] && return
# l'invite : le dossier courant (\w : ~ pour HOME), en couleur
PS1='\[\e[1;32m\]korin\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]\$ '
# /sbin : les commandes d'administration (sudo commande ; docs/etude/MULTI-UTILISATEUR.md, 2.9)
case ":$PATH:" in *:/sbin:*) ;; *) PATH="$PATH:/sbin" ;; esac
# /devtools/rust/bin : cargo, puis rustc (docs/CARGO.md) ; le PATH par défaut de libposix l'a déjà
case ":$PATH:" in *:/devtools/rust/bin:*) ;; *) PATH="$PATH:/devtools/rust/bin" ;; esac
# une commande introuvable dans le PATH : l'app /apps/<nom>.app/main s'il y en a une (« paint » ouvre Paint),
# dans le même ordre que le vfs pour un nom seul (/bin, puis /apps ; docs/BASH.md)
command_not_found_handle() {
	if [ -x "/apps/$1.app/main" ]; then
		local app="$1"; shift
		"/apps/$app.app/main" "$@"
		return
	fi
	echo "bash: $1: command not found" >&2
	return 127
}
HISTSIZE=1000
HISTCONTROL=ignoredups
# ls en couleur et en colonnes sur un terminal (seul dans un tube : un nom par ligne, sans couleur)
alias ls='ls --color=auto -C'
alias ll='ls -l --color=auto'
alias la='ls -a --color=auto -C'
