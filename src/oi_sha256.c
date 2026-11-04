/*
 * SHA-256 (FIPS 180-4) of a file, for (download (checksum ...)).
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#include "oi.h"
#include "oi_net.h"
#include "oi_sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long u32;      /* at least 32 bits; held to 32 below */

typedef struct {
    u32 h[8];
    unsigned char buf[64];
    size_t fill;
    unsigned long long bytes;
} sha256;

static const u32 K[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL,
};

#define M32(x) ((x) & 0xffffffffUL)
#define ROR(x, n) M32(((x) >> (n)) | ((x) << (32 - (n))))

static void block(sha256 *s, const unsigned char *p)
{
    u32 w[64], a, b, c, d, e, f, g, h;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((u32)p[i * 4] << 24) | ((u32)p[i * 4 + 1] << 16) | ((u32)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        u32 s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = M32(w[i - 16] + s0 + w[i - 7] + s1);
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
    e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (i = 0; i < 64; i++) {
        u32 S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        u32 ch = (e & f) ^ (M32(~e) & g);
        u32 t1 = M32(h + S1 + ch + K[i] + w[i]);
        u32 S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        u32 mj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = M32(S0 + mj);
        h = g; g = f; f = e; e = M32(d + t1);
        d = c; c = b; b = a; a = M32(t1 + t2);
    }
    s->h[0] = M32(s->h[0] + a); s->h[1] = M32(s->h[1] + b);
    s->h[2] = M32(s->h[2] + c); s->h[3] = M32(s->h[3] + d);
    s->h[4] = M32(s->h[4] + e); s->h[5] = M32(s->h[5] + f);
    s->h[6] = M32(s->h[6] + g); s->h[7] = M32(s->h[7] + h);
}

static void init(sha256 *s)
{
    static const u32 iv[8] = { 0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
                               0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL };
    memcpy(s->h, iv, sizeof iv);
    s->fill = 0;
    s->bytes = 0;
}

static void update(sha256 *s, const unsigned char *p, size_t n)
{
    s->bytes += n;
    while (n) {
        size_t k = 64 - s->fill;
        if (k > n)
            k = n;
        memcpy(s->buf + s->fill, p, k);
        s->fill += k;
        p += k;
        n -= k;
        if (s->fill == 64) {
            block(s, s->buf);
            s->fill = 0;
        }
    }
}

static void finish(sha256 *s, char out[65])
{
    unsigned long long bits = s->bytes * 8;
    unsigned char pad = 0x80, z = 0, len[8];
    int i;
    update(s, &pad, 1);
    while (s->fill != 56)
        update(s, &z, 1);
    for (i = 0; i < 8; i++)
        len[i] = (unsigned char)(bits >> (56 - i * 8));
    update(s, len, 8);
    for (i = 0; i < 8; i++)
        sprintf(out + i * 8, "%08lx", (unsigned long)s->h[i]);
    out[64] = 0;
}

int gn_sha256_file(const char *path, char out[65])
{
    size_t len = 0;
    char *data = gs_read_file(path, &len);
    sha256 s;
    if (!data)
        return -1;
    init(&s);
    update(&s, (const unsigned char *)data, len);
    finish(&s, out);
    free(data);
    return 0;
}
