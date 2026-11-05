/*
 * The file layer on the host, for the tests: Amiga paths onto the host's
 * folders. NAME:rest is $OI_VOLUMES/NAME/rest (OI_VOLUMES defaults to
 * the current folder); an empty part of a path ("a//b", or a leading /)
 * is the parent, as in AmigaDOS.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_sys.h"
#include "oi_sys_common.h"

#ifndef OI_AMIGA

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char *host_path(const char *amiga)
{
    const char *colon = strchr(amiga, ':');
    const char *vols = getenv("OI_VOLUMES");
    const char *rest = amiga;
    char *out, *o;
    size_t cap;
    if (!vols || !*vols)
        vols = ".";
    if (amiga[0] == '/' && !colon)
        return oi_strdup(amiga);    /* a host path (the tests' scripts) */
    cap = strlen(vols) + strlen(amiga) * 3 + 8;
    out = malloc(cap);
    if (!out)
        return NULL;
    o = out;
    if (colon) {
        if (colon == amiga) {
            o += sprintf(o, "%s/SYS", vols);       /* ":x" is the boot volume's root */
        } else {
            o += sprintf(o, "%s/%.*s", vols, (int)(colon - amiga), amiga);
        }
        rest = colon + 1;
        if (*rest)
            *o++ = '/';
    }
    /* the rest, an empty part going up one */
    while (*rest) {
        const char *sl = strchr(rest, '/');
        size_t n = sl ? (size_t)(sl - rest) : strlen(rest);
        if (n == 0 && sl) {
            o += sprintf(o, "../");
        } else {
            memcpy(o, rest, n);
            o += n;
            if (sl)
                *o++ = '/';
        }
        rest += n + (sl ? 1 : 0);
    }
    *o = 0;
    if (!*out)
        strcpy(out, ".");
    return out;
}

int gs_exists(const char *path)
{
    char *h = host_path(path);
    struct stat st;
    int r = GS_NONE;
    if (h && stat(h, &st) == 0)
        r = S_ISDIR(st.st_mode) ? GS_DIR : GS_FILE;
    free(h);
    return r;
}

long long gs_size(const char *path)
{
    char *h = host_path(path);
    struct stat st;
    long long r = -1;
    if (h && stat(h, &st) == 0 && !S_ISDIR(st.st_mode))
        r = (long long)st.st_size;
    free(h);
    return r;
}

int gs_date_cmp(const char *a, const char *b, int *res)
{
    char *x = host_path(a), *y = host_path(b);
    struct stat sa, sb;
    int r = -1;
    if (x && y && stat(x, &sa) == 0 && stat(y, &sb) == 0) {
        *res = sa.st_mtime < sb.st_mtime ? -1 : sa.st_mtime > sb.st_mtime;
        r = 0;
    }
    free(x);
    free(y);
    return r;
}

int gs_makedir(const char *path)
{
    char *h = host_path(path);
    int r = -1;
    if (h) {
        size_t n = strlen(h);
        while (n > 1 && h[n - 1] == '/')
            h[--n] = 0;
        r = mkdir(h, 0777) == 0 || errno == EEXIST ? 0 : -1;
    }
    free(h);
    return r;
}

int gs_delete(const char *path)
{
    char *h = host_path(path);
    struct stat st;
    int r = -1;
    if (h && stat(h, &st) == 0)
        r = S_ISDIR(st.st_mode) ? rmdir(h) : unlink(h);
    free(h);
    return r ? -1 : 0;
}

int gs_rename(const char *from, const char *to)
{
    char *x = host_path(from), *y = host_path(to);
    int r = (x && y && access(y, F_OK) != 0) ? rename(x, y) : -1;
    free(x);
    free(y);
    return r ? -1 : 0;
}

int gs_copy(const char *from, const char *to,
            int (*progress)(void *ud, long long now, long long total), void *ud)
{
    char *x = host_path(from), *y = host_path(to);
    FILE *in = x ? fopen(x, "rb") : NULL, *out = NULL;
    char buf[16384];
    long long total = gs_size(from), done = 0;
    int r = -1;
    size_t n;
    if (in)
        out = y ? fopen(y, "wb") : NULL;
    if (in && out) {
        r = 0;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
            if (fwrite(buf, 1, n, out) != n) {
                r = -1;
                break;
            }
            done += (long long)n;
            if (progress && progress(ud, done, total)) {
                r = -2;
                break;
            }
        }
        if (ferror(in))
            r = -1;
    }
    if (in)
        fclose(in);
    if (out && fclose(out))
        r = -1;
    if (r && out && y)
        unlink(y);
    free(x);
    free(y);
    return r;
}

/* The host has no protection bits like the Amiga's: kept beside the file,
 * in NAME.oi-protect, for the tests. */
long gs_get_protect(const char *path)
{
    char *p, *h;
    FILE *f;
    long bits = 0;
    if (gs_exists(path) == GS_NONE)
        return -1;
    p = oi_xprintf("%s.oi-protect", path);
    h = p ? host_path(p) : NULL;
    f = h ? fopen(h, "r") : NULL;
    if (f) {
        if (fscanf(f, "%lx", &bits) != 1)
            bits = 0;
        fclose(f);
    }
    free(p);
    free(h);
    return bits;
}

int gs_set_protect(const char *path, long bits)
{
    char *p, *h;
    FILE *f;
    if (gs_exists(path) == GS_NONE)
        return -1;
    p = oi_xprintf("%s.oi-protect", path);
    h = p ? host_path(p) : NULL;
    if (bits == 0) {
        if (h)
            unlink(h);
        free(p);
        free(h);
        return 0;
    }
    f = h ? fopen(h, "w") : NULL;
    if (f) {
        fprintf(f, "%lx\n", (unsigned long)bits);
        fclose(f);
    }
    free(p);
    free(h);
    return f ? 0 : -1;
}

int gs_set_comment(const char *path, const char *comment)
{
    (void)path;
    (void)comment;
    return 0;
}

struct gs_dir {
    DIR *d;
    char *host;
    char name[512];
};

gs_dir *gs_opendir(const char *path)
{
    gs_dir *d = calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->host = host_path(*path ? path : "");
    d->d = d->host ? opendir(d->host) : NULL;
    if (!d->d) {
        free(d->host);
        free(d);
        return NULL;
    }
    return d;
}

const char *gs_readdir(gs_dir *d, int *type)
{
    struct dirent *e;
    while ((e = readdir(d->d)) != NULL) {
        char *full;
        struct stat st;
        size_t n = strlen(e->d_name);
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (n > 11 && !strcmp(e->d_name + n - 11, ".oi-protect"))
            continue;
        snprintf(d->name, sizeof d->name, "%s", e->d_name);
        full = oi_xprintf("%s/%s", d->host, e->d_name);
        *type = (full && stat(full, &st) == 0 && S_ISDIR(st.st_mode)) ? GS_DIR : GS_FILE;
        free(full);
        return d->name;
    }
    return NULL;
}

void gs_closedir(gs_dir *d)
{
    if (!d)
        return;
    closedir(d->d);
    free(d->host);
    free(d);
}

int gs_patmatch(const char *pattern, const char *name)
{
    return gs_common_patmatch(pattern, name);
}

char *gs_read_file(const char *path, size_t *len)
{
    char *h = host_path(path);
    FILE *f = h ? fopen(h, "rb") : NULL;
    char *buf = NULL;
    long n;
    free(h);
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)n + 1);
        if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
            free(buf);
            buf = NULL;
        } else if (buf) {
            buf[n] = 0;
            if (len)
                *len = (size_t)n;
        }
    }
    fclose(f);
    return buf;
}

int gs_write_file(const char *path, const char *data, size_t len, int append)
{
    char *h = host_path(path);
    FILE *f = h ? fopen(h, append ? "ab" : "wb") : NULL;
    int r = -1;
    free(h);
    if (!f)
        return -1;
    if (fwrite(data, 1, len, f) == len)
        r = 0;
    if (fclose(f))
        r = -1;
    return r;
}

char *gs_getenv(const char *name)
{
    char *p = oi_xprintf("ENV:%s", name);
    size_t n = 0;
    char *v = p ? gs_read_file(p, &n) : NULL;
    free(p);
    if (v) {
        /* one line, as GetVar gives it */
        char *nl = strchr(v, '\n');
        if (nl)
            *nl = 0;
    }
    return v;
}

int gs_setenv(const char *name, const char *value, int archive)
{
    char *p = oi_xprintf("%s:%s", archive ? "ENVARC" : "ENV", name);
    int r = p ? gs_write_file(p, value, strlen(value), 0) : -1;
    free(p);
    return r;
}

char *gs_getassign(const char *name, const char *opts)
{
    char *v = oi_xprintf("%s:", name);
    (void)opts;
    if (v && gs_exists(v) == GS_DIR)
        return v;
    free(v);
    return NULL;
}

int gs_makeassign(const char *name, const char *path, int add)
{
    (void)add;
    (void)name;
    (void)path;
    return 0;
}

char *gs_getdevice(const char *path)
{
    const char *colon = strchr(path, ':');
    if (!colon)
        return oi_strdup("SYS");
    return oi_xprintf("%.*s", (int)(colon - path), path);
}

long long gs_diskspace(const char *path)
{
    const char *v = getenv("OI_DISKSPACE");
    if (gs_exists(path) == GS_NONE)
        return -1;
    return v ? atoll(v) : 100LL * 1024 * 1024;
}

char *gs_expandpath(const char *path)
{
    if (gs_exists(path) == GS_NONE)
        return NULL;
    if (strchr(path, ':'))
        return oi_strdup(path);
    return oi_xprintf("SYS:%s", path);
}

long gs_getversion(const char *path, int resident)
{
    char *buf;
    size_t len = 0;
    long v;
    (void)resident;
    if (!path) {
        const char *os = getenv("OI_OSVERSION");
        return os ? strtol(os, NULL, 0) : (47L << 16) | 111;    /* OS 3.2.3: exec 47.111 */
    }
    buf = gs_read_file(path, &len);
    if (!buf) {
        if (!strchr(path, ':') && !strchr(path, '/')) {
            /* a library's name alone: LIBS:name */
            char *p = oi_xprintf("LIBS:%s", path);
            buf = p ? gs_read_file(p, &len) : NULL;
            free(p);
        }
        if (!buf)
            return -1;
    }
    v = gs_common_scan_version(buf, len);
    free(buf);
    return v;
}

unsigned long gs_getsum(const char *path, int *ok)
{
    size_t len = 0;
    char *buf = gs_read_file(path, &len);
    unsigned long s;
    *ok = buf != NULL;
    if (!buf)
        return 0;
    s = gs_common_crc32(0, (const unsigned char *)buf, len);
    free(buf);
    return s;
}

char *gs_database(const char *what)
{
    const char *v = NULL;
    char name[64];
    size_t i;
    snprintf(name, sizeof name, "OI_DB_%s", what);
    for (i = 0; name[i]; i++)
        if (name[i] == '-')
            name[i] = '_';
    v = getenv(name);
    if (v)
        return oi_strdup(v);
    if (!oi_stricmp(what, "cpu"))
        return oi_strdup("68040");
    if (!oi_stricmp(what, "fpu"))
        return oi_strdup("68040");
    if (!oi_stricmp(what, "vblank"))
        return oi_strdup("50");
    if (!oi_stricmp(what, "chiprev"))
        return oi_strdup("AGA");
    if (!oi_stricmp(what, "graphics-mem"))
        return oi_strdup("2097152");
    if (!oi_stricmp(what, "total-mem"))
        return oi_strdup("134217728");
    if (!oi_stricmp(what, "os"))
        return oi_strdup("AmigaOS");
    return oi_strdup("unknown");
}

long gs_run(const char *command, const char *dir, int wait)
{
    (void)dir;
    if (wait < 0 || !command)
        return 0;   /* the host doesn't restart */
    if (getenv("OI_RUN_LOG")) {
        gs_write_file(getenv("OI_RUN_LOG"), command, strlen(command), 1);
        gs_write_file(getenv("OI_RUN_LOG"), "\n", 1, 1);
        return 0;
    }
    return 0;
}

int gs_is_volume(const char *path)
{
    size_t n = strlen(path);
    return n > 1 && path[n - 1] == ':' && !strchr(path, '/') && strchr(path, ':') == path + n - 1;
}

int gs_volume_mounted(const char *volume)
{
    return gs_exists(volume) == GS_DIR;
}

int gs_icon_tooltype(const char *path, const char *name, const char *value, int remove)
{
    (void)path; (void)name; (void)value; (void)remove;
    return 0;
}

char *gs_icon_get_tooltype(const char *path, const char *name)
{
    (void)path; (void)name;
    return NULL;
}

int gs_icon_default_tool(const char *path, const char *tool)
{
    (void)path; (void)tool;
    return 0;
}

char *gs_icon_get_default_tool(const char *path)
{
    (void)path;
    return NULL;
}

int gs_icon_stack(const char *path, long stack)
{
    (void)path; (void)stack;
    return 0;
}

long gs_icon_get_stack(const char *path)
{
    (void)path;
    return 4096;
}

int gs_icon_noposition(const char *path)
{
    (void)path;
    return 0;
}

int gs_icon_swapcolors(const char *path)
{
    (void)path;
    return 0;
}

int gs_wb_object(const char *name, int what)
{
    (void)name; (void)what;
    return 0;
}

void gs_delay_ms(long ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}

void gs_init(void)
{
}

void gs_cleanup(void)
{
}

#endif
