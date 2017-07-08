#pragma once

#include <string>
#include <unordered_map>

#include "Reference.hh"


// string and bytes objects are null-terminated for convenience (so we can use
// C standard library functions on them). this means that the number of
// allocated characters is actually (count + 1).

struct BytesObject {
  BasicObject basic;

  uint64_t count;
  char data[0];

  BytesObject();
};

struct UnicodeObject {
  BasicObject basic;

  uint64_t count;
  wchar_t data[0];

  UnicodeObject();
};

BytesObject* bytes_new(BytesObject* s, const uint8_t* data, size_t count);
BytesObject* bytes_concat(const BytesObject* a, const BytesObject* b);
uint8_t bytes_at(const BytesObject* s, size_t which);
size_t bytes_length(const BytesObject* s);
bool bytes_contains(const BytesObject* needle, const BytesObject* haystack);
// BytesObject uses basic_remove_reference

UnicodeObject* unicode_new(UnicodeObject* s, const wchar_t* data, size_t count);
UnicodeObject* unicode_concat(const UnicodeObject* a, const UnicodeObject* b);
wchar_t unicode_at(const UnicodeObject* s, size_t which);
size_t unicode_length(const UnicodeObject* s);
bool unicode_contains(const UnicodeObject* needle, const UnicodeObject* haystack);
// UnicodeObject uses basic_remove_reference
