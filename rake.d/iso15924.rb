def create_iso15924_script_list_file
  txt_files     = []
  zip_file      = "iso15924.txt.zip"
  url           = "https://unicode.org/iso15924/#{zip_file}"
  cpp_file_name = "src/common/iso15924_script_list.cpp"

  File.unlink(zip_file) if FileTest.exists?(zip_file)

  runq "wget", url, "wget --quiet -O #{zip_file} #{url}"
  runq "unzip", zip_file, "unzip -o -q #{zip_file}"

  txt_files = FileList["iso15924*.txt"].to_a

  raise "No text file found in download" if txt_files.empty?

  rows = IO.readlines(txt_files[0]).
    map(&:chomp).
    reject { |line| %r{^#}.match(line) }.
    select { |line| %r{;.*;.*;}.match(line) }.
    map    { |line| line.split(';') }.
    map    { |line| [
      (line[0][0..0].upcase + line[0][1..line[0].length].downcase).to_cpp_string,
      sprintf('%03s', line[1].gsub(%r{^0}, '')),
      line[2].to_u8_cpp_string,
    ] }

  header = <<EOT
/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   ISO 15924 script list

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

// -------------------------------------------------------------------------
// NOTE: this file is auto-generated by the "dev:iso15924_list" rake target.
// -------------------------------------------------------------------------

#include "common/common_pch.h"

#include "common/iso15924.h"

namespace mtx::iso15924 {

std::vector<script_t> const g_scripts{
EOT

  footer = <<EOT
};

} // namespace mtx::iso15924
EOT

  content = header + format_table(rows.sort, :column_suffix => ',', :row_prefix => "  { ", :row_suffix => "  },").join("\n") + "\n" + footer

  runq("write", cpp_file_name) { IO.write("#{$source_dir}/#{cpp_file_name}", content); 0 }

ensure
  (txt_files + [ zip_file ]).each do |file_name|
    File.unlink(file_name) if FileTest.exists?(file_name)
  end
end
