/*
 * Downloads, through libcurl: on the Amiga DalsinAI/gfopencurl's (HTTPS
 * through gfopentls.library and its CA list in S:GFOpenTLS), linked into
 * the program; on the host the system's. A build without curl
 * (OI_NO_CURL) has none, and (download) says so.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#ifndef OI_NET_H
#define OI_NET_H

#include <stddef.h>

/* Is there a network to download from (a TCP/IP stack running)? */
int gn_available(void);

/* Download url to path. progress() is called now and then; a non-zero
 * answer stops the download (it returns -2). Returns 0 when done, -1 with
 * err filled in when it failed. A broken transfer is tried again from
 * where it got to, up to four times. */
int gn_download(const char *url, const char *path, const char *agent,
                int (*progress)(void *ud, long long now, long long total), void *ud,
                char *err, size_t errlen);

void gn_cleanup(void);

/* SHA-256 of a file, as 64 lower-case hex digits; 0 when done. */
int gn_sha256_file(const char *path, char out[65]);

#endif
