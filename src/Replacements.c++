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

#include "Replacements.h++"

#include "generic_exception.h++"

#include <cstring>
#include <fstream>
#include <sstream>

void
Replacements::Replacement::replace(
    const std::string& src,
    std::string& dst) const {

  const std::regex regex(match_pattern, syntax_options);

  dst = std::move(std::regex_replace(
      src,
      regex,
      replacement_pattern,
      match_flags));
}

static bool
read_line(
    std::ifstream& ifs,
    std::string& line,
    int& line_number,
    bool skip_empty_line) {
  do {
    if (!std::getline(ifs, line)) {
      return false;
    }
    line_number++;

  } while ((line.size() && line[0] == '#') ||
      (skip_empty_line && std::strspn(line.c_str(), " \t") == line.size()));

  return true;
}

void
Replacements::read()
{
  // Open replacements input file
  std::ifstream ifs;
  try {
    ifs.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    ifs.open(file_name,
	std::ios_base::in | std::ios_base::binary);
    ifs.exceptions(std::ios_base::badbit);
  } catch (...) {
    throw generic_exception("Could not open replacements file '" + file_name + "' for reading");
  }

  int line_number = 1;
  std::string header;

  if (!read_line(ifs, header, line_number, true)) {
    return;
  }
  if (header != "ecmascript") {
    std::stringstream ss;
    ss << "'" << file_name << "': line " << line_number << ": file should have a line 'ecmascript' before replacment patterns.";
    throw generic_exception(ss.str());
  }

  while (true) {
    std::string tmp;
    std::string match_pattern;
    std::string replacement_pattern;

    // empty line(s) or match pattern
    if (!read_line(ifs, match_pattern, line_number, true)) {
      return;
    }

    // replacement pattern
    if (!read_line(ifs, replacement_pattern, line_number, false)) {
      std::stringstream ss;
      ss << "'" << file_name << "': line " << line_number << ": missing replacement pattern.";
      throw generic_exception(ss.str());
    }

    // add
    repl_vec.emplace_back(
	(std::regex_constants::ECMAScript |
	  std::regex_constants::multiline |
	  std::regex_constants::optimize),
	std::regex_constants::format_sed,
	match_pattern,
	replacement_pattern);

    // empty line or end of file
    if (!read_line(ifs, tmp, line_number, false)) {
      break;
    }
    if (std::strspn(tmp.c_str(), " \t") != tmp.size()) {
      std::stringstream ss;
      ss << "'" << file_name << "': line " << line_number << ": missing empty line after replacement pattern.";
      throw generic_exception(ss.str());
    }
  }
}

std::string
Replacements::replace(
    const std::string& src) const {

  std::string tmp(src);
  std::string tmp2;
  for (const auto& it : repl_vec) {
    it.replace(tmp, tmp2);
    std::swap(tmp, tmp2);
  }
  return tmp;
}

