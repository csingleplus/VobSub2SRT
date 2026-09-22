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

#include "OCRSymbol.h++"

#include <stdexcept>

#include <opencv2/imgproc.hpp>

#include "generic_exception.h++"

bool
OCRSymbol::is_one_of(
    const char* const set) const {
  // TODO if necessary make compatible with UTF-8
  return (std::strchr(set, priv_utf8_symbol[0]) != NULL);
}

void
OCRSymbol::build_stats(
    const cv::Mat& img,
    TextStats& stats) const {
  if (priv_bboxes.size() == 1) {
    const cv::Rect& bbox = priv_bboxes[0];
    stats.symbol_add(
	priv_utf8_symbol,
	bbox,
	bbox_top_row_left_pixel_pos(img, bbox) -
	bbox_bottom_row_left_pixel_pos(img, bbox));
  }
}

void
OCRSymbol::bbox_assign(
    const cv::Rect& bbox,
    bool test_only) {
  if (priv_bboxes.size()) {
    throw generic_exception("OCRSymbol::bbox_assign: bbox already assigned");
  }
  if (test_only) {
    return;
  }
  priv_bboxes.emplace_back(bbox);
}

void
OCRSymbol::bboxes_assign(
    const std::vector<cv::Rect>& bboxes,
    bool test_only) {
  if (priv_bboxes.size()) {
    throw generic_exception("OCRSymbol::bboxes_assign: bbox already assigned");
  }
  if (test_only) {
    return;
  }
  priv_bboxes = bboxes;
}

void
OCRSymbol::bboxes_remove() {
  priv_bboxes.clear();
}

void
OCRSymbol::assign_confidence(
    const cv::Mat& img,
    const TextStats& stats) {

  // punctuation
  if (priv_utf8_symbol[0] == '-') {
    // occurs as a word at the begin of a line
    priv_italic_confidence = -1;
    return;
  }
  if (strchr(".,'\"", priv_utf8_symbol[0])) {
    // ignore, inconclusive results
    return;
  }

  if (priv_bboxes.size() == 1) {
    cv::Rect &bbox = priv_bboxes[0];

    // TODO see if we can base the constants -1.8 on something (bbox width?)

    // Symbols mis-identified by OCR should not be assigned a confidence value.
    const std::optional<float> avg_width_opt = stats.symbol_width_avg(priv_utf8_symbol);
    if (!avg_width_opt.has_value()) {
      // No info, do not assign confidence value.
      return;
    }
    if (bbox.width < (avg_width_opt.value() - 1.8)) {
      // OCR possibly misidentified the symbol.
      return;
    }

    const std::optional<int> min_height_opt = stats.symbol_height_min(priv_utf8_symbol);
    if (!min_height_opt.has_value()) {
      // No info, do not assign confidence value.
      return;
    }
    if (bbox.height < (min_height_opt.value())) {
      // OCR possibly misidentified the symbol.
      return;
    }

    const std::optional<float> avg_pos_opt = stats.symbol_pos_avg(priv_utf8_symbol);
    if (!avg_pos_opt.has_value()) {
      // No info, do not assign confidence value.
      return;
    }
    const int top_pos = bbox_top_row_left_pixel_pos(img, bbox);
    const int bottom_pos = bbox_bottom_row_left_pixel_pos(img, bbox);
    if (top_pos == -1 || bottom_pos == -1) {
      // bad bbox, possibly a split combined one.
      return;
    }
    const int pos = top_pos - bottom_pos;

    if (pos > avg_pos_opt.value() + 0.05) {
      priv_italic_confidence = (1 + pos - avg_pos_opt.value()) * bbox.height;
    }
    else {
      priv_italic_confidence = -bbox.height;
    }
  }
  else if (priv_bboxes.size() == 0) {
    priv_italic_confidence = 11;
  }
  else if (priv_bboxes.size() > 1) {
    priv_italic_confidence = 5;
  }
}

void
OCRSymbol::dump(
    std::ostream& os) const {
  os << "    utf8_symbol: " << priv_utf8_symbol;
  for (std::size_t i = 0; i < priv_bboxes.size(); i++) {
    const cv::Rect& bbox = priv_bboxes[i];
    os << ", bbox " << (i + 1) << ": (" <<
      bbox.x << "," <<
      bbox.y << ")-(" <<
      (bbox.x + bbox.width) << "," <<
      (bbox.y + bbox.height) << ")";
  }
  os << ", ic: " << priv_italic_confidence <<
    std::endl;
}

void
OCRSymbol::bboxes_draw(
  const cv::Mat& img,
  const cv::Rect& line_bbox,
  unsigned char grayscale_color) const {

  for (std::size_t i = 0; i < priv_bboxes.size(); i++) {
    const cv::Rect& bbox = priv_bboxes[i];
    cv::rectangle(
	img,
	cv::Rect(
	  bbox.x - line_bbox.x,
	  bbox.y - line_bbox.y,
	  bbox.width,
	  bbox.height),
	cv::Scalar(grayscale_color),
	1);
  }
}

