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

#include "OCRSubtitle.h++"

#include <format>

#include "debug.h++"

static std::ostream &
write_pts(
  std::ostream& os,
  const unsigned pts) {
  return os << std::format("{:02}:{:02}:{:02},{:03}",
      pts / (90 * 1000 * 3600) /* hour */,
      (pts / (90 * 1000 * 60)) % 60 /* minutes */,
      (pts / (90 * 1000)) % 60 /* seconds */,
      (pts / 90) % 1000 /* mlliseconds */);
}

void
OCRSubtitle::bboxes_assign(
  const std::string & subname,
  const TextStats * const stats) {

  for (std::size_t i = 0; i < line_vec.size(); i++) {

    if (debug || subtitle_number == debug_subtitle_number) {
      std::cerr << "subtitle " << subtitle_number <<
       	", line " << (i + 1) <<
       	": bboxes_assign" << std::endl;
    }

    std::vector<cv::Rect> bbox_vec;
    // Tesseract OCR works best with black text on a white background.
    // Contour detection works best with white text on a black background:
    // then the contours are on the inside of the symbols, resulting in
    // accurate bounding boxes.
    bboxes_invert_and_detect(img, line_vec[i].bbox(), bbox_vec);
    bboxes_sort_and_combine(bbox_vec);

    line_vec[i].bboxes_assign(
      subname,
      img,
      bbox_vec,
      stats);
  }
}

void
OCRSubtitle::build_stats(
    TextStats& stats) const {
  for (auto& it : line_vec) {
    it.build_stats(img, stats);
  }
}

void
OCRSubtitle::bboxes_remove() {
  for (auto& it : line_vec) {
    it.bboxes_remove();
  }
}

std::size_t
OCRSubtitle::detect_italic(
  const TextStats& stats) {

  std::size_t line_fail_count = 0;

  for (std::size_t tl_i = 0; tl_i < line_vec.size(); tl_i++) {
    OCRLine &line = line_vec[tl_i];

    line.assign_confidence(img, stats);

    line.propagate_word_confidence();

    if (debug || subtitle_number == debug_subtitle_number) {
      std::cerr << "subtitle " << subtitle_number << ", line " << (tl_i + 1) << ": " << std::endl;
      line.dump(std::cerr);
    }
  }

  return line_fail_count;
}

void
OCRSubtitle::write_srt(
    std::ostream& os,
    const int base_duration,
    const int chars_per_sec,
    const bool show,
    const std::optional<uint32_t> next_start_pts_opt) {

  uint32_t adjusted_end_pts = end_pts;

  // fix end_pts if necessary
  if (end_pts == UINT_MAX && next_start_pts_opt.has_value()) {
    adjusted_end_pts = next_start_pts_opt.value();
  }

  if (base_duration > 0) {
    // 90 = 1 msec, 90000 = 1 sec
    const uint32_t calc =
      (90 * base_duration) +
      ((90000 * num_chars_for_duration()) / chars_per_sec);

    adjusted_end_pts = std::min(adjusted_end_pts, start_pts + calc);
  }

  os << subtitle_number << std::endl;

  write_pts(os, start_pts) << " --> ";
  write_pts(os, end_pts) << std::endl;

  for (const auto & it : line_vec) {
    if (show) {
      std::cout << "Subtitle " << subtitle_number << ": ";
      it.write_srt(std::cout);
    }
    it.write_srt(os);
  }
  os << std::endl;
}

std::size_t
OCRSubtitle::num_chars_for_duration() const {
  std::size_t num = 0;
  for (std::size_t i = 0; i < line_vec.size(); i++) {
    num += line_vec[i].num_chars_for_duration();
  }
  return num - 1; // cut off extra newline
}

