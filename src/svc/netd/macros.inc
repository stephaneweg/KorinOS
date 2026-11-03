; macros de la pile (stack.inc) : definies avant tout usage
macro st16be val*, base*, off* {
	rev16	w16,val
	strh	w16,[base,off]
}
macro st32be val*, base*, off* {
	rev	w16,val
	str	w16,[base,off]
}
macro ld16be dst*, base*, off* {
	ldrh	dst,[base,off]
	rev16	dst,dst
}
macro ld32be dst*, base*, off* {
	ldr	dst,[base,off]
	rev	dst,dst
}
macro net_stat n* {			; compteur n de net_st (x16, x17 changes)
	adr	x16,net_st
	ldr	x17,[x16,(n)*8]
	add	x17,x17,1
	str	x17,[x16,(n)*8]
}
macro net_lock_take {
	bl	nl_take
}
macro net_lock_drop {
	bl	nl_drop
}

