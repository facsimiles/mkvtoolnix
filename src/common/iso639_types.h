/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   ISO 639 types

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include <string>
#include <vector>

namespace mtx::iso639 {

struct language_t {
  std::string const english_name, alpha_3_code, alpha_2_code, alpha_3_bib_code;
  bool is_part_of_iso639_2{}, is_deprecated{};

  language_t(std::string &&p_english_name, std::string &&p_alpha_3_code, std::string &&p_alpha_2_code, std::string &&p_alpha_3_bib_code, bool p_is_part_of_iso639_2, bool p_is_deprecated)
    : english_name{std::move(p_english_name)}
    , alpha_3_code{std::move(p_alpha_3_code)}
    , alpha_2_code{std::move(p_alpha_2_code)}
    , alpha_3_bib_code{std::move(p_alpha_3_bib_code)}
    , is_part_of_iso639_2{p_is_part_of_iso639_2}
    , is_deprecated{p_is_deprecated}
  {
  }
};

extern std::vector<language_t> g_languages;

} // namespace mtx::iso639
