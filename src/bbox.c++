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

#include "bbox.h++"

#include <opencv2/imgproc.hpp>

// image should be white text on black background
void
bboxes_detect(
    const cv::Mat& img,
    const cv::Rect& line_bbox,
    std::vector<cv::Rect>& bbox_vec) {

  std::vector<std::vector<cv::Point> > contour_vec;
  {
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(
	cv::Mat(img, cv::Range(line_bbox.y, line_bbox.y + line_bbox.height)),
	contour_vec,
	hierarchy,
	cv::RETR_EXTERNAL,
	cv::CHAIN_APPROX_SIMPLE);
  }

  for (std::size_t i = 0; i < contour_vec.size(); i++) {
    cv::Rect tl_rect = cv::boundingRect(contour_vec[i]);
    // convert back to coordinates in orig img
    tl_rect.y += line_bbox.y;
    bbox_vec.push_back(tl_rect);
  }
}

// image should be black text on white background
void
bboxes_invert_and_detect(
    const cv::Mat& img,
    const cv::Rect& line_bbox,
    std::vector<cv::Rect>& bbox_vec) {

  cv::Mat line_img(img, cv::Range(line_bbox.y, line_bbox.y + line_bbox.height));
  cv::Mat inverted_line_img; // new mat, so that bitwise_not makes a copy
  cv::bitwise_not(line_img, inverted_line_img);

  std::vector<std::vector<cv::Point> > contour_vec;
  {
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(
	inverted_line_img,
       	contour_vec,
       	hierarchy,
       	cv::RETR_EXTERNAL,
       	cv::CHAIN_APPROX_SIMPLE);
  }

  for (std::size_t i = 0; i < contour_vec.size(); i++) {
    cv::Rect tl_rect = cv::boundingRect(contour_vec[i]);
    // convert back to coordinates in orig img
    tl_rect.y += line_bbox.y;
    bbox_vec.push_back(tl_rect);
  }
}

bool
bbox_sort_compare(
    const cv::Rect& a,
    const cv::Rect& b) {
  return (a.x < b.x) || ((a.x == b.x) && (a.width < b.width));
}

void
bboxes_sort(
    std::vector<cv::Rect>& bbox_vec) {
  // Sort the bboxes by X coordinate, then width
  std::sort(bbox_vec.begin(), bbox_vec.end(), bbox_sort_compare);
}

void
bboxes_sort_and_combine(
    std::vector<cv::Rect>& bbox_vec) {

  if (bbox_vec.empty())
    return;

  bboxes_sort(bbox_vec);

  // Double quotes are detected as two separate bboxes,
  // They are in the top half of the height range.
  int min_y = bbox_vec[0].y;
  int max_y = bbox_vec[0].y + bbox_vec[0].height;
  for (std::size_t i = 1; i < bbox_vec.size(); i++) {
    cv::Rect& r = bbox_vec[i];
    if (r.y < min_y)
      min_y = r.y;
    if ((r.y + r.height) > max_y)
      max_y = r.y + r.height;
  }
  int double_quote_max_y = min_y + ((max_y - min_y) / 2);

  // Combine some of the bboxes:
  // - Overlapping x coordinates: ':', ';', '=', 'i', 'j', '!' and '?', etc.
  //   - Careful: the contour bboxes of italic symbols sometimes also overlap
  //     a few pixels. So we need a threshold:
  //     - Either the first bbox should 100% overlap the second, or the range
  //       of overlap should be greater than 50% for one of them.
  //       The 50% is a choice, may have to be adjusted.
  //     the amount
  // - '"'
  std::size_t pos = 0;
  for (std::size_t i = 1; i < bbox_vec.size(); i++) {
    cv::Rect& rect1 = bbox_vec[pos];
    const cv::Rect& rect2 = bbox_vec[i];

    if (rect2.x < (rect1.x + rect1.width)) {
      // amount of overlap: from rect2.x to min(rect1.x+rect1.width, rect2.x+rect2.width)
      int overlap = std::min(rect1.x + rect1.width, rect2.x + rect2.width) - rect2.x;

      if ((overlap == rect2.width) || ((2 * overlap) >= rect1.width) || ((2 * overlap) >= rect2.width)) {
	rect1 = rect1 | rect2; // Union = minimum covering rectangle
      }
      else {
	pos++;
	if (pos < i) {
	  bbox_vec[pos] = rect2;
	}
      }
    }
    else if (
	((rect1.y + rect1.height) < double_quote_max_y) &&
	((rect2.y + rect2.height) < double_quote_max_y)) {
      // assume '"' (double quote), merge.
      rect1 = rect1 | rect2; // Union = minimum covering rectangle
    } else {
      pos++;
      if (pos < i) {
	bbox_vec[pos] = rect2;
      }
    }
  }
  bbox_vec.resize(pos + 1);
}

cv::Mat
bboxes_draw(
    const cv::Mat& img,
    const std::vector<cv::Rect>& bbox_vec) {

    cv::Mat result_img(img.clone());

  for (std::size_t i = 0; i < bbox_vec.size(); i++) {
    const cv::Rect& rect = bbox_vec[i];
    cv::rectangle(result_img,
      cv::Point(rect.x, rect.y),
      cv::Point(rect.x + rect.width - 1, rect.y + rect.height - 1),
      cv::Scalar(128),
      1);
  }

  return result_img;
}

cv::Mat
bboxes_draw(
  const cv::Mat& img,
  const cv::Rect& img_bbox,
  const std::vector<cv::Rect>& bbox_vec) {

  cv::Mat result_img(img_bbox.height, img_bbox.width, img.type());
  cv::Mat(img, img_bbox).copyTo(result_img);

  for (std::size_t i = 0; i < bbox_vec.size(); i++) {
    const cv::Rect& rect = bbox_vec[i];
    cv::rectangle(result_img,
      cv::Point(
	rect.x - img_bbox.x,
       	rect.y - img_bbox.y),
      cv::Point(
	rect.x - img_bbox.x + rect.width - 1,
	rect.y - img_bbox.y + rect.height - 1),
      cv::Scalar(128),
      1);
  }

  return result_img;
}

void
bbox_stream(
    std::ostream& os,
    const cv::Rect& bbox) {
    os << "(" << bbox.x << "," << bbox.y << ")-(" << (bbox.x + bbox.width) << "," << (bbox.y + bbox.height) << ")";
}

std::ostream&
bboxes_stream(
    std::ostream& os,
    const std::vector<cv::Rect>& bbox_vec) {
  for (std::size_t i = 0; i < bbox_vec.size(); i++) {
    const cv::Rect& r = bbox_vec[i];
    if (i > 0)
      os << ", ";
    os << i << ":";
    bbox_stream(os, r);
  }
  return os;
}

void
bboxes_get_overlapping(
  const std::vector<cv::Rect>& src,
  std::vector<std::size_t>& dst) {

  dst.clear();

  for (std::size_t src_i = 1; src_i < src.size(); src_i++) {
    const cv::Rect& pr = src[src_i - 1];
    const cv::Rect& cr = src[src_i];

    // We need a threshold for italic chars, for which the bboxes often
    // overlap a few pixels.

    // size of overlap: from cr.x to min(pr.x+pr.width, cr.x+cr.width)
    const int overlap = std::min(pr.x + pr.width, cr.x + cr.width) - cr.x;

    // if overlap amounts to at least half of one of the bboxes
    if (((2 * overlap) >= pr.width) || ((2 * overlap) >= cr.width)) {
      dst.emplace_back(src_i);
    }
  }
}

bool
bbox_is_column_white(
    const cv::Mat& img,
    const cv::Rect& r,
    const int rel_x) {

  const int col = r.x + rel_x;
  const int row_end = r.y + r.height;
  for (int row = r.y; row < row_end; row++) {
    if (img.at<uchar>(row, col) != ((uchar) 255)) {
      return false;
    }
  }
  return true;
}

int
bbox_max_seq_white_columns(
    const cv::Mat& img,
    const cv::Rect& bbox) {

  int max_seq_white_columns = 0;
  for (int start_rel_x = 0; start_rel_x < bbox.width; ) {
    int rel_x = start_rel_x;
    for (; rel_x < bbox.width; rel_x++) {
      if (!bbox_is_column_white(img, bbox, rel_x)) {
	break;
      }
    }
    const int seq_white_columns = rel_x - start_rel_x;
    max_seq_white_columns = std::max(max_seq_white_columns, seq_white_columns);
    start_rel_x += seq_white_columns + 1;
  }

  return max_seq_white_columns;
}

int
bbox_top_row_left_pixel_pos(
    const cv::Mat& img,
    const cv::Rect& bbox)
{
  if (bbox.height > 0) {
    for (int i = 0; i < bbox.width; i++) {
      if (img.at<uchar>(bbox.y, bbox.x + i) == ((uchar) 0)) {
	return i;
      }
    }
  }
  return -1;
}

int
bbox_bottom_row_left_pixel_pos(
    const cv::Mat& img,
    const cv::Rect& bbox)
{
  if (bbox.height > 0) {
    const int bottom_row = bbox.y + bbox.height - 1;
    for (int i = 0; i < bbox.width; i++) {
      if (img.at<uchar>(bottom_row, bbox.x + i) == ((uchar) 0)) {
	return i;
      }
    }
  }
  return -1;
}

void
bbox_shrink_vert_top(
    const cv::Mat& img,
    cv::Rect& bbox) {

  if (bbox.height > 0) {
    for (int i = 0; i < bbox.height; i++) {
      for (int j = 0; j < bbox.width; j++) {
	if (img.at<uchar>(bbox.y + i, bbox.x + j) == ((uchar) 0)) {
	  bbox.y += i;
	  bbox.height -= i;
	  return;
	}
      }
    }
    bbox.y += bbox.height;
    bbox.height = 0;
  }
}

void
bbox_shrink_vert_bottom(
    const cv::Mat& img,
    cv::Rect& bbox) {

  if (bbox.height > 0) {
    for (int i = 0; i < bbox.height; i++) {
      const int row = bbox.y + bbox.height - 1 - i;
      for (int j = 0; j < bbox.width; j++) {
	if (img.at<uchar>(row, bbox.x + j) == ((uchar) 0)) {
	  bbox.height -= i;
	  return;
	}
      }
    }
  }
  bbox.height = 0;
}

void
bbox_shrink_vert(
    const cv::Mat& img,
    cv::Rect& bbox) {
  bbox_shrink_vert_bottom(img, bbox);
  bbox_shrink_vert_top(img, bbox);
}

void
get_text_line_bboxes(
    const cv::Mat& image,
    std::vector<cv::Rect>& text_line_bbox_vec) {

  std::vector<cv::Rect> tmp;

  for (int row = 0; row < image.rows; row++) {
    for (; row < image.rows; row++) {
      if (cv::hasNonZero(cv::Mat(image, cv::Range(row, row + 1)))) {
	break;
      }
    }
    if (row == image.rows) {
      break;
    }
    const int text_start_row = row;

    for (; row < image.rows; row++) {
      if (!cv::hasNonZero(cv::Mat(image, cv::Range(row, row + 1)))) {
	break;
      }
    }
    const int text_end_row = row;

    int col = 0;
    for (col = 0; col < image.cols; col++) {
      int y = text_start_row;
      for (; y < text_end_row; y++) {
	if (image.at<uchar>(y, col) != ((uchar) 0)) {
	  break;
	}
      }
      if (y != text_end_row) {
	break;
      }
    }
    const int text_start_col = col;

    for (col = image.cols - 1; col > text_start_col; col--) {
      int y = text_start_row;
      for (; y < text_end_row; y++) {
	if (image.at<uchar>(y, col) != ((uchar) 0)) {
	  break;
	}
      }
      if (y != text_end_row) {
	break;
      }
    }
    const int text_end_col = col + 1;

    tmp.emplace_back(
	text_start_col,
	text_start_row,
	text_end_col - text_start_col,
	text_end_row - text_start_row);
  }

  // A single text line can have multiple bboxes.
  // For example: "you win." has a separate bbox for the dot
  // on the 'i'. This can also happen with accents above or below
  // characters.
  //
  // One way to deal with this is to perform OCR and use the y-range
  // of the bounding box returned for the text line. Bounding boxes
  // returned by OCR aren't entirely reliable.
  //
  // For now, look for a low height of the bbox, then merge
  // with the closest of prev and next bbox that does not have low height.
 
  for (std::size_t i = 0; i < tmp.size(); i++) {
    cv::Rect& cr = tmp[i];
    if (text_line_bbox_vec.size()) {
      cv::Rect& pr = text_line_bbox_vec.back();
      if (cr.height < (pr.height / 2)) {
	if ((i + 1) < tmp.size()) {
	  cv::Rect& nr = tmp[i + 1];
	  if (cr.height < (nr.height / 2)) {
	    const int dist_pr = cr.y - (pr.y + pr.height);
	    const int dist_nr = nr.y - (cr.y + cr.height);
	    if (dist_pr < dist_nr) {
	      pr |= cr;
	    } else {
	      nr |= cr;
	    }
	  }
	} else {
	  pr |= cr;
	}
      } else if ((i + 1) < tmp.size()) {
	cv::Rect& nr = tmp[i + 1];
	if (cr.height < (nr.height / 2)) {
	  nr |= cr;
	}
	else {
	  text_line_bbox_vec.emplace_back(cr);
	}
      } else {
	text_line_bbox_vec.emplace_back(cr);
      }
    } else if ((i + 1) < tmp.size()) {
      cv::Rect& nr = tmp[i + 1];
      if (cr.height < (nr.height / 2)) {
	nr |= cr;
      }
      else {
	text_line_bbox_vec.emplace_back(cr);
      }
    }
    else {
      text_line_bbox_vec.emplace_back(cr);
    }
  }
}

