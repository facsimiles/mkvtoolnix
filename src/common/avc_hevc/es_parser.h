/** AVC & HEVC ES parser base class

   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   \author Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include "common/common_pch.h"

#include "common/avc_hevc/types.h"
#include "common/math_fwd.h"

namespace mtx::avc_hevc {

class es_parser_c {
protected:
  int m_nalu_size_length{4};

  bool m_keep_ar_info{true};
  bool m_configuration_record_ready{}, m_configuration_record_changed{};

  int64_t m_stream_default_duration{-1}, m_forced_default_duration{-1}, m_container_default_duration{-1};
  int m_frame_number{}, m_num_skipped_frames{};
  bool m_first_keyframe_found{}, m_recovery_point_valid{}, m_b_frames_since_keyframe{};

  bool m_par_found{};
  mtx_mp_rational_t m_par{};

  std::deque<frame_t> m_frames, m_frames_out;

  std::deque<std::pair<int64_t, uint64_t>> m_provided_timestamps;
  int64_t m_max_timestamp{};
  std::map<int64_t, int64_t> m_duration_frequency;

  std::vector<memory_cptr> m_sps_list, m_pps_list, m_vps_list;

  memory_cptr m_unparsed_buffer;
  uint64_t m_stream_position{}, m_parsed_position{};

  mtx::avc_hevc::frame_t m_incomplete_frame;

  std::deque<std::pair<memory_cptr, uint64_t>> m_unhandled_nalus;

  bool m_first_cleanup{true}, m_simple_picture_order{}, m_discard_actual_frames{};

  debugging_option_c m_debug_keyframe_detection, m_debug_nalu_types, m_debug_timestamps, m_debug_sps_info;

  static std::unordered_map<int, std::string> ms_nalu_names_by_type;

  struct stats_t {
    std::vector<int> num_slices_by_type, num_nalus_by_type;
    std::size_t num_frames_out{}, num_frames_discarded{}, num_timestamps_in{}, num_timestamps_generated{}, num_timestamps_discarded{}, num_field_slices{}, num_frame_slices{}, num_idr_slices{}, num_sei_nalus{};

    stats_t(std::size_t num_slice_types, std::size_t num_nalu_types)
      : num_slices_by_type(num_slice_types, 0)
      , num_nalus_by_type(num_nalu_types, 0)
    {
    }
  } m_stats;

protected:
  es_parser_c(std::string const &debug_type, std::size_t num_slice_types, std::size_t num_nalu_types);
  virtual ~es_parser_c();

public:
  void force_default_duration(int64_t default_duration);
  bool is_default_duration_forced() const;
  void set_container_default_duration(int64_t default_duration);
  bool has_stream_default_duration() const;
  int64_t get_stream_default_duration() const;

  void set_keep_ar_info(bool keep);

  void set_nalu_size_length(int nalu_size_length);
  int get_nalu_size_length() const;

  bool frame_available();
  mtx::avc_hevc::frame_t get_frame();

  bool configuration_record_changed() const;

  void add_timestamp(int64_t timestamp);
  int64_t get_most_often_used_duration() const;

  int get_num_skipped_frames() const;

  void discard_actual_frames(bool discard = true);

  std::size_t get_num_field_slices() const;
  std::size_t get_num_frame_slices() const;

  bool has_par_been_found() const;
  mtx_mp_rational_t const &get_par() const;
  std::pair<int64_t, int64_t> const get_display_dimensions(int width = -1, int height = -1) const;

  void flush_unhandled_nalus();

  std::vector<int64_t> calculate_provided_timestamps_to_use();

  void cleanup();

  virtual void flush() = 0;
  virtual void clear() = 0;

  virtual void handle_nalu(memory_cptr const &nalu, uint64_t nalu_pos) = 0;

  virtual int get_width() const = 0;
  virtual int get_height() const = 0;

  virtual void calculate_frame_order() = 0;
  virtual void calculate_frame_timestamps_references_and_update_stats() = 0;

  virtual void init_nalu_names() const = 0;

public:
  std::string get_nalu_type_name(int type);
};

}