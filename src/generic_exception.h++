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

#include <exception>
#include <string>

#ifndef GENERIC_EXCEPTION_HXX
#define GENERIC_EXCEPTION_HXX

class generic_exception
  : public std::exception {
public:
  generic_exception(const std::string& what_arg)
    : what_str(what_arg) {
  }

  const char * what() const noexcept {
    return what_str.c_str();
  }

private:
  std::string what_str;
};

#endif // GENERIC_EXCEPTION_HXX
