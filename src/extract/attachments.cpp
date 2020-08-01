/*
   mkvextract -- extract tracks from Matroska files into other files

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   extracts attachments from Matroska files into other files

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include <cassert>
#include <iostream>

#include <ebml/EbmlHead.h>
#include <ebml/EbmlSubHead.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlVoid.h>
#include <matroska/FileKax.h>

#include <matroska/KaxAttached.h>
#include <matroska/KaxAttachments.h>
#include <matroska/KaxSegment.h>

#include "common/ebml.h"
#include "common/kax_analyzer.h"
#include "common/mm_io_x.h"
#include "common/mm_file_io.h"
#include "extract/mkvextract.h"

using namespace libmatroska;

struct attachment_t {
  std::string name, type;
  uint64_t size, id;
  KaxFileData *fdata;
  bool valid;

  attachment_t()
    : size{std::numeric_limits<uint64_t>::max()}
    , id{}
    , fdata(nullptr)
    , valid(false)
  {
  };

  attachment_t &parse(KaxAttached &att);
  static attachment_t parse_new(KaxAttached &att);
};

attachment_t
attachment_t::parse_new(KaxAttached &att) {
  attachment_t attachment;
  return attachment.parse(att);
}

attachment_t &
attachment_t::parse(KaxAttached &att) {
  size_t k;
  for (k = 0; att.ListSize() > k; ++k) {
    EbmlElement *e = att[k];

    if (Is<KaxFileName>(e))
      name = static_cast<KaxFileName *>(e)->GetValueUTF8();

    else if (Is<KaxMimeType>(e))
      type = static_cast<KaxMimeType *>(e)->GetValue();

    else if (Is<KaxFileUID>(e))
      id = static_cast<KaxFileUID *>(e)->GetValue();

    else if (Is<KaxFileData>(e)) {
      fdata = static_cast<KaxFileData *>(e);
      size  = fdata->GetSize();
    }
  }

  valid = (std::numeric_limits<uint64_t>::max() != size) && !type.empty();

  return *this;
}

static void
handle_attachments(KaxAttachments *atts,
                   std::vector<track_spec_t> &tracks) {
  int64_t attachment_ui_id = 0;
  std::map<int64_t, attachment_t> attachments;

  size_t i;
  for (i = 0; atts->ListSize() > i; ++i) {
    KaxAttached *att = dynamic_cast<KaxAttached *>((*atts)[i]);
    assert(att);

    attachment_t attachment = attachment_t::parse_new(*att);
    if (!attachment.valid)
      continue;

    ++attachment_ui_id;
    attachments[attachment_ui_id] = attachment;
  }

  for (auto &track : tracks) {
    attachment_t attachment = attachments[ track.tid ];

    if (!attachment.valid)
      mxerror(fmt::format(Y("An attachment with the ID {0} was not found.\n"), track.tid));

    // check for output name
    if (track.out_name.empty())
      track.out_name = attachment.name;

    mxinfo(fmt::format(Y("The attachment #{0}, ID {1}, MIME type {2}, size {3}, is written to '{4}'.\n"),
                       track.tid, attachment.id, attachment.type, attachment.size, track.out_name));
    try {
      mm_file_io_c out(track.out_name, MODE_CREATE);
      out.write(attachment.fdata->GetBuffer(), attachment.fdata->GetSize());
    } catch (mtx::mm_io::exception &ex) {
      mxerror(fmt::format(Y("The file '{0}' could not be opened for writing: {1}.\n"), track.out_name, ex));
    }
  }
}

bool
extract_attachments(kax_analyzer_c &analyzer,
                    options_c::mode_options_c &options) {
  if (options.m_tracks.empty())
    return false;

  auto attachments = analyzer.read_all(EBML_INFO(KaxAttachments));
  if (!dynamic_cast<KaxAttachments *>(attachments.get()))
    return false;

  handle_attachments(static_cast<KaxAttachments *>(attachments.get()), options.m_tracks);

  return true;
}
