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

#include <optional>
#include <string>
#include <unordered_map>
#include <ostream>

#include "bbox.h++"

#ifndef TEXT_STATS_HXX
#define TEXT_STATS_HXX

class MinAvgMaxStat {

public:
  MinAvgMaxStat();

  void add(const int value);

  std::optional<int> min() const;

  std::optional<float> avg() const;

  void dump(std::ostream& os) const;

private:
  int priv_min;
  int priv_min_count;

  int64_t priv_total;
  int priv_count;

  int priv_max;
  int priv_max_count;
};

class SymbolStat {
public:
  SymbolStat();

  void
  add(
      const cv::Rect& bbox,
      const int pos);

  std::optional<int>
  height_min() const;

  std::optional<float>
  width_avg() const;

  std::optional<float>
  pos_avg() const;

  void
  dump(
      std::ostream& os) const;

private:
  // min and max values can be bad if OCR has mis-identified a symbol.
  // if count is high, average is reliable.
  MinAvgMaxStat width_stat;
  MinAvgMaxStat height_stat;
  MinAvgMaxStat pos_stat;
};

class TextStats {

public:
  void
  symbol_add(
    const std::string& utf8_symbol,
    const cv::Rect& bbox,
    const int pos);

  void
  symbol_spacing_add(
      const int spacing);

  void
  word_spacing_add(
      const int spacing);

  std::optional<int>
  symbol_height_min(
      const std::string& utf8_symbol) const;

  std::optional<float>
  symbol_width_avg(
      const std::string& utf8_symbol) const;

  std::optional<float>
  symbol_pos_avg(
      const std::string& utf8_symbol) const;

  std::optional<int>
  word_spacing_min() const;

  std::optional<float>
  symbol_spacing_avg() const;

  void dump(
      std::ostream& os) const;

private:
  MinAvgMaxStat word_spacing_stat;

  MinAvgMaxStat symbol_spacing_stat;

  // unordered map for performance.
  std::unordered_map<std::string, SymbolStat> symbol_stats_map;
};

#endif // TEXT_STATS_HXX
#
