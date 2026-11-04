/* Shared by oi_sys_posix.c and oi_sys_amiga.c. Copyright (c) 2026 AmigaChrome Team. MIT licence. */
#ifndef OI_SYS_COMMON_H
#define OI_SYS_COMMON_H

#include <stddef.h>

int gs_common_patmatch(const char *pattern, const char *name);
long gs_common_scan_version(const char *buf, size_t len);
unsigned long gs_common_crc32(unsigned long crc, const unsigned char *p, size_t n);

#endif
