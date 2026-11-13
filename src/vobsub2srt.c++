/*
 *  VobSub2SRT is a simple command line program to convert DVD subtitles
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

// Tesseract API headers
#include <tesseract/baseapi.h>

// standard libs
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// VobSub2SRT internals
#include "langcodes.h++" // conversion of ISO639-1 to ISO639-3 language codes for Tesseract
#include "cmd_options.h++" // option parsing
#include "time.hpp" // conversion of PTS values to HH/MM/SS/MS
#include "version.h++" // self-explanatory, just a couple strings.
#include "codec.hpp" // subtitle decoder logic
#include "filter_ocr.hpp" // image filtering + OCR step


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

  // Open the subtitles using FFmpeg. This assumes DVD subtitles (dvdsub/dvd_subtitle codec)
  int subtitle_stream_index = -1;
  AVFormatContext* avfctx = dec_open(subname.c_str(), ifo_file.empty() ? nullptr : ifo_file.c_str(),
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
        std::string id = get_id(avfctx, static_cast<unsigned>(i));
        std::cout << i << ": " << (id.empty() ? "(no id. that's odd. may cause problems, please report if you see this)\n" : id.c_str()) << '\n';
      }
    }
    dec_close(avfctx);
    return 0;
  }

  // Handle stream IDs and language
  if(!lang.empty() && index >= 0) {
    std::cerr << "Setting both lang and index not supported.\n";
    dec_close(avfctx);
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
    int stream_idx = set_from_lang(avfctx, lang.c_str());
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
	dec_close(avfctx);
        return 1;
      }
      subtitle_stream_index = index;
    }

    if(subtitle_stream_index >= 0) {
      std::string lang1 = get_id(avfctx, static_cast<unsigned>(subtitle_stream_index));
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
    dec_close(avfctx);
    std::cerr << "Not a supported subtitle stream. Try using the --langlist option to find one.\n";
    return 1;
  }
  
  // Notify if IFO palette is used
  if (!ifo_file.empty()) {
    std::cout << "Using IFO file for palette: " << ifo_file << "\n";
  }

  // Open the subtitle decoder (implemented in codec.cpp)
  AVCodecContext* spu = dec_open(avfctx, subtitle_stream_index, ifo_file);
  if (!spu) {
    std::cerr << "Failed to open subtitle decoder\n";
    dec_close(avfctx);
    return 1;
  }

  // Init Tesseract
  tesseract::TessBaseAPI tess_api;
  const char* tess_data_path = tess_user_dir.empty() ? nullptr : tess_user_dir.c_str();
  if(tess_api.Init(tess_data_path, tess_lang, tesseract::OEM_LSTM_ONLY) == -1) {
    std::cerr << "Failed to initialize Tesseract OCR.\n";
    avcodec_free_context(&spu);
    dec_close(avfctx);
    return 1;
  }

  // Set page segmentation mode to PSM_SINGLE_BLOCK (6)
  tess_api.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);

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

    // DVD subtitles don't provide accurate duration per packet (packet->duration is always 0).
    // Use end_display_time from the dvdsub decoder; a value of 0 means "no explicit end time",
    // so mark it with UINT64_MAX and let the write-out pass derive one from the next subtitle.
    uint64_t end_pts_90k = UINT64_MAX;
    if (subtitle.end_display_time > 0 && subtitle.end_display_time < UINT32_MAX) {
      // sanity: reject durations that would overflow or produce garbage timing
      const int64_t max_end_ticks = static_cast<int64_t>(UINT32_MAX - start_pts_90k);
      if (static_cast<uint64_t>(subtitle.end_display_time * 90) <= static_cast<uint64_t>(max_end_ticks)) {
        end_pts_90k = start_pts_90k + subtitle.end_display_time * 90;
      } else if (verbose) {
        std::cerr << "WARNING: invalid end_display_time=" << subtitle.end_display_time
                  << " for subtitle #" << sub_counter << ", using next start time.\n";
      }
    }

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
                                             dump_format, &subname, &img_counter, tess_lang, verbose);

    if (!text.empty()) {
      // Convert string to unique_ptr<char[]>
      size_t text_len = text.length();
      std::unique_ptr<char[]> text_ptr(new char[text_len + 1]);
      memcpy(text_ptr.get(), text.c_str(), text_len + 1);

      conv_subs.emplace_back(sub_text_t(start_pts_90k, end_pts_90k, std::move(text_ptr)));

      if (show) {
        std::cout << "Subtitle " << sub_counter << ": " << text;
	if (verbose) {
	  // end_pts_90k may still be the UINT64_MAX sentinel here ("no explicit end time");
	  // it gets derived from the next subtitle's start in the write-out pass below.
	  const std::string timing = pts2srt(start_pts_90k) + " --> " +
	    (end_pts_90k == UINT64_MAX ? "(unset, will use next start)" : pts2srt(end_pts_90k));
	  std::cerr << '\n' << "timing= " << timing << '\n';
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
  dec_close(avfctx);

  auto time_f = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = time_f - time_s;
  if (dump_pngfiles) {
    std::cout << "Wrote images to '" << subname << "-####." << tess_lang << ".png.'\n";
  }
  if (dump_pgmfiles) {
    std::cout << "Wrote images to '" << subname << "-####." << tess_lang << ".pgm.'\n";
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
