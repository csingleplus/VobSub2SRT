/*
 *  This file is part of vobsub2srt.
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

#include <string>
#include <iostream>
#include <vector>

#include "bbox.h++"
#include "stats.h++"

// With the current algorithm, an italic_confidence value > 10.0 indicates that
// the symbol or word is very likely italic.
#define CONFIDENT_ITALIC(c)     (c > ((float) 10.0))

// With the current algorithm, an italic_confidence value < 4.0 indicates that
// the symbol or word is very likely not italic.
#define CONFIDENT_NOT_ITALIC(c) (c < ((float)  4.0))

// With the current algorithm, an italic_confidence value between 4.0 and 10.0
// indicates that the algorithm can't really tell if the symbol or word is
// italic or not, or has been unable to assign a value because there was no
// bounding box or some other issue.
// All words and symbols start with value 7.0, in the middle of the range
// where the algorithm is not confident either way.
#define DEFAULT_CONFIDENCE      ((float) 7.0)

#ifndef OCRSYMBOL_HXX
#define OCRSYMBOL_HXX

class OCRSymbol {

public:
  OCRSymbol(
      const char* const utf8_symbol);

  OCRSymbol(
      const std::string& utf8_symbol);

  // Call this method after method assign_confidence.
  // If called before assign_confidence, it returns DEFAULT_CONFIDENCE.
  //
  // See CONFIDENT_* and DEFAULT_CONFIDENCE macros for the interpretation of
  // the returned value.
  float
  italic_confidence() const {
    return priv_italic_confidence;
  }

  // Get the UTF8 symbol returned by OCR.
  const std::string &
  utf8_symbol() const {
    return priv_utf8_symbol;
  }

  // Call this method after method bbox_assign or bboxes_assign.
  // If called before bbox_assign or bboxes_assign, it returns
  // an empty vector.
  //
  // Get the bounding boxes for the symbol.
  //
  // A bounding box exactly bounds the symbol, meaning it has no whitespace at
  // the edges.  The pixels at x and y of the rectangle are included. The
  // pixels at (x + width - 1) and (y + height - 1) are included as well.  The
  // pixels at (x + width) and (y + height) are outside the bounding box and
  // not part of the symbol.
  //
  // The returned bounding box is not necessarily the same as the one
  // originally returned by OCR.  The bounding boxes returned by Tesseract are
  // unreliable or even assigned to the wrong symbol.  The algorithm that has
  // assigned the returned bounding box does its best to detect invalid bounding
  // boxes. If possible it replaces them by valid bounding boxes determined
  // with OpenCV's contour detection algorithm.
  //
  // About the returned vector:
  //
  // - In most cases, there is only one bounding box in the returned vector.
  //   If so, it is usually reliable.
  //
  // - If there are no bounding boxes in the returned vector, this usually
  //   means that OCR has interpreted spacing between symbols and words
  //   differently than the algorithm that assigns bounding boxes.
  //
  //   Sometimes this indicates that OCR has mis-identified some symbols.
  //   Sometimes the spacing between symbols and/or words is too hard to
  //   interpret for the algorithm that assigns bounding boxes.
  //
  // - If there are multiple bounding boxes in the returned vector, this can
  //   be caused by symbols of which the pixels touch, causing bounding boxes
  //   that are too large and bound multiple symbols.
  //
  //   This sometimes makes it hard to determine which bounding box is for
  //   which symbol in a word.  The algorithm tries its best to determine
  //   anyway, and can split such bounding boxes and repair the situation.
  //   It just is not successful in every case, sometimes leaving multiple
  //   options open.
  //
  //   Having multiple bounding boxes is not useful for italic detection, but
  //   is very useful for debugging and improving the algorithm that assigns
  //   them.
  const std::vector<cv::Rect> &
  bboxes() const {
    return priv_bboxes;
  }

  // Check if the first char of the symbol is in the set of specified
  // characters.
  //
  // For example: is_one_of(".,") returns true if the symbol is a '.' or a
  // ',', false if not.
  //
  // Does not support multi-byte UTF-8 characters (yet), but could be adapted
  // if required.
  bool
  is_one_of(
      const char* const set) const;

  // Build statistics for:
  // - better assignment of bounding boxes to symbols in cases where the
  //   bounding boxes returned by OCR are clearly invalid.
  // - italic detection.
  //
  // Statistics should be reliable.  If there's any doubt about the validity of
  // the information that build_stats would add, build_stats should not add it.
  void
  build_stats(
      const cv::Mat& img,
      TextStats& stats) const;

  // Assign a bounding box to the symbol.
  // Does nothing if test_only is true.
  // Throws a generic_exception if one or more bounding boxes have already been
  // assigned.
  void
  bbox_assign(
      const cv::Rect& bbox,
      bool test_only);

  // Assign bounding boxes to the symbol.
  // Does nothing if test_only is true.
  // Throws a generic_exception if one or more bounding boxes have already been
  // assigned.
  void
  bboxes_assign(
      const std::vector<cv::Rect>& bboxes,
      bool test_only);

  // Remove all assigned bounding boxes.
  //
  // The algorithm to determine bounding boxes has multiple phases:
  // 1. Only assign bounding boxes that are very likely correct to symbols.
  //    Don't assign a bounding box if there is any reason to doubt it.
  // 2. Build statistics about word spacing, symbol spacing, symbol bounding
  //    boxes and and ohter symbol properties.
  // 3. Remove all assigned bounding boxes with this method.
  // 4. Assign bounding boxes as best as possible to all symbols based on:
  //    - OCR symbol bounding boxes
  //    - OCR word bounding boxes
  //    - bounding boxes determined with OpenCV contour detection,
  //    - the collected statistics.
  void
  bboxes_remove();

  // Algorithm:
  // - Take at position A of first black pixel in top row of bbox.
  // - Take at position B of first black pixel in bottom row of bbox.
  // - Take rel_dist C = A - B.
  // - Method build_stats has already collected C for each symbol with a
  //   reliable bounding box.  The resulting TextStats have information about
  //   the minimum, average and maximum C value for each distinct symbol.
  // - Retrieve the average rel_dist value D for a symbol from the TextStats.
  // - Take E = C - D. if > 0, likely italic. If not, likely not italic.
  //   The greater the difference, the more confident (up to a point, then
  //   it's likely unreliable information such as a mis-identified symbol,
  //   or an unreliable bounding box).
  // 
  // This works as long as the same font is used throughout the subtitles, and
  // italic is used sparsely, otherwise the statistics will be off.
  // If italic is used too frequently, the algorithm needs to be adapted:
  // - a histogram could be used per symbol instead, with occurance counts per
  //   rel_dist value.
  // - there should be peaks in the histogram for two rel_dist values: one not
  //   italic, the larger one italic.
  // - not implementing that for now.
  //
  // Also:
  // - don't calculate italic confidence for:
  //   - some punctuation symbols that don't give reliable results
  //   - a symbol with a likely invalid bbox for the symbol, based on the
  //     statistics (could be a symbol mis-identified by OCR).
  //   - symbols without a bbox.
  //   - symbols with multiple bboxes
  //
  // TODO assign and store word bboxes for words that don't have good symbol
  // bboxes, and:
  //      - look at rel_dist of first symbol,
  //      - possibly also at pixels of last symbol, but would need right-side
  //        rel_dist statistic
  //
  void
  assign_confidence(
      const cv::Mat& img,
      const TextStats& stats);

  // Write the symbol to the output stream for the SRT file.
  void write(
    std::ostream& os) const {
    os << priv_utf8_symbol;
  }

  // Write the symbol to the output stream for the SRT file.
  void write_srt(
    std::ostream& os) const {
    os << priv_utf8_symbol;
  }

  // Dump debug information to the specified output stream.
  void
  dump(
      std::ostream& os) const;

  // Draw all bounding boxes as rectangles on the image.
  //
  // The provided image can be a subsection of the original image indicated by
  // line_bbox. If so, the coordinates of the drawn bboxes will be adjusted by
  // -line_bbox.x and -line_bbox.y pixels.
  void
  bboxes_draw(
    const cv::Mat& img,
    const cv::Rect& line_bbox,
    unsigned char grayscale_color) const;

private:
  // The UTF-8 symbol returned by OCR.
  std::string priv_utf8_symbol;

  // Potential bounding boxes for the word.
  // If assigned, such a bbox has been determined based on:
  // - ocr_bbox of this symbol
  // - bboxes determined with opencv contour detection
  // - ocr_bbox of the OCRWord this OCRSymbol is part of
  // It's not always possible to determine which bounding box is the correct
  // one for a symbol. In that case, all candidates are in the vector.
  std::vector<cv::Rect> priv_bboxes;

  // See CONFIDENT_* and DEFAULT_CONFIDENCE macros for the interpretation of
  // the italic confidence value.
  float priv_italic_confidence;
};

#endif // OCRSYMBOL_HXX

