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

#include "stats.h++"

#include <algorithm>
#include <map>

MinAvgMaxStat::MinAvgMaxStat()
    : priv_min(INT_MAX),
      priv_min_count(0),
      priv_total(0),
      priv_count(0),
      priv_max(0),
      priv_max_count(0) {
}

void
MinAvgMaxStat::add(const int value) {
  priv_count++;

  priv_total += value;
  if (value <= priv_min) {
    if (value < priv_min) {
      priv_min = value;
      priv_min_count = 1;
    }
    else {
      priv_min_count++;
    }
  }
  if (value >= priv_max) {
    if (value > priv_max) {
      priv_max = value;
      priv_max_count = 1;
    }
    else {
      priv_max_count++;
    }
  }
}

std::optional<int>
MinAvgMaxStat::min() const {
  // don't return a value if the sample is too small
  return (priv_min_count >= 10) ? std::optional(priv_min) : std::nullopt;
}

std::optional<float>
MinAvgMaxStat::avg() const {
  // don't return a value if the sample is too small
  return (priv_count >= 10) ?
    std::optional(((float) priv_total) / ((float) priv_count)) : std::nullopt;
}

void
MinAvgMaxStat::dump(std::ostream& os) const {
  if (priv_count) {
    os <<
      priv_min <<
	"(" << priv_min_count << ")" <<
      "/" <<
      (((double) priv_total) / priv_count) << 
	"(" << priv_count << ")" <<
      "/" <<
      priv_max <<
	"(" << priv_max_count << ")";
  }
  else {
    os << "none";
  }
}

SymbolStat::SymbolStat()
    : width_stat(),
      height_stat(),
      pos_stat() {
}

void
SymbolStat::add(
    const cv::Rect& bbox,
    const int pos) {
  width_stat.add(bbox.width);
  height_stat.add(bbox.height);
  pos_stat.add(pos);
}

std::optional<int>
SymbolStat::height_min() const {
  return height_stat.min();
}

std::optional<float> 
SymbolStat::width_avg() const {
  return width_stat.avg();
}

std::optional<float> 
SymbolStat::pos_avg() const {
  return pos_stat.avg();
}

void
SymbolStat::dump(std::ostream& os) const {
  os << "w ";
  width_stat.dump(os);
  os << ", h ";
  height_stat.dump(os);
  os << ", p ";
  pos_stat.dump(os);
}

void
TextStats::symbol_add(
    const std::string& utf8_symbol,
    const cv::Rect& bbox,
    const int pos) {

  auto it = symbol_stats_map.find(utf8_symbol);

  if (it != symbol_stats_map.end()) {
    /* check if the bbox is sane */
    const std::optional<float> width_avg_opt = it->second.width_avg();
    if (bbox.width < (width_avg_opt.value_or(0) / 2)) {
      return;
    }
  }

  symbol_stats_map.try_emplace(utf8_symbol).first->second.add(bbox, pos);
}

void
TextStats::symbol_spacing_add(
    const int spacing) {
  symbol_spacing_stat.add(spacing);
}

void
TextStats::word_spacing_add(
    const int spacing) {
  if (symbol_spacing_stat.avg().has_value() &&
      (spacing < (2 * symbol_spacing_stat.avg().value()))) {
    // bad value, do not add
    return;
  }

  word_spacing_stat.add(spacing);
}

std::optional<int>
TextStats::symbol_height_min(
    const std::string& utf8_symbol) const {
  const auto it = symbol_stats_map.find(utf8_symbol);
  return (it != symbol_stats_map.end()) ? it->second.height_min() : std::nullopt;
}

std::optional<float>
TextStats::symbol_width_avg(
    const std::string& utf8_symbol) const {
  const auto it = symbol_stats_map.find(utf8_symbol);
  return (it != symbol_stats_map.end()) ? it->second.width_avg() : std::nullopt;
}

std::optional<float>
TextStats::symbol_pos_avg(
    const std::string& utf8_symbol) const {
  const auto it = symbol_stats_map.find(utf8_symbol);
  return (it != symbol_stats_map.end()) ? it->second.pos_avg() : std::nullopt;
}

std::optional<int>
TextStats::word_spacing_min() const {
  return word_spacing_stat.min();
}

std::optional<float>
TextStats::symbol_spacing_avg() const {
  return symbol_spacing_stat.avg();
}

void
TextStats::dump(std::ostream& os) const {
  os << "text stats: " << std::endl;
  os << "  word spacing: ";
  word_spacing_stat.dump(os);
  os << std::endl;
  os << "  symbol spacing: ";
  symbol_spacing_stat.dump(os);
  os << std::endl;

  {
    std::map<std::string, SymbolStat> ordered_symbol_stats_map(
	symbol_stats_map.begin(),
       	symbol_stats_map.end());

    for (auto it : ordered_symbol_stats_map) {
      os << "  " << it.first << ": ";
      it.second.dump(os);
      os << std::endl;
    }
  }
}

