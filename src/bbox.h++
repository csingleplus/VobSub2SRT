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

#include <opencv2/core.hpp>

#include <vector>
#include <ostream>

// Functionality for bounding boxes of text lines, text words and text symbols.

// image should be white text on black background
void
bboxes_detect(
    const cv::Mat& img,
    const cv::Rect& line_bbox,
    std::vector<cv::Rect>& bbox_vec);

// image should be black text on white background
void
bboxes_invert_and_detect(
    const cv::Mat& img,
    const cv::Rect& line_bbox,
    std::vector<cv::Rect>& bbox_vec);

bool
bbox_sort_compare(
    const cv::Rect& a,
    const cv::Rect& b);

void
bboxes_sort(
  std::vector<cv::Rect>& bbox_vec);

void
bboxes_sort_and_combine(
    std::vector<cv::Rect>& bbox_vec);

cv::Mat
bboxes_draw(
    const cv::Mat& img,
    const std::vector<cv::Rect>& bbox_vec);

cv::Mat
bboxes_draw(
  const cv::Mat& img,
  const cv::Rect& img_bbox,
  const std::vector<cv::Rect>& bbox_vec);

void
bbox_stream(
    std::ostream& os,
    const cv::Rect& bbox);

std::ostream&
bboxes_stream(
    std::ostream& os,
    const std::vector<cv::Rect>& bbox_vec);

// src : Bboxes, ordered by x coordinate, ascending.
// dst : indices of bboxes that overlap the previous bbox.
void
bboxes_get_overlapping(
  const std::vector<cv::Rect>& src,
  std::vector<std::size_t>& dst);

bool
bbox_is_column_white(
    const cv::Mat& img,
    const cv::Rect& bbox,
    const int rel_x);

int
bbox_max_seq_white_columns(
    const cv::Mat& img,
    const cv::Rect& bbox);

// returns:
// 0..bbox.width-1  position relative to bbox.x of leftmost black pixel at
//                  bbox.y (top row of bbox).
// -1               if bbox.width <= 0; or bbox.height <= 0, or if there is no
//                  black pixel in the top row. All of these mean the bbox is
//                  invalid.
int
bbox_top_row_left_pixel_pos(
    const cv::Mat& img,
    const cv::Rect& bbox);

// returns:
// 0..bbox.width-1  position relative to bbox.x of leftmost black pixel at
//                  bbox.y (top row of bbox).
// -1               if bbox.width <= 0; or bbox.height <= 0, or if there is no
//                  black pixel in the top row. All of these mean the bbox is
//                  invalid.
int
bbox_bottom_row_left_pixel_pos(
    const cv::Mat& img,
    const cv::Rect& bbox);

void
bbox_shrink_vert_top(
    const cv::Mat& img,
    cv::Rect& bbox);

void
bbox_shrink_vert_bottom(
    const cv::Mat& img,
    cv::Rect& bbox);

void
bbox_shrink_vert(
    const cv::Mat& img,
    cv::Rect& bbox);

// image must be white text on black background
// text_line_bbox_vec is not cleared, just appended to
void
get_text_line_bboxes(
    const cv::Mat& img,
    std::vector<cv::Rect>& text_line_bbox_vec);

