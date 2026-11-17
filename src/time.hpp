/*
 *  This file is part of VobSub2SRT
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

#include <string>
#include <cstdint>

extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
}

std::string pts2srt(uint64_t pts);

/* Timing for a single decoded subtitle, in 90kHz units.
 * end_pts is UINT64_MAX when the decoder gave no explicit end time.
 * The write-out pass then derives one from the next subtitle's start. */
struct sub_timing_t {
  uint64_t start_pts_90k;
  uint64_t end_pts_90k;
  bool skip;   // true when this packet is a duplicate of the previous start PTS
};

/* Derive start/end timing for one decoded subtitle from its packet PTS and the
 * dvdsub decoder's end_display_time, rescaling to the SRT timebase.
 *
 * last_start_pts tracks the previous subtitle's start (stream timebase units)
 * for duplicate detection; it is updated only when the subtitle is not skipped.
 */
sub_timing_t get_subtitle_timing(const AVPacket* packet, const AVStream* stream,
                                     const AVSubtitle& subtitle, unsigned& last_start_pts,
                                     int sub_counter, bool verbose);
