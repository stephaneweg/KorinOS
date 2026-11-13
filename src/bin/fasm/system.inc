; fasm pour Korin PC : l'interface avec le système (ref/fasm/SOURCE/LINUX/X64/SYSTEM.INC, mêmes
; fonctions et mêmes registres pour le coeur) ; les appels de Linux deviennent des appels aux
; fonctions C de c/fasm_sys.c (System V : rdi, rsi, rdx ; rax rendu).
;
; Copyright (c) 1999-2022, Tomasz Grysztar ; interface de Korin : ce dépôt.

; Appel d'une fonction C : csave garde les registres que le C peut détruire (rcx, rdx, rsi, rdi,
; r8-r11 ; le coeur n'en suppose aucun détruit, sauf rax) et aligne la pile, que les push de 4
; octets de modes.inc laissent à 4 près ; crestore les rend (rax : le résultat).
macro csave
{
	mov	[c_save],rcx
	mov	[c_save+8],rdx
	mov	[c_save+16],rsi
	mov	[c_save+24],rdi
	mov	[c_save+32],r8
	mov	[c_save+40],r9
	mov	[c_save+48],r10
	mov	[c_save+56],r11
	mov	[c_save+64],rsp
	and	rsp,-16
}
macro crestore
{
	mov	rsp,[c_save+64]
	mov	rcx,[c_save]
	mov	rdx,[c_save+8]
	mov	rsi,[c_save+16]
	mov	rdi,[c_save+24]
	mov	r8,[c_save+32]
	mov	r9,[c_save+40]
	mov	r10,[c_save+48]
	mov	r11,[c_save+56]
}

; la mémoire : le .bss de korin.asm (sous 4 Go, comme le demande le coeur), -m pour moins
init_memory:
	mov	eax,esp
	and	eax,not 0FFFh
	add	eax,1000h-10000h
	mov	[stack_limit],eax
	mov	ecx,[memory_setting]
	shl	ecx,10
	jz	memory_all
	cmp	ecx,KORIN_MEMORY
	jbe	memory_size_ok
      memory_all:
	mov	ecx,KORIN_MEMORY
      memory_size_ok:
	mov	eax,korin_memory
	mov	[additional_memory],eax
	lea	edi,[eax+ecx]
	mov	[memory_end],edi
	shr	ecx,2
	add	eax,ecx
	mov	[additional_memory_end],eax
	mov	[memory_start],eax
	ret

exit_program:
	movzx	edi,al
	and	rsp,-16
	call	fk_exit

; ms depuis le démarrage -> eax (pour la durée affichée)
time_ms:
	csave
	call	fk_time_ms
	crestore
	ret

get_environment_variable:
	csave
	mov	edi,esi
	call	fk_getenv
	crestore
	test	rax,rax
	jz	no_environment_variable
	mov	rsi,rax
      copy_variable_value:
	lodsb
	cmp	edi,[memory_end]
	jae	out_of_memory
	stosb
	or	al,al
	jnz	copy_variable_value
	dec	edi
      no_environment_variable:
	ret

open:
	mov	r12d,esi
	mov	r13d,edi
	call	adapt_path
	mov	esi,r12d
	mov	edi,r13d
	csave
	mov	edi,buffer
	call	fk_open
	crestore
	test	rax,rax
	js	file_error
	mov	ebx,eax
	clc
	ret
    adapt_path:
	mov	esi,edx
	mov	edi,buffer
      copy_path:
	lods	byte [esi]
	cmp	al,'\'
	jne	path_char_ok
	mov	al,'/'
      path_char_ok:
	stos	byte [edi]
	or	al,al
	jnz	copy_path
	cmp	edi,buffer+1000h
	ja	out_of_memory
	ret
create:
	mov	r12d,esi
	mov	r13d,edi
	mov	r15d,edx
	call	adapt_path
	mov	esi,r12d
	mov	edi,r13d
	xor	r14d,r14d			; exécutable : le fichier de sortie d'un ELF exécutable
	cmp	r15d,[output_file]
	jne	do_create
	cmp	[output_format],5
	jne	do_create
	bt	[format_flags],0
	jnc	do_create
	inc	r14d
      do_create:
	csave
	mov	edi,buffer
	mov	esi,r14d
	call	fk_create
	crestore
	test	rax,rax
	js	file_error
	mov	ebx,eax
	clc
	ret
close:
	csave
	mov	edi,ebx
	call	fk_close
	crestore
	ret
read:
	csave
	mov	edi,ebx
	mov	esi,edx
	mov	edx,ecx
	call	fk_read
	crestore
	test	rax,rax
	js	file_error
	cmp	eax,ecx
	jne	file_error
	clc
	ret
    file_error:
	stc
	ret
write:
	csave
	mov	edi,ebx
	mov	esi,edx
	mov	edx,ecx
	call	fk_write
	crestore
	test	rax,rax
	js	file_error
	clc
	ret
lseek:
	csave
	movzx	eax,al
	mov	edi,ebx
	movsxd	rsi,edx
	mov	edx,eax
	call	fk_seek
	crestore
	test	rax,rax
	js	file_error
	clc
	ret

display_string:
	csave
	mov	r8d,esi
	mov	edi,esi
	or	ecx,-1
	xor	al,al
	repne	scasb
	neg	ecx
	sub	ecx,2
	mov	edx,ecx
	mov	esi,r8d
	mov	edi,[con_handle]
	call	fk_print
	crestore
	ret
display_character:
	mov	[character],dl
	csave
	mov	edi,[con_handle]
	mov	esi,character
	mov	edx,1
	call	fk_print
	crestore
	ret
display_number:
	mov	r14d,ebx
	mov	ecx,1000000000
	xor	edx,edx
	xor	bl,bl
      display_loop:
	div	ecx
	mov	r15d,edx
	cmp	ecx,1
	je	display_digit
	or	bl,bl
	jnz	display_digit
	or	al,al
	jz	digit_ok
	not	bl
      display_digit:
	mov	dl,al
	add	dl,30h
	mov	r10d,ecx
	call	display_character
	mov	ecx,r10d
      digit_ok:
	mov	eax,ecx
	xor	edx,edx
	mov	ecx,10
	div	ecx
	mov	ecx,eax
	mov	eax,r15d
	or	ecx,ecx
	jnz	display_loop
	mov	ebx,r14d
	ret

display_user_messages:
	mov	[displayed_count],0
	call	show_display_buffer
	cmp	[displayed_count],0
	je	line_break_ok
	cmp	[last_displayed],0Ah
	je	line_break_ok
	mov	dl,0Ah
	call	display_character
      line_break_ok:
	ret
display_block:
	test	ecx,ecx
	jz	block_displayed
	add	[displayed_count],ecx
	mov	al,[esi+ecx-1]
	mov	[last_displayed],al
	csave
	mov	edi,[con_handle]
	mov	edx,ecx
	call	fk_print
	crestore
      block_displayed:
	ret

fatal_error:
	mov	[con_handle],2
	mov	esi,error_prefix
	call	display_string
	pop	esi
	call	display_string
	mov	esi,error_suffix
	call	display_string
	mov	al,0FFh
	jmp	exit_program
assembler_error:
	mov	[con_handle],2
	call	display_user_messages
	mov	ebx,[current_line]
	test	ebx,ebx
	jz	display_error_message
	push	dword 0
      get_error_lines:
	mov	eax,[ebx]
	cmp	byte [eax],0
	je	get_next_error_line
	push	ebx
	test	byte [ebx+7],80h
	jz	display_error_line
	mov	edx,ebx
      find_definition_origin:
	mov	edx,[edx+12]
	test	byte [edx+7],80h
	jnz	find_definition_origin
	push	edx
      get_next_error_line:
	mov	ebx,[ebx+8]
	jmp	get_error_lines
      display_error_line:
	mov	esi,[ebx]
	call	display_string
	mov	esi,line_number_start
	call	display_string
	mov	eax,[ebx+4]
	and	eax,7FFFFFFFh
	call	display_number
	mov	dl,']'
	call	display_character
	pop	esi
	cmp	ebx,esi
	je	line_number_ok
	mov	dl,20h
	call	display_character
	push	esi
	mov	esi,[esi]
	movzx	ecx,byte [esi]
	inc	esi
	call	display_block
	mov	esi,line_number_start
	call	display_string
	pop	esi
	mov	eax,[esi+4]
	and	eax,7FFFFFFFh
	call	display_number
	mov	dl,']'
	call	display_character
      line_number_ok:
	mov	esi,line_data_start
	call	display_string
	mov	esi,ebx
	mov	edx,[esi]
	call	open
	mov	al,2
	xor	edx,edx
	call	lseek
	mov	edx,[esi+8]
	sub	eax,edx
	jz	line_data_displayed
	push	eax
	xor	al,al
	call	lseek
	mov	ecx,[esp]
	mov	edx,[additional_memory]
	lea	eax,[edx+ecx]
	cmp	eax,[additional_memory_end]
	ja	out_of_memory
	call	read
	call	close
	pop	ecx
	mov	esi,[additional_memory]
      get_line_data:
	mov	al,[esi]
	cmp	al,0Ah
	je	display_line_data
	cmp	al,0Dh
	je	display_line_data
	cmp	al,1Ah
	je	display_line_data
	or	al,al
	jz	display_line_data
	inc	esi
	loop	get_line_data
      display_line_data:
	mov	ecx,esi
	mov	esi,[additional_memory]
	sub	ecx,esi
	call	display_block
      line_data_displayed:
	mov	esi,lf
	call	display_string
	pop	ebx
	or	ebx,ebx
	jnz	display_error_line
	cmp	[preprocessing_done],0
	je	display_error_message
	mov	esi,preprocessed_instruction_prefix
	call	display_string
	mov	esi,[current_line]
	add	esi,16
	mov	edi,[additional_memory]
	xor	dl,dl
      convert_instruction:
	lodsb
	cmp	al,1Ah
	je	copy_symbol
	cmp	al,22h
	je	copy_symbol
	cmp	al,3Bh
	je	instruction_converted
	stosb
	or	al,al
	jz	instruction_converted
	xor	dl,dl
	jmp	convert_instruction
      copy_symbol:
	or	dl,dl
	jz	space_ok
	mov	byte [edi],20h
	inc	edi
      space_ok:
	cmp	al,22h
	je	quoted
	lodsb
	movzx	ecx,al
	rep	movsb
	or	dl,-1
	jmp	convert_instruction
      quoted:
	mov	al,27h
	stosb
	lodsd
	mov	ecx,eax
	jecxz	quoted_copied
      copy_quoted:
	lodsb
	stosb
	cmp	al,27h
	jne	quote_ok
	stosb
      quote_ok:
	loop	copy_quoted
      quoted_copied:
	mov	al,27h
	stosb
	or	dl,-1
	jmp	convert_instruction
      instruction_converted:
	xor	al,al
	stosb
	mov	esi,[additional_memory]
	call	display_string
	mov	esi,lf
	call	display_string
      display_error_message:
	mov	esi,error_prefix
	call	display_string
	pop	esi
	call	display_string
	mov	esi,error_suffix
	call	display_string
	mov	al,2
	jmp	exit_program

make_timestamp:
	csave
	call	fk_time
	mov	[timestamp],rax
	crestore
	mov	eax,dword [timestamp]
	mov	edx,dword [timestamp+4]
	ret

error_prefix db 'error: ',0
error_suffix db '.'
lf db 0xA,0
line_number_start db ' [',0
line_data_start db ':',0xA,0
preprocessed_instruction_prefix db 'processed: ',0
