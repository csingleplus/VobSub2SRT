/*
 *  This file is part of vobsub2srt
 *
 *  Copyright (C) 2026 Bastiaan Stougie <wififreedom2026@protonmail.com>
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

#include "OCRSubtitles.h++"

#include "generic_exception.h++"
#include "debug.h++"
#include "Replacements.h++"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <format>

static uchar
img_mean_nonzero(
    const cv::Mat& img) {
  uint64_t total = 0;
  uint64_t count = 0;
  for (int row = 0; row < img.rows; row++) {
    for (int col = 0; col < img.cols; col++) {
      const uchar value = img.at<uchar>(row, col);
      if (value) {
	total += value;
	count++;
      }
    }
  }
  return (uchar) (total / count);
}

static void
img_histogram(
    const cv::Mat& img,
    std::vector<int>& color_histogram) {
  color_histogram.clear();
  color_histogram.resize(256, 0);

  for (int row = 0; row < img.rows; row++) {
    for (int col = 0; col < img.cols; col++) {
      const uchar value = img.at<uchar>(row, col);
      color_histogram[static_cast<std::size_t>(value)]++;
    }
  }
}

std::ostream&
dump_histogram(
    std::ostream& os,
    std::vector<int>& color_histogram) {
  for (std::size_t i = 0; i < color_histogram.size(); i++) {
    if (color_histogram[i]) {
      os << "  " << std::format("{:4}", i) << ": " <<
	std::format("{:5}", color_histogram[i]) << std::endl;
    }
  }
  return os;
}

OCRSubtitles::SubtitleInfo::SubtitleInfo(
    const std::size_t subtitle_number,
    const uint32_t start_pts,
    const uint32_t end_pts,
    const unsigned char* const sp_image,
    const unsigned sp_width,
    const unsigned sp_height,
    const unsigned sp_stride)
    : subtitle_number(subtitle_number),
      start_pts(start_pts),
      end_pts(end_pts),
      bw_img(std::move(cv::Mat(
	      static_cast<int>(sp_height),
	      static_cast<int>(sp_width),
	      CV_8UC1,
	      (void*) sp_image,
	      static_cast<std::size_t>(sp_stride)).clone())),
      bw_line_bbox_vec(),
      combined_line_bbox_vec() {

  // Convert grayscale to white text on black background.
  // NOTA BENE: if text has grayscale color value < threshold, all text
  // will disappear.

  std::vector<int> histogram;
  img_histogram(bw_img, histogram);
  std::size_t min = INT_MAX;
  std::size_t count = 0;
  // skip 0 = transparent
  for (std::size_t i = 1; i < histogram.size(); i++) {
    if (histogram[i]) {
      min = std::min(min, i);
      count++;
    }
  }
  int threshold = 128;
  if (count == 1) {
    threshold = 255;
  }
  else if (count == 2 || count == 3) {
    // 2 or 3 colors, remove only darkest by thresholding
    // in order to not lose too much detail
    threshold = static_cast<int>(min);
  }

  cv::threshold(bw_img, bw_img, threshold, 255, cv::THRESH_BINARY);

  get_text_line_bboxes(bw_img, bw_line_bbox_vec);
}

OCRSubtitles::OCRSubtitles(
    const std::string &subname)
    : subname(subname),
      subtitle_info_vec(),
      subtitle_vec() {
}

bool
OCRSubtitles::append(
    const std::size_t subtitle_number,
    const uint32_t start_pts,
    const uint32_t end_pts,
    const unsigned char* const sp_image,
    const unsigned sp_width,
    const unsigned sp_height,
    const unsigned sp_stride) {

  // Tesseract performs slightly better with larger amounts of text,
  // so store subtitle information, then process in batches in
  // combined images.

  SubtitleInfo si(
      subtitle_number,
      start_pts,
      end_pts,
      sp_image,
      sp_width,
      sp_height,
      sp_stride);

  const bool has_lines = !si.bw_line_bbox_vec.empty();

  if (has_lines) {
    subtitle_info_vec.emplace_back(std::move(si));
  }

  if (debug || subtitle_number == debug_subtitle_number) {
    const cv::Mat sp_img(
	  static_cast<int>(sp_height),
	  static_cast<int>(sp_width),
	  CV_8UC1,
	  (void*) sp_image,
	  static_cast<std::size_t>(sp_stride));
    if (debug_ext.size()) {
      std::stringstream ss;
      ss << subname << "-" << subtitle_number << "-spudec." << debug_ext;
      cv::imwrite(ss.str(), sp_img);
      std::cerr << "subtitle " << subtitle_number << ": image saved as " << ss.str() << std::endl;
    }
    std::vector<int> sp_img_histogram;
    img_histogram(sp_img, sp_img_histogram);
    std::cerr << "subtitle " << subtitle_number << ", spudec image histogram (color value: count)" << std::endl;
    dump_histogram(std::cerr, sp_img_histogram);
    const int mean = img_mean_nonzero(sp_img);
    std::cerr << "subtitle " << subtitle_number << ", mean_nonzero " << mean << std::endl;
  }

  return has_lines;
}

void
OCRSubtitles::do_ocr(
    tesseract::TessBaseAPI& tess_base_api,
    const std::size_t ocr_batch_size) {

  // TODO: more consistent number of lines per image, not number of subtitles
  const std::size_t batch_size = ocr_batch_size > 0 ? ocr_batch_size : 1;

  if (debug) {
    std::cerr << "batch OCR: batch size " << batch_size << " subtitles." << std::endl;
  }

  for (std::size_t batch_i = 0; batch_i < subtitle_info_vec.size(); batch_i += batch_size) {
    const std::size_t batch_end_i = ((batch_i + batch_size) < subtitle_info_vec.size()) ?
      (batch_i + batch_size) : subtitle_info_vec.size();

    // Gather information required to calculate combined image dimensions:
    // - total number of lines
    // - maximum line bbox width
    // - maximum line bbox height
    int line_count = 0;
    int line_max_width = 0;
    int line_max_height = 0;
    for (std::size_t si_i = batch_i; si_i < batch_end_i; si_i++) {
      if (debug || ((si_i + 1) == debug_subtitle_number)) {
	std::cerr << "Batch OCR: subtitle " << (si_i + 1) << ", # lines: " <<
	  subtitle_info_vec[si_i].bw_line_bbox_vec.size() << std::endl;
      }
      for (const auto & lb_it : subtitle_info_vec[si_i].bw_line_bbox_vec) {
	line_count++;
	line_max_width = std::max(line_max_width, lb_it.width);
	line_max_height = std::max(line_max_height, lb_it.height);
      }
    }

    if (debug) {
      std::cerr << "batch OCR: batch has " << line_count << " lines." << std::endl;
    }

    // NUM_BORDER_PIXELS is used for the vertical distance between lines as well.
    const int combined_img_width =
      NUM_BORDER_PIXELS /* left border */
      + line_max_width
      + NUM_BORDER_PIXELS /* right border */;
    const int combined_img_height =
      NUM_BORDER_PIXELS /* top border */
      + (line_count * line_max_height) /* lines */
      + ((line_count - 1) * NUM_BORDER_PIXELS) /* spacing between lines */
      + NUM_BORDER_PIXELS /* bottom border */;

    // New image with all white pixels
    cv::Mat combined_img(
	combined_img_height,
	combined_img_width,
	CV_8UC1,
	cv::Scalar(255));

    // Copy text lines into image, left-aligned
    {
      std::size_t line_i = 0;
      for (std::size_t si_i = batch_i; si_i < batch_end_i; si_i++) {
	for (const auto & lb_it : subtitle_info_vec[si_i].bw_line_bbox_vec) {
	  cv::Rect target_bbox(
	      NUM_BORDER_PIXELS,
	      NUM_BORDER_PIXELS + line_i * (line_max_height + NUM_BORDER_PIXELS),
	      lb_it.width,
	      lb_it.height);
	  subtitle_info_vec[si_i].combined_line_bbox_vec.emplace_back(target_bbox);

	  // Ccopy line from bw_img into combined image, inverted to have black
	  // text on white background.
	  cv::Mat src(subtitle_info_vec[si_i].bw_img, lb_it);
	  cv::Mat dst(combined_img, target_bbox);
	  cv::bitwise_not(src, dst);

	  line_i++;
	}
      }
    }

    if (debug ||
	(((batch_i + 1) <= debug_subtitle_number) &&
	 (((batch_i + 1) + batch_size) > debug_subtitle_number))) {
      if (debug_ext.size()) {
	std::stringstream ss;
	ss << subname << "-" << "combined-" << (batch_i + 1) << "-" << batch_size << "." << debug_ext;
	cv::imwrite(ss.str(), combined_img);
	std::cerr << "batch OCR: batch image saved as " << ss.str() << std::endl;
      }
    }

    // now do ocr.
    batch_ocr(tess_base_api, combined_img, batch_i, batch_end_i);
  }
}

void
OCRSubtitles::correct_ocr(
    const std::string& replacements_file_name) {

  Replacements repl(replacements_file_name);
  repl.read();

  std::stringstream ss;
  {
    // reserve space
    std::string tmp;
    // string remains empty, but is pre-allocated
    tmp.reserve(64 * 1024);
    ss.str(std::move(tmp));
  }
  write(ss);

  std::istringstream iss;
  iss.str(repl.replace(ss.str()));

  read(iss);
}

void
OCRSubtitles::detect_italic() {
  // Strategy:
  // - Perform OCR on all subtitles (only once). This has already been
  //   done.
  // - Perform italic detection in multiple phases:
  //   - Phase 1: Assign high-confidence bboxes to symbols.
  //     - Iterate over all subtitles: assign only bboxes to symbols if highly
  //       confident that they are correct:
  //       - no invalid word bbox
  //       - no overlapping word bbox,
  //       - no invalid symbol bbox,
  //       - no overlapping symbol bbox, // TODO
  //       - all symbol bboxes inside the word bbox, // TODO
  //       - correct number of symbol bboxes for the number of symbols in 
  //         the word
  //   - Phase 2: build statistics.
  //     - Gather min, average and max width and height for each symbol
  //       that has a bbox assigned to it. Gather min, average, and max
  //       values for inter-word spacing and inter-symbol spacing within
  //       words.
  //     - Despite the effort in phase one, incorrect data may be used
  //       for the statistics, if:
  //       - OCR incorrectly identified a character
  //       - multiple issues with bboxes occur in the same word, for example,
  //         both an additional bbox (overlapping bboxes), and a single bbox
  //         for two symbols. This leads to a correct number of bboxes,
  //         which is accepted without further checks, because in this
  //         phase there are no statistics to base any checks on.
  //       For that reason, check for and exclude anything suspicious that
  //       could result in bad statistics, such as bounding boxes that can't
  //       be the correct size for a symbol, word spacing that can't be
  //       correct, etc.
  //   - Phase 3: remove all assigned bboxes from symbols.
  //     - With the statistics, it may be posible to arrive at a better result
  //       for bbox assignment. For example, based on the statistics about
  //       inter-word spacing, we can possibly better correct detected
  //       inaccurate word bboxes. This affects which bboxes are assigned to
  //       symbols. So wipe the previous result and start over.
  //   - Phase 4: use statistics to assign bboxes to symbols.
  //     - In this phase, we try to assign a bbox to as many symbols as
  //       possible, not just if there is high confidence.
  //   - Phase 5: detect italic
  //     - Based on the assigned bboxes, detect italic 

  TextStats text_stats;

  // Phase 1
  bboxes_assign(NULL);

  // Phase 2
  build_stats(text_stats);

  if (debug || debug_subtitle_number > 0) {
    text_stats.dump(std::cerr);
  }
  
  // Phase 3
  bboxes_remove();

  // Phase 4
  bboxes_assign(&text_stats);

  // Phase 5
  for (auto &it : subtitle_vec) {
    it.detect_italic(text_stats);
  }
}

void
OCRSubtitles::write_srt(
    std::ostream& os,
    const int base_duration,
    const int chars_per_sec,
    const bool show) {
  for (std::size_t i = 0; i < subtitle_vec.size(); i++) {
    subtitle_vec[i].write_srt(
	os,
	base_duration,
	chars_per_sec,
	show,
	((i + 1) < subtitle_vec.size()) ? 
	  std::optional(subtitle_vec[i + 1].start_pts_get()) : std::nullopt);
  }
}

void
OCRSubtitles::bboxes_assign(
    TextStats* const stats) {
  for (auto& it : subtitle_vec) {
    it.bboxes_assign(subname, stats);
  }
}

void
OCRSubtitles::build_stats(
    TextStats& stats) const {
  for (const auto& it : subtitle_vec) {
    it.build_stats(stats);
  }
}

void
OCRSubtitles::bboxes_remove() {
  for (auto& it : subtitle_vec) {
    it.bboxes_remove();
  }
}

void
OCRSubtitles::batch_ocr(
    tesseract::TessBaseAPI& tess_base_api,
    const cv::Mat& combined_img,
    const std::size_t batch_i,
    const std::size_t batch_end_i) {

  tess_base_api.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
  tess_base_api.SetImage(
      combined_img.data,
      combined_img.cols,
      combined_img.rows,
      1 /* bytes per pixel */,
      static_cast<int>(combined_img.step));
  tess_base_api.Recognize(0);

  // declare here for cleanup on exception
  tesseract::ResultIterator* ri = NULL;
  char* symbol = NULL;

  try {
    ri = tess_base_api.GetIterator();
    if (ri) {
      std::size_t si_i = batch_i; /* subtitle_info_vec index */
      std::vector<OCRLine> line_vec;
      std::vector<OCRWord> word_vec;
      std::vector<cv::Rect> word_bbox_vec;
      std::vector<cv::Rect> symbol_bbox_vec;

      do {
	bool first = true;

	do {
	  std::vector<OCRSymbol> symbol_vec;

	  do {
	    if (!first) {
	      ri->Next(tesseract::RIL_SYMBOL);
	    }
	    else {
	      first = false;
	    }

	    // If there is no more text in the image, GetUTF8Text returns NULL.
	    // This can only happen if the image is empty. In all other cases
	    // ri->Next returns false and the loop exits.
	    symbol = ri->GetUTF8Text(tesseract::RIL_SYMBOL);
	    if (!symbol) {
	      std::cerr << "WARNING: subtitle " << (si_i + 1) <<
		", line " << (line_vec.size() + 1) <<
		": ocr: image has no text (--batch-size with a larger value may fix this)" << std::endl;
	      delete ri;
	      ri = NULL;
	      return; // no text at all
	    }

	    // work around bug in tesseract, which recognizes 'I' as '|'
	    // '|' is very unlikely to occur in subtitles.
	    if (symbol[0] == '|')
	      symbol[0] = 'I';

	    // Tesseract's observed behaviour for the subtitles is equivalent
	    // to:
	    // - it calculates bounding boxes in some way, most perfectly
	    //   accurate, but some wildly inaccurate.
	    // - it then sorts the bounding boxes by something like the middle
	    //   of the bbox (sometimes they are not ordered by x coordinate),
	    //   inserting inaccurate bboxes (if any) in the wrong place.
	    // - it then returns the bounding box at index i for the symbol at
	    //   index i.
	    // - the returned bounding box may therefore be of a different
	    //   symbol.
	    // For this reason, we do not immediately assign bounding boxes to
	    // words and symbols, but make a list first, then check and improve
	    // the bounding boxes by using a list of bounding boxes determined
	    // with opencv contour detection, then assign the bounding boxes to
	    // words and symbols.
	    //
	    // The returned bottom and right of the bounding box are exclusive, just
	    // like for the contour bboxes we determine later.
	    //
	    // See tesseract/pageiterator.h, comment about "Coordinate system".
	    
	    symbol_vec.emplace_back(symbol);

	    int left = 0, top = 0, right = 0, bottom = 0;
	    if (!ri->BoundingBox(tesseract::RIL_SYMBOL,
		  &left, &top, &right, &bottom)) {
	      std::stringstream ss;
	      ss << "subtitle " << (si_i + 1) <<
		", line " << (line_vec.size() + 1) <<
		": ocr: could not get symbol bounding box";
	      throw generic_exception(ss.str());
	    }
	    symbol_bbox_vec.emplace_back(
	      cv::Rect(left, top, right - left, bottom - top));

	    delete[] symbol;
	    symbol = NULL;

	  } while (!ri->IsAtFinalElement(tesseract::RIL_WORD, tesseract::RIL_SYMBOL));

	  word_vec.emplace_back(
	      si_i + 1,
	      line_vec.size() + 1,
	      word_vec.size() + 1,
	      std::move(symbol_vec));

	  int left = 0, top = 0, right = 0, bottom = 0;
	  if (!ri->BoundingBox(tesseract::RIL_WORD,
		&left, &top, &right, &bottom)) {
	    std::stringstream ss;
	    ss << "subtitle " << (si_i + 1) <<
	      ", line " << (line_vec.size() + 1) <<
	      ": ocr: could not get word bounding box";
	    throw generic_exception(ss.str());
	  }
	  word_bbox_vec.emplace_back(
	    cv::Rect(left, top, right - left, bottom - top));
	} while (!ri->IsAtFinalElement(tesseract::RIL_TEXTLINE, tesseract::RIL_SYMBOL));

	while (si_i < batch_end_i && subtitle_info_vec[si_i].combined_line_bbox_vec.size() == 0) {
	  si_i++;
	}

	if (si_i < batch_end_i) {
	  const SubtitleInfo& si = subtitle_info_vec[si_i];

	  line_vec.emplace_back(
	      si_i + 1,
	      line_vec.size() + 1,
	      word_vec,
	      si.combined_line_bbox_vec[line_vec.size()],
	      word_bbox_vec,
	      symbol_bbox_vec);

	  if (line_vec.size() == si.combined_line_bbox_vec.size()) {
	    subtitle_vec.emplace_back(
		si.subtitle_number,
		si.start_pts,
		si.end_pts,
		combined_img,
		line_vec);

	    si_i++;
	  }
	}
	else {
	  std::stringstream ss;
	  ss << "subtitle " << (si_i + 1) <<
	    ", line " << (line_vec.size() + 1) <<
	    ": ocr: too many lines";
	  throw generic_exception(ss.str());
	}

      } while (ri->Next(tesseract::RIL_SYMBOL));

      if (si_i != batch_end_i) {
	std::stringstream ss;
	ss << "subtitle " << (si_i + 1) <<
	  ", line " << (line_vec.size() + 1) <<
	  ": ocr: too few lines";
      }

      delete ri;
      ri = NULL;
    }
    else {
      throw generic_exception("ocr: could not get tesseract ResultIterator");
    }
  } catch (...) {
    if (ri) {
      delete ri;
      ri = NULL;
    }
    if (symbol) {
      delete[] symbol;
      symbol = NULL;
    }

    throw;
  }
}

std::ostream&
OCRSubtitles::write(
    std::ostream& os) const {
  for (const auto& it : subtitle_vec) {
    it.write(os);
  }
  return os;
}

void
OCRSubtitles::read(
    std::istream& is) {
  for (auto & it : subtitle_vec) {
    it.read(is);
  }
}

