/*
 * sys/statfs.h -- statfs, fstatfs (libposix stat.c) : les valeurs de statvfs, que vfs ne connait
 * pas vraiment ; f_type dit FAT, comme la carte.
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
#ifndef _SYS_STATFS_H
#define _SYS_STATFS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MSDOS_SUPER_MAGIC	0x4d44

typedef struct { int __val[2]; } fsid_t;

struct statfs
{
	long f_type, f_bsize;
	unsigned long f_blocks, f_bfree, f_bavail, f_files, f_ffree;
	fsid_t f_fsid;
	long f_namelen, f_frsize, f_flags, f_spare[4];
};

int statfs (const char *, struct statfs *);
int fstatfs (int, struct statfs *);

#ifdef __cplusplus
}
#endif

#endif
