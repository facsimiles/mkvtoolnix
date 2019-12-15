/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   ProRes output module

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/codec.h"
#include "merge/connection_checks.h"
#include "output/p_prores.h"

using namespace libmatroska;

prores_video_packetizer_c::prores_video_packetizer_c(generic_reader_c *reader,
                                                     track_info_c &ti,
                                                     double fps,
                                                     int width,
                                                     int height)
  : generic_video_packetizer_c{reader, ti, MKV_V_PRORES, fps, width, height}
{
}

int
prores_video_packetizer_c::process(packet_cptr packet) {
  if ((packet->data->get_size() >= 8) && !std::memcmp(packet->data->get_buffer() + 4, "icpf", 4))
    packet->data->set_offset(8);

  return generic_video_packetizer_c::process(packet);
}

connection_result_e
prores_video_packetizer_c::can_connect_to(generic_packetizer_c *src,
                                           std::string &error_message) {
  auto vsrc = dynamic_cast<prores_video_packetizer_c *>(src);
  if (!vsrc)
    return CAN_CONNECT_NO_FORMAT;

  return generic_video_packetizer_c::can_connect_to(src, error_message);
}
