/*
 * The script's reader: Installer scripts are lists in brackets, strings in
 * double or single quotes, numbers (decimal, $hex, %binary) and symbols,
 * with ; comments to the end of the line.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#include "oi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p, *end;
    int line;
    char *err;
    size_t errlen;
    int failed;
} rd;

static void fail(rd *r, const char *what)
{
    if (!r->failed) {
        snprintf(r->err, r->errlen, "Line %d: %s", r->line, what);
        r->failed = 1;
    }
}

static oi_node *node_new(int type, int line)
{
    oi_node *n = calloc(1, sizeof *n);
    if (n) {
        n->type = type;
        n->line = line;
    }
    return n;
}

void oi_node_free(oi_node *n)
{
    int i;
    if (!n)
        return;
    for (i = 0; i < n->nkid; i++)
        oi_node_free(n->kid[i]);
    free(n->kid);
    free(n->text);
    free(n);
}

static int add_kid(oi_node *list, oi_node *k)
{
    if ((list->nkid & 7) == 0) {
        oi_node **nk = realloc(list->kid, (size_t)(list->nkid + 8) * sizeof *nk);
        if (!nk)
            return -1;
        list->kid = nk;
    }
    list->kid[list->nkid++] = k;
    return 0;
}

static void skip_space(rd *r)
{
    while (r->p < r->end) {
        char c = *r->p;
        if (c == '\n') {
            r->line++;
            r->p++;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v' || c == 0x1a) {
            r->p++;
        } else if (c == ';') {
            while (r->p < r->end && *r->p != '\n')
                r->p++;
        } else {
            break;
        }
    }
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static oi_node *read_string(rd *r)
{
    char q = *r->p++;
    size_t cap = 64, len = 0;
    char *buf = malloc(cap);
    oi_node *n;
    int line = r->line;
    if (!buf) {
        fail(r, "out of memory");
        return NULL;
    }
    for (;;) {
        int c;
        if (r->p >= r->end) {
            free(buf);
            r->line = line;
            fail(r, "unterminated string");
            return NULL;
        }
        c = (unsigned char)*r->p++;
        if (c == q)
            break;
        if (c == '\n')
            r->line++;
        if (c == '\\' && r->p < r->end) {
            c = (unsigned char)*r->p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'v': c = '\v'; break;
            case 'e': c = 27; break;
            case 'h': c = 0x9b; break;  /* CSI, as the Installer has it */
            case 'x': {
                int v = 0, k = 0, h;
                while (k < 2 && r->p < r->end && (h = hexval((unsigned char)*r->p)) >= 0) {
                    v = v * 16 + h;
                    r->p++;
                    k++;
                }
                c = v;
                break;
            }
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
                int v = c - '0', k = 1;
                while (k < 3 && r->p < r->end && *r->p >= '0' && *r->p <= '7') {
                    v = v * 8 + (*r->p++ - '0');
                    k++;
                }
                c = v & 0xff;
                break;
            }
            case '\n':
                r->line++;
                break;
            default:
                break;      /* \" \' \\ and anything else: the character */
            }
        }
        if (len + 2 > cap) {
            char *nb;
            cap *= 2;
            nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                fail(r, "out of memory");
                return NULL;
            }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    buf[len] = 0;
    n = node_new(GN_STR, line);
    if (!n) {
        free(buf);
        fail(r, "out of memory");
        return NULL;
    }
    n->text = buf;
    n->num = (long)len;     /* the length: a string may hold a \0 */
    return n;
}

static int is_delim(char c)
{
    return c == '(' || c == ')' || c == '"' || c == '\'' || c == ';' ||
           c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* A number: [+-]digits[.digits], $hex or %binary. The fraction is left
 * out, as the Installer does (it has whole numbers only). */
static int parse_number(const char *s, size_t len, long *out)
{
    size_t i = 0;
    int neg = 0;
    unsigned long v = 0;
    if (len && (s[0] == '-' || s[0] == '+')) {
        neg = s[0] == '-';
        i = 1;
    }
    if (i >= len)
        return 0;
    if (s[i] == '$') {
        size_t start = ++i;
        for (; i < len; i++) {
            int h = hexval((unsigned char)s[i]);
            if (h < 0)
                return 0;
            v = v * 16 + (unsigned long)h;
        }
        if (i == start)
            return 0;
    } else if (s[i] == '%') {
        size_t start = ++i;
        for (; i < len; i++) {
            if (s[i] != '0' && s[i] != '1')
                return 0;
            v = v * 2 + (unsigned long)(s[i] - '0');
        }
        if (i == start)
            return 0;
    } else {
        size_t start = i;
        for (; i < len && s[i] >= '0' && s[i] <= '9'; i++)
            v = v * 10 + (unsigned long)(s[i] - '0');
        if (i == start)
            return 0;
        if (i < len) {
            if (s[i] != '.' || i + 1 >= len)
                return 0;
            for (i++; i < len; i++)
                if (s[i] < '0' || s[i] > '9')
                    return 0;
        }
    }
    *out = neg ? -(long)v : (long)v;
    return 1;
}

static oi_node *read_atom(rd *r)
{
    const char *s = r->p;
    size_t len;
    long num;
    oi_node *n;
    while (r->p < r->end && !is_delim(*r->p))
        r->p++;
    len = (size_t)(r->p - s);
    if (parse_number(s, len, &num)) {
        n = node_new(GN_NUM, r->line);
        if (n)
            n->num = num;
    } else {
        n = node_new(GN_SYM, r->line);
        if (n) {
            n->text = malloc(len + 1);
            if (!n->text) {
                free(n);
                n = NULL;
            } else {
                memcpy(n->text, s, len);
                n->text[len] = 0;
            }
        }
    }
    if (!n)
        fail(r, "out of memory");
    return n;
}

static oi_node *read_item(rd *r, int depth);

static oi_node *read_list(rd *r, int depth)
{
    oi_node *list = node_new(GN_LIST, r->line);
    int open_line = r->line;
    if (!list) {
        fail(r, "out of memory");
        return NULL;
    }
    r->p++;     /* ( */
    if (depth > 200) {
        fail(r, "nested too deeply");
        oi_node_free(list);
        return NULL;
    }
    for (;;) {
        oi_node *k;
        skip_space(r);
        if (r->p >= r->end) {
            r->line = open_line;
            fail(r, "missing ')'");
            oi_node_free(list);
            return NULL;
        }
        if (*r->p == ')') {
            r->p++;
            return list;
        }
        k = read_item(r, depth + 1);
        if (!k || add_kid(list, k)) {
            oi_node_free(k);
            fail(r, "out of memory");
            oi_node_free(list);
            return NULL;
        }
    }
}

static oi_node *read_item(rd *r, int depth)
{
    char c = *r->p;
    if (c == '(')
        return read_list(r, depth);
    if (c == ')') {
        fail(r, "unexpected ')'");
        return NULL;
    }
    if (c == '"' || c == '\'')
        return read_string(r);
    return read_atom(r);
}

oi_node *oi_parse(const char *text, size_t len, char *err, size_t errlen)
{
    rd r;
    oi_node *top = node_new(GN_LIST, 1);
    r.p = text;
    r.end = text + len;
    r.line = 1;
    r.err = err;
    r.errlen = errlen;
    r.failed = 0;
    if (err && errlen)
        err[0] = 0;
    if (!top)
        return NULL;
    for (;;) {
        oi_node *k;
        skip_space(&r);
        if (r.p >= r.end)
            break;
        if (*r.p != '(') {
            /* the Installer only takes lists at the top: an atom there is
             * read and left out (some scripts end with a stray word) */
            if (*r.p == ')') {
                fail(&r, "unexpected ')'");
                break;
            }
            k = read_item(&r, 1);
            oi_node_free(k);
            if (r.failed)
                break;
            continue;
        }
        k = read_item(&r, 1);
        if (!k || add_kid(top, k)) {
            oi_node_free(k);
            if (!r.failed)
                fail(&r, "out of memory");
            break;
        }
    }
    if (r.failed) {
        oi_node_free(top);
        return NULL;
    }
    return top;
}
