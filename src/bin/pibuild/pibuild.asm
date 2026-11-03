; pibuild - construit un programme : pibuild [app|bin|sys|svc] dossier nom (docs/GCC.md)
; Du C, du C++, du BASIC ou de l'assembleur : c'est /gcc/share/pibuild.mk, lance par make dans le dossier,
; qui choisit, compile, lie et installe selon le type (sans type : app) : /apps/<nom>.app, /bin/<nom>,
; SD0:/sys/<nom> ou SD0:/sys/svc/<nom>.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	t_debut
	cmp	x19,3
	b.hs	.ok
	adr	x0,s_usage
	bl	t_dire
	mov	x0,2
	bl	t_fin
.ok:	mov	x21,8
	cmp	x19,4
	b.lo	.dir
	adr	x0,a_type			; TYPE=type (pibuild.mk le verifie)
	ldr	x1,[x20,8]
	mov	x2,16
	bl	copy
	mov	x21,16
.dir:	ldr	x0,[x20,x21]			; -C dossier
	adr	x1,a_dir
	str	x0,[x1]
	add	x21,x21,8
	adr	x0,a_name			; NAME=nom
	ldr	x1,[x20,x21]
	mov	x2,240
	bl	copy
	adr	x0,p_make
	adr	x1,argv
	adr	x2,envp
	adr	x3,fds
	bl	f_spawn_env
	tbnz	x0,63,.err
	sys	SYS_WAIT
	bl	t_fin
.err:	adr	x0,s_nomake
	bl	t_dire
	mov	x0,1
	bl	t_fin

; x0 = destination, x1 = chaine, x2 = octets au plus (feuille)
copy:
.c:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	cbz	w3,.ret
	subs	x2,x2,1
	b.ne	.c
	strb	wzr,[x0]
.ret:	ret

include '../../inc/tool.inc'

segment readable writeable
kit_data
tool_data
		align 8
argv		dd s_make, s_c
a_dir		dd 0
		dd s_f, s_mk, s_j, a_nameopt, a_typeopt, 0
envp		dd s_path, 0
fds		dd 0, 1, 2
p_make		db 'SD0:/gcc/bin/make',0
s_make		db 'make',0
s_c		db '-C',0
s_f		db '-f',0
s_mk		db '/gcc/share/pibuild.mk',0
s_j		db '-j4',0
s_path		db 'PATH=/bin:/gcc/bin',0
s_usage		db 'pibuild [app|bin|sys|svc|lib] dossier nom',10,0
s_nomake	db 'pibuild : pas de /gcc/bin/make',10,0
a_typeopt	db 'TYPE='
a_type		db 'app',0
		rb 16
a_nameopt	db 'NAME='
a_name		rb 244
