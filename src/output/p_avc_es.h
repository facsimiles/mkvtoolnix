/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   class definition for the MPEG 4 part 10 ES video output module

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include "common/common_pch.h"

#include "common/avc_es_parser.h"
#include "merge/generic_packetizer.h"

class avc_es_video_packetizer_c: public generic_packetizer_c {
protected:
  mtx::avc::es_parser_c m_parser;
  int64_t m_default_duration_for_interlaced_content;
  bool m_first_frame, m_set_display_dimensions;
  debugging_option_c m_debug_timestamps, m_debug_aspect_ratio;

public:
  avc_es_video_packetizer_c(generic_reader_c *p_reader, track_info_c &p_ti);

  virtual int process(packet_cptr packet);
  virtual void add_extra_data(memory_cptr data);
  virtual void set_headers();
  virtual void set_container_default_field_duration(int64_t default_duration);
  virtual unsigned int get_nalu_size_length() const;

  virtual void flush_frames();

  virtual translatable_string_c get_format_name() const {
    return YT("AVC/H.264 (unframed)");
  };

  virtual void connect(generic_packetizer_c *src, int64_t p_append_timestamp_offset = -1);
  virtual connection_result_e can_connect_to(generic_packetizer_c *src, std::string &error_message);

protected:
  virtual void handle_delayed_headers();
  virtual void handle_aspect_ratio();
  virtual void handle_actual_default_duration();
  virtual void flush_impl();
};
