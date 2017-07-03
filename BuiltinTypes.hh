#pragma once

#include <string>
#include <unordered_map>

#include "Environment.hh"

struct BytesObject {
  std::atomic<uint64_t> refcount;
  uint64_t count;
  char data[0];
};

struct UnicodeObject {
  std::atomic<uint64_t> refcount;
  uint64_t count;
  wchar_t data[0];
};

BytesObject* bytes_new(BytesObject* b, uint8_t* data, size_t count);
UnicodeObject* unicode_new(UnicodeObject* u, wchar_t* data, size_t count);
