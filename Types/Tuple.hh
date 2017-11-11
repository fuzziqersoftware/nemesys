#pragma once

#include <stdint.h>

#include <atomic>
#include <string>
#include <vector>

#include "../Exception.hh"
#include "Reference.hh"


struct TupleObject {
  BasicObject basic;
  uint64_t count;
  void* data[0];

  void** items();
  void* const * items() const;
  uint8_t* has_refcount_map();
  const uint8_t* has_refcount_map() const;
};

TupleObject* tuple_new(uint64_t count, ExceptionBlock* exc_block = NULL);
void tuple_delete(TupleObject* t);

void* tuple_get_item(const TupleObject* l, int64_t position,
    ExceptionBlock* exc_block = NULL);
void tuple_set_item(TupleObject* l, int64_t position, void* value,
    bool is_object, ExceptionBlock* exc_block = NULL);

size_t tuple_size(const TupleObject* d);
