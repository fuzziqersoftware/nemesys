#include "Strings.hh"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Strings.hh>

#include "../Debug.hh"
#include "../Compiler/Exception.hh"
#include "../Compiler/BuiltinFunctions.hh"

using namespace std;



extern shared_ptr<GlobalContext> global;

BytesObject::BytesObject() : basic(free), count(0) { }

BytesObject* bytes_new(const char* data, ssize_t count,
    ExceptionBlock* exc_block) {
  if (count < 0) {
    count = strlen(data);
  }

  size_t size = sizeof(BytesObject) + sizeof(char) * (count + 1);
  BytesObject* s = reinterpret_cast<BytesObject*>(malloc(size));
  if (!s) {
    raise_python_exception(exc_block, &MemoryError_instance);
    throw bad_alloc();
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

BytesObject* bytes_from_cxx_string(const string& data) {
  return bytes_new(data.data(), data.size());
}

BytesObject* bytes_concat(const BytesObject* a, const BytesObject* b,
    ExceptionBlock* exc_block) {
  uint64_t count = a->count + b->count;
  BytesObject* s = bytes_new(NULL, count, exc_block);
  memcpy(s->data, a->data, sizeof(char) * a->count);
  memcpy(&s->data[a->count], b->data, sizeof(char) * b->count);
  s->data[s->count] = 0;
  return s;
}

char bytes_at(const BytesObject* s, size_t which,
    ExceptionBlock* exc_block) {
  if (which >= s->count) {
    raise_python_exception_with_message(exc_block, global->IndexError_class_id,
        "bytes index out of range");
    throw out_of_range("index out of range for bytes object");
  }
  return s->data[which];
}

size_t bytes_length(const BytesObject* s) {
  return s->count;
}

bool bytes_equal(const BytesObject* a, const BytesObject* b) {
  if (a->count != b->count) {
    return false;
  }
  return !memcmp(a->data, b->data, a->count * sizeof(a->data[0]));
}

int64_t bytes_compare(const BytesObject* a, const BytesObject* b) {
  for (size_t x = 0; (x < a->count) && (x < b->count); x++) {
    if (a->data[x] < b->data[x]) {
      return -1;
    }
    if (a->data[x] > b->data[x]) {
      return 1;
    }
  }
  if (a->count == b->count) {
    return 0;
  }
  if (a->count < b->count) {
    return -1;
  }
  return 1;
}

bool bytes_contains(const BytesObject* haystack, const BytesObject* needle) {
  if (needle->count == 0) {
    return true;
  }
  return !!memmem(haystack->data, haystack->count * sizeof(char),
      needle->data, needle->count * sizeof(char));
}

string bytes_to_cxx_string(const BytesObject* s) {
  return string(reinterpret_cast<const char*>(s->data), s->count);
}



UnicodeObject::UnicodeObject() : basic(free), count(0) { }

UnicodeObject* unicode_new(const wchar_t* data, ssize_t count,
    ExceptionBlock* exc_block) {
  if (count < 0) {
    count = wcslen(data);
  }
  size_t size = sizeof(UnicodeObject) + sizeof(wchar_t) * (count + 1);
  UnicodeObject* s = reinterpret_cast<UnicodeObject*>(malloc(size));
  if (!s) {
    raise_python_exception(exc_block, &MemoryError_instance);
    throw bad_alloc();
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

UnicodeObject* unicode_from_cxx_wstring(const wstring& data) {
  return unicode_new(data.data(), data.size());
}

UnicodeObject* unicode_concat(const UnicodeObject* a, const UnicodeObject* b,
    ExceptionBlock* exc_block) {
  uint64_t count = a->count + b->count;
  UnicodeObject* s = unicode_new(NULL, count, exc_block);
  memcpy(s->data, a->data, sizeof(wchar_t) * a->count);
  memcpy(&s->data[a->count], b->data, sizeof(wchar_t) * b->count);
  s->data[s->count] = 0;
  return s;
}

wchar_t unicode_at(const UnicodeObject* s, size_t which,
    ExceptionBlock* exc_block) {
  if (which >= s->count) {
    raise_python_exception_with_message(exc_block, global->IndexError_class_id,
        "unicode index out of range");
    throw out_of_range("index out of range for unicode object");
  }
  return s->data[which];
}

size_t unicode_length(const UnicodeObject* s) {
  return s->count;
}

bool unicode_equal(const UnicodeObject* a, const UnicodeObject* b) {
  if (a->count != b->count) {
    return false;
  }
  return !memcmp(a->data, b->data, a->count * sizeof(a->data[0]));
}

int64_t unicode_compare(const UnicodeObject* a, const UnicodeObject* b) {
  for (size_t x = 0; (x < a->count) && (x < b->count); x++) {
    if (a->data[x] < b->data[x]) {
      return -1;
    }
    if (a->data[x] > b->data[x]) {
      return 1;
    }
  }
  if (a->count == b->count) {
    return 0;
  }
  if (a->count < b->count) {
    return -1;
  }
  return 1;
}

bool unicode_contains(const UnicodeObject* haystack, const UnicodeObject* needle) {
  if (needle->count == 0) {
    return true;
  }
  if (memmem(haystack->data, haystack->count * sizeof(wchar_t),
      needle->data, needle->count * sizeof(wchar_t)) != NULL) {
    return true;
  } else {
    return false;
  }
}

wstring unicode_to_cxx_wstring(const UnicodeObject* s) {
  return wstring(s->data, s->count);
}



BytesObject* unicode_encode_ascii(const UnicodeObject* s) {
  return unicode_encode_ascii(s->data, s->count);
}

BytesObject* unicode_encode_ascii(const wchar_t* s, ssize_t count) {
  if (count < 0) {
    count = wcslen(s);
  }
  BytesObject* ret = bytes_new(NULL, count);
  for (int64_t x = 0; x < count; x++) {
    ret->data[x] = s[x];
  }
  ret->data[count] = 0;
  return ret;
}

UnicodeObject* bytes_decode_ascii(const BytesObject* s) {
  return bytes_decode_ascii(s->data, s->count);
}

UnicodeObject* bytes_decode_ascii(const char* s, ssize_t count) {
  if (count < 0) {
    count = strlen(s);
  }
  UnicodeObject* ret = unicode_new(NULL, count);
  for (int64_t x = 0; x < count; x++) {
    ret->data[x] = s[x];
  }
  ret->data[count] = 0;
  return ret;
}
