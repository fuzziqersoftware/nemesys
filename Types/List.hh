#pragma once

#include <stdint.h>

#include <atomic>
#include <string>
#include <vector>

#include "../Exception.hh"
#include "Reference.hh"


struct ListObject {
  BasicObject basic;
  uint64_t count;
  uint64_t capacity;
  bool items_are_objects;
  void** items;
};

ListObject* list_new(uint64_t count, bool items_are_objects,
    ExceptionBlock* exc_block = NULL);
void list_delete(ListObject* l);

void* list_get_item(const ListObject* l, int64_t position,
    ExceptionBlock* exc_block = NULL);
void list_set_item(ListObject* l, int64_t position, void* value,
    ExceptionBlock* exc_block = NULL);
void list_insert(ListObject* l, int64_t position, void* value,
    ExceptionBlock* exc_block = NULL);
void list_append(ListObject* l, void* value, ExceptionBlock* exc_block = NULL);
void* list_pop(ListObject* l, int64_t position, ExceptionBlock* exc_block);
void list_clear(ListObject* l);

size_t list_size(const ListObject* d);
