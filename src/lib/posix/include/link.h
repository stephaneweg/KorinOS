/*
 * link.h -- dl_iterate_phdr (libposix dlopen.c) : le programme, puis les modules charges par dlopen.
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
#ifndef _POSIX_LINK_H
#define _POSIX_LINK_H

#include <elf.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ElfW(type)	Elf64_##type

struct dl_phdr_info
{
	Elf64_Addr dlpi_addr;
	const char *dlpi_name;
	const Elf64_Phdr *dlpi_phdr;
	Elf64_Half dlpi_phnum;
	unsigned long long dlpi_adds, dlpi_subs;
	size_t dlpi_tls_modid;
	void *dlpi_tls_data;
};

/* (pour qui lit ces listes dans un autre process : le chargeur de dlopen.c ne les tient pas) */
struct link_map
{
	Elf64_Addr l_addr;
	char *l_name;
	Elf64_Dyn *l_ld;
	struct link_map *l_next, *l_prev;
};

struct r_debug
{
	int r_version;
	struct link_map *r_map;
	Elf64_Addr r_brk;
	int r_state;
	Elf64_Addr r_ldbase;
};

int dl_iterate_phdr (int (*) (struct dl_phdr_info *, size_t, void *), void *);

#ifdef __cplusplus
}
#endif

#endif
