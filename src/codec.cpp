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
  if (!subname || subname[0] == '0') {
    std::cerr << "No filename specified. Exiting.\n";
    return nullptr;
  }

  AVFormatContext* fmt_ctx = avformat_alloc_context();
  if (!fmt_ctx) {
    return nullptr;
  }

  // Open the subtitle file directly
  int ret = avformat_open_input(&fmt_ctx, subname, nullptr, nullptr);
  if (ret < 0) {
    std::cerr << "Error opening subtitle file: " << subname << ". File not found.\n";
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

// Get subtitle ID/language for a given index.
// Returns empty string if not available.
std::string get_id(AVFormatContext* fmt_ctx, unsigned int index) {
  if (!fmt_ctx || index >= fmt_ctx->nb_streams)
    return "Invalid subtitle index.";

  AVStream* stream = fmt_ctx->streams[index];
  if (stream->metadata && av_dict_get(stream->metadata, "language", nullptr, 0)) {
    return av_dict_get(stream->metadata, "language", nullptr, 0)->value;
  }

  return "No language identified. May cause unexpected behavior. Try setting --tesseract-lang";
}

// Set subtitle stream from language code.
// Returns the stream index on success, -1 if not found.
int set_from_lang(AVFormatContext* fmt_ctx, const char* lang) {
  if (!fmt_ctx || !lang) {
    return -1;
  }

  for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && fmt_ctx->streams[i]->codecpar->codec_id == 94208) {
      AVDictionaryEntry* entry = av_dict_get(fmt_ctx->streams[i]->metadata, "language", nullptr, 0);
      if (entry && strcmp(entry->value, lang) == 0) {
        return static_cast<int>(i);
      }
    }
  }
  return -1;
}
