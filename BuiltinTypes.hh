#pragma once

#include <string>
#include <unordered_map>

#include "Environment.hh"

struct BytesObject {
  std::atomic<uint64_t> refcount;
  uint64_t size;
  char data[0];
};

struct UnicodeObject {
  std::atomic<uint64_t> refcount;
  uint64_t size;
  wchar_t data[0];
};
