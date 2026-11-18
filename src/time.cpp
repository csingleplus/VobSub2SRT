/*
 *  Conversion of PTS integer values to hours/minutes/seconds/milliseconds
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

#include "time.hpp"
#include <array>
#include <cstdio>
#include <iostream>

/** Converts time stamp in pts format to a string containing the time stamp for the srt format
 *
 * pts (presentation time stamp) is given with a 90kHz resolution (1/90 ms).
 * srt expects a time stamp as  HH:MM:SS,MS.
 */
std::string pts2srt(uint64_t pts) {
    uint64_t ms = pts / 90;
    uint32_t h = static_cast<uint32_t>(ms / (3600u * 1000u));
    ms -= h * 3600u * 1000u;
    uint32_t m = static_cast<uint32_t>(ms / (60u * 1000u));
    ms -= m * 60u * 1000u;
    uint32_t s = static_cast<uint32_t>(ms / 1000u);
    ms %= 1000u;

    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%02u:%02u:%02u,%03lu", h, m, s, ms);
    return buf.data();
}

sub_timing_t get_sub_timing(const AVPacket* packet, const AVStream* stream, const AVSubtitle& subtitle,
			    unsigned& last_start_pts, int sub_counter, bool verbose) {
  sub_timing_t result;
  result.start_pts_90k = 0;
  result.end_pts_90k = UINT64_MAX;
  result.skip = false;

  // Extract timing information from packet PTS
  AVRational tb = stream->time_base;
  int64_t pts_val = 0;
  if (packet->pts != AV_NOPTS_VALUE) {
    // Copy packet->pts for our own usage
    pts_val = packet->pts;
  } else {
    // Last resort: sequential timing based on stream's actual timebase
    std::cerr << "Warning: PTS for subtitle #" << sub_counter
              << " is " << packet->pts << ", using default 1 second interval with timebase " << tb.num << "/" << tb.den << "\n";
    // Add ~1 second worth of ticks in stream's timebase units (e.g., 90k for MPEG, 25 or 30 for others)
    pts_val = last_start_pts + (tb.num / tb.den);
  }

  // Convert from stream timebase to standard SRT format (90kHz)
  uint64_t start_pts_90k = static_cast<uint64_t>(av_rescale_q(pts_val, tb, AVRational{1, 90000}));

  /* DVD subtitles don't provide accurate duration per packet (packet->duration is always 0).
     Use end_display_time from the dvdsub decoder; a value of 0 means no explicit end time
     so mark it with UINT64_MAX and let the write-out pass derive one from the next subtitle start. */
  uint64_t end_pts_90k = UINT64_MAX;
  if (subtitle.end_display_time > 0 && subtitle.end_display_time < UINT32_MAX) {
    // sanity: reject durations that would overflow or produce garbage timing
    const int64_t max_end_ticks = static_cast<int64_t>(UINT32_MAX - start_pts_90k);
    if (static_cast<uint64_t>(subtitle.end_display_time * 90) <= static_cast<uint64_t>(max_end_ticks)) {
      end_pts_90k = start_pts_90k + subtitle.end_display_time * 90;
    } else if (verbose) {
      std::cerr << "WARNING: invalid end_display_time=" << subtitle.end_display_time
                << " for subtitle #" << sub_counter << ", using next start time.\n";
    }
  }

  // Track last PTS to detect duplicates
  if (pts_val == last_start_pts && pts_val != 0) {
    result.skip = true;
    if (verbose) {
      std::cerr << "WARNING: Invalid last start PTS " << pts_val << ". Skipping.";
    }
  } else {
    last_start_pts = static_cast<unsigned>(pts_val);
  }

  result.start_pts_90k = start_pts_90k;
  result.end_pts_90k = end_pts_90k;
  return result;
}
