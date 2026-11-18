/* Appels système des process, des threads et du système (core/proc.c,
 * core/thread.c, core/sys.c). */
#ifndef KORIN_PROC_H
#define KORIN_PROC_H

#include <stdint.h>

int64_t sys_wait(int64_t pid, uint64_t opt, uint64_t out);
int64_t sys_kill(int64_t pid);
int64_t sys_proclist_from(uint64_t buf, uint64_t max, uint64_t from);
int64_t sys_proc_info(int64_t pid, uint64_t out);
int64_t sys_proc_rights(int64_t pid);
int64_t sys_setuid(int64_t uid);
int64_t sys_proc_uid(int64_t pid);
int64_t sys_proc_setuid(int64_t pid, int64_t uid);
int64_t sys_proc_exe(int64_t pid, uint64_t out);
int64_t sys_proc_set_script(uint64_t path);
int64_t sys_getppid(void);
int64_t sys_spawn_image(uint64_t image, uint64_t size, uint64_t params);
int64_t sys_fork(void);
int64_t sys_exec(uint64_t path, uint64_t size, uint64_t params);
void proc_wake_waiters(void);

int64_t sys_thread_create(uint64_t entry, uint64_t stack, uint64_t arg, uint64_t tls, uint64_t prio, uint64_t name);
int64_t sys_thread_join(int64_t tid);
int64_t sys_thread_set_priority(int64_t tid, int64_t prio);
int64_t sys_thread_set_name(int64_t tid, uint64_t name);
int64_t sys_thread_kill(int64_t tid, int64_t code);
int64_t sys_thread_list(int64_t pid, uint64_t buf, uint64_t max);
int64_t sys_futex_wait(uint64_t addr, uint64_t val, uint64_t ms);
int64_t sys_futex_wake(uint64_t addr, uint64_t n);

int64_t sys_clock(int set, uint64_t a, uint64_t b, uint64_t c);
int64_t sys_sysinfo(uint64_t out);
int64_t sys_cpu_load(uint64_t out, uint64_t places);
int64_t sys_getrandom(uint64_t buf, uint64_t n);
int64_t sys_klog(uint64_t buf, uint64_t max);
/* journal du noyau : chaque ligne avec son heure, 64 Ko au plus */
void klog_write(const char *s);
void random_mix(uint64_t v);

#endif
