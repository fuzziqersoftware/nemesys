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



BytesObject* bytes_new(BytesObject* b, const uint8_t* data, size_t count) {
  if (!b) {
    size_t size = sizeof(BytesObject) + sizeof(uint8_t) * count;
    b = reinterpret_cast<BytesObject*>(malloc(size));
    if (!b) {
      return NULL;
    }
  }
  b->refcount = 1;
  b->count = count;
  if (data) {
    memcpy(b->data, data, sizeof(uint8_t) * count);
  }
  return b;
}

BytesObject* bytes_concat(const BytesObject* a, const BytesObject* b) {
  uint64_t count = a->count + b->count;
  BytesObject* s = bytes_new(NULL, NULL, count);
  if (!s) {
    return NULL;
  }
  memcpy(s->data, a->data, sizeof(uint8_t) * a->count);
  memcpy(&s->data[a->count], b->data, sizeof(uint8_t) * b->count);
  return s;
}

bool bytes_contains(const BytesObject* needle, const BytesObject* haystack) {
  if (haystack->count < needle->count) {
    return false;
  }
  return memmem(haystack->data, haystack->count * sizeof(uint8_t),
      needle->data, needle->count * sizeof(uint8_t));
}

UnicodeObject* unicode_new(UnicodeObject* u, const wchar_t* data, size_t count) {
  if (!u) {
    size_t size = sizeof(UnicodeObject) + sizeof(wchar_t) * count;
    u = reinterpret_cast<UnicodeObject*>(malloc(size));
    if (!u) {
      return NULL;
    }
  }
  u->refcount = 1;
  u->count = count;
  if (data) {
    memcpy(u->data, data, sizeof(wchar_t) * count);
  }
  return u;
}

UnicodeObject* unicode_concat(const UnicodeObject* a, const UnicodeObject* b) {
  uint64_t count = a->count + b->count;
  UnicodeObject* u = unicode_new(NULL, NULL, count);
  if (!u) {
    return NULL;
  }
  memcpy(u->data, a->data, sizeof(wchar_t) * a->count);
  memcpy(&u->data[a->count], b->data, sizeof(wchar_t) * b->count);
  return u;
}

bool unicode_contains(const UnicodeObject* needle, const UnicodeObject* haystack) {
  if (haystack->count < needle->count) {
    return false;
  }
  return memmem(haystack->data, haystack->count * sizeof(wchar_t),
      needle->data, needle->count * sizeof(wchar_t));
}
