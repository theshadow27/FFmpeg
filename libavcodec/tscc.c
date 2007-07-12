/*
 * TechSmith Camtasia decoder
 * Copyright (c) 2004 Konstantin Shishkov
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
 * TechSmith Camtasia decoder
 *
 * Fourcc: TSCC
 *
 * Codec is very simple:
 *  it codes picture (picture difference, really)
 *  with algorithm almost identical to Windows RLE8,
 *  only without padding and with greater pixel sizes,
 *  then this coded picture is packed with ZLib
 *
 * Supports: BGR8,BGR555,BGR24 - only BGR8 and BGR555 tested
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "avcodec.h"
#include "msrledec.h"
#include "inflate.h"


/*
 * Decoder context
 */
typedef struct TsccContext {

    AVCodecContext *avctx;
    AVFrame pic;

    // Bits per pixel
    int bpp;
    // Decompressed data size
    unsigned int decomp_size;
    // Decompression buffer
    unsigned char* decomp_buf;
    int height;

    uint32_t pal[256];
} CamtasiaContext;

/*
 *
 * Decode a frame
 *
 */
static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    CamtasiaContext * const c = avctx->priv_data;
    const unsigned char *encoded = buf;
    int len = buf_size;
    unsigned int infsize;

    if(c->pic.data[0])
            avctx->release_buffer(avctx, &c->pic);

    c->pic.reference = 1;
    c->pic.buffer_hints = FF_BUFFER_HINTS_VALID;
    if(avctx->get_buffer(avctx, &c->pic) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    infsize = c->decomp_size;
    if (av_inflate_single(c->decomp_buf, &infsize, encoded, len) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Inflate error\n");
        return -1;
    }

    if(infsize > 0)
        ff_msrle_decode(avctx, (AVPicture*)&c->pic, c->bpp, c->decomp_buf, infsize);

    /* make the palette available on the way out */
    if (c->avctx->pix_fmt == PIX_FMT_PAL8) {
        const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);

        if (pal) {
            c->pic.palette_has_changed = 1;
            memcpy(c->pal, pal, AVPALETTE_SIZE);
        }
        memcpy(c->pic.data[1], c->pal, AVPALETTE_SIZE);
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    /* always report that the buffer was completely consumed */
    return buf_size;
}



/*
 *
 * Init tscc decoder
 *
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
    CamtasiaContext * const c = avctx->priv_data;

    c->avctx = avctx;

    c->height = avctx->height;

    avcodec_get_frame_defaults(&c->pic);

    switch(avctx->bits_per_coded_sample){
    case  8: avctx->pix_fmt = PIX_FMT_PAL8; break;
    case 16: avctx->pix_fmt = PIX_FMT_RGB555; break;
    case 24:
             avctx->pix_fmt = PIX_FMT_BGR24;
             break;
    case 32: avctx->pix_fmt = PIX_FMT_RGB32; break;
    default: av_log(avctx, AV_LOG_ERROR, "Camtasia error: unknown depth %i bpp\n", avctx->bits_per_coded_sample);
             return -1;
    }
    c->bpp = avctx->bits_per_coded_sample;
    // buffer size for RLE 'best' case when 2-byte code preceeds each pixel and there may be padding after it too
    c->decomp_size = (((avctx->width * c->bpp + 7) >> 3) + 3 * avctx->width + 2) * avctx->height + 2;

    /* Allocate decompression buffer */
    if (c->decomp_size) {
        if ((c->decomp_buf = av_malloc(c->decomp_size)) == NULL) {
            av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
            return 1;
        }
    }

    return 0;
}



/*
 *
 * Uninit tscc decoder
 *
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    CamtasiaContext * const c = avctx->priv_data;

    av_freep(&c->decomp_buf);

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    return 0;
}

AVCodec ff_tscc_decoder = {
    .name           = "camtasia",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_TSCC,
    .priv_data_size = sizeof(CamtasiaContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("TechSmith Screen Capture Codec"),
};

