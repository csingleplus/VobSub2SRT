/*
 *  This file is part of vobsub2srt
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

std::string pts2srt(uint64_t pts);
