# Memoire de la session n64-mesa

Copie de la memoire de travail, sauvegardee a la demande de l'utilisateur le 3/11/2026.

Dans main depuis le 2/11/2026 ; branche et worktree supprimes le 3/11. Doc : docs/EMULATEURS.md, « Le rendu N64 par Mesa ». Le coeur asm depose des donnees brutes (n64_gl.inc, m_glOn) ; src/apps/n64emu/gl/n64gl.c parle a GL et genere les shaders. Lien : tools/emu/build-n64.sh, n64link.py pose l'ELF FASMARM (assemble a 0x4004000000) dans l'edition de liens C, sans relocation.

Prouve sans GPU : check_n64, n64gl_unit (llvmpipe, 13 200 pixels 0 ecart), images rejouees sur le PC, n64gltest a blanc, qpusim. **Pas prouve : rendu par le vrai GPU, temps par image, comparaison a GpuKit.** Le 2/11, armgl_init a refuse le GPU du Pi (eteint par un autre essai), puis le Pi a cesse de repondre pendant --bench. Mesure a refaire apres redemarrage : SD0:/n64gl/main --bench 4801 rom, puis avec --nogl. Le 2/11 sur demande de l'utilisateur, carte E: mise a jour : apps/n64emu.app/main et n64gl/main (Mesa, CRC 6943AAD7), lib/emukit.so (celui de main avec EM_STACK 8 Mo, un octet de difference, CRC 5FA37F47) ; anciens fichiers dans E:/n64gl (n64emu-asm-avant, emukit-avant.so). main a depuis un emukit plus recent (Quake), avec EM_STACK 8 Mo : celui de la carte est plus ancien.

Depuis, par FTP sur le Pi : SD0:/apps/n64emu.app/main = build de main fusionne (CRC E1251592) ; SD0:/n64gl/main reste 6943AAD7.

Le 3/11 : Mesa demarre sur le Pi, mais les 4 premiers jobs bloquent au rendu (CT1CS 288) et le service eteint le GPU jusqu'au redemarrage. Shader general (560 instructions, boucle) retire comme suspect principal, sans preuve ; armgl_lost repasse au logiciel si le GPU tombe. Dans main (e01c6b7), publie sur le Pi. A essayer apres redemarrage. Consigne : si un job V3D expire, arreter les essais.

**Why:** l'utilisateur veut le plus d'asm possible ; le C se limite a ce qui parle a GL, pour etre rejoue sur le PC.

**How to apply:** sur le Pi, jamais d'ecriture sur SD1: (UserFS) : --bench n'ecrit pas de .sav, le .glk va dans SD0:/home/n64gl. Essai sur le Pi autorise seulement pour SD0:/n64gl. Pieges : tools/mesa/testimage.py prend sdroot/ puis build/mesa/sdroot, pas build/emu/sdroot ; ~/armpi-mesa/out/armgl est partage entre sessions, build-n64.sh a ses objets dans out/n64gl ; les -I de Mesa pointent vers un autre worktree, mettre les notres d'abord ; un `\\n` dans un heredoc Python arrive comme vrai saut de ligne. Voir docs/MESA.md et docs/EMULATEURS.md.
