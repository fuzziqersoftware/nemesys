#include "BuiltinTypes.hh"

#include <stdlib.h>

using namespace std;


BytesObject* __attribute__((sysv_abi))
bytes_new(BytesObject* b, uint8_t* data, size_t count) {
  if (!b) {
    size_t size = sizeof(BytesObject) + sizeof(uint8_t) * count;
    b = reinterpret_cast<BytesObject*>(malloc(size));
    if (!b) {
      return NULL;
    }
  }
  b->refcount = 1;
  b->count = count;
  memcpy(b->data, data, sizeof(uint8_t) * count);
  return b;
}

UnicodeObject* __attribute__((sysv_abi))
unicode_new(UnicodeObject* u, wchar_t* data, size_t count) {
  if (!u) {
    size_t size = sizeof(UnicodeObject) + sizeof(wchar_t) * count;
    u = reinterpret_cast<UnicodeObject*>(malloc(size));
    if (!u) {
      return NULL;
    }
  }
  u->refcount = 1;
  u->count = count;
  memcpy(u->data, data, sizeof(wchar_t) * count);
  return u;
}
