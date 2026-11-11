/* Canal du serveur graphique (core/ws.c) : docs/ABI.md, 0x50-0x5B. */
#ifndef KORIN_WS_H
#define KORIN_WS_H

#include <stdint.h>

struct process;

int64_t sys_ws_register(void);
int64_t sys_ws_call(uint64_t msg);
int64_t sys_ws_next(uint64_t msg, uint64_t ms);
int64_t sys_ws_reply(uint64_t num, uint64_t msg);
int64_t sys_ws_post(int64_t pid, uint64_t ev);
int64_t sys_ws_display(uint64_t out);
int64_t sys_ev_wait(uint64_t ev, uint64_t ms);
int64_t sys_screen_info(uint64_t out);
int64_t sys_clip_set(uint64_t buf, uint64_t len);
int64_t sys_clip_get(uint64_t buf, uint64_t max);
int64_t sys_ws_signal(uint64_t bits);
int64_t sys_ws_mode(uint64_t w, uint64_t h, uint64_t out);
void ws_proc_exit(struct process *p);

/* l'écran, fourni par l'architecture */
struct fb_desc {
	uint64_t phys;
	uint64_t width, height;
	uint64_t pitch_bytes;
};
void arch_fb_info(struct fb_desc *d);

#endif
