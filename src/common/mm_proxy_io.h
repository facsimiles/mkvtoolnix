/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   IO callback class definitions

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include "common/common_pch.h"

class mm_proxy_io_private_c;
class mm_proxy_io_c: public mm_io_c {
protected:
  MTX_DECLARE_PRIVATE(mm_proxy_io_private_c)

  explicit mm_proxy_io_c(mm_proxy_io_private_c &p);

public:
  mm_proxy_io_c(mm_io_cptr const &proxy_io);
  virtual ~mm_proxy_io_c();

  virtual void setFilePointer(int64 offset, libebml::seek_mode mode=libebml::seek_beginning);
  virtual uint64 getFilePointer();
  virtual void clear_eof();
  virtual bool eof();
  virtual void close();
  virtual std::string get_file_name() const;
  virtual mm_io_c *get_proxied() const;

protected:
  virtual uint32 _read(void *buffer, size_t size);
  virtual size_t _write(const void *buffer, size_t size);

  void close_proxy_io();
};
