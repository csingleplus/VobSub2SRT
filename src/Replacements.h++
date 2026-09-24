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

#include <regex>

#ifndef REPLACEMENTS_HXX
#define REPLACEMENTS_HXX

class Replacements {

public:
  class Replacement {
  public:
    Replacement(
	const std::regex_constants::syntax_option_type syntax_options,
	const std::regex_constants::match_flag_type match_flags,
	const std::string& match_pattern,
	const std::string& replacement_pattern)
	: syntax_options(syntax_options),
	  match_flags(match_flags),
	  match_pattern(match_pattern),
	  replacement_pattern(replacement_pattern) {
    }

  void
  replace(
      const std::string& src,
      std::string& dst) const;

  private:
    // See: https://en.cppreference.com/cpp/regex/syntax_option_type
    std::regex_constants::syntax_option_type syntax_options;

    // See: https://en.cppreference.com/cpp/regex/match_flag_type
    std::regex_constants::match_flag_type match_flags;

    std::string match_pattern;

    std::string replacement_pattern;
  };

  Replacements(
      const std::string& file_name)
      : file_name(file_name) {
  }

  void
  read();

  std::string
  replace(
      const std::string& src) const;

private:
  std::string file_name;

  std::vector<Replacement> repl_vec;
};

#endif // REPLACEMENTS_HXX
