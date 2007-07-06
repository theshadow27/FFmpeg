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

#ifndef AVCODEC_INFLATE_H
#define AVCODEC_INFLATE_H

#include <stdint.h>

typedef struct AVInflateContext AVInflateContext;

/**
 * Allocate a new inflate context.
 *
 * @return Pointer to new context, NULL on failure
 */
extern AVInflateContext *av_inflate_open(void);

/**
 * Decompress (part of) a compressed bitstream.
 *
 * @param ctx     Context returned by av_inflate_open()
 * @param outbuf  Pointer to output buffer
 * @param outsize Pointer to size of output buffer, overwritten with number
 *                of decompressed bytes written to buffer
 * @param inbuf   Pointer to compressed input data
 * @param insize  Number of bytes compressed input
 * @return Number of input bytes used, or negative on error
 */
extern int av_inflate(AVInflateContext *ctx,
                      uint8_t *outbuf, unsigned int *outsize,
                      const uint8_t *inbuf, unsigned int insize);

/**
 * Reset context to initial state.
 *
 * @param ctx Context to reset
 */
extern void av_inflate_reset(AVInflateContext *ctx);

/**
 * Destroy a context freeing all used memory.
 *
 * @param ctx Context to destroy
 */
extern void av_inflate_close(AVInflateContext *ctx);

/**
 * Decompress a single block of compressed data.
 *
 * This function is useful when the entire compressed stream is
 * available in a single buffer.
 *
 * @param outbuf  Pointer to output buffer
 * @param outsize Pointer to size of output buffer, overwritten with number
 *                of decompressed bytes written to buffer
 * @param inbuf   Pointer to compressed input data
 * @param insize  Number of bytes compressed input
 * @return Number of input bytes used, or negative on error
 */
extern int av_inflate_single(uint8_t *outbuf, unsigned int *outsize,
                             const uint8_t *inbuf, unsigned int insize);

#endif /* AVCODEC_INFLATE_H */
