/*
 * CDXL video decoder
 * Copyright (c) 2011 Paul B Mahol, onemda@gmail.com
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"

typedef struct {
    AVFrame frame;
} CDXLContext;

static av_cold int cdxl_decode_init(AVCodecContext *avctx)
{
    CDXLContext * const c = avctx->priv_data;

    avcodec_get_frame_defaults(&c->frame);
    avctx->pix_fmt = PIX_FMT_PAL8;

    return 0;
}

static av_cold int cdxl_decode_end(AVCodecContext *avctx)
{
    CDXLContext *c = avctx->priv_data;
    if (c->frame.data[0])
        avctx->release_buffer(avctx, &c->frame);
    return 0;
}

static int cdxl_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                             AVPacket *pkt)
{
    const uint8_t *buf = pkt->data;
    int i, x, plane, buf_size = pkt->size;
    int encoding, video_size, palette_size;
    const uint8_t *palette, *video;
    CDXLContext *c = avctx->priv_data;
    AVFrame * const p = &c->frame;
    GetBitContext gb;

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference = 0;
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    palette_size = AV_RB16(&buf[20]);
    video_size = buf_size - palette_size - 32;
    palette = buf + 32;
    video = palette + palette_size;
    encoding = buf[1] & 3;
    av_log(NULL, AV_LOG_ERROR, "p:%d v:%d en:%d\n", palette_size, video_size, encoding);
    if (encoding == 0) {
        uint32_t *new_palette = (uint32_t *) c->frame.data[1];
        for (i = 0; i < 256; i++) {
            unsigned xxx= bytestream_get_be16(&palette);
            unsigned r= (xxx&0xF)*17;
            unsigned g= (xxx&0xF0)*17>>4;
            unsigned b= (xxx&0xF00)*17>>8;
            new_palette[i] = (0xFF << 24) | (r+g*256+b*256*256);
        }
        memset(c->frame.data[0], 0, c->frame.linesize[0] * avctx->height);
        init_get_bits(&gb, video, video_size*8);
        for(plane=0; plane<8; plane++){
            for(i=0; i<68; i++){
                for(x=0; x<80; x++){
                    c->frame.data[0][c->frame.linesize[0]*i + x] |= get_bits1(&gb)<<(plane);
                }
            }
        }
            memcpy(c->frame.data[0]+ c->frame.linesize[0]*i , video + 80*i, 68);

//         memcpy(c->frame.data[0], video, 4624);
    }
    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->frame;
    return buf_size;
}

AVCodec ff_cdxl_decoder = {
    .name           = "cdxl",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_CDXL,
    .priv_data_size = sizeof(CDXLContext),
    .init           = cdxl_decode_init,
    .close          = cdxl_decode_end,
    .decode         = cdxl_decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("CDXL video"),
};
