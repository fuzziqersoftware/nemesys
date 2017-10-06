#pragma once

#include <string>
#include <unordered_map>

#include "../Exception.hh"
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

BytesObject* bytes_new(BytesObject* s, const char* data, ssize_t count,
    ExceptionBlock* exc_block = NULL);
BytesObject* bytes_concat(const BytesObject* a, const BytesObject* b,
    ExceptionBlock* exc_block = NULL);
char bytes_at(const BytesObject* s, size_t which,
    ExceptionBlock* exc_block = NULL);
size_t bytes_length(const BytesObject* s);
bool bytes_equal(const BytesObject* a, const BytesObject* b);
int64_t bytes_compare(const BytesObject* a, const BytesObject* b);
bool bytes_contains(const BytesObject* needle, const BytesObject* haystack);
std::string bytes_to_cxx_string(const BytesObject* s);

UnicodeObject* unicode_new(UnicodeObject* s, const wchar_t* data, ssize_t count,
    ExceptionBlock* exc_block = NULL);
UnicodeObject* unicode_concat(const UnicodeObject* a, const UnicodeObject* b,
    ExceptionBlock* exc_block = NULL);
wchar_t unicode_at(const UnicodeObject* s, size_t which,
    ExceptionBlock* exc_block = NULL);
size_t unicode_length(const UnicodeObject* s);
bool unicode_equal(const UnicodeObject* a, const UnicodeObject* b);
int64_t unicode_compare(const UnicodeObject* a, const UnicodeObject* b);
bool unicode_contains(const UnicodeObject* needle, const UnicodeObject* haystack);
std::wstring unicode_to_cxx_wstring(const UnicodeObject* s);

BytesObject* unicode_encode_ascii(const UnicodeObject* s);
BytesObject* unicode_encode_ascii(const wchar_t* s, ssize_t size = -1);
UnicodeObject* bytes_decode_ascii(const BytesObject* s);
UnicodeObject* bytes_decode_ascii(const char* s, ssize_t size = -1);
