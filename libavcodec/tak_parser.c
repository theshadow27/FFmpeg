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
    int index;
} TAKParseContext;

static av_cold int tak_init(AVCodecParserContext *s)
{
    ff_tak_init_crc();
    return 0;
}

#define MAX_HEADER_SIZE 33
static int tak_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    TAKParseContext *t = s->priv_data;
    ParseContext *pc = &t->pc;
    int next = END_NOT_FOUND, i = 0;
    GetBitContext gb;
    int consumed = 0;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        *poutbuf      = buf;
        *poutbuf_size = buf_size;
        return buf_size;
    }

    while(buf_size) {
        if(t->index + MAX_HEADER_SIZE > pc->index){
            int tmp_buf_size = FFMIN(2*MAX_HEADER_SIZE, buf_size);
            const uint8_t *tmp_buf = buf;
            ff_combine_frame(pc, END_NOT_FOUND, &tmp_buf, &tmp_buf_size);
            consumed += tmp_buf_size;
            buf      += tmp_buf_size;
            buf_size -= tmp_buf_size;
        }
        for(; t->index + MAX_HEADER_SIZE <= pc->index; t->index++){
            if (pc->buffer[ t->index ] == 0xFF && pc->buffer[ t->index+1 ] == 0xA0) {
                TAKStreamInfo ti;
                init_get_bits(&gb, pc->buffer + t->index, 8*MAX_HEADER_SIZE);
                if (!ff_tak_decode_frame_header(avctx, &gb, t->pc.frame_start_found ? &ti :&t->ti, 127) &&
                    !ff_tak_check_crc(pc->buffer + t->index, get_bits_count(&gb) / 8)) {
                    if (!t->pc.frame_start_found) {
                        t->pc.frame_start_found = 1;
                        s->duration = t->ti.last_frame_samples ?
                                      t->ti.last_frame_samples :
                                      t->ti.frame_samples;
                    } else {
                        t->pc.frame_start_found = 0;
                        next = t->index - pc->index;
                        t->index = 0;
                        goto found;
                    }
                }
            }
        }
    }
found:
    if (consumed && !buf_size && next == END_NOT_FOUND || ff_combine_frame(&t->pc, next, &buf, &buf_size) < 0) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size + consumed;
    }
    if(next != END_NOT_FOUND) {
        next += consumed;
        pc->overread = FFMAX(0, -next);
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
