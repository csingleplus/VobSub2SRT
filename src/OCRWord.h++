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

#include <ostream>
#include <vector>

#include <opencv2/core.hpp>

#include "bbox.h++"
#include "stats.h++"
#include "OCRSymbol.h++"

#ifndef OCR_WORD_HXX
#define OCR_WORD_HXX

// An OCRword is a sequence of symbols not separated by whitespace, as produced
// by OCR. It can include punctuation. It can in practice include more than one
// word, for example OCRword [he's] includes punctuation and two words.
// Other examples: ["hello], [dear"], [out!], [will,], [two...], [..three].
class OCRWord {

public:
  OCRWord(
      const std::size_t subtitle_number,
      const std::size_t line_number,
      const std::size_t word_number,
      std::vector<OCRSymbol>&& symbol_vec)
      : subtitle_number(subtitle_number),
        line_number(line_number),
	word_number(word_number),
	symbol_vec(std::move(symbol_vec)),
        priv_italic_confidence(DEFAULT_CONFIDENCE) {
  }
      
  std::size_t
  num_ocr_symbols() const {
    return symbol_vec.size();
  }

  float
  italic_confidence() const {
    return priv_italic_confidence;
  }

  bool
  is_minus() const {
    return (symbol_vec.size() == 1) && (symbol_vec[0].utf8_symbol()[0] == '-');
  }

  bool
  bboxes_assign(
      const cv::Mat& img,
      const std::vector<cv::Rect>& src,
      const TextStats* const stats,
      const bool test_only);

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

  std::ostream&
  write(
      std::ostream& os) const;

  void
  derive_confidence();

  bool
  derive_confidence(
      const float left_confidence,
      const float right_confidence);

  // only words are italic, not punctuation.
  std::ostream&
  write_srt_punct_at_begin(
      std::ostream& os) const;

  // only words are italic, not punctuation.
  std::ostream&
  write_srt_between_punct(
      std::ostream& os) const;

  // only words are italic, not punctuation.
  std::ostream&
  write_srt_punct_at_end(
      std::ostream& os) const;

  void
  dump(
      std::ostream& os) const;

  void
  symbol_bboxes_draw(
    const cv::Mat& img,
    const cv::Rect& line_bbox,
    unsigned char grayscale_color) const;

private:
  bool
  has_symbol_of(
      const char* const set);

  std::size_t
  num_punct_at_begin() const;

  std::size_t
  num_punct_at_end() const;

  bool
  bboxes_assign(
      const std::vector<cv::Rect>& src,
      const TextStats* stats,
      const bool test_only);

  bool
  bboxes_assign_repair_too_few_bboxes(
      const cv::Mat& img,
      const std::vector<cv::Rect>& src,
      const TextStats& stats,
      const bool test_only);

private:
  // The number of the subtitle in the OCRSubtitles, starts at 1.
  std::size_t subtitle_number;

  // The number of the line in the OCRSubtitle, starts at 1.
  std::size_t line_number;

  // The number of the word in the OCRLine, starts at 1.
  std::size_t word_number;

  // The symbols of the OCRWord. 
  std::vector<OCRSymbol> symbol_vec;

  // value computed from italic_confidence values of symbols.
  // negative value means probably not italic
  // positive value means probably italic
  // larger negative / positive value means more confidence
  float priv_italic_confidence;
};

inline std::ostream&
operator<<(std::ostream& os, const OCRWord& word) {
  return word.write(os);
}

#endif // OCR_WORD_HXX

