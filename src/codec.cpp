/*
 *  Codec/decoder functions
 *
 *  This file is part of VobSub2SRT.
 *
 *  Copyright (C) 2010-2016 Rüdiger Sonderfeld <ruediger@c-plusplus.de>
 *  Copyright (C) 2026 Christopher Ogloff <chris.ogloff@gmail.com> (libav implementation)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "codec.hpp"
#include <iostream>
#include <string>

AVCodecContext* dec_open(AVFormatContext* fmt_ctx, int stream_i, const std::string &ifo_file) {
  AVStream* stream = fmt_ctx->streams[stream_i];

  const AVCodec* d = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!d) {
    return nullptr;
  }

  AVCodecContext* spu = avcodec_alloc_context3(d);
  if (!spu) {
    return nullptr;
  }

  // Set IFO palette option if provided (dvdsub decoder option, lives in priv_data)
  if (!ifo_file.empty()) {
    av_opt_set(spu->priv_data, "ifo_palette", ifo_file.c_str(), 0);
  }

  // Copy codec parameters from the stream
  avcodec_parameters_to_context(spu, stream->codecpar);

  // Open the decoder
  if (avcodec_open2(spu, d, nullptr) < 0) {
    avcodec_free_context(&spu);
    return nullptr;
  }
  return spu;
}

// Close decoder context
void dec_close(AVFormatContext* &fmt_ctx) {
  if (fmt_ctx) {
    avformat_close_input(&fmt_ctx);
  }
}
