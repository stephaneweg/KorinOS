; ============================================================================
;  arch/pi5/stubs.asm - pilotes du Pi 5 pas encore ecrits
;
;  Le noyau generique les appelle ; ici ils annoncent leur absence et
;  rendent « pas de peripherique », pour que le systeme demarre quand meme
;  (console, carte SD, interface graphique, applications).
;  A porter (docs/PI5.md) : son, GPU V3D 7.1 ; le reseau (RP1 MACB, Wi-Fi)
;  viendra en services, comme sur le Pi 4 (docs/RESEAU.md).
; ============================================================================

