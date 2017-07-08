#pragma once

#include <string>
#include <unordered_map>

#include "Environment.hh"


// for convenience, add_reference returns o (so callers don't have to push it
// onto the stack to keep it)
void* add_reference(void* o);
void basic_remove_reference(void* o);



// string and bytes objects are null-terminated for convenience (so we can use
// C standard library functions on them). this means that the number of
// allocated characters is actually (count + 1).

struct BytesObject {
  std::atomic<uint64_t> refcount;
  uint64_t count;
  char data[0];

  BytesObject();
};

struct UnicodeObject {
  std::atomic<uint64_t> refcount;
  uint64_t count;
  wchar_t data[0];

  UnicodeObject();
};

BytesObject* bytes_new(BytesObject* b, const uint8_t* data, size_t count);
BytesObject* bytes_concat(const BytesObject* a, const BytesObject* b);
bool bytes_contains(const BytesObject* needle, const BytesObject* haystack);
// BytesObject uses basic_decref

UnicodeObject* unicode_new(UnicodeObject* u, const wchar_t* data, size_t count);
UnicodeObject* unicode_concat(const UnicodeObject* a, const UnicodeObject* b);
bool unicode_contains(const UnicodeObject* needle, const UnicodeObject* haystack);
// UnicodeObject uses basic_decref
