/*
 *  This file is part of vobsub2srt.
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

#include <string>
#include <iostream>
#include <vector>

#include "bbox.h++"
#include "stats.h++"

#define CONFIDENT_ITALIC(c)     (c > ((float) 10.0))
#define CONFIDENT_NOT_ITALIC(c) (c < ((float)  4.0))
#define DEFAULT_CONFIDENCE      ((float) 7.0)

#ifndef OCRSYMBOL_HXX
#define OCRSYMBOL_HXX

class OCRSymbol {

public:
  OCRSymbol(
      const char* const utf8_symbol)
      : priv_utf8_symbol(),
	priv_bboxes(),
	priv_italic_confidence(DEFAULT_CONFIDENCE) {

    if (utf8_symbol == NULL) {
      std::cerr << "NULL symbol" << std::endl;
      throw std::invalid_argument("OCRSymbol(): NULL symbol");
    }

    const std::size_t len = std::strlen(utf8_symbol);
    if (len > 6) {
      throw std::invalid_argument("OCRSymbol(): invalid UTF-8 symbol");
    }

    priv_utf8_symbol = std::string(utf8_symbol);
}

  float
  italic_confidence() const {
    return priv_italic_confidence;
  }

  const std::string &
  utf8_symbol() const {
    return priv_utf8_symbol;
  }

  const std::vector<cv::Rect> &
  bboxes() const {
    return priv_bboxes;
  }

  bool
  is_one_of(
      const char* const set) const;

  void
  build_stats(
      const cv::Mat& img,
      TextStats& stats) const;

  void
  bbox_assign(
      const cv::Rect& bbox,
      bool test_only);

  void
  bboxes_assign(
      const std::vector<cv::Rect>& bboxes,
      bool test_only);

  void
  bboxes_remove();

  // Algorithm:
  // - take at position A of first black pixel in top row of bbox,
  // - take at position B of first black pixel in bottom row of bbox,
  // - take rel_dist C = A - B.
  // - store statistics for each symbol and calculate average_rel_dist value D for a symbol
  // - take C - D. if > 0, likely italic.
  // 
  // This works as long as italic is used sparsely, otherwise the statistics will be off.
  // If italic is used too frequently:
  // - a histogram could be used per symbol instead, with occurance counts per rel_dist value.
  // - there should be peaks in the histogram for two rel_dist values: one not italic,
  //   the larger one italic.
  // - not implementing that for now.
  //
  // Also:
  // - don't calculate italic confidence for:
  //   - some punctuation symbols that don't give reliable results
  //   - a symbol with a likely invalid bbox for the symbol, based on the
  //     statistics (could be a symbol mis-identified by OCR).
  // Special cases:
  //   - symbols without a bbox TODO
  //   - symbols with multiple bboxes TODO
  //
  // TODO also assign and store word bboxes for words that don't have good symbol bboxes, and:
  //      - look at rel_dist of first symbol,
  //      - possibly also at pixels of last symbol, but would need right-side rel_dist statistic
  // TODO rename 'pos' to something more correct, like rel_dist
  //
  void
  assign_confidence(
      const cv::Mat& img,
      const TextStats& stats);

  void write_srt(
    std::ostream& os) const {
    os << priv_utf8_symbol;
  }

  void
  dump(std::ostream& os) const;

  void
  bboxes_draw(
    const cv::Mat& img,
    const cv::Rect& line_bbox,
    unsigned char grayscale_color) const;

private:
  std::string priv_utf8_symbol;

  // Potential bounding boxes for the word.
  // If assigned, such a bbox has been determined based on:
  // - ocr_bbox of this symbol
  // - bboxes determined with opencv contour detection
  // - ocr_bbox of the OCRWord this OCRSymbol is part of
  // It's not always possible to determine which bounding box is the correct
  // one for a symbol. In that case, all candidates are in the vector.
  std::vector<cv::Rect> priv_bboxes;

  // negative value means probably not italic
  // positive value means probably italic
  // larger negative / positive value means more confidence
  float priv_italic_confidence;
};

#endif // OCRSYMBOL_HXX

