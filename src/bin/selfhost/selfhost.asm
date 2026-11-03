; selfhost - verifie l'auto-hebergement d'ARMPI : lance FASMARM natif
; (SD0:/bin/fasmarm) sur une serie de sources, chacune dans son repertoire,
; attend chaque assemblage et affiche code de sortie et duree.
; Avec une ligne d'arguments : un seul assemblage dans le repertoire courant.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	stp	x0,x1,[sp,-16]!			; (arguments)
	bl	kits_open
	ldp	x0,x1,[sp],16
	cbz	x0,.list
	mov	x1,x0				; arguments fournis : un seul travail
	adr	x0,dot
	bl	run_job
	sys	SYS_EXIT
.list:	adr	x19,jobs			; liste : (repertoire, arguments)
	mov	x20,0				; nombre d'echecs
.next:	ldrb	w0,[x19]
	cbz	w0,.done
	mov	x0,x19
	bl	strend
	add	x21,x0,1			; arguments
	mov	x0,x21
	bl	strend
	add	x22,x0,1			; travail suivant
	mov	x0,x19
	mov	x1,x21
	bl	run_job
	cbz	x0,.ok
	add	x20,x20,1
.ok:	mov	x19,x22
	b	.next
.done:	adr	x0,msg_end
	bl	lang_puts
	mov	x0,x20
	bl	print_dec
	adr	x0,msg_nl
	bl	puts
	mov	x0,x20
	sys	SYS_EXIT

; x0 = chaine -> x0 = adresse de son 0 final
strend:
.c:	ldrb	w1,[x0]
	cbz	w1,.e
	add	x0,x0,1
	b	.c
.e:	ret

; x0 = repertoire, x1 = arguments -> x0 = code de sortie de fasmarm
run_job:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	sys	SYS_CHDIR
	adr	x0,msg_run
	bl	lang_puts
	mov	x0,x19
	bl	puts
	adr	x0,msg_sep
	bl	lang_puts
	mov	x0,x20
	bl	puts
	adr	x0,msg_nl
	bl	puts
	sys	SYS_TIME_MS
	mov	x21,x0
	adr	x0,path_fasm
	mov	x1,x20
	sys	SYS_SPAWN
	tbnz	x0,63,.fail
	sys	SYS_WAIT
	mov	x22,x0
	sys	SYS_TIME_MS
	sub	x21,x0,x21
	adr	x0,msg_code
	bl	lang_puts
	mov	x0,x22
	bl	print_dec
	adr	x0,msg_ms
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,msg_ms2
	bl	puts
	mov	x0,x22
	b	.out
.fail:	mov	x22,x0
	adr	x0,msg_fail
	bl	lang_puts
	mov	x0,x22
	bl	print_dec
	adr	x0,msg_nl
	bl	puts
	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue


segment readable writeable
kit_data

path_fasm	db 'SD0:/bin/fasmarm',0
dot		db '.',0
jobs		db 'SD0:/t/bin/hello',0,'hello.asm hello.elf',0
		db 'SD0:/t',0,'InstructionFormatsARM64.asm if64.bin',0
		db 'SD0:/t',0,'InstructionFormatsTHUMB32.asm t32.bin',0
		db 'SD0:/src/kernel',0,'kernel.asm kernel8.img',0
		db 'SD0:/src/kernel',0,'kernel.asm kernel_2712.img -d ARCH=pi5',0
		db 'SD0:/src/kits/uikit',0,'uikit.asm uikit.so',0
		db 'SD0:/src/apps/control',0,'control.asm control.elf',0
		db 'SD0:/src/svc/echo',0,'echo.asm echo.elf',0
		db 'SD0:/src/svc/hwtest',0,'hwtest.asm hwtest.elf',0
		db 'SD0:/src/svc/vfs',0,'vfs.asm vfs.elf',0
		db 'SD0:/src/bin/kittest',0,'kittest.asm kittest.elf',0
		db 'SD0:/src/svc/ramfs',0,'ramfs.asm ramfs.elf',0
		db 'SD0:/src/svc/fatfs',0,'fatfs.asm fatfs.elf',0
		db 'SD0:/src/svc/imgblk',0,'imgblk.asm imgblk.elf',0
		db 'SD0:/src/svc/sd',0,'sd.asm sd.elf',0
		db 'SD0:/src/svc/init',0,'init.asm init.elf',0
		db 'SD0:/src/svc/init2',0,'init2.asm init2.elf',0
		db 'SD0:/src/svc/hwmanager',0,'hwmanager.asm hwmanager.elf',0
		db 'SD0:/src/svc/hidmanager',0,'hidmanager.asm hidmanager.elf',0
		db 'SD0:/src/svc/gt911',0,'gt911.asm gt911.elf',0
		db 'SD0:/src/svc/usbd',0,'usbd.asm usbd.elf',0
		db 'SD0:/src/svc/usbkeyboard',0,'usbkeyboard.asm usbkeyboard.elf',0
		db 'SD0:/src/svc/usbmouse',0,'usbmouse.asm usbmouse.elf',0
		db 'SD0:/src/svc/usbstorage',0,'usbstorage.asm usbstorage.elf',0
		db 'SD0:/src/bin/fasmarm',0,'fasmarm.asm fasmarm.elf',0
		db 'SD0:/src/svc/usbnet',0,'usbnet.asm usbnet.elf',0
		db 'SD0:/src/svc/netd',0,'netd.asm netd.elf',0
		db 'SD0:/src/kits/netkit',0,'netkit.asm netkit.so',0
		db 'SD0:/src/svc/genet',0,'genet.asm genet.elf',0
		db 'SD0:/src/svc/wifi',0,'wifi.asm wifi.elf',0
		db 'SD0:/src/sys/wifimenu',0,'wifimenu.asm wifimenu.elf',0
		db 'SD0:/src/svc/telnetd',0,'telnetd.asm telnetd.elf',0
		db 'SD0:/src/svc/ftpd',0,'ftpd.asm ftpd.elf',0
		db 'SD0:/src/bin/sh',0,'sh.asm sh.elf',0
		db 'SD0:/src/bin/cat',0,'cat.asm cat.elf',0
		db 'SD0:/src/bin/mkdir',0,'mkdir.asm mkdir.elf',0
		db 'SD0:/src/bin/rmdir',0,'rmdir.asm rmdir.elf',0
		db 'SD0:/src/bin/rm',0,'rm.asm rm.elf',0
		db 'SD0:/src/bin/cp',0,'cp.asm cp.elf',0
		db 'SD0:/src/bin/mv',0,'mv.asm mv.elf',0
		db 'SD0:/src/bin/touch',0,'touch.asm touch.elf',0
		db 'SD0:/src/bin/ls',0,'ls.asm ls.elf',0
		db 'SD0:/src/bin/basename',0,'basename.asm basename.elf',0
		db 'SD0:/src/bin/dirname',0,'dirname.asm dirname.elf',0
		db 'SD0:/src/bin/sleep',0,'sleep.asm sleep.elf',0
		db 'SD0:/src/bin/test',0,'test.asm test.elf',0
		db 'SD0:/src/bin/true',0,'true.asm true.elf',0
		db 'SD0:/src/bin/false',0,'false.asm false.elf',0
		db 'SD0:/src/bin/echo',0,'echo.asm echo.elf',0
		db 'SD0:/src/bin/pwd',0,'pwd.asm pwd.elf',0
		db 'SD0:/src/bin/wc',0,'wc.asm wc.elf',0
		db 'SD0:/src/bin/head',0,'head.asm head.elf',0
		db 'SD0:/src/bin/tail',0,'tail.asm tail.elf',0
		db 'SD0:/src/bin/tee',0,'tee.asm tee.elf',0
		db 'SD0:/src/bin/tr',0,'tr.asm tr.elf',0
		db 'SD0:/src/bin/uniq',0,'uniq.asm uniq.elf',0
		db 'SD0:/src/bin/cut',0,'cut.asm cut.elf',0
		db 'SD0:/src/bin/sort',0,'sort.asm sort.elf',0
		db 'SD0:/src/bin/grep',0,'grep.asm grep.elf',0
		db 'SD0:/src/bin/find',0,'find.asm find.elf',0
		db 'SD0:/src/bin/sed',0,'sed.asm sed.elf',0
		db 'SD0:/src/bin/reboot',0,'reboot.asm reboot.elf',0
		db 'SD0:/src/bin/screenshot',0,'screenshot.asm screenshot.elf',0
		db 'SD0:/src/svc/vncd',0,'vncd.asm vncd.elf',0
		db 'SD0:/src/svc/rdpd',0,'rdpd.asm rdpd.elf',0
		db 'SD0:/src/bin/rngtest',0,'rngtest.asm rngtest.elf',0
		db 'SD0:/src/sys/elegant',0,'elegant.asm elegant.elf',0
		db 'SD0:/src/apps/uidemo',0,'uidemo.asm uidemo.elf',0
		db 'SD0:/src/apps/viewer',0,'viewer.asm viewer.elf',0
		db 'SD0:/src/apps/editor',0,'editor.asm editor.elf',0
		db 'SD0:/src/sys/menubar',0,'menubar.asm menubar.elf',0
		db 'SD0:/src/kits/audiokit',0,'audiokit.asm audiokit.so',0
		db 'SD0:/src/svc/sound',0,'sound.asm sound.elf',0
		db 'SD0:/src/svc/pwmaudio',0,'pwmaudio.asm pwmaudio.elf',0
		db 'SD0:/src/svc/hdmiaudio',0,'hdmiaudio.asm hdmiaudio.elf',0
		db 'SD0:/src/svc/i2smic',0,'i2smic.asm i2smic.elf',0
		db 'SD0:/src/svc/usbaudio',0,'usbaudio.asm usbaudio.elf',0
		db 'SD0:/src/apps/mixer',0,'mixer.asm mixer.elf',0
		db 'SD0:/src/sys/soundmenu',0,'soundmenu.asm soundmenu.elf',0
		db 'SD0:/src/bin/soundctl',0,'soundctl.asm soundctl.elf',0
		db 'SD0:/src/bin/sontest',0,'sontest.asm sontest.elf',0
		db 'SD0:/src/kits/appkit',0,'appkit.asm appkit.so',0
		db 'SD0:/src/bin/threadtest',0,'threadtest.asm threadtest.elf',0
		db 'SD0:/src/bin/memtest',0,'memtest.asm memtest.elf',0
		; (surface attachee d'Elegant, lecteur video)
		db 'SD0:/src/bin/surfacetest',0,'surfacetest.asm surfacetest.elf',0
		; (plusieurs fenetres par programme, UIKit)
		db 'SD0:/src/bin/multiwin',0,'multiwin.asm multiwin.elf',0
		db 'SD0:/src/bin/mediatest',0,'mediatest.asm mediatest.elf',0
		db 'SD0:/src/apps/mediaplayer',0,'mediaplayer.asm mediaplayer.elf',0
		; formats audio d'AudioKit (docs/SON.md 9)
		db 'SD0:/src/bin/codectest',0,'codectest.asm codectest.elf',0
		db 'SD0:/src/bin/play',0,'play.asm play.elf',0
		db 'SD0:/src/apps/music',0,'music.asm music.elf',0
		; synthese FM et MIDI d'AudioKit
		db 'SD0:/src/bin/synthtest',0,'synthtest.asm synthtest.elf',0
		db 'SD0:/src/apps/fmtracker',0,'fmtracker.asm fmtracker.elf',0
		; GPU V3D : service, kit, essai (docs/V3D.md)
		db 'SD0:/src/svc/v3d',0,'v3d.asm v3d.elf',0
		db 'SD0:/src/kits/gpukit',0,'gpukit.asm gpukit.so',0
		db 'SD0:/src/bin/gputest',0,'gputest.asm gputest.elf',0
		; pibuild et sa fenetre (docs/GCC.md)
		db 'SD0:/src/bin/pibuild',0,'pibuild.asm pibuild.elf',0
		db 'SD0:/src/apps/builder',0,'builder.asm builder.elf',0
		; archives : le kit, puis son outil (docs/KITS.md)
		db 'SD0:/src/kits/archivekit',0,'archivekit.asm archivekit.so',0
		db 'SD0:/src/bin/arc',0,'arc.asm arc.elf',0
		; plomberie des formats bureautiques (docs/KITS.md)
		db 'SD0:/src/kits/documentkit',0,'documentkit.asm documentkit.so',0
		db 0
msg_run		db 'selfhost : fasmarm dans ',0
msg_sep		db ' : ',0
msg_code	db 'selfhost : code de sortie ',0
msg_ms		db ', ',0
msg_ms2		db ' ms',10,0
msg_fail	db 'selfhost : lancement impossible, erreur ',0
msg_end		db 'selfhost : termine, echecs = ',0
msg_nl		db 10,0
