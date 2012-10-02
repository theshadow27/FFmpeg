/*
 * TAK parser
 * Copyright (c) 2012 Paul B Mahol
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

/**
 * @file
 * TAK parser
 **/

#include "libavutil/bswap.h"
#include "parser.h"
#include "tak.h"

typedef struct TAKParseContext {
    ParseContext  pc;
    TAKStreamInfo ti;
} TAKParseContext;

static av_cold int tak_init(AVCodecParserContext *s)
{
    ff_tak_init_crc();
    return 0;
}

static int tak_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    TAKParseContext *t = s->priv_data;
    int next = END_NOT_FOUND, i = 0;
    GetBitContext gb;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        *poutbuf      = buf;
        *poutbuf_size = buf_size;
        return buf_size;
    }

    if (!t->pc.frame_start_found) {
        for (; i < buf_size; i++) {
            t->pc.state64 = (t->pc.state64 << 8) | buf[i];

            if ((t->pc.state64 >> 48) == 0xFFA0) {
                init_get_bits(&gb, buf + i - 7, (buf_size - i + 7) * 8);

                if (!ff_tak_decode_frame_header(avctx, &gb, &t->ti, 127) &&
                    !ff_tak_check_crc(buf + i - 7, get_bits_count(&gb) / 8)) {
                    t->pc.frame_start_found = 1;
                    s->duration = t->ti.last_frame_samples ?
                                  t->ti.last_frame_samples :
                                  t->ti.frame_samples;
                    break;
                }
            }
        }
    }

    if (t->pc.frame_start_found) {
        for (; i < buf_size; i++) {
            t->pc.state64 = (t->pc.state64 << 8) | buf[i];
            if ((t->pc.state64 >> 48) == 0xFFA0) {
                TAKStreamInfo ti;

                init_get_bits(&gb, buf + i - 7, (buf_size - i + 7) * 8);

                if (!ff_tak_decode_frame_header(avctx, &gb, &ti, 127) &&
                    !ff_tak_check_crc(buf + i - 7, get_bits_count(&gb) / 8)) {
                    next = i - 7;
                    t->pc.state64 = -1;
                    t->pc.frame_start_found = 0;
                    break;
                }
            }
        }
    }

    if (ff_combine_frame(&t->pc, next, &buf, &buf_size) < 0) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser ff_tak_parser = {
    .codec_ids      = { AV_CODEC_ID_TAK },
    .priv_data_size = sizeof(TAKParseContext),
    .parser_init    = tak_init,
    .parser_parse   = tak_parse,
    .parser_close   = ff_parse_close,
};
