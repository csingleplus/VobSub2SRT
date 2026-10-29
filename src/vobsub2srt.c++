/*
 *  VobSub2SRT is a simple command line program to convert .idx/.sub subtitles
 *  into .srt text subtitles by using OCR (tesseract). See README.
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

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

// Tesseract OCR
#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

// libpng
/** some systems do not use a subdir for png. */
#if __has_include(<png.h>)
#include <png.h>
#elif __has_include(<libpng/png.h>)
#include <libpng/png.h>
#endif

// standard libs
#include <memory>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <chrono>
#include <array>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdio>
#include <vector>

// Language and option handling
#include "langcodes.h++"
#include "cmd_options.h++"
#include "version.h++"


// Version string for display
const char* version = _VOBSUB2SRT_VERSION_;
const char* codename = _VOBSUB2SRT_CODENAME_;

// helper struct for caching and fixing end_pts in some cases
struct sub_text_t {
  uint64_t start_pts;
  uint64_t end_pts;
  std::unique_ptr<char[]> text;

  sub_text_t(uint64_t start_pts, uint64_t end_pts, std::unique_ptr<char[]> text);
};

sub_text_t::sub_text_t(uint64_t start_pts, uint64_t end_pts, std::unique_ptr<char[]> text)
  : start_pts(start_pts), end_pts(end_pts), text(std::move(text)) {}


/** Converts time stamp in pts format to a string containing the time stamp for the srt format
 *
 * pts (presentation time stamp) is given with a 90kHz resolution (1/90 ms).
 * srt expects a time stamp as  HH:MM:SS,MS.
 */
std::string pts2srt(uint64_t pts) {
  uint64_t ms = pts/90l;
  uint32_t const h = ms / (3600u * 1000u);
  ms -= h * 3600u * 1000u;
  uint32_t const m = ms / (60u * 1000u);
  ms -= m * 60u * 1000u;
  uint32_t const s = ms / 1000u;
  ms %= 1000u;

  std::array<char, 32> buf{};
  std::snprintf(buf.data(), buf.size(), "%02d:%02d:%02d,%03ld", h, m, s, ms);
  return buf.data();
}


/** Converts image data to Netpbm PGM format */
void dump_pgm(const std::string &filename, unsigned counter, const char* file_lang, unsigned width, unsigned height,
              unsigned stride, const unsigned char *image) {

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
void dump_png(const std::string &filename, unsigned counter, const char* file_lang, unsigned width, unsigned height, unsigned stride, const unsigned char* image) {

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

/**
 * Open a media file and return the format context.
 * Also finds the first subtitle stream index.
 */

AVFormatContext* media_open(const char* subname, const char*,
				int& subtitle_stream_index) {
  std::string sub_path = subname;
  if (sub_path.empty()) {
    std::cerr << "No filename specified. Exiting.\n";
    return nullptr;
  }
  // Append ".idx" extension only if the filename has no extension TODO: check for other media extensions
  size_t dot_pos = sub_path.find_last_of('.');
  if (dot_pos == std::string::npos) { /* if .idx exists, use it. better to use std::filesystem */
    sub_path += ".idx";
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

/**
 * Close the media file.
 */
void media_close(AVFormatContext*& fmt_ctx) {
  if (fmt_ctx) {
    avformat_close_input(&fmt_ctx);
  }
}

/**
 * Get subtitle stream count
 */
unsigned int sub_get_indexes_count(AVFormatContext* fmt_ctx) {
  if (!fmt_ctx) return 0;
  // Count subtitle streams
  unsigned count = 0;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && fmt_ctx->streams[i]->codecpar->codec_id == 94208) {
      ++count;
    }
  }
  return count;
}

/**
 * Get subtitle ID/language for a given index.
 * Returns empty string if not available.
 */
std::string sub_get_id(AVFormatContext* fmt_ctx, unsigned int index) {
  if (!fmt_ctx || index >= fmt_ctx->nb_streams)
    return "Invalid subtitle index.";

  AVStream* stream = fmt_ctx->streams[index];
  if (stream->metadata && av_dict_get(stream->metadata, "language", nullptr, 0)) {
    return av_dict_get(stream->metadata, "language", nullptr, 0)->value;
  }

  return "No language identified. May cause unexpected behavior. Try setting --tesseract-lang";
}

/**
 * Set subtitle stream from language code.
 * Returns 0 on success, -1 if not found.
 */
int sub_set_from_lang(AVFormatContext* fmt_ctx, const char* lang) {
  if (!fmt_ctx || !lang) {
    if (fmt_ctx) media_close(fmt_ctx);
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

/**
 * Extract subtitle image data from AVSubtitle, optionally dump images, then run OCR.
 *
 * Convert to grayscale and invert so text is dark on light background
 * (required for Tesseract OCR accuracy).
 */
enum class DumpFormat { NONE, PNG, PGM };
int min_width = 8;
int min_height = 8;

std::string extract_subtitle_text(AVSubtitle* subtitle, tesseract::TessBaseAPI& tess,
                                  int y_threshold, DumpFormat dump_format = DumpFormat::NONE,
                                  const std::string* filename_base = nullptr, unsigned* counter_ptr = nullptr,
				  const char* file_lang = nullptr, bool verbose = false) {
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

        if (has_palette) {
          // 8-bit indexed color: each byte is a palette index
          unsigned char idx = rect->data[0][y * rect->w + x];
          // Look up luminance from pre-computed palette
          pixvalue = palette_luma[idx];
        } else {
          // Fallback: treat as direct grayscale
          pixvalue = rect->data[0][y * rect->w + x];
        }

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

    // Filter image with super2xsai before OCR to smooth out text

    const AVFilter* buffer = avfilter_get_by_name("buffer");
    const AVFilter* bufsink = avfilter_get_by_name("buffersink");
    const AVFilter* super2xsai = avfilter_get_by_name("super2xsai");

    if (!buffer || !bufsink || !super2xsai) {
      std::cerr << "Failed to find required filters (buffer/buffersink/super2xsai).\n";
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

    /* Add unsharp filter for edge clarity */

    char us_args[32];
    snprintf(us_args, sizeof(us_args), "3:3:3:3");

    AVFilterContext* unsharp_ctx = nullptr;
    const AVFilter* unsharp = avfilter_get_by_name("unsharp");
    if (!unsharp) {
      std::cerr << "Failed to init unsharp filter.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }

    if (avfilter_graph_create_filter(&unsharp_ctx, unsharp, "unsharp", us_args, nullptr, flt_graph) < 0) {
      std::cerr << "Failed to create unsharp filter context.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }

    AVFilterContext* bufsnk_ctx = nullptr;
    if (avfilter_graph_create_filter(&bufsnk_ctx, bufsink, "out", nullptr, nullptr, flt_graph) < 0) {
      std::cerr << "Failed to create buffersink filter context.\n";
      avfilter_graph_free(&flt_graph);
      continue;
    }

    if ((avfilter_link(bufsrc_ctx, 0, s2xs_ctx, 0) < 0 || avfilter_link(s2xs_ctx, 0, unsharp_ctx, 0) < 0 ||
	 avfilter_link(unsharp_ctx, 0, bufsnk_ctx, 0) < 0)) {
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
    in_frame->linesize[0] = rect->linesize[0];
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
      std::vector<unsigned char> tess_buffer(out_image->width * out_image->height);

      int src_linesize = out_image->linesize[0];
      int bpp = (out_image->format == AV_PIX_FMT_RGB24) ? 3 : 1;

      for (int y = 0; y < out_image->height; ++y) {
        const unsigned char* src_row = out_image->data[0] + y * src_linesize;
        unsigned char* dst_row = tess_buffer.data() + y * out_image->width;

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

        // Convert RGB to grayscale
        std::vector<unsigned char> imgbuf(out_image->width * out_image->height);
        int src_linesize = out_image->linesize[0];
        int bpp = (out_image->format == AV_PIX_FMT_RGB24) ? 3 : 1;

        for (int y = 0; y < out_image->height; ++y) {
          const unsigned char* src_row = out_image->data[0] + y * src_linesize;
          unsigned char* dst_row = imgbuf.data() + y * out_image->width;

          if (bpp == 3) {
            for (int x = 0; x < out_image->width; ++x) {
              const unsigned char* pixel = src_row + x * 3;
              dst_row[x] = (pixel[0] * 76 + pixel[1] * 150 + pixel[2] * 29) >> 8;
            }
          } else {
            memcpy(dst_row, src_row, out_image->width);
          }
        }

        // Dump the grayscale buffer
        switch (dump_format) {
        case DumpFormat::PNG:
          dump_png(*filename_base, *counter_ptr, file_lang, width, height, width, imgbuf.data());
          break;
        case DumpFormat::PGM:
          dump_pgm(*filename_base, *counter_ptr, file_lang, width, height, width, imgbuf.data());
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

      tess.SetImage(tess_buffer.data(), out_image->width, out_image->height, 1, out_image->width);
      char* ocr_result = tess.GetUTF8Text();
      if (ocr_result && strlen(ocr_result) > 0) {
        result += ocr_result;
      }
      delete[] ocr_result;
    } else {
      std::cerr << "Failed to get filtered frame from buffersink: " << ret << "\n";
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

int main(int argc, char **argv) {
  bool show = false;
  bool dump_pgmfiles = false;
  bool dump_pngfiles = false;
  bool verbose = false;
  bool env = false;
  bool list_languages = false;
  bool nofilter = false;
  std::string ifo_file;
  std::string subname;
  std::string lang;
  std::string outfile;
  std::string tess_lang_user;
  std::string blacklist;
  std::string tess_user_dir;
  int tess_user_dpi = 72;
  int index = -1;
  int y_threshold = 16;

  auto time_s = std::chrono::steady_clock::now(); // Start runtime counter
  av_log_set_level(AV_LOG_WARNING); // Set libav warning level.

  {
    cmd_options opts;
    opts.
      add_option("output", outfile, "Set output filename to use, otherwise set to <subname>.<lang>.srt", 'o').
      add_option("env", env, "Show environment variables used and exit.", 'e').
      add_option("nofilter", nofilter, "Disable default Tesseract character blacklist (\"|\\/\")").
      add_option("show", show, "Show subtitles being written.").
      add_option("dump-pgm", dump_pgmfiles, "Save subtitles as sequential NetPBM image files (<subname>-0001.pgm etc.).").
      add_option("dump-png", dump_pngfiles, "Above, but in PNG format.").
      add_option("verbose", verbose, "Debug prints to stderr.").
      add_option("ifo", ifo_file, "IFO file for DVD subtitle palette extraction (e.g., VIDEO_TS.IFO/VTS_01_0.IFO)").
      add_option("lang", lang, "Language to select", 'l').
      add_option("langlist", list_languages, "List languages and exit").
      add_option("index", index, "Subtitle index", 'i').
      add_option("tesseract-lang", tess_lang_user, "Desired Tesseract language (e.g. eng, deu, fra, etc.)").
      add_option("tesseract-data", tess_user_dir, "Path to Tesseract data (e.g. you have tessdata_best traineddata files in a custom directory).").
      add_option("dpi", tess_user_dpi, "Set DPI for Tesseract OCR. Default: 72").
      add_option("blacklist", blacklist, "Character blacklist to improve the OCR (e.g. \"|\\/`_~<>\" to avoid common misreads).").
      add_option("y-threshold", y_threshold, "Y (luminance) threshold below which colors treated as black (0-255). Default: 16").
      add_option("min-width", min_width, "Minimum width in pixels to consider a subpicture for OCR. Default: 8").
      add_option("min-height", min_height, "Minimum height in pixels to consider a subpicture for OCR. Default: 8");

    opts.add_unnamed(subname, "subname", nullptr);
    std::cout << "VobSub2SRT version " << version << " " << codename << '\n';
    if(!opts.parse_cmd(argc, argv)) {
      // Check opts.error; --help sets exit but not error
      if (opts.error) {
	// Actual parsing error occurred
        return 1;
      } else {
	// Exit cleanly after showing help
	return 0;
      }
    }
  }

  // 

  // Display relevant environment variable status
  if (env) {
    const char* tesspfx = std::getenv("TESSDATA_PREFIX");
    bool tessdata_env = (tesspfx != nullptr);

    if (!tess_user_dir.empty()) {
      std::cout << "--tesseract-data set to: " << tess_user_dir << ", overriding $TESSDATA_PREFIX if set.\n";
    } else if (tessdata_env) {
      std::cout << "$TESSDATA_PREFIX: " << tesspfx << '\n';
    } else {
      std::cout << "--tesseract-data or TESSDATA_PREFIX unset. Using default Tesseract data directory.\n";
    }

    return 0;
  }

  // Set Y threshold
  if (y_threshold != 16) {
    std::cout << "Using Y palette threshold: " << y_threshold << "\n";
  }

  // Open the subtitles using FFmpeg. This assumes DVD subtitles (dvd_subtitle codec)
  int subtitle_stream_index = -1;
  AVFormatContext* avfctx = media_open(subname.c_str(), ifo_file.empty() ? nullptr : ifo_file.c_str(),
                                      subtitle_stream_index);
  if (!avfctx || subtitle_stream_index < 0) {
    //    std::cerr << "Couldn't open container or sub/idx VobSub files for '" << subname << "'\n";
    return 1;
  }

  // List languages and exit
  if(list_languages) {
    std::cout << "Languages:\n";
    for(size_t i = 0; i < avfctx->nb_streams; ++i) {
      if (avfctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && avfctx->streams[i]->codecpar->codec_id == 94208) {
        std::string id = sub_get_id(avfctx, static_cast<unsigned>(i));
        std::cout << i << ": " << (id.empty() ? "(no id. that's odd. may cause problems, please report if you see this)\n" : id.c_str()) << '\n';
      }
    }
    media_close(avfctx);
    return 0;
  }

  // Handle stream IDs and language
  if(!lang.empty() && index >= 0) {
    std::cerr << "Setting both lang and index not supported.\n";
    media_close(avfctx);
    return 1;
  }

  // Default OCR language to eng unless overridden by user or inferred from selected subtitle track.
  std::string tess_lang_str = tess_lang_user.empty() ? "eng" : tess_lang_user;

  // Helper to check if a language code should skip conversion (placeholder/special codes)
  auto is_placeholder_lang = [](const std::string& lang) -> bool {
    // Skip placeholder and special ISO-639-3 codes that have ambiguous/misc languages
    return lang == "und" || lang == "unk" || lang == "mul" || lang == "mis" || lang == "zxx" ||
           (lang.length() == 3 && lang[0] == 'q' && lang[1] >= 'a' && lang[1] <= 't'); // qaa-qtz
  };

  // Track if encountering a placeholder language code
  bool placeholder_lang_used = false;

  if(!lang.empty()) {
    int stream_idx = sub_set_from_lang(avfctx, lang.c_str());
    if (stream_idx < 0) {
      std::cerr << "No matching language for '" << lang << "' found! (Trying to use default)\n";
    } else {
      subtitle_stream_index = stream_idx;
      if(tess_lang_user.empty()) {
        // Same as above.
        if(lang.length() == 3 && !is_placeholder_lang(lang)) {
          tess_lang_str = lang;
        } else {
          char const *const lang3 = iso639_1_to_639_3(lang.c_str());
          if(lang3) {
            tess_lang_str = lang3;
          } else {
            if(is_placeholder_lang(lang)) {
              placeholder_lang_used = true;
            }
          }
        }
      }
    }
  } else {
    if(index >= 0) {
      if(static_cast<unsigned>(index) >= avfctx->nb_streams) {
        std::cerr << "Index argument out of range: " << index << " ("
                  << avfctx->nb_streams << ")\n";
	media_close(avfctx);
        return 1;
      }
      subtitle_stream_index = index;
    }

    if(subtitle_stream_index >= 0) {
      std::string lang1 = sub_get_id(avfctx, static_cast<unsigned>(subtitle_stream_index));
      if(!lang1.empty() && tess_lang_user.empty()) {
        // If the language code is already 3 characters and not a placeholder, use it directly.
        if(lang1.length() == 3 && !is_placeholder_lang(lang1)) {
          tess_lang_str = lang1;
        } else {
          char const *const lang3 = iso639_1_to_639_3(lang1.c_str());
          if(lang3) {
            tess_lang_str = lang3;
          } else {
            // No valid conversion found, mark as placeholder
            if(is_placeholder_lang(lang1)) {
              placeholder_lang_used = true;
            }
          }
        }
      }
    }
    
  }
  char const *tess_lang = tess_lang_str.c_str();

  // Get codec context for the subtitle stream
  AVStream* stream = avfctx->streams[subtitle_stream_index];
  if (verbose) {
  std::cerr << "Subtitle stream codec_id=" << stream->codecpar->codec_id 
            << " (" << avcodec_get_name(stream->codecpar->codec_id) << ")\n";
  }

  if (stream->codecpar->codec_id != 94208) {
    media_close(avfctx);
    std::cerr << "Not a supported subtitle stream. Try using the --langlist option to find one.\n";
    return 1;
  }
  
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    std::cerr << "Failed to find decoder for codec ID: " << stream->codecpar->codec_id << "("
	      << avcodec_get_name(stream->codecpar->codec_id) << ")" << "\n";
    media_close(avfctx);
    return 1;
  }

  AVCodecContext* spu = avcodec_alloc_context3(codec);
  if (!spu) {
    std::cerr << "Failed to allocate codec context\n";
    media_close(avfctx);
    return 1;
  }

  // Set IFO palette option if provided
  if (!ifo_file.empty()) {
    std::cout << "Using IFO file for palette: " << ifo_file << "\n";
    av_opt_set(spu->priv_data, "ifo_palette", ifo_file.c_str(), 0);
  }

  // Copy codec parameters
  avcodec_parameters_to_context(spu, stream->codecpar);

  // Open the decoder
  if (avcodec_open2(spu, codec, nullptr) < 0) {
    std::cerr << "Failed to open subtitle decoder\n";
    avcodec_free_context(&spu);
    media_close(avfctx);
    return 1;
  }

  if (verbose) {
  int dbg_ifo = av_opt_set(spu, "ifo_palette", ifo_file.c_str(), AV_OPT_SEARCH_CHILDREN);
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(dbg_ifo, errbuf, sizeof errbuf);
  av_log(nullptr, AV_LOG_ERROR, "ifo_palette: %s\n", errbuf);
  }

  // Init Tesseract
  tesseract::TessBaseAPI tess_api;
  const char* tess_data_path = tess_user_dir.empty() ? nullptr : tess_user_dir.c_str();
  if(tess_api.Init(tess_data_path, tess_lang, tesseract::OEM_LSTM_ONLY) == -1) {
    std::cerr << "Failed to initialize Tesseract OCR.\n";
    avcodec_free_context(&spu);
    media_close(avfctx);
    return 1;
  }

  // Set blacklist if not empty, otherwise strip the most common OCR failures                                                                                  
  if(!blacklist.empty()) {
    tess_api.SetVariable("tessedit_char_blacklist", blacklist.c_str());
  } else if (!nofilter && blacklist.empty()) {
    tess_api.SetVariable("tessedit_char_blacklist", "\\/|");
  }

  // Set DPI to 72, or user-specified with --dpi
  if (tess_user_dpi > 0) {
    char buf[32];
      snprintf(buf, sizeof(buf), "%d", tess_user_dpi);
    const char* tess_dpi = buf;
    tess_api.SetVariable("user_defined_dpi", tess_dpi);
  }

  // Attempt to improve whitespace detection (old Tesseract vars, possibly not relevant anymore)
  std::string tess_rej_spaces = "0";
  std::string tess_improve_thresh = "1";
  tess_api.SetVariable("tessedit_use_reject_spaces", tess_rej_spaces.c_str());
  tess_api.SetVariable("tosp_improve_thresh", tess_improve_thresh.c_str());

  // Open srt output file - strip only the last extension (container format)
  std::string srt_filename;
  if(!outfile.empty()) {
    // Use user-provided output filename
    srt_filename = outfile;
  } else {
    // Default: <subname>.<lang>.srt
    std::string base_name = subname;
    size_t last_dot = base_name.find_last_of('.');
    if (last_dot != std::string::npos) {
      base_name = base_name.substr(0, last_dot);
    }
    srt_filename = base_name + "." + std::string(tess_lang) + ".srt";
  }
  FILE *srtout = std::fopen(srt_filename.c_str(), "w");
  if(!srtout) {
    std::cerr << "Could not open .srt file for writing.\n";
    return 1;
  }

  // Check for valid packet to read subtitles
  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    std::cerr << "Failed to allocate packet\n";
    return 1;
  }

  int sub_counter = 1;
  unsigned img_counter = 1;
  unsigned last_start_pts = 0;
  std::vector<sub_text_t> conv_subs;
  conv_subs.reserve(255);

  // Determine dump format based on user options
  DumpFormat dump_format = DumpFormat::NONE;
  if (dump_pngfiles) {
    dump_format = DumpFormat::PNG;
  } else if (dump_pgmfiles) {
    dump_format = DumpFormat::PGM;
  }

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

    // Extract timing information from packet PTS
    AVRational tb = stream->time_base;
    int64_t pts_val = 0;
    
    if (packet->pts != AV_NOPTS_VALUE) {
      // Copy packet->pts for our own usage
      pts_val = packet->pts;
    } else {
      // Last resort: sequential timing based on stream's actual timebase
      std::cerr << "Warning: PTS for subtitle #" << sub_counter 
                << " is " << packet->pts << ", using default 1 second interval with timebase " << tb.den << "/" << tb.num << "\n";
      // Add ~1 second worth of ticks in stream's timebase units (e.g., 90k for MPEG, 25 or 30 for others)
      pts_val = last_start_pts + (tb.den / tb.num);  
    }

    // Convert from stream timebase to standard SRT format (90kHz)
    uint64_t start_pts_90k = static_cast<uint64_t>(av_rescale_q(pts_val, tb, AVRational{1, 90000}));

    // DVD subtitles don't provide accurate duration per packet (packet->duration is always 0)
    // Use end_display_time from the dvdsub decoder.
    uint64_t end_pts_90k = start_pts_90k + (subtitle.end_display_time*90);

    // Track last PTS to detect duplicates (in stream timebase units)
    if (pts_val == last_start_pts && pts_val != 0) {
      if (verbose) {
	std::cerr << "WARNING: Invalid last start PTS " << pts_val << ". Skipping.";
	}
      av_packet_unref(packet);
      continue;
    }

    last_start_pts = static_cast<unsigned>(pts_val);

    // Process the decoded subtitle with OCR and optionally dump images
    std::string text = extract_subtitle_text(&subtitle, tess_api, y_threshold,
                                             dump_format, &subname, &img_counter, tess_lang);

    if (!text.empty()) {
      // Convert string to unique_ptr<char[]>
      size_t text_len = text.length();
      std::unique_ptr<char[]> text_ptr(new char[text_len + 1]);
      memcpy(text_ptr.get(), text.c_str(), text_len + 1);

      conv_subs.emplace_back(sub_text_t(start_pts_90k, end_pts_90k, std::move(text_ptr)));

      if (show) {
        std::cout << "Subtitle " << sub_counter << ": " << text;
	if (verbose) {
	  std::cerr << '\n' << "timing= " << pts2srt(start_pts_90k) << " --> " << pts2srt(end_pts_90k) << '\n';
	}
      }
      if (verbose) {
	std::cerr << "\nduration=" << packet->duration << " position= " << packet->pos << " start_display_time= "
		  << subtitle.start_display_time << " end_display_time= " << subtitle.end_display_time << " pts_ms= "
		  << pts_val << '\n';
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

  // Write the file, fixing end_pts when needed
  for(unsigned i = 0; i < conv_subs.size(); ++i) {
    if(conv_subs[i].end_pts == UINT64_MAX) {
      if(i+1 < conv_subs.size()) {
        // Use next subtitle's start time as this subtitle's end time
        conv_subs[i].end_pts = conv_subs[i+1].start_pts;
      } else {
        // Last subtitle: add a reasonable default duration (~2 seconds in 90kHz = 180000 ticks)
	// this may have been a kludgy workaround that should be fixed. needs testing with a bad pts subtitle.
        conv_subs[i].end_pts = conv_subs[i].start_pts + 180000;
      }
    }

    std::fprintf(srtout, "%u\n%s --> %s\n%s\n\n", i+1, pts2srt(conv_subs[i].start_pts).c_str(),
		 pts2srt(conv_subs[i].end_pts).c_str(), conv_subs[i].text.get());
  }

  // Close up shop
  tess_api.End();
  std::fclose(srtout);
  avcodec_free_context(&spu);
  media_close(avfctx);

  auto time_f = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = time_f - time_s;
  if (dump_pngfiles) {
    std::cout << "Wrote images to '" << subname << "-count." << tess_lang << ".png.\n";
  }
  if (dump_pgmfiles) {
    std::cout << "Wrote images to '" << subname << "-count." << tess_lang << ".pgm.\n";
  }
  std::cout << "Wrote subtitles to '" << srt_filename << "', " << elapsed.count() << " seconds elapsed.\n";
  
  // Warn if a placeholder language code was used, and instruct the user
  if(placeholder_lang_used) {
    std::cerr << "\nWARNING: The subtitle track had a placeholder/unknown language code.\n"
              << "If the OCR results look wrong, please specify the correct language with --tesseract-lang\n"
              << "See --help for more information.\n";
  }

  return 0;
}
