/*
 *  This file is part of vobsub2srt
 *
 *  Copyright (C) 2026 Bastiaan Stougie <wififreedm2026@protonmail.com>
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

#include "OCRLine.h++"

#include <opencv2/imgcodecs.hpp>

#include "generic_exception.h++"
#include "debug.h++"

std::size_t
OCRLine::num_ocr_symbols() const {
  std::size_t num = 0;
  for (std::size_t i = 0; i < word_vec.size(); ++i) {
    num += word_vec[i].num_ocr_symbols();
  }
  return num;
}

// Goals:
// - Don't modify the OCR result: symbols, words, lines. Adapt to OCR
//   mistakes by adjusting bounding boxes and excluding bounding boxes
//   from italic detetion instead.
// - Best effort. The quality of the COR result is not 100% perfect. So this
//   algorithm can only try to repair and work around as many issues as
//   possible.
// - Italic detection per word. Not per symbol or per line.
// - Punctuation is often part of OCR words, but should not be considered
//   part of the word for italic detection.
//
// stats is NULL on first call, then after that, stats are built,
// then the method is called again with stats != NULL.
bool
OCRLine::bboxes_assign(
    const std::string & subname,
    const cv::Mat& img,
    std::vector<cv::Rect> const & symbol_contour_bbox_vec,
    const TextStats * const stats) {
  // About symbol_contour_bbox_vec: These are bounding boxes for the symbols
  // in the line, detected with (opencv) contour detection. They are
  // ordered by cv::Rect.x coordinate, ascending.
  // These bounding boxes can be inaccurate:
  // - in case of touching characters: a combined bbox, resulting in
  //   too few bboxes. This is likely to happen sometimes.
  // - for italic characters that do not touch, the bounding boxes
  //   can overlap a little bit: for example for "va", the rightmost
  //   pixel of "v" may be above the first pixel(s) of "a".
  //   This is to be expected because we don't have italic ("slanted" /
  //   "sheared" / "skewed") bounding boxes.
  // - for double quotes vs multiple single quotes: a combined bbox,
  //   resulting in too few bboxes. This is unlikely to happen.

  // It's important to improve the quality of the symbol bboxes both for
  // gathering statistics, and for italic detection in general.
  // Problems that can't be fixed by only looking at symbol OCR bboxes and
  // symbol contour bboxes are:
  // - a bbox that spans two touching symbols
  //   - this happens if there is and invalid OCR bbox and a combined countour
  //     bbox because two symbols touch.
  //   - it isn't a problem for statistics because the number of bboxes for
  //     a word won't match the number of symbols of the OCR word, and for that
  //     reason the symbols of the word will not be included in the statistics
  //     (unless there's a second problem that causes one *more* bbox in the
  //     word, in which case it's not detectable, but that happens very
  //     infrequently).
  symbol_bboxes_improve(
    stats ? std::string() : subname,
    img,
    symbol_ocr_bbox_vec,
    symbol_contour_bbox_vec,
    nsv);

  word_bboxes_improve(
    stats ? std::string() : subname,
    img,
    word_ocr_bbox_vec,
    nsv,
    nwv,
    stats);


  if (stats) {
    // Further improve word bboxes. Potential problems to fix:
    // - word_bboxes_improve() may have removed invalid word bboxes,
    //   and now some symbol bboxes are not bounded by any word bbox.
    // - OCR has produced a word OCR bbox that is not invalid, but
    //   inaccurate and excludes a symbol that is now not part of
    //   any word OCR bbox.
    // - OCR has produced a word OCR bbox that is too narrow and does not
    //   cover all of the symbols of the word completely.
    if (!word_bboxes_bound_all_symbol_bboxes(nwv, nsv)) {
      std::vector<cv::Rect> tmp;

      // Fixing word bboxes is not simple if there are also
      // problems with symbol bboxes.

      // Try to fix by adding symbol bboxes and combining them into words.
      // Vary word spacing for combining symbol bboxes into word bboxes to get
      // at least the required number of word bboxes, or more.
      // This is not exact science: word spacing varies, and OCR may have made
      // a mistake, too.
      std::optional<int> min_word_spacing_opt = stats->word_spacing_min();
      std::optional<float> avg_symbol_spacing_opt = stats->symbol_spacing_avg();
      if (min_word_spacing_opt.has_value() && avg_symbol_spacing_opt.has_value()) {

	const int min_word_spacing_lim = 2 * avg_symbol_spacing_opt.value();
	for (int min_word_spacing = min_word_spacing_opt.value();
	     (tmp.size() < word_vec.size()) && (min_word_spacing >= min_word_spacing_lim);
	     min_word_spacing--) {
	  word_bboxes_fill_gaps_and_combine_based_on_spacing(nwv, nsv, tmp, min_word_spacing);
	}
      }

      if (debug || subtitle_number == debug_subtitle_number) {
	if (debug_ext.size()) {
	  std::stringstream ss;
	  ss << subname << "-" << subtitle_number << "-" << line_number << "-word-supplem-bboxes." << debug_ext;
	  cv::imwrite(ss.str(), bboxes_draw(img, bordered_bbox(), tmp));
	}
	cerr_log() << ": bboxes_assign: word supplem bboxes: ";
	bboxes_stream(std::cerr, tmp) << std::endl;
      }

      // If now we have too many word bboxes, see if we can merge some.
      while (tmp.size() > word_vec.size()) {

	// first evaluate possible merges
	std::vector<std::size_t> possibles;
	for (std::size_t word_i = 0; (word_i + 1) < word_vec.size(); word_i++) {
	  OCRWord &cw = word_vec[word_i];

	  const cv::Rect cr = tmp[word_i];
	  const cv::Rect nr = tmp[word_i + 1];

	  std::vector<cv::Rect> ccands;
	  std::vector<cv::Rect> ncands;

	  bboxes_get_word_candidates(cr, nsv, ccands);
	  bboxes_get_word_candidates(nr, nsv, ncands);

	  if ((cw.num_ocr_symbols() > ccands.size()) &&
	      (cw.num_ocr_symbols() == (ccands.size() + ncands.size()))) {
	    // This is still a guess. It may be that there is a symbol bbox in
	    // cw that covers two symbols.

	    // test if we can repair this word without merge of word bboxes
	    if (!cw.bboxes_assign(img, ccands, stats, true)) {
	      possibles.emplace_back(word_i);
	    }
	    // clear any assigned bboxes after the experiment
	    cw.bboxes_remove();
	  } else {
	    // This is also a guess. There may be overlapping bboxes in cw.
	    // do not consider a possibility.
	  }
	}

	if (possibles.size() == (tmp.size() - word_vec.size())) {
	  if (debug || subtitle_number == debug_subtitle_number) {
	    cerr_log() << ": bboxes_assign: word bboxes: merge index " << possibles[0] <<
	      " and " << (possibles[0] + 1) << std::endl;
	  }
	  // merge
	  tmp[possibles[0]] = tmp[possibles[0]] | tmp[possibles[0] + 1];
	  tmp.erase(tmp.begin() + possibles[0] + 1);

	  if (debug || subtitle_number == debug_subtitle_number) {
	    if (debug_ext.size()) {
	      std::stringstream ss;
	      ss << subname << "-" << subtitle_number << "-" << line_number << "-word-merged-bboxes." << debug_ext;
	      cv::imwrite(ss.str(), bboxes_draw(img, bordered_bbox(), tmp));
	    }
	    cerr_log() << ": bboxes_assign: word merged bboxes: ";
	    bboxes_stream(std::cerr, tmp) << std::endl;
	  }
	}
	else {
	  // If 0 possibles, OCR may have missed a symbol. Seen:
	  // - OCR recognizes "lll" instead of "I'll", so missed a symbol.
	  // Don't second-guess OCR, this code is just for italics detection for now.
	  // TODO: we could warn, though.
	  if (debug || subtitle_number == debug_subtitle_number) {
	    cerr_log() << ": bboxes_assign: word bboxes: FAILED: " <<
	      "merge possibilities inconclusive: " <<
	      possibles.size() << " possibles:";
	    for (const auto & it : possibles) {
	      std::cerr << " " << it;
	    }
	    std::cerr <<  std::endl;
	  }
	  break;
	}
      }

      if (tmp.size() == word_vec.size()) {
	// The number of word bboxes now matches word_vec.size(). Do the
	// numbers of symbols per word match?
	
	// This is the phase where we can try to assign, and still adjust
	// word bboxes by moving the first symbol from the next word,
	// or the last symbol from the previous word.

	// If word i has too few symbols, look at the neighbouring symbol
	// bboxes:
	// - If the previous symbol bbox could match the first symbol of word
	//   i, and the previous symbol bbox could _not_ match the last symbol
	//   of word i-1, move it to word i by adjusting word bboxes.
	// - Same for next symbol bbox, last symbol of word_i, and first
	//   symbol of word i+1.
	// - Can or can't match: always look at average symbol height and width
	//   in OCRWord::bboxes_assign(), and fail.
      }

      nwv.swap(tmp);
    }
  }

  // if word bboxes are really bad and span multiple words,
  // or OCR has split a word into two (in word_vec),
  // the improved bboxes can end up with fewer or more words
  // in that case, fall back to the original word_ocr_bbox_vec.
  // TODO: improve more.
  std::vector<cv::Rect> *word_bbox_vec =
    (word_vec.size() == nwv.size()) ? &nwv : &word_ocr_bbox_vec;

  // now assign symbol bboxes to symbols
  bool success = true;
  for (std::size_t word_i = 0; word_i < word_vec.size(); word_i++) {
    OCRWord &word = word_vec[word_i];
    const cv::Rect wr = (*word_bbox_vec)[word_i];
    std::vector<cv::Rect> cands;

    if (debug || subtitle_number == debug_subtitle_number) {
      cerr_log() << ", word " << (word_i + 1) << ": " << word << std::endl;
    }

    bboxes_get_word_candidates(wr, nsv, cands);

    if (debug || subtitle_number == debug_subtitle_number) {
      if (debug_ext.size()) {
	std::stringstream ss;
	ss << subname << "-" << subtitle_number << "-" << line_number << "-" << (word_i + 1) << "-word-cands." << debug_ext;
	cv::imwrite(ss.str(), bboxes_draw(img, bordered_bbox(), cands));
      }
      cerr_log() << ", word " << (word_i + 1) << ": " << word << ": improved symbol bboxes for word: ";
      bboxes_stream(std::cerr, cands) << std::endl;
    }

    if (word.bboxes_assign(img, cands, stats, false)) {
      if (debug || subtitle_number == debug_subtitle_number) {
	if (debug_ext.size()) {
	  std::stringstream ss;
	  ss << subname << "-" << subtitle_number << "-" << line_number << "-" << (word_i + 1) << "-symbol-assigned-bboxes." << debug_ext;
	  cv::imwrite(ss.str(), word_symbol_bboxes_draw(word_i, img, 128));
	}
	cerr_log() << ", word " << (word_i + 1) << ": " << word << ": assigned symbol bboxes for word: ";
	bboxes_stream(std::cerr, cands) << std::endl;
      }
    } else {
      success = false;
    }
  }

  if (success) {
    return true;
  }

  if (stats) {
    if (debug || subtitle_number == debug_subtitle_number) {
      cerr_log() << ": bboxes_assign: FAILED" << std::endl;
    }
  }
  return false;
}

void
OCRLine::build_stats(
    const cv::Mat& img,
    TextStats &stats) const {
  for (auto &it : word_vec) {
    it.build_stats(img, stats);
  }

  for (std::size_t i = 1; i < nwv.size(); i++) {
    const int space_pixels = nwv[i].x - (nwv[i - 1].x + nwv[i - 1].width);
    // Word bboxes are not reliable if they overlap.
    if (space_pixels > 0)
      stats.word_spacing_add(space_pixels);
  }
}

void
OCRLine::bboxes_remove() {
  for (auto& it : word_vec) {
    it.bboxes_remove();
  }
}

void
OCRLine::assign_confidence(
    const cv::Mat& img,
    const TextStats& stats) {
  for (std::size_t word_i = 0; word_i < word_vec.size(); word_i++) {
    word_vec[word_i].assign_confidence(img, stats);
  }
}

void
OCRLine::propagate_word_confidence() {
  // Derive italic confidence for words from symbols.
  // This may be inconclusive.
  for (std::size_t word_i = 0; word_i < word_vec.size(); word_i++) {
     word_vec[word_i].derive_confidence();
  }

  // Propagate italic_confidence for uncertain words where appropriate.
  bool have_uncertain = false;
  bool made_change = false;
  do {
    have_uncertain = false;
    made_change = false;

    for (std::size_t word_i = 0; word_i < word_vec.size(); word_i++) {
      OCRWord &word = word_vec[word_i];

      if (!CONFIDENT_NOT_ITALIC(word.italic_confidence()) &&
	  !CONFIDENT_ITALIC(word.italic_confidence())) {
	float left_confidence = DEFAULT_CONFIDENCE;
	float right_confidence = DEFAULT_CONFIDENCE;

	if (word_i > 0) {
	  left_confidence = word_vec[word_i - 1].italic_confidence();
	}
	if (word_i < (word_vec.size() - 1)) {
	  right_confidence = word_vec[word_i + 1].italic_confidence();
	}

	if (word.derive_confidence(left_confidence, right_confidence)) {
	  made_change = true;
	}
	else {
	  have_uncertain = true;
	}
      }
    }
  } while (have_uncertain && made_change);
}

void
OCRLine::write_srt(std::ostream & os) const {
  // Only look at italic_confidence of words, not of symbols.
  bool entire_line_is_italic = true;
  bool in_italic = false;
  for (std::size_t word_i = 0; word_i < word_vec.size(); word_i++) {
    const bool word_is_last = ((word_i + 1) == word_vec.size());
    const OCRWord &word = word_vec[word_i];

    const bool word_is_italic = CONFIDENT_ITALIC(word.italic_confidence());
    if (!word_is_italic && !(word_i == 0 && word.is_minus())) {
      entire_line_is_italic = false;
    }

    // TODO if there is a single quote in the middle of an OCR word,
    // the part before and after it should be considered separate
    // words, and italic_confidence should also be separate.
    // This should already be taken into account before the call to
    // propagate_word_confidence().

    word.write_srt_punct_at_begin(os);
    if (word_is_italic && !in_italic) {
      os << "<i>";
      in_italic = true;
    }
    word.write_srt_between_punct(os);
    if (word_is_last) {
      if (entire_line_is_italic) {
	word.write_srt_punct_at_end(os);
      }
      if (in_italic) {
	os << "</i>";
	in_italic = false;
      }
      if (!entire_line_is_italic) {
	word.write_srt_punct_at_end(os);
      }
      os << "\n";
    } else {
      if (in_italic && !word_is_last &&
	  (!CONFIDENT_ITALIC(word_vec[word_i + 1].italic_confidence()))) {
	os << "</i>";
	in_italic = false;
      }
      word.write_srt_punct_at_end(os);
      os << " ";
    }
  }
}

std::size_t
OCRLine::num_chars_for_duration() const {
  std::size_t num = 0;
  for (std::size_t i = 0; i < word_vec.size(); i++) {
    // +1 for space or newline
    num += word_vec[i].num_ocr_symbols() + 1;
  }
  return num;
}

void
OCRLine::dump(std::ostream& os) const {
  for (const auto& it : word_vec) {
    it.dump(os);
  }
}

void
OCRLine::init(
    std::vector<OCRWord>& word_vec_arg,
    std::vector<cv::Rect>& word_ocr_bbox_vec_arg,
    std::vector<cv::Rect>& symbol_ocr_bbox_vec_arg) {
  word_vec.swap(word_vec_arg);
  word_ocr_bbox_vec.swap(word_ocr_bbox_vec_arg);
  // Tesseract does seem to sort bboxes, but not by x coordinate and then
  // width (maybe by their center?)
  bboxes_sort(word_ocr_bbox_vec);
  symbol_ocr_bbox_vec.swap(symbol_ocr_bbox_vec_arg);
  // Tesseract does seem to sort bboxes, but not by x coordinate and then
  // width (maybe by their center?)
  bboxes_sort(symbol_ocr_bbox_vec);
}

std::ostream&
OCRLine::cerr_log() const {
  return std::cerr << "subtitle " << subtitle_number <<
    ", line " << line_number;
}

cv::Rect
OCRLine::bordered_bbox() const {
  return cv::Rect(
      priv_bbox.x - NUM_BORDER_PIXELS,
      priv_bbox.y - NUM_BORDER_PIXELS,
      priv_bbox.width + 2 * NUM_BORDER_PIXELS,
      priv_bbox.height + 2 * NUM_BORDER_PIXELS);
}

// Note that a double quote has a column of whitespace in the middle.
// Specify check_all_columns = true only if certain that the bbox is not for a
// symbol that may have a white column, such as a double-quote.
bool
OCRLine::bbox_is_invalid(
    const cv::Mat& img,
    const cv::Rect& bbox,
    const bool check_all_columns) {

  // bbox has width <= 0 or height <= 0: bad
  if (bbox.width <= 0 || bbox.height <= 0) {
    return true;
  }

  if (check_all_columns)
  {
    // bbox has at least 1 column of only white pixels: bad
    for (int i = 0; i < bbox.width; i++) {
      int j = 0;
      for (; j < bbox.height; j++) {
	if (img.at<uchar>(bbox.y + j, bbox.x + i) != ((uchar) 255)) {
	  break;
	}
      }
      if (j == bbox.height) {
	// white-only column found, bad bbox
	return true;
      }
    }
  }
  else {
    // bbox has a left column with only white pixels: bad
    int i = 0;
    for (i = 0; i < bbox.height; i++) {
      if (img.at<uchar>(bbox.y + i, bbox.x) != ((uchar) 255)) {
	break;
      }
    }
    if (i == bbox.height) {
      return true;
    }

    // bbox has a right column with only white pixels: bad
    for (i = 0; i < bbox.height; i++) {
      if (img.at<uchar>(bbox.y + i, bbox.x + bbox.width - 1) != ((uchar) 255)) {
	break;
      }
    }
    if (i == bbox.height) {
      return true;
    }
  }

  // bbox has a top row with only white pixels: bad
  {
    int i = 0;
    for (; i < bbox.width; i++) {
      if (img.at<uchar>(bbox.y, bbox.x + i) != ((uchar) 255)) {
	break;
      }
    }
    if (i == bbox.width) {
      return true;
    }
  }

  // OCR bbox has a bottom row with only white pixels: bad
  {
    int i = 0;
    for (; i < bbox.width; i++) {
      if (img.at<uchar>(bbox.y + bbox.height - 1, bbox.x + i) != ((uchar) 255)) {
	break;
      }
    }
    if (i == bbox.width) {
      return true;
    }
  }

  return false;
}

void
OCRLine::symbol_bboxes_remove_invalid(
    const cv::Mat& img,
    const std::vector<cv::Rect>& src,
    std::vector<cv::Rect>& dst) {

  dst.clear();

  const int dq_y_limit = priv_bbox.y + (priv_bbox.height / 2);

  for (auto & it : src) {
    const bool maybe_double_quote = ((it.y + it.height) < dq_y_limit);

    if (!bbox_is_invalid(img, it, !maybe_double_quote)) {
      dst.emplace_back(it);
    }
  }
}

void
OCRLine::symbol_bboxes_fill_gaps(
    const std::vector<cv::Rect>& src,
    const std::vector<cv::Rect>& src2,
    std::vector<cv::Rect>& dst) {

  // Strategy:
  // - Add all src bboxes to dst. they may overlap.
  // - Add all src2 bboxes that cover some range that is not covered by any
  //   src bbox.
  // - Deal with resulting overlap elsewhere.
  // - At return elements of dst are in ascending order of cv::Rect.x.

  dst.clear();

  std::size_t src2_i = 0;
  std::size_t src_i = 0;
  int src_max_x = -1;
  while (src2_i < src2.size()) {
    // add and skip all src bboxes that come before this src2 bbox.
    while ((src_i < src.size()) && (src[src_i].x + src[src_i].width) <= src2[src2_i].x) {
      dst.emplace_back(src[src_i]);
      src_max_x = std::max(src_max_x, src[src_i].x + src[src_i].width);
      src_i++;
    }

    // skip all src bboxes until a gap occurs
    while ((src_i < src.size()) && (src[src_i].x <= src_max_x)) {
      dst.emplace_back(src[src_i]);
      src_max_x = std::max(src_max_x, src[src_i].x + src[src_i].width);
      src_i++;
    }

    if (src_i < src.size()) {
      // gap from src_max_x (inclusive) to src[src_i].x (exclusive)
      if ((src2[src2_i].x + src2[src2_i].width) <= src_max_x) {
	// src2 comes completely before gap, so skip
	src2_i++;
      }
      else if (src2[src2_i].x >= src[src_i].x) {
	// src2 comes completely after gap, so we can't fill the gap
	// (space between symbols)
	dst.emplace_back(src[src_i]);
	src_max_x = std::max(src_max_x, src[src_i].x + src[src_i].width);
	src_i++;
      }
      else {
	// src2 covers (part of) gap
	dst.emplace_back(src2[src2_i]);
	src2_i++;
      }
    }
    else {
      // gap from src_max_x (inclusive) to infinity (no more src bboxes)
      if ((src2[src2_i].x + src2[src2_i].width) <= src_max_x) {
	// src2 comes completely before gap, so skip
	src2_i++;
      } else {
	dst.emplace_back(src2[src2_i]);
	src2_i++;
      }
    }
  }
  while (src_i < src.size()) {
    dst.emplace_back(src[src_i]);
    src_i++;
  }
}

void
OCRLine::symbol_bboxes_replace_combined(
  const std::vector<cv::Rect>& src,
  const std::vector<cv::Rect>& src2,
  std::vector<cv::Rect>& dst) {

  dst.clear();

  // If possible, replace one OCR bbox from src by two contour bboxes from
  // src2 of which the union bounds the OCR bbox in x direction.
  // This improves situations with a combined OCR bbox for, for example:
  // - italic "y.",
  // - non-italic "xt,", "fo"
  // and possibly for more situations where the combined bbox was not deemed
  // invalid by bbox_is_invalid(), because there is no vertical line of white
  // pixels in between the symbols.
  // Contour bboxes are correct in these situations, because the pixels of the
  // symbols not touch.
  std::size_t src_i = 0;
  std::size_t src2_i = 0;
  while (src_i < src.size()) {
    const cv::Rect &r = src[src_i];

    while ((src2_i < src2.size()) && (src2[src2_i].x < r.x)) {
      src2_i++;
    }

    if ((src2_i < src2.size()) &&
       	(src2[src2_i].x == r.x) && 
	((src2_i + 1) < src2.size()) &&
	((src2[src2_i + 1].x + src2[src2_i + 1].width) ==
	 (r.x + r.width))) {
      dst.emplace_back(src2[src2_i]);
      src2_i++;
      dst.emplace_back(src2[src2_i]);
      src2_i++;
      src_i++;
    } else {
      dst.emplace_back(r);
      src_i++;
    }
  }
}

// TODO should we guarantee that all bounding box vectors are ordered by:
// - X coordinate (already the case)
// - and then also by WIDTH (not the case yet)

// This is not 100% guaranteed to be an improvement
// Take theoretical italic "mrv", where:
// - there is a good OCR bounding box for "m" and "r", but the OCR bounding box
//   for "v" was invalid
// - the contour bboxes are for "m" and "rv", because r and v touch
// This will result in src having improved bounding boxes "m", "r", and "rv".
// If "m" and "r" overlap a little due to italic, and "m" and "rv" as well for
// the same reason, and "r" and "rv" overlap, this will remove "r", which
// is a better bounding box than "rv". In that case, we'll have to split
// a combined bbox later.
void
OCRLine::symbol_bboxes_remove_inaccurate_overlapping(
    const std::vector<cv::Rect>& src,
    std::vector<cv::Rect>& dst) {

  dst.clear();

  // Sometimes tesseract creates a wildly inaccurate OCR bbox for an
  // OCR symbol S. Typically still in the x range of the OCR word,
  // but that's not guaranteed.
  // - Tesseract then sorts the OCR bboxes by x position, inserting
  //   the inaccurate OCR bbox for OCR symbol S in the wrong place.
  //   Note: Tesseract does not always sort by x perfectly. Seen:
  //   23:(373,1702)-(386,1722), 24:(372,1702)-(404,1722)
  //   So how does Tesseract do it, exactly? Does it sort by the center
  //   of the bbox maybe?
  // - Tesseract then returns this inaccurate OCR bbox for the wrong
  //   symbol, and the OCR bboxes for other OCR symbols can be shifted.
  // - At a different position in the ordered OCR bboxes, an OCR bbox
  //   is missing, or an OCR bbox covers two OCR symbols.
  //
  // If the current OCR bbox is such an inaccurate OCR bbox, there
  // can be four different cases:
  // a. The current OCR bbox overlaps both previous and next OCR bbox.
  // b. The current OCR bbox overlaps only the next OCR bbox.
  // c. The current OCR bbox overlaps only the previous OCR bbox.
  // d. The current OCR bbox does not overlap any other OCR bbox.
  //
  // Case a has been handled by remove_invalid_bboxes for instances where
  // there was whitespace in between prev and next bbox.
  // Now we look at where previous and next bbox touch or in case
  // of italic, slightly overlap.
  for (std::size_t i = 0; i < src.size(); i++) {
    const cv::Rect &r = src[i];

    if ((i > 0) && ((i + 1) < src.size())) {
      const cv::Rect &p = src[i - 1];
      const cv::Rect &n = src[i + 1];
      if (/* current overlaps prev */
	  (r.x < (p.x + p.width)) &&
	  /* current overlaps next */
	  ((r.x + r.width) > n.x) &&
	  /* prev touches or overlaps next */
	  ((p.x + p.width) >= n.x)) {
	// do not add to dst
	continue;
      }
    }
    dst.emplace_back(r);
  }
}

void
OCRLine::symbol_bboxes_remove_overlapped_by_1(
    const std::vector<cv::Rect>& src,
    std::vector<cv::Rect>& dst) {

  dst.clear();

  for (std::size_t i = 0; i < src.size(); i++) {
    const cv::Rect &r = src[i];
    if (dst.size()) {
      const cv::Rect &p = dst.back();
      if ((r.x + r.width) <= (p.x + p.width)) {
	// skip r
	continue;
      }
    }

    if ((i + 1) < src.size()) {
      const cv::Rect &n = src[i + 1];
      if (r.x == n.x && (r.x + r.width) <= (n.x + n.width)) {
	// skip r
	continue;
      }
    }

    dst.emplace_back(r);
  }
}

void
OCRLine::symbol_bboxes_improve(
    const std::string & subname,
    const cv::Mat& img,
    const std::vector<cv::Rect>& src, // ocr bboxes
    const std::vector<cv::Rect>& src2, // contour bboxes
    std::vector<cv::Rect>& dst) {

  dst.clear();

  if (debug || subtitle_number == debug_subtitle_number) {
    if (subname.size() && debug_ext.size()) {
      std::stringstream ss;
      ss << subname << "-" << subtitle_number << "-" << line_number << "-symbol-ocr-bboxes." << debug_ext;
      cv::imwrite(ss.str(), bboxes_draw(img, bordered_bbox(), src));
    }
    cerr_log() << ": symbol ocr      bboxes: ";
    bboxes_stream(std::cerr, src) << std::endl;

    if (subname.size() && debug_ext.size()) {
      std::stringstream ss;
      ss << subname << "-" << subtitle_number << "-" << line_number << "-symbol-contour-bboxes." << debug_ext;
      cv::imwrite(ss.str(), bboxes_draw(img, bordered_bbox(), src2));
    }
    cerr_log() << ": symbol contour  bboxes: ";
    bboxes_stream(std::cerr, src2) << std::endl;
  }

  std::vector<cv::Rect> tmp, tmp2;

  symbol_bboxes_remove_invalid(img, src, tmp);

  symbol_bboxes_fill_gaps(tmp, src2, tmp2);

  symbol_bboxes_replace_combined(tmp2, src2, tmp);

  symbol_bboxes_remove_inaccurate_overlapping(tmp, tmp2);

  // Strategy: remove all bboxes that are completely overlapped by one other
  // bbox, then repair any combined bboxes.
  symbol_bboxes_remove_overlapped_by_1(tmp2, dst);

  // After these steps, all symbols should be fully covered.
  //
  // There can still be overlapping bboxes:
  // 1 Seen: for "tw" that touch each other: a good OCR bbox for the "t", 
  //   completely overlapped by a combined contour bbox for "tw".
  // 2 Seen: for "b" at the begin of a word: there is a bad OCR bbox that
  //   bounds the first half of the width of "b", completely overlapped by a
  //   correct contour bbox for "b". Also seen for "h", "F", "B", "I" at
  //   the begin of a word.
  // 3 Maybe other cases?
  if (debug || subtitle_number == debug_subtitle_number) {
    if (subname.size() && debug_ext.size()) {
      std::stringstream ss;
      ss << subname << "-" << subtitle_number << "-" << line_number << "-symbol-improved-bboxes." << debug_ext;
      cv::imwrite(ss.str(), bboxes_draw(img, bordered_bbox(), dst));
    }
    cerr_log() << ": symbol improved bboxes: ";
    bboxes_stream(std::cerr, dst) << std::endl;
  }
}

void
OCRLine::word_bboxes_remove_overlapping(
    const std::vector<cv::Rect>& src,
    std::vector<cv::Rect>& dst) {

  dst.clear();

  // TODO the next one could also overlap the one after it
  for (std::size_t i = 0; i < src.size(); i++) {
    if ((i + 1) < src.size() && ((src[i].x + src[i].width) > src[i + 1].x)) {
      // remove this one and the next one
      i++;
    }
    dst.emplace_back(src[i]);
  }
}

void
OCRLine::word_bboxes_remove_invalid(
    const cv::Mat& img,
    const std::vector<cv::Rect>& src, // word bboxes
    std::vector<cv::Rect>& dst,
    const TextStats* const /* stats */) {

  dst.clear();

  for (auto & it : src) {
    if (!bbox_is_invalid(img, it, false)) {
      dst.emplace_back(it);
    }
  }
}

void
OCRLine::word_bboxes_remove_too_much_spacing(
    const cv::Mat& img,
    const std::vector<cv::Rect>& src, // word bboxes
    std::vector<cv::Rect>& dst,
    const TextStats& stats) {

  dst.clear();

  // Strategy: remove word bboxes that may overlap multiple words,
  // because they have suspiciously many sequential white columns.
  //
  // To be recombined according to word_vec later.
  std::optional<int> min_word_spacing_opt = stats.word_spacing_min();
  std::optional<float> avg_symbol_spacing_opt = stats.symbol_spacing_avg();
  if (!min_word_spacing_opt.has_value() || !avg_symbol_spacing_opt.has_value()) {
    return;
  }

  const int max_spacing = std::max(min_word_spacing_opt.value(), (int)(2 * avg_symbol_spacing_opt.value()));

  for (auto & it : src) {
    const int max_seq_white_columns = bbox_max_seq_white_columns(img, it);
    if (max_seq_white_columns <= max_spacing) {
      dst.emplace_back(it);
    }
  }
}

void
OCRLine::word_bboxes_improve(
    const std::string &subname,
    const cv::Mat& img,
    const std::vector<cv::Rect>& src,
    const std::vector<cv::Rect>& /* symbol_src */,
    std::vector<cv::Rect>& dst,
    const TextStats * const stats) {

  if (debug || subtitle_number == debug_subtitle_number) {
    if (subname.size() && debug_ext.size()) {
      std::stringstream ss;
      ss << subname << "-" << subtitle_number << "-" << line_number << "-word-ocr-bboxes." << debug_ext;
      cv::imwrite(ss.str(), bboxes_draw(img, bordered_bbox(), src));
    }
    cerr_log() << ": word ocr      bboxes: ";
    bboxes_stream(std::cerr, src) << std::endl;
  }

  std::vector<cv::Rect> tmp;

  // Remove overlapping before removing invalid bboxes.
  // In case of an invalid bbox, there may also be an overlapping bbox that
  // bounds two words. We can only detect that overlapping bbox before removing
  // the invalid bboxes.
  word_bboxes_remove_overlapping(src, tmp);

  if (stats) {
    std::vector<cv::Rect> tmp2;

    word_bboxes_remove_invalid(img, tmp, tmp2, stats);

    word_bboxes_remove_too_much_spacing(img, tmp2, dst, *stats);
  }
  else {
    word_bboxes_remove_invalid(img, tmp, dst, stats);
  }

  // TODO also remove other types of inaccurate bboxes.
  //      for example: if any of the contour bboxes partially or completely overlap a word bbox,
  //      the word bbox can't be trusted.

  // TODO we can also exclude entire lines if there are issues, for more reliable statistics

  if (debug || subtitle_number == debug_subtitle_number) {
    if (subname.size() && debug_ext.size()) {
      std::stringstream ss;
      ss << subname << "-" << subtitle_number << "-" << line_number << "-word-improved-bboxes." << debug_ext;
      cv::imwrite(ss.str(), bboxes_draw(img, bordered_bbox(), dst));
    }
    cerr_log() << ": word improved bboxes: ";
    bboxes_stream(std::cerr, dst) << std::endl;
  }
}

bool
OCRLine::word_bboxes_bound_all_symbol_bboxes(
    const std::vector<cv::Rect>& src, // word bboxes
    const std::vector<cv::Rect>& src2) { // symbol bboxes

  std::size_t src2_i = 0;
  std::size_t src_i = 0;
  int src_max_x = -1;
  while (src2_i < src2.size()) {
    // skip all src bboxes that come before this src2 bbox.
    while ((src_i < src.size()) && (src[src_i].x + src[src_i].width) <= src2[src2_i].x) {
      src_max_x = std::max(src_max_x, src[src_i].x + src[src_i].width);
      src_i++;
    }

    // skip all src bboxes until a gap occurs
    while ((src_i < src.size()) && (src[src_i].x <= src_max_x)) {
      src_max_x = std::max(src_max_x, src[src_i].x + src[src_i].width);
      src_i++;
    }

    if (src_i < src.size()) {
      // gap from src_max_x (inclusive) to src[src_i].x (exclusive)
      if ((src2[src2_i].x + src2[src2_i].width) <= src_max_x) {
	// src2 comes completely before gap, so skip
	src2_i++;
      }
      else if (src2[src2_i].x >= src[src_i].x) {
	// src2 comes completely after gap, so we can't fill the gap
	// (space between symbols)
	src_max_x = std::max(src_max_x, src[src_i].x + src[src_i].width);
	src_i++;
      }
      else {
	// src2 covers (part of) gap
	return false;
      }
    }
    else {
      // gap from src_max_x (inclusive) to infinity (no more src bboxes)
      if ((src2[src2_i].x + src2[src2_i].width) <= src_max_x) {
	// src2 comes completely before gap, so skip
	src2_i++;
      } else {
	return false;
      }
    }
  }

  return true;
}

void
OCRLine::word_bboxes_fill_gaps_and_combine_based_on_spacing(
    const std::vector<cv::Rect>& src, // word bboxes
    const std::vector<cv::Rect>& src2, // symbol bboxes
    std::vector<cv::Rect>& dst,
    const int min_word_spacing) {

  dst.clear();

  std::vector<cv::Rect> tmp;

  // Fill the gaps with symbol bboxes
  symbol_bboxes_fill_gaps(src, src2, tmp);

  // Combine added symbol bboxes into words.
  // Notes:
  // - An inaccurate too narrow src word bbox may have been completely
  //   overlapped by a symbol bbox from src2. TODO
  std::size_t tmp_i = 0;
  while (tmp_i < tmp.size()) {
    cv::Rect r = tmp[tmp_i];

    cv::Rect new_word(r);
    tmp_i++;
    while (tmp_i < tmp.size()) {
      cv::Rect rr = tmp[tmp_i];
      if (rr.x >= (new_word.x + new_word.width + min_word_spacing)) {
	break;
      }
      new_word = new_word | rr; // union
      tmp_i++;
    }
    dst.emplace_back(new_word);
  }
}

void
OCRLine::bboxes_get_word_candidates(
  const cv::Rect word,
  const std::vector<cv::Rect> &src,
  std::vector<cv::Rect>& dst) {

  dst.clear();

  for (std::size_t src_i = 0; src_i < src.size(); src_i++) {
    const cv::Rect r = src[src_i];

    if ((r.x >= word.x) && ((r.x + r.width) <= (word.x + word.width))) {
      dst.emplace_back(r);
    }
    else if ((r.x < (word.x + word.width)) && ((r.x + r.width) > word.x)) {
      if (debug) {
	cerr_log() << ": bboxes_get_word_candidates: partial overlap: w: ";
	bbox_stream(std::cerr, word);
	std::cerr << ", s: ";
	bbox_stream(std::cerr, r);
	std::cerr << std::endl;
      }
      // inaccurate word bounding box or inaccurate symbol bounding box.
      // do not add symbol bounding box.
    }
    else if (r.x > (word.x + word.width)) {
      break;
    }
  }
}

cv::Mat
OCRLine::word_symbol_bboxes_draw(
  std::size_t const word_index,
  const cv::Mat& img,
  unsigned char grayscale_color) const {

  if (word_index > word_vec.size()) {
    throw generic_exception("symbol_bboxes_draw: word index out of range");
  }

  const cv::Rect img_bbox = bordered_bbox();
  cv::Mat result_img(img_bbox.height, img_bbox.width, img.type());
  cv::Mat(img, img_bbox).copyTo(result_img);

  word_vec[word_index].symbol_bboxes_draw(result_img, img_bbox, grayscale_color);

  return result_img;
}

cv::Mat
OCRLine::symbol_bboxes_draw(
  const cv::Mat& img,
  unsigned char grayscale_color) const {

  const cv::Rect img_bbox = bordered_bbox();
  cv::Mat result_img(img_bbox.height, img_bbox.width, img.type());
  cv::Mat(img, img_bbox).copyTo(result_img);

  for (const auto& it : word_vec) {
    it.symbol_bboxes_draw(result_img, img_bbox, grayscale_color);
  }

  return result_img;
}

