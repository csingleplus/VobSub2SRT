/*
 *  Filter/image-preprocessing + OCR step defs
 *
 *  This file is part of VobSub2SRT.
 *
 *  Copyright (C) 2010-2016 Rüdiger Sonderfeld <ruediger@c-plusplus.de>
 *  Copyright (C) 2026 Christopher Ogloff <chris.ogloff@gmail.com> (libav implementation)
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

#pragma once
extern "C" {
  #include <libavcodec/avcodec.h>   // AVSubtitle / AVSubtitleRect
}

#include <tesseract/baseapi.h> // Tesseract API
#include <string>

/* Image dump format for --dump-pgm / --dump-png */
enum class DumpFormat { NONE, PNG, PGM };

/* Subpicture size thresholds (set via --min-width / --min-height) */
extern int min_width;
extern int min_height;

/**
 * Extract subtitle image data from AVSubtitle, optionally dump images, then run OCR.
 *
 * Convert to grayscale and invert so text is dark on light background
 * (required for Tesseract OCR accuracy).
 */
std::string extract_subtitle_text(AVSubtitle* subtitle, tesseract::TessBaseAPI& tess,
                                  int y_threshold, DumpFormat dump_format = DumpFormat::NONE,
                                  const std::string* filename_base = nullptr, unsigned* counter_ptr = nullptr,
                                  const char* file_lang = nullptr, bool verbose = false);
