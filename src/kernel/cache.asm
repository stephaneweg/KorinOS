; ============================================================================
;  cache.asm - entretien des caches (ARMv8, commun a toutes les cartes)
; ============================================================================

; x0 = debut, x1 = taille : nettoie + invalide le cache D jusqu'au point de coherence
; (avant qu'un maitre DMA / le VideoCore lise, et avant de relire ce qu'il a ecrit)
dcache_flush_range:
	add	x1,x0,x1
	bic	x0,x0,63
.line:	dc	civac,x0
	add	x0,x0,64
	cmp	x0,x1
	b.lo	.line
	dsb	sy
	ret
