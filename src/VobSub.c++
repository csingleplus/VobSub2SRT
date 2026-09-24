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

#include "VobSub.h++"

VobSub::VobSub()
    : priv_spu(NULL),
      priv_vob(NULL) {
  mp_msg_init();
}

VobSub::~VobSub() {
  close();
  mp_msg_uninit();
}

void
VobSub::open(
    const std::string& sub_file_name,
    const std::string& ifo_file_name,
    const int y_threshold) {

  if (sub_file_name.empty()) {
    throw generic_exception("VobSub::open: empty file name");
  }

  if (priv_vob) {
    throw generic_exception("VobSub::open: already open");
  }

  priv_vob = vobsub_open(
      sub_file_name.c_str(),
      ifo_file_name.empty() ? NULL : ifo_file_name.c_str(),
      1,
      y_threshold,
      &priv_spu);
  if (!priv_vob || vobsub_get_indexes_count(priv_vob) == 0) {
    close();
    throw generic_exception(
	std::string("Couldn't open VobSub files '") +
       	sub_file_name + ".idx and " +
       	sub_file_name + ".sub'");
  }
}

void
VobSub::close() {
  if (priv_vob) {
    vobsub_close(priv_vob);
    priv_vob = NULL;
  }
  if (priv_spu) {
    spudec_free(priv_spu);
    priv_spu = NULL;
  }
}

spu_t
VobSub::spu() {
  if (!priv_spu) {
    throw generic_exception("VobSub::spu: not open");
  }

  return priv_spu;
}

vob_t
VobSub::vob() {
  if (!priv_vob) {
    throw generic_exception("VobSub::vob: not open");
  }

  return priv_vob;
}

