/*
 *  Filter/image-preprocessing + OCR step
 *
 *  This file is part of VobSub2SRT.
 *
 *  Copyright (C) 2010-2016 Rüdiger Sonderfeld <ruediger@c-plusplus.de>
 *  Copyright (C) 2026 Christopher Ogloff <chris.ogloff@gmail.com> (libav implementation)
 *
 *  Some info about DVD subpictures gathered from http://mpucoder.com/DVD/index.html
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

#include "filter_ocr.hpp"
#include "time.hpp" // pts2srt + get_sub_timing (used by get_sub_text)

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/imgutils.h>
}

// libpng (include path supplied by PkgConfig::PNG)
#include <png.h>

// standard libs
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

/** Subpicture size thresholds (set via --min-width / --min-height) */
int min_width = 8;
int min_height = 8;

/** Converts image data to Netpbm PGM format */
static void dump_pgm(const std::string &filename, unsigned counter, const char* file_lang,
	      unsigned width, unsigned height, unsigned stride, const unsigned char *image) {

  char buf[500];
  if (file_lang && strlen(file_lang) > 0) {
    std::snprintf(buf, sizeof(buf), "%s-%04u.%s.pgm", filename.c_str(), counter, file_lang);
  } else {
    std::snprintf(buf, sizeof(buf), "%s-%04u.pgm", filename.c_str(), counter);
  }
  std::ofstream pgm(buf, std::ios::binary);
  if(pgm) {
    pgm << "P5\n" << width << " " << height << " 255\n";
    for(size_t i = 0; i < height; ++i) {
      pgm.write(reinterpret_cast<const char*>(image + i * stride), width);
    }
  }
}

/** Or, to PNG */
static void dump_png(const std::string &filename, unsigned counter, const char* file_lang,
	      unsigned width, unsigned height, unsigned stride, const unsigned char* image) {

  char buf[500];
  if (file_lang && strlen(file_lang) > 0) {
    std::snprintf(buf, sizeof(buf), "%s-%04u.%s.png", filename.c_str(), counter, file_lang);
  } else {
    std::snprintf(buf, sizeof(buf), "%s-%04u.png", filename.c_str(), counter);
  }
  FILE* pngfile = std::fopen(buf, "wb");
  if (!pngfile) {
    return;
  }
  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) {
    std::cerr << "Unable to allocate memory for PNG file structure!\n";
    return;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_write_struct(&png_ptr, nullptr);
    return;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return;
  }
  png_init_io(png_ptr, pngfile);
  png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png_ptr, info_ptr);

  for (size_t row = 0; row < height; ++row) {
    png_write_row(png_ptr, const_cast<png_bytep>(image + row * stride));
  }

  png_write_end(png_ptr, nullptr);

  png_destroy_write_struct(&png_ptr, &info_ptr);
  std::fclose(pngfile);
}

/** Startup check that all required libavfilter filters exist. */
int check_required_filters() {
  static const char* filter_list[4] = {"buffer", "buffersink", "pad", "super2xsai"};
  int missing = 0;
  for (const char* name : filter_list) {
    if (!avfilter_get_by_name(name)) {
      std::cerr << "libavfilter: required filter '" << name << "' not found.\n";
      missing++;
    }
  }
  if (missing) {
    std::cerr << "Your libavfilter build is broken or mismatched with your headers.\n";
    return -1;
  }
  return 0;
}

std::string get_sub_text(AVSubtitle* subtitle, tesseract::TessBaseAPI& tess, int y_threshold,
			 DumpFormat dump_format, const std::string* filename_base, unsigned* counter_ptr,
			 const char* file_lang, bool verbose) {
  std::string result;

  /* Required filters are validated once at startup by check_required_filters()
     (called from main). These lookups therefore succeed here; the guard below
     is only a defensive safety net against a NULL deref. */
  const AVFilter* buffer     = avfilter_get_by_name("buffer");
  const AVFilter* bufsink    = avfilter_get_by_name("buffersink");
  const AVFilter* pad_flt    = avfilter_get_by_name("pad");
  const AVFilter* super2xsai = avfilter_get_by_name("super2xsai");
  if (!buffer || !bufsink || !pad_flt || !super2xsai)
    return result; // defensive: already validated at startup, should not happen

  for (unsigned i = 0; i < subtitle->num_rects; ++i) {
    AVSubtitleRect* rect = subtitle->rects[i];

    // Skip if too small
    if (rect->w < min_width || rect->h < min_height) continue;

    /**
     * Convert to grayscale image for OCR
     */
    auto image = std::make_unique<unsigned char[]>(rect->w * rect->h);
    bool has_palette = (rect->data[1] != nullptr);

    // Pre-compute luminance for all palette entries
    std::array<int, 256> palette_luma;
    const unsigned char* palette = has_palette ? rect->data[1] : nullptr;
    if (has_palette) {
      for (int i = 0; i < 256; ++i) {
        int r = palette[i * 4];     // R
        int g = palette[i * 4 + 1]; // G
	int b = palette[i * 4 + 2]; // B
        // Compute luminance using BT.601 coefficients
        palette_luma[i] = (r * 299 + g * 587 + b * 114) / 1000;
      }
      if (verbose) {
        /* DVD subpicture palettes define four entries (per SET_COLOR / spec):
           0 = background, 1 = pattern (foreground), 2 = emphasis 1, 3 = emphasis 2
	   The A byte is the contrast value (0x0 = transparent, 0xF = opaque). */
        static const char* names[4] = {"background", "pattern", "emphasis 1", "emphasis 2"};
        for (int i = 0; i < 4; ++i) {
          std::cerr << "[DEBUG] palette[" << i << "] (" << names[i] << ") rgb=("
                    << static_cast<int>(palette[i * 4]) << ","
                    << static_cast<int>(palette[i * 4 + 1]) << ","
                    << static_cast<int>(palette[i * 4 + 2]) << ") alpha="
                    << static_cast<int>(palette[i * 4 + 3]) << " luma="
                    << palette_luma[i] << '\n';
        }
      }
    } else {
      // Fallback: all zeros (black)
      palette_luma.fill(0);
    }

    if (verbose) {
      /* Geometry diagnostic: distinguishes glyph missing from the image
         (decode/bounding-box clip) from OCR dropping the first char at the margin. */
      std::cerr << "[DEBUG] rect x=" << rect->x << " y=" << rect->y
                << " w=" << rect->w << " h=" << rect->h
                << " linesize[0]=" << rect->linesize[0] << '\n';

      // Histogram of the four palette indices actually present in the bitmap.
      unsigned long hist[4] = {0, 0, 0, 0};
      for (int y = 0; y < rect->h; ++y) {
        const unsigned char* row = rect->data[0] + y * rect->linesize[0];
        for (int x = 0; x < rect->w; ++x) {
          unsigned char idx = row[x];
          if (idx < 4) hist[idx]++;
        }
      }
      std::cerr << "[DEBUG] index histogram: bg=" << hist[0]
                << " pattern=" << hist[1]
                << " emph1=" << hist[2]
                << " emph2=" << hist[3] << '\n';

      /* Leftmost column (from the left edge) that contains an opaque pixel.
         0 = content touches the left edge (e.g. not enough margin for Tesseract to reliably see the first letter).
         large = a gap exists (geometry/decode suspect). */
      int leftmost_opaque_col = -1;
      if (has_palette) {
        for (int x = 0; x < rect->w; ++x) {
          bool col_opaque = false;
          for (int y = 0; y < rect->h && !col_opaque; ++y) {
            unsigned char idx = rect->data[0][y * rect->linesize[0] + x];
            if (palette[idx * 4 + 3] > 0) col_opaque = true;
          }
          if (col_opaque) { leftmost_opaque_col = x; break; }
        }
      }
      std::cerr << "[DEBUG] leftmost opaque column = " << leftmost_opaque_col << '\n';
    }

    /* Binarize by palette structure rather than a global absolute Y cut.
       DVD subpicture palettes have four entries: 0=background, 1=pattern
       (main text), 2/3=emphasis. Instead of guessing a per-title luma
       threshold, classify each palette entry as text or background:
         - transparent entries (alpha == 0) are always background;
         - index 0 is the background by DVD convention (unless another
           opaque index is overwhelmingly more frequent, >5x);
         - the remaining opaque entries are text/emphasis.
       This inverts both light-text-on-dark and dark-text-on-light
       subtitles to black-text-on-white automatically. */

    /* A user-supplied --y-threshold (anything other than the default 16)
       is an explicit override: it forces the legacy absolute-luma cut for
       palette-coded subtitles as well, for titles where the automatic
       classification still misbehaves. */

    const bool y_override = (y_threshold != 16);
    bool idx_is_text[4] = {false, false, false, false};
    if (has_palette && !y_override) {
      unsigned long count[4] = {0, 0, 0, 0};
      for (int y = 0; y < rect->h; ++y) {
        const unsigned char* row = rect->data[0] + y * rect->linesize[0];
        for (int x = 0; x < rect->w; ++x) {
          unsigned char idx = row[x];
          if (idx < 4 && palette[idx * 4 + 3] > 0) count[idx]++;
        }
      }
      /* Prefer index 0 as background (standard DVD subpicture convention).
         Only override if another opaque index is overwhelmingly more
         frequent (>5x), which guards against dense text dominating a small
         background box and flipping the polarity. */

      int bg_idx = 0;
      for (int i = 1; i < 4; ++i) {
        if (count[i] > 5 * count[0]) { bg_idx = i; break; }
      }
      for (int i = 0; i < 4; ++i) {
        if (i != bg_idx && palette[i * 4 + 3] > 0 && count[i] > 0)
          idx_is_text[i] = true;
      }
      /* Safety net: if nothing was classified as text, fall back to the
         standard convention (index 0 background, indices 1..3 text). */
      if (!idx_is_text[1] && !idx_is_text[2] && !idx_is_text[3]) {
        for (int i = 1; i < 4; ++i)
          if (palette[i * 4 + 3] > 0) idx_is_text[i] = true;
      }
    }

    for (int y = 0; y < rect->h; ++y) {
      const unsigned char* row = rect->data[0] + y * rect->linesize[0];
      for (int x = 0; x < rect->w; ++x) {
        unsigned char idx = row[x];
        bool is_bg;
        if (!has_palette || (y_override && idx < 4)) {
          /* No palette data, or an explicit --y-threshold override:
             legacy absolute-luma threshold. Transparent entries have no
             real color, so they always map to background regardless of
             their stored RGB values. */
          if (has_palette && idx < 4 && palette[idx * 4 + 3] == 0) {
            is_bg = true;
          } else {
            is_bg = (palette_luma[idx] < y_threshold);
          }
        } else if (idx >= 4) {
          is_bg = true;                    // out-of-range index -> background
        } else if (palette[idx * 4 + 3] == 0) {
          is_bg = true;                    // transparent -> background
        } else {
          is_bg = !idx_is_text[idx];       // text/emphasis -> black
        }
        image[y * rect->w + x] = is_bg ? 255 : 0;
      }
    }

    /* Pad subtitles with 8px each side, filter image with
       super2xsai before OCR to clarify text */

    if (verbose) {
      std::cerr << "[DEBUG] input linesize: " << rect->linesize[0] << '\n';
    }


    AVFilterGraph* flt_graph = avfilter_graph_alloc();
    if (!flt_graph) {
      std::cerr << "Failed to allocate filter graph.\n";
      continue;
    }

    char srcargs[128];
    snprintf(srcargs, sizeof(srcargs), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d",
	     rect->w, rect->h, AV_PIX_FMT_GRAY8, 1, 90000);

    AVFilterContext* bufsrc_ctx = nullptr;
    if (avfilter_graph_create_filter(&bufsrc_ctx, buffer, "in", srcargs, nullptr, flt_graph) < 0) {
      std::cerr << "Failed to init source buffer.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }

    AVFilterContext* s2xs_ctx = nullptr;
    if (avfilter_graph_create_filter(&s2xs_ctx, super2xsai, "super2xsai", nullptr, nullptr, flt_graph) < 0) {
      std::cerr << "Failed to init super2xsai filter.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }

    /* Pad 8px each side to help Tesseract recognition */

    char pad_args[64];
    snprintf(pad_args, sizeof(pad_args), "iw+16:ih+16:8:8:color=white");

    AVFilterContext* pad_ctx = nullptr;
    if (avfilter_graph_create_filter(&pad_ctx, pad_flt, "pad", pad_args, nullptr, flt_graph) < 0) {
      std::cerr << "Failed to init pad filter.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }

    /* Send filtered image to output context */

    AVFilterContext* bufsnk_ctx = nullptr;
    if (avfilter_graph_create_filter(&bufsnk_ctx, bufsink, "out", nullptr, nullptr, flt_graph) < 0) {
      std::cerr << "Failed to create buffersink filter context.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }

    if ((avfilter_link(bufsrc_ctx, 0, pad_ctx, 0) < 0 || avfilter_link(pad_ctx, 0, s2xs_ctx, 0) < 0 ||
	 avfilter_link(s2xs_ctx, 0, bufsnk_ctx, 0) < 0)) {
      std::cerr << "Failed to link filter graph.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }

    if (avfilter_graph_config(flt_graph, nullptr) < 0) {
      std::cerr << "Failed to configure filters.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }


    AVFrame* in_frame = av_frame_alloc();
    if (!in_frame) {
      std::cerr << "Failed to allocate input frame.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }

    in_frame->width = rect->w;
    in_frame->height = rect->h;
    in_frame->linesize[0] = rect->w;
    in_frame->format = AV_PIX_FMT_GRAY8;

    if (av_frame_get_buffer(in_frame, 0) < 0) {
      std::cerr << "Failed to allocate frame buffer.\n";
      av_frame_free(&in_frame);
      avfilter_graph_free(&flt_graph);
      continue;
    }

    av_image_copy_plane(in_frame->data[0], in_frame->linesize[0], image.get(), rect->w, rect->w, rect->h);

    if (av_buffersrc_add_frame_flags(bufsrc_ctx, in_frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
      std::cerr << "Failed to add frame to filter.\n";
      av_frame_free(&in_frame);
      avfilter_graph_free(&flt_graph);
      continue;
    }

    AVFrame* out_image = av_frame_alloc();
    if (!out_image) {
      std::cerr << "Failed to allocate output frame.\n";
      av_frame_free(&in_frame);
      avfilter_graph_free(&flt_graph);
      continue;
    }

    int ret = av_buffersink_get_frame(bufsnk_ctx, out_image);

    if (ret >= 0) {
      // Use the upscaled image for OCR and check if we have valid data
      if (out_image->data[0] == nullptr) {
        std::cerr << "[ERROR] Output frame has no data pointer!\n";
        av_frame_free(&out_image);
        av_frame_free(&in_frame);
        avfilter_graph_free(&flt_graph);
        continue;
      }

      /* Copy filtered image to a contiguous buffer for Tesseract (remove any row padding)
         Convert from RGB24 to grayscale since Tesseract only accepts 8-bit grayscale */
      std::vector<unsigned char> img_buffer(out_image->width * out_image->height);

      int src_linesize = out_image->linesize[0];
      int bpp = (out_image->format == AV_PIX_FMT_RGB24) ? 3 : 1;
      
      for (int y = 0; y < out_image->height; ++y) {
        const unsigned char* src_row = out_image->data[0] + y * src_linesize;
        unsigned char* dst_row = img_buffer.data() + y * out_image->width;

	if (bpp == 3) {
          // Convert RGB to grayscale using BT.601 luminance formula
          for (int x = 0; x < out_image->width; ++x) {
            const unsigned char* pixel = src_row + x * 3;
            dst_row[x] = (pixel[0] * 76 + pixel[1] * 150 + pixel[2] * 29) >> 8; // BT.601 luma
          }
    	} else {
          // Already grayscale
	  memcpy(dst_row, src_row, out_image->width);
	}
      }

    // Dump image if requested (PNG or PGM format)
      if (dump_format != DumpFormat::NONE && filename_base && counter_ptr && file_lang) {
        unsigned width = out_image->width;
        unsigned height = out_image->height;

        // Dump the image buffer to sequential files
        switch (dump_format) {
        case DumpFormat::PNG:
          dump_png(*filename_base, *counter_ptr, file_lang, width, height, width, img_buffer.data());
          break;
        case DumpFormat::PGM:
          dump_pgm(*filename_base, *counter_ptr, file_lang, width, height, width, img_buffer.data());
          break;
        default:
          break;
        }
        (*counter_ptr)++;
      }

      if (verbose) {
      std::cerr << "[DEBUG] Filter output: " << out_image->width
		<< "x" << out_image->height << " format=" << out_image->format
                << " linesize[0]=" << out_image->linesize[0] << "\n";
      }

      tess.SetImage(img_buffer.data(), out_image->width, out_image->height, 1, out_image->width);
      char* ocr_result = tess.GetUTF8Text();
      if (ocr_result && strlen(ocr_result) > 0) {
        result += ocr_result;
      }
      delete[] ocr_result;
    } else {
      std::cerr << "Failed to get filtered frame from output buffer: " << ret << "\n";
      // Fallback to unfiltered image (this code path needs further testing.)
      tess.SetImage(image.get(), rect->w, rect->h, 1, rect->w);
      char* ocr_result = tess.GetUTF8Text();
      if (ocr_result && strlen(ocr_result) > 0) {
        result += ocr_result;
      }
      delete[] ocr_result;
    }

    av_frame_free(&out_image);
    av_frame_free(&in_frame);
    avfilter_graph_free(&flt_graph);
  }

  tess.Clear();
  return result;
}

std::vector<sub_text_t> process_subtitles(AVFormatContext* avfctx, int subtitle_stream_index,
					  AVCodecContext* spu, const AVStream* stream,
					  tesseract::TessBaseAPI& tess_api, int y_threshold,
					  DumpFormat dump_format, const std::string& subname,
					  const char* tess_lang, bool show, bool verbose) {


  // Check for valid packet to read subtitles
  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    std::cerr << "Failed to allocate packet\n";
    return {};
  }

  int sub_counter = 1;
  unsigned img_counter = 1;
  unsigned last_start_pts = 0;
  std::vector<sub_text_t> conv_subs;
  conv_subs.reserve(255);

  while (av_read_frame(avfctx, packet) >= 0) {

    // Check if this is a subtitle packet
    if (packet->stream_index != subtitle_stream_index) {
      av_packet_unref(packet);
      continue;
    }

    // Decode the subtitle packet
    int got_sub = 0;
    AVSubtitle subtitle;
    memset(&subtitle, 0, sizeof(subtitle));
    int ret = avcodec_decode_subtitle2(spu, &subtitle, &got_sub, packet);

    if (ret < 0) {
      std::cerr << "Decode error: " << ret << "\n";
      av_packet_unref(packet);
      continue;
    }

    if (!got_sub) {
      if (verbose) {
	std::cerr << "No subtitle. Skipping.\n";
      }
      av_packet_unref(packet);
      continue;
    }

    /* Derive start/end timing (usually 90kHz) from the packet PTS + decoder end time.
       get_sub_timing() also performs duplicate-PTS detection and tracks
       last_start_pts internally. it reports a skip when this is a duplicate. */
    sub_timing_t sub_timing = get_sub_timing(packet, stream, subtitle, last_start_pts, sub_counter, verbose);

    if (sub_timing.skip) {
      std::cerr << "WARNING: Duplicate subtitle/invalid PTS. Skipping.";
      av_packet_unref(packet);
      continue;
    }

    // Process the decoded subtitle with OCR and optionally dump images
    std::string text = get_sub_text(&subtitle, tess_api, y_threshold, dump_format, &subname, &img_counter, tess_lang, verbose);

    if (!text.empty()) {
      // Convert string to unique_ptr<char[]>
      size_t text_len = text.length();
      std::unique_ptr<char[]> text_ptr(new char[text_len + 1]);
      memcpy(text_ptr.get(), text.c_str(), text_len + 1);
      conv_subs.emplace_back(sub_text_t(sub_timing.start_pts_90k, sub_timing.end_pts_90k, std::move(text_ptr)));

      if (show) {
        std::cout << "Subtitle " << sub_counter << ": " << text;
	if (verbose) {
	  /* If end_pts is UINT64_MAX sentinel here (no explicit end time)
	     it gets derived from the next subtitle's start in the write-out pass below. */
	  const std::string timing = pts2srt(sub_timing.start_pts_90k) + " --> " +
	    (sub_timing.end_pts_90k == UINT64_MAX ? "(unset, will use next start)" : pts2srt(sub_timing.end_pts_90k));
	  std::cerr << '\n' << "timing= " << timing << '\n';
	}
      }
      if (verbose) {
	std::cerr << "\nduration=" << packet->duration << " position= " << packet->pos << " start_display_time= "
		  << subtitle.start_display_time << " end_display_time= " << subtitle.end_display_time << " pts= "
		  << packet->pts << '\n';
      }

      // Increment amount of decoded subtitles
      sub_counter++;
    }

    // Clean up for next iteration
    avsubtitle_free(&subtitle);
    av_packet_unref(packet);
  }

  // Free the packet
  av_packet_free(&packet);

  return conv_subs;
}
