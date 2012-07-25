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
    AVCodecContext *avctx;
    AVFrame         pic;
    GetBitContext   gb;
    uint32_t        queue;
    int             index;

    uint8_t         *frame;

    uint8_t         *src, *src_end;

    uint32_t pal[256];

} HNM4VideoContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    HNM4VideoContext * const c = avctx->priv_data;

    avctx->pix_fmt = PIX_FMT_PAL8;
    c->avctx = avctx;

    avcodec_get_frame_defaults(&c->pic);
    c->frame = av_mallocz(avctx->height * avctx->width);
    if (!c->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int get_bit(HNM4VideoContext *c)
{
    int result;

    if (!c->index && c->src_end - c->src >= 4) {
        c->queue = *(uint32_t*)c->src;
        c->src += 4;
        c->index = 32;
    }
    result = !!(c->queue & (1LL<<(c->index-1)));
    c->index--;
    return result;
}

static int decode_intra(HNM4VideoContext *c)
{
    AVCodecContext *avctx = c->avctx;
    uint8_t *dst, *dst_end, *dst_start;

    dst_start = dst = c->frame;
    dst_end = dst + avctx->height * avctx->width;

    av_log(0,0, "HEAD %X\n", *(uint32_t*)c->src);

    c->src+=4;

    while (c->src_end > c->src) {
        if (get_bit(c)) {
            *dst++ = *c->src++;
        } else {
            int count, offset;

            if (get_bit(c)) {
                count  = *c->src & 7;
                offset = ((*(uint16_t*)c->src)>>3) - 8192; c->src+=2;

                if (!count)
                    count = *c->src++;
                if (!count) {
                    break;
                }
            } else {
                count  = get_bit(c) * 2;
                count += get_bit(c);
                offset = (*c->src++) - 256;
            }
            count += 2;

            if(dst_end - dst < count){
                av_log(0,0, "Overwrite %d\n", count);
                return 0;
            }
            if(dst - dst_start < -offset){
                av_log(0,0, "Overreference %Ld\n", dst - dst_start + offset);
                return 0;
            }
            while(count--){
                *dst = dst[offset];
                dst++;
            }
        }
    }
    av_log(0,0, "Remaining %Ld %Ld\n", c->src_end - c->src, dst_end - dst);

    return 0;
}

static int decode_inter(HNM4VideoContext *c)
{
    return 0;
}

static int set_palette(HNM4VideoContext *c, GetByteContext *gb)
{
    uint32_t *palette = c->pal;
    int start, count, i;

    while (bytestream2_get_bytes_left(gb) >= 2) {
        start = bytestream2_get_byte(gb);
        count = bytestream2_get_byte(gb);

        if (start == 0xFF && count == 0xFF){
            return 0;
        }

        if (count == 0)
            count = 256;

        if (start + count > AVPALETTE_COUNT)
            return AVERROR_INVALIDDATA;

        for (i = start; i < start + count; i++) {
            unsigned r, g, b;

            r = bytestream2_get_byte(gb)*255/63;
            g = bytestream2_get_byte(gb)*255/63;
            b = bytestream2_get_byte(gb)*255/63;

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

    c->pic.reference = 3;
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
    memcpy(c->pic.data[1], c->pal, 256*4);

//     init_get_bits(&c->gb, pkt->data, pkt->size * 8);
    c->src = pkt->data;
    c->src_end = pkt->data + pkt->size;
    if (pkt->flags & AV_PKT_FLAG_KEY)
        ret = decode_intra(c);
    else
        ret = decode_inter(c);

    if (ret)
        return AVERROR_INVALIDDATA;

    dst = c->pic.data[0];
    src = c->frame;
#if 0
    for (i = 0; i < avctx->height; i++) {
        memcpy(dst, src, avctx->width);
        dst += c->pic.linesize[0];
        src += avctx->width;
    }
#else
    for (i = 0; i < avctx->height; i+=2) {
        int j;
        for(j=0; j<avctx->width; j+=2){
            dst[j] = src[2*j];
            dst[j+c->pic.linesize[0]] = src[2*j+1];
            dst[j+1] = src[2*j+2];
            dst[j+c->pic.linesize[0]+1] = src[2*j+3];
        }
        dst += 2*c->pic.linesize[0];
        src += 2*avctx->width;
    }
#endif

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
