#include "Tuple.hh"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Strings.hh>

#include "../BuiltinFunctions.hh"

using namespace std;



void** TupleObject::items() {
  return reinterpret_cast<void**>(&this->data[0]);
}

void* const * TupleObject::items() const {
  return reinterpret_cast<void* const *>(&this->data[0]);
}

uint8_t* TupleObject::has_refcount_map() {
  return reinterpret_cast<uint8_t*>(&this->data[this->count]);
}

const uint8_t* TupleObject::has_refcount_map() const {
  return reinterpret_cast<const uint8_t*>(&this->data[this->count]);
}



TupleObject* tuple_new(uint64_t count, ExceptionBlock* exc_block) {
  TupleObject* t = reinterpret_cast<TupleObject*>(malloc(
      sizeof(TupleObject) + (count * sizeof(void*)) + ((count + 7) / 8)));
  if (!t) {
    raise_python_exception(exc_block, &MemoryError_instance);
    throw bad_alloc();
  }
  t->basic.refcount = 1;
  t->basic.destructor = reinterpret_cast<void (*)(void*)>(tuple_delete);
  t->count = count;

  // clear the data and has_refcount map
  for (size_t x = 0; x < t->count; x++) {
    t->data[x] = 0;
  }
  memset(t->has_refcount_map(), 0, (t->count + 7) / 8);

  return t;
}

void tuple_delete(TupleObject* t) {
  uint8_t* has_refcount_map = t->has_refcount_map();
  for (size_t x = 0; x < t->count; x++) {
    if (has_refcount_map[x / 8] & (0x80 >> (x & 7))) {
      delete_reference(t->data[x]);
    }
  }
  free(t);
}



void* tuple_get_item(const TupleObject* t, int64_t position,
    ExceptionBlock* exc_block) {
  if (position < 0) {
    position += t->count;
  }
  if ((position < 0) || (position >= static_cast<ssize_t>(t->count))) {
    raise_python_exception(exc_block, create_instance(IndexError_class_id));
    throw out_of_range("index out of range for tuple object");
  }

  // return a new reference to ret if it's an object
  void* ret = t->items()[position];
  if (t->has_refcount_map()[position / 8] & (0x80 >> (position & 7))) {
    add_reference(ret);
  }
  return ret;
}

void tuple_set_item(TupleObject* t, int64_t position, void* value,
    bool has_refcount, ExceptionBlock* exc_block) {
  if (position < 0) {
    position += t->count;
  }
  if ((position < 0) || (position >= static_cast<ssize_t>(t->count))) {
    raise_python_exception(exc_block, create_instance(IndexError_class_id));
    throw out_of_range("index out of range for tuple object");
  }

  // delete the previous object (if it was an object) and set the refcount
  // bitmap correctly
  uint8_t* has_refcount_byte = &t->has_refcount_map()[position / 8];
  if (*has_refcount_byte & (0x80 >> (position & 7))) {
    delete_reference(t->items()[position]);

    if (!has_refcount) {
      *has_refcount_byte &= ~(0x80 >> (position & 7));
    }
  } else {
    if (has_refcount) {
      *has_refcount_byte |= (0x80 >> (position & 7));
    }
  }

  t->items()[position] = value;

  // the input reference was a borrowed reference so we can't just move it into
  // the tuple
  if (has_refcount) {
    add_reference(value);
  }
}

size_t tuple_size(const TupleObject* t) {
  return t->count;
}
