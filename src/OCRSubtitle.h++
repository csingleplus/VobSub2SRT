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

#include "OCRLine.h++"

#ifndef OCRSUBTITLE_HXX
#define OCRSUBTITLE_HXX

class OCRSubtitle {

public:
  OCRSubtitle(
      const std::size_t subtitle_number,
      const uint32_t start_pts,
      const uint32_t end_pts,
      const cv::Mat& img,
      std::vector<OCRLine>& line_vec_arg)
      : subtitle_number(subtitle_number),
	start_pts(start_pts),
	end_pts(end_pts),
	img(img),
	line_vec() {
    line_vec.swap(line_vec_arg);
  }

  uint32_t
  start_pts_get() const {
    return start_pts;
  }

  void bboxes_assign(
    const std::string& subname,
    const TextStats* const stats);

  void
  build_stats(
      TextStats& stats) const;

  void bboxes_remove();

  std::size_t detect_italic(
      const TextStats& stats);

  void
  write_srt(
      std::ostream& os,
      const int base_duration,
      const int chars_per_sec,
      const bool show,
      const std::optional<uint32_t> next_start_pts_opt);

private:
  std::size_t
  num_chars_for_duration() const;

private:
  std::size_t subtitle_number;

  uint32_t start_pts;

  uint32_t end_pts;

  cv::Mat img;

  std::vector<OCRLine> line_vec;
};

#endif // OCRSUBTITLE_HXX


