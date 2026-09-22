/*
 *  VobSub2SRT is a simple command line program to convert .idx/.sub subtitles
 *  into .srt text subtitles by using OCR (tesseract). See README.
 *
 *  Copyright (C) 2010-2016 Rüdiger Sonderfeld <ruediger@c-plusplus.de>
 *  Copyright (C) 2026 Christopher Ogloff <chris.ogloff@gmail.com>
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

// Tesseract OCR
#include <tesseract/baseapi.h>

// Builtins/standard libs
#include <fstream>
#include <format>

// Language and option handling.
#include "langcodes.h++"
#include "cmd_options.h++"
#include "version.h++"

// Italics detection with help of opencv
#include "debug.h++"
#include "OCRSubtitles.h++"
#include "VobSub.h++"

#include <opencv2/imgcodecs.hpp>

int
main2(int argc, char **argv) {
  bool show = false;
  std::string dump_ext;
  int verb = -1;
  bool list_languages = false;
  std::string ifo_file;
  std::string subname;
  std::string lang;
  std::string tess_lang_user;
  std::string blacklist;
  std::string tesseract_user_dir;
  std::string tess_user_dpi = "72";
  int index = -1;
  int y_threshold = 16;
  std::size_t ocr_batch_size = 20;
  bool detect_italic = false;
  int base_duration = 0;
  int chars_per_sec = 19;
  
  {
    cmd_options opts;
    int debug_st_num = 0;
    int batch_size = 0;
    opts.
      add_option("show", show, "Show subtitles being written.").
      add_option("dump-images", dump_ext, "Save subtitle image files with specified extension (<subname>-<number>.ext) (pgm, png, ...).").
      add_option("debug", debug, "Write all kinds of debug info to standard error.").
      add_option("debug-number", debug_st_num, "Write all kinds of debug info to standard error, for a specific subtitle number.").
      add_option("debug-images", debug_ext, "Also write debug images for options --debug and --debug-number (pgm, png, ...).").
      add_option("verbose", verb, "Decoder verbosity, a value of 1 or 2.").
      add_option("ifo", ifo_file, "Name of the IFO file. Default: tries to open <subname>.ifo(case insensitive).\n\t\t\t\tIFO file is optional but may fix empty palette issues!").
      add_option("lang", lang, "Language to select", 'l').
      add_option("langlist", list_languages, "List languages and exit").
      add_option("index", index, "Subtitle index", 'i').
      add_option("tesseract-lang", tess_lang_user, "Desired Tesseract language (e.g. eng, deu, fra, esp, eng+fra)\n\t\t\t\t(Default: autodetect)").
      add_option("tesseract-data", tesseract_user_dir, "Path to Tesseract data (e.g. you have tessdata_best and wish to\n\t\t\t\tuse it. Default: autodetect)").
      add_option("dpi", tess_user_dpi, "Set DPI for Tesseract OCR. Default: 72.").
      add_option("blacklist", blacklist, "Character blacklist to improve the OCR (e.g. \"|\\/`_~<>\")").
      add_option("y-threshold", y_threshold, "Y (luminance) threshold below which colors treated as black (Default: 16)").
      add_option("ocr-batch-size", batch_size, "Perform OCR on combined images. Can fix empty or inaccurate OCR results. (Default: 20).").
      add_option("detect-italic", detect_italic, "Detect italic. Add <i> and </i> to the output where applicable.").
      add_option("base-duration", base_duration, "Max subtitle display duration (msec) = base_duration + 1000 * subtitle_length_in_chars / chars_per_sec (Default: 0 = disable, recommended: 1500)").
      add_option("chars-per-sec", chars_per_sec, "See --base-duration (Default: 19, recommended: 15..20).");

    opts.add_unnamed(subname, "subname", "name of the subtitle files without .idx/.sub ending.");
    std::cout << "VobSub2SRT version " << version << '\n';
    if(!opts.parse_cmd(argc, argv)) {
      return 0;
    }

    if (debug_st_num > 0) {
      debug_subtitle_number = static_cast<std::size_t>(debug_st_num);
    }
    if (batch_size > 0) {
      ocr_batch_size = static_cast<std::size_t>(batch_size);
    }

    if (base_duration < 0)
      base_duration = 0;
    if (chars_per_sec < 0)
      chars_per_sec = 1;
  }

  if (verb > 0) {
    verbose = verb; // mplayer verbose level
  }
  
  // Set Y threshold
  if (y_threshold != 16) {
    std::cout << "Using Y palette threshold: " << y_threshold << "\n";
  }
  
  // Open the sub/idx subtitles
  VobSub vobsub;
  vobsub.open(subname, ifo_file, y_threshold);

  // list languages and exit
  if(list_languages) {
    std::cout << "Languages:\n";
    for(size_t i = 0; i < vobsub_get_indexes_count(vobsub.vob()); ++i) {
      char const *const id = vobsub_get_id(vobsub.vob(), i);
      std::cout << i << ": " << (id ? id : "(no id)") << '\n';
    }
    return 0;
  }
  
  // Handle stream Ids and language
  if(!lang.empty() && index >= 0) {
    std::cerr << "Setting both lang and index not supported.\n";
    return 1;
  }

  // Default OCR language to eng unless overridden by user or inferred from selected subtitle track.
  char const *tess_lang = tess_lang_user.empty() ? "eng" : tess_lang_user.c_str();
  if(!lang.empty()) {
    if(vobsub_set_from_lang(vobsub.vob(), lang.c_str()) < 0) {
      std::cerr << "No matching language for '" << lang << "' found! (Trying to use default)\n";
    } else if(tess_lang_user.empty()) {
      char const *const lang3 = iso639_1_to_639_3(lang.c_str());
      if(lang3) {
        tess_lang = lang3;
      }
    }
  } else {
    if(index >= 0) {
      if(static_cast<unsigned>(index) >= vobsub_get_indexes_count(vobsub.vob())) {
        std::cerr << "Index argument out of range: " << index << " ("
             << vobsub_get_indexes_count(vobsub.vob()) << ")\n";
        return 1;
      }
      vobsub_id = index;
    }
    
    if(vobsub_id >= 0) {
      char const *const lang1 = vobsub_get_id(vobsub.vob(), vobsub_id);
      if(lang1 && tess_lang_user.empty()) {
        char const *const lang3 = iso639_1_to_639_3(lang1);
        if(lang3) {
          tess_lang = lang3;
        }
      }
    }
  }

  // Init Tesseract
  tesseract::TessBaseAPI tess_base_api;
  if(tess_base_api.Init(NULL, tess_lang, tesseract::OEM_LSTM_ONLY) == -1) {
    std::cerr << "Failed to initialize Tesseract (OCR).\n";
    return 1;
  }
  
  // Set blacklist if not empty
  if(!blacklist.empty()) {
    tess_base_api.SetVariable("tessedit_char_blacklist", blacklist.c_str());
  }
  
  // Announce tesseract language data dir for verbosity. This needs a bit of work.
  if(verb>=1) {
    std::cout << "Using Tesseract data directory: " << tesseract_user_dir << ".\n";
  }

  // Run Tesseract multithreaded
  // TODO test: does this produce better, same, or worse results
  std::string tess_parallel = "1";
  tess_base_api.SetVariable("tessedit_parallelize", tess_parallel.c_str());

  // Set DPI to 72, or user-specified with --dpi
  // TODO test: do different values produce better results
  tess_base_api.SetVariable("user_defined_dpi", tess_user_dpi.c_str());

  // Attempt to improve whitespace detection
  std::string tess_rej_spaces = "0";
  std::string tess_improve_thresh = "1";
  tess_base_api.SetVariable("tessedit_use_reject_spaces", tess_rej_spaces.c_str());
  tess_base_api.SetVariable("tosp_improve_thresh", tess_improve_thresh.c_str());
  
  // Open srt output file
  std::ofstream srt_ofs;
  try {
    srt_ofs.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    srt_ofs.open(subname + ".srt",
	std::ios_base::out | std::ios_base::trunc);
  } catch (...) {
    throw generic_exception("Could not open .srt file for writing");
  }
  
  // Read subtitles and convert
  void *packet;
  int timestamp; // pts100
  int len;
  unsigned last_start_pts = 0;
  unsigned last_end_pts = 0;
  unsigned sub_counter = 1;

  OCRSubtitles subtitles(subname);
  
  while ((len = vobsub_get_next_packet(vobsub.vob(), &packet, &timestamp)) > 0) {
    if (timestamp >= 0) {
      spudec_assemble(vobsub.spu(), reinterpret_cast<unsigned char*>(packet), len, timestamp);
      if (!spudec_heartbeat(vobsub.spu(), timestamp)) {
	// spudec_assemble is still assembling packet from its fragments
	continue;
      }

      unsigned char const *sp_image;
      unsigned sp_width, sp_height, sp_stride;
      size_t sp_image_size; // should be at least height * stride
      unsigned start_pts = 0, end_pts = 0;

      // Get the image data to know dimensions and timing info
      spudec_get_data(vobsub.spu(), &sp_image, &sp_image_size, &sp_width, &sp_height, &sp_stride, &start_pts, &end_pts);

      if (sp_width == 0 || sp_height == 0) {
	std::cerr << sub_counter << ": start " << start_pts <<
	  ", image width: " << sp_width << ", height: " << sp_height <<
	  ": skipping" << std::endl;
	continue;
      }

      if (verbose > 0 and static_cast<unsigned>(timestamp) != start_pts) {
	std::cerr << sub_counter << ": time stamp from .idx (" << timestamp
		  << ") doesn't match time stamp from .sub ("
		  << start_pts << ")\n";
      }

      // deal with subtitles that have the same start time
      if (start_pts == last_start_pts && last_end_pts != UINT_MAX) {
	start_pts = last_end_pts;
      }
      last_start_pts = start_pts;
      last_end_pts = end_pts;

      if (dump_ext.size()) {
	std::stringstream ss;
	ss << subname << "-" << std::format("{:04}", sub_counter) << "." << dump_ext;
	cv::Mat sp_img(
	    static_cast<int>(sp_height),
	    static_cast<int>(sp_width),
	    CV_8UC1,
	    (void *) sp_image,
	    static_cast<std::size_t>(sp_stride));
	cv::imwrite(ss.str(), sp_img);
      }

      const bool have_text = subtitles.append(
	  sub_counter,
	  start_pts,
	  end_pts,
	  sp_image,
	  sp_width,
	  sp_height,
	  sp_stride);

      if (have_text) {
	sub_counter++;
      }
    }
    else {
      std::cerr << "timestamp < 0" << std::endl;
    }
  }

  subtitles.do_ocr(tess_base_api, ocr_batch_size);

  if (detect_italic) {
    subtitles.detect_italic();
  }

  subtitles.write_srt(srt_ofs, base_duration, chars_per_sec, show);

  std::cout << "Wrote Subtitles to '" << subname << ".srt'\n";
  return 0;
}

int
main(int argc, char **argv) {
  try {
    return main2(argc, argv);
  }
  catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
  }
  return 1;
}
