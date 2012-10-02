/*
 * Raw TAK demuxer
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

#include "libavcodec/tak.h"
#include "avformat.h"
#include "internal.h"
#include "rawdec.h"
#include "apetag.h"

static int tak_probe(AVProbeData *p)
{
    if (!memcmp(p->buf, "tBaK", 4))
        return AVPROBE_SCORE_MAX / 2;
    return 0;
}

static int tak_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    GetBitContext gb;
    AVStream *st;
    uint8_t *buffer = NULL;
    int ret;

    st = avformat_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id   = AV_CODEC_ID_TAK;
    st->need_parsing      = AVSTREAM_PARSE_FULL_RAW;

    if (avio_rl32(pb) != MKTAG('t', 'B', 'a', 'K')) {
        avio_seek(pb, -4, SEEK_CUR);
        return 0;
    }

    while (!url_feof(pb)) {
        enum TAKMetaDataType type;
        int size;

        type = avio_r8(pb) & 0x7f;
        size = avio_rl24(pb);

        switch (type) {
        case TAK_METADATA_STREAMINFO:
        case TAK_METADATA_ENCODER:
            buffer = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!buffer)
                return AVERROR(ENOMEM);

            if (avio_read(pb, buffer, size) != size) {
                av_freep(&buffer);
                return AVERROR(EIO);
            }

            init_get_bits(&gb, buffer, size * 8);
            break;
        case TAK_METADATA_MD5: {
            uint8_t md5[16];
            int i;

            if (size != 19)
                return AVERROR_INVALIDDATA;
            avio_read(pb, md5, 16);
            avio_skip(pb, 3);
            av_log(s, AV_LOG_VERBOSE, "MD5=");
            for (i = 0; i < 16; i++)
                av_log(s, AV_LOG_VERBOSE, "%02x", md5[i]);
            av_log(s, AV_LOG_VERBOSE, "\n");
            break;
            }
        case TAK_METADATA_END:
            if (pb->seekable) {
                int64_t curpos = avio_tell(pb);

                ff_ape_parse_tag(s);
                avio_seek(pb, curpos, SEEK_SET);
            }
            return 0;
            break;
        default:
            ret = avio_skip(s->pb, size);
            if (ret < 0)
                return ret;
        }

        if (type == TAK_METADATA_STREAMINFO) {
            TAKStreamInfo ti;

            avpriv_tak_parse_streaminfo(&gb, &ti);
            if (ti.samples > 0)
                st->duration = ti.samples;
            st->codec->extradata             = buffer;
            st->codec->extradata_size        = size;
            st->codec->bits_per_coded_sample = ti.bps;
            if (ti.ch_layout)
                st->codec->channel_layout = ti.ch_layout;
            st->codec->sample_rate = ti.sample_rate;
            st->codec->channels    = ti.channels;
            st->start_time = 0;
            buffer = NULL;
            avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
        } else if (type == TAK_METADATA_ENCODER) {
            av_log(s, AV_LOG_VERBOSE, "encoder version: %0X\n",
                   get_bits_long(&gb, TAK_ENCODER_VERSION_BITS));
            av_freep(&buffer);
        }
    }

    return AVERROR_EOF;
}

AVInputFormat ff_tak_demuxer = {
    .name           = "tak",
    .long_name      = NULL_IF_CONFIG_SMALL("raw TAK"),
    .read_probe     = tak_probe,
    .read_header    = tak_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "tak",
    .raw_codec_id   = AV_CODEC_ID_TAK,
};
