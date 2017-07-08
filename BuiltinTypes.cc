#include "BuiltinTypes.hh"

#include <stdlib.h>

using namespace std;



void* add_reference(void* o) {
  std::atomic<uint64_t>* refcount = reinterpret_cast<std::atomic<uint64_t>*>(o);
  (*refcount)++;
  return o;
}

void basic_remove_reference(void* o) {
  std::atomic<uint64_t>* refcount = reinterpret_cast<std::atomic<uint64_t>*>(o);
  if (--(*refcount) == 0) {
    free(o);
  }
}



BytesObject::BytesObject() : refcount(1), count(0) { }
UnicodeObject::UnicodeObject() : refcount(1), count(0) { }



BytesObject* bytes_new(BytesObject* s, const uint8_t* data, size_t count) {
  if (!s) {
    size_t size = sizeof(BytesObject) + sizeof(uint8_t) * (count + 1);
    s = reinterpret_cast<BytesObject*>(malloc(size));
    if (!s) {
      return NULL;
    }
  }
  s->refcount = 1;
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

bool bytes_contains(const BytesObject* needle, const BytesObject* haystack) {
  if (haystack->count < needle->count) {
    return false;
  }
  return memmem(haystack->data, haystack->count * sizeof(uint8_t),
      needle->data, needle->count * sizeof(uint8_t));
}

UnicodeObject* unicode_new(UnicodeObject* s, const wchar_t* data, size_t count) {
  if (!s) {
    size_t size = sizeof(UnicodeObject) + sizeof(wchar_t) * (count + 1);
    s = reinterpret_cast<UnicodeObject*>(malloc(size));
    if (!s) {
      return NULL;
    }
  }
  s->refcount = 1;
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

bool unicode_contains(const UnicodeObject* needle, const UnicodeObject* haystack) {
  if (haystack->count < needle->count) {
    return false;
  }
  return memmem(haystack->data, haystack->count * sizeof(wchar_t),
      needle->data, needle->count * sizeof(wchar_t));
}
