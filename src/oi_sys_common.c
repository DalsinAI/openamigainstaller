/*
 * What both file layers share: AmigaDOS pattern matching, reading a
 * file's $VER:, and the checksum (getsum).
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#include "oi.h"
#include "oi_sys.h"
#include "oi_sys_common.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- patterns */

typedef struct cont {
    const char *p, *pe;
    const char *must_pass;      /* the match must have moved past this */
    const struct cont *next;
} cont;

static int lc(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if (c >= 0xc0 && c <= 0xde && c != 0xd7)
        return c + 32;      /* Latin-1 capitals */
    return c;
}

static const char *item_end(const char *p, const char *pe)
{
    if (p >= pe)
        return pe;
    switch (*p) {
    case '#':
    case '~':
        return item_end(p + 1, pe);
    case '\'':
        return p + 2 <= pe ? p + 2 : pe;
    case '(': {
        int depth = 0;
        for (; p < pe; p++) {
            if (*p == '\'' && p + 1 < pe) {
                p++;
                continue;
            }
            if (*p == '(')
                depth++;
            else if (*p == ')' && --depth == 0)
                return p + 1;
        }
        return pe;
    }
    case '[':
        for (p++; p < pe; p++) {
            if (*p == '\'' && p + 1 < pe) {
                p++;
                continue;
            }
            if (*p == ']')
                return p + 1;
        }
        return pe;
    default:
        return p + 1;
    }
}

static int class_match(const char *p, const char *pe, int ch)
{
    int neg = 0, hit = 0;
    p++;    /* [ */
    if (p < pe && *p == '~') {
        neg = 1;
        p++;
    }
    while (p < pe && *p != ']') {
        int lo = (unsigned char)*p, hi;
        if (lo == '\'' && p + 1 < pe)
            lo = (unsigned char)*++p;
        p++;
        hi = lo;
        if (p + 1 < pe && *p == '-' && p[1] != ']') {
            p++;
            hi = (unsigned char)*p;
            if (hi == '\'' && p + 1 < pe)
                hi = (unsigned char)*++p;
            p++;
        }
        if ((lc(ch) >= lc(lo) && lc(ch) <= lc(hi)) || (ch >= lo && ch <= hi))
            hit = 1;
    }
    return hit != neg;
}

static int mt(const char *p, const char *pe, const char *s, const char *se, const cont *k, int depth)
{
    if (depth > 2000)
        return 0;
    if (p >= pe) {
        if (!k)
            return s == se;
        if (k->must_pass && s <= k->must_pass)
            return 0;
        return mt(k->p, k->pe, s, se, k->next, depth + 1);
    }
    switch (*p) {
    case '#': {
        const char *ie = item_end(p + 1, pe);
        cont again;
        if (mt(ie, pe, s, se, k, depth + 1))
            return 1;
        /* once more, then the # again; only if it took something */
        again.p = p;
        again.pe = pe;
        again.must_pass = s;
        again.next = k;
        return mt(p + 1, ie, s, se, &again, depth + 1);
    }
    case '(': {
        const char *close = item_end(p, pe);
        const char *alt = p + 1, *q;
        const char *inner_end = close > p + 1 && close[-1] == ')' ? close - 1 : close;
        int d = 0;
        cont after;
        after.p = close;
        after.pe = pe;
        after.must_pass = NULL;
        after.next = k;
        for (q = alt; q <= inner_end; q++) {
            if (q < inner_end && *q == '\'' && q + 1 < inner_end) {
                q++;
                continue;
            }
            if (q < inner_end && *q == '(')
                d++;
            else if (q < inner_end && *q == ')')
                d--;
            if (q == inner_end || (*q == '|' && d == 0)) {
                if (mt(alt, q, s, se, &after, depth + 1))
                    return 1;
                alt = q + 1;
            }
        }
        return 0;
    }
    case '~': {
        const char *ie = item_end(p + 1, pe);
        const char *t;
        for (t = s; t <= se; t++)
            if (!mt(p + 1, ie, s, t, NULL, depth + 1) && mt(ie, pe, t, se, k, depth + 1))
                return 1;
        return 0;
    }
    case '?':
        return s < se && mt(p + 1, pe, s + 1, se, k, depth + 1);
    case '[': {
        const char *ie = item_end(p, pe);
        return s < se && class_match(p, ie, (unsigned char)*s) && mt(ie, pe, s + 1, se, k, depth + 1);
    }
    case '%':
        return mt(p + 1, pe, s, se, k, depth + 1);
    case '\'':
        if (p + 1 < pe) {
            p++;
            return s < se && lc((unsigned char)*p) == lc((unsigned char)*s) &&
                   mt(p + 1, pe, s + 1, se, k, depth + 1);
        }
        return mt(p + 1, pe, s, se, k, depth + 1);
    default:
        return s < se && lc((unsigned char)*p) == lc((unsigned char)*s) &&
               mt(p + 1, pe, s + 1, se, k, depth + 1);
    }
}

int gs_common_patmatch(const char *pattern, const char *name)
{
    return mt(pattern, pattern + strlen(pattern), name, name + strlen(name), NULL, 0);
}

/* ---------------------------------------------------------------- $VER: */

/* "$VER: name 47.19 (8.3.2021)" or a library's "name 47.19 (...)" ID:
 * the first number.number after the name. */
long gs_common_scan_version(const char *buf, size_t len)
{
    size_t i;
    for (i = 0; i + 5 < len; i++) {
        if (buf[i] == '$' && buf[i + 1] == 'V' && buf[i + 2] == 'E' && buf[i + 3] == 'R' && buf[i + 4] == ':') {
            size_t j = i + 5;
            /* past the name: to the first digit that follows a space */
            while (j < len && buf[j] == ' ')
                j++;
            while (j < len && buf[j] && buf[j] != '\n') {
                if ((buf[j] >= '0' && buf[j] <= '9') && (buf[j - 1] == ' ' || buf[j - 1] == '\t')) {
                    long v = 0, r = 0;
                    while (j < len && buf[j] >= '0' && buf[j] <= '9')
                        v = v * 10 + (buf[j++] - '0');
                    if (j < len && buf[j] == '.') {
                        j++;
                        while (j < len && buf[j] >= '0' && buf[j] <= '9')
                            r = r * 10 + (buf[j++] - '0');
                    }
                    return (v << 16) | (r & 0xffff);
                }
                j++;
            }
        }
    }
    return -1;
}

/* ---------------------------------------------------------------- getsum */

/* The checksum getsum gives: CRC-32 (IEEE 802.3), as a long. */
unsigned long gs_common_crc32(unsigned long crc, const unsigned char *p, size_t n)
{
    static unsigned long table[256];
    static int ready;
    size_t i;
    if (!ready) {
        unsigned long c;
        int k, j;
        for (k = 0; k < 256; k++) {
            c = (unsigned long)k;
            for (j = 0; j < 8; j++)
                c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
            table[k] = c;
        }
        ready = 1;
    }
    crc ^= 0xffffffffUL;
    for (i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return (crc ^ 0xffffffffUL) & 0xffffffffUL;
}
