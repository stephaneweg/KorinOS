/*
 * sys/epoll.h -- epoll sur poll (libposix epoll.c, docs/POSIX.md).
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#ifndef _SYS_EPOLL_H
#define _SYS_EPOLL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPOLL_CLOEXEC	02000000
#define EPOLL_CTL_ADD	1
#define EPOLL_CTL_DEL	2
#define EPOLL_CTL_MOD	3

#define EPOLLIN		0x001
#define EPOLLPRI	0x002
#define EPOLLOUT	0x004
#define EPOLLERR	0x008
#define EPOLLHUP	0x010
#define EPOLLRDHUP	0x2000
#define EPOLLET		(1U << 31)

typedef union epoll_data
{
	void *ptr;
	int fd;
	uint32_t u32;
	uint64_t u64;
} epoll_data_t;

struct epoll_event
{
	uint32_t events;
	epoll_data_t data;
};

int epoll_create (int);
int epoll_create1 (int);
int epoll_ctl (int, int, int, struct epoll_event *);
int epoll_wait (int, struct epoll_event *, int, int);

#ifdef __cplusplus
}
#endif

#endif
