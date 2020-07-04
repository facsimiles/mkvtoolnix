def create_iana_language_subtag_registry_list_file
  txt_file      = "language-subtag-registry.txt"
  url           = "https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry"
  cpp_file_name = "src/common/iana_language_subtag_registry_list.cpp"

  File.unlink(txt_file) if FileTest.exists?(txt_file)

  runq "wget", url, "wget --quiet -O #{txt_file} #{url}"

  entry   = {}
  entries = {}
  process = lambda do
    type = entry.delete(:type)

    if type
      entries[type] ||= []
      entries[type]  << entry
    end

    entry = {}
  end

  IO.readlines(txt_file).
    map(&:chomp).
    each do |line|

    if line == '%%'
      process.call

    elsif %r{^(Type|Subtag|Description): *(.+)}i.match(line)
      entry[$1.downcase.to_sym] = $2

    elsif %r{^Prefix: *(.+)}i.match(line)
      entry[:prefix] ||= []
      entry[:prefix]  << $1
    end
  end

  process.call

  formatter = lambda do |entry|
    if entry[:prefix]
      prefix = '{ ' + entry[:prefix].sort.map(&:to_cpp_string).join(', ') + ' }'
    else
      prefix = '{}'
    end

    [ entry[:subtag].downcase.to_cpp_string,
      entry[:description].to_u8_cpp_string,
      prefix,
    ]
  end

  rows = entries["extlang"].map(&formatter)

  extlangs = "std::vector<entry_t> const g_extlangs{\n" +
    format_table(rows.sort, :column_suffix => ',', :row_prefix => "  { ", :row_suffix => "  },").join("\n") +
    "\n};\n"

  rows = entries["variant"].map(&formatter)

  variants = "std::vector<entry_t> const g_variants{\n" +
    format_table(rows.sort, :column_suffix => ',', :row_prefix => "  { ", :row_suffix => "  },").join("\n") +
    "\n};\n"

  header = <<EOT
/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   IANA language subtag registry

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

// ----------------------------------------------------------------------------------------------
// NOTE: this file is auto-generated by the "dev:iana_language_subtag_registry_list" rake target.
// ----------------------------------------------------------------------------------------------

#include "common/common_pch.h"

#include "common/iana_language_subtag_registry.h"

namespace mtx::iana::language_subtag_registry {

EOT

  footer = <<EOT

} // namespace mtx::iana::language_subtag_registry
EOT

  content = header +
    extlangs +
    "\n" +
    variants +
    footer

  runq("write", cpp_file_name) { IO.write("#{$source_dir}/#{cpp_file_name}", content); 0 }

ensure
  File.unlink(txt_file) if FileTest.exists?(txt_file)
end
