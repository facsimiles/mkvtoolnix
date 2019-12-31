/*
   mkvinfo -- utility for gathering information about Matroska files

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   retrieves and displays information about a Matroska file

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/command_line.h"
#include "common/kax_info.h"
#include "common/version.h"
#include "info/info_cli_parser.h"

void
setup(char const *argv0) {
  mtx_common_init("mkvinfo", argv0);
  mtx::cli::g_version_info = get_version_info("mkvinfo", vif_full);
}

int
main(int argc,
     char **argv) {
  setup(argv[0]);

  auto options = info_cli_parser_c(mtx::cli::args_in_utf8(argc, argv)).run();

  set_process_priority(-1);

  if (options.m_file_name.empty())
    mxerror(Y("No file name given.\n"));

  mtx::kax_info_c info;

  info.set_calc_checksums(options.m_calc_checksums);
  info.set_show_summary(options.m_show_summary);
  info.set_show_hexdump(options.m_show_hexdump);
  info.set_show_size(options.m_show_size);
  info.set_show_track_info(options.m_show_track_info);
  info.set_hex_positions(options.m_hex_positions);
  info.set_hexdump_max_size(options.m_hexdump_max_size);
  info.set_verbosity(options.m_verbose);

  if (options.m_hex_positions)
    info.set_show_positions(true);

  try {
    info.open_and_process_file(options.m_file_name);
  } catch (mtx::kax_info::exception &ex) {
    mxinfo(ex.what());
    mxexit(2);
  }

  mxexit();

  return 0;
}
