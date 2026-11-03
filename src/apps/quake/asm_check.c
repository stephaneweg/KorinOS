/* asm_check.c -- les decalages de src/apps/quake/asm : la construction echoue s'ils ne suivent plus les structures du C */
#include <stddef.h>
#include "quakedef.h"
#include "d_local.h"
#include "r_local.h"

#define AT(type, field, off) _Static_assert (offsetof (type, field) == (off), #type "." #field)

AT (espan_t, u, 0); AT (espan_t, v, 4); AT (espan_t, count, 8); AT (espan_t, pnext, 16);
_Static_assert (sizeof (espan_t) == 24, "espan_t");
AT (edge_t, u, 0); AT (edge_t, u_step, 4); AT (edge_t, prev, 8); AT (edge_t, next, 16); AT (edge_t, surfs, 24); AT (edge_t, nextremove, 32);
AT (surf_t, next, 0); AT (surf_t, prev, 8); AT (surf_t, spans, 16); AT (surf_t, key, 24); AT (surf_t, last_u, 28); AT (surf_t, spanstate, 32);
AT (surf_t, insubmodel, 60); AT (surf_t, d_ziorigin, 64); AT (surf_t, d_zistepu, 68); AT (surf_t, d_zistepv, 72);
_Static_assert (sizeof (surf_t) == 88, "surf_t");
AT (finalvert_t, flags, 24);
_Static_assert (sizeof (finalvert_t) == 32, "finalvert_t");
AT (stvert_t, onseam, 0); AT (stvert_t, s, 4); AT (stvert_t, t, 8);
_Static_assert (sizeof (stvert_t) == 12, "stvert_t");
AT (affinetridesc_t, skinwidth, 16);
AT (particle_t, org, 0); AT (particle_t, color, 12);
AT (channel_t, leftvol, 8); AT (channel_t, rightvol, 12); AT (channel_t, pos, 20);
AT (sfxcache_t, data, 20);
AT (viddef_t, colormap, 8);
