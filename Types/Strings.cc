#include "Strings.hh"

#include <inttypes.h>
#include <stdlib.h>

#include <phosg/Strings.hh>

using namespace std;



BytesObject::BytesObject() : basic(free), count(0) { }

BytesObject* bytes_new(BytesObject* s, const uint8_t* data, size_t count) {
  if (!s) {
    size_t size = sizeof(BytesObject) + sizeof(uint8_t) * (count + 1);
    s = reinterpret_cast<BytesObject*>(malloc(size));
    if (!s) {
      return NULL;
    }
  }
  s->basic.refcount = 1;
  s->basic.destructor = delete_reference;
  s->count = count;
  if (data) {
    memcpy(s->data, data, sizeof(uint8_t) * count);
    s->data[s->count] = 0;
  }
  return s;
}

BytesObject* bytes_concat(const BytesObject* a, const BytesObject* b) {
  uint64_t count = a->count + b->count;
  BytesObject* s = bytes_new(NULL, NULL, count);
  if (!s) {
    return NULL;
  }
  memcpy(s->data, a->data, sizeof(uint8_t) * a->count);
  memcpy(&s->data[a->count], b->data, sizeof(uint8_t) * b->count);
  s->data[s->count] = 0;
  return s;
}

uint8_t bytes_at(const BytesObject* s, size_t which) {
  if (which >= s->count) {
    return 0;
  }
  return s->data[which];
}

size_t bytes_length(const BytesObject* s) {
  return s->count;
}

bool bytes_contains(const BytesObject* haystack, const BytesObject* needle) {
  return memmem(haystack->data, haystack->count * sizeof(uint8_t),
      needle->data, needle->count * sizeof(uint8_t));
}

string bytes_to_cxx_string(const BytesObject* s) {
  return string(s->data, s->count);
}



UnicodeObject::UnicodeObject() : basic(free), count(0) { }

UnicodeObject* unicode_new(UnicodeObject* s, const wchar_t* data, size_t count) {
  if (!s) {
    size_t size = sizeof(UnicodeObject) + sizeof(wchar_t) * (count + 1);
    s = reinterpret_cast<UnicodeObject*>(malloc(size));
    if (!s) {
      return NULL;
    }
  }
  s->basic.refcount = 1;
  s->basic.destructor = delete_reference;
  s->count = count;
  if (data) {
    memcpy(s->data, data, sizeof(wchar_t) * count);
    s->data[s->count] = 0;
  }
  return s;
}

UnicodeObject* unicode_concat(const UnicodeObject* a, const UnicodeObject* b) {
  uint64_t count = a->count + b->count;
  UnicodeObject* s = unicode_new(NULL, NULL, count);
  if (!s) {
    return NULL;
  }
  memcpy(s->data, a->data, sizeof(wchar_t) * a->count);
  memcpy(&s->data[a->count], b->data, sizeof(wchar_t) * b->count);
  s->data[s->count] = 0;
  return s;
}

wchar_t unicode_at(const UnicodeObject* s, size_t which) {
  if (which >= s->count) {
    return 0;
  }
  return s->data[which];
}

size_t unicode_length(const UnicodeObject* s) {
  return s->count;
}

bool unicode_contains(const UnicodeObject* haystack, const UnicodeObject* needle) {
  return memmem(haystack->data, haystack->count * sizeof(wchar_t),
      needle->data, needle->count * sizeof(wchar_t));
}

wstring unicode_to_cxx_wstring(const UnicodeObject* s) {
  return wstring(s->data, s->count);
}
