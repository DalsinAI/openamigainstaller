/*
 * Downloads with libcurl (oi_net.h). On the Amiga, bsdsocket.library is
 * opened for the program's task and its errno pointed at the C library's,
 * which libcurl reads; gfopencurl opens gfopentls.library itself.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OI_NO_CURL

#include <curl/curl.h>

#ifdef OI_AMIGA
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <amitcp/socketbasetags.h>
#include <errno.h>
struct Library *SocketBase;
#endif

#define TRIES 4

static int inited;

static int net_open(void)
{
#ifdef OI_AMIGA
    if (!SocketBase) {
        if (!(SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4)))
            return -1;  /* no TCP/IP stack running */
        SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), (ULONG)&errno,
                       SBTM_SETVAL(SBTC_LOGTAGPTR), (ULONG)"OpenInstaller", TAG_DONE);
    }
#endif
    if (!inited) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            return -1;
        inited = 1;
    }
    return 0;
}

int gn_available(void)
{
#ifdef OI_AMIGA
    if (SocketBase)
        return 1;
    {
        struct Library *b = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
        if (!b)
            return 0;
        CloseLibrary(b);
        return 1;
    }
#else
    return 1;
#endif
}

typedef struct dl {
    FILE *fp;
    long long from, got, total;
    int (*progress)(void *, long long, long long);
    void *ud;
    int stopped;
} dl;

static size_t on_data(char *p, size_t size, size_t n, void *arg)
{
    dl *d = arg;
    size_t len = size * n;
    if (fwrite(p, 1, len, d->fp) != len)
        return 0;   /* the disk is full: curl stops with a write error */
    d->got += (long long)len;
    return len;
}

static int on_progress(void *arg, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    dl *d = arg;
    (void)ultotal;
    (void)ulnow;
    if (dltotal > 0)
        d->total = d->from + (long long)dltotal;
    if (d->progress && d->progress(d->ud, d->from + (long long)dlnow, d->total)) {
        d->stopped = 1;
        return 1;
    }
    return 0;
}

static long long file_len(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0)
        n = 0;
    fclose(f);
    return n;
}

/* The C library's name for an Amiga path is the path itself on the
 * Amiga; on the host the tests' volumes apply. */
#ifndef OI_AMIGA
static char *local_name(const char *apath)
{
    const char *colon = strchr(apath, ':');
    const char *vols = getenv("OI_VOLUMES");
    if (!colon)
        return oi_strdup(apath);
    if (!vols || !*vols)
        vols = ".";
    return oi_xprintf("%s/%.*s/%s", vols, (int)(colon - apath), apath, colon + 1);
}
#else
static char *local_name(const char *apath)
{
    return oi_strdup(apath);
}
#endif

int gn_download(const char *url, const char *path, const char *agent,
                int (*progress)(void *ud, long long now, long long total), void *ud,
                char *err, size_t errlen)
{
    char ebuf[CURL_ERROR_SIZE];
    char *local = local_name(path);
    dl d;
    int tries = 0, resume = 0, result = -1;
    if (!local) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    if (net_open()) {
        snprintf(err, errlen, "there is no network (no TCP/IP stack is running)");
        free(local);
        return -1;
    }
    memset(&d, 0, sizeof d);
    d.progress = progress;
    d.ud = ud;
    for (;;) {
        CURL *c = curl_easy_init();
        CURLcode rc;
        long code = 0;
        if (!c) {
            snprintf(err, errlen, "no memory for the download");
            break;
        }
        d.from = resume ? file_len(local) : 0;
        d.got = 0;
        d.fp = fopen(local, d.from ? "ab" : "wb");
        if (!d.fp) {
            snprintf(err, errlen, "can't write %s", path);
            curl_easy_cleanup(c);
            break;
        }
        ebuf[0] = 0;
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
        curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(c, CURLOPT_USERAGENT, agent);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, ebuf);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
        /* a stalled connection (under 100 bytes a second for a minute) is
         * dropped and tried again from where it got to */
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 100L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
        if (d.from)
            curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)d.from);
        rc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(c);
        if (fclose(d.fp) && rc == CURLE_OK)
            rc = CURLE_WRITE_ERROR;
        d.fp = NULL;
        if (rc == CURLE_OK) {
            result = 0;
            break;
        }
        if (d.stopped) {
            result = -2;
            snprintf(err, errlen, "stopped");
            break;
        }
        snprintf(err, errlen, "%s", ebuf[0] ? ebuf : curl_easy_strerror(rc));
        /* tried again from where it got to; a full disk or a page that
         * isn't there is not */
        if (++tries >= TRIES || rc == CURLE_WRITE_ERROR ||
            (code >= 400 && code < 500 && code != 408 && code != 416 && code != 429))
            break;
        resume = !(d.from && (rc == CURLE_RANGE_ERROR || code == 416));
    }
    free(local);
    return result;
}

void gn_cleanup(void)
{
    if (inited) {
        curl_global_cleanup();
        inited = 0;
    }
#ifdef OI_AMIGA
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
#endif
}

#else   /* OI_NO_CURL */

int gn_available(void)
{
    return 0;
}

int gn_download(const char *url, const char *path, const char *agent,
                int (*progress)(void *ud, long long now, long long total), void *ud,
                char *err, size_t errlen)
{
    (void)url; (void)path; (void)agent; (void)progress; (void)ud;
    snprintf(err, errlen, "this OpenInstaller was built without downloads");
    return -1;
}

void gn_cleanup(void)
{
}

#endif
