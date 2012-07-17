/*
 * Cryo HNM4 video decoder
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

#define BITSTREAM_READER_LE

#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"

typedef struct {
    AVFrame         pic;
    GetBitContext   gb;
    uint32_t        queue;
    int             index;

    uint8_t         *frame;
} HNM4VideoContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    HNM4VideoContext * const c = avctx->priv_data;

    avctx->pix_fmt = PIX_FMT_PAL8;

    avcodec_get_frame_defaults(&c->pic);
    c->frame = av_mallocz(avctx->height * avctx->width);
    if (!c->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int get_bit(HNM4VideoContext *c)
{
    int result;

    if (!c->index && get_bits_left(&c->gb) >= 32) {
        c->queue = get_bits_long(&c->gb, 32);
        c->index = 32;
    }
    result = c->queue & c->index;
    c->index--;
    return result;
}

static int decode_intra(HNM4VideoContext *c)
{
    uint8_t *dst;

    dst = c->frame;

    skip_bits_long(&c->gb, 32);

    while (get_bits_left(&c->gb) > 0) {
        if (get_bit(c)) {
            *dst++ = get_bits(&c->gb, 8);
        } else {
            int count, offset;

            if (get_bit(c)) {
                count  = get_bits(&c->gb, 3);
                offset = get_bits(&c->gb, 13) - 8192;

                if (!count)
                    count = get_bits(&c->gb, 8);
                if (!count)
                    break;
            } else {
                count  = get_bit(c) * 2 + get_bit(c);
                offset = get_bits(&c->gb, 8) - 256;
            }

            memcpy(dst, dst + offset, count);
        }
    }

    return 0;
}

static int decode_inter(HNM4VideoContext *c)
{
    return 0;
}

static int set_palette(HNM4VideoContext *c, GetByteContext *gb)
{
    uint32_t *palette = (uint32_t *)c->pic.data[1];
    int start, count, i;

    while (bytestream2_get_bytes_left(gb) >= 2) {
        start = bytestream2_get_byte(gb);
        count = bytestream2_get_byte(gb);

        if (start == 0xFF)
            return 0;

        if (count == 0)
            count = 256;

        if (start + count > AVPALETTE_COUNT)
            return AVERROR_INVALIDDATA;

        for (i = start; i < count; i++) {
            unsigned r, g, b;

            r = bytestream2_get_byte(gb);
            g = bytestream2_get_byte(gb);
            b = bytestream2_get_byte(gb);

            palette[i] = 0xFF << 24 | r << 16 | g << 8 | b;
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *pkt)
{
    HNM4VideoContext * const c = avctx->priv_data;
    uint8_t *src, *dst;
    int ret, i;

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    c->pic.reference = 0;
    if ((ret = avctx->get_buffer(avctx, &c->pic)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    if (pkt->side_data_elems > 0 && pkt->side_data[0].type == AV_PKT_DATA_PALETTE) {
        GetByteContext g;

        bytestream2_init(&g, pkt->side_data[0].data, pkt->side_data[0].size);
        if ((ret = set_palette(c, &g)) < 0)
            return ret;
    }

    init_get_bits(&c->gb, pkt->data, pkt->size * 8);

    if (pkt->flags & AV_PKT_FLAG_KEY)
        ret = decode_intra(c);
    else
        ret = decode_inter(c);

    if (ret)
        return AVERROR_INVALIDDATA;

    dst = c->pic.data[0];
    src = c->frame;
    for (i = 0; i < avctx->height; i++) {
        memcpy(dst, src, avctx->width);
        dst += c->pic.linesize[0];
        src += avctx->width;
    }

    *data_size      = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    return pkt->size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    HNM4VideoContext * const c = avctx->priv_data;

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    av_freep(&c->frame);
    return 0;
}

AVCodec ff_hnm4_decoder = {
    .name           = "hnm4",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_HNM4,
    .priv_data_size = sizeof(HNM4VideoContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Cryo HNM4 video"),
};
