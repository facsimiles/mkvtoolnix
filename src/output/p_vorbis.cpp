/*
  mkvmerge -- utility for splicing together matroska files
      from component media subtypes

  p_vorbis.h

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

/*!
    \file
    \version $Id$
    \brief Vorbis packetizer
    \author Moritz Bunkus <moritz@bunkus.org>
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(COMP_MSC)
#include <unistd.h>
#endif

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "common.h"
#include "pr_generic.h"
#include "p_vorbis.h"
#include "matroska.h"
#include "mkvmerge.h"

using namespace libmatroska;

vorbis_packetizer_c::vorbis_packetizer_c(generic_reader_c *nreader,
                                         unsigned char *d_header,
                                         int l_header,
                                         unsigned char *d_comments,
                                         int l_comments,
                                         unsigned char *d_codecsetup,
                                         int l_codecsetup,
                                         track_info_c *nti)
  throw (error_c):
  generic_packetizer_c(nreader, nti) {
  int i;

  packetno = 0;
  last_bs = 0;
  samples = 0;
  memset(headers, 0, 3 * sizeof(ogg_packet));
  headers[0].packet = (unsigned char *)safememdup(d_header, l_header);
  headers[1].packet = (unsigned char *)safememdup(d_comments, l_comments);
  headers[2].packet = (unsigned char *)safememdup(d_codecsetup, l_codecsetup);
  headers[0].bytes = l_header;
  headers[1].bytes = l_comments;
  headers[2].bytes = l_codecsetup;
  headers[0].b_o_s = 1;
  headers[1].packetno = 1;
  headers[2].packetno = 2;
  vorbis_info_init(&vi);
  vorbis_comment_init(&vc);
  for (i = 0; i < 3; i++)
    if (vorbis_synthesis_headerin(&vi, &vc, &headers[i]) < 0)
      throw error_c("Error: vorbis_packetizer: Could not extract the "
                    "stream's parameters from the first packets.\n");

  set_track_type(track_audio);
  if (use_durations)
    set_track_default_duration((int64_t)(1024000000000.0 *
                                            ti->async.linear / vi.rate));
  ti->async.displacement = initial_displacement;
}

vorbis_packetizer_c::~vorbis_packetizer_c() {
  int i;

  for (i = 0; i < 3; i++)
    if (headers[i].packet != NULL)
      safefree(headers[i].packet);
  vorbis_info_clear(&vi);
  vorbis_comment_clear(&vc);
}

void
vorbis_packetizer_c::set_headers() {
  unsigned char *buffer;
  int n, offset, i, lsize;

  set_codec_id(MKV_A_VORBIS);

  // We use lacing for the blocks. The first bytes is the number of
  // packets being laced which is one less than the number of blocks that
  // are actually stored. For each packet in the lace there's the length
  // coded like this:
  // length = 0
  // while (next byte == 255) { length += 255 }
  // length += this byte which is < 255
  // The last packet's length can be calculated by the length of
  // the KaxCodecPrivate and all prior packets, so there's no length for it -
  // and that's why the first byte is (num_packets - 1).
  lsize = 1 + (headers[0].bytes / 255) + 1 + (headers[1].bytes / 255) + 1 +
    headers[0].bytes + headers[1].bytes + headers[2].bytes;
  buffer = (unsigned char *)safemalloc(lsize);

  buffer[0] = 2;                // The number of packets less one.
  offset = 1;
  for (i = 0; i < 2; i++) {
    for (n = headers[i].bytes; n >= 255; n -= 255) {
      buffer[offset] = 255;
      offset++;
    }
    buffer[offset] = n;
    offset++;
  }
  for (i = 0; i <= 2; i++) {
    memcpy(&buffer[offset], headers[i].packet, headers[i].bytes);
    offset += headers[i].bytes;
  }

  set_codec_private(buffer, lsize);

  safefree(buffer);

  set_audio_sampling_freq((float)vi.rate);
  set_audio_channels(vi.channels);

  generic_packetizer_c::set_headers();
}

/*
 * Some notes - processing is straight-forward if no AV synchronization
 * is needed - the packet is simply stored in the Matroska file.
 * Unfortunately things are not that easy if AV sync is done. For a
 * negative displacement packets are simply discarded if their timecode
 * is set before the displacement. For positive displacements the packetizer
 * has to generate silence packets and put them into the Matroska file first.
 */
int
vorbis_packetizer_c::process(memory_c &mem,
                             int64_t timecode,
                             int64_t,
                             int64_t,
                             int64_t) {
  unsigned char zero[2];
  ogg_packet op;
  int64_t this_bs, samples_here, samples_needed;

  debug_enter("vorbis_packetizer_c::process");

  // Positive displacement, first packet? Well then lets create silence.
  if ((packetno == 0) && (initial_displacement > 0)) {
    // Create a fake packet so we can use vorbis_packet_blocksize().
    zero[0] = 0;
    zero[1] = 0;
    memset(&op, 0, sizeof(ogg_packet));
    op.packet = zero;
    op.bytes = 2;

    // Calculate how many samples we have to create.
    samples_needed = vi.rate * initial_displacement / 1000000000;

    this_bs = vorbis_packet_blocksize(&vi, &op);
    samples_here = (this_bs + last_bs) / 4;
    while ((samples + samples_here) < samples_needed) {
      samples += samples_here;
      last_bs = this_bs;
      samples_here = (this_bs + last_bs) / 4;
      memory_c mem(zero, 2, false);
      add_packet(mem, samples * 1000000000 / vi.rate,
                 samples_here * 1000000000 / vi.rate);
    }
  }

  // Recalculate the timecode if needed.
  if (timecode == -1) {
    if (initial_displacement > 0)
      timecode = samples * 1000000000 / vi.rate;
    else
      timecode = samples * 1000000000 / vi.rate + initial_displacement;
  } else
    timecode += initial_displacement;

  // Handle the linear sync - simply multiply with the given factor.
  timecode = (int64_t)((double)timecode * ti->async.linear);

  // Update the number of samples we have processed so that we can
  // calculate the timecode on the next call.
  op.packet = mem.data;
  op.bytes = mem.size;
  this_bs = vorbis_packet_blocksize(&vi, &op);
  samples_here = (this_bs + last_bs) / 4;
  samples += samples_here;
  last_bs = this_bs;

  // If a negative sync value was used we may have to skip this packet.
  if (timecode < 0) {
    debug_leave("vorbis_packetizer_c::process");
    return EMOREDATA;
  }

  mxverb(2, "Vorbis: samples_here: %lld\n", samples_here);
  add_packet(mem, (int64_t)timecode,
             (int64_t)(samples_here * 1000000000 * ti->async.linear /
                       vi.rate));

  debug_leave("vorbis_packetizer_c::process");

  return EMOREDATA;
}

void
vorbis_packetizer_c::dump_debug_info() {
  mxdebug("vorbis_packetizer_c: queue: %d\n", packet_queue.size());
}

int
vorbis_packetizer_c::can_connect_to(generic_packetizer_c *src) {
  vorbis_packetizer_c *vsrc;

  vsrc = dynamic_cast<vorbis_packetizer_c *>(src);
  if (vsrc == NULL)
    return CAN_CONNECT_NO_FORMAT;
  if ((vi.rate != vsrc->vi.rate) || (vi.channels != vsrc->vi.channels) ||
      (headers[0].bytes != vsrc->headers[0].bytes) ||
      (headers[1].bytes != vsrc->headers[1].bytes) ||
      (headers[2].bytes != vsrc->headers[2].bytes) ||
      (headers[0].packet == NULL) ||
      (headers[1].packet == NULL) ||
      (headers[2].packet == NULL) ||
      (vsrc->headers[0].packet == NULL) ||
      (vsrc->headers[1].packet == NULL) ||
      (vsrc->headers[2].packet == NULL) ||
      memcmp(headers[0].packet, vsrc->headers[0].packet, headers[0].bytes) ||
      memcmp(headers[1].packet, vsrc->headers[1].packet, headers[1].bytes) ||
      memcmp(headers[2].packet, vsrc->headers[2].packet, headers[2].bytes))
    return CAN_CONNECT_NO_PARAMETERS;
  return CAN_CONNECT_YES;
}
