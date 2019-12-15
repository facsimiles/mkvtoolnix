/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   class definition for the ProRes video output module

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include "common/common_pch.h"

#include "merge/generic_packetizer.h"
#include "output/p_generic_video.h"

class prores_video_packetizer_c: public generic_video_packetizer_c {
public:
  prores_video_packetizer_c(generic_reader_c *reader, track_info_c &ti, double fps, int width, int height);

  virtual int process(packet_cptr packet) override;

  virtual translatable_string_c get_format_name() const override {
    return YT("ProRes video");
  }
  virtual connection_result_e can_connect_to(generic_packetizer_c *src, std::string &error_message) override;
};
