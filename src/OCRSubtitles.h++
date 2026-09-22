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

#include "OCRSubtitle.h++"

#include <tesseract/baseapi.h>

#ifndef OCRSUBTITLES_HXX
#define OCRSUBTITLES_HXX

class OCRSubtitles {

private:
  class SubtitleInfo {

  public:
    SubtitleInfo(
	const std::size_t subtitle_number,
	const uint32_t start_pts,
	const uint32_t end_pts,
	const unsigned char* const sp_image,
	const unsigned sp_width,
	const unsigned sp_height,
	const unsigned sp_stride);
#if 0
    {
      init(
	subtitle_number,
	start_pts,
	end_pts,
	sp_image,
	sp_width,
	sp_height,
	sp_stride);
    }
#endif

    std::size_t subtitle_number;
    uint32_t start_pts;
    uint32_t end_pts;

    // original image after conversion to white text on black background
    cv::Mat bw_img;

    std::vector<cv::Rect> bw_line_bbox_vec;
    std::vector<cv::Rect> combined_line_bbox_vec;
  };

public:
  OCRSubtitles(
      const std::string &subname);

  bool
  append(
      const std::size_t subtitle_number,
      const uint32_t start_pts,
      const uint32_t end_pts,
      const unsigned char* const sp_image,
      const unsigned sp_width,
      const unsigned sp_height,
      const unsigned sp_stride);

  void
  do_ocr(
      tesseract::TessBaseAPI& tess_base_api,
      const std::size_t ocr_batch_size);

  void
  detect_italic();

  void
  write_srt(
      std::ostream& os,
      const int base_duration,
      const int chars_per_sec,
      const bool show);

private:
  void
  bboxes_assign(
      TextStats * const stats);

  void
  build_stats(
      TextStats& stats) const;

  void
  bboxes_remove();

  void
  batch_ocr(
      tesseract::TessBaseAPI& tess_base_api,
      const cv::Mat& combined_img,
      const std::size_t batch_i,
      const std::size_t batch_end_i);

private:
  std::string subname;

  std::vector<SubtitleInfo> subtitle_info_vec;

  std::vector<OCRSubtitle> subtitle_vec;
};

#endif // OCRSUBTITLES_HXX

