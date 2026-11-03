/* sys/membarrier.h -- membarrier (libposix signal.c, SYS_MEMBARRIER : memes commandes que Linux) */
#ifndef _SYS_MEMBARRIER_H
#define _SYS_MEMBARRIER_H

#ifdef __cplusplus
extern "C" {
#endif

#define MEMBARRIER_CMD_QUERY				0
#define MEMBARRIER_CMD_PRIVATE_EXPEDITED		8
#define MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED	16
#define MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE	32
#define MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE 64

int membarrier (int cmd, unsigned flags, int cpu_id);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MEMBARRIER_H */
