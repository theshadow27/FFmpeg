/*
 * CDXL demuxer
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
#include "avformat.h"
#include "internal.h"

#define CDXL_HEADER_SIZE 32
#define CDXL_SAMPLE_RATE 11025

typedef struct CDXLContext {
    int raw_sound_size;
} CDXLContext;

static int cdxl_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVIOContext *pb = s->pb;
    AVStream *video_stream, *audio_stream;
    uint8_t header[CDXL_HEADER_SIZE];

    if (avio_read(pb, header, CDXL_HEADER_SIZE) !=
        CDXL_HEADER_SIZE)
        return AVERROR(EIO);

    audio_stream = avformat_new_stream(s, NULL);
    if (!audio_stream)
        return AVERROR(ENOMEM);
    audio_stream->codec->bits_per_coded_sample = 8;
    audio_stream->codec->sample_rate = CDXL_SAMPLE_RATE;
    audio_stream->codec->sample_fmt  = AV_SAMPLE_FMT_U8;
    audio_stream->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    audio_stream->codec->codec_tag   = 0;
    audio_stream->codec->codec_id    = CODEC_ID_PCM_S8;
    audio_stream->codec->bit_rate    = CDXL_SAMPLE_RATE;
    audio_stream->codec->channels    = (AV_RB8(&header[1]) & 8) + 1;

    video_stream = avformat_new_stream(s, NULL);
    if (!video_stream)
        return AVERROR(ENOMEM);
    video_stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    video_stream->codec->codec_id   = CODEC_ID_CDXL;
    video_stream->codec->pix_fmt    = PIX_FMT_PAL8;
    video_stream->codec->width      = AV_RB16(&header[14]);
    video_stream->codec->height     = AV_RB16(&header[16]);
    avpriv_set_pts_info(video_stream, 16, 1, 12);
    avio_seek(pb, 0, SEEK_SET);
    return 0;
}

static int cdxl_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    CDXLContext *c = s->priv_data;
    int ret, current_size;
    uint8_t header[CDXL_HEADER_SIZE];

    if (c->raw_sound_size) {
        ret = av_get_packet(pb, pkt, c->raw_sound_size);
        if (ret < 0)
            return ret;
        pkt->stream_index = c->raw_sound_size = 0;
    } else {
        if (avio_read(pb, header, CDXL_HEADER_SIZE) !=
                CDXL_HEADER_SIZE)
            return AVERROR(EIO);
        avio_seek(pb, -CDXL_HEADER_SIZE, SEEK_CUR);

        current_size = AV_RB32(&header[2]);
        c->raw_sound_size = AV_RB16(&header[22]);
        ret = av_get_packet(pb, pkt, current_size - c->raw_sound_size);
        if (ret < 0)
            return ret;
        pkt->stream_index = 1;
    }

    return ret;
}

AVInputFormat ff_cdxl_demuxer = {
    .name           = "cdxl",
    .long_name      = NULL_IF_CONFIG_SMALL("CDXL"),
    .priv_data_size = sizeof(CDXLContext),
    .read_header    = cdxl_read_header,
    .read_packet    = cdxl_read_packet,
    .extensions     = "cdxl",
};
