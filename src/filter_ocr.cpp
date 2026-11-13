/*
 *  Filter/image-preprocessing + OCR step
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

#include "filter_ocr.hpp"

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

std::string extract_subtitle_text(AVSubtitle* subtitle, tesseract::TessBaseAPI& tess,
                                  int y_threshold, DumpFormat dump_format,
                                  const std::string* filename_base, unsigned* counter_ptr,
                                  const char* file_lang, bool verbose) {
  std::string result;

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
    if (has_palette) {
      const unsigned char* palette = rect->data[1];
      for (int i = 0; i < 256; ++i) {
        int r = palette[i * 4];     // R
        int g = palette[i * 4 + 1]; // G
	int b = palette[i * 4 + 2]; // B
        // Compute luminance using BT.601 coefficients
        palette_luma[i] = (r * 299 + g * 587 + b * 114) / 1000;
      }
    } else {
      // Fallback: all zeros (black)
      palette_luma.fill(0);
    }

    for (int y = 0; y < rect->h; ++y) {
      for (int x = 0; x < rect->w; ++x) {
        unsigned char pixvalue = 0;
          // 8-bit indexed color: each byte is a palette index
          unsigned char idx = rect->data[0][y * rect->linesize[0] + x];
          // Look up luminance from pre-computed palette
          pixvalue = palette_luma[idx];

        // Apply Y threshold for better OCR:
        // We want dark text on light background for Tesseract.
        // DVD subtitles typically have light text on dark background,
        // so we invert: below threshold becomes light (background),
        // above threshold becomes dark (text).
        int threshold = y_threshold;
        if (pixvalue < threshold) {
          image[y * rect->w + x] = 255;
        } else {
          image[y * rect->w + x] = 0;
        }
      }
    }

    // Pad subtitles with 8px each side, filter image with
    // super2xsai and unsharp before OCR to clarify text

    const AVFilter* buffer = avfilter_get_by_name("buffer");
    const AVFilter* bufsink = avfilter_get_by_name("buffersink");
    const AVFilter* pad_flt = avfilter_get_by_name("pad");
    const AVFilter* super2xsai = avfilter_get_by_name("super2xsai");

    if (!buffer || !bufsink || !super2xsai || !pad_flt) { // this seems redundant and/or flawed
      std::cerr << "Failed to find required filters (buffer/buffersink/pad/super2xsai).\n";
      continue;
    }

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

      // Copy filtered image to a contiguous buffer for Tesseract (remove any row padding)
      // Convert from RGB24 to grayscale since Tesseract only accepts 8-bit grayscale
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
      std::cerr << "[DEBUG] Filter output: " << out_image->width << "x" << out_image->height << " format=" << out_image->format
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
      // Fallback to unfiltered image
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
