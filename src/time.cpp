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
