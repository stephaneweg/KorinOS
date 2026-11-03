# Essai de -j : a attend le fichier de b, ce qui echoue si make les lance l'un apres l'autre
par: a b
a:
	@i=0; while [ ! -e b.ok ] && [ $$i -lt 20 ]; do sleep 1; i=$$((i+1)); done; [ -e b.ok ]
	@echo a fini
b:
	@sleep 2
	@touch b.ok
	@echo b fini
.PHONY: par a b
