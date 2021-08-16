/** MPEG video helper functions (MPEG 1, 2 and 4)

   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   \file

*/

#include "common/common_pch.h"

#include <cmath>

#include "common/bit_reader.h"
#include "common/checksums/base_fwd.h"
#include "common/endian.h"
#include "common/hacks.h"
#include "common/math.h"
#include "common/mm_io.h"
#include "common/mm_file_io.h"
#include "common/mpeg.h"
#include "common/hevc/util.h"
#include "common/hevc/es_parser.h"
#include "common/hevc/hevcc.h"
#include "common/list_utils.h"
#include "common/memory_slice_cursor.h"
#include "common/strings/formatting.h"
#include "common/timestamp.h"

namespace mtx::hevc {

es_parser_c::es_parser_c()
  : mtx::avc_hevc::es_parser_c{"hevc"s, 3, 64}
{
  if (debugging_c::requested("hevc_statistics"))
    init_nalu_names();
}

es_parser_c::~es_parser_c() {
  mxdebug_if(m_debug_timestamps, fmt::format("stream_position {0} parsed_position {1}\n", m_stream_position, m_parsed_position));

  if (!debugging_c::requested("hevc_statistics"))
    return;

  mxdebug(fmt::format("HEVC statistics: #frames: out {0} discarded {1} #timestamps: in {2} generated {3} discarded {4} num_fields: {5} num_frames: {6}\n",
                      m_stats.num_frames_out, m_stats.num_frames_discarded, m_stats.num_timestamps_in, m_stats.num_timestamps_generated, m_stats.num_timestamps_discarded,
                      m_stats.num_field_slices, m_stats.num_frame_slices));

  static const char *s_type_names[] = {
    "B",  "P",  "I", "unknown"
  };

  mxdebug("hevc: Number of NALUs by type:\n");
  for (int i = 0, size = m_stats.num_nalus_by_type.size(); i < size; ++i)
    if (0 != m_stats.num_nalus_by_type[i])
      mxdebug(fmt::format("  {0}: {1}\n", get_nalu_type_name(i), m_stats.num_nalus_by_type[i]));

  mxdebug("hevc: Number of slices by type:\n");
  for (int i = 0; 2 >= i; ++i)
    if (0 != m_stats.num_slices_by_type[i])
      mxdebug(fmt::format("  {0}: {1}\n", s_type_names[i], m_stats.num_slices_by_type[i]));
}

bool
es_parser_c::headers_parsed()
  const {
  return m_configuration_record_ready
      && !m_sps_info_list.empty()
      && (m_sps_info_list.front().get_width()  > 0)
      && (m_sps_info_list.front().get_height() > 0);
}

void
es_parser_c::normalize_parameter_sets(bool normalize) {
  m_normalize_parameter_sets = normalize;
}

void
es_parser_c::flush() {
  if (m_unparsed_buffer && (5 <= m_unparsed_buffer->get_size())) {
    m_parsed_position += m_unparsed_buffer->get_size();
    auto marker_size   = get_uint32_be(m_unparsed_buffer->get_buffer()) == mtx::avc_hevc::NALU_START_CODE ? 4 : 3;
    auto nalu_size     = m_unparsed_buffer->get_size() - marker_size;
    handle_nalu(memory_c::clone(m_unparsed_buffer->get_buffer() + marker_size, nalu_size), m_parsed_position - nalu_size);
  }

  m_unparsed_buffer.reset();
  if (!m_pending_frame_data.empty()) {
    build_frame_data();
    m_frames.emplace_back(m_incomplete_frame);
  }

  cleanup();
}

void
es_parser_c::clear() {
  m_unparsed_buffer.reset();
  m_extra_data_pre.clear();
  m_extra_data_initial.clear();
  m_pending_frame_data.clear();

  m_parsed_position = 0;
}

void
es_parser_c::add_parameter_sets_to_extra_data() {
  std::unordered_map<uint32_t, bool> is_in_extra_data;

  for (auto const &data : m_extra_data_pre) {
    auto nalu_type = (data->get_buffer()[0] >> 1) & 0x3f;
    if (mtx::included_in(nalu_type, NALU_TYPE_VIDEO_PARAM, NALU_TYPE_SEQ_PARAM, NALU_TYPE_PIC_PARAM))
      return;

    is_in_extra_data[mtx::checksum::calculate_as_uint(mtx::checksum::algorithm_e::adler32, *data)] = true;
  }

  auto old_extra_data = std::move(m_extra_data_pre);

  m_extra_data_pre.clear();
  m_extra_data_pre.reserve(m_vps_list.size() + m_sps_list.size() + m_pps_list.size() + old_extra_data.size() + m_extra_data_initial.size());

  auto inserter = std::back_inserter(m_extra_data_pre);

  std::copy(m_vps_list.begin(), m_vps_list.end(), inserter);
  std::copy(m_sps_list.begin(), m_sps_list.end(), inserter);
  std::copy(m_pps_list.begin(), m_pps_list.end(), inserter);

  for (auto const &data : m_extra_data_initial)
    if (!is_in_extra_data[mtx::checksum::calculate_as_uint(mtx::checksum::algorithm_e::adler32, *data)])
      inserter = data;

  std::copy(old_extra_data.begin(), old_extra_data.end(), inserter);

  m_extra_data_initial.clear();
}

void
es_parser_c::flush_incomplete_frame() {
  if (m_pending_frame_data.empty() || !m_configuration_record_ready)
    return;

  build_frame_data();

  m_frames.push_back(m_incomplete_frame);
  m_incomplete_frame.clear();
}

void
es_parser_c::add_nalu_to_extra_data(memory_cptr const &nalu,
                                    extra_data_position_e position) {
  if (position == extra_data_position_e::dont_store)
    return;

  nalu->take_ownership();

  auto &container = position == extra_data_position_e::pre  ? m_extra_data_pre : m_extra_data_initial;
  container.push_back(nalu);
}

void
es_parser_c::add_nalu_to_pending_frame_data(memory_cptr const &nalu) {
  nalu->take_ownership();
  m_pending_frame_data.emplace_back(nalu);
}

void
es_parser_c::handle_slice_nalu(memory_cptr const &nalu,
                               uint64_t nalu_pos) {
  if (!m_configuration_record_ready) {
    m_unhandled_nalus.emplace_back(nalu, nalu_pos);
    return;
  }

  mtx::avc_hevc::slice_info_t si;
  if (!parse_slice(nalu, si))   // no conversion to RBSP; the bit reader takes care of it
    return;

  if (!m_pending_frame_data.empty() && si.first_slice_segment_in_pic_flag)
    flush_incomplete_frame();

  if (!m_pending_frame_data.empty()) {
    add_nalu_to_pending_frame_data(nalu);
    return;
  }

  bool is_i_slice = (SLICE_TYPE_I == si.slice_type);
  bool is_b_slice = (SLICE_TYPE_B == si.slice_type);

  m_incomplete_frame.m_si       =  si;
  m_incomplete_frame.m_keyframe =  m_recovery_point_valid
                                || (   is_i_slice
                                    && (   (m_debug_keyframe_detection && !m_b_frames_since_keyframe)
                                        || (NALU_TYPE_IDR_W_RADL == si.nalu_type)
                                        || (NALU_TYPE_IDR_N_LP   == si.nalu_type)
                                        || (NALU_TYPE_CRA_NUT    == si.nalu_type)));
  m_incomplete_frame.m_type     = m_incomplete_frame.m_keyframe ? 'I' : is_b_slice ? 'B' : 'P';
  m_incomplete_frame.m_position = nalu_pos;
  m_recovery_point_valid        = false;

  if (m_incomplete_frame.m_keyframe) {
    m_first_keyframe_found    = true;
    m_b_frames_since_keyframe = false;
    cleanup();

  } else
    m_b_frames_since_keyframe |= is_b_slice;

  add_nalu_to_pending_frame_data(nalu);

  ++m_frame_number;
}

void
es_parser_c::handle_vps_nalu(memory_cptr const &nalu,
                             extra_data_position_e extra_data_position) {
  vps_info_t vps_info;

  if (!parse_vps(mpeg::nalu_to_rbsp(nalu), vps_info))
    return;

  size_t i;
  for (i = 0; m_vps_info_list.size() > i; ++i)
    if (m_vps_info_list[i].id == vps_info.id)
      break;

  auto update_codec_private = false;

  if (m_vps_info_list.size() == i) {
    m_vps_list.push_back(nalu->clone());
    m_vps_info_list.push_back(vps_info);
    m_configuration_record_changed = true;

  } else if (m_vps_info_list[i].checksum != vps_info.checksum) {
    mxdebug_if(m_debug_parameter_sets, fmt::format("hevc: VPS ID {0:04x} changed; checksum old {1:04x} new {2:04x}\n", vps_info.id, m_vps_info_list[i].checksum, vps_info.checksum));

    m_vps_info_list[i] = vps_info;
    m_vps_list[i]      = nalu->clone();
    m_configuration_record_changed    = true;

    // Update codec private if needed
    if (m_codec_private.vps_data_id == (int) vps_info.id)
      update_codec_private = true;
  }

  // Update codec private if needed
  if (-1 == m_codec_private.vps_data_id)
    update_codec_private = true;

  if (update_codec_private) {
    m_codec_private.profile_space              = vps_info.profile_space;
    m_codec_private.tier_flag                  = vps_info.tier_flag;
    m_codec_private.profile_idc                = vps_info.profile_idc;
    m_codec_private.profile_compatibility_flag = vps_info.profile_compatibility_flag;
    m_codec_private.progressive_source_flag    = vps_info.progressive_source_flag;
    m_codec_private.interlaced_source_flag     = vps_info.interlaced_source_flag;
    m_codec_private.non_packed_constraint_flag = vps_info.non_packed_constraint_flag;
    m_codec_private.frame_only_constraint_flag = vps_info.frame_only_constraint_flag;
    m_codec_private.level_idc                  = vps_info.level_idc;
    m_codec_private.vps_data_id                = vps_info.id;
  }

  add_nalu_to_extra_data(nalu, extra_data_position);
}

void
es_parser_c::handle_sps_nalu(memory_cptr const &nalu,
                             extra_data_position_e extra_data_position) {
  sps_info_t sps_info;

  auto parsed_nalu = parse_sps(mpeg::nalu_to_rbsp(nalu), sps_info, m_vps_info_list, m_keep_ar_info);
  if (!parsed_nalu)
    return;

  parsed_nalu = mpeg::rbsp_to_nalu(parsed_nalu);

  size_t i;
  for (i = 0; m_sps_info_list.size() > i; ++i)
    if (m_sps_info_list[i].id == sps_info.id)
      break;

  auto use_sps_info         = true;
  auto update_codec_private = false;

  if (m_sps_info_list.size() == i) {
    m_sps_list.push_back(parsed_nalu->clone());
    m_sps_info_list.push_back(sps_info);
    m_configuration_record_changed = true;

  } else if (m_sps_info_list[i].checksum != sps_info.checksum) {
    mxdebug_if(m_debug_parameter_sets, fmt::format("hevc: SPS ID {0:04x} changed; checksum old {1:04x} new {2:04x}\n", sps_info.id, m_sps_info_list[i].checksum, sps_info.checksum));

    cleanup();

    m_sps_info_list[i] = sps_info;
    m_sps_list[i]      = parsed_nalu->clone();
    m_configuration_record_changed    = true;

    // Update codec private if needed
    if (m_codec_private.sps_data_id == (int) sps_info.id)
      update_codec_private = true;

  } else
    use_sps_info = false;

  add_nalu_to_extra_data(parsed_nalu, extra_data_position);

  // Update codec private if needed
  if (-1 == m_codec_private.sps_data_id)
    update_codec_private = true;

  if (update_codec_private) {
    m_codec_private.min_spatial_segmentation_idc = sps_info.min_spatial_segmentation_idc;
    m_codec_private.chroma_format_idc = sps_info.chroma_format_idc;
    m_codec_private.bit_depth_luma_minus8 = sps_info.bit_depth_luma_minus8;
    m_codec_private.bit_depth_chroma_minus8 = sps_info.bit_depth_chroma_minus8;
    m_codec_private.max_sub_layers_minus1 = sps_info.max_sub_layers_minus1;
    m_codec_private.temporal_id_nesting_flag = sps_info.temporal_id_nesting_flag;
    m_codec_private.sps_data_id = sps_info.id;
  }

  if (use_sps_info && m_debug_sps_info)
    sps_info.dump();

  if (!use_sps_info)
    return;

  if (!has_stream_default_duration()
      && sps_info.timing_info_valid()) {
    m_stream_default_duration = sps_info.default_duration();
    mxdebug_if(m_debug_timestamps, fmt::format("Stream default duration: {0}\n", m_stream_default_duration));
  }

  if (!m_par_found
      && sps_info.ar_found
      && (0 != sps_info.par_den)) {
    m_par_found = true;
    m_par       = mtx_mp_rational_t(sps_info.par_num, sps_info.par_den);
  }
}

void
es_parser_c::handle_pps_nalu(memory_cptr const &nalu,
                             extra_data_position_e extra_data_position) {
  pps_info_t pps_info;

  if (!parse_pps(mpeg::nalu_to_rbsp(nalu), pps_info))
    return;

  size_t i;
  for (i = 0; m_pps_info_list.size() > i; ++i)
    if (m_pps_info_list[i].id == pps_info.id)
      break;

  if (m_pps_info_list.size() == i) {
    m_pps_list.push_back(nalu->clone());
    m_pps_info_list.push_back(pps_info);
    m_configuration_record_changed = true;

  } else if (m_pps_info_list[i].checksum != pps_info.checksum) {
    mxdebug_if(m_debug_parameter_sets, fmt::format("hevc: PPS ID {0:04x} changed; checksum old {1:04x} new {2:04x}\n", pps_info.id, m_pps_info_list[i].checksum, pps_info.checksum));

    if (m_pps_info_list[i].sps_id != pps_info.sps_id)
      cleanup();

    m_pps_info_list[i] = pps_info;
    m_pps_list[i]      = nalu->clone();
    m_configuration_record_changed    = true;
  }

  add_nalu_to_extra_data(nalu, extra_data_position);
}

void
es_parser_c::handle_sei_nalu(memory_cptr const &nalu,
                             extra_data_position_e extra_data_position) {
  if (parse_sei(mpeg::nalu_to_rbsp(nalu), m_user_data))
    add_nalu_to_extra_data(nalu, extra_data_position);
}

void
es_parser_c::handle_unspec62_nalu(memory_cptr const &nalu) {
  if (parse_dovi_rpu(mpeg::nalu_to_rbsp(nalu), m_dovi_rpu_data_header))
    add_nalu_to_pending_frame_data(nalu);
}

void
es_parser_c::handle_nalu_internal(memory_cptr const &nalu,
                                  uint64_t nalu_pos) {
  static debugging_option_c s_debug_discard_access_unit_delimiters{"hevc_discard_access_unit_delimiters"};

  if (1 > nalu->get_size())
    return;

  int type = (*(nalu->get_buffer()) >> 1) & 0x3F;

  mxdebug_if(m_debug_nalu_types, fmt::format("NALU type 0x{0:02x} ({1}) size {2}\n", type, get_nalu_type_name(type), nalu->get_size()));

  ++m_stats.num_nalus_by_type[std::min(type, 63)];

  switch (type) {
    case NALU_TYPE_VIDEO_PARAM:
      flush_incomplete_frame();
      handle_vps_nalu(nalu);
      break;

    case NALU_TYPE_SEQ_PARAM:
      flush_incomplete_frame();
      handle_sps_nalu(nalu);
      break;

    case NALU_TYPE_PIC_PARAM:
      flush_incomplete_frame();
      handle_pps_nalu(nalu);
      break;

    case NALU_TYPE_PREFIX_SEI:
      flush_incomplete_frame();
      handle_sei_nalu(nalu);
      break;

    case NALU_TYPE_END_OF_SEQ:
    case NALU_TYPE_END_OF_STREAM:
      flush_incomplete_frame();
      break;

    case NALU_TYPE_ACCESS_UNIT:
      flush_incomplete_frame();
      if (!s_debug_discard_access_unit_delimiters)
        add_nalu_to_extra_data(nalu);
      break;

    case NALU_TYPE_FILLER_DATA:
      // Skip these.
      break;

    case NALU_TYPE_UNSPEC62:
      handle_unspec62_nalu(nalu);
      break;

    case NALU_TYPE_TRAIL_N:
    case NALU_TYPE_TRAIL_R:
    case NALU_TYPE_TSA_N:
    case NALU_TYPE_TSA_R:
    case NALU_TYPE_STSA_N:
    case NALU_TYPE_STSA_R:
    case NALU_TYPE_RADL_N:
    case NALU_TYPE_RADL_R:
    case NALU_TYPE_RASL_N:
    case NALU_TYPE_RASL_R:
    case NALU_TYPE_BLA_W_LP:
    case NALU_TYPE_BLA_W_RADL:
    case NALU_TYPE_BLA_N_LP:
    case NALU_TYPE_IDR_W_RADL:
    case NALU_TYPE_IDR_N_LP:
    case NALU_TYPE_CRA_NUT:
      if (!m_configuration_record_ready && !m_vps_info_list.empty() && !m_sps_info_list.empty() && !m_pps_info_list.empty()) {
        m_configuration_record_ready = true;
        flush_unhandled_nalus();
      }
      handle_slice_nalu(nalu, nalu_pos);
      break;

    case NALU_TYPE_SUFFIX_SEI:
    case NALU_TYPE_RSV_NVCL45:
    case NALU_TYPE_RSV_NVCL46:
    case NALU_TYPE_RSV_NVCL47:
    case NALU_TYPE_UNSPEC56:
    case NALU_TYPE_UNSPEC57:
    case NALU_TYPE_UNSPEC58:
    case NALU_TYPE_UNSPEC59:
    case NALU_TYPE_UNSPEC60:
    case NALU_TYPE_UNSPEC61:
    case NALU_TYPE_UNSPEC63:
      add_nalu_to_pending_frame_data(nalu);
      break;

    default:
      flush_incomplete_frame();
      if (!m_configuration_record_ready && !m_vps_info_list.empty() && !m_sps_info_list.empty() && !m_pps_info_list.empty()) {
        m_configuration_record_ready = true;
        flush_unhandled_nalus();
      }
      add_nalu_to_extra_data(nalu);

      break;
  }
}

void
es_parser_c::handle_nalu(memory_cptr const &nalu,
                         uint64_t nalu_pos) {
  try {
    handle_nalu_internal(nalu, nalu_pos);

  } catch (bool) {
  } catch (mtx::mm_io::end_of_file_x const &) {
  }
}

bool
es_parser_c::parse_slice(memory_cptr const &nalu,
                         mtx::avc_hevc::slice_info_t &si) {
  try {
    mtx::bits::reader_c r(nalu->get_buffer(), nalu->get_size());
    r.enable_rbsp_mode();

    unsigned int i;

    si.clear();

    r.get_bits(1);                      // forbidden_zero_bit
    si.nalu_type = r.get_bits(6);       // nal_unit_type
    r.get_bits(6);                      // nuh_reserved_zero_6bits
    si.temporal_id = r.get_bits(3) - 1; // nuh_temporal_id_plus1

    bool RapPicFlag = (si.nalu_type >= 16 && si.nalu_type <= 23); // RapPicFlag
    si.first_slice_segment_in_pic_flag = r.get_bits(1); // first_slice_segment_in_pic_flag

    if (RapPicFlag)
      r.get_bits(1);  // no_output_of_prior_pics_flag

    si.pps_id = r.get_unsigned_golomb();  // slice_pic_parameter_set_id

    size_t pps_idx;
    for (pps_idx = 0; m_pps_info_list.size() > pps_idx; ++pps_idx)
      if (m_pps_info_list[pps_idx].id == si.pps_id)
        break;
    if (m_pps_info_list.size() == pps_idx) {
      mxdebug_if(m_debug_parameter_sets, fmt::format("slice parser error: PPS not found: {0}\n", si.pps_id));
      return false;
    }

    pps_info_t &pps = m_pps_info_list[pps_idx];
    size_t sps_idx;
    for (sps_idx = 0; m_sps_info_list.size() > sps_idx; ++sps_idx)
      if (m_sps_info_list[sps_idx].id == pps.sps_id)
        break;
    if (m_sps_info_list.size() == sps_idx)
      return false;

    si.sps = sps_idx;
    si.pps = pps_idx;

    sps_info_t &sps = m_sps_info_list[sps_idx];

    bool dependent_slice_segment_flag = false;
    if (!si.first_slice_segment_in_pic_flag) {
      if (pps.dependent_slice_segments_enabled_flag)
        dependent_slice_segment_flag = r.get_bits(1); // dependent_slice_segment_flag

      auto log2_min_cb_size_y   = sps.log2_min_luma_coding_block_size_minus3 + 3;
      auto log2_ctb_size_y      = log2_min_cb_size_y + sps.log2_diff_max_min_luma_coding_block_size;
      auto ctb_size_y           = 1 << log2_ctb_size_y;
      auto pic_width_in_ctbs_y  = ceil(static_cast<double>(sps.width)  / ctb_size_y);
      auto pic_height_in_ctbs_y = ceil(static_cast<double>(sps.height) / ctb_size_y);
      auto pic_size_in_ctbs_y   = pic_width_in_ctbs_y * pic_height_in_ctbs_y;
      auto v                    = mtx::math::int_log2((pic_size_in_ctbs_y - 1) * 2);

      r.get_bits(v);  // slice_segment_address
    }

    if (!dependent_slice_segment_flag) {
      for (i = 0; i < pps.num_extra_slice_header_bits; i++)
        r.get_bits(1);  // slice_reserved_undetermined_flag[i]

      si.slice_type = r.get_unsigned_golomb();  // slice_type

      if (pps.output_flag_present_flag)
        r.get_bits(1);    // pic_output_flag

      if (sps.separate_colour_plane_flag == 1)
        r.get_bits(1);    // colour_plane_id

      if ( (si.nalu_type != NALU_TYPE_IDR_W_RADL) && (si.nalu_type != NALU_TYPE_IDR_N_LP) ) {
        si.pic_order_cnt_lsb = r.get_bits(sps.log2_max_pic_order_cnt_lsb); // slice_pic_order_cnt_lsb
      }

      ++m_stats.num_slices_by_type[1 < si.slice_type ? 2 : si.slice_type];
    }

    return true;
  } catch (...) {
    return false;
  }
}

void
es_parser_c::build_frame_data() {
  if (m_incomplete_frame.m_keyframe && m_normalize_parameter_sets)
    add_parameter_sets_to_extra_data();

  auto all_nalus = std::move(m_extra_data_pre);
  all_nalus.reserve(all_nalus.size() + m_pending_frame_data.size());

  std::copy(m_pending_frame_data.begin(), m_pending_frame_data.end(), std::back_inserter(all_nalus));

  m_extra_data_pre.clear();
  m_pending_frame_data.clear();

  auto final_size = 0;

  for (auto const &nalu : all_nalus)
    final_size += m_nalu_size_length + nalu->get_size();

  m_incomplete_frame.m_data = memory_c::alloc(final_size);
  auto dest                 = m_incomplete_frame.m_data->get_buffer();

  for (auto const &nalu : all_nalus) {
    mtx::mpeg::write_nalu_size(dest, nalu->get_size(), m_nalu_size_length);
    std::memcpy(dest + m_nalu_size_length, nalu->get_buffer(), nalu->get_size());

    dest += m_nalu_size_length + nalu->get_size();
  }
}

int64_t
es_parser_c::duration_for(mtx::avc_hevc::slice_info_t const &si)
  const {
  int64_t duration = -1 != m_forced_default_duration                                                  ? m_forced_default_duration * 2
                   : (m_sps_info_list.size() > si.sps) && m_sps_info_list[si.sps].timing_info_valid() ? m_sps_info_list[si.sps].default_duration()
                   : -1 != m_stream_default_duration                                                  ? m_stream_default_duration * 2
                   : -1 != m_container_default_duration                                               ? m_container_default_duration * 2
                   :                                                                                    20000000 * 2;
  return duration;
}

void
es_parser_c::calculate_frame_order() {
  auto frames_begin      = m_frames.begin();
  auto frames_end        = m_frames.end();
  auto frame_itr         = frames_begin;

  auto &idr_si           = frame_itr->m_si;
  auto &sps              = m_sps_info_list[idr_si.sps];

  auto idx               = 0u;

  m_simple_picture_order = false;

  while (frames_end != frame_itr) {
    auto &si = frame_itr->m_si;

    if (si.sps != idr_si.sps) {
      m_simple_picture_order = true;
      break;
    }

    if ((NALU_TYPE_IDR_W_RADL == si.nalu_type) || (NALU_TYPE_IDR_N_LP == si.nalu_type)) {
      frame_itr->m_presentation_order = 0;
      mxdebug_if(m_debug_frame_order, fmt::format("frame order: KEY!\n"));

    } else {
      int poc_msb;
      int max_poc_lsb = 1 << (sps.log2_max_pic_order_cnt_lsb);
      int poc_lsb     = si.pic_order_cnt_lsb;

      auto condition1 = poc_lsb < m_prev_pic_order_cnt_lsb && (m_prev_pic_order_cnt_lsb - poc_lsb) >= (max_poc_lsb / 2);
      auto condition2 = poc_lsb > m_prev_pic_order_cnt_lsb && (poc_lsb - m_prev_pic_order_cnt_lsb) >  (max_poc_lsb / 2);

      if (condition1)
        poc_msb = m_prev_pic_order_cnt_msb + max_poc_lsb;
      else if (condition2)
        poc_msb = m_prev_pic_order_cnt_msb - max_poc_lsb;
      else
        poc_msb = m_prev_pic_order_cnt_msb;

      if (mtx::included_in(si.nalu_type, NALU_TYPE_BLA_W_LP, NALU_TYPE_BLA_W_RADL, NALU_TYPE_BLA_N_LP))
        poc_msb = 0;

      frame_itr->m_presentation_order = poc_lsb + poc_msb;

      mxdebug_if(m_debug_frame_order,
                 fmt::format("frame order: {0} lsb {1} msb {2} max_poc_lsb {3} prev_lsb {4} prev_msb {5} cond1 {6} cond2 {7} NALsize {8} type {9} ({10})\n",
                             frame_itr->m_presentation_order, poc_lsb, poc_msb, max_poc_lsb, m_prev_pic_order_cnt_lsb, m_prev_pic_order_cnt_msb, condition1, condition2, frame_itr->m_data->get_size(), static_cast<unsigned int>(si.nalu_type), get_nalu_type_name(si.nalu_type)));

      if (   (frame_itr->m_si.temporal_id == 0)
          && !mtx::included_in(si.nalu_type, NALU_TYPE_TRAIL_N, NALU_TYPE_TSA_N, NALU_TYPE_STSA_N, NALU_TYPE_RADL_N, NALU_TYPE_RASL_N, NALU_TYPE_RADL_R, NALU_TYPE_RASL_R)) {
        m_prev_pic_order_cnt_lsb = poc_lsb;
        m_prev_pic_order_cnt_msb = poc_msb;
      }
    }

    frame_itr->m_decode_order = idx;

    ++frame_itr;
    ++idx;
  }
}

void
es_parser_c::calculate_frame_timestamps(std::vector<int64_t> const &provided_timestamps_to_use) {
  auto frames_begin           = m_frames.begin();
  auto frames_end             = m_frames.end();
  auto previous_frame_itr     = frames_begin;
  auto provided_timestamp_itr = provided_timestamps_to_use.begin();

  for (auto frame_itr = frames_begin; frames_end != frame_itr; ++frame_itr) {
    if (frame_itr->m_has_provided_timestamp) {
      frame_itr->m_start = *provided_timestamp_itr;
      ++provided_timestamp_itr;

      if (frames_begin != frame_itr)
        previous_frame_itr->m_end = frame_itr->m_start;

    } else {
      frame_itr->m_start = frames_begin == frame_itr ? m_max_timestamp : previous_frame_itr->m_end;
      ++m_stats.num_timestamps_generated;
    }

    frame_itr->m_end = frame_itr->m_start + duration_for(frame_itr->m_si);

    previous_frame_itr = frame_itr;
  }

  m_max_timestamp = m_frames.back().m_end;

  mxdebug_if(m_debug_timestamps,
             fmt::format("CLEANUP frames <pres_ord dec_ord has_prov_ts tc dur>: {0}\n",
                         std::accumulate(m_frames.begin(), m_frames.end(), ""s, [](auto const &accu, auto const &frame) {
                           return accu + fmt::format(" <{0} {1} {2} {3} {4}>", frame.m_presentation_order, frame.m_decode_order, frame.m_has_provided_timestamp, frame.m_start, frame.m_end - frame.m_start);
                         })));

void
es_parser_c::calculate_frame_references() {
  for (auto frame_itr = m_frames.begin(), frames_end = m_frames.end(); frames_end != frame_itr; ++frame_itr) {
    if (frame_itr->is_i_frame()) {
      m_previous_i_p_start = frame_itr->m_start;
      continue;
    }

    frame_itr->m_ref1 = m_previous_i_p_start - frame_itr->m_start;

    if (frame_itr->is_p_frame()) {
      m_previous_i_p_start = frame_itr->m_start;
      continue;
    }

    auto next_i_p_frame_itr = frame_itr + 1;

    while ((frames_end != next_i_p_frame_itr) && next_i_p_frame_itr->is_b_frame())
      ++next_i_p_frame_itr;

    auto forward_ref_start = frames_end != next_i_p_frame_itr ? next_i_p_frame_itr->m_start : m_max_timestamp;
    frame_itr->m_ref2      = forward_ref_start - frame_itr->m_start;
  }
}

void
es_parser_c::update_frame_stats() {
  mxdebug_if(m_debug_timestamps, fmt::format("DECODE order dump\n"));

  for (auto &frame : m_frames) {
    mxdebug_if(m_debug_timestamps, fmt::format("  type {0} TS {1} size {2} pos 0x{3:x} ref1 {4} ref2 {5}\n", frame.m_type, mtx::string::format_timestamp(frame.m_start), frame.m_data->get_size(), frame.m_position, frame.m_ref1, frame.m_ref2));

    ++m_duration_frequency[frame.m_end - frame.m_start];

    if (frame.m_si.field_pic_flag)
      ++m_stats.num_field_slices;
    else
      ++m_stats.num_frame_slices;
  }
}

void
es_parser_c::calculate_frame_timestamps_references_and_update_stats() {
  auto provided_timestamps_to_use = calculate_provided_timestamps_to_use();

  if (!m_simple_picture_order)
    std::sort(m_frames.begin(), m_frames.end(), [](auto const &f1, auto const &f2) { return f1.m_presentation_order < f2.m_presentation_order; });

  calculate_frame_timestamps(provided_timestamps_to_use);
  calculate_frame_references();

  if (!m_simple_picture_order)
    std::sort(m_frames.begin(), m_frames.end(), [](auto const &f1, auto const &f2) { return f1.m_decode_order < f2.m_decode_order; });

  update_frame_stats();
}

memory_cptr
es_parser_c::get_hevcc()
  const {
  return hevcc_c{static_cast<unsigned int>(m_nalu_size_length), m_vps_list, m_sps_list, m_pps_list, m_user_data, m_codec_private}.pack();
}

void
es_parser_c::set_hevcc(memory_cptr const &hevcc_bytes) {
  auto hevcc = hevcc_c::unpack(hevcc_bytes);

  for (auto const &nalu : hevcc.m_vps_list)
    handle_vps_nalu(nalu, extra_data_position_e::dont_store);

  for (auto const &nalu : hevcc.m_sps_list)
    handle_sps_nalu(nalu, extra_data_position_e::dont_store);

  for (auto const &nalu : hevcc.m_pps_list)
    handle_pps_nalu(nalu, extra_data_position_e::dont_store);

  for (auto const &nalu : hevcc.m_sei_list)
    handle_sei_nalu(nalu, extra_data_position_e::initial);
}

void
es_parser_c::dump_info()
  const {
  auto dump_ps = [](std::string const &type, std::vector<memory_cptr> const &buffers) {
    mxinfo(fmt::format("Dumping {0}:\n", type));
    for (int idx = 0, num_entries = buffers.size(); idx < num_entries; ++idx)
      mxinfo(fmt::format("  {0} size {1} adler32 0x{2:08x}\n", idx, buffers[idx]->get_size(), mtx::checksum::calculate_as_uint(mtx::checksum::algorithm_e::adler32, *buffers[idx])));
  };

  dump_ps("m_vps",                m_vps_list);
  dump_ps("m_sps",                m_sps_list);
  dump_ps("m_pps_list",           m_pps_list);
  dump_ps("m_extra_data_pre",     m_extra_data_pre);
  dump_ps("m_extra_data_initial", m_extra_data_initial);
  dump_ps("m_pending_frame_data", m_pending_frame_data);

  mxinfo("Dumping m_frames_out:\n");
  for (auto &frame : m_frames_out) {
    mxinfo(fmt::format("  size {0} key {1} start {2} end {3} ref1 {4} adler32 0x{5:08x}\n",
                       frame.m_data->get_size(),
                       frame.m_keyframe,
                       mtx::string::format_timestamp(frame.m_start),
                       mtx::string::format_timestamp(frame.m_end),
                       mtx::string::format_timestamp(frame.m_ref1),
                       mtx::checksum::calculate_as_uint(mtx::checksum::algorithm_e::adler32, *frame.m_data)));
  }
}

void
es_parser_c::init_nalu_names()
  const {
  if (!ms_nalu_names_by_type.empty())
    return;

  ms_nalu_names_by_type = std::unordered_map<int, std::string>{
    { NALU_TYPE_TRAIL_N,       "trail_n"       },
    { NALU_TYPE_TRAIL_R,       "trail_r"       },
    { NALU_TYPE_TSA_N,         "tsa_n"         },
    { NALU_TYPE_TSA_R,         "tsa_r"         },
    { NALU_TYPE_STSA_N,        "stsa_n"        },
    { NALU_TYPE_STSA_R,        "stsa_r"        },
    { NALU_TYPE_RADL_N,        "radl_n"        },
    { NALU_TYPE_RADL_R,        "radl_r"        },
    { NALU_TYPE_RASL_N,        "rasl_n"        },
    { NALU_TYPE_RASL_R,        "rasl_r"        },
    { NALU_TYPE_RSV_VCL_N10,   "rsv_vcl_n10"   },
    { NALU_TYPE_RSV_VCL_N12,   "rsv_vcl_n12"   },
    { NALU_TYPE_RSV_VCL_N14,   "rsv_vcl_n14"   },
    { NALU_TYPE_RSV_VCL_R11,   "rsv_vcl_r11"   },
    { NALU_TYPE_RSV_VCL_R13,   "rsv_vcl_r13"   },
    { NALU_TYPE_RSV_VCL_R15,   "rsv_vcl_r15"   },
    { NALU_TYPE_BLA_W_LP,      "bla_w_lp"      },
    { NALU_TYPE_BLA_W_RADL,    "bla_w_radl"    },
    { NALU_TYPE_BLA_N_LP,      "bla_n_lp"      },
    { NALU_TYPE_IDR_W_RADL,    "idr_w_radl"    },
    { NALU_TYPE_IDR_N_LP,      "idr_n_lp"      },
    { NALU_TYPE_CRA_NUT,       "cra_nut"       },
    { NALU_TYPE_RSV_RAP_VCL22, "rsv_rap_vcl22" },
    { NALU_TYPE_RSV_RAP_VCL23, "rsv_rap_vcl23" },
    { NALU_TYPE_RSV_VCL24,     "rsv_vcl24"     },
    { NALU_TYPE_RSV_VCL25,     "rsv_vcl25"     },
    { NALU_TYPE_RSV_VCL26,     "rsv_vcl26"     },
    { NALU_TYPE_RSV_VCL27,     "rsv_vcl27"     },
    { NALU_TYPE_RSV_VCL28,     "rsv_vcl28"     },
    { NALU_TYPE_RSV_VCL29,     "rsv_vcl29"     },
    { NALU_TYPE_RSV_VCL30,     "rsv_vcl30"     },
    { NALU_TYPE_RSV_VCL31,     "rsv_vcl31"     },
    { NALU_TYPE_VIDEO_PARAM,   "video_param"   },
    { NALU_TYPE_SEQ_PARAM,     "seq_param"     },
    { NALU_TYPE_PIC_PARAM,     "pic_param"     },
    { NALU_TYPE_ACCESS_UNIT,   "access_unit"   },
    { NALU_TYPE_END_OF_SEQ,    "end_of_seq"    },
    { NALU_TYPE_END_OF_STREAM, "end_of_stream" },
    { NALU_TYPE_FILLER_DATA,   "filler_data"   },
    { NALU_TYPE_PREFIX_SEI,    "prefix_sei"    },
    { NALU_TYPE_SUFFIX_SEI,    "suffix_sei"    },
    { NALU_TYPE_RSV_NVCL41,    "rsv_nvcl41"    },
    { NALU_TYPE_RSV_NVCL42,    "rsv_nvcl42"    },
    { NALU_TYPE_RSV_NVCL43,    "rsv_nvcl43"    },
    { NALU_TYPE_RSV_NVCL44,    "rsv_nvcl44"    },
    { NALU_TYPE_RSV_NVCL45,    "rsv_nvcl45"    },
    { NALU_TYPE_RSV_NVCL46,    "rsv_nvcl46"    },
    { NALU_TYPE_RSV_NVCL47,    "rsv_nvcl47"    },
    { NALU_TYPE_UNSPEC48,      "unspec48"      },
    { NALU_TYPE_UNSPEC49,      "unspec49"      },
    { NALU_TYPE_UNSPEC50,      "unspec50"      },
    { NALU_TYPE_UNSPEC51,      "unspec51"      },
    { NALU_TYPE_UNSPEC52,      "unspec52"      },
    { NALU_TYPE_UNSPEC53,      "unspec53"      },
    { NALU_TYPE_UNSPEC54,      "unspec54"      },
    { NALU_TYPE_UNSPEC55,      "unspec55"      },
    { NALU_TYPE_UNSPEC56,      "unspec56"      },
    { NALU_TYPE_UNSPEC57,      "unspec57"      },
    { NALU_TYPE_UNSPEC58,      "unspec58"      },
    { NALU_TYPE_UNSPEC59,      "unspec59"      },
    { NALU_TYPE_UNSPEC60,      "unspec60"      },
    { NALU_TYPE_UNSPEC61,      "unspec61"      },
    { NALU_TYPE_UNSPEC62,      "unspec62"      },
    { NALU_TYPE_UNSPEC63,      "unspec63"      },
  };
}

}                              // namespace mtx::hevc