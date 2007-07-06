/*
 * Copyright (c) 2007 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* #define DEBUG */

#define ALT_BITSTREAM_READER_LE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/adler32.h"
#include "libavutil/bswap.h"
#include "libavutil/crc.h"
#include "get_bits.h"
#include "inflate.h"

#define CONTAINER_RAW  0
#define CONTAINER_ZLIB 1
#define CONTAINER_GZIP 2

#define DEFLATE_TYPE_NOCOMP   0
#define DEFLATE_TYPE_FIXED    1
#define DEFLATE_TYPE_DYNAMIC  2
#define DEFLATE_TYPE_RESERVED 3

#define ZLIB_BUFSIZE 32768

enum AVInflateState {
    AV_INFLATE_INIT,
    AV_INFLATE_MAGIC,

    AV_INFLATE_GZCOMP,
    AV_INFLATE_GZFLAGS,
    AV_INFLATE_GZHEAD,
    AV_INFLATE_GZXLEN,
    AV_INFLATE_GZXDATA,
    AV_INFLATE_GZNAME,
    AV_INFLATE_GZCOMM,
    AV_INFLATE_GZHCRC,

    AV_INFLATE_BSTART,
    AV_INFLATE_BFINAL,

    AV_INFLATE_NCLEN,
    AV_INFLATE_NCDATA,

    AV_INFLATE_HLIT,
    AV_INFLATE_CLLEN,
    AV_INFLATE_CLV,
    AV_INFLATE_CLR1,
    AV_INFLATE_CLR2,
    AV_INFLATE_CLR3,

    AV_INFLATE_CODE,
    AV_INFLATE_LENEXTRA,
    AV_INFLATE_DDIST,
    AV_INFLATE_FDIST,
    AV_INFLATE_DISTEXTRA,

    AV_INFLATE_END,

    AV_INFLATE_FOOTER,
    AV_INFLATE_EOF,
};

struct AVInflateContext {
    const AVClass *av_class;

    unsigned int container;

    enum AVInflateState state;

    GetBitContext gb;
    unsigned int skip;

    VLC cl_vlc;
    VLC ll_vlc;
    VLC dist_vlc;

    unsigned int bfinal;
    unsigned int btype;

    unsigned int nclen;

    uint8_t ll_len[288 + 32];

    unsigned int hlit;
    unsigned int hdist;
    unsigned int hclen;

    uint8_t cl_len[19];
    unsigned int ncl;

    unsigned int clv;
    unsigned int clr;

    unsigned int i;

    unsigned int code;
    unsigned int len;
    unsigned int dist;

    unsigned int gzflags;
    unsigned int gzcomp;
    unsigned int gzskip;

    unsigned int tailsize;
    unsigned int tailbits;
    uint8_t tailbuf[8];

    unsigned int cpoff;
    unsigned int cplen;

    unsigned int bufsize;
    uint8_t *buf;

    unsigned int csum;
    const AVCRC *crc;
    unsigned int osize;
};

static const uint8_t cl_perm[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static const uint8_t len_tab[29][2] = {
    { 0,   0 }, { 0,   1 }, { 0,   2 }, { 0,   3 },
    { 0,   4 }, { 0,   5 }, { 0,   6 }, { 0,   7 },
    { 1,   8 }, { 1,  10 }, { 1,  12 }, { 1,  14 },
    { 2,  16 }, { 2,  20 }, { 2,  24 }, { 2,  28 },
    { 3,  32 }, { 3,  40 }, { 3,  48 }, { 3,  56 },
    { 4,  64 }, { 4,  80 }, { 4,  96 }, { 4, 112 },
    { 5, 128 }, { 5, 160 }, { 5, 192 }, { 5, 224 },
    { 0, 255 },
};

static const uint8_t dist_tab[32][2] = {
    {  0, 0 }, {  0, 1 }, {  0, 2 }, {  0, 3 },
    {  1, 2 }, {  1, 3 }, {  2, 2 }, {  2, 3 },
    {  3, 2 }, {  3, 3 }, {  4, 2 }, {  4, 3 },
    {  5, 2 }, {  5, 3 }, {  6, 2 }, {  6, 3 },
    {  7, 2 }, {  7, 3 }, {  8, 2 }, {  8, 3 },
    {  9, 2 }, {  9, 3 }, { 10, 2 }, { 10, 3 },
    { 11, 2 }, { 11, 3 }, { 12, 2 }, { 12, 3 },
    { 13, 2 }, { 13, 3 },
};

static const uint16_t ll_vlc_codes[288] = {
  0xc,  0x8c, 0x4c,  0xcc, 0x2c,  0xac, 0x6c,  0xec,
 0x1c,  0x9c, 0x5c,  0xdc, 0x3c,  0xbc, 0x7c,  0xfc,
  0x2,  0x82, 0x42,  0xc2, 0x22,  0xa2, 0x62,  0xe2,
 0x12,  0x92, 0x52,  0xd2, 0x32,  0xb2, 0x72,  0xf2,
  0xa,  0x8a, 0x4a,  0xca, 0x2a,  0xaa, 0x6a,  0xea,
 0x1a,  0x9a, 0x5a,  0xda, 0x3a,  0xba, 0x7a,  0xfa,
  0x6,  0x86, 0x46,  0xc6, 0x26,  0xa6, 0x66,  0xe6,
 0x16,  0x96, 0x56,  0xd6, 0x36,  0xb6, 0x76,  0xf6,
  0xe,  0x8e, 0x4e,  0xce, 0x2e,  0xae, 0x6e,  0xee,
 0x1e,  0x9e, 0x5e,  0xde, 0x3e,  0xbe, 0x7e,  0xfe,
  0x1,  0x81, 0x41,  0xc1, 0x21,  0xa1, 0x61,  0xe1,
 0x11,  0x91, 0x51,  0xd1, 0x31,  0xb1, 0x71,  0xf1,
  0x9,  0x89, 0x49,  0xc9, 0x29,  0xa9, 0x69,  0xe9,
 0x19,  0x99, 0x59,  0xd9, 0x39,  0xb9, 0x79,  0xf9,
  0x5,  0x85, 0x45,  0xc5, 0x25,  0xa5, 0x65,  0xe5,
 0x15,  0x95, 0x55,  0xd5, 0x35,  0xb5, 0x75,  0xf5,
  0xd,  0x8d, 0x4d,  0xcd, 0x2d,  0xad, 0x6d,  0xed,
 0x1d,  0x9d, 0x5d,  0xdd, 0x3d,  0xbd, 0x7d,  0xfd,
 0x13, 0x113, 0x93, 0x193, 0x53, 0x153, 0xd3, 0x1d3,
 0x33, 0x133, 0xb3, 0x1b3, 0x73, 0x173, 0xf3, 0x1f3,
  0xb, 0x10b, 0x8b, 0x18b, 0x4b, 0x14b, 0xcb, 0x1cb,
 0x2b, 0x12b, 0xab, 0x1ab, 0x6b, 0x16b, 0xeb, 0x1eb,
 0x1b, 0x11b, 0x9b, 0x19b, 0x5b, 0x15b, 0xdb, 0x1db,
 0x3b, 0x13b, 0xbb, 0x1bb, 0x7b, 0x17b, 0xfb, 0x1fb,
  0x7, 0x107, 0x87, 0x187, 0x47, 0x147, 0xc7, 0x1c7,
 0x27, 0x127, 0xa7, 0x1a7, 0x67, 0x167, 0xe7, 0x1e7,
 0x17, 0x117, 0x97, 0x197, 0x57, 0x157, 0xd7, 0x1d7,
 0x37, 0x137, 0xb7, 0x1b7, 0x77, 0x177, 0xf7, 0x1f7,
  0xf, 0x10f, 0x8f, 0x18f, 0x4f, 0x14f, 0xcf, 0x1cf,
 0x2f, 0x12f, 0xaf, 0x1af, 0x6f, 0x16f, 0xef, 0x1ef,
 0x1f, 0x11f, 0x9f, 0x19f, 0x5f, 0x15f, 0xdf, 0x1df,
 0x3f, 0x13f, 0xbf, 0x1bf, 0x7f, 0x17f, 0xff, 0x1ff,
  0x0,  0x40, 0x20,  0x60, 0x10,  0x50, 0x30,  0x70,
  0x8,  0x48, 0x28,  0x68, 0x18,  0x58, 0x38,  0x78,
  0x4,  0x44, 0x24,  0x64, 0x14,  0x54, 0x34,  0x74,
  0x3,  0x83, 0x43,  0xc3, 0x23,  0xa3, 0x63,  0xe3,
};

static const uint8_t ll_vlc_len[288] = {
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9,
  7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7,
  8, 8, 8, 8, 8, 8, 8, 8,
};

static inline unsigned int
rev_bits16(unsigned int v, unsigned int nb)
{
    unsigned int r;

    if (nb <= 8)
        return av_reverse[v] >> (8 - nb);

    r = av_reverse[v & 255] << 8;
    r |= av_reverse[v >> 8];
    return r >> (16 - nb);
}

static unsigned int
build_codes(unsigned int ncodes, uint8_t *clen, unsigned int *codes)
{
    unsigned int next_code[16];
    unsigned int cl_count[16];
    unsigned int max_bits = 0;
    unsigned int max_code = 0;
    unsigned int code;
    unsigned int i;

    memset(cl_count, 0, sizeof(cl_count));

    for (i = 0; i < ncodes; i++) {
        cl_count[clen[i]]++;
        max_bits = FFMAX(max_bits, clen[i]);
        if (clen[i])
            max_code = i;
    }

    dprintf(NULL, "build_codes: ncodes=%d max_code=%d max_bits=%d\n",
            ncodes, max_code, max_bits);

    code = 0;
    cl_count[0] = 0;

    for (i = 1; i <= max_bits; i++) {
        code = (code + cl_count[i-1]) << 1;
        next_code[i] = code;
    }

    for (i = 0; i <= max_code; i++) {
        unsigned int len = clen[i];
        if (len) {
            codes[i] = rev_bits16(next_code[len]++, len);
        } else {
            codes[i] = 0;
        }
    }

    return max_code + 1;
}

static int
build_vlc_fixed(AVInflateContext *ctx)
{
    if(init_vlc(&ctx->ll_vlc, 9, 288,
                ll_vlc_len, sizeof(ll_vlc_len[0]), sizeof(ll_vlc_len[0]),
                ll_vlc_codes, sizeof(ll_vlc_codes[0]), sizeof(ll_vlc_codes[0]),
                INIT_VLC_LE))
        return -1;

    return 0;
}

static int
build_dynamic_codes(AVInflateContext *ctx)
{
    unsigned int ll_codes[288];
    unsigned int dist_codes[32];
    unsigned int nll, ndist;
    uint8_t *dist_len = ctx->ll_len + ctx->hlit + 257;

    nll = build_codes(ctx->hlit + 257, ctx->ll_len, ll_codes);
    ndist = build_codes(ctx->hdist + 1, dist_len, dist_codes);

    if(init_vlc(&ctx->ll_vlc, 9, nll,
                ctx->ll_len, sizeof(ctx->ll_len[0]), sizeof(ctx->ll_len[0]),
                ll_codes, sizeof(ll_codes[0]), sizeof(ll_codes[0]),
                INIT_VLC_LE))
        return -1;;

    if(init_vlc(&ctx->dist_vlc, 9, ndist,
                dist_len, sizeof(dist_len[0]), sizeof(dist_len[0]),
                dist_codes, sizeof(dist_codes[0]), sizeof(dist_codes[0]),
                INIT_VLC_LE))
        return -1;

    return 0;
}

static int
build_codelen_codes(AVInflateContext *ctx)
{
    unsigned int cl_codes[19];
    unsigned int ncl;

    ncl = build_codes(19, ctx->cl_len, cl_codes);

    if(init_vlc(&ctx->cl_vlc, 7, ncl,
                ctx->cl_len, sizeof(ctx->cl_len[0]), sizeof(ctx->cl_len[0]),
                cl_codes, sizeof(cl_codes[0]), sizeof(cl_codes[0]),
                INIT_VLC_LE))
        return -1;

    return 0;

}

#define check_bits(label, gb, n)                                        \
    case AV_INFLATE_ ## label:                                          \
        ctx->state = AV_INFLATE_ ## label;                              \
        if ((n) > (gb)->size_in_bits - get_bits_count(gb) && insize) {  \
            needbits = n;                                               \
            goto outbits;                                               \
        }

static inline void
check_tail(AVInflateContext *ctx, const uint8_t *inbuf, unsigned int insize)
{
    if (ctx->tailbits) {
        unsigned int rb = get_bits_count(&ctx->gb);
        if (rb >= ctx->tailbits) {
            dprintf(ctx, "tailbits=%d rb=%d\n", ctx->tailbits, rb);
            init_get_bits(&ctx->gb, inbuf, insize * 8);
            skip_bits_long(&ctx->gb, rb - ctx->tailbits);
            ctx->tailbits = 0;
        }
    }
}

#define rs_bits(what, label, dst, gb, n)                        \
    do {                                                        \
        check_bits(label, gb, n);                               \
        (dst) = what##_bits((gb), (n));                         \
        dprintf(ctx, "%-9s %-9s %4x %4x\n",                     \
                #what, #label, n, dst);                         \
        check_tail(ctx, inbuf, insize);                         \
    } while(0)

#define read_bits(label, dst, gb, n) rs_bits(get, label, dst, gb, n)
#define show_bits(label, dst, gb, n) rs_bits(show, label, dst, gb, n)

#define read_vlc(label, dst, gb, tab)                           \
    do {                                                        \
        check_bits(label, gb, ctx->tab.bits * 2);               \
        (dst) = get_vlc2(gb, ctx->tab.table, ctx->tab.bits, 2); \
        dprintf(ctx, "read_vlc  %-9s %4x %c\n", #label,         \
                dst, (dst)<127 && (dst)>32? (dst): '.');        \
        check_tail(ctx, inbuf, insize);                         \
    } while(0)

#define decode_code_lens()                                              \
do {                                                                    \
    ctx->i = 0;                                                         \
    while (ctx->i < ctx->hlit + 257 + ctx->hdist + 1) {                 \
        read_vlc(CLV, ctx->clv, &ctx->gb, cl_vlc);                      \
                                                                        \
        if (ctx->clv < 16) {                                            \
            ctx->ll_len[ctx->i++] = ctx->clv;                           \
        } else if (ctx->clv < 19) {                                     \
            ctx->clr = 0;                                               \
                                                                        \
            if (ctx->clv == 16) {                                       \
                read_bits(CLR1, ctx->clr, &ctx->gb, 2);                 \
                ctx->clr += 3;                                          \
                ctx->clv = ctx->ll_len[ctx->i-1];                       \
            } else if (ctx->clv == 17) {                                \
                read_bits(CLR2, ctx->clr, &ctx->gb, 3);                 \
                ctx->clr += 3;                                          \
                ctx->clv = 0;                                           \
            } else if (ctx->clv == 18) {                                \
                read_bits(CLR3, ctx->clr, &ctx->gb, 7);                 \
                ctx->clr += 11;                                         \
                ctx->clv = 0;                                           \
            }                                                           \
                                                                        \
            while (ctx->clr--) {                                        \
                ctx->ll_len[ctx->i++] = ctx->clv;                       \
            }                                                           \
        } else {                                                        \
            av_log(ctx, AV_LOG_ERROR,                                   \
                   "decode_code_lens: invalid code %d\n", ctx->clv);    \
            goto err;                                                   \
        }                                                               \
    }                                                                   \
} while(0)

#define build_vlc_dynamic()                                             \
do {                                                                    \
    check_bits(HLIT, &ctx->gb, 14);                                     \
    ctx->hlit  = get_bits(&ctx->gb, 5);                                 \
    ctx->hdist = get_bits(&ctx->gb, 5);                                 \
    ctx->hclen = get_bits(&ctx->gb, 4);                                 \
                                                                        \
    dprintf(ctx, "hlit=%d hdist=%d hclen=%d\n",                         \
            ctx->hlit, ctx->hdist, ctx->hclen);                         \
                                                                        \
    for (ctx->i = 0; ctx->i < ctx->hclen + 4; ctx->i++) {               \
        read_bits(CLLEN, ctx->cl_len[cl_perm[ctx->i]], &ctx->gb, 3);    \
    }                                                                   \
    for (; ctx->i < 19; ctx->i++) {                                     \
        ctx->cl_len[cl_perm[ctx->i]] = 0;                               \
    }                                                                   \
                                                                        \
    if (build_codelen_codes(ctx))                                       \
        goto err;                                                       \
                                                                        \
    decode_code_lens();                                                 \
                                                                        \
    free_vlc(&ctx->cl_vlc);                                             \
                                                                        \
    if (build_dynamic_codes(ctx))                                       \
        goto err;                                                       \
} while(0)

static void
copy_bytes(uint8_t *dst, const uint8_t *src, unsigned int len)
{
    while (len--)
        *dst++ = *src++;
}

static int
copy_offset(AVInflateContext *ctx, uint8_t *p,
            const uint8_t *start, const uint8_t *end,
            unsigned int offset, unsigned int len)
{
    unsigned int outlen = FFMIN(len, end - p);

    if (offset <= p - start) {
        copy_bytes(p, p - offset, outlen);
    } else {
        unsigned int os = p - start;
        unsigned int bp = offset - os;
        unsigned int bl = FFMIN(outlen, bp);

        dprintf(ctx, "os=%d offset=%d outlen=%d bp=%d bufsize=%d bl=%d\n",
                os, offset, outlen, bp, ctx->bufsize, bl);

        if (bp > ctx->bufsize) {
            av_log(ctx, AV_LOG_ERROR, "offset too large: %d > %d\n",
                   offset, os + ctx->bufsize);
            return -1;
        }

        copy_bytes(p, ctx->buf + ctx->bufsize - bp, bl);
        if (bl < outlen)
            copy_bytes(p + bl, start, outlen - bl);
    }

    ctx->cplen = len - outlen;
    ctx->cpoff = offset;

    return outlen;
}

extern int
av_inflate(AVInflateContext *ctx, uint8_t *outbuf, unsigned int *outsize,
           const uint8_t *inbuf, unsigned int insize)
{
    uint8_t *outend = outbuf + *outsize;
    uint8_t *out = outbuf;
    unsigned int csum = 0;
    unsigned int gzisize = 0;
    unsigned int needbits = 0;
    unsigned int bitpos;
    unsigned int bytepos = 0;
    unsigned int nlen;
    unsigned int tmp;
    unsigned int magic;
    int ncopy;

    if (!*outsize)
        return 0;

    dprintf(ctx, "state=%d tailsize=%d skip=%d insize=%d\n",
            ctx->state, ctx->tailsize, ctx->skip, insize);

    if (ctx->cplen)
        out += copy_offset(ctx, out, outbuf, outend, ctx->cpoff, ctx->cplen);

    if (out == outend)
        goto out;

    if (ctx->tailsize) {
        unsigned int td = sizeof(ctx->tailbuf) - ctx->tailsize;
        ctx->tailbits = ctx->tailsize * 8;
        if (td) {
            td = FFMIN(td, insize);
            memcpy(ctx->tailbuf + ctx->tailsize, inbuf, td);
            ctx->tailsize += td;
            memset(ctx->tailbuf + ctx->tailsize, 0,
                   sizeof(ctx->tailbuf) - ctx->tailsize);
        }
        init_get_bits(&ctx->gb, ctx->tailbuf, ctx->tailsize * 8);
    } else {
        init_get_bits(&ctx->gb, inbuf, insize * 8);
        ctx->tailbits = 0;
    }

    if (ctx->skip)
        skip_bits_long(&ctx->gb, ctx->skip);

#define gzskip(label)                           \
    do {                                        \
        read_bits(label, tmp, &ctx->gb, 8);     \
    } while(--ctx->gzskip)

#define gznull(label)                           \
    do {                                        \
        read_bits(label, tmp, &ctx->gb, 8);     \
    } while(tmp)

    switch (ctx->state) {
    case AV_INFLATE_INIT:

        show_bits(MAGIC, magic, &ctx->gb, 16);
        if (magic == 0x8b1f) {
            dprintf(ctx, "gzip format\n");

            skip_bits(&ctx->gb, 16);
            read_bits(GZCOMP, ctx->gzcomp, &ctx->gb, 8);
            read_bits(GZFLAGS, ctx->gzflags, &ctx->gb, 8);
            dprintf(ctx, "gzip CM=%d FLG=%x\n", ctx->gzcomp, ctx->gzflags);
            ctx->gzskip = 6;
            gzskip(GZHEAD);
            if (ctx->gzflags & 4) {
                read_bits(GZXLEN, ctx->gzskip, &ctx->gb, 16);
                gzskip(GZXDATA);
            }
            if (ctx->gzflags & 8) {
                gznull(GZNAME);
            }
            if (ctx->gzflags & 16) {
                gznull(GZCOMM);
            }
            if (ctx->gzflags & 2) {
                read_bits(GZHCRC, tmp, &ctx->gb, 16);
            }

            ctx->container = CONTAINER_GZIP;
            ctx->csum = 0xffffffff;
            ctx->crc = av_crc_get_table(AV_CRC_32_IEEE_LE);
        } else if (av_bswap16(magic) % 31 == 0) {
            unsigned cm = magic & 0xf;
            unsigned cinfo = (magic >> 4) & 0xf;
            unsigned flg = magic >> 8;

            if (cm == 8 && cinfo <= 7) {
                dprintf(ctx, "zlib format\n");

                if (flg & 0x20) {   /* FDICT */
                    av_log(ctx, AV_LOG_ERROR, "preset dictionary flag set\n");
                    goto err;
                }

                skip_bits(&ctx->gb, 16);
                ctx->container = CONTAINER_ZLIB;
                ctx->csum = 1;
            }
        }

    case AV_INFLATE_BSTART:

        while (!ctx->bfinal && out < outend) {
            check_bits(BFINAL, &ctx->gb, 3);
            ctx->bfinal = get_bits1(&ctx->gb);
            ctx->btype = get_bits(&ctx->gb, 2);

            dprintf(ctx, "bfinal=%d btype=%d\n", ctx->bfinal, ctx->btype);

            if (ctx->btype == DEFLATE_TYPE_NOCOMP) {
                align_get_bits(&ctx->gb);
                check_bits(NCLEN, &ctx->gb, 32);
                ctx->nclen = get_bits(&ctx->gb, 16);
                nlen = get_bits(&ctx->gb, 16);

                if ((ctx->nclen ^ nlen) != 0xffff) {
                    av_log(ctx, AV_LOG_ERROR,
                           "corrupt uncompressed block length: %x %x\n",
                           ctx->nclen, nlen);
                    goto err;
                }

                while (ctx->nclen && out < outend) {
                    read_bits(NCDATA, *out, &ctx->gb, 8);
                    out++;
                    ctx->nclen--;
                }

                ctx->state = ctx->nclen? AV_INFLATE_NCDATA: AV_INFLATE_BSTART;
            } else if (ctx->btype != DEFLATE_TYPE_RESERVED) {
                if (ctx->btype == DEFLATE_TYPE_DYNAMIC) {
                    build_vlc_dynamic();
                } else {
                    build_vlc_fixed(ctx);
                }

                do {
                    read_vlc(CODE, ctx->code, &ctx->gb, ll_vlc);
                    if (ctx->code < 256) {
                        *out++ = ctx->code;
                    } else if (ctx->code > 256 && ctx->code < 286) {
                        ctx->code -= 257;

                        ctx->len = len_tab[ctx->code][1] + 3;
                        if (len_tab[ctx->code][0]) {
                            read_bits(LENEXTRA, tmp, &ctx->gb,
                                      len_tab[ctx->code][0]);
                            ctx->len += tmp;
                        }

                        if (ctx->btype == DEFLATE_TYPE_DYNAMIC) {
                            read_vlc(DDIST, ctx->code, &ctx->gb, dist_vlc);
                        } else {
                            read_bits(FDIST, ctx->code, &ctx->gb, 5);
                            ctx->code = av_reverse[ctx->code] >> 3;
                        }

                        ctx->dist = (dist_tab[ctx->code][1] << dist_tab[ctx->code][0]) + 1;
                        if (dist_tab[ctx->code][0]) {
                            read_bits(DISTEXTRA, tmp, &ctx->gb,
                                      dist_tab[ctx->code][0]);
                            ctx->dist += tmp;
                        }

                        ncopy = copy_offset(ctx, out, outbuf, outend,
                                            ctx->dist, ctx->len);
                        if (ncopy < 0)
                            goto err;
                        out += ncopy;
                    } else if (ctx->code == 256) {
                        free_vlc(&ctx->ll_vlc);
                        free_vlc(&ctx->dist_vlc);
                        ctx->state = AV_INFLATE_BSTART;
                    } else {
                        av_log(ctx, AV_LOG_ERROR, "invalid code %d\n", ctx->code);
                        break;
                    }
                } while (ctx->code != 256 && out < outend);

                if (ctx->code != 256 && out == outend) {
                    ctx->state = AV_INFLATE_CODE;
                    break;
                }
            } else {
                av_log(ctx, AV_LOG_ERROR, "invalid block type %d\n", ctx->btype);
                goto err;
            }
        }

        if (ctx->bfinal && ctx->state == AV_INFLATE_BSTART) {
            if (ctx->container != CONTAINER_RAW) {
                align_get_bits(&ctx->gb);
                check_bits(FOOTER, &ctx->gb, 32 * ctx->container);
            }

            ctx->state = AV_INFLATE_END;

            if (ctx->container == CONTAINER_GZIP) {
                csum = get_bits_long(&ctx->gb, 32);
                gzisize = get_bits_long(&ctx->gb, 32);
            } else if (ctx->container == CONTAINER_ZLIB) {
                csum = get_bits_long(&ctx->gb, 32);
                csum = av_bswap32(csum);
            }
        }
    }

outbits:
    bitpos = get_bits_count(&ctx->gb);
    ctx->skip = bitpos & 7;
    bytepos = bitpos / 8;

    if (needbits || ctx->skip) {
        unsigned isize;
        const uint8_t *in;

        if (ctx->tailbits) {
            unsigned tb = ctx->tailsize * 8;
            isize = (tb - bitpos + 7) / 8;
            memmove(ctx->tailbuf, ctx->tailbuf + bytepos, isize);
            ctx->tailsize = isize;
            in = inbuf;
        } else {
            ctx->tailsize = 0;
            in = inbuf + bytepos;
            insize -= bytepos;
        }

        isize = FFMIN(sizeof(ctx->tailbuf)/2 - ctx->tailsize, insize);
        memcpy(ctx->tailbuf + ctx->tailsize, in, isize);
        ctx->tailsize += isize;
        bytepos = in + isize - inbuf;

        dprintf(ctx, "state=%d bitpos=%d tailbits=%d tailsize=%d "
                "skip=%d need=%d\n", ctx->state, bitpos, ctx->tailbits,
                ctx->tailsize, ctx->skip, needbits);
    } else {
        ctx->tailsize = 0;
    }

out:
    *outsize = out - outbuf;

    if (ctx->container == CONTAINER_ZLIB) {
        ctx->csum = av_adler32_update(ctx->csum, outbuf, *outsize);
    } else if (ctx->container == CONTAINER_GZIP) {
        ctx->csum = av_crc(ctx->crc, ctx->csum, outbuf, *outsize);
        ctx->osize += *outsize;
    }

    if (ctx->state == AV_INFLATE_END) {
        if (ctx->container == CONTAINER_ZLIB) {
            if (ctx->csum != csum)
                av_log(ctx, AV_LOG_ERROR, "adler32 mismatch %08x != %08x\n",
                       ctx->csum, csum);
        } else if(ctx->container == CONTAINER_GZIP) {
            ctx->csum ^= 0xffffffff;
            if (ctx->csum != csum)
                av_log(ctx, AV_LOG_ERROR, "gzip crc mismatch %08x != %08x\n",
                       ctx->csum, csum);
            if (ctx->osize != gzisize)
                av_log(ctx, AV_LOG_ERROR, "gzip isize mismatch %08x != %08x\n",
                       ctx->osize, gzisize);
        }

        ctx->state = AV_INFLATE_EOF;
    }

    if (*outsize > 0 && ctx->state < AV_INFLATE_END) {
        if (!ctx->buf) {
            ctx->buf = av_malloc(ZLIB_BUFSIZE);
            if (!ctx->buf)
                goto err;
        }

        if (*outsize >= ZLIB_BUFSIZE) {
            ctx->bufsize = ZLIB_BUFSIZE;
            memcpy(ctx->buf, out - ctx->bufsize, ctx->bufsize);
        } else if (ctx->bufsize + *outsize > ZLIB_BUFSIZE) {
            unsigned drop = ctx->bufsize + *outsize - ZLIB_BUFSIZE;
            unsigned bsize = ctx->bufsize - drop;
            memmove(ctx->buf, ctx->buf + drop, bsize);
            memcpy(ctx->buf + bsize, outbuf, *outsize);
            ctx->bufsize = ZLIB_BUFSIZE;
        } else {
            memcpy(ctx->buf + ctx->bufsize, outbuf, *outsize);
            ctx->bufsize += *outsize;
        }
    }

    return bytepos - ctx->tailbits / 8;

err:
    *outsize = 0;
    return -1;
}

static const char *
inflate_name(void *p)
{
    return "inflate";
}

static const AVClass inflate_avclass = {
    "AVInflateContext",
    inflate_name,
};

static void
inflate_init(AVInflateContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->av_class = &inflate_avclass;
}

extern AVInflateContext *
av_inflate_open(void)
{
    AVInflateContext *ctx = av_malloc(sizeof(*ctx));
    if (ctx)
        inflate_init(ctx);
    return ctx;
}

static void
free_tables(AVInflateContext *ctx)
{
    free_vlc(&ctx->cl_vlc);
    free_vlc(&ctx->ll_vlc);
    free_vlc(&ctx->dist_vlc);
    av_freep(&ctx->buf);
}

extern void
av_inflate_reset(AVInflateContext *ctx)
{
    free_tables(ctx);
    inflate_init(ctx);
}

extern void
av_inflate_close(AVInflateContext *ctx)
{
    free_tables(ctx);
    av_free(ctx);
}

extern int
av_inflate_single(uint8_t *outbuf, unsigned int *outsize,
                  const uint8_t *inbuf, unsigned int insize)
{
    AVInflateContext ctx;
    int ret;

    inflate_init(&ctx);

    ret = av_inflate(&ctx, outbuf, outsize, inbuf, insize);
    free_tables(&ctx);

    return ret;
}

#ifdef TEST
extern int
main(int argc, char **argv)
{
    static uint8_t in_buf[16384];
    static uint8_t out_buf[65536];
    AVInflateContext *ctx = av_inflate_open();
    unsigned int outsize;
    unsigned int n;
    int s;

#ifdef DEBUG
    av_log_level = AV_LOG_DEBUG;
#endif

    while ((n = fread(in_buf, 1, sizeof(in_buf), stdin)) > 0) {
        uint8_t *in = in_buf;

        do {
            outsize = sizeof(out_buf);
            s = av_inflate(ctx, out_buf, &outsize, in, n);
            dprintf(ctx, "n=%d s=%d o=%d\n", n, s, outsize);

            if (s < 0)
                goto err;

            in += s;
            n -= s;

            fwrite(out_buf, 1, outsize, stdout);
        } while(n);
    }

    do {
        av_inflate(ctx, out_buf, &outsize, NULL, 0);
        fwrite(out_buf, 1, outsize, stdout);
    } while(outsize);

err:
    av_inflate_close(ctx);

    return 0;
}
#endif
