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

/**
 * Open a media file and return the format context.
 * Also finds the first subtitle stream index.
 */

AVFormatContext* dec_open(const char* subname, const char*,
				int& subtitle_stream_index) {
  std::string sub_path = subname;
  // Append ".idx" extension only if the filename has no extension TODO: check for other media extensions
  size_t dot_pos = sub_path.find_last_of('.');
  if (dot_pos == std::string::npos) { /* if .idx exists, use it. better to use std::filesystem */
    sub_path += ".idx";
  } else if (sub_path.empty()) {
    std::cerr << "No filename specified. Exiting.\n";
    return nullptr;
  }

  AVFormatContext* fmt_ctx = avformat_alloc_context();
  if (!fmt_ctx) {
    return nullptr;
  }

  // Try to open the subtitles directly
  int ret = avformat_open_input(&fmt_ctx, sub_path.c_str(), nullptr, nullptr);
  if (ret < 0) {
    std::cerr << "Error opening subtitle file: " << sub_path << "\n"
	      << "Container or .idx file not found. Please specify an absolute path, e.g. /home/user/file.vob.\n";
    avformat_free_context(fmt_ctx);
    return nullptr;
  }

  // Find subtitle stream
  for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && fmt_ctx->streams[i]->codecpar->codec_id == 94208) {
      subtitle_stream_index = static_cast<int>(i);
      break;
    }
  }

  // Probe stream info to get codec details
avformat_find_stream_info(fmt_ctx, nullptr);

if (subtitle_stream_index < 0 || fmt_ctx->streams[subtitle_stream_index]->codecpar->codec_type !=
    AVMEDIA_TYPE_SUBTITLE) {
  std::cerr << "No subtitle stream found in file\n";
  avformat_close_input(&fmt_ctx);
  return nullptr;
 }

return fmt_ctx;
}

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
