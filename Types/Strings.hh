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

BytesObject* bytes_new(BytesObject* s, const char* data, ssize_t count);
BytesObject* bytes_concat(const BytesObject* a, const BytesObject* b);
char bytes_at(const BytesObject* s, size_t which);
size_t bytes_length(const BytesObject* s);
bool bytes_contains(const BytesObject* needle, const BytesObject* haystack);
std::string bytes_to_cxx_string(const BytesObject* s);

UnicodeObject* unicode_new(UnicodeObject* s, const wchar_t* data, ssize_t count);
UnicodeObject* unicode_concat(const UnicodeObject* a, const UnicodeObject* b);
wchar_t unicode_at(const UnicodeObject* s, size_t which);
size_t unicode_length(const UnicodeObject* s);
bool unicode_contains(const UnicodeObject* needle, const UnicodeObject* haystack);
std::wstring unicode_to_cxx_wstring(const UnicodeObject* s);

BytesObject* encode_ascii(const UnicodeObject* s);
BytesObject* encode_ascii(const wchar_t* s, ssize_t size = -1);
UnicodeObject* decode_ascii(const BytesObject* s);
UnicodeObject* decode_ascii(const char* s, ssize_t size = -1);
