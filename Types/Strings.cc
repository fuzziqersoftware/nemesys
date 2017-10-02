#include "Strings.hh"

#include <inttypes.h>
#include <stdlib.h>

#include <phosg/Strings.hh>

#include "../Debug.hh"

using namespace std;



BytesObject::BytesObject() : basic(free), count(0) { }

BytesObject* bytes_new(BytesObject* s, const char* data, ssize_t count) {
  if (count < 0) {
    count = strlen(data);
  }
  if (!s) {
    size_t size = sizeof(BytesObject) + sizeof(char) * (count + 1);
    s = reinterpret_cast<BytesObject*>(malloc(size));
    if (!s) {
      return NULL;
    }
  }
  s->basic.refcount = 1;
  s->basic.destructor = free;
  s->count = count;
  if (data) {
    memcpy(s->data, data, sizeof(char) * count);
    s->data[s->count] = 0;
    if (debug_flags & DebugFlag::ShowRefcountChanges) {
      fprintf(stderr, "[refcount:create] created Bytes object %p: %s\n",
          s, data);
    }
  } else if (debug_flags & DebugFlag::ShowRefcountChanges) {
    fprintf(stderr, "[refcount:create] created Bytes object %p with %zd bytes\n",
        s, count);
  }
  return s;
}

BytesObject* bytes_concat(const BytesObject* a, const BytesObject* b) {
  uint64_t count = a->count + b->count;
  BytesObject* s = bytes_new(NULL, NULL, count);
  if (!s) {
    return NULL;
  }
  memcpy(s->data, a->data, sizeof(char) * a->count);
  memcpy(&s->data[a->count], b->data, sizeof(char) * b->count);
  s->data[s->count] = 0;
  return s;
}

char bytes_at(const BytesObject* s, size_t which) {
  if (which >= s->count) {
    return 0;
  }
  return s->data[which];
}

size_t bytes_length(const BytesObject* s) {
  return s->count;
}

bool bytes_contains(const BytesObject* haystack, const BytesObject* needle) {
  if (needle->count == 0) {
    return true;
  }
  return memmem(haystack->data, haystack->count * sizeof(char),
      needle->data, needle->count * sizeof(char));
}

string bytes_to_cxx_string(const BytesObject* s) {
  return string(reinterpret_cast<const char*>(s->data), s->count);
}



UnicodeObject::UnicodeObject() : basic(free), count(0) { }

UnicodeObject* unicode_new(UnicodeObject* s, const wchar_t* data, ssize_t count) {
  if (count < 0) {
    count = wcslen(data);
  }
  if (!s) {
    size_t size = sizeof(UnicodeObject) + sizeof(wchar_t) * (count + 1);
    s = reinterpret_cast<UnicodeObject*>(malloc(size));
    if (!s) {
      return NULL;
    }
  }
  s->basic.refcount = 1;
  s->basic.destructor = free;
  s->count = count;
  if (data) {
    memcpy(s->data, data, sizeof(wchar_t) * count);
    s->data[s->count] = 0;
    if (debug_flags & DebugFlag::ShowRefcountChanges) {
      fprintf(stderr, "[refcount:create] created Unicode object %p: %ls\n",
          s, data);
    }
  } else if (debug_flags & DebugFlag::ShowRefcountChanges) {
    fprintf(stderr, "[refcount:create] created Unicode object %p with %zd chars\n",
        s, count);
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
  if (needle->count == 0) {
    return true;
  }
  return memmem(haystack->data, haystack->count * sizeof(wchar_t),
      needle->data, needle->count * sizeof(wchar_t));
}

wstring unicode_to_cxx_wstring(const UnicodeObject* s) {
  return wstring(s->data, s->count);
}



BytesObject* encode_ascii(const UnicodeObject* s) {
  return encode_ascii(s->data, s->count);
}

BytesObject* encode_ascii(const wchar_t* s, ssize_t count) {
  if (count < 0) {
    count = wcslen(s);
  }
  BytesObject* ret = bytes_new(NULL, NULL, count);
  for (int64_t x = 0; x < count; x++) {
    ret->data[x] = s[x];
  }
  ret->data[count] = 0;
  return ret;
}

UnicodeObject* decode_ascii(const BytesObject* s) {
  return decode_ascii(s->data, s->count);
}

UnicodeObject* decode_ascii(const char* s, ssize_t count) {
  if (count < 0) {
    count = strlen(s);
  }
  UnicodeObject* ret = unicode_new(NULL, NULL, count);
  for (int64_t x = 0; x < count; x++) {
    ret->data[x] = s[x];
  }
  ret->data[count] = 0;
  return ret;
}
