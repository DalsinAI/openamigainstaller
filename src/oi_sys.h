/*
 * The file system and the machine, for the interpreter. oi_sys_amiga.c
 * is dos.library, exec and icon.library; oi_sys_posix.c is the host's,
 * for the tests, with Amiga volumes as folders: NAME: is
 * $OI_VOLUMES/NAME (the current folder when it is unset).
 *
 * Paths are Amiga paths throughout ("Work:Games/x", "/" for the parent).
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#ifndef OI_SYS_H
#define OI_SYS_H

#include <stddef.h>

#define GS_NONE 0
#define GS_FILE 1
#define GS_DIR  2

int gs_exists(const char *path);                /* GS_* */
long long gs_size(const char *path);            /* -1 if none */
int gs_date_cmp(const char *a, const char *b, int *res);   /* res <0 a older; 0 ok */
int gs_makedir(const char *path);               /* 0 ok */
int gs_delete(const char *path);                /* a file or an empty drawer */
int gs_rename(const char *from, const char *to);
int gs_copy(const char *from, const char *to,
            int (*progress)(void *ud, long long now, long long total), void *ud);
long gs_get_protect(const char *path);          /* -1 if none */
int gs_set_protect(const char *path, long bits);
int gs_set_comment(const char *path, const char *comment);

/* A drawer's entries, one by one. */
typedef struct gs_dir gs_dir;
gs_dir *gs_opendir(const char *path);
const char *gs_readdir(gs_dir *d, int *type);  /* NULL at the end */
void gs_closedir(gs_dir *d);

/* AmigaDOS patterns (#? ~ | ( ) [ ] %), as MatchPatternNoCase. */
int gs_patmatch(const char *pattern, const char *name);

/* Reading and writing whole files. */
char *gs_read_file(const char *path, size_t *len);  /* malloc'd, NUL ended */
int gs_write_file(const char *path, const char *data, size_t len, int append);

/* The machine. */
char *gs_getenv(const char *name);              /* ENV:name, malloc'd */
int gs_setenv(const char *name, const char *value, int archive);
char *gs_getassign(const char *name, const char *opts);    /* malloc'd or NULL */
int gs_makeassign(const char *name, const char *path, int add);
char *gs_getdevice(const char *path);           /* the device's name */
long long gs_diskspace(const char *path);       /* bytes free, -1 if not known */
char *gs_expandpath(const char *path);          /* malloc'd, NULL if none */
long gs_getversion(const char *path, int resident);    /* version<<16 | revision, -1 */
unsigned long gs_getsum(const char *path, int *ok);
char *gs_database(const char *what);            /* malloc'd, "unknown" if not */
long gs_run(const char *command, const char *dir, int wait);  /* return code */
int gs_is_volume(const char *path);             /* "NAME:" alone */
int gs_volume_mounted(const char *volume);

/* Icons (icon.library); the host keeps none. */
int gs_icon_tooltype(const char *path, const char *name, const char *value,
                     int remove);
char *gs_icon_get_tooltype(const char *path, const char *name);
int gs_icon_default_tool(const char *path, const char *tool);
char *gs_icon_get_default_tool(const char *path);
int gs_icon_stack(const char *path, long stack);
long gs_icon_get_stack(const char *path);
int gs_icon_noposition(const char *path);
int gs_icon_swapcolors(const char *path);

/* Workbench (openwbobject and its kin) */
int gs_wb_object(const char *name, int what);   /* 0 open, 1 show, 2 close */

void gs_delay_ms(long ms);
void gs_init(void);
void gs_cleanup(void);

#endif
