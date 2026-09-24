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

#include "OCRWord.h++"

#ifndef OCRLine_HXX
#define OCRLine_HXX

#define NUM_BORDER_PIXELS 10

class OCRLine {

public:
  OCRLine(
      const std::size_t subtitle_number,
      const std::size_t line_number,
      std::vector<OCRWord>& word_vec_arg,
      const cv::Rect& bbox,
      std::vector<cv::Rect>& word_ocr_bbox_vec_arg,
      std::vector<cv::Rect>& symbol_ocr_bbox_vec_arg);

  cv::Rect
  bbox() const {
    return priv_bbox;
  }

  // Returns the number of symbols, not including whitespace.
  std::size_t
  num_ocr_symbols() const;

  std::ostream&
  write(
      std::ostream& os) const;

  void
  read(
      std::istream& is);

  bool
  bboxes_assign(
      const std::string & subname,
      const cv::Mat& img,
      const std::vector<cv::Rect>& symbol_contour_bbox_vec,
      const TextStats* const stats);

  void
  build_stats(
      const cv::Mat& img,
      TextStats& stats) const;

  void
  bboxes_remove();

  void
  assign_confidence(
      const cv::Mat& img,
      const TextStats& stats);

  void
  propagate_word_confidence();

  void
  write_srt(
      std::ostream& os) const;

  // Returns the number of symbols, including whitespace:
  // spaces between words and newlines in between lines.
  // It does not count a newline at the end.
  std::size_t
  num_chars_for_duration() const;

  void dump(
     std::ostream& os) const;

private:
  std::ostream&
  cerr_log() const;

  cv::Rect
  bordered_bbox() const;

  bool bbox_is_invalid(
      const cv::Mat& img,
      const cv::Rect& bbox,
      const bool check_all_columns);

  // dst will be cleared first.
  void symbol_bboxes_remove_invalid(
      const cv::Mat &img,
      const std::vector<cv::Rect>& src,
      std::vector<cv::Rect>& dst);

  // dst will be cleared first.
  void symbol_bboxes_fill_gaps(
      const std::vector<cv::Rect>& src,
      const std::vector<cv::Rect>& src2,
      std::vector<cv::Rect>& dst);

  // dst will be cleared first.
  void symbol_bboxes_replace_combined(
      const std::vector<cv::Rect>& src,
      const std::vector<cv::Rect>& src2,
      std::vector<cv::Rect>& dst);

  // Not 100% improvement, there may be edge cases, but also does little
  // harm in the edge cases (symbols will still all be covered).
  // dst will be cleared first.
  void symbol_bboxes_remove_inaccurate_overlapping(
      const std::vector<cv::Rect>& src,
      std::vector<cv::Rect>& dst);

  // Not 100% improvement, there may be edge cases, but also does little
  // harm in the edge cases (symbols will still all be covered).
  // dst will be cleared first.
  void symbol_bboxes_remove_overlapped_by_1(
      const std::vector<cv::Rect>& src,
      std::vector<cv::Rect>& dst);

  // The goal of this method is to improve symbol bboxes without introducing
  // new issues. So a replacement must be:
  // - either 100% logically deducted correct,
  // - or nearly 100% accurate and not harmful in edge cases.
  // src and dst can be the same vector without conflict
  void symbol_bboxes_improve(
      const std::string & subname,
      const cv::Mat& img,
      const std::vector<cv::Rect>& src, // ocr bboxes
      const std::vector<cv::Rect>& src2, // contour bboxes
      std::vector<cv::Rect>& dst);

  // dst will be cleared first.
  void word_bboxes_remove_overlapping(
      const std::vector<cv::Rect>& src,
      std::vector<cv::Rect>& dst);

  // dst will be cleared first.
  void word_bboxes_remove_invalid(
      const cv::Mat& img,
      const std::vector<cv::Rect>& src,
      std::vector<cv::Rect>& dst,
      const TextStats * const stats);

  // dst will be cleared first.
  void word_bboxes_remove_too_much_spacing(
      const cv::Mat& img,
      const std::vector<cv::Rect>& src,
      std::vector<cv::Rect>& dst,
      const TextStats& stats);

  void word_bboxes_improve(
      const std::string &subname,
      const cv::Mat& img,
      const std::vector<cv::Rect>& src, // word ocr bboxes
      const std::vector<cv::Rect>& symbol_src, // improved symbol bboxes
      std::vector<cv::Rect>& dst,
      const TextStats* const stats);

  bool word_bboxes_bound_all_symbol_bboxes(
      const std::vector<cv::Rect>& src, // word bboxes
      const std::vector<cv::Rect>& src2); // symbol bboxes

  void word_bboxes_fill_gaps_and_combine_based_on_spacing(
      const std::vector<cv::Rect>& src,
      const std::vector<cv::Rect>& src2,
      std::vector<cv::Rect>& dst,
      const int min_word_spacing);

  // word: Word bbox.
  // src : Symbol bboxes.
  //       Ordered by x coordinate, ascending, and width, ascending.
  //       Symbol bboxes are allowed to overlap each other.
  // dst : Cleard, then filled with all symbol bboxes from src that are
  //       completely within word bbox.
  //       Ordered by x coordinate, ascending, and width, ascending.
  void
  bboxes_get_word_candidates(
    const cv::Rect word,
    const std::vector<cv::Rect>& src,
    std::vector<cv::Rect>& dst);

  cv::Mat
  word_symbol_bboxes_draw(
    std::size_t const word_index,
    const cv::Mat& img,
    unsigned char grayscale_color) const;

  cv::Mat
  symbol_bboxes_draw(
    const cv::Mat& img,
    unsigned char grayscale_color) const;

private:
  std::size_t subtitle_number;
  std::size_t line_number;

  std::vector<OCRWord> word_vec;

  // Bounding box for the line. Reliable.
  cv::Rect priv_bbox;

  // Bounding boxes for the words as returned by OCR.
  // These are not reliable and need to be compared to opencv contour bboxes
  // before assigning bboxes to ocr_words and ocr_symbols.
  // Used to help determine accurate bboxes for symbols.
  // Sorted by tesseract by cv::Rect.x coordinate, ascending.
  std::vector<cv::Rect> word_ocr_bbox_vec;

  // Bounding boxes for the symbols as returned by OCR.
  // - With Tesseract's LSTM engine, this is correct to the pixel for most
  //   symbols, but can be wildly inaccurate for some symbols.
  // Used to help determine accurate bboxes for symbols.
  // Sorted by tesseract by cv::Rect.x coordinate, ascending.
  std::vector<cv::Rect> symbol_ocr_bbox_vec;

  // improved word bboxes
  std::vector<cv::Rect> nwv;

  // improved symbol bboxes
  std::vector<cv::Rect> nsv;
};

#endif // OCRLine_HXX

