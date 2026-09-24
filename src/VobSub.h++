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

#include <string>

#include "generic_exception.h++"

#include "mp_msg.h"
#include "vobsub.h"
#include "spudec.h"

typedef void* vob_t;
typedef void* spu_t;

#ifndef VOBSUB_HXX
#define VOBSUB_HXX

class VobSub {

public:
  VobSub();

  ~VobSub();

  void open(
      const std::string& sub_file_name,
      const std::string& ifo_file_name,
      const int y_threshold);

  void close();

  spu_t spu();

  vob_t vob();

private:
  spu_t priv_spu;
  vob_t priv_vob;
};

#endif // VOBSUB_HXX

