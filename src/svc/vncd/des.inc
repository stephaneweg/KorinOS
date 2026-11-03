; ============================================================================
;  svc/vncd/des.inc - DES (FIPS 46-3), chiffrement d'un bloc, pour la "VNC
;  Authentication" (RFC 6143 7.2.2). Ecrit pour la clarte, pas la vitesse :
;  une permutation = une boucle sur sa table (positions 1..W, bit 1 = poids
;  fort, comme dans la norme). Deux blocs par connexion, la vitesse importe peu.
;  Verifie : cle 133457799BBCDFF1, clair 0123456789ABCDEF -> 85E813540F0AB405.
;
;	adr	x0,cle			; 8 octets (bits de parite ignores)
;	adr	x1,sous_cles		; 16 x 8 octets
;	bl	des_cles
;	adr	x0,sous_cles
;	adr	x1,clair		; 8 octets
;	adr	x2,chiffre		; 8 octets (peut etre clair)
;	bl	des_bloc
; ============================================================================
	align 4

; x0 = valeur (W bits), x1 = table, x2 = nombre de bits produits, x3 = W
; -> x0 ; n'utilise que x0..x6
des_perm:
	mov	x4,0
.l:	cbz	x2,.f
	ldrb	w5,[x1],1
	sub	x5,x3,x5			; bit numero p : decalage W - p
	lsr	x6,x0,x5
	and	x6,x6,1
	orr	x4,x6,x4,lsl 1
	sub	x2,x2,1
	b	.l
.f:	mov	x0,x4
	ret

; x0 = cle (8 octets), x1 = 16 sous-cles de 48 bits (u64 chacune)
des_cles:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	ldr	x0,[x0]
	rev	x0,x0				; gros-boutiste
	adr	x1,des_pc1
	mov	x2,56
	mov	x3,64
	bl	des_perm
	lsr	x21,x0,28			; C, D : 28 bits chacun
	and	x22,x0,0xFFFFFFF
	mov	x19,0
.t:	adr	x1,des_rot			; rotations a gauche de C et D
	ldrb	w2,[x1,x19]
	mov	x3,28
	sub	x3,x3,x2
	lsl	x4,x21,x2
	lsr	x5,x21,x3
	orr	x21,x4,x5
	and	x21,x21,0xFFFFFFF
	lsl	x4,x22,x2
	lsr	x5,x22,x3
	orr	x22,x4,x5
	and	x22,x22,0xFFFFFFF
	orr	x0,x22,x21,lsl 28
	adr	x1,des_pc2
	mov	x2,48
	mov	x3,56
	bl	des_perm
	str	x0,[x20,x19,lsl 3]
	add	x19,x19,1
	cmp	x19,16
	b.lo	.t
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sous-cles (des_cles), x1 = bloc clair (8 o), x2 = bloc chiffre (8 o)
des_bloc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x24,x2
	ldr	x0,[x1]
	rev	x0,x0
	adr	x1,des_ip			; permutation initiale
	mov	x2,64
	mov	x3,64
	bl	des_perm
	lsr	x20,x0,32			; L
	mov	w21,w0				; R
	mov	x22,0
.r:	mov	x0,x21				; f(R, K) : expansion, xor de la cle,
	adr	x1,des_e			; S-boxes, permutation P
	mov	x2,48
	mov	x3,32
	bl	des_perm
	ldr	x1,[x19,x22,lsl 3]
	eor	x0,x0,x1
	mov	x23,0
	mov	x5,0
.s:	mov	x6,6				; 6 bits pour la boite i
	mul	x6,x6,x5
	mov	x7,42
	sub	x6,x7,x6
	lsr	x7,x0,x6
	and	x7,x7,63
	lsr	x8,x7,4				; ligne = b5 b0, colonne = b4..b1
	and	x8,x8,2
	and	x9,x7,1
	orr	x8,x8,x9
	lsl	x8,x8,4
	ubfx	x9,x7,1,4
	orr	x8,x8,x9
	adr	x9,des_s
	add	x9,x9,x5,lsl 6
	ldrb	w9,[x9,x8]
	orr	x23,x9,x23,lsl 4
	add	x5,x5,1
	cmp	x5,8
	b.lo	.s
	mov	x0,x23
	adr	x1,des_p
	mov	x2,32
	mov	x3,32
	bl	des_perm
	eor	x0,x0,x20			; L' = R, R' = L xor f(R, K)
	mov	x20,x21
	mov	x21,x0
	add	x22,x22,1
	cmp	x22,16
	b.lo	.r
	orr	x0,x20,x21,lsl 32		; R16 L16, permutation finale
	adr	x1,des_fp
	mov	x2,64
	mov	x3,64
	bl	des_perm
	rev	x0,x0
	str	x0,[x24]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; tables de la norme ; des_s : huit boites de 4 lignes x 16 colonnes
des_pc1   db 57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18
          db 10, 2, 59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36
          db 63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22
          db 14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4
des_pc2   db 14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10
          db 23, 19, 12, 4, 26, 8, 16, 7, 27, 20, 13, 2
          db 41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48
          db 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32
des_ip    db 58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4
          db 62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8
          db 57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3
          db 61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7
des_fp    db 40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31
          db 38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29
          db 36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27
          db 34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25
des_e     db 32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9
          db 8, 9, 10, 11, 12, 13, 12, 13, 14, 15, 16, 17
          db 16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25
          db 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1
des_p     db 16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10
          db 2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25
des_rot   db 1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1
des_s     db 14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7
          db 0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8
          db 4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0
          db 15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13
          db 15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10
          db 3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5
          db 0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15
          db 13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9
          db 10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8
          db 13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1
          db 13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7
          db 1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12
          db 7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15
          db 13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9
          db 10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4
          db 3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14
          db 2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9
          db 14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6
          db 4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14
          db 11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3
          db 12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11
          db 10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8
          db 9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6
          db 4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13
          db 4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1
          db 13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6
          db 1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2
          db 6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12
          db 13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7
          db 1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2
          db 7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8
          db 2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11
	align 4
