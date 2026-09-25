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

#include <iostream>

#include "debug.h++"
#include "bbox.h++"
#include "OCRWord.h++"

bool
OCRWord::bboxes_assign(
    const cv::Mat& img,
    const std::vector<cv::Rect>& src, // candidate bboxes for this word
    const TextStats* const stats,
    const bool test_only) {

  auto cerr_log = [&]() -> std::ostream& {
    return std::cerr << "subtitle " << subtitle_number <<
      ", line " << line_number <<
      ", word " << word_number <<
      ": " << *this <<
      (test_only ? " (test only)" : "") <<
      ": OCRWord::bboxes_assign: ";
  };

  // TODO if not stats yet, detect all overlapping. Else,
  // ignore slight overlaps (often italic).
  std::vector<std::size_t> overl;
  bboxes_get_overlapping(src, overl);

  if (debug || subtitle_number == debug_subtitle_number) {
    cerr_log() <<
      "num_sym=" << num_ocr_symbols() <<
      ", num_cands=" << src.size() <<
      ": num_overl=" << overl.size() <<
      std::endl;
  }

  if (!stats) {
    // only assign if high confidence that bboxes are correct
    if ((overl.size() == 0) && (src.size() == num_ocr_symbols())) {
      return bboxes_assign(src, stats, test_only);
    }
    else {
      if (debug || subtitle_number == debug_subtitle_number) {
	cerr_log() << "no bboxes assigned, need reliable statistics" << std::endl;
      }
      return false;
    }
  }

  if ((overl.size() == 0) && (src.size() == num_ocr_symbols())) {
    return bboxes_assign(src, stats, test_only);
  }
  else if ((overl.size() == 0) && (src.size() < num_ocr_symbols())) {
    return bboxes_assign_repair_too_few_bboxes(img, src, *stats, test_only);
  }

  if (debug || subtitle_number == debug_subtitle_number) {
    cerr_log() <<
      "num_sym=" << num_ocr_symbols() <<
      ", num_cands=" << src.size() <<
      ", num_overl=" << overl.size() <<
      ": not implemented" <<
      std::endl;
  }

  return false;
}

void
OCRWord::build_stats(
    const cv::Mat& img,
    TextStats& stats) const {
  // symbols
  for (std::size_t i = 0; i < symbol_vec.size(); i++) {
    symbol_vec[i].build_stats(img, stats);
  }

  // symbol spacing within a word
  for (std::size_t i = 1; i < symbol_vec.size(); i++) {
    const OCRSymbol& p = symbol_vec[i - 1];
    const OCRSymbol& c = symbol_vec[i];

    if (c.bboxes().size() == 1 && p.bboxes().size() == 1) {
      const int space_pixels = c.bboxes()[0].x -
	  (p.bboxes()[0].x + p.bboxes()[0].width);
      // don't add if overlapping: low confidence, possibly italic.
      if (space_pixels >= 0)
	stats.symbol_spacing_add(space_pixels);
    }
  }
}

void
OCRWord::bboxes_remove() {
  for (auto& it : symbol_vec) {
    it.bboxes_remove();
  }
}

void
OCRWord::assign_confidence(
    const cv::Mat& img,
    const TextStats& stats) {
  for (auto& it : symbol_vec) {
    it.assign_confidence(img, stats);
  }
}

void
OCRWord::derive_confidence() {
  // Compute italic_confidence from symbols
  // Just take the average, this seems to work well.
  float sum_confidence = 0.0;
  for (std::size_t i = 0; i < symbol_vec.size(); i++) {
    sum_confidence += symbol_vec[i].italic_confidence();
  }
  priv_italic_confidence = sum_confidence / symbol_vec.size();
}

bool
OCRWord::derive_confidence(
    const float left_confidence,
    const float right_confidence) {

  if (CONFIDENT_ITALIC(left_confidence)) {
    if (CONFIDENT_ITALIC(right_confidence)) {
      priv_italic_confidence = 200.0;
      return true;
    } else if (!CONFIDENT_NOT_ITALIC(right_confidence)) {
      priv_italic_confidence = 100.0;
      return true;
    }
  } else if (CONFIDENT_NOT_ITALIC(left_confidence)) {
    if (CONFIDENT_NOT_ITALIC(right_confidence)) {
      priv_italic_confidence = -200.0;
      return true;
    } else if (!CONFIDENT_ITALIC(right_confidence)) {
      priv_italic_confidence = -100.0;
      return true;
    }
  } else {
    if (CONFIDENT_ITALIC(right_confidence)) {
      priv_italic_confidence = 101.0;
      return true;
    } else if (CONFIDENT_NOT_ITALIC(right_confidence)) {
      priv_italic_confidence = -101.0;
      return true;
    }
  }
  return false;
}


std::ostream&
OCRWord::write(
    std::ostream& os) const {
  for (const auto& it : symbol_vec) {
    it.write(os);
  }
  return os;
}

// only words are italic, not punctuation.
std::ostream&
OCRWord::write_srt_punct_at_begin(std::ostream& os) const {
  const std::size_t num = num_punct_at_begin();

  for (std::size_t i = 0; i < num; i++) {
    symbol_vec[i].write_srt(os);
  }
  return os;
}

// only words are italic, not punctuation.
std::ostream&
OCRWord::write_srt_between_punct(std::ostream& os) const {
  const std::size_t num_at_end = num_punct_at_end();
  if (num_at_end == symbol_vec.size()) {
    // already written by write_srt_punct_at_begin
    return os;
  }
  const std::size_t num_at_begin = num_punct_at_begin();

  for (std::size_t i = num_at_begin; i < symbol_vec.size() - num_at_end; i++) {
    symbol_vec[i].write_srt(os);
  }
  return os;
}

// only words are italic, not punctuation.
std::ostream&
OCRWord::write_srt_punct_at_end(std::ostream& os) const {
  const std::size_t num_at_end = num_punct_at_end();
  if (num_at_end == symbol_vec.size()) {
    // already written by write_srt_punct_at_begin
    return os;
  }
  const std::size_t start = symbol_vec.size() - num_at_end;

  for (std::size_t i = start; i < symbol_vec.size(); i++) {
    symbol_vec[i].write_srt(os);
  }
  return os;
}

bool
OCRWord::has_symbol_of(
    const char * const set) {
  for (std::size_t i = 0; i < symbol_vec.size(); i++) {
    if (symbol_vec[i].is_one_of(set)) {
      return true;
    }
  } 
  return false;
}

std::size_t
OCRWord::num_punct_at_begin() const {
  for (std::size_t i = 0; i < symbol_vec.size(); i++) {
    if (!symbol_vec[i].is_one_of("-.,;:\"'?!")) {
      return i;
    }
  }
  return symbol_vec.size();
}

std::size_t
OCRWord::num_punct_at_end() const {
  for (std::size_t i = symbol_vec.size(); i > 0; i--) {
    if (!symbol_vec[i - 1].is_one_of("-.,;:\"'?!")) {
      return (symbol_vec.size() - i);
    }
  }
  return symbol_vec.size();
}

bool
OCRWord::bboxes_assign(
    const std::vector<cv::Rect>& src,
    const TextStats* stats,
    const bool test_only) {

  auto cerr_log = [&]() -> std::ostream& {
    return std::cerr << "subtitle " << subtitle_number <<
      ", line " << line_number <<
      ", word " << word_number <<
      ": " << *this <<
      (test_only ? " (test only)" : "") <<
      ": OCRWord::bboxes_assign: ";
  };

  if (src.size() != symbol_vec.size()) {
    throw std::logic_error("bboxes_assign: BUG: mismatching number of symbol bboxes");
  }

  if (stats) {
    for (std::size_t i = 0; i < symbol_vec.size(); ++i) {
      const std::optional<int> min_height_opt =
	stats->symbol_height_min(symbol_vec[i].utf8_symbol());
      if (min_height_opt.has_value() &&
	  (src[i].height < min_height_opt.value())) {
	if (debug || subtitle_number == debug_subtitle_number) {
	  cerr_log() <<
	    ": symbol " << (i + 1) << ": " << symbol_vec[i].utf8_symbol() <<
	    ": candidate bbox height " << src[i].height <<
	    " < stats min height " << min_height_opt.value() <<
	    ", no solution." << std::endl;
	}
	// No solution.
	// (or: OCR possibly misidentified the symbol, but that's not something we can fix).
	return false;
      }
    }
  }

  for (std::size_t i = 0; i < symbol_vec.size(); ++i) {
    symbol_vec[i].bbox_assign(src[i], test_only);
  }

  return true;
}

bool
OCRWord::bboxes_assign_repair_too_few_bboxes(
    const cv::Mat& img,
    const std::vector<cv::Rect>& src,
    const TextStats& stats,
    const bool test_only) {

  auto cerr_log = [&]() -> std::ostream& {
    return std::cerr << "subtitle " << subtitle_number <<
      ", line " << line_number <<
      ", word " << word_number <<
      (test_only ? " (test only)" : "") <<
      ": OCRWord::bboxes_assign_repair_too_few_bboxes: ";
  };

  // CASE TYPE: No overlapping bboxes, too few bboxes.
  //
  // All symbols have been covered by bboxes.
  //
  // Possible causes:
  // - A combined OCR bbox and a combined contour bbox for touching
  //   symbols, possibly multiple times.
  //   Examples: "ff" "ft" "fw" "kt" "rj" "rt" "rv" "tf" "tt" "tw", or
  //   touching italic symbols.
  // - an OCR bbox for the *word* that is too narrow, causing a symbol bbox
  //   to be outside the bbox of the word. This symbol bbox is then outside
  //   all word bboxes.

  std::vector<cv::Rect> cands = src;

  // Method 1
  //
  // - This method works best for text that is not italic.
  // - The method is based on statistic about the average width of the
  //   symbols, and is therefore expected to work well if the font is the
  //   same size for all symbols in all subtitles. 
  // - This may not work if OCR has incorrectly identified a character.
  bool ok = true;
  std::size_t cand_i = 0;
  for (std::size_t sym_i = 0; sym_i < symbol_vec.size(); ) {
    OCRSymbol& symbol = symbol_vec[sym_i];

    if (cand_i >= cands.size()) {
      if (debug || subtitle_number == debug_subtitle_number) {
	cerr_log() << "possible bboxes too wide: no more bboxes " << std::endl;
      }
      ok = false;
      break;
    }

    const cv::Rect& cand = cands[cand_i];

    const std::optional<int> min_height_opt = stats.symbol_height_min(symbol.utf8_symbol());
    if (min_height_opt.has_value() &&
	(cand.height < min_height_opt.value())) {
	if (debug || subtitle_number == debug_subtitle_number) {
	  cerr_log() << "possible bboxes too wide" <<
	    ": symbol " << (sym_i + 1) << ": " << symbol.utf8_symbol() <<
	    ": candidate bbox height " << cand.height <<
	    " < stats min height " << min_height_opt.value() <<
	    ", no solution." << std::endl;
	}
      // No solution.
      // (or: OCR possibly misidentified the symbol, but that's not something we can fix).
      ok = false;
      break;
    }

    const std::optional<float> avg_width_opt = stats.symbol_width_avg(symbol.utf8_symbol());
    if (!avg_width_opt.has_value()) {
      if (debug || subtitle_number == debug_subtitle_number) {
	cerr_log() << "possible bboxes too wide" <<
	  ": symbol " << (sym_i + 1) << ": " << symbol.utf8_symbol() <<
	  ": no average width info for symbol " <<
	  ", accepting bbox for current symbol as is." << std::endl;
      }
      // intentionally continue past this symbol:
      // - this symbol might not be part of the problem.
      // - if this symbol turns out to be part of the problem,
      //   fall back to method 2.
      // - if this symbol turns out not to be part of the problem, 
      //   method 1 can still fix the problem.
      symbol.bbox_assign(cand, test_only);
      cand_i++;
      sym_i++;
      continue;
    }
    const float avg_width = avg_width_opt.value();

    // This works mainly for characters that are not italic.
    // This check can improve results by preventing the less accurate
    // check for a combined bbox below.
    if ((cand.width >= (avg_width - 1)) &&
	(cand.width <= (avg_width + 1))) {
      symbol.bbox_assign(cand, test_only);
      cand_i++;
      sym_i++;
      continue;
    }

    if ((sym_i + 1) == symbol_vec.size()) {
      if (debug || subtitle_number == debug_subtitle_number) {
	cerr_log() << "possible bboxes too wide" <<
	  ": symbol " << (sym_i + 1) << ": " << symbol.utf8_symbol() <<
	  ": no next symbol"
	  ", accepting bbox for current symbol as is." << std::endl;
      }
      // can't be a combined bbox for this symbol and the next
      symbol.bbox_assign(cand, test_only);
      cand_i++;
      sym_i++;
      continue;
    }

    OCRSymbol& symbol2 = symbol_vec[sym_i + 1];

    const std::optional<float> avg_width2_opt = stats.symbol_width_avg(symbol2.utf8_symbol());
    if (!avg_width2_opt.has_value()) {
      if (debug || subtitle_number == debug_subtitle_number) {
	cerr_log() << "possible bboxes too wide" <<
	  ": symbol " << (sym_i + 1) << ": " << symbol.utf8_symbol() <<
	  ": no average width info for next symbol " <<
	  ": symbol " << (sym_i + 2) << ": " << symbol2.utf8_symbol() <<
	  ", accepting bbox for current symbol as is." << std::endl;
      }
      // intentionally continue past this symbol:
      // - this symbol might not be part of the problem.
      symbol.bbox_assign(cand, test_only);
      cand_i++;
      sym_i++;
      continue;
    }
    const float avg_width2 = avg_width2_opt.value();
    const float sum_avg_widths = avg_width + avg_width2;

    // Check for a combined bbox
    if (cand.width >= (sum_avg_widths * 80 / 100)) {
      if (debug || subtitle_number == debug_subtitle_number) {
	cerr_log() << "possible bboxes too wide" <<
	  ": symbol " << (sym_i + 1) << ": " << symbol.utf8_symbol() <<
	  " and next symbol" <<
	  ": symbol " << (sym_i + 2) << ": " << symbol2.utf8_symbol() <<
	  ": split combined bbox" << std::endl;
      }
      const int width1 = cand.width * avg_width / sum_avg_widths;
      const int width2 = cand.width - width1;

      cv::Rect r1(cand.x, cand.y, width1, cand.height);
      bbox_shrink_vert(img, r1);

      cv::Rect r2(cand.x + width1, cand.y, width2, cand.height);
      bbox_shrink_vert(img, r2);

      symbol.bbox_assign(r1, test_only);
      symbol2.bbox_assign(r2, test_only);

      cand_i++;
      sym_i += 2;
      continue;
    }

    if (debug || subtitle_number == debug_subtitle_number) {
      cerr_log() << "possible bboxes too wide" <<
	": symbol " << (sym_i + 1) << ": " << symbol.utf8_symbol() <<
	": likely not a combined bbox" <<
	", bbox w: " << cand.width <<
	", s1 avg w: " << avg_width <<
	", s2 avg w: " << avg_width2 <<
	", accepting bbox for current symbol as is." <<
	std::endl;
    }

    // not a combined bbox
    symbol.bbox_assign(cand, test_only);
    cand_i++;
    sym_i++;
  }

  if (ok) {
    if (cand_i < cands.size()) {
      if (debug || subtitle_number == debug_subtitle_number) {
	cerr_log() << "possible bboxes too wide" <<
	  ": candidates left" << std::endl;
      }
      ok = false;
    }
  }

  if (ok) {
    if (debug || subtitle_number == debug_subtitle_number) {
      cerr_log() << "possible bboxes too wide" <<
	": split combined bbox: repaired" << std::endl;
    }
    return true;
  }

  // unsuccessful, clean up
  bboxes_remove();

  // If many bboxes in a word are closer together than normal, touch,
  // or overlap, the word is very likely italic.
  //
  // If a word is italic, the bboxes are hard to assign to symbols
  // because it's hard to identify which too-wide bboxes are for
  // combined characters. The italic characters themselves are often
  // also wider than normal, complicating the matter.
  //
  // Bboxes of italic symbols can be narrower or wider (narrower: an
  // italic "t" can be narrower that a normal "t" because the horizontal
  // bar that extends to the left shifts right. Similar for "f" and "i".
  // For most symbols, the italic version is wider than their
  // non-italic version.
  //
  // Spacing between symbols is also not entirely consistent, which
  // also complicates detection.
  //
  // Ideas:
  // - Look at character height and possibly relative y position.
  //   gjpq are below, bdfjklt are above, rest in the middle.
  //   Above and below are relative, we can look at relative y
  //   coordinate as well, if we can gather statistics.
  //   Can't be a combined bbox if the next character does not fit in
  //   the bbox, and other logical conclusions possible. Gets
  //   complex very fast, and effectiveness depends on which
  //   combination of symbols is in the word.
  //   But we can loop and apply conclusions until we make no more
  //   progress.
  //   - For example: "hu":
  //     - if the bbox is as high as the "h", so higher than the "u",
  //       no conclusion can be drawn if it's only for "h", or
  //       for "hu" combined.
  //       - if we look further ahead:
  //         - "hul", with a high and low bbox, bboxes should be
  //           assigned to "h" and "u", because the second one can't
  //           be a combined one, therefore the first neither.
  //         - "hulXXXXX", with two high bboxes: at least one of them
  //           must be a combined bbox.
  //     - if the bbox is as high as the "u", so lower than the "h",
  //       the bbox can't be for the "h", so we can't match and have
  //       to give up.
  //   - For example: "ul":
  //     - if the bbox is as high as the "u", so lower than the "l",
  //       it can't be a bbox for both.
  //     - if the bbox is as high as the "l", so higher than the "u",
  //       it must be a combined bbox for both.
  //     higher than the "u", so can't be a bbox for both.
  // - Alternative approach, take a word with 5 symbols:
  //   - if there's one bbox short, every symbol can be covered by a
  //     limited number of bboxes:
  //     - symbol 1 by bbox 1
  //     - symbol 2 by bbox 1 or 2
  //     - symbol 3 by bbox 2 or 3
  //     - symbol 4 by bbox 3 or 4
  //     - symbol 5 by bbox 4
  //   - if there are 2 bboxes short, and two combined bboxes for 2
  //     symbols each, every symbol can be covered by a limited number
  //     of bboxes:
  //     - symbol 1 by bbox 1
  //     - symbol 2 by bbox 1 or 2
  //     - symbol 3 by bbox 2
  //     - symbol 4 by bbox 2 or 3
  //     - symbol 5 by bbox 3
  //     - not seen in practice: one bbox for 3 characters, but
  //       could theoretically happen:
  //       - symbol 1,2,3 in bbox 1, 4 in bbox 2, 5 in bbox 3
  //       - etc.
  //   - only solve the case with 1 bbox short.
  // - Alternative approach: identify problem symbols. If none,
  //   don't try to match, just assign the word bbox. If there
  //   are, exclude the bboxes that could bound these problem
  //   symbols, then combine and assign the rest.
  //   For example: problematic symbol "y" in position 5 of 7, with
  //   only 6 bboxes. BBox 4 or 5 can cover y, so exclude both.
  //   Assign union of bbox 1,2,3 to symbol 1,2,3, and assign
  //   union of bbox 6 to symbol 7.
  // - Alternative approach: best effort, incomplete.
  //   - try to assign as many bboxes as possible based on height.
  // - Alternative approach: if we can't match, it's italic.
  //   Mark symbols as fixed italic.

  if (cands.size() == (symbol_vec.size() - 1)) {
    ok = true;

    // first index: symbol index
    // inner vector contains: candidate bbox indices for the symbol
    std::vector<std::vector<std::size_t> > sym_cands(symbol_vec.size());

    // For each symbol, add all candidate bboxes, without looking at
    // their properties yet.
    for (std::size_t sym_i = 0; sym_i < symbol_vec.size(); sym_i++) {
      // in the same order as they appear in cands
      if (sym_i > 0) {
	sym_cands[sym_i].emplace_back(sym_i - 1 /* = index in cands[] */);
      }
      if ((sym_i + 1) < symbol_vec.size()) {
	sym_cands[sym_i].emplace_back(sym_i /* = index in cands[] */);
      }
    }

    // Remove candidates bboxes with height < minimum height of symbol.
    // Minimum width is not entirely reliable: italic symbols may not be
    // included in the statistics and some italic versions of symbols can be
    // narrower than their normal versions, for example "i", "l", "t".)
    for (std::size_t sym_i = 0; sym_i < symbol_vec.size(); sym_i++) {
      OCRSymbol& symbol = symbol_vec[sym_i];
      std::vector<std::size_t>& cv = sym_cands[sym_i];
      
      const std::optional<float> min_height_opt = stats.symbol_height_min(symbol.utf8_symbol());
      if (!min_height_opt.has_value()) {
	if (debug || subtitle_number == debug_subtitle_number) {
	  cerr_log() << "possible bboxes too wide" <<
	    ": symbol " << (sym_i + 1) << ": " << symbol.utf8_symbol() <<
	    ": no minimum height info for symbol" << std::endl;
	}
	// leave the candidate bbox in place
	continue;
      }
      const int min_height = min_height_opt.value();

      for (std::size_t i = 0; i < cv.size(); ) {
	if (cands[cv[i]].height < min_height) {
	  // bbox height < minimum height of symbol
	  cv.erase(cv.begin() + i);
	} else {
	  i++;
	}
      }
    }

    // TODO: could add the following:
    // Look at combined height. For example, a combined bbox for "l" and "p"
    // would need a higher bbox than the ones for "l" and "p" individually.
    // - Would need statistics about relative y position of symbols within the line as well.

    // Try to deduct more:
    // - Check if there is a a bbox that can only be assigned to one symbol,
    //   and if so, remove any other candidate bbox from that symbol.
    // - If one was found, repeat.
    bool made_change = false;
    do {
      made_change = false;

      for (std::size_t cand_i = 0; cand_i < cands.size(); cand_i++) {
	const std::size_t sym1_i = cand_i;
	const std::size_t sym2_i = cand_i + 1;
	std::vector<std::size_t>& v1 = sym_cands[sym1_i];
	std::vector<std::size_t>& v2 = sym_cands[sym2_i];
	auto v1_it = std::find(v1.begin(), v1.end(), cand_i);
	auto v2_it = std::find(v2.begin(), v2.end(), cand_i);
	const bool found1 = v1_it != v1.end();
	const bool found2 = v2_it != v2.end();
	if (found1 && found2) {
	  if (v1.size() == 1 || v2.size() == 1) {
	    // Check if a split would result in acceptable widths
	    // Don't actually split. Only eliminate possibilities.
	    const OCRSymbol& symbol1 = symbol_vec[cand_i];
	    const OCRSymbol& symbol2 = symbol_vec[cand_i + 1];

	    const std::optional<float> avg_width1_opt = stats.symbol_width_avg(symbol1.utf8_symbol());
	    const std::optional<float> avg_width2_opt = stats.symbol_width_avg(symbol2.utf8_symbol());
	    if (avg_width1_opt.has_value() && avg_width2_opt.has_value()) {
	      const float avg_width1 = avg_width1_opt.value();
	      const float avg_width2 = avg_width2_opt.value();
	      const float sum_avg_widths = avg_width1 + avg_width2;

	      const cv::Rect& cand = cands[cand_i];
	      const int width1 = cand.width * avg_width1 / sum_avg_widths;
	      const int width2 = cand.width - width1;

	      if ((v1.size()) > 1 && (width1 < (0.5 * avg_width1))) {
		// width1 is unacceptable for symbol1, remove cand_i for symbol1
		v1.erase(v1_it);
		made_change = true;
	      }
	      if ((v2.size()) > 1 && (width2 < (0.5 * avg_width2))) {
		// width1 is unacceptable for symbol2, remove cand_i for symbol2
		v2.erase(v2_it);
		made_change = true;
	      }
	    }
	  }
	}
	else if (found1 && !found2) {
	  if (v1.size() > 1) {
	    // remove any other candidate from sym_cands[cand_i]
	    v1.clear();
	    v1.emplace_back(cand_i);
	    made_change = true;
	  }
	}
	else if (!found1 && found2) {
	  if (v2.size() > 1) {
	    // remove any other candidate from sym_cands[cand_i + 1]
	    v2.clear();
	    v2.emplace_back(cand_i);
	    made_change = true;
	  }
	}
      }
    } while (made_change);

    // Check if there is a symbol without candidate bbox.
    // If so, the attempt failed.
    if (ok) {
      for (std::size_t sym_i = 0; sym_i < sym_cands.size(); sym_i++) {
	if (sym_cands[sym_i].size() == 0) {
	  ok = false;
	  break;
	}
      }
    }

    if (ok) {
      for (std::size_t sym_i = 0; sym_i < sym_cands.size(); sym_i++) {
	const std::size_t sym1_i = sym_i;
	OCRSymbol& symbol1 = symbol_vec[sym1_i];

	const std::size_t sym2_i = sym_i + 1;
	if ((sym2_i < sym_cands.size()) &&
	    (sym_cands[sym1_i].size() == 1) &&
	    (sym_cands[sym2_i].size() == 1) &&
	    (sym_cands[sym1_i][0] == sym_cands[sym2_i][0])) {
	  // split the bbox
	  OCRSymbol& symbol2 = symbol_vec[sym2_i];

	  const std::optional<float> avg_width1_opt = stats.symbol_width_avg(symbol1.utf8_symbol());
	  const std::optional<float> avg_width2_opt = stats.symbol_width_avg(symbol2.utf8_symbol());
	  if (avg_width1_opt.has_value() && avg_width2_opt.has_value()) {
	    const float avg_width1 = avg_width1_opt.value();
	    const float avg_width2 = avg_width2_opt.value();
	    const float sum_avg_widths = avg_width1 + avg_width2;

	    const cv::Rect& cand = cands[sym_cands[sym1_i][0]];
	    const int width1 = cand.width * avg_width1 / sum_avg_widths;
	    const int width2 = cand.width - width1;

	    if ((width1 < (0.5 * avg_width1)) ||
		(width2 < (0.5 * avg_width2))) {
	      // width1 or width2 is unacceptable
	      sym_cands[sym1_i].clear();
	      sym_cands[sym2_i].clear();
	      ok = false;
	      break;
	    }

	    cv::Rect r1(cand.x, cand.y, width1, cand.height);
	    bbox_shrink_vert(img, r1);
	    symbol1.bbox_assign(r1, test_only);

	    cv::Rect r2(cand.x + width1, cand.y, width2, cand.height);
	    bbox_shrink_vert(img, r2);
	    symbol2.bbox_assign(r2, test_only);

	    // skip next symbol
	    sym_i++;
	    continue;
	  } else {
	    // no statistics info, can't split
	    sym_cands[sym1_i].clear();
	    sym_cands[sym2_i].clear();
	    ok = false;
	    break;
	  }
	}

	// assign all remaining candidate bboxes to the symbol
	if (sym_cands[sym_i].size()) {
	  std::vector<cv::Rect> bboxes;
	  for (std::size_t i = 0; i < sym_cands[sym_i].size(); i++) {
	    bboxes.emplace_back(cands[sym_cands[sym_i][i]]);
	  }
	  symbol1.bboxes_assign(bboxes, test_only);
	} else {
	  if (debug || subtitle_number == debug_subtitle_number) {
	    cerr_log() << "possible bboxes too wide" <<
	      ": symbol " << (sym_i + 1) << ": " << symbol1.utf8_symbol() <<
	      ": no candidates for symbol" << std::endl;
	  }
	  ok = false;
	  break;
	}
      }
    }
  }

  if (ok) {
    if (debug || subtitle_number == debug_subtitle_number) {
      cerr_log() << "possible bboxes too wide: repaired" << std::endl;
    }
    return true;
  }

  // remove any assigned bboxes.
  bboxes_remove();
  if (debug || subtitle_number == debug_subtitle_number) {
    cerr_log() << "possible bboxes too wide: could not repair" << std::endl;
    dump(std::cerr);
    bboxes_stream(std::cerr, cands) << std::endl;
  }
  return false;
}

void
OCRWord::dump(std::ostream& os) const {
  os << "  word: ";
  write(os);
  os << ": ic: " << priv_italic_confidence <<
    ", " << symbol_vec.size() << " symbols: " <<
    std::endl;
  for (const auto& it : symbol_vec) {
    it.dump(os);
  }
}

void
OCRWord::symbol_bboxes_draw(
  const cv::Mat& img,
  const cv::Rect& line_bbox,
  unsigned char grayscale_color) const {
  for (std::size_t i = 0; i < symbol_vec.size(); i++) {
    symbol_vec[i].bboxes_draw(img, line_bbox, grayscale_color);
  }
}

